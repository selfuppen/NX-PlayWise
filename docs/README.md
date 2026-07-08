# Developer Documentation

This directory contains implementation-facing documentation for the clean-slate Nintendo Switch play-time control project.

The source architecture review is:

- `simplied/TECH_ARCHITECTURE_STABILITY_TESTABILITY_PLAN.md`

Read these documents in this order when starting development:

1. `DEVELOPMENT_GUIDE.md` - coding boundaries, module responsibilities, and default engineering rules.
2. `ARCHITECTURE.md` - runtime layers and dependency direction.
3. `PROTOCOL.md` - SD card layout, token protocol, request queue, result shape, and error contract.
4. `TESTING.md` - host-side unit and integration test requirements.
5. `DETAILED_TEST_PLAN.md` - step-by-step development, simulator, and real Switch observe testing.
6. `REAL_SWITCH_TESTING.md` - staged real-device validation checklist.
7. `IMPLEMENTATION_ROADMAP.md` - recommended implementation order and acceptance gates.

Primary project principles:

- Default to `observe`.
- Prefer fail-open behavior.
- Back up before any PCTL write.
- Consume nonces only after successful writes.
- Keep common logic host-testable.
- Gate raw block and suspend behind real-device probes.
