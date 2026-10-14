#!/usr/bin/env python3
"""Bounded, audited controller input for an explicitly authorized test console.

Uses upstream 2.5 press/release, not the optional clickSeq extension. Input
commands have no acknowledgement: socket success never proves a UI outcome.
"""
from __future__ import annotations

from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import html
import json
from pathlib import Path
import time
from typing import Callable, Iterator

from sys_botbase_client import BUTTONS, SysBotBaseClient


class BoundedInputBatch:
    def __init__(self, client: SysBotBaseClient, out: Path, *,
                 health: Callable[[], None], duration_s: int = 120,
                 max_actions: int = 24, clock: Callable[[], float] = time.monotonic,
                 sleep: Callable[[float], None] = time.sleep) -> None:
        if not 1 <= duration_s <= 600 or not 1 <= max_actions <= 100:
            raise ValueError("batch bounds outside supported range")
        out.mkdir(parents=True, exist_ok=False)
        self.client, self.out, self.health = client, out, health
        self.clock, self.sleep = clock, sleep
        self.deadline = clock() + duration_s
        self.max_actions, self.actions = max_actions, 0
        self.held: list[str] = []
        self.input_started = self.failed = self.closed = self.private = False
        self.frames: list[dict] = []
        self.event("batch_started", duration_s=duration_s, max_actions=max_actions)

    def event(self, kind: str, **fields: object) -> None:
        value = {"kind": kind, "host_utc": datetime.now(timezone.utc).isoformat(),
                 "host_monotonic": self.clock(), **fields}
        with (self.out / "events.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(value, ensure_ascii=False) + "\n")

    def guard(self) -> None:
        if self.failed or self.closed:
            raise RuntimeError("batch stopped; do not replay input")
        if self.clock() >= self.deadline:
            self.failed = True
            self.event("batch_stopped", reason="deadline")
            raise TimeoutError("batch deadline reached")
        try:
            self.health()
        except Exception:
            self.failed = True
            self.event("batch_stopped", reason="health_check_failed")
            raise

    def wait(self, seconds: float) -> None:
        self.guard()
        if seconds < 0 or self.clock() + seconds >= self.deadline:
            self.failed = True
            self.event("batch_stopped", reason="wait_exceeds_deadline")
            raise TimeoutError("wait exceeds batch deadline")
        self.sleep(seconds)
        self.guard()

    def hold(self, buttons: list[str], *, hold_ms: int = 120,
             settle_s: float = .35) -> None:
        if not buttons or len(set(buttons)) != len(buttons) or any(b not in BUTTONS for b in buttons):
            raise ValueError("invalid controller buttons")
        if not 50 <= hold_ms <= 3000 or not 0 <= settle_s <= 10:
            raise ValueError("invalid input timing")
        self.guard()
        if self.actions >= self.max_actions:
            self.failed = True
            self.event("batch_stopped", reason="action_limit")
            raise RuntimeError("batch action limit reached")
        # Reserve conservative wire time before changing the controller state.
        if self.clock() + hold_ms / 1000 + settle_s + len(buttons) * .2 >= self.deadline:
            self.failed = True
            self.event("batch_stopped", reason="input_exceeds_deadline")
            raise TimeoutError("input exceeds batch deadline")
        self.actions += 1
        if not self.private:
            self.event("input_started", buttons=buttons, hold_ms=hold_ms)
        try:
            for button in buttons:
                self.input_started = True
                # Include an ambiguous failed send in the release set.
                self.held.append(button)
                self.client.press(button)
                self.sleep(.1)
            self.sleep(hold_ms / 1000)
            for button in reversed(buttons):
                self.client.release(button)
                self.held.remove(button)
                self.sleep(.1)
            # A query is a wire barrier, not an input/UI acknowledgement.
            if not self.client.get_system_language():
                raise RuntimeError("empty sys-botbase barrier")
            self.wait(settle_s)
            if not self.private:
                self.event("input_sent", acknowledgement="none")
        except Exception:
            self.failed = True
            self.event("batch_stopped", reason="input_or_health_failed")
            raise

    @contextmanager
    def sensitive_input(self) -> Iterator[None]:
        """Suppress button/timing details for secrets; never log exception text."""
        if self.private:
            raise RuntimeError("nested sensitive input")
        self.guard()
        self.private = True
        self.event("sensitive_input_started")
        try:
            yield
            self.event("sensitive_input_finished")
        finally:
            self.private = False

    def capture(self, stage: str) -> Path:
        self.guard()
        if not stage or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in stage):
            raise ValueError("invalid screenshot stage")
        filename = f"{len(self.frames):03d}-{stage}.jpg"
        path = self.out / filename
        try:
            self.client.capture_screenshot(path)
            frame = {"stage": stage, "path": filename,
                     "host_utc": datetime.now(timezone.utc).isoformat(),
                     "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                     "manual_observation": None}
            self.frames.append(frame)
            self.event("screenshot", **frame)
            self.gallery()
            return path
        except Exception:
            self.failed = True
            self.event("batch_stopped", reason="capture_failed")
            raise

    def gallery(self) -> None:
        cards = "".join(
            f'<figure><h2>{html.escape(f["stage"])}</h2><p>{html.escape(f["host_utc"])}</p>'
            f'<a href="{f["path"]}"><img src="{f["path"]}"></a>'
            f'<p>SHA-256: {f["sha256"]}</p></figure>' for f in self.frames)
        (self.out / "gallery.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>PlayWise 画面集中复核</title>'
            '<style>body{background:#f0f3fa;color:#172640;font:16px system-ui;margin:24px}'
            'figure{background:#fff;padding:16px;margin:16px 0}img{max-width:100%}'
            'p{overflow-wrap:anywhere}@media(prefers-color-scheme:dark){'
            'body{background:#0b0f19;color:#f3f6fd}figure{background:#162032}}</style>'
            '<h1>PlayWise 画面集中复核</h1><p>机器采集与导航记录；人工观察待独立填写。</p>' + cards,
            encoding="utf-8")

    def close(self) -> None:
        if self.closed:
            return
        # Do not initialize hiddbg for a batch that never sent input. Do not
        # reconnect/reinitialize on a lost socket or fatal: record unknown.
        cleanup = "not_needed"
        if self.input_started:
            try:
                self.health()
                for button in reversed(self.held[:]):
                    self.client.release(button)
                    self.held.remove(button)
                self.client.raw("detachController")
                if not self.client.get_system_language():
                    raise RuntimeError("empty cleanup barrier")
                self.health()
                cleanup = "detach_sent_barrier_passed"
            except Exception:
                self.failed = True
                cleanup = "unknown_stop_device_operations"
        self.closed = True
        self.event("batch_closed", failed=self.failed, cleanup=cleanup)
        (self.out / "summary.json").write_text(json.dumps({
            "failed": self.failed, "cleanup": cleanup, "qualified": False,
            "manual_observations": None, "frames": self.frames,
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def __enter__(self) -> "BoundedInputBatch":
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        if exc_type is not None:
            self.failed = True
        self.close()
