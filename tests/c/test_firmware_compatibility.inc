static void test_firmware_compatibility(void)
{
    /* First install, system upgrade, AMS-only upgrade, digest-only change, then
       independent snapshot/status/layout/storage/identity failure boundaries. */
    for (int sample = 0; sample < 19; ++sample) {
        PtcMemStorage mem;
        PtcPctlStub pctl;
        PtcFakeTime clock;
        PtcSysmodule sysmodule;
        char text[8192], environment[512], accepted[2048];
        const char *hos = sample == 13 ? "18.1.0" : sample == 2 || sample == 3 ? "22.5.0" : "23.0.1";
        const char *ams = sample == 13 ? "1.7.1" : sample == 3 ? "1.11.2" : "1.12.1";
        bool success = sample < 4 || sample == 8 || sample == 10 || (sample >= 11 && sample < 18);
        const char *expected_status = sample == 3 ? "verified" : "accepted_unknown";
        ptc_mem_storage_init(&mem);
        ptc_pctl_stub_init(&pctl);
        pctl.status.unrestricted_today = true;
        pctl.status.play_timer_enabled = true;
        ptc_fake_time_init(&clock, 1783526401, 2380, 720);
        ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &clock.provider);
        seed_release_setup(&mem);
        if (sample > 0 && sample != 16) {
            check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
                "{\"version\":1,\"phase\":\"active\",\"compatibility_status\":\"verified\","
                "\"restriction_cleared\":true,\"snapshot_available\":true,\"activate_after\":0}"),
                "seed active install before upgrade");
            check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/compatibility.json",
                "{\"version\":1,\"status\":\"verified\",\"environment\":{\"hos\":\"22.5.0\","
                "\"firmware_hash\":\"test-hash\",\"model\":\"mariko-oled\",\"atmosphere\":true,"
                "\"atmosphere_version\":\"1.11.2\"},\"release_id\":\"playwise-" PLAYWISE_VERSION "+test\"}"),
                "seed previously accepted environment");
            check_true(mem.storage.vtable->read_text(&mem.storage, "app/compatibility.json", accepted, sizeof(accepted)),
                "retain accepted environment for failure checks");
            check_true(mem.storage.vtable->write_text_atomic(&mem.storage,
                "app/backups/install_pctl_snapshot.json", "original-install-snapshot"), "seed immutable recovery image");
        }
        snprintf(environment, sizeof(environment),
            "{\"read_ok\":%s,\"hos\":\"%s\",\"firmware_hash\":\"%s\",\"model\":\"%s\","
            "\"atmosphere\":true,\"atmosphere_version\":\"%s\"}",
            sample == 10 || sample >= 16 ? "false" : "true", hos, sample == 2 ? "test-hash" : "new-hash",
            sample >= 16 ? "unknown" : sample == 8 ? "mariko" : "mariko-oled", ams);
        check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/environment.json", environment),
            "seed reported new firmware environment");
        if (sample == 4) pctl.snapshot_error = PTC_ERR_PCTL_READ_FAILED;
        if (sample == 5) pctl.read_status_fail_on_call = 1;
        if (sample == 6) {
            pctl.raw_settings_override_enabled = true;
            pctl.raw_settings[PTC_PLAY_TIMER_HEADER_WORDS * 2] = 1; /* Invalid day flag. */
        }
        if (sample == 7) mem.fail_write_path_contains = "compatibility.json";
        if (sample == 9) check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/build.json",
            "{\"profile\":\"release\",\"playwise_version\":\"" PLAYWISE_VERSION "\",\"release_id\":\"other-build\"}"),
            "seed mismatched build identity");
        if (sample == 11) check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
            "{\"version\":1,\"phase\":\"released\",\"compatibility_status\":\"verified\","
            "\"restriction_cleared\":true,\"snapshot_available\":true,\"activate_after\":1783526406}"),
            "seed unexpired setup grace before upgrade");
        if (sample == 12) check_true(mem.storage.vtable->write_text_atomic(&mem.storage,
            "app/flags/disable.flag", "manual-disable\n"), "seed existing manual deactivation");
        if (sample == 14) check_true(mem.storage.vtable->remove_path(&mem.storage, "app/environment.json"),
            "missing environment file is advisory");
        if (sample == 15) check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/environment.json",
            "{\"read_ok\":true,\"hos\":\"23.0.1\",\"model\":\"mariko-oled\",\"atmosphere\":false,\"atmosphere_version\":null}"),
            "unknown Atmosphere version is advisory");
        if (sample == 18) pctl.snapshot_error = PTC_ERR_PCTL_READ_FAILED;

        if (sample == 0 || sample == 16) {
            check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/inbox/pending/setup-new-firmware.json",
                "{\"version\":1,\"request_id\":\"setup-new-firmware\",\"type\":\"complete_setup\","
                "\"created_at\":1,\"payload\":{}}"), "queue high firmware initial setup");
            check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process high firmware initial setup");
            check_true(mem.storage.vtable->read_text(&mem.storage, "app/results/setup-new-firmware.json", text, sizeof(text)) &&
                strstr(text, "\"status\":\"ok\""), "new firmware can complete initial setup");
            if (sample == 16) {
                check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/setup.json",
                    "{\"version\":1,\"phase\":\"pending\",\"compatibility_status\":\"accepted_unknown\","
                    "\"snapshot_available\":true,\"handover_today_pending\":true,\"handover_day_index\":2380,"
                    "\"handover_unlimited\":true}"), "seed interrupted takeover after recovery");
                check_true(mem.storage.vtable->write_text_atomic(&mem.storage, "app/flags/disable.flag",
                    "startup_transaction_restored\n"), "retain recovery stop until parent retries");
                for (int attempt = 0; attempt < 2; ++attempt) {
                    char request[256], path[160];
                    pctl.snapshot_error = attempt == 0 ? PTC_ERR_PCTL_READ_FAILED : PTC_ERR_OK;
                    snprintf(path, sizeof(path), "app/inbox/pending/partial-retry-%d.json", attempt);
                    snprintf(request, sizeof(request), "{\"version\":1,\"request_id\":\"partial-retry-%d\","
                        "\"type\":\"retry_setup_release\",\"created_at\":2,\"payload\":{}}", attempt);
                    check_true(mem.storage.vtable->write_text_atomic(&mem.storage, path, request), "queue parent repair of partial metadata setup");
                    check_int(ptc_sysmodule_process_all(&sysmodule), 1, "process known failed or successful repair");
                    snprintf(path, sizeof(path), "app/results/partial-retry-%d.json", attempt);
                    check_true(mem.storage.vtable->read_text(&mem.storage, path, text, sizeof(text)) &&
                        strstr(text, attempt == 0 ? "pctl_backup_failed" : "\"status\":\"ok\""),
                        "repair requires successful real PCTL checks even with advisory metadata");
                    check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag") == (attempt == 0),
                        "repair removes stop only after proven success");
                }
            }
        } else {
            check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), success ? 0 : -1,
                "version drift requires actual read-only compatibility");
            check_true(mem.storage.vtable->read_text(&mem.storage, "app/backups/install_pctl_snapshot.json", text, sizeof(text)) &&
                !strcmp(text, "original-install-snapshot"), "upgrade leaves installation recovery image intact");
            if (!success) {
                check_true(mem.storage.vtable->read_text(&mem.storage, "app/compatibility.json", text, sizeof(text)) &&
                    !strcmp(text, accepted), "failed checks never accept the new environment");
                check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag"),
                    "failed compatibility checks keep protection enabled");
            }
        }
        if (success) {
            check_true(mem.storage.vtable->read_text(&mem.storage, "app/setup.json", text, sizeof(text)) &&
                strstr(text, sample == 11 ? "\"phase\":\"released\"" : "\"phase\":\"active\"") &&
                strstr(text, expected_status),
                "new firmware remains usable without claiming qualification");
            check_true(mem.storage.vtable->read_text(&mem.storage, "app/compatibility.json", text, sizeof(text)) &&
                strstr(text, sample == 14 ? "unknown" : hos) &&
                strstr(text, sample == 14 || sample == 15 ? "unknown" : ams) && strstr(text, expected_status),
                "compatibility records actual system and AMS versions");
            check_true(mem.storage.vtable->exists(&mem.storage, "app/flags/disable.flag") == (sample == 12),
                "version difference neither creates nor clears a disable flag");
            ptc_sysmodule_init(&sysmodule, "app", &mem.storage, &pctl.pctl, &clock.provider);
            check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 0, "accepted environment survives a repeated bootstrap");
            unsigned int reads = pctl.read_status_calls;
            check_int(ptc_sysmodule_bootstrap_setup(&sysmodule), 0, "partial metadata does not block later ticks");
            check_int((int)pctl.read_status_calls, (int)reads, "partial metadata does not trigger repeated PCTL polling");
        }
        check_int((int)pctl.apply_target_calls, 0, "compatibility check does not rewrite daily allowance");
        check_int((int)pctl.start_timer_calls, 0, "compatibility check does not restart timer");
        check_true(!pctl.timer_stopped && !pctl.restore_called, "compatibility check never stops or restores PCTL");
    }
}
