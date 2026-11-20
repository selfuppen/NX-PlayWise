"""Exercise authorization, stale confirmations and pairing recovery in Eden."""
from __future__ import annotations

from pathlib import Path

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import decode, digest, read, require, save
from tools.lib.ptc_token_v2 import encode_token, tier_for_minutes

SAFEGUARD_SCENARIOS = ("setup", "pin_cooldown", "pairing_restore", "quota_recheck")


def verify_safeguards(output: Path) -> None:
    def ui(name: str) -> dict:
        return read(output / f"checkpoints/{name}.json")["ui"]

    first = read(output / "checkpoints/pin-locked.json")
    reopened = read(output / "checkpoints/pin-locked-reopened.json")
    require(first["instance"] != reopened["instance"] and
            all(row["ui"]["overlay"] == 13 and row["ui"]["auth_cooldown_seconds"] > 0
                for row in (first, reopened)) and ui("pin-read-only")["parent_support_only"] is True and
            ui("pin_cooldown")["parent_support_only"] is False and
            read(output / "app-final/auth-counters.json") == {"failed_attempts": 0, "cooldown_until": 0},
            "PIN cooldown evidence differs")
    auth = read(output / "assertions/pin_cooldown.json")
    require(auth["before_reopen"] == auth["after_reopen"] and
            auth["before_reopen"]["failed_attempts"] == 5 and auth["before_reopen"]["cooldown_until"] > 0,
            "reopen changed the cooldown deadline")

    pairing = read(output / "assertions/pairing_restore.json")
    ledger = output / "app-final/ledger/used_nonces.jsonl"
    entries = [decode(line) for line in ledger.read_text(encoding="utf-8").splitlines()]
    require(pairing["old_export_sha256"] != pairing["new_export_sha256"] and
            pairing["ledger_before"] == pairing["ledger_after_restore"] == pairing["ledger_after_rejection"] and
            digest(ledger) == pairing["ledger_after_new_code"] and len(entries) == 2 and
            [entry["nonce"] for entry in entries] == [0, pairing["new_nonce"]],
            "pairing rotation or retained nonce evidence differs")
    for key, kind, status in (("restore_id", "restore_config_backup", "ok"),
                              ("old_preview_id", "preview_offline_code", "error"),
                              ("new_grant_id", "offline_code", "ok")):
        row = read(output / f"receipts/{pairing[key]}.json")
        require((row["request_id"], row["type"], row["status"]) == (pairing[key], kind, status),
                f"pairing receipt differs: {key}")
        if key == "old_preview_id":
            require(row["error"]["code"] == 203, "old signing key was not rejected")

    before, changed, failed = (ui(f"quota-{phase}") for phase in ("before", "changed", "failed"))
    quota = read(output / "assertions/quota_recheck.json")
    minutes = quota["draft_minutes"]
    require(0 <= before["played_minutes"] < minutes <= changed["played_minutes"] and
            before["confirm_hold_required"] is False and
            changed["confirm_hold_required"] is True and failed["confirm_hold_required"] is True and
            all(row["overlay"] == 3 and row["quota_draft_minutes"] == minutes and
                row["confirm_hold_progress"] == 0 for row in (before, changed, failed)) and
            changed["quota_refresh_failed"] is False and failed["quota_refresh_failed"] is True and
            quota["rules_before"] == quota["rules_after_change"] == quota["rules_after_failure"],
            "quota confirmation or failed refresh evidence differs")
    reconfirmation = quota.get("reconfirmation")
    if reconfirmation is not None:
        status = read(output / f"receipts/{reconfirmation['status_id']}.json")
        updated = ui("quota-reconfirmed-status")
        require(status["type"] == "status" and status["status"] == "ok" and
                status["state"]["played_minutes"] == updated["played_minutes"] > failed["played_minutes"] and
                updated["overlay"] == 3 and updated["confirm_hold_required"] is True and
                updated["quota_refresh_failed"] is False and updated["quota_draft_minutes"] == minutes and
                reconfirmation["rules_sha256"] == quota["rules_before"],
                "second status change did not preserve the draft for explicit reconfirmation")
    writes = [read(path) for path in (output / "receipts").glob("*.json")]
    writes = [row for row in writes if row["type"] == "set_today_limit"]
    require(len(writes) == 1 and writes[0]["request_id"] == quota["write_id"] and
            writes[0]["status"] == "ok" and
            read(output / "app-final/rules.json")["today_override_minutes"] == minutes,
            "fresh held quota confirmation did not persist")


class SafeguardFlow(MainFlow):
    def execute(self) -> None:
        self.start()
        for name in SAFEGUARD_SCENARIOS:
            self.run_case(name, getattr(self, name))

    def auth_counters(self) -> dict:
        auth = read(self.app / "auth.json")
        return {key: auth[key] for key in ("failed_attempts", "cooldown_until")}

    def pin_cooldown(self) -> None:
        for attempt in range(1, 6):
            self.pin_open()
            self.enter_pin("000")
            self.wait_ui(view=0, overlay=13)
            require(self.auth_counters()["failed_attempts"] == attempt, "failed PIN count differs")
            if attempt < 5:
                self.action(button="B")
                self.wait_ui(view=0, overlay=0)
        before = self.auth_counters()
        require(before["cooldown_until"] > 0, "fifth wrong PIN did not persist the cooldown")
        self.capture("pin-locked")
        self.reopen()
        self.action(button="Minus")
        self.wait_ui(view=0, overlay=13)
        self.action(button="A")
        self.wait_ui(view=0, overlay=13)
        after = self.auth_counters()
        require(after == before, "reopen cleared or extended the PIN cooldown")
        self.capture("pin-locked-reopened")
        self.action(button="X")
        self.wait_ui(view=1, parent_page=4, overlay=0, parent_support_only=True)
        self.capture("pin-read-only")
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        self.action(button="Minus")
        entry = self.wait_ui(view=0, waiting=False)["ui"]
        if entry["overlay"] == 13:
            self.wait_ui(overlay=13, auth_cooldown_seconds=0)
            self.action(button="A")
        self.wait_ui(overlay=22, pin_length=0)
        self.enter_pin("110")
        self.wait_ui(view=1, overlay=0, parent_support_only=False)
        require(self.auth_counters() == {"failed_attempts": 0, "cooldown_until": 0},
                "successful PIN failed to clear the cooldown record")
        self.checked("pin_cooldown", ["cooldown survives NRO reopen", "read-only support remains usable",
                                      "normal PIN works after expiry"], before_reopen=before, after_reopen=after)

    def export_pairing(self) -> tuple[dict, str]:
        self.page(2)
        self.action(target="card_1")
        self.wait_ui(overlay=22, pin_length=0)
        self.enter_pin("110")
        self.wait_ui(overlay=7, waiting=False)
        self.action(button="A")
        self.wait_ui(overlay=38, waiting=False)
        path = self.app / "parent-import.json"
        pairing = read(path), digest(path)
        self.action(button="B")
        self.wait_ui(overlay=7)
        self.action(button="B")
        self.wait_ui(overlay=0, waiting=False)
        return pairing

    def pairing_restore(self) -> None:
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        self.code_success()
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        old, old_hash = self.export_pairing()
        ledger = self.app / "ledger/used_nonces.jsonl"
        initial = digest(ledger)
        self.page(4)
        self.action(target="card_3")
        self.wait_ui(overlay=44)
        before = self.ids()
        self.action(target="backup_13")
        self.wait_ui(overlay=22)
        self.enter_pin("110")
        self.submitted("create_config_backup", before)
        self.wait_ui(overlay=44, config_backup_ready=True)
        self.action(target="backup_17")
        self.action(target="backup_12")
        self.wait_ui(config_groups=0)
        self.action(target="backup_10")
        self.wait_ui(config_groups=1024)
        before = self.ids()
        self.action(target="backup_14")
        self.wait_ui(overlay=22)
        self.enter_pin("110")
        self.wait_ui(overlay=3, confirm_hold_required=True)
        restored = self.submitted("restore_config_backup", before)
        self.wait_ui(overlay=44)
        after_restore = digest(ledger)
        require(after_restore == initial, "pairing restore changed consumed nonces")
        self.action(button="B")
        self.wait_ui(overlay=0)
        fresh, new_hash = self.export_pairing()
        device = old["device_id"]
        require(fresh["device_id"] == device and fresh["grant_secret"] != old["grant_secret"],
                "pairing restore did not export a rotated key for the same device")
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        tier = tier_for_minutes(30)
        for nonce in range(1, 512):
            old_code = encode_token(tier, nonce, device, old["grant_secret"], self.code_day)
            new_code = encode_token(tier, nonce, device, fresh["grant_secret"], self.code_day)
            if old_code != new_code:
                break
        else:
            raise RuntimeError("rotated key produced no distinct test code")
        rules = digest(self.app / "rules.json")
        denied = self.wait_receipt("preview_offline_code", self.type_code(old_code))
        require(denied["status"] == "error" and denied["error"]["code"] == 203,
                "unused code from old pairing was not rejected")
        after_rejection = digest(ledger)
        require(after_rejection == initial and digest(self.app / "rules.json") == rules,
                "rejected old pairing changed rules or ledger")
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        preview = self.wait_receipt("preview_offline_code", self.type_code(new_code))
        require(preview["status"] == "ok" and preview["preview"]["effective_add_minutes"] == 30,
                "rotated pairing code failed preview")
        self.wait_ui(overlay=3, waiting=False)
        before = self.ids()
        self.action(button="A")
        grant = self.wait_receipt("offline_code", before)
        require(grant["status"] == "ok", "rotated pairing code was not redeemed")
        self.wait_ui(overlay=12, code_result_failed=False, code_result_pending=False)
        require(read(self.app / "rules.json")["today_override_minutes"] == 180 and
                ledger.read_bytes().startswith(self.ledger), "new grant did not extend allowance and ledger")
        self.checked("pairing_restore", ["backup import rotates the exported signing key",
                     "old unused code fails while new code redeems and prior nonce remains"],
                     old_export_sha256=old_hash, new_export_sha256=new_hash, new_nonce=nonce,
                     ledger_before=initial, ledger_after_restore=after_restore,
                     ledger_after_rejection=after_rejection, ledger_after_new_code=digest(ledger),
                     restore_id=restored["request_id"], old_preview_id=denied["request_id"],
                     new_grant_id=grant["request_id"])

    def quota_fixture(self, name: str) -> Path:
        path = self.app / "support/automation-quota-status.txt"
        require(not path.exists(), "prior Eden status fixture was not consumed")
        pending = path.with_suffix(".tmp")
        with pending.open("x", encoding="ascii") as handle:
            handle.write(name)
        pending.replace(path)
        return path

    def quota_recheck(self) -> None:
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        self.today_action(2, "disable_today_limit")
        self.refresh(unrestricted_today=1)
        played = self.observe()["ui"]["played_minutes"]
        require(0 <= played < 1439, "fresh simulated use is unavailable or too high for a risk boundary")
        minutes = played + 1
        self.action(target="card_0")
        self.wait_ui(overlay=20)
        self.action(button="L")
        self.duration(minutes)
        self.wait_ui(overlay=3, confirm_hold_required=False, quota_draft_minutes=minutes)
        baseline = digest(self.app / "rules.json")
        self.capture("quota-before")
        before = self.ids()
        advance = self.quota_fixture("advance")
        self.confirm()
        self.wait_receipt("status", before)
        changed = self.wait_ui(overlay=3, waiting=False, confirm_hold_required=True, confirm_hold_progress=0)
        require(not advance.exists() and changed["ui"]["played_minutes"] >= minutes,
                "fresh use did not cross the draft allowance and require a held confirmation")
        self.capture("quota-changed")
        after_change = digest(self.app / "rules.json")
        failure = self.quota_fixture("fail")
        self.action(button="A", observe_quota_hold=True)
        failed = self.wait_ui(overlay=3, waiting=False, quota_refresh_failed=True,
                              quota_draft_minutes=minutes, confirm_hold_progress=0)
        self.capture("quota-failed")
        after_failure = digest(self.app / "rules.json")
        require(not failure.exists() and baseline == after_change == after_failure and
                all(read(self.app / f"results/{request_id}.json")["type"] == "status"
                    for request_id in self.ids() - before),
                "a write escaped the quota recheck or failed refresh")
        before = self.ids()
        self.action(button="A", observe_quota_hold=True)
        refreshed = self.wait_receipt("status", before)
        ready = self.wait_ui(waiting=False, confirm_hold_progress=0, quota_refresh_failed=False)
        reconfirmation = None
        if ready["ui"]["overlay"] == 3:
            # A simulated minute can elapse during the held retry. The UI must
            # show the new usage and keep the write pending on a new decision.
            current = ready["ui"]
            rules_sha256 = digest(self.app / "rules.json")
            require(refreshed["state"]["played_minutes"] == current["played_minutes"] > failed["ui"]["played_minutes"] and
                    current["confirm_hold_required"] is True and current["quota_draft_minutes"] == minutes and
                    rules_sha256 == baseline and
                    all(read(self.app / f"results/{request_id}.json")["type"] == "status"
                        for request_id in self.ids() - before) and
                    not any(path.is_file() for folder in ("pending", "processing")
                            for path in (self.app / "inbox" / folder).glob("*.json")),
                    "confirmation remains open without a verified new usage endpoint")
            save(self.output / "checkpoints/quota-reconfirmed-status.json", ready)
            reconfirmation = {"status_id": refreshed["request_id"], "rules_sha256": rules_sha256}
            self.action(button="A", observe_quota_hold=True)
        result = self.wait_receipt("set_today_limit", before)
        require(result["status"] == "ok", "fresh quota confirmation did not submit")
        self.wait_ui(overlay=0, waiting=False)
        self.checked("quota_recheck", ["crossing the draft allowance now requires a hold", "failed refresh keeps draft",
                     "only the fresh confirmation writes"], rules_before=baseline,
                     rules_after_change=after_change, rules_after_failure=after_failure,
                     draft_minutes=minutes, write_id=result["request_id"], reconfirmation=reconfirmation)
