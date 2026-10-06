[Documentation index](../README_en.md)

<div align="center">

  [English](PROTOCOL.md) | [简体中文](协议.md)

</div>

# Protocol Specification

## Configuration backup and selective import

The fixed production path is `sdmc:/switch/playwise/backups/config-backup.json`; Eden uses its isolated root. Version 1 uses `format:"playwise-config-backup"`, `created_at` (Unix seconds), `source_device`, and `files`. Each entry carries an allowlisted relative `path`, lowercase 64-character `sha256`, `exists`, and a JSON text `content`. The allowlist is config/rules/auth/credentials, calendar catalog/active, and their referenced library documents. Limits: 134 files, 16384 bytes per file, archive below 4 MiB, existing calendar validation and 64-entry catalog limit. Duplicate keys/paths, invalid types/ranges, traversal, unsupported versions, changed hashes and missing references are refused before modification.

Backups include all rules, UI preferences, PIN hash/salt, device ID, pairing URL, grant secret and saved calendars. They exclude PCTL snapshots, environment qualifications/confirmations, setup authorization, external hbmenu config, runtime state, logs and replay ledgers. Keep the archive private: it contains the grant key.

| ID / type | payload | Behavior |
| --- | --- | --- |
| 49 / `create_config_backup` | `{}` | Backend serializes a consistent request-owned stage; NRO assembles and atomically saves it after PIN verification and overwrite confirmation |
| 50 / `restore_config_backup` | `{"groups":511,"stage_id":"request-id","sha256":"64-char digest"}` | NRO validates and splits the archive; backend rechecks it, copies to private transaction input, and rechecks the manifest digest |

The eleven bits are weekly 1, scheduled 2, today 4, buffer 8, holidays/calendars 16, bedtime 32, eye care 64, TV rules 128, UI preferences 256, PIN 512, pairing 1024. Default is 511 excluding expired today; all is 2047. Unselected values stay unchanged. Old missing rule fields use defaults. Today requires the source date to match this console. Requests/results contain no PIN, key or theme fields.

Both actions verify the current PIN; import requires a hold after previewing timestamp, source, selections and today's effect. Selecting PIN requires proving knowledge of the source PIN and ends the parent session on success. Selecting pairing always generates a new random grant key, restores the device ID/URL, and requires phone re-pairing. Without pairing selected, the local key is preserved. Played time, non-TV usage, buffer eligibility and the current replay ledger remain local.

Selected files join the existing persistent recovery transaction, PCTL application/readback, result commit and startup recovery. New calendar files are journaled before publication; existing files are never overwritten. Failure rolls back all preimages/additions; failed rollback keeps recovery materials and enters protection. A durable result commits the transaction. Backend processes files separately within its 512 KiB heap. Stages are cleaned by their owning request. Recovery/hot reload blocks import. Restrictions require local qualification and Nintendo-control/overlay confirmation; source authorization is never accepted. Success reloads rules, preferences, theme, language, audio and pairing and invalidates backend caches.

`507 config_backup_invalid` rejects malformed/unsupported/out-of-range/incomplete archives; `508 config_backup_changed` rejects a changed preview digest. Existing storage, PCTL, qualification, busy and recovery errors still apply.

The Time Plan independent column contains bedtime, eye care, TV rules and buffer, each 90px. Public modes are TV/non-TV (handheld and tabletop); wire values remain `docked/undocked/unknown`. Only Eden exposes simulation buttons, atomically writes its isolated `operation-mode.txt`, notifies the existing mode provider, and preserves usage and mode across restart. Production has no simulation handler or buttons.

## TV mode policy (candidate: pending)

Optional rules v2 fields are `force_docked=false`, `undocked_limit_enabled=false`, and `undocked_daily_minutes=30` (0–1440). Missing fields keep old installations disabled. A disabled allowance is different from an enabled zero-minute allowance. `PtcRules.dock_policy` contains these three fields.

The read-only platform provider reports TV (`docked`), handheld/tabletop (`undocked`), or `unknown`, plus TV support availability. Switch uses `ommGetOperationMode` with `omm` service permission; Lite rejects force-docked, but accepts an undocked allowance. No 1952 calls are added. Enabled policies sample every second; mode changes aim to begin enforcement/restoration within two seconds. Writes remain serialized and require a target change or mismatched readback.

Undocked allowance is a ceiling within the daily total. Docking and a today-only waiver remove only the dock reason; total allowance, bedtime and eye care still apply. Unlimited today, grant codes and self-buffer do not waive TV mode rules. A dock-blocked grant returns 326 before consuming a nonce or buffer eligibility.

Accounting uses reliable configured minutes converted to nanoseconds minus 1454 remaining nanoseconds, preserving nanosecond precision and exposing whole-minute estimates. Initial enable starts from that point; sleep is never charged by wall time. A consumption interval with either endpoint not confirmed as TV, a restart gap, or a recoverable read interruption is conservatively charged as undocked. Unprovable gaps, timer resets and configuration mismatches remain unknown. Unlimited days use the shared 1440-minute eye-care timer cap until both features stop requiring it. Temporary BLOCKED settings are never interpreted as base consumption.

Runtime fields: `dock_day_index`, `undocked_used_ns`, `dock_last_used_ns`, `dock_baseline_known`, `dock_usage_known`, `dock_tracking_started`, `dock_interval_unknown`, `dock_last_mode` (0 unknown, 1 undocked, 2 docked), `dock_waived_day_index`, `dock_waived`, `dock_enforced`. Restart, edits, switches and waiver preserve today's use; a new local day starts a new count. Observed use is committed before control transaction preimages. `backups/dock_pctl_snapshot.json` participates in rollback. Full installation restore disables dock policy and clears active baseline metadata while retaining historical use.

| ID / type | Payload |
| --- | --- |
| 47 / `set_dock_policy` | `{"force_docked":false,"undocked_limit_enabled":true,"undocked_daily_minutes":30}` |
| 48 / `waive_dock_policy_today` | `{"expected_day_index":2380}` |

First enable reuses environment-bound Nintendo parental-control and recovery-overlay confirmation. Waivers use single-action parent PIN authorization in NRO/Overlay and reject stale local dates. Errors: 326 `dock_active`, 327 `dock_mode_unavailable`, 328 `dock_confirmation_required`, 329 `dock_date_mismatch`, 330 `dock_unsupported`. A recovery waiver is allowed under disable.flag while preserving the flag and other restrictions; failed saves, writes, readback or results do not commit a waiver.

`state.dock`: `available`, the three policy fields, `operation_mode`, `dock_supported_available`, `dock_supported`, `usage_available`, `used_minutes`, `remaining_minutes`, `waived_today`, `unlimited_capped`. `restriction_reasons.dock` joins the composed target. Missing status is unavailable, never zero. Unknown mode restricts enabled policies; unknown use restricts handheld while confirmed TV remains subject to the daily total. Reliable continuity automatically recovers; an unprovable interval stays unknown until reliable evidence is available, including after docking or a parent waiver. Diagnostic rules/state and runtime snapshots contain these safe fields; secrets, PIN material, nonces and transaction preimages remain excluded.

Hardware dock/undock, HOME/game/sleep, simultaneous restrictions and recovery validation remain pending. The local libnx reference was commit `dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb`; its public OMM wrapper does not establish private PCTL layout or units. For installed libnx 4.12 without that wrapper, the adapter supplies the same read-only command 0 / byte response contract; no dependency upgrade or mode-policy writes are introduced.


## Eye care breaks (candidate: pending)

Eye-care results project the current reliable PCTL read without waiting for a later Enforce tick. Matching configured quota (1440 minutes for unlimited rules), available remaining time, an enabled timer and no pending confirmation allow an initial playing preview; a known same-day baseline adds the latest usage delta. Across midnight, the accumulated cycle is retained and new-day usage is added. Missing, mismatched or disabled readings remain unknown. Projection does not persist a baseline, write PCTL or start timers. Usage minutes do not decrease with wall time; rest instances and deadlines still come from persisted state.

During an eye care break, PCTL's temporary blocked state and zero remaining minutes describe enforcement of that break, not exhausted daily allowance. `restriction_reasons.daily_allowance` is `false` when only the eye care break applies. The UI shows the break and cycle in today's status, details, preview, and persistent header; it does not present the temporary zero as daily allowance or consumed time. Only a genuine daily allowance or bedtime restriction prevents skipping the current break.

Optional `rules.json` fields are `eye_care_enabled` (default `false`), `eye_care_play_minutes` (default 40, range 1–240), and `eye_care_rest_minutes` (default 10, range 1–60). Older files use these defaults. `state.json` persists `eye_care_day_index`, `eye_care_accumulated_minutes`, `eye_care_last_used_minutes`, `eye_care_usage_known`, `eye_care_resting`, `eye_care_rest_deadline` (Unix seconds), and `eye_care_break_id`. Usage is derived from PCTL configured minutes minus remaining minutes; unavailable readings pause accumulation and report phase `unknown`. Wall time, including sleep, counts toward a break.

`set_eye_care_policy` accepts `enabled`, `play_minutes`, and `rest_minutes`. Initial enablement requires recorded confirmation of enabled Nintendo Parental Controls and overlay availability. `skip_eye_care_break` accepts the active nonzero `break_id` and rejects stale or expired instances. Result `state.eye_care` contains `enabled`, `play_minutes`, `rest_minutes`, `phase` (`off|playing|resting|paused|unknown`), `used_minutes`, `rest_remaining_seconds`, `break_id`, and `unlimited_capped`. `state.restriction_reasons` lists `bedtime`, `daily_allowance`, and `eye_care`. Errors 322, 323, and 324 mean invalid break instance, missing enablement confirmation, and grants blocked during a break, respectively.

Daily allowance and bedtime restrictions take precedence and reset the eye care cycle. An unlimited day temporarily writes a 1440-minute PCTL limit for a usage reading; disabling eye care restores the unlimited target. PCTL writes use backup, readback, and transactional recovery. The release sysmodule does not initiate `StartPlayTimer (1451)`. Status confirmation checks only whether Nintendo Parental Controls is enabled; no separate suspension switch is required. Hardware A/B evidence for 1440-minute readings and actual pause behavior is still pending.

Ordinary idle time can complete a rest: after establishing a reliable usage baseline, two reliable readings proving unchanged usage for `rest_minutes` reset the cycle. Shorter idle intervals retain accumulated usage. `state.json.eye_care_idle_since` stores the beginning of the provable rest interval in Unix seconds, defaulting to 0 and updating on the first reliable reading or increased usage. Unchanged readings after wake can prove rest even without intermediate scheduler ticks. Minute polling may delay reset by about one minute. Failed or mismatched readings interrupt proof of rest but retain the last reliable usage baseline; a recovered same-day reading includes the missed usage delta. Midnight changes only the daily usage baseline, preserving accumulated eye-care usage, the break instance and its deadline.

`setup.disable_flag_present` reports the actual deactivation flag; missing fields in old results default to `false`. While the flag exists, a parent may skip the same nonzero `break_id` still persisted as resting even after its countdown reaches zero. Other expired or mismatched instances are rejected. This recovery retains the flag and respects genuine daily allowance and bedtime restrictions; previous-day usage must not exhaust a new day's allowance. Resuming management still requires the existing parental confirmation flow.

This document serves as the authoritative contract for the standard distribution release profile. File schemas are version 1 (`schema_version: 1`); offline tokens decode v1/v2, with the user interface utilizing v2 8-digit numeric codes. Device Lab employs an isolated profile and root directory; its high-risk requests do not belong to the standard release protocol.

## Runtime Directory Structure

```text
sdmc:/switch/playwise/
├── build.json
├── package-artifacts.json (Release IDs, sizes, and SHA-256 digests for standard package runtime binaries)
├── defaults/ (Immutable initial installation templates bundled with the package)
│   ├── config.json
│   ├── auth.json
│   ├── rules.json
│   ├── state.json
│   ├── setup.json
│   └── compatibility.json
├── config.json
├── credentials.json
├── auth.json
├── grant-issued.json
├── pending-redemption.json (Present only while confirmed redemptions await result completion)
├── rules.json
├── state.json
├── setup.json
├── compatibility.json
├── environment.json
├── inbox/{pending,processing,done}/
├── results/
├── ledger/used_nonces.jsonl
├── ledger/redemption-history.jsonl
├── activity/history.jsonl (Up to 200 sanitized family activity records)
├── stats/daily-summaries.jsonl (Up to 30 on-console daily summary records)
├── backups/install_pctl_snapshot.json
├── recovery/active/
├── handover/
│   ├── intent.json
│   ├── ready.json
│   ├── runtime-ready.json
│   └── reload.json (Present only during hot-reload transactions)
├── flags/
├── logs/
│   ├── YYYY-MM-DD/
│   └── undated/<boot-id>/
└── support/
```

`build.json`, `package-artifacts.json`, and `defaults/*.json` are program assets written by the installation package; remaining JSON files under the root directory represent live runtime data. At startup, the release sysmodule and Companion create missing root `config.json`, `auth.json`, `rules.json`, `state.json`, `setup.json`, and `compatibility.json` files atomically from `defaults/`; existing runtime files are never overwritten by installation templates, even if corrupted. If template initialization fails, the background service refuses to enter PCTL control scheduling. Standard distribution builds never create `capabilities.json`.

Standard installation Zips must never bundle the six root runtime JSON files above. Merging newly extracted `atmosphere` and `switch` folders directly onto the SD card root replaces only program assets and default templates. `credentials.json`, PIN data, rules, runtime state, ledgers, logs, backups, and recovery materials remain preserved; only an explicit full-clean installation may purge this user data.

## Configuration Contracts

### `build.json`

Identical to `build/generated/release-manifest.json` generated during build packaging, containing at minimum:

`release-manifest.json` is used solely for packaging gates and component parity checks, not bundled in the standard Zip; only `build.json` is retained inside the Zip for runtime use.

```json
{
  "schema_version": 1,
  "playwise_version": "<current-version>",
  "commit": "<40-hex>",
  "release_id": "playwise-<current-version>+<12-hex>",
  "profile": "release",
  "protocol_version": 1,
  "recovery_version": 1,
  "pctl_layout_version": 1,
  "build": {
    "devkitpro": "<compiler identity>",
    "libnx": "<version>",
    "libtesla_commit": "<40-hex>"
  },
  "verified_environment": {
    "model": "Nintendo Switch OLED",
    "hos": "22.5.0",
    "atmosphere": "1.11.2",
    "result": "pending"
  }
}
```

`qualification.status` and `verified_environment.result` default to `pending`. When the unified packaging command explicitly passes `--manual-device-verified`, the target environment defaults to `Nintendo Switch OLED`, HOS `22.5.0`, and Atmosphère `1.11.2` (overridable via `--verified-model`, `--verified-hos`, `--verified-atmosphere`). Only the release manifest updates to `manual_verified`, additionally logging `method:"manual"`, `scope:"main_features"`, and `artifact_binding:"embedded-manifest"`. This indicates the packager's manual verification declaration; full qualification is denoted only by an external `qualification.json` bound to the release Zip hash.

The exact same compact JSON must be embedded into sysmodule, NRO, and Overlay binaries. Mismatches in profile, version, or release ID return `504 release_manifest_invalid`. When Companion encounters this error, it prompts the user to perform a full console reboot; if the issue persists, it directs users to file an issue on GitHub.

### `package-artifacts.json`

Standard installation packages bundle a schema v1 component manifest. Its top-level `release_id` must match the NRO embedded identity, and `artifacts` must strictly list `switch/playwise/pctc.nro`, `switch/.overlays/playwise.ovl`, and `atmosphere/contents/4200000000BD2300/exefs.nsp`. Each entry records byte `size` and 64-character lowercase hex `sha256`. Prior to initiating a hot-reload, the NRO streams and validates all three installed files on the SD card; missing files, size discrepancies, or hash mismatches trigger an "Incomplete Installation" warning, halting handover. This manifest detects partial or corrupted file writes, not release digital signatures.

### Standard Release Hot-Reload Transactions

At launch, the NRO uses `pm:shell` to obtain the PID of standard Title ID `4200000000BD2300`, comparing `profile`, `release_id`, `boot_id`, PID, and IPC version in `handover/runtime-ready.json` against its own identity. Matching `release_id`s display "Loaded"; differing IDs prompt children to consult parents, requiring PIN authentication before proceeding, while "Support & Recovery → Software Info" permanently retains status details and manual reload actions.

Upon confirmation, the NRO atomically creates `handover/reload.json` and `intent.json` with `action:"quiesce"`. Journal v1 logs the transaction ID, `prepared/boot_disabled/boot_restored/target_launched` phase, source/target release IDs, and PIDs. The running background service ceases accepting new IPC submissions, returning `QUIESCING` during quiescence; after completing ongoing serialized operations with no active `recovery/active/meta.json`, it writes a matching `ready.json`. Newer background services append `handover_version:1`, source PID, profile, and release ID; older versions returning transaction ID and ready status remain supported.

Upon receiving ready confirmation, the NRO closes its legacy IPC handle and renames the empty `boot2.flag` atomically to `.playwise-hot-reload-backup` in the same directory. The running service detects flag absence, cleans up, and terminates cleanly. Forced termination is prohibited. After confirming old PID termination three consecutive times, the NRO immediately restores the original flag and launches the installed SD binary under the same Title ID via `NcmStorageId_None`. Success requires matching target PID, `release` profile, target `release_id`, new boot ID distinct from the source process, and successful IPC v2 reconnection. Timeouts for handover, exit, and launch are 10, 10, and 30 seconds respectively; if the target is absent, at most one retry launch is attempted.

All journal transitions are committed atomically to the SD filesystem. On startup, the NRO recovers completed `.tmp` files, restores original empty flags, and clears transaction intents; if the source PID has vanished, it attempts launching the installed background service. Unrecognized backups, non-empty flags, conflicting journals, or foreign handover files are never overwritten. Failures prior to service exit restore the flag, cancel quiescence, and resume old service operations; post-exit failures ensure the flag is restored without launching a second owner, prompting for a full reboot without auto-rebooting.

### `config.json` and `credentials.json`

```json
{
  "version": 1,
  "device_id": "kid-switch",
  "max_add_minutes": 240,
  "default_request_timeout_ms": 60000,
  "pairing_base_url": "https://selfuppen.github.io/NX-PlayWise/",
  "parent_shortcut_mask": "c0",
  "custom_shortcut_enabled": false,
  "show_parent_shortcut_hint": true,
  "theme": "system",
  "ui_language": "system",
  "setup_wizard_step": 0,
  "setup_wizard_version": 5,
  "setup_wizard_completed": false
}
```

The latter fields in `config.json` are maintained by Companion's onboarding setup:

- `parent_shortcut_mask`: lowercase hex bitmask for custom shortcuts (e.g. `c0` for `L + R`, `300` for `ZL + ZR`). Only supported controller button chords are permitted. The UI provides 14 presets (two shoulder pairs alone or combined with direction keys, `Plus`, or `Minus`); arbitrary chord recording remains hidden pending hardware validation, though valid legacy non-preset masks are preserved during upgrades. The `+` symbol denotes simultaneous button presses rather than literal Plus buttons to prevent confusion. The fixed parent shortcut `Minus` is excluded from this mask and remains permanently active; custom chords never replace it. Custom combinations require a continuous ~400ms hold, triggering only once per hold; directional buttons and left joystick pushes share the same mask. The fixed `Minus` button triggers on release without holding. This distinction is communicated across onboarding, zone selection, child footers, and parent settings. Missing or invalid masks fall back to `L + R`, with enablement governed by the next field.
- `custom_shortcut_enabled`: controls whether custom button chords are active. Defaults to `false` on clean installations, leaving only fixed `Minus` active; choosing presets updates UI drafts until explicitly confirmed by parents. Missing fields with valid masks evaluate to `true`. When combinations include `Plus` or `Minus`, the ~400ms hold check takes precedence; pressing and releasing `Minus` alone opens Parent Portal, while pressing and releasing `Plus` alone exits the NRO.
- `show_parent_shortcut_hint`: controls shortcut hint visibility in Child Zone, defaulting to `true`. Disabling hides hints without deactivating `Minus` or custom chords.
- `theme`: Companion NRO appearance preference (`system`, `light`, `dark`), defaulting to `system`. `system` inspects Switch OS theme settings at launch and upon resuming from HOME menu; fallback is light theme. This setting never enters IPC, requests, sysmodule state, or `common/` control logic; changing themes never restarts services, alters business models, or affects timers, PCTL, tokens, queues, or overlays.
- `ui_language`: interface language preference across console app and overlay (`system`, `zh-Hans`, `zh-Hant`, `en`), defaulting to `system`. When set to `system`, Traditional Chinese system settings resolve to `zh-Hant`; English (ENUS/ENGB) or other non-Chinese languages resolve to `en`; unknown languages fall back to Simplified Chinese. Parents can override this explicitly. Language translation applies exclusively during text rendering and layout measurement; protocol fields, requests, persistent data, and control logic remain untranslated. Changing languages purges font glyph caches.
- `setup_wizard_step`: v5 wizard progress (`0` for no pending step; `1` environment/preparation, `2` parent settings, `3` confirmation). `setup_wizard_version:5` distinguishes the three-step flow. Unfinished older wizards restart at step 1 without changing saved PINs or preferences. A versioned legacy step 0 migrates to completed even if the backend is unavailable or protected. Without a completion record, a confirmed active backend remains the migration fallback.
- `setup_wizard_completed`: independent boolean UI completion marker. Skipping or failing activation also completes the guide; it never changes `setup.json.phase` or authentication. Inactive controls, protection and failed refreshes cannot reopen a completed wizard. Persistence errors retain progress and completion for the session; reopening may require setup again.

Step 1 defaults to system language with a fixed bilingual Chinese/English language title and help. It also offers a read-only “Enable Controls Help” dialog showing the system settings steps. Controller and touch share the same entry; closing preserves step and focus, and viewing help does not write control status or submit requests. Changes apply immediately and clear glyph caches. Save failures preserve session language, theme, shortcut and progress choices and allow continuing. On entering step 2, a legal empty `auth.json` receives default PIN `110` with existing salt/HMAC protection; existing or corrupt credentials are never overwritten. Canceled, mismatched or failed PIN changes retain the old valid PIN and display inline feedback.

Step 3 uses `complete_setup` without a second confirmation modal. Pending activation can enter Support; polling retains the original request ID without resubmission. After 30 seconds, an unknown result opens Support while remaining pending. Backend preflight, snapshot, read-back and rollback rules remain enforced. Only confirmed active, non-disabled controls count as success. Otherwise Support opens with diagnostic export focused. Missing PINs or authentication failures permit read-only Support and sanitized export; other parent capabilities still require valid authorization.

### Homebrew Menu Advanced Entry

Accessible only under "Security & Preferences" after PIN authentication; excluded from onboarding, with child zones showing no status or hotkeys. It configures hbmenu launching without anti-tamper guarantees. Before configuration, the existence of `/atmosphere/config/override_config.ini` and `/switch/.packages/More Menu/Photo Album/package.ini` is recorded; all `[hbl_config]` sections in the former are replaced with `override_any_app=false`, Controller applet program ID `0100000000001003`, `override_key_0=X`, and `39_bit` address space, while the latter is removed. Both files belong to a single transaction; failure at any step triggers rollback to original pre-images. Once active, holding `X` and pressing `A` over the HOME menu "Controllers" icon opens hbmenu; "Controllers" serves solely as the launch carrier.

Existing files are backed up atomically as `.playwise.bak`; absent files are recorded in `override_config.ini.playwise.state` without creating empty backups. The state file tracks transaction phase, original existence, backup hashes, and target hashes. Existing `.playwise.bak`, `.playwise.state`, or `.playwise.conflict` files are never overwritten. Standard restoration executes only if current configs equal PlayWise target values; external modifications require parental long-press confirmation to force restoration after saving `.playwise.conflict`. Upon successful restore, backups are renamed back and state files removed. Legacy `switch/playwise/backups/album_restriction/` directories are ignored. UI prompts for a console reboot following changes and forbids manual deletion of `.playwise.*` files.

If target configurations already exist prior to PlayWise establishing a transaction, and no PlayWise transaction or backup evidence exists, status displays as "Configured Externally" rather than reporting missing backup anomalies. This indicates the entry is operational, but PlayWise did not alter it and cannot restore prior states; it never fabricates pre-install backups from current configs. Anomaly states occur only when PlayWise transaction evidence (`configured`, `enabling`, `restoring`, `rollback_required`) conflicts with live files or backups.

These fields are not secrets or security credentials for standard IPC requests; Companion updates them via atomic writes, falling back to defaults if absent.

```json
{
  "version": 1,
  "grant_secret": "<64-character lowercase hex generated on sysmodule first boot>"
}
```

Production secrets must never appear in templates, manifests, logs, or support archives. On initial launch, the sysmodule generates a unique 64-character lowercase hex secret via `randomGet`. Management interfaces permit saving 32–64 printable non-whitespace ASCII characters; updating the secret invalidates existing mobile pairings and outstanding issued codes.

Parents may intentionally switch to the public demo secret `playwise-public-demo-secret-0001` after PIN re-authentication, risk review, and secondary confirmation. This value is not confidential and must never serve as an installation seed; when enabled, anyone knowing the device ID can issue 1–120 minute codes up to 1440 minutes daily. Parent pages display persistent red warnings. Exiting demo mode generates a fresh random 64-character secret without retaining the replaced key.

### `auth.json`

```json
{
  "version": 1,
  "pin_hash": "<64-hex>",
  "pin_salt": "<32-hex>",
  "hash": "hmac-sha256",
  "updated_at": 1783526400,
  "failed_attempts": 0,
  "cooldown_until": 0
}
```

PINs accept 1–64 ASCII digits at parental discretion; short PINs trigger UI warnings regarding weak protection. The hash key is the UTF-8 PIN, with message `"PTC-PIN1" || salt16`; all valid lengths share this format. Empty hash/salt denotes uncompleted onboarding. A 5th consecutive failure triggers a 30-second cooldown, followed by 60s, 2m, 4m, and a maximum of 10m; `failed_attempts` and `cooldown_until` persist across reboots and reset to zero upon successful validation. Missing fields default to 0.

Modifying PINs requires verifying the active PIN under standard validation and cooldown rules; cancellation, auth failure, or mismatched new PIN entries never overwrite existing credentials.

### `rules.json`

```json
{
  "version": 1,
  "week": [
    {"mode": "unlimited", "minutes": 120},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "unlimited", "minutes": 120}
  ],
  "today_override_present": false,
  "today_override_day_index": 0,
  "today_override_mode": "limit",
  "today_override_minutes": 60,
  "holiday_enabled": false,
  "holiday_mode": "unlimited",
  "holiday_minutes": 120,
  "makeup_workday_mode": "limit",
  "makeup_workday_minutes": 60,
  "scheduled_override_enabled": false,
  "scheduled_override_start_day_index": 0,
  "scheduled_override_end_day_index": 0,
  "scheduled_override_mode": "limit",
  "scheduled_override_minutes": 60,
  "daily_buffer_minutes": 0
}
```

Daily allowance weekday order is Sun=0 to Sat=6, with mode restricted to `limit` and `unlimited`; public `blocked` or `limit_action` fields do not exist. Bedtime operates as an independent overlay, not a 0-minute daily rule.

Rule evaluation priority is strictly: `today_override > scheduled_override > statutory_holiday/makeup_workday > week`. Specific date overrides support at most one interval at a time, inclusive of start and end dates, spanning 1–366 days; active intervals replace daily total allowances without resetting accumulated screen time, and do not act as extra time. When disabled, they do not participate in rule calculation. `daily_buffer_minutes` allows 0, 5, 10, or 15 (0 disables buffer). When `holiday_enabled:false`, when dates fall outside covered calendar years, or on standard dates, rules fall back to weekly schedules. Upgrading from legacy `rules.json` populates missing fields with defaults in disabled states.

Bedtime release candidate schema uses `rules.json version:2`; legacy or missing fields migrate to a disabled master switch. `bedtime_week` stores seven independent `{enabled,start_minute,end_minute}` entries for Sun=0 to Sat=6 without weekday/weekend mappings. Active windows must span overnight (`start_minute > end_minute`) at 1-minute precision; prior-day end times must not exceed next-day start times. Windows belong to their start date, with instance IDs evaluated as `(start_day_index << 16) | start_minute`. Clean installs default to Sun–Thu 21:00–07:00 and Fri–Sat 22:00–08:00, with master switch off.

Resolution priority is `bedtime scheduled override > bedtime statutory holiday/makeup workday > bedtime week`. Specific date bedtime overrides support `inherit|disabled|custom` up to 366 days; holidays default to 22:00–08:00, and makeup workdays default to 21:00–07:00. Standard NRO saves bedtime with `activation:"immediate"` after PIN verification; if the current time falls inside an active window and saving will immediately trigger restrictions, save buttons turn red with "Save (Lockout Immediate)", requiring a 1-second controller or touch hold. Active un-skipped restrictions are not re-flagged.

Bedtime requests are `set_bedtime_policy`, `confirm_bedtime_requirements`, `skip_bedtime`, `clear_bedtime_skip`, `disable_bedtime`, and `overlay_ready`; full restore uses `restore_install_snapshot`. `clear_bedtime_skip` carries expected `window_instance_id`, clearing only matching skipped windows; active windows rewrite restrictions immediately, retaining skipped state on failure. Under `disable.flag`, clearing skips that write new restrictions is rejected; skipping, disabling, and restoring remain whitelisted. PINs are validated client-side, authorizing a single recovery action without entering requests, IPC, queues, results, logs, or diagnostics. `overlay_ready` binds release ID, sysmodule boot ID, and `environment.json` SHA-256 to the current boot session; official setting confirmations bind to the same environment hash, invalidating upon environment changes.

Status responses expose current/next windows, sources, instance IDs, skip states, recovery phases, official setting confirmations, overlay handshakes, and `restriction_reasons`. `bedtime.skipped_window` projects `{available,window_instance_id,start_day_index,start_minute,end_minute,source}` for matching skipped instances across the next 8 days. Existing `bedtime.skipped` denotes current active window skip, while `next_*` points to the next un-skipped window. Active bedtime rejects standard grant codes and self-buffer claims prior to consuming tokens/nonces/eligibility; bedtime does not deduct daily allowance. Exiting bedtime on the same day restores pre-lockout snapshots; crossing midnight recalculates against the new day's base quota.

Bedtime resolves against Switch local date, weekday, and minute without assuming trusted hardware clocks or preventing system clock tampering; clock jumps trigger immediate recalculation. Background scheduling targets restriction within 60 seconds of window onset or wake-up. Entrance failures retain base allowances and log warnings; exit failures retain restriction, enter protection mode, and halt standard writes while keeping overlay recovery accessible. When bedtime is active, background services reject quick grants and unlimited play before writing PCTL, preventing stale UI bypasses. Bedtime routines never invoke `1451`.

When daily limits or bedtime activate, Nintendo PCTL dialogs block games, Homebrew, HOME menu, System Settings, and PlayWise NRO; the overlay is PlayWise's sole operational interface, while native Nintendo dialogs and master PIN unlocking serve as system-level recovery paths. The overlay defaults to the child grant redemption page, offering code entry and buffer claiming; during active bedtime, codes can be edited but cannot be submitted until bedtime clears. Authenticating parental PINs unlocks quick grants (5–120m), unlimited play today, skipping bedtime, disabling bedtime, or long-pressing full snapshot restoration; unavailable actions display rationale and are disabled. Overlay PIN entry uses joystick pushes for `1`–`8`, directional buttons for `1/3/5/7`, `X/Y` for `0/9`, `ZL` for backspace, and `+` for confirmation; joysticks register one digit per push from deadzone. PIN screens display static layout guides and masks; touch cannot input PIN digits directly. PIN auth authorizes one action only, excluded from logs. Recovery results re-query and present actual restriction states; background disconnects never create `restore_install_snapshot.flag` or promise auto-recovery on reboot. Unhandshaked overlays do not disable schedules to prevent bypass by overlay removal; onboarding prompts risk acceptance after confirming Nintendo Parental Controls is enabled. Protocols and UI are included in standard packages; builds default to `pending` or `manual_verified` until full qualification reports pass.

Built-in holiday calendars, versioning, release dates, and sources are detailed in [Holiday Calendar Data](节假日日历.md). Uncovered years fall back to weekly schedules; `calendar_update_warning:true` triggers 30 days prior to calendar expiration.

Parents can place a UTF-8 [custom region calendar](../使用/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md) in SD `/switch/playwise/calendar-import/`. `calendars/library/<region>-<year>-<sha256>.json` stores content-addressed imports, `calendars/catalog.json` records the latest file per region and year, and `calendars/active.json` pins the applied versions. Missing `active.json` selects the separate built-in China 2026 option `builtin-cn`. Import request ID 45, `import_holiday_calendar`, takes `file_name` and lowercase SHA-256; activation request ID 46, `activate_holiday_calendar`, takes `option_id` and the expected `catalog_sha256`. Error 505 is `calendar_invalid`; 506 is `calendar_catalog_changed`. Import is allowed under `disable.flag` and never writes PCTL; activation is blocked while disabled and atomically updates the active manifest, PCTL, state, activity, and result. A missing selected year falls back to weekly rules. Damaged active files stop new control writes rather than silently switching regions. State includes `calendar_option_id`, `calendar_source` (`builtin|user_import`), daily `calendar_covered`, and update warnings. User imports are always labeled as such and are not claimed as officially verified.

### `state.json`

Key fields:

```json
{
  "version": 1,
  "last_enforced_day_index": 0,
  "last_enforced_mode": 0,
  "last_enforced_minutes": 0,
  "apply_status": "idle",
  "apply_pending_confirmation": false,
  "apply_confirmation_deadline": 0,
  "pending_mode": 0,
  "pending_minutes": 0,
  "updated_at": 0,
  "v2_failed_attempts": 0,
  "v2_cooldown_until": 0,
  "buffer_claimed": false,
  "buffer_claim_day_index": 0,
  "buffer_claimed_minutes": 0,
  "summary_day_index": 0,
  "summary_grant_minutes": 0,
  "bedtime_enforced": false,
  "bedtime_window_instance_id": 0,
  "bedtime_start_day_index": 0,
  "bedtime_skipped_instance_id": 0
}
```

Daily Enforce writes current-day PCTL settings upon date or rule transitions, verifying mode and total minutes on read-back; it never calls `1451 StartPlayTimer`. Nintendo limits track console screen-on time, meaning HOME and System Settings usage also consumes allowance; therefore, midnight transitions, startup recovery, future rule changes, and unchanged daily rules must never activate timing cycles. Unconfirmed Enforce writes record `apply_status:"applied_pending_confirmation"` with a 30-second deadline; confirmation clears pending state, while timeouts trigger rollback. Five consecutive v2 format or signature errors invoke a 10-minute cooldown.

Interactive user edits write settings and read back immediately. When target settings take effect and timers are running, or when unlimited play lifts restrictions, transactions commit directly without calling `1451`; if timers have not started on restricted targets, or restrictions persist after extra time / unlimited play, a single `1451` fallback call is permitted before reading back again. Fallback failures, unconfirmed read-backs, or inexact settings roll back against transaction snapshots; nonces, buffer eligibility, and activity logs are consumed only upon complete commits. Recovery paths restore original snapshot timer states independently.

BLOCKED targets verify settings only and never invoke `1451` because of a transient `restricted_now:false`. Autonomous background writes also prohibit this fallback.

Self-buffer claims are permitted only on restricted days with non-zero buffer policies where buffer has not been claimed today. Claims reuse PCTL snapshot, rules/state/activity pre-images, and result commit transactions, capping total daily allowance at 1440 minutes. Even if capping adds 0 minutes, the day is marked claimed; failures at any stage roll back without consuming eligibility.

Hardware forensics (Nintendo Switch OLED, HOS 22.5.0, 2026-08-20) demonstrate that private command 1455 `restricted_now` reflects instantaneous system restriction state: after Horizon displays time-up notifications, this value may return to `false`. Write requests must not mandate persistent `true`, nor return 306 solely because `restricted_now:false` when remaining time is 0. Restricted targets must verify exact configured daily minutes; when 1454 reports 0 remaining minutes, transactions may commit even if 1455 is `false`, displaying "Lockout may be imminent" in the UI. It does not prove notification display or software suspension. Read-back failures or unapplied modes roll back and return errors.

`recovery/active/` within confirmation windows represents active transactions owned by the running sysmodule process; standard scheduler ticks must not treat them as startup recovery targets. Only initial sysmodule bootstrap checks legacy startup transactions for rollback; successful recovery writes `disable.flag`, requiring parental reconfirmation.

Transactions are isolated by `meta.json.request_id`; the background owner is `enforce`. Other ordinary requests return `325 control_busy`, while status and overlay_ready remain available. Allowlisted recovery requests first roll back existing transactions. Parse errors and rejected new requests must not roll back or clear another owner's transaction. Pending state without matching recovery files creates a deactivation flag. The journal also saves bedtime snapshots, bedtime instance metadata and eye-care snapshots so failed result commits or timeouts restore their pre-images. Older journals without these added fields remain compatible.

`ledger/used_nonces.jsonl` compares date, token version and nonce using complete JSON lines, avoiding numeric prefix matches and fixed 4096-byte reads. Legacy rows without `token_version` mean v1. Read errors, oversized lines, damaged tails or duplicate JSON keys return `500 storage_read_failed`, never an unused nonce. Before starting transactions, normalization and deduplication discard only records earlier than today, retaining today's and future nonces; failed compaction blocks writes. Rules and state use initialization defaults only for confirmed missing files; read failures, empty files and oversized contents fail instead. A successful redemption adding 0 minutes at the 1440-minute cap still consumes its code; a successful buffer claim consumes daily eligibility. Overnight bedtime windows continue to belong to their start date.

### Setup, Compatibility, and Environment

`setup.json.phase` supports `unconfigured`, `pending`, `released`, `active`, `failed`, `protection`, and `restored`. Prior to takeover, the background service performs read-only inspection of today's PCTL mode, configured quota, and remaining minutes, staging `handover_*` fields in `setup.json`. For restricted days, it prioritizes reading configured total allowance; if unreadable, it computes allowance only when both consumed estimate and remaining minutes are available. It writes a single-day `today_override`, saves the install snapshot, and re-checks `0x44`, timers, quota, and remaining minutes (allowing 1-minute sampling jitter). Matching states transition directly to `active` without writing PCTL, altering modes, or calling `1451`. Existing balances and limits remain intact, resuming weekly schedules the next day.

If total allowance or remaining minutes cannot be determined safely, takeover returns `313 handover_state_unavailable` without writing PCTL or applying default rules; securely saved snapshots are preserved for future recovery. If state alters after initial inspection, 313 is returned. Parents can retain current settings and re-attempt detection later.

`restored + disable.flag` or automatic transaction recovery resulting in `active + disable.flag` displays "Lift Deactivation and Resume Management", requiring parental confirmation before submitting `complete_setup`. Older failed takeovers (`failed + setup_release_failed + handover_today_pending`) can also be resubmitted: background services repeat read-only preflight checks, verifying snapshots and quotas before removing flags. Environment changes (`protection + runtime_fingerprint_changed`) display "System Environment Changed, Re-check and Resume", performing read-only preflight checks upon confirmation, updating fingerprints, clearing flags, and preserving PINs, keys, rules, and original snapshots. Other `protection + disable.flag` causes cannot be bypassed this way. Preflight failures retain deactivation flags for later retry. `compatibility_status` evaluates to `pending`, `verified`, `accepted_unknown`, or `protection`.

`environment.json` records HOS, firmware hash/digest, product model, and Atmosphère presence; exact Atmosphère versions appear only in qualification manifests.

`compatibility.json` records accepted environment fingerprints and timestamps. Unknown environments can be accepted via PIN and long-press confirmation; snapshot, PCTL layout, startup transaction, PCTL initialization, or manifest failures cannot be bypassed in standard releases.

## Requests

Files are staged at `inbox/pending/<request_id>.json`; IPC service is `pctc:u`. Common envelope:

```json
{
  "version": 1,
  "request_id": "client-unique-id",
  "type": "status",
  "created_at": 1783526400,
  "payload": {}
}
```

Standard distribution profile accepts exclusively:

| ID | type | payload |
| ---: | --- | --- |
| 1 | `offline_code` | `{"code":"12345678"}` |
| 2 | `status` | `{}` |
| 3 | `set_today_limit` | `{"minutes":60}` |
| 4 | `add_today_minutes` | `{"minutes":15}` |
| 5 | `disable_today_limit` | `{}` |
| 7 | `restore_today_policy` | `{}` |
| 8 | `set_weekly_template` | `{"days":[7 mode/minutes objects]}` |
| 19 | `complete_setup` | `{}` |
| 20 | `retry_setup_release` | `{}` |
| 21 | `restore_install_snapshot` | `{}` |
| 22 | `preview_offline_code` | `{"code":"12345678"}` |
| 23 | `set_holiday_policy` | `{"enabled":true,"holiday_rule":{"mode":"unlimited","minutes":0},"makeup_workday_rule":{"mode":"limit","minutes":60}}` |
| 29 | `clear_redemption_history` | `{}` |
| 30 | `set_scheduled_override` | `{"enabled":true,"start_day_index":2380,"end_day_index":2386,"rule":{"mode":"limit","minutes":90}}` |
| 31 | `set_autonomy_policy` | `{"daily_buffer_minutes":10}` |
| 32 | `claim_daily_buffer` | `{}` |
| 33 | `clear_activity_history` | `{}` |
| 42 | `clear_bedtime_skip` | `{"window_instance_id":155977020}` |

IDs 6, 9–18, and 24–28 do not belong to standard profiles; unlisted request types return `102 unknown_request_type`. When `disable.flag` is present, status and setup/recovery requests execute normally, while standard control writes return `300 disabled`.

Companion must refresh status before parents open "Set Today's Allowance". Home cards separate daily rules from remaining allowance: restricted days display reliable 1454 readings; unlimited days display "Unlimited"; unreadable states show "Unavailable"; stale data indicates "Status Pending". Temporary unlock states alter access without changing 0x44 daily rules or clearing 1454 readings; official timers pause during temporary unlock, and while writes may save new settings, `1451` calls are forbidden. UI displays active rules with notices that "Limits resume upon entering sleep mode." When consumption estimates are reliable, editors default to the greater of active allowance and `played_minutes`, clamped to 1–1440 minutes; if current allowance is unavailable and consumed minutes are 0, it defaults to 1 minute. When estimates are unavailable, it uses active allowance or 60 minutes. Editors clarify that total allowance includes time already played today. Selecting unlimited play, unavailable estimates, or values below consumed minutes requires a 1-second controller or touch hold, warning that restrictions may trigger immediately and explaining recovery via extra time or grant codes. Post-adjustment balances are displayed as "Estimated Remaining Playtime" only when configured minutes and 1454 readings are reliable; otherwise, UI displays "Unavailable" until refreshed. If restrictions take effect after `set_today_limit`, recent action summaries display unlock guidance; if remaining time is 0 but `restricted_now:false`, UI indicates limits may be imminent rather than claiming lockout has occurred.

"Set Today's Allowance", "Quick Grant", weekly schedule, statutory holidays, and makeup workdays share the two-field hour/minute editor. Hours and minutes must be entered completely, with minutes bounded to 0–59; standard quotas span 1–1440 minutes, while quick grants span 1–120 minutes (maximums "24h 0m" and "2h 0m"). Empty fields, 0 total minutes, or out-of-range inputs cannot be submitted. Touch selects fields directly, controller Minus toggles fields; numeric inputs apply to the active field, while quick increments adjust total minutes across both fields. Requests store converted `minutes`.

`preview_offline_code` runs identical token, date, device, nonce, cooldown, and state validation as live redemption, computing results without modifying PCTL, rules, state, results, or ledgers, and without consuming nonces. Redemptions during unlimited play return `301 unlimited_not_allowed`. Companion must re-invoke preview upon user confirmation; if safety semantics change (availability, truncation, zero remaining), new previews must be confirmed before submitting `offline_code`. High-risk confirmations in Companion NRO accept a 1-second hold on controller `A` or touch buttons, displaying hold progress; releasing or cancelling resets progress. Overlays accept controller holds only.

## Results

Results are written atomically to `results/<request_id>.json`, moving requests to `inbox/done/`. Success envelope:

```json
{
  "version": 1,
  "request_id": "client-unique-id",
  "type": "status",
  "status": "ok",
  "state": {
    "day_index": 2380,
    "restriction_enabled_available": true,
    "restriction_enabled": true,
    "temporary_unlocked_available": true,
    "temporary_unlocked": false,
    "limited_today": 1,
    "blocked_today": 0,
    "unrestricted_today": 0,
    "remaining_available": true,
    "remaining_minutes": 45,
    "played_minutes_available": true,
    "played_minutes": 15,
    "play_timer_enabled": 1,
    "restricted_now": 0,
    "rule_source": "weekly",
    "calendar_covered": true,
    "calendar_update_warning": false,
    "forecast": [
      {"day_index":2380,"mode":1,"minutes":60,"rule_source":"weekly","calendar_covered":true},
      {"day_index":2381,"mode":1,"minutes":90,"rule_source":"scheduled_override","calendar_covered":true}
    ],
    "autonomy": {
      "daily_buffer_minutes": 10,
      "claimed_today": false,
      "available": true,
      "reason": "available"
    },
    "usage_summary": {
      "available": true,
      "known_days_7": 5,
      "consumed_minutes_7": 210,
      "known_days_30": 18,
      "consumed_minutes_30": 720
    }
  },
  "completed_at": 1783526400
}
```

`status` also handles combined read-only inspection: `setup` provides compatibility, write confirmation, active recovery transactions, and sanitized deactivation reasons; `environment` provides HOS, model, and Atmosphère detection. Inspection never removes `disable.flag`, takes over systems, or writes PCTL. Legacy `disable.flag` without recorded reasons leaves `disable_reason` empty, displayed as "Unrecorded in legacy version". `restriction_enabled_available` and `temporary_unlocked_available` distinguish confirmed `false` from unreadable states. `limited_today`, `blocked_today`, and `unrestricted_today` reflect 0x44 configured rules, unaffected by temporary unlock toggles. `play_timer_enabled` and `restricted_now` use tri-state integers: `1` for true, `0` for false, `-1` for unavailable IPC reads; UI renders 0 as "Not Timing" or "Not Triggered", reserving "Unconfirmed" for `-1`.

Successful write results serve as commit records. The service may retain `recovery/active` until result persistence completes, but returns `recovery_active:false` in the result; transactions held by other operations (e.g. Enforce confirmation windows) return `true`. Companion re-queries actual recovery states upon completing local actions, avoiding cached warnings.

The background service maintains `support/recent-events.jsonl` atomically, retaining up to 20 sanitized events (unchanged status queries omitted). Status queries return this summary via `recent_events`, with Support pages displaying the latest 3 entries. Entries record timestamps, request types, event names, error reasons, and sanitized details, omitting payloads, PINs, codes, keys, nonces, or recovery secrets. Event write failures never block control execution.

Family activity logs reside in `activity/history.jsonl`, keeping up to 200 entries (oldest evicted). Actions are restricted to stable enums: daily rules, schedules, bedtime skips/clears (`bedtime_skip_cleared`), buffer policies, offline grants, buffer claims, protection events, and clears, storing timestamps, local day index, requested minutes, and effective minutes; PINs, keys, codes, nonces, and payloads are omitted. Rule, grant, and buffer transactions mandate logging activity entries prior to commits, rolling back on failure without consuming grant nonces. Protection logging operates best-effort without blocking safety recovery. `clear_activity_history` restores prior entries if results fail to commit.

`stats/daily-summaries.jsonl` retains up to 30 on-console summaries, logging rule sources, limit rules, reliable remaining minutes, cumulative grants, and consumed estimates. Save failures do not impact PCTL control. 7/30-day aggregations evaluate only unique dates with `consumed_available:true`, omitting interpolation or predictions; duplicate dates select newer `captured_at` records. UI labels this "Allowance Consumption Estimate" and scopes it to "Console Usage", never attributing it to specific user accounts.

Per-title playtime statistics utilize the read-only `platform/usage_stats.h` interface without introducing PDM or title logic into `platform/pctl.h`. The release adapter reports unavailable until PDM hardware verification passes, omitting Top 3 output and retaining "Game Breakdown Unavailable". It never calls PCTL `1952`.

Failures set `status` to `error` and append:

```json
"error": {"code": 312, "reason": "protection_mode", "message": "Security preflight checks failed; entered protection mode"}
```

Results omit control modes or dry-run fields.

`rule_source` reflects the active rule source: `weekly`, `today_override`, `scheduled_override`, `statutory_holiday`, `makeup_workday`, evaluated alongside PCTL targets by the sysmodule without UI guessing. `forecast` returns 7 days starting today; parents see all 7, children see today and tomorrow. Calendar or schedule updates recalculate daily rules immediately, preserving existing `today_override`.

`preview_offline_code` success results append:

```json
"preview": {
  "grant_minutes": 30,
  "remaining_after_available": true,
  "remaining_after_minutes": 75,
  "effective_add_minutes": 30,
  "capped": false,
  "converts_unlimited_to_limited": false
}
```

Redemption follows: "Input → Non-consuming Preview → Confirmation → Re-validation → Live Redemption → Success". Active unlimited play triggers notices that grant codes are unavailable, rejected during preview and live redemption. Cancellation, preview failure, or sync failure clears entered codes from the UI, confirming codes remain unconsumed. If remaining time is 0 or unknown, NRO accepts a 1-second hold on controller `A` or touch buttons; overlay accepts controller holds only.

Before submitting live `offline_code` requests, Companion writes `pending-redemption.json` atomically. It records version, request ID, confirmation timestamp, submission status, and minute snapshots; full codes are strictly excluded. The file is removed immediately if submission fails. If interrupted, the NRO checks `results/` and inbox using the request ID upon reboot: unsubmitted entries are treated as unconsumed; submitted entries poll `results/<request_id>.json`, displaying success or "Confirming Result" without re-submitting. The recovery marker is removed once the final result is presented to the user.

### User-Facing Error Handling

Error `306` (`pctl_effect_not_observed`) indicates background services could not verify target settings or target modes failed to apply. `restricted_now:false` alone does not justify 306. Possible causes include disabled parental controls or un-synchronized system state. Users should verify "System Settings → Parental Controls", return to PlayWise, and select "Re-check"; if issues persist, preserve diagnostic logs rather than manually editing PCTL data.

Error `313` (`handover_state_unavailable`) indicates initial takeover could not reliably determine configured allowance and remaining minutes simultaneously, or state mutated during confirmation. PlayWise does not alter PCTL under this error; existing settings should be preserved and re-checked later or the next day, avoiding assumptions that total allowance equals remaining time.

Self-buffer claims return `314 autonomy_disabled`, `315 daily_buffer_already_claimed`, or `316 daily_buffer_limited_only`, rejected prior to PCTL writes and indicating disabled buffer, prior claim today, or non-restricted days respectively. These business rejections must not be presented as system errors.

`played_minutes` is retained for version 1 protocol backward-compatibility and does not represent actual gameplay duration. Implementations avoid calling PCTL `1952 GetPlayTimerSpentTimeForTest`, which accumulates during HOME menu usage. On restricted days with reliable configured and remaining minutes, "Configured minutes − Remaining minutes" computes consumption estimates; unlimited days return `played_minutes_available:false` and `played_minutes:-1`. User interfaces must label this "Allowance Consumed (Estimate)" rather than actual playtime.

## Console Home and Read-Only Details

Companion NRO Child Zone prioritizes "Remaining Playtime Today", with code entry and buffer claiming on the right. `A` enters codes, `X` claims buffer, `Y` refreshes, `B` exits; `+` or touching "Usage Details" opens read-only dialogs displaying timers, runtime state, and 7/30-day consumption estimates. Tomorrow's plan and rule sources appear on the home screen; "Game breakdown unavailable" appears in details. Parent shortcut priorities remain unchanged.

Daily dispatch displays six action cards grouped under "Today's Allowance Adjustments (Today Only)" and "Bedtime & Self-Buffer". Group headers explain that standard schedules resume tomorrow, bedtime operates independently, and self-buffer appends time only when eligible; "Today Only" clarifies that unlimited play does not lift bedtime restrictions. Group titles are not interactive.

Overlay high-risk redemption buttons and parent action cards requiring holds display 1-second progress bars; releasing `A`, cancelling, or leaving the page resets progress, with sustained holds submitting only once. Actions not requiring holds omit progress indicators, and high-risk redemptions accept only physical controller holds. This visual feedback does not alter protocol schemas or Device Lab's 2-second forensics confirmation.

Parent "Daily Dispatch" displays allowance summaries on the left and action cards on the right ("Set Today's Allowance", "Quick Grant", "Unlimited Play Today", "Clear Today's Adjustments", "Skip This Bedtime", "Self-Buffer Claim Status"). "Set Today's Allowance" indicates dynamic status and rationale: active, cleared this session, unconfigured, awaiting effect, status pending, controls disabled, recovering, or timers paused during temporary unlock; unconfigured states explain which rule layer governs today. "Cleared" is displayed only after receiving `restore_today_policy` success during the active NRO session; rebooting displays "Unconfigured" honestly. When fresh status confirms no `today_override`, clear cards are disabled, providing explanations on controller trigger without submitting requests. Quick grants offer `+15/+30/+60/Custom` (custom bounded to 1–120m) before showing impact summaries. Clear actions submit `restore_today_policy`, removing only `today_override`. "Skip This Bedtime" skips active or locked instances once without entering schedule managers, refreshing status, verifying PIN, displaying confirmation, submitting `skip_bedtime`, and refreshing results. Status freshness threshold is 120 seconds; rejected stale instances refresh and prompt explicitly. `+` or touching "View Details" opens today's details; global error icons open Support & Recovery directly.

Today's allowance editors use `set_today_limit` and `disable_today_limit` for limit/unlimited modes; quick "Unlimited Today" shortcuts directly access the latter. Active unlimited adjustments display "Unlimited" on allowance cards and "Active" on shortcut cards. Execution badges assert active status only on fresh data, displaying unconfirmed when stale. Confirmation pages support manual refresh, re-evaluating status and impacts prior to submission; changes in balance, estimates, sources, or hazard classifications reset hold progress and require reconfirmation. Refresh failures preserve drafts without submitting.

Today's details and child usage details share a 1120×640 read-only dialog. Parent details provide touchable, `L/R`-switchable "Today's Decision / Usage & Status" tabs; child details open "Usage & Status" directly. "Today's Decision" uses `ptc_rules_resolve` to highlight active rules, total minutes, and final rationale, breaking down "Today's Adjustment → Specific Date Allowance → Statutory Holidays → Weekly Schedule" using explicit states ("Active", "Overridden", "Unconfigured", "Not Matched", "Disabled", "Calendar Uncovered", "Status Pending"). Bedtime and self-buffer appear as parallel conclusions. "Usage & Status" highlights "Remaining Playtime Today", displaying configured allowances, consumed estimates, system timers, runtime state, and 7/30-day aggregations. Data older than 120 seconds displays "Status Pending" rather than zero; unrecorded dates display unavailable. Explanations note that screen time includes HOME menu usage, per-title stats are pending, and missing dates are excluded from estimates. Error banners remain persistent, while recent actions appear only in Parent Portal. Cards omit operational focus, with back as the sole persistent action.

Home balances reflect reliable background readings; data older than 120s, refresh errors, or missing timestamps display "Status Pending", uninitialized states display "Awaiting Refresh", and missing reads show "Unavailable". Daily allowances show forecasted minutes or confirmed unlimited rules, avoiding reverse-calculation from remaining time. Normal results use compact banners; failures, deactivation, protection, recovery, pending effects, exhausted quotas, and temporary unlocks display expanded notices distinguishing "Restricted" from "Lockout may be imminent". Hiding PINs, confirmations, rollbacks, or nonce security semantics is prohibited.

Top status bars show the last confirmed mode (limit, unlimited, bedtime) and confirmation timestamp when data is stale, prompting `Y` to refresh, without presenting stale minutes as current balances. Uninitialized states display "Status unacquired | Press Y to refresh". Master bedtime switches govern rule execution; drafts can be edited and saved while off, with saved states, drafts, and skipped instances displayed separately.

## Parent Web App Pairing

Companion generates QR codes containing URL fragments using `device_id`, `grant_secret`, and optional `config.json.pairing_base_url`, defaulting to:

```text
https://selfuppen.github.io/NX-PlayWise/#device_id=<url-encoded>&grant_secret=<url-encoded>
```

URL fragments are never transmitted in HTTP request headers. Upon reading the fragment, the PWA must immediately clear address bar parameters via `history.replaceState`, validate device IDs (1–32 characters, `[A-Za-z0-9_-]`) and secrets (32–64 printable non-whitespace ASCII characters), and prompt for parental confirmation before overwriting `localStorage`. Cancellation, missing fields, or validation failures must never overwrite saved configurations.

Custom base URLs support up to 256 characters, restricted to absolute HTTPS or local development URLs (`http://localhost`, loopback, private IPs). URLs must omit userinfo, control characters, whitespace, or existing fragments, but may include paths and query parameters. Companion appends `#device_id=...&grant_secret=...` automatically. Editing or resetting URLs requires PIN authentication, accompanied by warnings that custom sites can read pairing secrets.

File pairing utilizes `sdmc:/switch/playwise/parent-import.json`: written only after parents authenticate PINs and execute "Export Configuration File". Success screens outline the path and instructions to copy files from SD `/switch/playwise/`; failure screens omit target paths. "Generate on Phone/PC" provides direct export buttons without re-prompting for PINs since QR code entry already authenticated; generation management retains standalone export buttons with dedicated PIN authentication.

```json
{"version":1,"device_id":"kid-switch","grant_secret":"<32–64 chars>"}
```

Offline grant codes rely on standard HMAC-SHA256 signatures, binding redemption to console `device_id`, secret `grant_secret`, Switch local date (Day Index), and a monotonically increasing anti-replay `nonce`. Because codes embed cryptographic signatures, each Switch console must export its own `parent-import.json` (or scan its own QR code); configurations cannot be interchanged across consoles.

The standalone `playwise-offline.html` is bundled with releases. It inlines all runtime assets, makes zero network requests, and enforces identical schemas, validation, confirmation prompts, v2 test vectors, and nonce limits. Complete delivery bundles `playwise-complete-<version>.zip` contain strictly the matching standard package `playwise-<version>.zip` and this offline HTML; nested files must be byte-identical to source builds without packaging scripts or extra documents. Opening from `file://` disables Service Workers, PWA installation prompts, and QR code target routing. When Web Crypto is unavailable, built-in fallback HMAC-SHA256 implementations are used; if secure random sources are absent, unissued daily nonces are selected sequentially. If persistent browser storage is denied, memory-only storage is allowed with notices that configs will be lost on tab closure.

Displaying QR codes, exporting files from management pages, viewing secrets, saving new keys, and generating codes on-console require PIN authentication. Exporting from authenticated QR pages skips re-authentication. When touch triggers sensitive menus, PIN dialogs ignore the initiating touch event until released. "Offline Grants" provides four menus: "Generate Code Now", "Generate on Phone/PC", "Code Generation Management", and "Grant History". Parent Portal tabs are `TODAY / PLAN / GRANT / SETTINGS / SUPPORT`. Settings provides cards for Appearance, PIN, Shortcuts, hbmenu Entry, Family Activities, Audio, and Language, paired with dynamic Inspector panels on the right. Support retains six safety actions, issue summaries, environment info, and recent events, displaying error badges directly on the tab.

"Time Schedules" divides cards into "Quota Rules (High to Low)" (Specific Date Quota, Statutory Holidays, Weekly Schedule) and "Parallel & Supplementary" (Bedtime, Self-Buffer). The right panel indicates priority: "Today's Adjustment > Specific Date Quota > Statutory Holidays > Weekly Schedule"; bedtime operates in parallel, and self-buffer appends time on restricted days. Up/Down navigates within groups, Left/Right moves to adjacent groups, and Down at group ends enters the footer. Weekly schedules, holidays, and bedtime use full-screen subpages, with `B` returning to root and restoring card focus.

Under healthy states, parent footers display stable actions (subpages show `B Back to Plan / Y Refresh`, height 44px); global health summaries are omitted. Under protection, failed, recovery, emergency deactivation, or temporary bypass states, the right side displays touchable shortcuts to Support. Stale states appear only on top status bars without promoting footer warnings. Read confirmations merge into status cards; waiting, failure, non-refresh write results, and persistent runtime states appear in bottom-right status capsules. Detail buttons appear only when error causes or suggestions exist, accessible via `X` or touch; state transitions never open dialogs automatically.

"Code Generation Management" consolidates five actions: "Manage Device ID", "Manage Grant Secret", "Export Phone/PC Config", "Edit QR Base URL", and "Restore Default QR URL", navigated via dual-column cards, directional buttons, `A`, and touch. Actions return to or remain on the management page; restoring default URLs requires PIN authentication and `B Cancel / A Confirm` reconfirmation. Inputs, random generation, and saving are distinct operations; inputs and generation update drafts, committed via `+`, with exit confirmation prompts if unsaved drafts exist.

"Generate on Phone/PC" presents two workflows: Option 1 scans the QR code online and confirms "Import Device"; Option 2 exports `sdmc:/switch/playwise/parent-import.json`, opens `playwise-offline.html` from the complete bundle, and selects "Import Configuration File". Guidance prompts saving files locally and opening them in system browsers rather than embedded webviews. The QR panel displays the full `pairing_base_url` dynamically without truncation; both paths warn that credentials contain grant secrets. Custom URLs assume official PWAs or deployments matching `tools/ptc_frontend`; Companion validates URL formats without probing network endpoints.

## On-Console Grant Code Generation

### Schedule Editing and User Hints

Companion weekly schedules, statutory holidays, and specific date quotas share draft states and today's impact explanations; machine fields remain `today_override` and `scheduled_override`, labeled "Today's Allowance Adjustment" and "Specific Date Allowance" in the UI. Quota projections display daily minutes alongside active bedtime impacts; active bedtime notes that "Adjusting quota will not immediately lift restriction", avoiding guesses when state is unknown. Previews use `ptc_rules_resolve` with fixed priority: "Today's Adjustment > Specific Date Allowance > Statutory Holidays > Weekly Schedule". Saving weekly or holiday drafts opens impact confirmation dialogs detailing modified counts, active rules, post-save rules, projected remaining time, and override rationale before sending requests; unchanged days note active override sources. If saving might exhaust quotas immediately, long-press confirmation is required; unknown states that might alter today also mandate holds. Cancellation aborts submission; failures preserve drafts and forecasts; successes transition cards to saved states with "Active / Today Unchanged". Stale data (>120s), missing timestamps, clock jumps, or refresh errors prompt refreshing before using old consumed values for remaining estimates.

Region calendar management offers B back, Y refresh, X format help and matching touch controls; L/R switches lists, Up/Down selects, Left/Right or ZL/ZR pages, and A/+ opens confirmation. Help preserves the selected list, page and item on return. Back and help remain available during refresh. On-screen help summarizes the [documented file format](../使用/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md). Plan editors share back/refresh geometry between rendering and touch, and eye care uses the shorter "Skip this break" action label.

Holiday schedule "View Holiday Calendar" controller navigation routes: Right from rule cards or save buttons enters calendar; Left returns to save buttons; Up returns to makeup workday rules; Down enters footer. Touch hitboxes match visible buttons, restoring focus upon calendar exit.

The calendar initially selects the closest holiday or makeup-workday page across the active region's covered years. Dates inside a holiday have zero distance; ties prefer upcoming dates. Empty or unreadable calendars open page one. Manual paging keeps its existing order. Import Files shows brief help and the full document URL: https://github.com/selfuppen/NX-PlayWise/blob/main/docs/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md . The Today skip-break card matches neighboring action widths and uses the same short confirmation label.

Specific date quotas utilize `set_scheduled_override`. Start dates (8-digit `YYYYMMDD`) and durations (1–366 days) are entered via system numeric keyboards; daily quotas reuse hour/minute editors, with `X` toggling limit/unlimited. Cancel or invalid inputs preserve drafts. Specific date bedtime uses bedtime's `scheduled_override`; both intervals operate independently. Draft exit, failure preservation, and refresh protection semantics remain intact.

Weekly bedtime windows and specific date custom rules share a 24-hour visual timeline and five presets: Early Bird, Standard, Balanced, Weekend, and Relaxed. Dialogs partition into "Switch/Mode, Parallel Start/End Times, Timeline & Duration, Presets, Instructions, Fixed Footer Buttons"; touching presets or pressing `Y` updates drafts. Inherited or disabled modes show neutral timeline explanations with disabled fields and presets; switching to `custom` enables editing. UI reuse preserves `PtcBedtimePolicy`, request JSON, overnight checks, and PCTL semantics.

Child pages, parent tabs, and schedule subpages render synchronized time projections: current time formats as `HH:mm`, remaining time reflects latest background state without local UI countdowns. Only fresh data within 120 seconds displays progress bars; limit denominators clamp authoritative total allowances from `forecast[0]` to 0–100%, with unlimited days showing full bars. Missing totals, stale states, or read failures show grey tracks and "Status Pending / Unavailable" rather than zero. 0m, <10m, and <30m trigger exhausted, hazardous, and warning states respectively.

Settings retained existing options. Onboarding displays progress across 3 steps; Support displays current issues and recommended actions before showing environment and events. Failed or protection states suggest retry repairs; deactivation suggests re-checking and taking over; unrecoverable transactions or recent errors suggest exporting diagnostics; in-progress actions omit secondary recommendations. Suggestions and default focus never substitute for PINs, confirmations, holds, or security checks.

### On-Console Grant Workflows

NRO state freshness evaluates to 120 seconds inclusive; data older than 120s, missing timestamps, clock jumps, or refresh errors display "Status Pending", never labeling post-failure caches as fresh. Generation pages separate "Next Grant Duration" and "Generated Code"; 8-digit codes display as `1234 5678` with grouping spaces in UI only, while inputs and requests use raw 8-digit strings. Ongoing requests prevent duplicate generation while preserving back navigation and adjustments.

Redemption success pages distinguish "Actual Added Minutes" from "Remaining Playtime After Redemption". Actual added minutes verify strictly against `ledger/redemption-history.jsonl`: requiring `redeemed_at` to match result `completed_at`, with matching dates, code durations, remaining availability, and minutes; multi-matches, missing records, or field mismatches display unavailable rather than using preview values. Remaining time reflects redemption result snapshots and completion times without resetting to current time upon recovery. Verification does not add requests or persistent fields, nor affect final success, nonces, or interruption recovery.

Generation pages label issued face values as "Code Duration", with actual added minutes governed by redemption results. If successful results exceed 120s or lack timestamps, remaining areas display "Status Pending" without accent colors, retaining verified added minutes. Failed recovery displays actionable guidance: used codes require new codes, date mismatches prompt date refreshes, validation errors prompt digit checks, cooldowns require waiting, and storage/control errors require parental attention. Failure indicates the code was unconsumed, not that expired codes are re-validated.

Parent "Generate Code Now" opens on-console generation, producing today-valid v2 8-digit codes using device keys. Default duration is 20 minutes; pages provide a focusable "Code Duration" field, with `A` or touch opening hour/minute editors, and `+` generating codes. Editors accept 1–4 minutes, 5–120 minutes (5m increments), and 150/180/210/240 minutes, respecting `max_add_minutes`; invalid inputs display supported ranges without generating incorrect codes. Modifying drafts does not alter currently displayed codes or durations.

`grant-issued.json` tracks issued nonces by `day_index`. Generators exclude both issued nonces and nonces consumed in `ledger/used_nonces.jsonl` today, selecting remaining values starting from `randomGet`. Generation does not write to ledgers; nonces are consumed only upon successful redemption and result/ledger commit. Generating new codes, adjusting durations, refreshing status, or exiting pages does not revoke issued codes; codes expire only through successful consumption, date shifts, device name changes, or key changes. UI displays code durations and creation snapshots, noting previously issued codes remain redeemable today. When all 512 daily nonces are exhausted, further generation is rejected without clearing issued sets or reusing nonces. Results display date, minutes, 8-digit code, and "Usable only once upon successful redemption."

## Write and Recovery Sequences

Standard write transactions commit in strict order:

1. Read and validate request, setup, configs, rules, and tokens;
2. Atomically persist PCTL snapshots and rules/state/ledger pre-images in `recovery/active/`;
3. Apply PCTL targets and read back settings;
4. Interactive edits invoke a single `1451` fallback only if runtime state is not ready, then read back; Enforce never invokes fallback;
5. Atomically commit rules/state/result;
6. Append nonces only for successful tokens;
7. Clear recovery transactions and archive requests.

Invalid tokens, write failures, result errors, ledger errors, and rollback paths must never consume nonces prematurely. If rollbacks cannot be proven byte-for-byte, live state is preserved and `disable.flag` created.

## Logging and Diagnostics

Valid Switch local dates write to `logs/YYYY-MM-DD/`; invalid dates write to `logs/undated/<boot-id>/`. NRO and Overlay IPC connection diagnostics write to `ipc-client.log` in matching directories, logging client types, connection phases, libnx Result codes/modules/descriptions, and actual/expected interface versions; identical errors deduplicate in-process, appending `connected` upon re-establishment, omitting payloads, PINs, keys, or codes. Support exports enforce a strict whitelist (build, config, rules, state, setup, compatibility, environment), forbidding credentials, auth, codes, raw nonces, or recovery data. Exports append an `onboarding` v1 summary: UI completion, step, pending activation, status/environment availability, reference match, parental controls as `1/0/-1`, PIN readiness, and session issues with step, stable error code and category. Categories are `language_save`, `pin_setup`, `theme_save`, `shortcut_save`, `progress_save`, `status_read`, `activation`, `install_defaults`; no arbitrary messages, PINs or request payloads are included. Parental control status is represented by `parental_control_enabled`; `official_pause_setting` is no longer exported as a manual check. Clock sync is not detected. Export works with the available local data even without a backend. Exports also append a sanitized `runtime_snapshot`: latest status read timestamp, cache age, timer mode, remaining/played availability and minutes, restriction state, rule source, emergency flag, active recovery transactions, in-flight status, and transport type. This snapshot indicates cache age without reading PCTL or exposing raw layouts. Progress, success, and failure appear on "Support & Recovery"; success displays full file paths, instructions for attaching files to GitHub Issues, and `https://github.com/selfuppen/NX-PlayWise/issues`.

## Device Lab Protocol Boundaries

Device Lab retains legacy experiment fields and slots such as `original_pause_state`, `pause_on_game_a`, `pause_on_game_b`, and `pause_off_game_b`. These came from an earlier suspension-switch assumption; they do not establish that the setting exists on the current console or add a standard setup requirement. Do not fabricate prerequisites or claim full qualification when hardware cannot establish them. The following describes the retained experiment protocol; actual software behavior must be recorded independently.

Device Lab operates under `profile=device-lab`, `pwtl:u`, Title ID `4200000000BD23F0`, and `sdmc:/switch/playwise-device-lab`. Probes and guided requests compile and dispatch only under `PLAYWISE_DEVICE_LAB`; release package scanning rejects these identifiers:

NRO uses schema v2 `lab/boot-switch.json` to persist boot flag transitions between standard packages and the Lab, reading legacy schema v1 interruptions. Because Switch SD renames do not guarantee overwriting targets, journal updates write fully to adjacent `.tmp` files before removing old generations and promoting new ones; coexisting generations, lone `.tmp` files, or incomplete `.tmp` files recover automatically without operator intervention. Unknown backups, non-empty Lab flags, or conflicting journals reject overwriting.

Standard and Lab sysmodules share a handover protocol free of Lab requests or dangerous PCTL capabilities: initiators write `handover/intent.json` with random transaction IDs; source processes stop accepting IPC submissions, complete ongoing serialized operations, and write `handover/ready.json` when no active recovery exists. IPC interface version is v2, returning `QUIESCING` during quiescence without client downgrades to SD queues. NRO launches targets via `NcmStorageId_None` only after committing boot journals and proving source PID termination via `pm:shell`; target `handover/runtime-ready.json` must match PID, boot ID, profile, and release ID. If safety cannot be verified, launching a second owner is forbidden, falling back to reboot workflows (at most one reboot per boundary).

NRO and Overlay Chinese interfaces translate status, phases, verdicts, and error descriptions without altering English machine identifiers in requests, sessions, phases, or reports. Corrupted `session.json` prevents starting new phases; restoring standard boot flags mandates persistent `exact_restore_proved`.

- `lab_campaign_start`: initiates persistent qualification campaigns, recording legacy experiment states `payload.original_pause_state=on|off`;
- `lab_campaign_status`: read-only query for campaign ID, status, entry method, next slot, and attempt counts;
- `lab_campaign_abandon`: archives campaigns when no un-restored sessions exist, without deleting drafts, failed reports, or accepted reports;
- `lab_session_start`: saves original `0x44` and timer states, executing raw/libnx comparisons for `1006/1031/1035/1457/1458` and `195101` identical write-backs; `payload.mode` accepts `restriction_quick`, `timer_activation_ab`, or `full`. Qualification sessions require matching campaign IDs, fixed slots, anonymous `game_slot`, legacy suspension expectations, and `context_confirmed=true`. A/B mode requires non-empty weekly schedules; missing modes default to `full` for legacy compatibility;
- `lab_phase_start`: `restriction_quick` executes `restriction_effect` once; `full` executes `home_stopped`, `home_started`, `game_foreground`, `game_suspended`, `sleep_wake`, and `restriction_effect` sequentially; `timer_activation_ab` executes `ab_home_awake`, `ab_sleep_wake`, `ab_limited_settings_only`, `ab_restriction_settings_only`, `ab_grant_settings_only`, `ab_restriction_before_unlimited`, and `ab_unlimited_settings_only`. A/B `1451` fallback executes inside corresponding settings-only phases. Standard phases sample for 75s; A/B HOME/standby phases sample for 90s; remaining A/B and restriction phases sample for 15s;
- `lab_session_status`: read-only query for persistent phases, deadlines, recovery status, and report paths;
- `lab_observation`: alert visibility `payload.observation` accepts `restriction_visible`, `no_visible_restriction`, `unsure`; runtime behavior `payload.runtime_effect` accepts `continued`, `paused_or_suspended`, `exited`, `unsure` (omitted values evaluate to `unsure`);
- `lab_session_restore`: idempotent restoration of original settings and timers from any phase; if status is `awaiting_observation`, successful restoration retains this status without substituting for `lab_observation` or marking reports complete.

Phases cannot be skipped or reordered. Samples record monotonic timestamps, raw Results/values for `1453/1454/1455/1952`, and full `0x44` hex/SHA-256; `1952` exists exclusively in Lab reports, never in release `played_minutes`. The sysmodule enforces restriction phase deadlines independently of overlay survival; `1457` is polled non-blockingly every 100ms, latching initial triggers for forensic reference without affecting completeness, visibility, or qualification. Incomplete restorations write `flags/disable.flag`, halt writes, and set sessions to `restore_required`. Full mode marks foreground/suspended/standby phases `precondition_not_met` if baseline `0x44` is all zeros; A/B mode rejects all-zero baselines immediately.

Active sessions write `lab/report-<run-id>.draft.json` with `report_status:"draft"`; focused/full modes publish `reports/<run-id>.json` and delete drafts only after required phases, two-tier observations, and exact restorations complete; A/B mode publishes after all seven automated phases and exact restore complete. Anomaly drafts are preserved for forensics, but excluded from NRO report lists. Reports use `version:2`/`schema_version:2`; v1 reports serve as historical evidence only. A/B reports log settings-only conditions, fallback execution, and success in `timer_activation_ab.fallback_cases` across `limited`, `grant`, and `unlimited` targets, preserving pre-write `0x44`, post-write readings, and pre/post fallback values. Deductions during standby that cannot exclude wake-up screen-on time evaluate to `inconclusive`/`null`. Reports embed actual `environment.json` and Device Lab `build.json`; missing identity files halt execution before hazardous operations. Commands `1006/1031/1035/1457/1458` provide `comparable`, evaluating to true only if raw and libnx Results succeed.

Campaigns define four fixed slots: Timer activation A/B, legacy `pause_on_game_a`, `pause_on_game_b`, and `pause_off_game_b`. Session run IDs embed unique random suffixes, adding campaign slots, attempt numbers, anonymous game slots, and `entry_method=hot_switch|reboot`. Only final, verified (`exact_restore_proved`), slot-compliant runs advance campaigns; failed attempts allow retrying the current slot. Completing all four writes schema v1 `reports/<campaign-id>.campaign.json` listing the four accepted run IDs, which qualification tools validate exclusively.

Restriction phase `settings_write_scope` preserves all mutations alongside legacy `outside_today_changed_offsets`, categorizing expected global header initialization, daily slots, and unexpected mutations. Release adapters initialize headers to `0x0101/0x0001` when enabling limit days, making bytes `0, 1, 2` expected header initialization rather than arbitrary corruption. Product verdicts evaluate via `unexpected_changed_offsets` and `unexpected_bytes_unchanged`.

Reports separate `ipc_callable`, `wire_shape_confirmed`, and `product_semantics`. New reports log factual `home_usage_counted` when HOME usage accumulates; `unsafe_for_home_start` remains for schema v1 parsing compatibility. `summary` provides automated phase counts, observation states, and `complete`; focused mode requires 1/1, full mode requires 6/6 with two-tier observations and exact restore, and A/B mode requires 7/7 with exact restore. `complete` denotes execution completeness, not that all product semantics are proven. `activation_evidence_complete` applies exclusively to A/B mode; `lifecycle_evidence_complete` applies to full mode (`false` on all-zero baselines). Reports omit PINs, secrets, tokens, credentials, rules, or user configs. Lab evidence informs human review, never enabling `1952`, forced lockouts, suspensions, or app exits, and never automatically altering release qualification baselines.

The read-only status `environment` object also includes optional `atmosphere_version`; missing or empty means unconfirmed. Displaying it adds no firmware qualification or control gate.

## In-app troubleshooting guide

Parent Support & recovery adds a read-only four-page FAQ with controller/touch paging and focus preservation. Fresh status availability and controls boolean project on/off/unknown; clock sync is always unchecked and requires manual verification. Help covers system setup, DBI/QuickNTP sync followed by refresh and actual counting/restriction tests, temporary unlock, codes, overlay recovery, backend and diagnostics. Enabled or 0 remaining does not prove actual blocking. Viewing/paging neither writes PCTL nor syncs time nor submits requests; protocol fields and confirmation markers are unchanged.
