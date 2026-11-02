from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    executable = Path(sys.argv[1]).resolve(strict=True)
    root = Path(__file__).resolve().parents[2] / "build" / "host"
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="online-package-", dir=root) as directory:
        return subprocess.run([str(executable), Path(directory).as_posix()], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
