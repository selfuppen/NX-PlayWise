# Detailed Test Plan

This document is the step-by-step test plan for development, simulator validation, and real Switch observe validation.

The current codebase target is Observe MVP:

- Host-side token and request queue behavior works.
- `offline_code` and `status` requests can be processed in observe mode.
- Generated SDMC layout is safe by default.
- Real Switch testing is limited to disabled/observe stages until a libnx sysmodule and companion NRO are built.

## 1. Development Environment

### 1.1 Required Host Tools

Minimum host tools:

- Windows PowerShell.
- Python 3.12 or newer.
- Git.

For later Switch binary builds:

- devkitPro.
- devkitA64.
- libnx.
- GNU Make.
- Switch homebrew build environment variables, especially `DEVKITPRO`.

Current host verification does not require devkitPro.

### 1.2 Repository Sanity Check

Run:

```powershell
git status --short
python --version
```

Expected:

- Python is available.
- Worktree only contains intentional local changes.

### 1.3 MVP Token Test

Run:

```powershell
python .\tests\mvp\test_token_v1.py
```

Expected:

- Fixture file is generated at `tests/fixtures/token_v1_fixture.json`.
- Output includes `MVP token v1 tests passed`.

Validated behavior:

- Valid token verifies.
- CLI output is deterministic with fixed nonce.
- Wrong secret returns `bad_signature`.
- Wrong day returns `wrong_date`.
- Reused nonce returns `used_token`.
- Minutes over max returns `minutes_exceed_limit`.

### 1.4 Observe Queue Test

Run:

```powershell
python .\tests\observe\test_observe_queue.py
```

Expected:

- Valid `offline_code` request produces an `ok` result with `dry_run: true`.
- `status` request produces an `ok` result with `dry_run: true`.
- Bad signature produces an `error` result with reason `bad_signature`.
- Request files are moved from `pending` to `done`.
- No nonce ledger consumption happens in observe mode.

## 2. Host-Side Protocol Probe

The protocol probe creates a local SDMC-like directory layout and can submit/process observe requests.

### 2.1 Initialize SDMC Layout

Run:

```powershell
python .\tools\protocol_probe.py init `
  --root .\.tmp\sdmc `
  --device test-device `
  --secret test-secret
```

Expected layout:

```text
.tmp/sdmc/switch/play-time-control/
├── config.json
├── auth.json
├── rules.json
├── state.json
├── capabilities.json
├── inbox/
│   ├── pending/
│   ├── processing/
│   └── done/
├── results/
├── logs/
├── ledger/
├── backups/
└── flags/
```

Important default:

- `control_mode` is `observe`.

### 2.2 Generate A Test Grant Code

Run:

```powershell
python .\tools\grant_code.py `
  --minutes 30 `
  --device test-device `
  --secret test-secret `
  --day-index 2380 `
  --nonce 4660
```

Record the printed code.

### 2.3 Submit Offline Code Request

Run:

```powershell
python .\tools\protocol_probe.py request `
  --root .\.tmp\sdmc `
  --type offline_code `
  --code <CODE_FROM_PREVIOUS_STEP>
```

Expected:

- A JSON request appears under `inbox/pending/`.

### 2.4 Process Observe Requests

Run:

```powershell
python .\tools\protocol_probe.py process-observe `
  --root .\.tmp\sdmc `
  --day-index 2380
```

Expected:

- The request is moved to `inbox/done/`.
- A matching result appears under `results/`.
- Result includes `"dry_run": true`.
- No PCTL write is attempted.

### 2.5 Submit Status Request

Run:

```powershell
python .\tools\protocol_probe.py request `
  --root .\.tmp\sdmc `
  --type status

python .\tools\protocol_probe.py process-observe `
  --root .\.tmp\sdmc `
  --day-index 2380
```

Expected:

- A status result is written.
- Result includes `mode: "observe"` and `dry_run: true`.

## 3. Simulator Testing

Simulator testing is for companion UI and file protocol only.

Do validate:

- NRO launches.
- Offline code entry writes request JSON.
- Parent-zone PIN flow works.
- Parent-zone actions write expected request types.
- Manually inserted result JSON is displayed correctly.

Do not claim simulator validation for:

- boot2 sysmodule startup.
- PCTL IPC.
- Play timer behavior.
- raw block.
- suspend.

Simulator observe preparation:

1. Create SDMC layout with `protocol_probe.py init`.
2. Copy or mount the generated `switch/play-time-control/` directory into the simulator SD card path.
3. Run companion NRO once available.
4. Submit request from UI.
5. If sysmodule is not available in simulator, manually run host `process-observe` against the same SDMC directory or manually write result JSON.

Pass criteria:

- UI generates protocol-compatible request files.
- UI reads matching result files.
- Timeout is displayed as backend-not-responding, not as business failure.

## 4. Real Switch Testing

Real Switch testing must be staged. Do not start with enforce.

### 4.1 Hardware Preconditions

Required:

- Real Nintendo Switch.
- Atmosphere CFW.
- Writable SD card.
- Homebrew Menu.
- Known way to remove boot2 sysmodule from SD card if needed.
- `disable.flag` recovery path understood before boot2 testing.

### 4.2 SD Card Preparation For Observe

On PC:

```powershell
python .\tools\protocol_probe.py init `
  --root .\.tmp\sdmc `
  --device kid-switch `
  --secret replace-with-long-random-secret
```

Copy:

```text
.tmp/sdmc/switch/play-time-control/
```

to:

```text
sdmc:/switch/play-time-control/
```

Confirm on SD card:

- `config.json` exists.
- `control_mode` is `observe`.
- `flags/disable.flag` does not exist unless deliberately testing fail-open.

### 4.3 NRO-Only Stage

Purpose:

- Validate SD card layout and companion file access without sysmodule boot2 risk.

Steps once companion NRO exists:

1. Copy companion NRO to `sdmc:/switch/play-time-control/pctc.nro` or the agreed NRO path.
2. Launch from Homebrew Menu.
3. Submit a status request.
4. Confirm request appears in `inbox/pending/`.
5. Manually provide a matching result or run host observe processor on copied SDMC contents.

Pass criteria:

- Companion reads/writes expected paths.
- No boot2 sysmodule is installed.

### 4.4 Disabled Boot2 Stage

Purpose:

- Validate sysmodule startup without PCTL access.

Required config:

```json
{
  "version": 1,
  "control_mode": "disabled"
}
```

Steps once sysmodule binary exists:

1. Install sysmodule files under Atmosphere contents path.
2. Include `boot2.flag`.
3. Boot Switch.
4. Check `logs/sysmodule.log`.
5. Submit a request.
6. Confirm result reason is `disabled`.

Pass criteria:

- Sysmodule starts and logs.
- Request result is written.
- PCTL read/write is not attempted.

### 4.5 Observe Boot2 Stage

Purpose:

- Validate real sysmodule request processing and PCTL read-only path.

Required config:

```json
{
  "version": 1,
  "control_mode": "observe"
}
```

Steps:

1. Boot with sysmodule installed.
2. Generate a valid same-day code.
3. Submit offline code from companion.
4. Wait for result.
5. Inspect `results/<request_id>.json`.
6. Inspect `logs/events.jsonl`.

Pass criteria:

- Result has `status: "ok"`.
- Result has `mode: "observe"`.
- Result has `dry_run: true`.
- PCTL is not written.
- Nonce is not consumed.
- If PCTL read adapter is present, state is included.
- If PCTL read fails, result is structured and stable.

### 4.6 Observe Rejection Cases

Run in observe mode:

- Wrong secret.
- Wrong date.
- Minutes over max.
- Malformed token.
- Unknown request type.
- Bad JSON.

Pass criteria:

- Result is `status: "error"`.
- Reason is stable.
- No nonce is consumed.
- No PCTL write is attempted.
- Companion does not show backend timeout when sysmodule processed the request.

## 5. Recovery And Rollback

To disable sysmodule behavior:

1. Create `sdmc:/switch/play-time-control/flags/disable.flag`.
2. Reboot if necessary.
3. Remove `boot2.flag` if boot behavior must be stopped entirely.

To clean request state:

- Move stuck files from `inbox/processing/` back to `inbox/pending/`, or archive them.
- Keep `results/` and `logs/events.jsonl` for debugging.

Do not delete backups before reviewing PCTL write behavior in later grant/enforce stages.

