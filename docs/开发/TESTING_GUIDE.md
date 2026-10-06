[Documentation index](../README_en.md)

<div align="center">

  [English](TESTING_GUIDE.md) | [简体中文](测试指南.md)

</div>

# Testing Guide

## Config import regression

Run `python tools/test.py` and `python tools/package_remote.py --with-previews` (includes the Eden NRO). Host C regressions cover snapshots, selective merge/defaults, expired dates, PIN/key rotation, calendar restore, atomic overwrite, write/PCTL/readback/result failures and interrupted restart rollback. Check eleven selectors, PIN/hold/cancel/failure flows, eight settings cards and four 90px independent plan cards in three languages and both themes. Eden simulation must preserve use while testing TV-only, exhausted non-TV, combined restrictions and restart. Production package gates must exclude simulation controls.

## TV mode policy acceptance

Deterministic C tests cover sub-minute consumption, conservative transition attribution, stable TV, sleep, restart, day rollover, zero/disabled allowance, first enable, shared unlimited cap, waiver dates, Lite, prerequisites, nonce/buffer preservation and write/result rollback. UI tests cover controller/touch entry and fields, three languages, missing old status, draft retention and immediate-restriction hold gates. The actual renderer produces `dock-*` dark/light previews in all three languages.

Run `python tools/test.py` and `python tools/package_remote.py`; the latter must build and verify the standard Zip and `build/eden-test/pctc-eden.nro`. Add `--with-previews` for visual QA. Eden reads `sdmc:/switch/playwise-eden/operation-mode.txt`: `docked`, `undocked`, or other text for unknown; a missing file defaults to undocked. Changes are sampled on the next second and provide no hardware evidence.

Hardware acceptance must bind candidate Zip SHA-256 and model/HOS/Atmosphere, exercise dock transitions (begin within two seconds), HOME/game/sleep, fragmented use, midnight/restart, edits, waiver expiry, simultaneous total/bedtime/eye-care limits, grants without bypass, restricted Overlay single-use PIN recovery, unavailable readings, write/readback/restore failures, emergency disable and installation snapshot restore. Keep qualification pending until these pass; Host/Eden never substitute for real-device evidence.


## Eye care, bedtime and allowance safety regression

`tests/c/test_control_safety.c` runs through the C host entry point in `python tools/package_remote.py`, followed by standard package and Eden application builds and validation. Coverage includes exact nonce matching, all 512 v2 nonces, real filesystem oversized-ledger expiry cleanup and current-day normalization/deduplication, read failures, damaged tails, duplicate JSON keys, pending transaction isolation and timeout rollback, orphan pending protection, failed rules/state reads preventing writes, short and complete natural rests, recovered usage deltas, midnight cycles and read-only projections, protected recovery under lower new-day quotas, recovery write failures, bedtime result failures restoring snapshots and instance metadata, and BLOCKED targets avoiding 1451. Both memory and filesystem storage reject truncated reads; Device Lab tests read complete reports.

Additional hardware checks: a fresh cycle after the configured natural rest; a 10-minute break starting at 23:58 ending at 00:08; independent new-day allowance rollover; overlay PIN recovery for expired protected breaks while retaining deactivation. Eden and host tests cannot establish official 1440-minute timing, sleep exclusion or actual suspension semantics; candidate status remains pending.

## Three-step setup and diagnostic acceptance

Status confirmation checks only whether Nintendo Parental Controls is enabled. Enabled controls do not require a suspension check; disabled and unreadable controls show distinct guidance and allow continuing setup. Diagnostics retain `parental_control_enabled` as `1/0/-1` and omit `official_pause_setting`. Verify consistent setup, bedtime and eye care wording in all three languages. On step 1, controller and touch can open “Enable Controls Help”, showing Settings → Parent Control settings → Parental Controls settings (restrictions set by a parent or guardian) → I do not have a smart device → Next → Next. Closing preserves step and focus; the dialog blocks the underlying Continue action. Viewing help does not write control status or submit requests, and clock help remains reachable.

`make test-host` includes `test_nro_setup`, which runs the real NRO preferences, wizard, polling and export code against fixed time, memory storage and host libnx/input shims without including implementation files. Coverage includes four languages and glyph refreshes, session choices after failed writes, default PIN errors allowing progress, mismatched PIN preservation, reopening a skipped/completed guide, read-only Support without valid credentials, offline sanitized export, activation pending after 30 seconds with the original request retained, late success and explicit failure. UI C tests cover three-page hit targets, inert collapsed controls, reference matching and safe diagnostic categories.

Run `python tools/test.py` and `python tools/package_remote.py --with-previews`; the default Eden NRO must still build and validate. Inspect all three pages in both themes and all three languages, expanded preferences, time help and error paths. Hardware checks cover system language, enabled/disabled/unreadable parental controls, current firmware, offline Support/export, touch and controller navigation. Host/Eden do not establish PCTL evidence.

## Eye care candidate checks

Verify default calendar positioning during holidays, between holidays, on makeup days, across years, with future-date ties, empty imports and unreadable data. Import help and full URLs must remain visible with both populated and empty lists in all three languages and themes. Today's skip-break card must match neighboring widths; its former right half must not activate it. The first reliable status read must provide a playing preview; missing readings, disabled timers or mismatched settings stay unknown. Status projection must neither write PCTL nor start timers. Full calendar format: https://github.com/selfuppen/NX-PlayWise/blob/main/docs/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md

During a break, verify `daily_allowance:false` and `eye_care:true`, no false quota exhaustion in today's status or `X Details`, the break and cycle in the left preview, and the cycle in the persistent header. The temporary PCTL block must not disable skipping the current break. The Time Plan eye care switch should appear as a capsule slider with the same controller and touch action.

Host regression should cover default-off legacy rules, the 1–240/1–60 bounds, 40/10 cycles, unchanged usage during sleep, breaks spanning sleep and restart, day rollover, daily and bedtime priority, immediate policy changes, the 1440-minute unlimited mapping, current and stale `break_id` skips, and PCTL write, readback, and recovery failures. Run `python tools/test.py` and `python tools/package_remote.py`; the latter builds and validates `build/eden-test/pctc-eden.nro` by default.

> [!NOTE]
> Passing `python tools/package_remote.py --only eden` confirms UI layouts, controls, and local algorithms, but because Eden runs in-process with mock PCTL stubs, it cannot validate real cross-process IPC (`pctc:u`), genuine Horizon PCTL enforcement, or the production Sysmodule and Tesla Overlay. Eden testing does not substitute for the standard production build gate (`python tools/package_remote.py`).

Host UI regression should also check that the Time Plan Eye Care card badge follows its own switch regardless of the autonomy buffer, that ±1/±10 minute touch controls are distinct and reachable, and that Today's Schedule has eight navigable, touchable cards. Check the skip and TV waiver actions and disabled reasons, plus the details preview for minute-level use, break countdown, paused, unknown, stale, and elapsed states in all three languages and both themes. Verify the TV waiver under emergency disable and in the Overlay parent area. With 30 total and 180 non-TV minutes, usage must stop at 30, including when docking or waiving today's TV rules. Cover both weekly and today-override total sources. The seven-day forecast restores 44px rows at 51px spacing with matching touch and nearest-neighbor navigation.
Focused previews are named `eye-care-resting-*`, `parent-details-eye-care-playing-*`, and `parent-details-eye-care-resting-*` under `build/ui-previews/parent/`, `en/parent/`, and `zh_hant/parent/`.

For hardware A/B, record package hash, HOS/CFW, game, enabled Nintendo Parental Controls, and PCTL configured/remaining readings before and after awake HOME use and sleep. Compare original unlimited mode with the temporary 1440-minute mode; verify sleep does not consume use time. Observe actual software pause and resume when a break begins and ends, PIN-authorized overlay and Today's Schedule skips, handoff to daily allowance and bedtime restrictions, and restoration of the original unlimited rule after disabling. Check that an expired `break_id` after status refresh prompts a new look instead of releasing the wrong break. Keep the candidate `pending` until this evidence is complete.

## Console Application Visual Refresh Acceptance

The Companion NRO's light, dark, and follow-system themes use a unified semantic color palette covering both homepages, five parent tabs, secondary settings pages, first-time setup, and all modals. The visual refresh does not alter the web frontend, Overlay, Device Lab, or the control protocol. `python tools/test.py` provides fast local regressions, while authoritative C/Python/UI, text catalog, and package gates are executed via `python tools/package_remote.py`.

- **Palette Testing**: Verify that both light and dark themes maintain at least 4.5:1 text contrast and at least 3:1 focus and control border contrast against all background layers. Focus specifically on primary buttons, the main time card, and success, warning, and error banners.
- **Touch Testing**: Verify inner and outer hit boundaries of primary controls, button spacing, submission blocking during synchronization or disabled states, and modal dialogs preventing click-through to background elements. All visible "+ Save / Complete Input" buttons must trigger the same action as the physical controller `Plus` button. Controller navigation retains existing pagination, selection order, and shortcuts.
- **Unified Gate**: Uses the vendored Noto Sans SC font in the repository to generate structured preview subdirectories under `build/ui-previews/` (including `child/`, `parent/`, `grant/`, `redeem/`, `plan/`, `holiday/`, `scheduled/`, `bedtime/`, `autonomy/`, `settings/`, `support/`, `setup/`, `matrix/`, and `error/`). `matrix-overlay-01` through `matrix-overlay-41` cover all modal dialogs (including standard bedtime configuration and Eye Care time editing); indices 0 to 5 of `matrix-child-state-*` and `matrix-parent-state-*` represent unknown, zero quota, unlimited, expired, syncing, and maximum quota states respectively. Dedicated previews also cover today's adjustments active/cleared/unset, both tabs of detailed status, weekly plan changed/overridden/hazardous/unknown confirmation, holiday confirmation, bedtime main page/error feedback/scheduled date modes, autonomous grace buffer, and child claimed buffer states across light and dark themes.
- **Visual Inspection**: Check 1280x720 layout, title and digit hierarchy, text wrapping, focus margins, button boundaries, and feedback regions for all previews. QR codes must always render black-on-white; secrets remain masked per existing security rules. All previews are explicitly watermarked with `HOST PREVIEW / SAMPLE DATA`.
- **Physical Console Acceptance Needed**: Handheld touch controls, controller navigation and long-press confirmation, TV viewing distance readability, shared OS font rendering, immediate theme switching, and UI refresh smoothness during timer/redemption processing. Host previews and emulators cannot substitute for these checks, nor do they serve as PCTL qualification evidence.

Testing is split into deterministic host regression, devkitPro container builds, and physical device qualification. Standard distribution builds run automated tests covering full safety and recovery capabilities, but the console does not execute cyclic write/rollback self-tests.

## Local Regression

```bash
python tools/test.py
```

A passing run concludes with `PASS: local tests`. This entry point covers:

- Token v1/v2, 8-character codes, calendar dates, signatures, replay prevention, nonce submission ordering, and cooldown periods;
- Request/result schemas, atomic request queue, IPC deduplication, and stuck request recovery;
- First-time setup pre-flight check, installation snapshot, 5-second grace period, compatibility states, and recovery priorities;
- Standard PCTL transactions, failure injection, rollback, and result/ledger failure handling;
- Scheduled date quota 1/366-day boundaries, cross-week/month/year rules, priority hierarchy, and 7-day forecast; daily autonomous grace buffer disable/unlimited rejection, one-time claiming, day transitions, 1440-minute truncation, and PCTL/state/activity/result failure rollbacks;
- Family activity log capped at 200 entries, clear-history failure rollbacks, and offline code nonce preservation upon activity write failures; daily summary reliable balance, 7/30-day unique date aggregation, missing-date non-interpolation, future/duplicate/out-of-order records, and clock-rollback tolerance;
- Standalone read-only statistics adapter fixed to unavailable prior to evidence gate clearance, strictly scoped to local console without synthetic game entries;
- Enforce deduplication, `applied_pending_confirmation`, 30-second confirmation/rollback window, exact settings read-back submission when played time exceeds recovered weekly quota with 1455 already restored to false, UI/diagnostic retention of 306 error real runtime snapshots, and separation between active recovery and boot legacy transactions;
- PIN, rules, and setup template idempotent initialization, data retention across direct overwrite upgrades, and diagnostic sanitization;
- Standard hot-reload identity verification, journal phases with `.tmp` atomic recovery, empty flag staging/restoration, unknown backup/conflicting journal error handling, and static lifecycle contract forbidding process termination and dual-daemon concurrency;
- Isolation of contracts across standard package, complete package, and Device Lab, `package-artifacts.json` component sizes and SHA-256 hashes, and package helper failure branches;
- `version.h` and `version.mk` consistency, PWA official links, and external link security attributes;
- Single-file offline page consistency with modular source, inline CSP, zero runtime network dependencies, Web Crypto/built-in HMAC test vectors, and graceful degradation without persistent storage.

Tests must use fixed timestamps, deterministic fixtures, and host mock adapters without depending on real Switch system services.

## Authoritative Container Verification

Whenever C, C++, Makefile, NRO, Overlay, sysmodule, or packaging logic is modified, run:

```bash
python tools/package_remote.py
```

The script cleans existing build artifacts in a single SSH session, executes C host, UI, and Python regressions, compiles the three standard distribution binaries, and generates the standard install package and complete delivery package.

Distribution gate assertions:

- `build/packages/` contains only the current version's standard install package `playwise-v<version>.zip` and complete delivery package `playwise-complete-v<version>.zip`;
- Standard install Zip contains only standard distribution sysmodule, NRO, Overlay, `boot2.flag`, `build.json`, `package-artifacts.json`, immutable `defaults/*.json`, and empty runtime directories. It must not contain the six mutable root seeds, installers, or the offline web page; the manifest must match exact sizes and SHA-256 hashes of the three runtime binaries;
- Device Lab Zip retains the `atmosphere/contents/4200000000BD23F0/flags/` directory entry without including `boot2.flag`; first-time long-press toggle must atomically create the Lab flag even if the directory does not initially exist. When simulating an interrupted state with `standard_disabled` journal, standard flag staged, and Lab flag not yet created, Lab NRO does not connect to `pwtl:u` and displays recovery, while standard NRO does not connect to `pctc:u` when its flag is missing; neither may black-screen;
- Complete delivery Zip contains only the byte-identical standard package and `playwise-offline.html`, rejecting missing, duplicate, extra, path-traversal, or inconsistent entries;
- `build.json` in the Zip matches the externally generated release manifest byte-for-byte with profile `release`;
- All three binaries embed the same manifest; NRO and Overlay display the current build version; NRO embeds repository and PWA links;
- No LAB handlers, non-standard protocol requests, runtime control modes, `capabilities.json`, or placeholder secrets;
- Never seed `credentials.json`; first boot must generate a unique random secret; missing runtime files are created atomically from `defaults/`; existing files are never overwritten even if invalid;
- Merging the new package over an existing installation or using `tools/install_package_to_sd.ps1` / DBI MTP `tools/install_package_via_dbi_mtp.ps1` replaces binaries and manifest while preserving credentials, PIN, rules, state, ledger, logs, and backups byte-for-byte; installer scripts are not included in the runtime Zip. DBI MTP script rejects NSP/XCI installation paths and verifies SHA-256 read-backs via MTP;
- The Switch state adapter must first query `1455 restricted_now` via `pctl`; only when unavailable may it fall back to a short-lived `pctl:s` session reading private settings. It must never treat unknown as `false` due to primary query failure, nor skip runtime confirmation and rollback after direct writes.

Manual SSH, `docker exec`, or copying files manually must not replace this entry point.

## Standard In-Place Overwrite and Hot Reload

Host C/UI/package tests cover: matching and non-matching `release_id`, target profile/PID/boot ID mismatches, parent confirmation consumed only once, non-pending states without hidden buttons, no triggering buttons in child mode, pending states blocking other configuration changes, active recovery blocking reload, handover timeouts, source PID not exiting, flag/backup conflicts, journal phase recovery, missing/mismatched components, and at most one retry on missing target. Source code contracts must verify absence of `pmshellTerminateProcess`, proving source PID exit and flag restoration precede target process launch.

Physical console verification on OLED / HOS 22.5.0 / Atmosphère 1.11.2 using package tied to candidate commit hash:

1. Close NRO and Overlay with console powered on; overwrite `atmosphere` and `switch` using DBI/MTP, FTP, or USB.
2. Re-open NRO; verify child area only prompts "Enter Parent Zone to Resolve"; authenticating PIN presents hot-reload confirmation; canceling allows re-entry from "Software Info".
3. Upon confirmation, record old/new PID, release ID, and boot ID; concurrent requests during handover return `QUIESCING`; UI continuously displays phases 1/4 to 4/4 and blocks other settings submissions.
4. Upon completion, verify configuration, PIN, secrets, rules, nonces, balances, history, and recovery data are intact; open Overlay and confirm automatic reconnection to new IPC service.
5. Interrupt NRO at `prepared`, `boot_disabled`, `boot_restored`, and `target_launched` stages; verify empty flag restoration, current background daemon startup, and single PCTL owner invariant.
6. Inject unsupported quiesce, source process failure to exit, target identity mismatch, and target launch failure. The old process must remain active while still present; post-exit failures must preserve empty boot flags, prompt for a full reboot, and never auto-reboot or force-terminate.

Card reader removal/re-insertion requires shutdown and reboot, which is outside hot-reload acceptance. Device Lab remains independently tested per the next section.

## Device Lab Isolated Qualification

Historical assumption: the existing campaign retains `original_pause_state` and suspension ON/OFF experiment slots. These do not establish a separate suspension switch on the current console. Standard status confirmation requires enabled Nintendo Parental Controls. Do not fabricate legacy field values or claim campaign qualification when hardware cannot establish prerequisites; record continued, suspended, or exited software behavior as observed.

Developers run in the devkitPro environment:

```bash
make device-lab-package
```

Verify:

- Output resides only in `build/device-lab/`, never in `build/packages/`;
- Title ID is `4200000000BD23F0`, IPC service is `pwtl:u`, SD directory is `switch/playwise-device-lab`;
- Manifest profile is `device-lab`;
- NRO features a Chinese 3-step card wizard recommending "Enable Lab Daemon", "Continue Overlay Forensics", "Restore Standard Daemon", or recovery based on boot journal, Lab service, session, and reports; all boot flag toggles require holding ZL+ZR+A; page refreshes during recovery waiting; boot flag toggles use persistent journals, execute idempotently, and avoid overwriting unknown files on conflict; host tests simulate Switch `rename` non-overwrite and journal recovery across existing/clean/corrupted `.tmp` states;
- First launch of NRO without Lab boot flag enabled does not query `pwtl:u`, rendering the home screen immediately; Lab Overlay uses persistent SD queue from boot, preventing blocks if the daemon service has not yet registered;
- Overlay uses a fixed dark Chinese interface offering "Focused Restriction Retest", "Timer Activation A/B (7 Stages)", and "Advanced Full Forensics (6 Stages)"; displays mode, stage progress, next action, countdown timer, auto-verdict, restoration status, and report path. General operations, 2-tier manual observation, and recovery support touch; restriction-writing stages strictly require holding physical controller A for 2 seconds; Simplified Chinese system shared font is enforced even under non-Chinese system locales;
- Zip contains `switch/playwise-device-lab/playwise-device-lab.nro`, `switch/.overlays/playwise-device-lab.ovl`, Lab sysmodule, and device-lab manifest;
- Package does not contain `boot2.flag` by default;
- `make packages` neither builds nor bundles any LAB artifacts.

### Guided Console Forensics

Qualification baseline is Nintendo Switch OLED, HOS 22.5.0, Atmosphère 1.11.2, with Tesla/Ultrahand installed. Configure an active non-empty schedule in Nintendo Parental Controls with at least 10 minutes remaining; prepare two non-critical games that tolerate brief interruptions without unsaved progress:

1. Install Device Lab Zip and launch `playwise-device-lab.nro`. Confirm the home screen shows "Prepare to Enable Lab Daemon" and hold `ZL+ZR+A` for 1 second. NRO checks the source process read-only via `pm:shell`. When seamless handover is possible, it transitions without rebooting; otherwise, it configures boot flags and requests a single reboot. Follow on-screen guidance on conflicts.
2. Open `playwise-device-lab.ovl` from Tesla/Ultrahand. Default selection is "Qualification Campaign". Record the legacy `original_pause_state` experiment field (see the historical assumption note in this section). Free focus, Timer A/B, and Advanced Forensics remain individually selectable. When the campaign starts, Overlay displays 4-step progress, anonymous games A/B, next action, countdown, verdict/recovery status, and report path; pressing `Minus` expands machine values, request IDs, and error codes.
3. Focused mode enters `restriction_effect` directly. Full mode executes `home_stopped`, `home_started`, `game_foreground`, `game_suspended`, `sleep_wake`, sampling each for 75 seconds. If the initial 0x44 buffer is all zeroes, stages 3–5 will warn of insufficient baseline prerequisites.
4. Under a safe game, enter "Observe Time Restriction Effect", verify no unsaved progress, and hold physical controller `A` for 2 seconds (touch only displays safety warnings). Close Overlay and observe within ~15 seconds whether time restriction prompts appear and whether the game continues, pauses/suspends, or exits. The background daemon auto-recovers without requiring Overlay to stay open. Re-open Overlay to record prompt visibility, then record actual game behavior.
5. Only when Overlay displays `exact_restore_proved` is recovery considered proven. If `restore_required` is shown, press `A` immediately to retry; Lab `disable.flag` blocks all subsequent forensic writes.
6. Before manual observations are submitted, only `lab/report-<run-id>.draft.json` is generated. Once final and `exact_restore_proved`, proceed to the next item. The four steps are Timer A/B, Game A with Suspend ON, Game B with Suspend ON, and Game B with Suspend OFF. Verify original Nintendo Parental Controls settings upon completion and retain four accepted reports plus `reports/<campaign-id>.campaign.json`. Closing Overlay or sleeping does not discard progress. Finally restore the standard daemon from NRO.

A/B mode requires an active Nintendo schedule with >=10 minutes remaining. HOME must stay illuminated for >=90 seconds; standby must last >=90 seconds, reopening Overlay immediately upon wake. Execute settings-only targets for restricted, extended, and unlimited in sequence. Reports record pre-state, settings-only results, fallback invocations, and readouts; correlation must not be reported as causality. If screen illumination time cannot be ruled out after wake, standby conclusions must be marked `inconclusive`.

Fault injection covers SD mount/mkdir failure, `pm:shell` unavailable, source process refusing exit, NRO interruption, target launch failure, stale/mismatched runtime-ready status, recovery timeout, missing reports, failure error codes, corrupted `session.json`, v1/v2 journal interruptions, and unknown flag/backup conflicts. Assert no second PCTL owner is ever started.

Reports must adhere to `schema_version:2` (retaining `version:2`), containing raw nanoseconds/booleans and Results for `1453/1454/1455/1952`, full 0x44 hex/hash before and after, `1006/1031/1035/1457/1458` raw vs libnx comparison, target-bound fallback, restriction event auxiliary evidence, environment/build identity, and byte-for-byte recovery proofs; focused/full modes include 2-tier manual observations.

## PDM and Bedtime Evidence Gates

Before per-game statistics can be released, standalone read-only `pdm:qry` experiments must execute A/B runs across HOME, foreground game, suspended, standby, day transition, manual clock change, multi-account, and unknown titles. Reports must prove deduplication, reordering, missing events, and clock rollback degradation semantics, confirming statistics represent local console usage. If evidence is insufficient, the Release adapter remains unavailable and cannot display today's/7-day Top 3. The experiment must never invoke PCTL `1952`, write rules/state, or alter control states.

Bedtime is shipped disabled by default in standard packages and requires Device Lab evidence for qualification. Qualification gates must prove: triggers occur within 60 seconds of schedule arrival or within-window wake; modals block Game, Homebrew, HOME, System Settings, and PlayWise NRO; Overlay remains operable, sharing NRO PIN/attempts/cooldown, executing one-time authorization, skipping current window, disabling bedtime, or performing full install snapshot restoration. Each recovery action must submit a request, re-read PCTL, and dismiss modals; if daily quota remains exhausted, it must display "still restricted by daily quota" rather than misreporting failure or clearing all limits.

Standard extensions and autonomous grace buffers must be rejected within the bedtime window without consuming nonces/eligibility; balance recovery on the same day, new quotas on day transition, persistence across reboot, immediate recalculation on clock changes, entry/exit write failure handling, and recovery whitelisting under `disable.flag` must be verified. When the daemon is unreachable, Overlay displays "Request Unconfirmed" and external recovery instructions, and `restore_install_snapshot.flag` must not appear. If Overlay is missing or unhandshaked, standard NRO must require confirming Nintendo Parental Controls is enabled with clear risk warnings before first enablement.

Bedtime time boundaries must be verified on a physical console with official Nintendo Parental Controls enabled. Status confirmation checks only whether Nintendo Parental Controls is enabled. Record restriction prompts and actual software behavior without requiring a separate suspension switch. PlayWise PIN cannot replace official Nintendo PIN. Adjusting system clock across boundaries with official PIN must trigger daemon logging and immediate re-evaluation; the report must note that v1 does not maintain a trusted clock nor prevent clock-manipulation bypass.

## Qualification Verification

Target baseline:

- Nintendo Switch OLED;
- HOS 22.5.0;
- Atmosphère 1.11.2.

Authoritative qualification commands:

```powershell
python tools/test.py
python tools/package_remote.py
python tools/make_bedtime_qualification_template.py `
  --commit <candidate-commit> `
  --release-id <candidate-release-id> `
  --output .\device-reports\bedtime-qualification.json
.\tools\prepare_device_lab_sd.ps1 -SourceFolder .\build\packages -Drive E -WipeAll
.\tools\prepare_device_lab_sd.ps1 -SourceFolder .\build\packages -Drive E -WipeAll -Apply
python tools/verify_device_qualification.py `
  --packages .\build\packages `
  --reports .\device-reports `
  --expected-model oled `
  --expected-hos 22.5.0 `
  --expected-atmosphere 1.11.2
python tools/promote_qualified_build.py `
  --packages .\build\packages `
  --verification .\build\qualification\verification.json `
  --out .\build\qualified
```

The SD script defaults to dry-run; verify drive letter and console backup directory before `-Apply`. Device Lab qualification input includes the complete historical campaign plus the final `bedtime-qualification.json`. Template `null` values must be replaced by real console observations. Promotion copies original verified Zips matching calculated hashes without recompiling.

The standard package must then execute the 17-step verification workflow:

1. Install on a console with official time limits enabled and reboot.
2. Confirm initial read-only pre-flight check succeeds with zero PCTL writes.
3. Create a PIN of chosen length, confirm generated secret is non-placeholder, and confirm 7-day schedule.
4. Confirm handover, verify install snapshot, clear current restriction, 5-second sync, and Active state.
5. Test status, daily total quota, quick grant fixed/custom values, unlimited today, clear daily adjustment, and weekly schedule saving; confirm clearing only removes `today_override` without affecting scheduled dates or bedtime skips. Temporarily unlock from Nintendo system settings, confirm limits and quotas persist, and verify restriction resumes after sleep/wake.
6. Redeem a candidate-generated 15-minute v2 grant code, confirming timer, remaining balance, and nonce commit exactly once.
7. Inject state sync latency, verify "Syncing" banner, and confirm resolution or contract rollback within 30 seconds.
8. Reboot to verify setup, rules, state, ledger, and in-flight transaction recovery.
9. Create `disable.flag`, confirm write kill-switch, verify diagnostics and install snapshot recovery.
10. Restore pre-install snapshot, verify exact raw/timer state restoration, and enter `restored` state.
11. In `restored`, verify Step 3 shows "Re-enable and Retake Control"; confirming runs pre-flight, deletes `disable.flag` on success, enters `active` after 5 seconds grace, or preserves disabled flag and `restored` state on failure without overwriting snapshot.
12. Inject Enforce propagation delay, verify scheduler in same process does not misidentify active recovery as boot legacy transaction within 30-second window; reboot within window, verify legacy transaction recovery, `disable.flag` creation, and safe recovery from `active` via "Re-enable and Retake Control".
13. Set next day to 60 minutes, leave console on HOME before day-end and sleep; keep screen and game off for >=90 minutes on next day. Verify `pctl_debug.jsonl` contains only Enforce `apply_target` without `stage:"start_timer"`, 1454 balance does not decrease from background day sync, and UI does not display false 60-minute quota consumption.
14. Record 1454/1952 A/B across HOME, System Settings, foreground game, suspended game, standby, and wake. Confirm HOME/Settings screen illumination counts toward official Nintendo usage semantics, standby does not accumulate, and wake resumes accumulation.
15. Configure scheduled date quotas across week, month, and year boundaries; verify 7-day preview and priority hierarchy "Today Adjustment > Scheduled Date > National Holidays > Weekly Plan"; rules persist across reboot.
16. Claim autonomous grace buffer once via controller and once via touch on limited days, verifying second attempt rejection, day transition reset, and 1440-minute truncation; confirm eligibility and PCTL pre-state recovery under failure injection.
17. In light and dark themes, verify family activity pagination/clear, 7/30-day reliable days, child zone today/tomorrow rule sources; verify statistics failures or corrupted logs do not impact time control.

## Failure Forensics

Export sanitized diagnostic bundles from the Support page. If NRO fails to boot, preserve:

- `build.json`, `setup.json`, `compatibility.json`, `environment.json`;
- `backups/install_pctl_snapshot.json`;
- `recovery/active/`;
- `results/`, `logs/`, and relevant crash reports.
- When Overlay or NRO reports SD transport, inspect `logs/YYYY-MM-DD/ipc-client.log`; verify entries contain `client`, `stage`, `rc`, `module`, `description`, `version`, and `expected`, without logging request bodies, PINs, secrets, or 8-character codes. A working connection records `client=overlay stage=connected` and displays "Transport: IPC".
- Open Overlay under normal, quota exhausted, bedtime active, and bedtime lifted but daily quota exhausted states: always displays grant code page first; `L` only submits buffer when available and not under bedtime; `R`/touch enters PIN page, `-` expands status; codes can be edited but not submitted during bedtime. Bedtime page allows skipping or restoring current window via `Y/X` and touch. PIN page supports joystick 8-direction `1`–`8`, deadzones, D-pad, `X/Y` for `0/9`, `ZL` backspace, `+` confirm, and `B` cancel. Touch supports backspace, confirm, and cancel only.

Never transmit `credentials.json`, `auth.json`, full ledger, or raw offline codes. To stop auto-boot, remove `atmosphere/contents/4200000000BD2300/flags/boot2.flag`.

## Console Home Visual Regression

- **Parent Dashboard Layout**: The first four cards on the right correspond to "Today Quota Adjustment (Today Only)", while the last two correspond to "Bedtime & Autonomous Buffer". Both light and dark themes must maintain non-overlapping focus/touch hitboxes, and group headers must be non-interactive. Unlimited today must clearly state that bedtime remains independently enforced.
- **Child Dashboard**: Remaining time readable at a glance; `A` one-step input, `X` buffer, `Y` refresh, `B` exit, and parent shortcut combo retain existing behaviors. Check buffer disabled, available, claimed, and deactivated states.
- **Parent Schedule Page**: Six unified action cards: Today Quota, Quick Grant, Unlimited Today, Clear Today Adjustment, Skip Current Bedtime, and Autonomous Buffer. "Skip Current Bedtime" acts as a quick shortcut to skip the current or next upcoming window once. Today Quota card covers active, cleared in session, unset, pending, unconfirmed, disabled, recovering, and temporary unlock states. Quick grant fixed and custom values require confirmation via "Current Balance -> Balance After Operation" summary.
- **Unlimited Today Interaction**: Quick grant card is grayed out, displaying reason and re-enable prompt; neither `A` nor touch may initiate status refresh or grant requests. When bedtime is disabled, has no upcoming window, or current window is already skipped, "Skip Current Bedtime" is grayed out. Stale states (>120 seconds) must refresh first before determining button availability.
- **Modals and Editors**: `+` and touch open details; parent details default to "Today Decision", switchable via touch or `L/R` to "Usage & Status". Decision chain covers all 4 rule levels, bedtime window, and buffer status. Today quota editor supports `ZL` limited, `ZR` unlimited, and touch mode toggle; unlimited mode rejects minute input and submits unlimited request. Bedtime page verifies D-pad navigation, `A` toggle, `-` shortcut, touch, and `L/R` tabs; shared status bar prompts `Y` at 120/121-second boundary.
- **Preview Generation**: Console layout previews use vendored `third_party/fonts/noto-sans-sc/NotoSansSC-Regular.ttf` with a fixed sample clock (08:16). To streamline frequent development builds, automated preview rendering is decoupled from default test and packaging passes: `make test` and `python tools/package_remote.py` omit preview rendering by default. When running `python tools/package_remote.py --previews` (or `--only previews`, `--with-previews`, `--release`), it runs host preview rendering and synchronizes images via `tools/sync_doc_previews.py` to `docs/images/usage/` and `docs/images/usage-en/`. In incremental mode, `ui_preview` verifies existing PPM contents before writing to skip redundant mount I/O and preserve timestamps, while `tools/convert_ui_previews.py` performs mtime-based incremental skipping and multi-process parallel PNG conversion via `ProcessPoolExecutor`, reducing repeated preview runs to sub-second durations. To only regenerate previews, use `python tools/package_remote.py --previews` (or append `--clean` for a full clean rebuild).

## Console UI Details and Curve Rendering Regression

- Bedtime full editor covers 7-day independent load/save, weekday/weekend copying, calendar toggles, statutory holiday/workday rules, scheduled date 1/366-day boundaries, `HHMM` direct input, 15/60-minute steps, overnight and adjacent overlap rejections.
- Bedtime skip covers current, tonight, future, already skipped, no schedule, stale status (>120s), PIN error/cancel, and stale daemon instance rejection. The confirmation modal displays the calendar date and exact time of the locked instance.
- Detail pages use key statistics and grouped cards; parent zone supports `L/R`, D-pad, and touch switching between "Today Decision" and "Usage & Status".
- Weekly schedule, national holidays, and bedtime top-level editor pages hide top-level tabs; `B` returns to the 5-card hub and restores source card focus. Support & Recovery is directly reachable via `L/R`.
- `test-host` runs `ui_preview --check-primitives` without fonts, validating anti-aliased rounded corners and rings, mirror symmetry, designated radii, stroke clipping, and 4x4 coverage sampling.
- Unified package gate generates real renderer previews: inspect `details-*`, `parent-details-*`, `child-details-*`, `settings-advanced-*`, `support-*`, and all modal matrices.
- Open console application and Overlay under System Traditional Chinese, Manual Simplified Chinese, Manual Traditional Chinese, and English; verify titles, decision reasons, plan save results, PIN error/cooldown countdowns, and confirmations use the selected locale without glyph corruption or clipping. `tests/devkit/test_ui_text_catalog.py` verifies all keyed messages across all three language catalogs.
- Weekly quota set to 1, 14, 15 minutes saves, persists across exit/re-entry, and rejects 0 or 1441 minutes.
- Conclude by running `python tools/test.py` and `python tools/package_remote.py`; physical console verification further inspects handheld and docked display edge rendering, controller/touch navigation hierarchy, and overall interaction smoothness.

Calendar UI regression: verify B back, Y refresh, X format help, L/R lists, ZL/ZR paging and matching touch buttons. Help must block touches on the underlying list and preserve selection/page when closed, including during refresh. Verify visible back/refresh geometry on every plan editor and the equal-width eye care skip/save buttons without footer overlap.

## Overlay previews and troubleshooting regression

Run `python tools/test.py` and `python tools/package_remote.py --with-previews`, including standard package and default Eden validation. Each language gets ten 448x720 production Overlay scenes. Support previews cover four help pages plus unknown, disabled and stale parental controls in light/dark themes. Run `python tools/sync_doc_previews.py --check` to check documentation freshness.

Host checks cover guide navigation with zero/three events, touch paging, wraparound, focus preservation, emergency-disable access, input blocking underneath and inactive-guide navigation. Inspect long text, page numbers, check status and buttons in all three languages/themes. Time sync is manual; unknown controls must not appear off. Verify controller/touch and unavailable-backend help on a Switch; manually refresh after DBI/QuickNTP sync and retest restrictions with a non-critical game. Host previews do not prove PCTL effects.
