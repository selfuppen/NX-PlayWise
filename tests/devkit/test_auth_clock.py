#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    sources = [ROOT / 'companion/overlay/source/main.cpp',
               ROOT / 'companion/nro/nro_setup.c', ROOT / 'companion/nro/nro_security.c']
    for path in sources:
        text = path.read_text(encoding='utf-8')
        assert 'ptc_companion_auth_verify_pin(' not in text, (
            f'{path.name}: PIN must not receive client libc time (Overlay can return positive uptime)')
        assert 'ptc_companion_auth_verify_pin_now(' in text
    clock = (ROOT / 'companion/auth_clock.c').read_text(encoding='utf-8')
    assert '#ifdef __SWITCH__' in clock and '"time:u"' in clock
    assert 'timeInitialize(' not in clock, 'PIN clock must not retain all time service sessions/shared memory'
    overlay = sources[0].read_text(encoding='utf-8')
    assert 'PTC_AUTH_TIME_UNAVAILABLE' in overlay
    assert 'PTC_UI_T_PIN_CLOCK_UNAVAILABLE' in overlay
    assert 'auth_clock.c' in (ROOT / 'companion/overlay/Makefile').read_text(encoding='utf-8')
    assert 'auth_clock.o' in (ROOT / 'companion/nro/Makefile').read_text(encoding='utf-8')
    print('PASS: both UI clients use the canonical short-lived PIN clock; unavailable clock fails closed')


if __name__ == '__main__':
    main()
