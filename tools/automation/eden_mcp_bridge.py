"""Launch an externally installed Eden MCP server with owned-process evidence.

The repository tool uses only the standard library. The optional emulator and
MCP dependencies belong to the caller's explicitly selected Python runtime.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import runpy
import site
import subprocess
import sys
import threading
from datetime import datetime, timezone


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--native-dir", required=True, type=Path)
    args = parser.parse_args()
    if args.site:
        sys.path.insert(0, str(args.site.resolve()))
        site.addsitedir(str(args.site.resolve()))
    os.environ["EDEN_CONFIG_FILE"] = str(args.config.resolve())
    lock = threading.Lock()

    def record(**values: object) -> None:
        with lock, args.trace.open("a", encoding="utf-8") as output:
            output.write(json.dumps({"at": datetime.now(timezone.utc).isoformat(), **values}) + "\n")

    original = subprocess.Popen

    class OwnedProcess(original):
        def __init__(self, *values: object, **options: object) -> None:
            self.capture = (args.native_dir / f"native-{len(list(args.native_dir.glob('native-*'))):03d}.log").open("xb")
            options["stdout"] = self.capture
            options["stderr"] = subprocess.STDOUT
            super().__init__(*values, **options)
            record(event="spawn", pid=self.pid, executable=self.args[0])

        def wait(self, *values: object, **options: object) -> int:
            code = super().wait(*values, **options)
            record(event="wait", pid=self.pid, return_code=code)
            self.capture.close()
            return code

        def terminate(self) -> None:
            record(event="terminate", pid=self.pid)
            return super().terminate()

        def kill(self) -> None:
            record(event="kill", pid=self.pid)
            return super().kill()

    subprocess.Popen = OwnedProcess
    runpy.run_module("eden_mcp.server", run_name="__main__")


if __name__ == "__main__":
    main()
