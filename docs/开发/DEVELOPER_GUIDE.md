[Documentation index](../README_en.md)

<div align="center">

  [English](DEVELOPER_GUIDE.md) | [简体中文](开发指南.md)

</div>

# Developer Guide

PlayWise validates protocols, queues, security state machines, and recovery transactions on the host side first before interacting with physical Switch PCTL. Environment prerequisites and platform specifics are detailed in the [Development Environment Guide](DEVELOPMENT_ENVIRONMENT_GUIDE.md); external behavioral changes must be synchronized with the [Protocol Specification](../设计/PROTOCOL.md) and [Testing Guide](TESTING_GUIDE.md).

## Architectural Boundaries

```text
Parent Web App (PWA) / Command-Line Interface (CLI)
        │ Generate offline grant codes
        ▼
Console Application (Companion NRO) / In-Game Overlay (Tesla Overlay)
        │ pctc:u IPC preferred, atomic SD queue fallback
        ▼
Standard Release Background Service (sysmodule)
        │ token / rules / setup / recovery
        ▼
platform adapter ──> PCTL / SDMC / time / logs
        ▲
        └── common (Pure C, zero platform dependencies)
```

- `common/`: tokens, rules, policies, request/result definitions, stable errors, and version constants; must not depend on libnx, SD paths, UI libraries, or real-time clocks.
- `platform/`: adapters for PCTL, storage, time, and read-only usage statistics; private 0x44 layouts reside strictly within the Switch adapter layer. `platform/usage_stats.h` must not include or depend on `platform/pctl.h`.
- `sysmodule/`: the sole security boundary, responsible for preflight checks, setup, transactions, recovery, queueing, nonces, daily Enforce, and audit logging.
- `companion/`: strictly creates requests and renders results; never reads secrets, validates tokens, or calls PCTL directly.
- Companion NRO theming is an isolated rendering layer capability: platform-agnostic preference parsing and semantic palettes reside in `companion/nro/ui_theme.*`; libnx system theme queries and configuration persistence occur only in the NRO orchestration layer, while rendering routines receive only a read-only theme view. Timers, requests, PCTL, tokens, sysmodules, and overlays must not depend on NRO themes.
- Companion NRO and the in-game overlay share stable string keys in `companion/ui_language.*`. Text source files are modularized under `companion/ui_text/{keys,zh_hans,zh_hant,en}/{common,child,today,plan,grant,settings,support,setup,overlay}.inc`, with top-level `ui_text_*.inc` aggregating these modules; each key belongs to exactly one module, and cross-page strings reside in `common`. New user-visible dynamic strings must specify identical parameters across key files and trilingual modules, without embedding Chinese phrases or `is_en` branches into business logic; `tests/devkit/test_ui_text_catalog.py` verifies module completeness and parameter parity across all three languages. Legacy whole-sentence lookup tables serve legacy unmigrated pages and should not be extended.
- `device_lab/`: internal high-risk operational entry point; must never introduce LAB enums or fields into standard release UI.
- `tools/` and `tests/`: maintain standard library portability and deterministic host-side testing.

Module responsibilities within UI, NRO orchestration, and sysmodule control logic:

```text
companion/nro/
├── ui_model.h                         Common UI enums, data types, and PtcUiModel
├── ui_state.*                         Plan and status projection, formatting, and navigation logic
├── ui_input_state.*                   Numeric, duration, PIN, and long-press input models
├── ui_state.c                         Action, overlay, and support state machines
├── ui_plan/navigation/status_state.c  Plan deduction, parent navigation, and status display projection
├── ui_result_state.c                  Background results, history records, and feedback string projection
├── ui_layout.c + ui_hit_test.c        Widget geometry and hit-testing
├── ui_graphics.c                      Render lifecycle, animation clock, and top-level draw dispatch
├── ui_render_primitives/text.c        Pixel primitives, theme colors, font caches, and typography
├── ui_render_core/components.c        Framework widgets and shared state components
├── ui_render_action_card.c            Parent action cards, action icons, and in-card visual elements
├── ui_render_child/setup/pages.c      Child Zone, initial setup wizard, and Parent Portal root pages
├── ui_render_plan.c                   Weekly schedule, holiday allowance, bedtime, and 7-day preview
├── ui_render_dialogs.c                Generic dialog shells
├── ui_render_dialog_policy/input.c    Policy confirmation and numeric/PIN input overlays
├── ui_render_overlay_*.c              Credentials/history, schedule editor, and support detail dialogs
├── nro_runtime.c                      Runtime utilities, animations, notifications, and hot-reload state
├── nro_input_dialogs.c                Native OS keyboard, PIN, date, and minute input flows
├── nro_preferences.c                  Shortcuts, themes, and initial setup preference persistence
├── nro_actions/action_dispatch.c      System recovery refresh and user action/confirmation dispatch
├── nro_plan_actions.c                 Schedule drafts, saving, and exit navigation
├── nro_requests.c                     Generic request submission and rule draft projection
├── nro_redemption.c                   Grant code preview submission and redemption recovery
├── nro_result_poll.c                  Background result polling, parsing, and subsequent actions
├── nro_setup.c                        Initial setup wizard and Parent Portal entry workflow
├── nro_security.c                     Sensitive action authorization, PIN changes, and auth retries
├── nro_pairing.c                      Pairing credentials, on-console code generation, QR codes, and web export
├── nro_history.c                      Grant code and family activity history operations
├── nro_policy_requests.c              Temporary quotas, self-buffer, and bedtime request submission
├── nro_support.c                      Sanitized diagnostics export
├── nro_actions.c                      Parent actions and navigation
├── nro_input.c                        Overlay button semantics and shared interaction behavior
├── nro_touch_input.c                  Touch hit routing, reusing overlay interaction behavior
└── main.c                             Initialization, main loop, rendering, and cleanup

sysmodule/
├── lab_session.c + lab_session_report.c Device Lab session state machine and evidence report assembly
├── sysmodule_storage.c                config/rules/state/setup/snapshot/recovery persistence
├── sysmodule_audit.c                  Event and PCTL debug audit logging
├── sysmodule_history.c                Nonces, activity/redemption history, and daily summaries
├── sysmodule_results.c                Result state assembly, extended fields, and write-back
├── sysmodule_control.c                PCTL application, observation, rule updates, and exact recovery
├── sysmodule_setup.c                  Setup, handover, install snapshot restore, and startup checks
├── sysmodule_requests.c               Request validation, rule writes, and top-level dispatch
├── sysmodule_request_grants/recovery.c Grant codes, buffer quota, and restricted state recovery
└── sysmodule_core.c                   Lifecycle, queue scanning, scheduling, cleanup, and Enforce orchestration
```

`ui_render_internal.h`, `nro_app_internal.h`, and `sysmodule_internal.h` are strictly private to their respective translation units; public business code must not include these private headers. New helpers should default to `static`, exposed via private headers only when strictly necessary across units. Makefiles must explicitly enumerate modular source files used by host and Eden targets; tests must never directly `#include` implementation C files.

IPC requests must be persisted before acceptance. Once accepted over IPC, clients must not re-submit; they must await the IPC response or read the SD result file matching the same request ID.

Temporary quota plans and statutory holidays are pure, deterministic functions in `common/rules`, evaluated in strict priority order: "Today's quota adjustment > Temporary quota schedule > Statutory holidays > Weekly schedule". Self-buffer claims follow the existing PCTL snapshot and rules/state/activity/result commit transactions; failures must never record a claim date or consume a nonce. Family activity logs record only stable actions, timestamps, and minute durations (up to 200 entries), never repurposing support logs or redemption ledgers to store confidential fields.

Companion Parent Portal navigation consists of five top-level pages: `TODAY / PLAN / GRANT / SETTINGS / SUPPORT`. The `PLAN` root acts as the 5-card hub; Weekly Schedule, Statutory Holidays, and Bedtime open full-screen first-level editor subpages via `PtcUiPlanPage`, hiding top-level tabs and restoring card focus upon exit. `SETTINGS` retains only Appearance, PIN, Shortcuts, Homebrew Menu Access, and Family Activities, while `SUPPORT` is accessible as a top-level tab via `L/R`. Weekly, Holiday, and Specific Date bedtime subpages save independently; submission merges the active subpage and master switch onto saved policies, transmitting complete `PtcBedtimePolicy`, `set_bedtime_policy`, and `activation:"immediate"`. Switching tabs or exiting validates current drafts; validation failures preserve drafts without navigating, and never clear other subpages. Weekly and Specific Date custom windows share `ptc_ui_bedtime_*_rect` layout and the same 24-hour timeline; rendering, touch, and tests must avoid hardcoded coordinate divergence. Inherited and exempt modes on specific dates retain timeline context while disabling time inputs and presets.

The 26 functional cards across the five top-level tabs maintain a uniform 120px height with explicit `UiActionIcon`s, single-line titles, and zero font guessing or arbitrary line breaks. Subtitles project current status, using semantic truncation for long lines. Quick grant and theme cards prioritize tags and graphical representations. Quota impacts evaluate through `PtcUiPlanImpactProjection`: only when today's effective quota changes will directional arrows depict "current quota/remaining → post-action quota/remaining"; consumption estimates, rule sources, and projected values occupy separate informational lines. When editing other weekdays, when overridden by higher-priority rules, or when rule sources change without altering total minutes, editors and confirmation dialogs display "Today's quota unchanged" with rationale, avoiding numeric predictions or arrows. When no draft exists, the UI shows current "Today's allowance / Remaining today" without disguising it as a forecast.

The status bar in the upper-right corner of the Parent Portal is the sole persistent indicator for system health, remaining playtime, and stale cache state. The footer maintains a fixed 44px height, displaying standard page actions during normal health; only states requiring parental attention (protection mode, failures, recovery transactions, emergency deactivation, temporary bypass) render a focusable, touchable Support button on the right. Standard refresh responses must not occupy primary workspace; waiting, failure, and non-refresh write results utilize compact, on-demand feedback banners. Adding new state conditions requires updating rendering, hit-testing, focus navigation, and host C unit tests simultaneously.

Daily summaries are non-blocking control loop sidecars: they aggregate reliable status by unique calendar date; file corruption or write failures do not affect Enforce outcomes. Per-title playtime statistics constitute an isolated, lower-privilege read-only subsystem; until PDM hardware verification passes, the Switch release adapter reports permanently unavailable, omitting `pdm:qry` initialization, with the UI retaining placeholder cards. PDM, title IDs, user accounts, and Top 3 logic must never be introduced into the PCTL adapter, nor may PCTL command `1952` be invoked to fabricate user statistics.

Bedtime is included in standard releases, but disabled by default on new installations. Baseline qualification status defaults to `pending`; `manual_verified` notes manual verification of primary features, but does not imply Bedtime qualification. Standard NRO only configures overnight schedules when unrestricted; initial enablement requires confirming Nintendo Parental Controls is enabled and binding the confirmation to the current environment fingerprint. Lockout visibility, reboots, HOME/foreground/suspended/sleep transitions, and day-boundary recoveries must be verified via Device Lab hardware reports before a build can be declared qualified.

## Standard Release Builds, Support, and Device Lab

Standard distribution builds use the `release` profile, driven by paired versions in `common/version.h` and `common/version.mk`. Python manifest generation and package validation read both files via `tools/playwise_version.py`; any mismatch immediately fails the build. Support is an intrinsic feature of standard releases, not a separate package.

Public artifacts are divided into two tiers: `playwise-<version>.zip` is the standard Switch installation package containing only runtime directories; `playwise-complete-<version>.zip` is the recommended full delivery bundle for end users, strictly embedding the standard package and generated `playwise-offline.html`. Aggregation wrappers must never alter or unwrap the standard package, nor attach Device Lab, install scripts, or extra notes.

The standard package must merge cleanly onto the SD card root. Default setup JSON files reside in `switch/playwise/defaults/`, never under live `switch/playwise/*.json` paths in the Zip; Release sysmodule and Companion create live files atomically only when missing, never overwriting existing user data. Standard in-place upgrades overwrite only the NRO, Overlay, sysmodule, `build.json`, `package-artifacts.json`, and immutable templates; explicit `-Clean`/`-Full` flags are required to purge user data. New `release_id`s trigger environment re-detection prompts while preserving PINs, encryption keys, rules, and original installation snapshots.

Standard releases support in-place over-the-network upgrades (DBI/MTP, FTP, USB) without removing the SD card. NRO evaluates running background services strictly by `release_id` rather than semantic version ordering, allowing hot-reloads during rebuilds or downgrades. Upgrades mandate verifying clean `boot2.flag`, matching sizes and SHA-256 in `package-artifacts.json`, zero active requests or recovery state, and verified parental PIN. Handoff leverages IPC v2 `quiesce/ready` flows without extra business requests; calling `pmshellTerminateProcess` or launching duplicate Title ID processes without verifying prior PID termination is strictly prohibited.

Hot reloading utilizes `handover/reload.json` v1 and `boot2.flag.playwise-hot-reload-backup`. The NRO signals the running background service to cease accepting requests and complete pending operations, closes its own IPC session, temporarily persists boot flags, and awaits process exit; it then restores the flag and launches the new SD binary using `NcmStorageId_None`. Reload is successful only when `runtime-ready.json` under the new PID matches `release`, target `release_id`, new boot ID, and successful IPC v2 reconnection. Timeouts for handover, exit, and launch are 10, 10, and 30 seconds. Failures before exit restore the flag, clear intent, and resume old service operations; post-exit launch failures retry once if the target does not exist, ultimately prompting for a complete reboot without auto-rebooting.

Upon launch, the NRO recovers known interrupted transactions prior to standard boot flag checks: promoting complete `.tmp` journals, restoring empty flags, and clearing matching intents; if the source PID has vanished, it launches the installed background service. Unrecognized backups, non-empty flags, contradictory journals, or foreign handover files are never overwritten. Card reader upgrades still require powering off, removing the card, and rebooting; Device Lab maintains its own hot-switch implementation independently.

Device Lab specifications:

| Attribute | Standard Release Build | Device Lab |
| --- | --- | --- |
| Title ID | `4200000000BD2300` | `4200000000BD23F0` |
| IPC Interface | `pctc:u` | `pwtl:u` |
| SD Root Directory | `sdmc:/switch/playwise` | `sdmc:/switch/playwise-device-lab` |
| Manifest Profile | `release` | `device-lab` |
| Default `boot2.flag` | Present | Omitted |
| Default Build Target | `make packages` | Explicit `make device-lab-package` only |

Standard release binaries must never contain dangerous handlers, engineering probes, runtime control modes, or `capabilities.json`. Bedtime requests, rules v2, and standard UI are valid release features; packaging gates ensure consistency across all three components without treating `bedtime` as experimental contamination. Manual verification declarations in manifests must not be represented as full qualification until `bedtime-qualification.json` passes. The LAB NRO uses a state-driven Chinese wizard with persistent watermarks, requiring long-press ZL+ZR+A confirmation for boot flag switches; the overlay employs a fixed dark theme with physical controller long-press confirmation for restriction phases. Protocol status values and report fields maintain English machine representations, with Chinese reserved for display.

PCTL system dialogs triggered by daily limits or bedtime block games, Homebrew, HOME menu, System Settings, and PlayWise NRO; the overlay serves as PlayWise's only on-device recovery entry point. Native Nintendo dialogs and master PIN unlocking serve as system-level recovery paths. NRO manages configuration only when unrestricted; the overlay provides daily limit recovery (grant codes, PIN one-time minutes, unlimited today) and bedtime recovery (skip, disable, restore snapshot). Recovery actions remain whitelisted during `disable.flag` and protection mode. Overlay timeouts must never write recovery flags, preserving diagnostics and prompting external intervention. If initial handshakes cannot be validated, standard NRO explicitly prompts risk acceptance after confirming Nintendo Parental Controls is enabled; background services will not disable plans if handshakes subsequently disappear.

Device Lab NRO must not query optional `pwtl:u` synchronously: before initial enablement, after writing boot flags prior to reboot, or during interrupted transitions, the service may be absent and SM calls would hang indefinitely. The NRO evaluates phase strictly via boot journals and persistent `session.json`, routing recovery requests via SD queues. Parent `flags/` directories for both titles must be created before enablement; Lab packages retain the `flags/` directory while omitting `boot2.flag`. The Device Lab overlay similarly uses the persistent SD request queue; experimental background services claim queued requests and write results after rebooting, without persistent IPC handles.

Standard NRO performs a read-only check of its empty `boot2.flag` at startup. If the flag is stashed by Device Lab or missing, it avoids connecting to `pctc:u`, displaying actionable guidance to "Restore standard service and reboot"; this prevents client hangs without rebuilding, overwriting, or moving boot flags unilaterally.

Eden testing relies on a third profile, `eden-test`. It operates through a single NRO: running `sysmodule_core` in-process alongside deterministic mock PCTL, storing data under `sdmc:/switch/playwise-eden`, omitting `pctc:u`, using Title ID `nro-only`, and excluded from `make packages` or Device Lab targets. Built via `make eden-test-nro`, artifacts reside in `build/eden-test/`. This target accelerates iteration across UI, queues, rules, PINs, nonces, bedtime limits, and recovery workflows. Standard releases and Eden share the bedtime configuration interface; because mock PCTL does not suspend NRO, `PLAYWISE_EDEN` conditionally relaxes official setting confirmation and overlay handshake checks at compile time. This relaxation is prohibited in standard or Lab builds, and packaging gates strictly reject Eden markers in release binaries.

### Equivalence Boundaries: Eden vs Production Suite

Using `python tools/package_remote.py --only eden` yields rapid test builds, but its verification boundaries must be recognized:

- **What Eden Validates (High Confidence)**:
  - **UI & Visual Layout**: Page structures, typography, dark/light themes, text wrapping, and multilingual string catalogs;
  - **Frontend Navigation & Controls**: Gamepad focus movement, touch hit-testing, modal prompts, and action confirmations;
  - **Local Algorithm & State Machine**: Timer and eye-care rest countdowns, bedtime schedule calculations, PIN validation, and offline code generation/parsing;
  - **C Core Syntax & Portability**: Compiled with the exact same devkitA64 toolchain.
- **What Eden Cannot Validate (Architectural Differences)**:
  - **Cross-process IPC Communication**: Eden bundles the background core in-process; IPC piping (`pctc:u`), message serialization, concurrency, and timeouts are unexercised.
  - **Real Horizon PCTL Enforcement**: Eden uses `pctl_stub.c`; private PCTL IPC commands, native popups, and hardware-level sleep/blocking cannot be tested in Eden.
  - **CFW & Security Preflights**: `-DPLAYWISE_EDEN` relaxes parental controls setup requirements and Overlay handshake checks.
  - **Other Production Components**: `--only eden` completely omits building the production background Sysmodule (`exefs.nsp`) and Tesla Overlay (`playwise.ovl`).
- **Tiered Verification Workflow**: Use `python tools/package_remote.py --only eden [--skip-tests]` for frequent UI/logic iterations; execute the default `python tools/package_remote.py` to build and validate the complete release suite before code integration or delivery.

## Security State Machine

Rules and state reads distinguish confirmed missing files from read failures. Only missing files allow initialization defaults; empty files and truncated reads prevent control writes. Nonce ledgers validate complete lines, never treating corrupt data as unused; transaction preparation removes only expired dates while retaining today's and future records.

Recovery journals isolate request_id owners, with enforce owning background transactions. Other ordinary operations return 325 control_busy instead of reusing or clearing pending readback transactions; parse failures cannot touch another owner's transaction. Recovery requests roll back the earlier transaction first. Bedtime snapshots, instance metadata and eye-care snapshots are backed up and restored with rules/state; interactive requests retain the journal until result commit. BLOCKED and autonomous writes prohibit 1451 fallback. The same eye-care break can be recovered during protection while retaining disable.flag.

1. Startup initiates read-only validation across build manifests, environment fingerprints, PCTL state, layouts, and pending transactions.
2. `verified` indicates baseline qualification match; `accepted_unknown` denotes structural validity requiring parental confirmation; security preflight failures transition to `protection`.
3. Only when PIN, secret, and rules are valid and confirmed by parents is the immutable install snapshot written and restrictions lifted.
4. `released` retains a 5-second synchronization grace period before transitioning to `active`.
5. Enforce writes record `applied_pending_confirmation`; confirmation within 30 seconds transitions to idle, while timeouts trigger rollback. Active transactions during this window (`recovery/active`) must not be treated as stale startup transactions.
6. Startup checks for legacy transactions once; successful recovery creates `disable.flag` and halts new writes; unproven rollbacks trigger immediate circuit-breaking.

`disable.flag` is not a total shutdown: status queries, diagnostics export, retry repairs, and install snapshot recovery must continue functioning. Lifting deactivation under `active` or `restored` requires parental confirmation and complete read-only preflight re-validation before clearing flags, removing restrictions, and resuming management. Recovery precedence is: install snapshot restore, setup retry, legacy startup transaction, write circuit break, standard requests, Enforce.

## Data and Secrets

- `config.json` stores only non-sensitive options; control modes and secrets must never be added.
- The sysmodule generates a 32-byte secret via `randomGet` on first boot, storing it in `credentials.json`.
- The QR code encoder is vendored in `third_party/qrcodegen/`, with upstream commit, license, and check date tracked in `UPSTREAM.txt`; QR codes encode URL fragments locally without network calls.
- `playwise-public-demo-secret-0001` is an explicit, public low-security demo secret, never a production seed; enabling it requires PIN re-authentication, secondary confirmation, and persistent UI warnings.
- PINs support 1–64 digits with a random 16-byte salt; all lengths share identical credential schemas. `auth.json` stores HMAC-SHA256 hashes, salts, attempt counts, and lockout expirations without plaintext PINs.
- Diagnostics export enforces a strict whitelist, never bundling `credentials.json`, `auth.json`, tokens, raw ledgers, or recovery secrets.
- If the system clock is invalid, logs are written to `logs/undated/<boot-id>/` to prevent silent data loss.

## Upstream and Build Rules

### Switch UI Character Compatibility

User-visible text in Companion NRO and Overlay must use glyphs validated on physical consoles with project fonts. Mathematical signs use ASCII `+` and `-`, ranges are phrased as "1 to 120", and status transitions use standard terminology; avoid typographic minus signs, full-width symbols, em-dashes, arrows, ellipses, or middle dots that risk missing glyphs. New special symbols require font validation and updating character test gates. Documentation and web text are exempt.

Upstream investigation order: repository specifications, pinned vendored versions, local upstream checkouts, and finally GitHub. Checkouts at `../libnx`, `../libtesla`, and `../Atmosphere` are optional reference sources, not build dependencies. Libnx does not expose commands 1451, 1454, 1952, 145601, or 195101; parameters and layouts must not be inferred from missing definitions.

### Atmosphère Source Reading Decisions

Verify that an issue belongs to CFW infrastructure before consulting Atmosphère; regular PlayWise business logic does not require it. Before investigating, record the commit, tag, and dirty status per the [Development Environment Guide](DEVELOPMENT_ENVIRONMENT_GUIDE.md), basing findings strictly on tracked code under that commit.

| Issue or Modification | Priority Atmosphère Areas | Answers Provided | What It Cannot Replace |
| --- | --- | --- | --- |
| `boot2.flag`, `atmosphere/contents` scanning, sysmodule autostart failures | `libraries/libstratosphere/source/boot2/`, content-specific flag documentation | Directory enumeration, flag evaluation, custom program launch triggers | PlayWise package gates, boot journals, and hardware reboot verification |
| `pctc:u` registration, service waits, boot order, process lifecycle anomalies | `stratosphere/sm/`, `stratosphere/pm/`, boot2 | Service manager extensions, process launch, and resource management paths | libnx client API contracts, PlayWise IPC persistence, and fallback rules |
| hbmenu shortcut access or `override_config.ini` recovery modifications | `docs/features/configurations.md`, `config_templates/override_config.ini`, `libraries/libstratosphere/source/cfg/` | Configuration keys, button overrides, program matching, address space parsing | PlayWise configuration pre-images, atomic rollback, and parental confirmation protocol |
| Sysmodule OOM, unexpected termination, crash report forensics | `stratosphere/pm/`, `stratosphere/creport/`, `stratosphere/fatal/` | Resource pools and crash handling paths, identifying environment evidence to preserve | PlayWise native logs, support export, recovery transactions, and fault injection |
| Adjusting Atmosphère/HOS qualification baselines | Relevant diffs between tags/commits and `docs/changelog.md` | Evaluating changes in boot2, SM/PM, config parsing, or fatal handling | Physical qualification of candidate release Zips on explicit hardware/HOS/Atmosphère setups |

Atmosphère is not a generic Switch SDK. For libnx wrappers, service dispatching, and version gates, consult `../libnx`; for overlay lifecycle, input, and rendering, inspect vendored libtesla, referencing `../libtesla` as needed; PlayWise tokens, queues, recovery, and stable errors follow repository protocol specifications. Additional PCTL private command constraints are documented in [PCTL Integration Architecture](../设计/PCTL_ARCHITECTURE.md).

Local regression testing:

```text
python tools/test.py
```

The standalone parent web app is compiled deterministically from modular frontend sources; after modifying `tools/ptc_frontend`:

```text
python tools/build_ptc_standalone.py
python tools/build_ptc_standalone.py --check
```

Commits must include updated `tools/ptc_frontend/playwise-offline.html`; Pages deployment and full package builds use `--check` to reject stale artifacts. End users open the generated HTML directly without Python.

Authoritative C, NRO, Overlay, sysmodule, and package verification:

```text
python tools/package_remote.py
```

When preparing a release, run the unified release script in a clean workspace:

```text
python tools/release_version.py <version>
```

When primary features have been manually verified on default hardware and OS versions, pass `--manual-device-verified`; details and overrides are covered in the [Development Environment Guide](DEVELOPMENT_ENVIRONMENT_GUIDE.md#full-switch-build).

The script rejects invalid versions and existing `v<version>` tags, updates `common/version.h` and `common/version.mk` in lockstep, runs full package verification, and creates release commits and annotated tags. After tagging, the patch version automatically increments to the next development cycle (e.g. `1.0.0` to `1.0.1-alpha`) with an alpha commit, ensuring release tags point to clean releases while branch HEAD continues development. `--no-verify` is reserved for testing the release tool in isolation; official releases must never bypass verification or force-move existing tags.

Maintainer container workflows must not be replaced with ad-hoc SSH, `docker exec`, or manual file copying. Other environments must reproduce identical clean, test, build, packaging, and validation gates. Device Lab builds remain internal and are excluded from standard distribution packaging.

## Pre-Merge Checklist

- Are deterministic tests provided for both success and failure paths?
- Do read-only preflight checks complete before setup writes?
- Are pre-images persisted before writing, with bounded confirmation and rollback capability?
- Are nonces consumed only after token write, result file, and ledger commit all succeed?
- Does `disable.flag` continue permitting status queries, support export, and recovery?
- Is the standard installation package unique with all three components sharing a single manifest, and does the complete bundle contain only that package and byte-identical offline HTML?
- Are release binaries free of LAB handlers, runtime control modes, and placeholder secrets?
- Are documents encoded in UTF-8, with physical hardware qualification status explicitly stated?

## Overlay previews and in-app help

Production Overlay view types/colors and read-only drawing methods live in `companion/overlay/source/render_types.hpp` and `render_methods.hpp`. The GUI and `tests/overlay_preview/render.cpp` share them. A C++17 host adapter renders deterministic sample states with the pinned font. `ui-previews` renders entry, confirmation, success, parent actions, bedtime, eye-care and unknown scenes in three languages; the existing conversion and sync pipeline updates Chinese/English documentation. Font/input/PCTL qualification still needs a Switch. Include shared drawing headers in catalog and glyph gates.

Support & recovery provides four read-only FAQ pages, preserving focus on close. Only fresh backend readings inform controls status; clock sync remains a manual check. Reading help must not change PCTL, time or requests.
