#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import secrets
import time
from typing import Any


APP_DIR = Path("switch") / "playwise"


@dataclass(frozen=True)
class AppPaths:
    sdmc_root: Path

    @property
    def app_root(self) -> Path:
        return self.sdmc_root / APP_DIR

    @property
    def pending(self) -> Path:
        return self.app_root / "inbox" / "pending"

    @property
    def processing(self) -> Path:
        return self.app_root / "inbox" / "processing"

    @property
    def done(self) -> Path:
        return self.app_root / "inbox" / "done"

    @property
    def results(self) -> Path:
        return self.app_root / "results"

    @property
    def logs(self) -> Path:
        return self.app_root / "logs"

    @property
    def ledger(self) -> Path:
        return self.app_root / "ledger"

    @property
    def backups(self) -> Path:
        return self.app_root / "backups"

    @property
    def flags(self) -> Path:
        return self.app_root / "flags"


def new_request_id(now_ms: int | None = None) -> str:
    if now_ms is None:
        now_ms = int(time.time() * 1000)
    return f"{now_ms}-{secrets.randbelow(0x10000):04x}"


def write_json_atomic(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def create_layout(sdmc_root: Path) -> AppPaths:
    paths = AppPaths(sdmc_root)
    for directory in [
        paths.pending,
        paths.processing,
        paths.done,
        paths.results,
        paths.logs,
        paths.ledger,
        paths.backups,
        paths.flags,
    ]:
        directory.mkdir(parents=True, exist_ok=True)
    return paths


def write_request(
    sdmc_root: Path,
    request_type: str,
    payload: dict[str, Any] | None = None,
    request_id: str | None = None,
    created_at: int | None = None,
) -> str:
    paths = create_layout(sdmc_root)
    if request_id is None:
        request_id = new_request_id()
    if created_at is None:
        created_at = int(time.time())
    request = {
        "version": 1,
        "request_id": request_id,
        "type": request_type,
        "created_at": created_at,
        "payload": payload or {},
    }
    write_json_atomic(paths.pending / f"{request_id}.json", request)
    return request_id


def list_pending(paths: AppPaths) -> list[Path]:
    if not paths.pending.exists():
        return []
    return sorted(p for p in paths.pending.glob("*.json") if p.is_file())


def move_to_processing(paths: AppPaths, pending_file: Path) -> Path:
    target = paths.processing / pending_file.name
    paths.processing.mkdir(parents=True, exist_ok=True)
    pending_file.replace(target)
    return target


def archive_done(paths: AppPaths, processing_file: Path) -> Path:
    target = paths.done / processing_file.name
    paths.done.mkdir(parents=True, exist_ok=True)
    processing_file.replace(target)
    return target

