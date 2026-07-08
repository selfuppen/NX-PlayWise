# Real Switch Testing

Real-device testing must be staged. Do not jump directly to `enforce`.

## Preconditions

- Nintendo Switch real hardware.
- Atmosphere CFW.
- Writable SD card.
- Homebrew Menu for companion NRO.
- Known recovery path if sysmodule misbehaves.
- `disable.flag` behavior verified before strong-control tests.

## Stage 1: Host Tests

Goal:

- Prove common logic, request queue, policy, and test doubles.

Pass criteria:

- Unit tests pass.
- Integration tests pass.
- Fixtures are deterministic.

## Stage 2: Companion Simulator

Goal:

- Validate UI and file protocol without PCTL.

Pass criteria:

- Companion starts.
- Offline code entry writes a request.
- Parent zone PIN flow works.
- Parent operations write expected request types.
- Manually inserted result files display correctly.

## Stage 3: NRO-Only Safe Package

Goal:

- Validate SD card paths and companion on hardware without boot2 sysmodule.

Pass criteria:

- Companion can read/write `sdmc:/switch/play-time-control/`.
- No sysmodule auto-start is installed.

## Stage 4: Disabled Boot2 Smoke Test

Goal:

- Validate sysmodule boot and logging without touching PCTL.

Required config:

```json
{
  "control_mode": "disabled"
}
```

Pass criteria:

- boot2 sysmodule starts.
- `sysmodule.log` is written.
- Requests return disabled errors.
- No PCTL read/write is attempted.

## Stage 5: Observe PCTL Read

Goal:

- Validate PCTL read path and dry-run result.

Pass criteria:

- Valid offline code returns `dry_run: true`.
- Current state is reported.
- PCTL is not written.
- Nonce is not consumed.

## Stage 6: Grant Minimum Write

Goal:

- Validate smallest safe PCTL write.

Method:

- Use a 1-minute valid grant code.
- Keep backup enabled.

Pass criteria:

- Backup file is created before write.
- Today limit changes as expected.
- Result is success.
- Nonce is consumed after success.
- Event log records the write.

## Stage 7: Grant Rejection Cases

Goal:

- Prove failures do not modify PCTL.

Cases:

- Repeated code.
- Wrong date.
- Wrong secret.
- Minutes over max.
- Unlimited guard.
- Backup failure if testable.

Pass criteria:

- Stable error result is written.
- PCTL target is unchanged.
- Nonce is not consumed unless the original successful grant already consumed it.

## Stage 8: Weekly, Bedtime, Parent Unlock

Goal:

- Validate full local-management behaviors before strong enforcement.

Pass criteria:

- Weekly template applies expected today target.
- Bedtime state changes at expected times.
- Parent unlock starts, ends, and expires.
- Event log captures transitions.

## Stage 9: Enforce

Goal:

- Validate play timer refresh and strong-control mode.

Pass criteria:

- Enforce starts or refreshes play timer as designed.
- Writes still require backup.
- Results remain structured.
- `disable.flag` still forces fail-open.

## Stage 10: Raw Block And Suspend Probe

Goal:

- Validate high-risk capabilities separately.

Rules:

- Run only after previous stages pass.
- Run one capability at a time.
- Keep recovery path ready.
- Record manual observations.

Pass criteria:

- Probe result is logged.
- Capability is marked true only after successful verification.
- Normal raw block/suspend requests are rejected before probe and allowed after probe.

