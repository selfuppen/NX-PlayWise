#!/usr/bin/env python3
"""Mirror published release bytes, then publish the two-channel update feed."""
from __future__ import annotations

import argparse
import base64
from functools import cmp_to_key
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Any
import urllib.error
import urllib.parse
import urllib.request

GITHUB_REPO = "selfuppen/NX-PlayWise"
GITEE_REPO = "selfuppen/nx-playwise"
BRANCH = "update-feed"
MARKER = "<!-- playwise-release-mirror:v1 -->"
MAX_ATTACHMENT = 100 * 1024 * 1024
MAX_ZIP = 32 * 1024 * 1024
VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?\Z")


class SyncError(RuntimeError):
    pass


def version_parts(version: str) -> tuple[tuple[int, ...], list[str]]:
    match = VERSION.fullmatch(version)
    if not match or len(version) >= 64:
        raise SyncError("release tag is not a bounded SemVer")
    core = tuple(int(match[i]) for i in range(1, 4))
    if any(v > 0xFFFFFFFF for v in core):
        raise SyncError("SemVer core overflow")
    pre = (match[4] or "").split(".") if match[4] else []
    for identifiers, numeric in ((pre, True), ((match[5] or "x").split("."), False)):
        if any(not v or (numeric and v.isdigit() and len(v) > 1 and v[0] == "0") for v in identifiers):
            raise SyncError("invalid SemVer identifier")
    return core, pre


def compare(left: str, right: str) -> int:
    lc, lp = version_parts(left)
    rc, rp = version_parts(right)
    if lc != rc:
        return (lc > rc) - (lc < rc)
    if not lp or not rp:
        return (not lp) - (not rp)
    for a, b in zip(lp, rp):
        if a == b:
            continue
        if a.isdigit() and b.isdigit():
            return (int(a) > int(b)) - (int(a) < int(b))
        if a.isdigit() != b.isdigit():
            return -1 if a.isdigit() else 1
        return (a > b) - (a < b)
    return (len(lp) > len(rp)) - (len(lp) < len(rp))


def release_version(release: dict) -> str:
    tag = release.get("tag_name", "")
    if not isinstance(tag, str) or not tag.startswith("v"):
        raise SyncError("release tag must start with v")
    version_parts(tag[1:])
    return tag[1:]


def preview(release: dict) -> bool:
    return bool(release.get("prerelease")) or bool(version_parts(release_version(release))[1])


def candidates(releases: list[dict]) -> list[dict]:
    valid = []
    for release in releases:
        if release.get("draft"):
            continue
        try:
            release_version(release)
        except SyncError:
            continue
        valid.append(release)
    return sorted(valid, key=cmp_to_key(lambda a, b: compare(release_version(b), release_version(a))))


def json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def hashes(path: Path) -> dict:
    sha, md5 = hashlib.sha256(), hashlib.md5()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
            md5.update(block)
    return {"size": path.stat().st_size, "sha256": sha.hexdigest(), "md5": md5.hexdigest()}


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req: Any, fp: Any, code: int, msg: str, headers: Any, newurl: str) -> Any:
        old, new = urllib.parse.urlsplit(req.full_url), urllib.parse.urlsplit(newurl)
        # API credentials never follow a redirect. Downloads are anonymous.
        allowed = {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"} if old.hostname in {
            "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"} else {"gitee.com", "raw.giteeusercontent.com"}
        if req.get_method() != "GET" or req.has_header("Authorization") or new.scheme != "https" or new.hostname not in allowed or new.username or new.password:
            raise SyncError("unexpected network redirect")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class Api:
    def __init__(self, github_token: str, gitee_token: str):
        self.github_token, self.gitee_token = github_token, gitee_token
        self.opener = urllib.request.build_opener(SafeRedirect())

    def request(self, provider: str, path: str, method: str = "GET", fields: dict | None = None,
                file: Path | None = None, filename: str = "") -> Any:
        host = "https://api.github.com" if provider == "github" else "https://gitee.com/api/v5"
        headers = {"User-Agent": "PlayWise-release-mirror/1", "Accept": "application/json"}
        data = None
        if provider == "github":
            headers["Authorization"] = "Bearer " + self.github_token
        if method != "GET":
            values = dict(fields or {})
            if provider == "gitee":
                values["access_token"] = self.gitee_token
            if file:
                boundary = "playwise-" + os.urandom(16).hex()
                parts = []
                for key, value in values.items():
                    parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode())
                safe_name = filename.replace('"', "_").replace("\r", "_").replace("\n", "_")
                parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{safe_name}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode())
                data = b"".join(parts) + file.read_bytes() + f"\r\n--{boundary}--\r\n".encode()
                headers["Content-Type"] = "multipart/form-data; boundary=" + boundary
            else:
                data = urllib.parse.urlencode({k: str(v).lower() if isinstance(v, bool) else v for k, v in values.items()}).encode()
                headers["Content-Type"] = "application/x-www-form-urlencoded"
        req = urllib.request.Request(host + path, data=data, headers=headers, method=method)
        try:
            with self.opener.open(req, timeout=180) as response:
                raw = response.read(8 * 1024 * 1024 + 1)
                if len(raw) > 8 * 1024 * 1024:
                    raise SyncError("API response exceeds limit")
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as exc:
            # Do not print request bodies, URLs with credentials, or server echoes.
            raise SyncError(f"{provider} API {method} failed: HTTP {exc.code}") from None
        except (urllib.error.URLError, TimeoutError, ValueError):
            raise SyncError(f"{provider} API {method} failed") from None

    def pages(self, provider: str, path: str) -> list[dict]:
        result = []
        for page in range(1, 101):
            rows = self.request(provider, path + f"?per_page=100&page={page}")
            if not isinstance(rows, list):
                raise SyncError("invalid paginated API response")
            result.extend(rows)
            if len(rows) < 100:
                return result
        raise SyncError("pagination limit reached")

    def download(self, url: str, destination: Path) -> None:
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != "https" or parsed.hostname not in {"github.com", "gitee.com"} or parsed.username or parsed.password:
            raise SyncError("invalid public attachment URL")
        try:
            with self.opener.open(urllib.request.Request(url, headers={"User-Agent": "PlayWise-release-mirror/1"}), timeout=180) as response, destination.open("xb") as out:
                size = 0
                for block in iter(lambda: response.read(1024 * 1024), b""):
                    size += len(block)
                    if size > MAX_ATTACHMENT:
                        raise SyncError("attachment exceeds Gitee 100 MiB limit")
                    out.write(block)
        except (urllib.error.URLError, TimeoutError):
            raise SyncError("anonymous attachment download failed") from None


class Git:
    def __init__(self, directory: Path, github_token: str, gitee_token: str):
        self.directory = directory
        self.env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_TRACE")}
        self.env.update(GIT_TERMINAL_PROMPT="0", GIT_CONFIG_COUNT="2", GIT_AUTHOR_NAME="PlayWise release mirror",
                        GIT_AUTHOR_EMAIL="actions@users.noreply.github.com", GIT_COMMITTER_NAME="PlayWise release mirror",
                        GIT_COMMITTER_EMAIL="actions@users.noreply.github.com")
        for i, (host, user, token) in enumerate((("github.com", "x-access-token", github_token), ("gitee.com", "selfuppen", gitee_token))):
            self.env[f"GIT_CONFIG_KEY_{i}"] = f"http.https://{host}/.extraheader"
            self.env[f"GIT_CONFIG_VALUE_{i}"] = "Authorization: Basic " + base64.b64encode(f"{user}:{token}".encode()).decode()
        self.run("init", "--bare", ".")
        self.parent = ""

    def run(self, *args: str, data: bytes | None = None) -> bytes:
        result = subprocess.run(["git", *args], cwd=self.directory, env=self.env, input=data, capture_output=True)
        if result.returncode:
            raise SyncError("git operation failed: " + args[0])
        return result.stdout

    def load(self) -> dict:
        remote = f"https://github.com/{GITHUB_REPO}.git"
        if not self.run("ls-remote", "--heads", remote, BRANCH).strip():
            return {"schema_version": 1, "releases": {}}
        self.run("fetch", "--no-tags", remote, f"refs/heads/{BRANCH}")
        self.parent = self.run("rev-parse", "FETCH_HEAD").decode().strip()
        value = json.loads(self.run("show", f"{self.parent}:mirror-state.json"))
        if value.get("schema_version") != 1 or not isinstance(value.get("releases"), dict):
            raise SyncError("invalid mirror ownership state")
        return value

    def tag(self, tag: str) -> None:
        ref = "refs/tags/" + tag
        self.run("fetch", "--no-tags", f"https://github.com/{GITHUB_REPO}.git", f"{ref}:{ref}")
        # Never force-move an existing tag.
        self.run("push", f"https://gitee.com/{GITEE_REPO}.git", f"{ref}:{ref}")

    def publish(self, feed: dict, state: dict) -> None:
        files = {"updates.json": json_bytes(feed), "mirror-state.json": json_bytes(state)}
        if self.parent and all(self.run("show", f"{self.parent}:{name}") == data for name, data in files.items()):
            # Repair a previous partial two-provider publication as well.
            commit = self.parent
        else:
            tree = b""
            for name, data in sorted(files.items()):
                blob = self.run("hash-object", "-w", "--stdin", data=data).strip()
                tree += b"100644 blob " + blob + b"\t" + name.encode() + b"\n"
            tree_id = self.run("mktree", data=tree).decode().strip()
            parent = ["-p", self.parent] if self.parent else []
            commit = self.run("commit-tree", tree_id, *parent, data=b"Publish verified PlayWise update feed\n").decode().strip()
        # GitHub holds durable ownership state first. Gitee can be repaired on rerun.
        for repo in (f"https://github.com/{GITHUB_REPO}.git", f"https://gitee.com/{GITEE_REPO}.git"):
            self.run("push", repo, f"{commit}:refs/heads/{BRANCH}")


def public_url(provider: str, tag: str, name: str) -> str:
    repo = GITHUB_REPO if provider == "github" else GITEE_REPO
    return f"https://{provider}.com/{repo}/releases/download/{urllib.parse.quote(tag, safe='+')}/{urllib.parse.quote(name, safe='+')}"


def sync(api: Api, git: Git, directory: Path, *, tag: str = "") -> dict:
    state = git.load()
    published = candidates(api.pages("github", f"/repos/{GITHUB_REPO}/releases"))
    by_tag = {r["tag_name"]: r for r in published}
    latest: dict[bool, dict] = {}
    for release in published:
        latest.setdefault(preview(release), release)
    if tag and tag not in by_tag:
        raise SyncError("requested tag has no published SemVer release")
    selected = {r["tag_name"] for r in latest.values()} | (set(state["releases"]) & set(by_tag))
    if "seen_release_ids" in state:
        seen = set(state["seen_release_ids"])
        selected.update(r["tag_name"] for r in published if r["id"] not in seen)
    if tag:
        selected.add(tag)
    mirrors = {r["tag_name"]: r for r in api.pages("gitee", f"/repos/{GITEE_REPO}/releases")}
    for index, release_tag in enumerate(sorted(selected)):
        release = by_tag[release_tag]
        assets = api.pages("github", f"/repos/{GITHUB_REPO}/releases/{release['id']}/assets")
        names = [a["name"] for a in assets]
        if len(names) != len(set(names)) or any(not n or "/" in n or "\\" in n or any(ord(c) < 32 for c in n) for n in names):
            raise SyncError("invalid or duplicate release attachment name")
        git.tag(release_tag)
        mirror = mirrors.get(release_tag)
        old = state["releases"].get(release_tag, {})
        body = (release.get("body") or "") + "\n\n" + MARKER
        fields = {"tag_name": release_tag, "target_commitish": release_tag, "name": release.get("name") or release_tag,
                  "body": body, "prerelease": preview(release)}
        if mirror:
            if MARKER not in (mirror.get("body") or "") and mirror["id"] != old.get("id"):
                raise SyncError("existing Gitee release is not owned by this mirror")
            mirror = api.request("gitee", f"/repos/{GITEE_REPO}/releases/{mirror['id']}", "PATCH", fields)
        else:
            mirror = api.request("gitee", f"/repos/{GITEE_REPO}/releases", "POST", fields)
        path = f"/repos/{GITEE_REPO}/releases/{mirror['id']}/attach_files"
        attachments = api.pages("gitee", path)
        owned = {"id": mirror["id"], "assets": {}}
        for number, asset in enumerate(assets):
            name = asset["name"]
            if asset.get("state") != "uploaded" or type(asset.get("size")) is not int or not 0 <= asset["size"] <= MAX_ATTACHMENT:
                raise SyncError("release attachment is incomplete or exceeds capacity")
            existing = [a for a in attachments if a["name"] == name]
            if len(existing) > 1:
                raise SyncError("duplicate Gitee attachment")
            cached = old.get("assets", {}).get(name, {})
            identity = {"github_id": asset["id"], "updated_at": asset["updated_at"], "digest": asset.get("digest")}
            if (existing and all(cached.get(k) == v for k, v in identity.items()) and cached.get("gitee_id") == existing[0]["id"]
                    and cached.get("size") == existing[0]["size"] == asset["size"] and re.fullmatch("[0-9a-f]{64}", cached.get("sha256", ""))):
                owned["assets"][name] = cached
                continue
            source = directory / f"source-{index}-{number}"
            api.download(public_url("github", release_tag, name), source)
            digest = hashes(source)
            if digest["size"] != asset["size"] or (asset.get("digest") and asset["digest"] != "sha256:" + digest["sha256"]):
                raise SyncError("GitHub attachment digest mismatch")
            if existing:
                target = directory / f"existing-{index}-{number}"
                api.download(public_url("gitee", release_tag, name), target)
                if hashes(target) != digest:
                    if cached.get("gitee_id") != existing[0]["id"] or old.get("id") != mirror["id"]:
                        raise SyncError("conflicting Gitee attachment is not owned by this mirror")
                    api.request("gitee", path + f"/{existing[0]['id']}", "DELETE")
                    existing = []
            if not existing:
                attached = api.request("gitee", path, "POST", file=source, filename=name)
                target = directory / f"uploaded-{index}-{number}"
                api.download(public_url("gitee", release_tag, name), target)
                if hashes(target) != digest or attached.get("size") != digest["size"]:
                    raise SyncError("Gitee anonymous download digest mismatch")
            else:
                attached = existing[0]
            owned["assets"][name] = {**identity, **digest, "gitee_id": attached["id"]}
        state["releases"][release_tag] = owned
    feed: dict = {"schema_version": 1, "releases": []}
    for release in latest.values():
        version = release_version(release)
        name = f"playwise-{version}.zip"
        package = state["releases"][release["tag_name"]]["assets"].get(name)
        if not package or not 0 < package["size"] <= MAX_ZIP:
            raise SyncError("latest release has no compatible standard ZIP")
        notes = "".join(c for c in (release.get("body") or "") if ord(c) >= 32 or c == "\n")
        notes = notes.encode("utf-8")[:2047].decode("utf-8", errors="ignore")
        feed["releases"].append({"version": version, "tag": release["tag_name"], "prerelease": preview(release), "notes": notes,
            "package": {"name": name, **{k: package[k] for k in ("size", "sha256", "md5")},
                        "gitee_url": public_url("gitee", release["tag_name"], name),
                        "github_url": public_url("github", release["tag_name"], name)}})
    state["seen_release_ids"] = sorted(r["id"] for r in published)
    git.publish(feed, state)
    return feed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", default="", help="Also synchronize this published tag; default repairs both channels and known mirrors")
    args = parser.parse_args()
    github_token, gitee_token = os.environ.get("GH_TOKEN", ""), os.environ.get("GITEE_TOKEN", "")
    if not github_token or not gitee_token:
        parser.error("GH_TOKEN and GITEE_TOKEN must be set")
    try:
        with tempfile.TemporaryDirectory(prefix="playwise-release-mirror-") as temp:
            root = Path(temp)
            repo = root / "git"
            repo.mkdir()
            feed = sync(Api(github_token, gitee_token), Git(repo, github_token, gitee_token), root, tag=args.tag)
            print(f"Verified and published {len(feed['releases'])} update channels")
    except (SyncError, OSError, KeyError, TypeError, ValueError) as exc:
        # Untrusted API responses and exceptions may contain credential echoes.
        print(str(exc) if isinstance(exc, SyncError) else "Release mirror failed: invalid response or local I/O", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
