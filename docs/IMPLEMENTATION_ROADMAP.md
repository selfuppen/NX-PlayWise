# Implementation Roadmap

This roadmap turns the architecture review into implementation phases with acceptance gates.

## Phase 1: Common Core Foundation

Build:

- Stable error codes and reason/message mapping.
- Time math: day index, weekday, bedtime ranges.
- Token v1: Crockford Base32, payload pack/unpack, HMAC verification.
- Request/result schema constants.
- Deterministic fixture generator.

Acceptance:

- Unit tests cover all pure functions.
- Fixture generation is deterministic.
- No common code depends on libnx or filesystem.

## Phase 2: Test Harness And Interfaces

Build:

- `StorageVTable`.
- `PctlVTable`.
- `mem_storage`.
- `pctl_stub`.
- Fake time provider.
- Basic host integration test runner.

Acceptance:

- Integration tests can run without Switch hardware.
- Storage failures and PCTL failures can be injected.

## Phase 3: Policy And Queue Core

Build:

- Rule engine.
- Control policy.
- Nonce ledger behavior.
- Request queue state machine.
- Result builder.

Acceptance:

- pending -> processing -> done -> result works in host tests.
- Observe never writes or consumes nonce.
- Grant consumes nonce only after successful write.
- Capability gates reject raw block and suspend by default.

## Phase 4: Sysmodule Skeleton

Build:

- Boot lifecycle.
- Config/rules/state/capabilities loading.
- SDMC storage adapter.
- Event logger.
- Backup writer.
- Request dispatcher.

Acceptance:

- `disabled` mode can boot, log, and return results.
- Bad requests return structured errors.
- Stuck processing requests are recovered.

## Phase 5: PCTL Adapter

Build:

- Real PCTL read status.
- Real apply target.
- Start/stop timer wrappers.
- Raw block probe path.
- Suspend probe path.

Acceptance:

- Adapter is isolated from business logic.
- Host tests still use `pctl_stub`.
- Real writes remain behind control policy and backup.

## Phase 6: Companion Foundation

Build:

- Child status screen.
- Offline code input.
- Request client.
- Matching-result wait.
- Timeout display.

Acceptance:

- Simulator can write requests.
- Manually inserted results display correctly.
- Timeout is shown as backend-not-responding.

## Phase 7: Parent Zone

Build:

- PIN initialization.
- PIN hash verification.
- Today limit operations.
- Weekly template editing.
- Bedtime editing.
- Parent unlock controls.
- Probe controls for raw block and suspend.

Acceptance:

- Child UI cannot access parent operations directly.
- Parent requests match protocol.
- High-risk operations are visibly gated by sysmodule result.

## Phase 8: Packaging And Real-Device Stages

Build:

- Safe NRO-only package.
- Disabled boot2 package.
- Observe package.
- Grant package.
- Enforce package.

Acceptance:

- Packages default to safe settings.
- `boot2.flag` is only included when explicitly requested.
- Real-device checklist in `REAL_SWITCH_TESTING.md` is followed.

## Deferred Optimization Work

Do after v1 behavior is stable:

- UI polish.
- Report presentation.
- Sensitive field masking.
- Historical request/result cleanup policy.
- Desktop protocol probe improvements.
- More offline token actions.

