# Testing Guide

Testing is part of the architecture. The project should prove behavior on host before using real Switch services.

## Test Layers

Unit tests:

- Target pure `common` functions.
- Do not use filesystem.
- Do not use libnx.
- Do not require real time.

Integration tests:

- Use `mem_storage`.
- Use `pctl_stub`.
- Use fake time.
- Exercise request queue and control policy end to end.

Simulator tests:

- Validate companion UI and file protocol only.
- Do not claim real PCTL behavior.

Real Switch tests:

- Validate boot2, PCTL reads/writes, raw block, suspend, and play timer behavior.

## Required Unit Coverage

- Token encode/decode.
- HMAC 40-bit verification.
- Day index calculation.
- Weekday calculation.
- Bedtime cross-day logic.
- Request/result schema validation.
- Control mode decisions.
- Unlimited guard.
- Raw block capability guard.
- Suspend capability guard.
- Nonce lookup and consume timing.
- Error code mapping.

## Required Integration Coverage

Use `mem_storage + pctl_stub + deterministic fixtures`.

Cases:

- Valid offline code in `observe` returns dry-run success.
- Valid offline code in `grant` writes target.
- Successful `grant` consumes nonce.
- Repeated code is rejected after successful grant.
- Wrong date is rejected.
- Wrong secret is rejected.
- Minutes over max are rejected.
- `disable.flag` overrides all modes.
- `set_today_limit` applies target.
- `add_today_minutes` applies target.
- `disable_today_limit` applies unlimited target.
- `block_today` is rejected before raw block capability is verified.
- `restore_today_policy` recalculates from weekly template.
- `set_weekly_template` updates local rules.
- `set_bedtime` updates local rules.
- `parent_unlock_start`, `parent_unlock_end`, and expiry work.
- `probe_raw_block` can mark capability in stub.
- `probe_suspend` can mark capability in stub.
- Backup failure blocks writes.
- PCTL read failure returns stable error.
- PCTL write failure returns stable error.
- Storage write failure returns stable error where possible.
- Request moves through pending -> processing -> done.
- Stuck processing request is recovered on startup.

## Fixture Requirements

Fixtures should be generated, not handwritten.

Each fixture should define:

- Case name.
- Device ID.
- Grant secret.
- Date or day index.
- Nonce.
- Minutes.
- Expected token.
- Expected result.

`tools/make_fixtures.py` should generate deterministic fixture JSON and expected tokens from the same protocol rules used by `tools/grant_code.py`.

## Test Acceptance Gates

Before enabling real PCTL writes:

- All host unit tests pass.
- All host integration tests pass.
- `observe` verifies valid codes without nonce consumption.
- Bad code paths never consume nonce.
- Backup failure blocks writes.
- Capability gates reject raw block and suspend before probe.

Before enabling `enforce`:

- `grant` stage has passed real-device minimum write tests.
- Rejection cases pass on real hardware.
- Backup files are readable and useful.
- Event logs capture success and failure paths.

