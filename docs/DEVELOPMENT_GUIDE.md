# Development Guide

This project should be implemented stability-first. The first useful milestone is not a polished UI; it is a host-testable core that proves token handling, request processing, control policy, and failure behavior before touching real PCTL writes.

## Language And Runtime

- Use C by default for `common`, `sysmodule`, and `companion`.
- Keep `common` free of libnx, filesystem calls, SD card paths, UI, and real clocks.
- Use Python for developer tools such as fixture generation and protocol probing.
- Do not introduce C++ unless a later decision records why it is needed.

If C++ is introduced later:

- Build with `-fno-exceptions`.
- Build with `-fno-rtti`.
- Avoid complex static initialization.
- Avoid uncontrolled dynamic allocation in sysmodule hot paths.

## Dependency Direction

Allowed direction:

```text
companion      -> common
sysmodule      -> common
sysmodule      -> platform adapters
tests          -> common
tests          -> test doubles
tools          -> protocol-compatible fixture logic
```

Forbidden direction:

```text
common         -> libnx
common         -> SDMC paths
common         -> UI
common         -> process-global mutable platform state
business logic -> raw PCTL u16 layout
```

## Module Responsibilities

`common`:

- Token encode/decode.
- HMAC verification.
- Time math.
- Rule evaluation.
- Control policy.
- Request/result schema constants.
- Stable error codes.

`sysmodule`:

- Boot lifecycle.
- Service initialization and retry.
- Request queue processing.
- Config/state/capabilities loading.
- Backup orchestration.
- Event logging.
- PCTL adapter calls.

`companion`:

- Child status screen.
- Offline code entry.
- Parent-zone PIN flow.
- Local rule editing UI.
- Request file creation.
- Result display.

`tools`:

- Grant code generation.
- Deterministic fixture generation.
- Protocol probing.
- SD card package assembly.

`tests`:

- Host-side unit tests.
- Host-side integration tests with `mem_storage` and `pctl_stub`.
- Protocol fixtures.

## Stability Rules

- Default config is `control_mode: "observe"`.
- Default package must not include `boot2.flag`.
- `disable.flag` overrides every control mode.
- Unknown `control_mode` degrades to `observe`.
- Bad JSON, unknown schema, and unknown request types must not touch PCTL.
- Any request that can be parsed should produce a result file.
- No invalid token path may consume a nonce.
- No dry-run path may consume a nonce.
- PCTL write failures must not consume a nonce.
- Backup failure must block the write.

## Review Checklist

Before merging implementation work, check:

- Can the changed logic run in host tests without a Switch?
- Does `observe` avoid writes and nonce consumption?
- Does every write path require backup first?
- Does every high-risk path check capabilities?
- Are error codes stable and mapped to reason strings?
- Are user-facing Chinese messages present for result errors?
- Does the change avoid exposing `grant_secret` in child-visible UI?

