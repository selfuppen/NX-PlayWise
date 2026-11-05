#!/usr/bin/env python3
from __future__ import annotations

from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.build.sync_gitee_releases import (
    GITEE_REPO, GITHUB_REPO, MARKER, Api, Git, SafeRedirect, SyncError,
    candidates, compare, hashes, json_bytes, preview, public_url, published_calendars, sync, version_parts,
)


class FakeApi:
    def __init__(self):
        self.releases: list[dict] = []
        self.mirrors: list[dict] = []
        self.assets: dict[int, list[dict]] = {}
        self.attachments: dict[int, list[dict]] = {}
        self.bytes: dict[str, bytes] = {}
        self.uploads = self.downloads = self.deletes = 0
        self.fail_upload = False
        self.corrupt_upload = False

    def release(self, tag: str, pre: bool = False, *, draft: bool = False) -> dict:
        r = {"tag_name": tag, "prerelease": pre, "draft": draft, "id": len(self.releases) + 1, "name": tag, "body": "说明\n" + "🙂" * 1100}
        self.releases.append(r)
        self.assets[r["id"]] = []
        self.asset(r, f"playwise-{tag[1:]}.zip", b"standard")
        self.asset(r, f"playwise-complete-{tag[1:]}.zip", b"complete")
        return r

    def asset(self, release: dict, name: str, data: bytes) -> None:
        assets = self.assets[release["id"]]
        assets.append({"id": len(assets) + 1, "name": name, "size": len(data), "state": "uploaded", "updated_at": "2026-11-03T00:00:00Z",
                       "digest": "sha256:" + hashlib.sha256(data).hexdigest()})
        self.bytes[public_url("github", release["tag_name"], name)] = data

    def pages(self, provider: str, path: str) -> list[dict]:
        if path.endswith("/assets"):
            return deepcopy(self.assets[int(path.split("/")[-2])])
        if path.endswith("/attach_files"):
            return deepcopy(self.attachments[int(path.split("/")[-2])])
        return deepcopy(self.releases if provider == "github" else self.mirrors)

    def request(self, provider: str, path: str, method: str = "GET", fields: dict | None = None,
                file: Path | None = None, filename: str = "") -> dict | None:
        assert provider == "gitee"
        if path.endswith("/attach_files"):
            if self.fail_upload:
                raise SyncError("injected upload failure")
            self.uploads += 1
            rid = int(path.split("/")[-2])
            release = next(r for r in self.mirrors if r["id"] == rid)
            data = file.read_bytes()
            attached = {"name": filename, "id": 100 + self.uploads, "size": len(data)}
            self.attachments[rid].append(attached)
            self.bytes[public_url("gitee", release["tag_name"], filename)] = b"corrupt" if self.corrupt_upload else data
            return attached
        if method == "DELETE":
            rid, aid = int(path.split("/")[-3]), int(path.split("/")[-1])
            self.attachments[rid] = [a for a in self.attachments[rid] if a["id"] != aid]
            self.deletes += 1
            return None
        if method == "PATCH":
            release = next(r for r in self.mirrors if r["id"] == int(path.split("/")[-1]))
            release.update(fields)
            return deepcopy(release)
        release = {**fields, "id": 1000 + len(self.mirrors)}
        self.mirrors.append(release)
        self.attachments[release["id"]] = []
        return deepcopy(release)

    def download(self, url: str, destination: Path) -> None:
        self.downloads += 1
        with destination.open("xb") as out:
            out.write(self.bytes[url])


class FakeGit:
    def __init__(self):
        self.state: dict = {"schema_version": 1, "releases": {}}
        self.feed: dict | None = None
        self.publications = 0
        self.tags: list[str] = []

    def load(self) -> dict:
        return deepcopy(self.state)

    def tag(self, tag: str) -> None:
        self.tags.append(tag)

    def publish(self, feed: dict, state: dict) -> None:
        # Assert both JSON serialization round trips before accepting publication.
        self.state = json.loads(json_bytes(state))
        self.feed = json.loads(json_bytes(feed))
        assert state == self.state and feed == self.feed
        self.publications += 1


class MirrorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.api, self.git = FakeApi(), FakeGit()

    def run_sync(self, *, tag: str = "", calendars: list[dict] | None = None) -> dict:
        with tempfile.TemporaryDirectory() as directory:
            return sync(self.api, self.git, Path(directory), tag=tag, calendars=calendars)

    def test_calendar_publication_without_new_software(self) -> None:
        calendars = published_calendars(ROOT / "data" / "holiday-calendars")
        current = next(calendar for calendar in calendars if calendar["year"] == 2027)
        self.assertEqual(len(current["holidays"]), 7)
        self.assertEqual(current["workdays"], [
            "2027-01-31", "2027-02-14", "2027-05-08", "2027-09-26", "2027-10-09",
        ])
        feed = self.run_sync(calendars=calendars)
        self.assertEqual(feed["releases"], [])
        self.assertEqual(feed["holiday_calendars"], calendars)
        with tempfile.TemporaryDirectory() as directory:
            invalid = {**current, "workdays": ["2027-02-05"]}
            path = Path(directory) / "CN-2027.json"
            path.write_bytes(json_bytes(invalid))
            with self.assertRaisesRegex(SyncError, "Invalid published holiday calendar"):
                published_calendars(path.parent)

    def test_bootstrap_repeat_and_late_attachment(self) -> None:
        self.api.release("v1.0.0")
        stable = self.api.release("v2.0.0")
        beta = self.api.release("v3.0.0-beta.1", False)  # Historical incorrect flag.
        self.api.release("v4.0.0", draft=True)
        feed = self.run_sync()
        self.assertEqual({r["tag"] for r in feed["releases"]}, {stable["tag_name"], beta["tag_name"]})
        self.assertTrue(next(r for r in feed["releases"] if "beta" in r["version"])["prerelease"])
        self.assertEqual(self.api.uploads, 4)
        self.assertTrue(all(len(r["notes"].encode()) < 2048 for r in feed["releases"]))
        downloads = self.api.downloads
        self.assertEqual(self.run_sync(), feed)
        self.assertEqual(self.api.downloads, downloads)
        self.api.asset(beta, "empty-extra.txt", b"")
        self.assertEqual(self.run_sync(), feed)
        self.assertEqual(self.api.uploads, 5)
        self.assertEqual(self.api.deletes, 0)

    def test_new_nonlatest_release_and_explicit_history(self) -> None:
        old = self.api.release("v1.0.0")
        self.api.release("v5.0.0")
        self.run_sync()
        self.api.release("v2.0.0")  # Must not lose a release whose event was coalesced.
        self.run_sync()
        self.assertIn("v2.0.0", self.git.state["releases"])
        self.run_sync(tag=old["tag_name"])
        self.assertIn(old["tag_name"], self.git.state["releases"])

    def test_partial_or_bad_upload_never_publishes(self) -> None:
        self.api.release("v1.0.0")
        before = self.run_sync()
        self.api.release("v2.0.0")
        for flag in ("fail_upload", "corrupt_upload"):
            setattr(self.api, flag, True)
            with self.assertRaises(SyncError):
                self.run_sync()
            self.assertEqual(before, self.git.feed)
            self.assertEqual(self.git.publications, 1)
            setattr(self.api, flag, False)

    def test_owned_replacement_and_unknown_conflict(self) -> None:
        r = self.api.release("v1.0.0")
        self.run_sync()
        name = self.api.assets[r["id"]][0]["name"]
        self.api.assets[r["id"]].pop(0)
        self.api.asset(r, name, b"replacement")
        self.run_sync()
        self.assertEqual(self.api.deletes, 1)
        self.api.assets[r["id"]].pop()
        self.api.asset(r, name, b"second replacement")
        mirrored = self.api.mirrors[0]
        next(a for a in self.api.attachments[mirrored["id"]] if a["name"] == name)["id"] = 9999
        with self.assertRaisesRegex(SyncError, "not owned"):
            self.run_sync()
        self.assertEqual(self.api.deletes, 1)

    def test_unowned_release_rejected_and_withdrawn_feed_removed(self) -> None:
        r = self.api.release("v1.0.0")
        self.api.mirrors.append({"tag_name": r["tag_name"], "id": 7, "body": "manual"})
        with self.assertRaisesRegex(SyncError, "not owned"):
            self.run_sync()
        self.api.mirrors.clear()
        self.run_sync()
        r["draft"] = True
        self.assertEqual(self.run_sync()["releases"], [])
        self.assertEqual(len(self.api.mirrors), 1)

    def test_no_standard_zip_and_digest_mismatch(self) -> None:
        r = self.api.release("v1.0.0")
        self.api.assets[r["id"]][0]["digest"] = "sha256:" + "0" * 64
        with self.assertRaisesRegex(SyncError, "GitHub attachment digest"):
            self.run_sync()
        self.assertIsNone(self.git.feed)
        self.api.assets[r["id"]].pop(0)
        with self.assertRaisesRegex(SyncError, "standard ZIP"):
            self.run_sync()
        self.assertIsNone(self.git.feed)

    def test_semver_boundaries(self) -> None:
        ordered = ["1.0.0-alpha", "1.0.0-alpha.2", "1.0.0-alpha.10", "1.0.0-beta", "1.0.0", "1.1.0-alpha"]
        for i, a in enumerate(ordered):
            for j, b in enumerate(ordered):
                self.assertEqual(compare(a, b), (i > j) - (i < j))
        self.assertEqual(compare("1.0.0+a", "1.0.0+b"), 0)
        for bad in ("01.0.0", "1.0", "1.0.0-a..b", "1.0.0-01", "4294967296.0.0"):
            with self.assertRaises(SyncError):
                version_parts(bad)

    def test_anonymous_redirect_boundaries(self) -> None:
        handler = SafeRedirect()
        request = urllib.request.Request(public_url("github", "v1.0.0", "playwise-1.0.0.zip"))
        self.assertIsNotNone(handler.redirect_request(request, None, 302, "", {}, "https://release-assets.githubusercontent.com/github-production-release-asset/1/x?a=b"))
        for url in ("http://github.com/x", "https://evil.test/x", "https://gitee.com/x", "https://user@github.com/x"):
            with self.assertRaises(SyncError):
                handler.redirect_request(request, None, 302, "", {}, url)
        request.add_header("Authorization", "Bearer secret")
        with self.assertRaises(SyncError):
            handler.redirect_request(request, None, 302, "", {}, "https://github.com/x")
        first = "https://gitee.com/selfuppen/nx-playwise/attach_files/3580057/download/playwise-3.0.2.zip"
        cdn = "https://foruda.gitee.com/attach_file/1793690895109930364/playwise-3.0.2.zip?token=test&ts=1"
        gitee_request = urllib.request.Request(public_url("gitee", "v3.0.2", "playwise-3.0.2.zip"))
        next_request = handler.redirect_request(gitee_request, None, 302, "", {}, first)
        self.assertEqual(handler.redirect_request(next_request, None, 302, "", {}, cdn).full_url, cdn)
        for url in ("https://foruda.gitee.com.evil/attach_file/1/a.zip", "https://foruda.gitee.com/other/1/a.zip",
                    "https://foruda.gitee.com:444/attach_file/1/a.zip", "https://github.com/x"):
            with self.assertRaises(SyncError):
                handler.redirect_request(next_request, None, 302, "", {}, url)
        for blocked in (urllib.request.Request(first, headers={"Authorization": "Bearer secret"}),
                        urllib.request.Request(first, data=b"access_token=secret", method="POST")):
            with self.assertRaises(SyncError):
                handler.redirect_request(blocked, None, 302, "", {}, cdn)

    def test_real_api_upload_form_and_error_redaction(self) -> None:
        api = Api("github-secret", "gitee-secret")
        requests = []
        timeouts = []
        class Opener:
            def open(self, request: urllib.request.Request, timeout: int) -> io.BytesIO:
                requests.append(request)
                timeouts.append(timeout)
                return io.BytesIO(b'{"id":7,"size":3}')
        api.opener = Opener()
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "file"
            file.write_bytes(b"zip")
            result = api.request("gitee", "/repos/selfuppen/nx-playwise/releases/1/attach_files", "POST", file=file, filename="playwise.zip")
            self.assertEqual(result["id"], 7)
            request = requests[-1]
            self.assertNotIn("secret", request.full_url)
            self.assertIn(b'name="access_token"\r\n\r\ngitee-secret', request.data)
            self.assertIn(b'filename="playwise.zip"', request.data)
            self.assertIn(b"\r\n\r\nzip\r\n", request.data)
            self.assertIsNone(request.get_header("Authorization"))
            self.assertEqual(timeouts[-1], 600)
            api.request("github", "/repos/selfuppen/NX-PlayWise/releases")
            self.assertEqual(requests[-1].get_header("Authorization"), "Bearer github-secret")
            self.assertEqual(timeouts[-1], 180)
        class Denied:
            def open(self, request: urllib.request.Request, timeout: int) -> None:
                raise urllib.error.HTTPError(request.full_url, 403, "gitee-secret", {}, None)
        api.opener = Denied()
        with self.assertRaises(SyncError) as error:
            api.request("gitee", "/test", "POST", {"name": "test"})
        self.assertIn("403", str(error.exception))
        self.assertNotIn("secret", str(error.exception))

    def test_api_failure_categories_never_echo_credentials(self) -> None:
        api = Api("github-secret", "gitee-secret")
        for failure, category in ((TimeoutError("gitee-secret"), "timeout"),
                                  (urllib.error.URLError(TimeoutError("gitee-secret")), "network TimeoutError")):
            class Failed:
                def open(self, request, timeout):
                    raise failure
            api.opener = Failed()
            with self.assertRaises(SyncError) as error:
                api.request("gitee", "/test", "POST", {"name":"x"})
            self.assertIn(category, str(error.exception))
            self.assertNotIn("secret", str(error.exception))
        class Invalid:
            def open(self, request, timeout):
                return io.BytesIO(b"gitee-secret")
        api.opener = Invalid()
        with self.assertRaisesRegex(SyncError, "invalid JSON response") as error:
            api.request("gitee", "/test", "POST", {"name":"x"})
        self.assertNotIn("secret", str(error.exception))

    def test_real_git_feed_roundtrip_and_partial_publish_repair(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ("github", "gitee", "work"):
                (root / name).mkdir()
            for name in ("github", "gitee"):
                subprocess.run(["git", "init", "--bare", str(root / name)], capture_output=True, check=True)
            class LocalGit(Git):
                def run(self, *args: str, data: bytes | None = None) -> bytes:
                    args = tuple(str(root / "github") if a == f"https://github.com/{GITHUB_REPO}.git" else
                                 str(root / "gitee") if a == f"https://gitee.com/{GITEE_REPO}.git" else a for a in args)
                    return super().run(*args, data=data)
            git = LocalGit(root / "work", "github-secret", "gitee-secret")
            self.assertEqual(git.load(), {"schema_version": 1, "releases": {}})
            feed, state = {"schema_version": 1, "releases": []}, {"schema_version": 1, "releases": {}}
            git.publish(feed, state)
            self.assertEqual(git.load(), state)
            head = git.parent
            git.publish(feed, state)
            self.assertEqual(git.load(), state)
            self.assertEqual(head, git.parent)
            # Gitee is missing a publication: the same verified commit repairs it.
            subprocess.run(["git", "--git-dir", str(root / "gitee"), "update-ref", "-d", "refs/heads/update-feed"], check=True)
            git.publish(feed, state)
            self.assertEqual(git.run("ls-remote", "--heads", str(root / "gitee"), "update-feed").decode().split()[0], head)


if __name__ == "__main__":
    unittest.main()
