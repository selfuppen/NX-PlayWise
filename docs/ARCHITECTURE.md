# Architecture

The project is split into four layers so that risky Switch behavior is isolated and most behavior is testable on a desktop.

## Layer 1: Common Core

Common core is pure C logic.

Responsibilities:

- 20-character token encode/decode.
- HMAC-SHA256 verification with 40-bit truncation.
- Day index and weekday calculation.
- Bedtime cross-day evaluation.
- Request/result schema constants.
- Error code mapping.
- Rule engine.
- Control policy.
- Nonce consumption decision rules.

Common core must not:

- Include libnx headers.
- Read or write files.
- Know SD card paths.
- Call real PCTL IPC.
- Render UI.
- Depend on the current real clock except through injected inputs.

## Layer 2: Platform Adapters

Platform adapters hide environment-specific operations behind C vtables.

Required adapters:

- Storage: read, atomic write, append, rename, remove, exists, list JSON files.
- PCTL: read status, apply target, start timer, stop timer, probe raw block, probe suspend.
- Time: expose current Unix time and day index to orchestration code.
- Logging: write human logs and append structured event lines.

Host tests replace platform adapters with:

- `mem_storage`
- `pctl_stub`
- fake time provider

## Layer 3: Sysmodule Orchestration

Sysmodule orchestration connects file protocol, common core decisions, platform adapters, and result writing.

Responsibilities:

- Initialize services after boot delay.
- Load config, rules, state, and capabilities.
- Recover stuck `processing` requests on startup.
- Move requests through queue states.
- Call common token verifier and control policy.
- Create backups before PCTL writes.
- Append events.
- Write result files for success and error paths.

Sysmodule orchestration should be thin. If behavior can be tested without libnx, put it in common core.

## Layer 4: Companion UI

Companion UI is a client of the request queue.

Responsibilities:

- Display current status and latest result.
- Accept child offline codes.
- Protect parent zone with local PIN hash.
- Submit parent-zone management requests.
- Wait for matching result IDs.
- Show timeout as backend-not-responding, not as business failure.

Companion UI is not the security boundary. Sysmodule must validate every request independently.

## Control Modes

`disabled`:

- Reject requests.
- Do not read PCTL.
- Do not write PCTL.

`observe`:

- Read status if available.
- Validate and compute expected results.
- Do not write PCTL.
- Do not consume nonces.
- Mark results with `dry_run: true`.

`grant`:

- Allow valid offline grants and parent-zone operations to write PCTL.
- Require backup first.
- Consume nonce only after successful write and result persistence.

`enforce`:

- Includes `grant`.
- May enable or refresh play timer.
- May execute bedtime/suspend strong-control behavior when capability gates pass.

## High-Risk Capability Gates

Raw block and suspend are implemented in v1 but gated.

Required checks:

- Control mode allows the operation.
- `capabilities.json` marks the capability verified.
- Backup succeeds.
- PCTL adapter succeeds.

Host tests may verify gating behavior only. Real capability verification must happen on hardware.

