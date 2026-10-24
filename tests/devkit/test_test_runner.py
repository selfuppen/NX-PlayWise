from __future__ import annotations

import os
from pathlib import Path
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.test import CommandError, run


def main() -> None:
    # A legacy caller environment must not make a successful UTF-8 report
    # unreadable, and this command-local override must not mutate the caller.
    with patch.dict(os.environ, {"PYTHONUTF8": "0", "PYTHONIOENCODING": "cp936"}):
        result = run([sys.executable, '-c', 'print("中文回归")'])
        assert result.stdout.strip() == '中文回归' and result.stderr == ''
        assert os.environ['PYTHONIOENCODING'] == 'cp936'
    for channel in ('stdout', 'stderr'):
        try:
            run([sys.executable, '-c', f'import sys; sys.{channel}.buffer.write(bytes([255]))'])
        except UnicodeDecodeError:
            pass
        else:
            raise AssertionError('Invalid subprocess output must fail in the calling thread')
    command = [sys.executable, '-c', 'import sys; print("失败信息", file=sys.stderr); sys.exit(3)']
    try:
        run(command)
    except CommandError as exc:
        assert exc.result.returncode == 3 and '失败信息' in exc.result.stderr
    else:
        raise AssertionError('Nonzero exit must fail the runner')
    assert run(command, check=False).returncode == 3
    normalized = run([sys.executable, '-c', 'import sys; sys.stdout.buffer.write(b"a\\r\\nb\\rc")'])
    assert normalized.stdout == 'a\nb\nc'
    print('PASS: command-local UTF-8, strict output decode and subprocess failure propagation')


if __name__ == '__main__':
    main()
