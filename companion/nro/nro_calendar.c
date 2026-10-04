#include "nro_app_internal.h"

int holiday_calendar_view_pages(const PtcUiModel *model)
{
    size_t count;
    if (!model) return 1;
    if (!model->calendar_builtin && !model->holiday_calendar_data) return 1;
    count = model->holiday_calendar_data
        ? model->holiday_calendar_data->group_count + model->holiday_calendar_data->workday_count
        : ptc_holiday_calendar_arrangement_count(2026);
    return count ? (int)((count + 3u) / 4u) : 1;
}

static void load_calendar_view_year(UiState *ui)
{
    PtcCalendarIndex *active = NULL;
    PtcCalendarRuntime *runtime = NULL;
    uint16_t jan1;
    size_t i;
    ui->model.holiday_calendar_data = NULL;
    ui->model.holiday_calendar_page = 0;
    if (ui->calendar_view_year_index < 0 ||
        ui->calendar_view_year_index >= ui->calendar_view_year_count) return;
    ui->model.holiday_calendar_year = ui->calendar_view_years[ui->calendar_view_year_index];
    ui->model.holiday_calendar_has_previous_year = ui->calendar_view_year_index > 0;
    ui->model.holiday_calendar_has_next_year =
        ui->calendar_view_year_index + 1 < ui->calendar_view_year_count;
    if (ui->model.calendar_builtin) return;
    active = malloc(sizeof(*active));
    runtime = malloc(sizeof(*runtime));
    if (!active || !runtime || !ui->calendar_view_data ||
        !ptc_calendar_index_load(ui->client.storage, APP_ROOT, true, active, NULL) ||
        !ptc_calendar_selection_validate(ui->client.storage, APP_ROOT, active) ||
        !ptc_day_index_from_date(ui->model.holiday_calendar_year, 1, 1, &jan1) ||
        !ptc_calendar_runtime_from_selection(ui->client.storage, APP_ROOT, active,
            jan1, runtime)) goto done;
    for (i = 0; i < runtime->set.count; ++i) {
        if (runtime->set.years[i]->year != ui->model.holiday_calendar_year) continue;
        *ui->calendar_view_data = *runtime->set.years[i];
        ui->model.holiday_calendar_data = ui->calendar_view_data;
        break;
    }
done:
    free(runtime);
    free(active);
}

void open_holiday_calendar_view(UiState *ui)
{
    PtcCalendarIndex *active = NULL;
    uint16_t current_year = 2026;
    uint8_t month, day;
    size_t i;
    if (!ui) return;
    (void)ptc_date_from_day_index(ui->model.day_index, &current_year, &month, &day);
    ui->calendar_view_year_count = 0;
    ui->calendar_view_year_index = 0;
    if (!ui->calendar_view_data) ui->calendar_view_data = malloc(sizeof(*ui->calendar_view_data));
    if (ui->model.calendar_builtin) {
        ui->calendar_view_years[ui->calendar_view_year_count++] = 2026;
    } else {
        active = malloc(sizeof(*active));
        if (!active || !ptc_calendar_index_load(ui->client.storage, APP_ROOT, true, active, NULL) ||
            !ptc_calendar_selection_validate(ui->client.storage, APP_ROOT, active)) goto done;
        for (i = 0; i < active->count; ++i)
            ui->calendar_view_years[ui->calendar_view_year_count++] = active->entries[i].year;
        for (i = 1; i < (size_t)ui->calendar_view_year_count; ++i) {
            uint16_t value = ui->calendar_view_years[i];
            size_t j = i;
            while (j > 0 && ui->calendar_view_years[j - 1] > value) {
                ui->calendar_view_years[j] = ui->calendar_view_years[j - 1];
                --j;
            }
            ui->calendar_view_years[j] = value;
        }
        for (i = 0; i < (size_t)ui->calendar_view_year_count; ++i)
            if (ui->calendar_view_years[i] == current_year) {
                ui->calendar_view_year_index = (int)i;
                break;
            }
    }
done:
    free(active);
    load_calendar_view_year(ui);
    ui->model.overlay = PTC_UI_OVERLAY_HOLIDAY_CALENDAR;
    ui->model.overlay_selection = 2;
    {
        const char *title = ui->model.calendar_region_name[0] ? ui->model.calendar_region_name :
            (ui->model.calendar_builtin ? ptc_ui_text(PTC_UI_T_CALENDAR_BUILTIN_CHINA) :
             ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE));
        size_t bytes = strlen(title);
        if (bytes >= sizeof(ui->model.overlay_title)) {
            bytes = sizeof(ui->model.overlay_title) - 1u;
            while (bytes > 0 && ((unsigned char)title[bytes] & 0xc0u) == 0x80u)
                --bytes;
        }
        memcpy(ui->model.overlay_title, title, bytes);
        ui->model.overlay_title[bytes] = '\0';
    }
}

void holiday_calendar_view_step(UiState *ui, int direction)
{
    int next;
    if (!ui || (direction != -1 && direction != 1)) return;
    next = ui->model.holiday_calendar_page + direction;
    if (next >= 0 && next < holiday_calendar_view_pages(&ui->model)) {
        ui->model.holiday_calendar_page = next;
        return;
    }
    next = ui->calendar_view_year_index + direction;
    if (next < 0 || next >= ui->calendar_view_year_count) return;
    ui->calendar_view_year_index = next;
    load_calendar_view_year(ui);
    if (direction < 0)
        ui->model.holiday_calendar_page = holiday_calendar_view_pages(&ui->model) - 1;
}

static void calendar_preview(UiState *ui)
{
    PtcUiModel *model = &ui->model;
    PtcUiCalendarRow *row;
    PtcImportedCalendarYear *imported = NULL;
    PtcCalendarIndex *catalog = NULL, *selected = NULL, *active = NULL;
    PtcCalendarRuntime *runtime = NULL;
    char *text = NULL, path[320], years[96] = {0};
    uint16_t year;
    uint8_t month, day;
    size_t i;
    model->calendar_preview_count = 0;
    model->calendar_pending_file[0] = '\0';
    model->calendar_pending_sha256[0] = '\0';
    model->calendar_pending_option_id[0] = '\0';
    model->calendar_pending_catalog_sha256[0] = '\0';
    if (model->calendar_manager_selected < 0 ||
        model->calendar_manager_selected >= model->calendar_manager_count) return;
    row = &model->calendar_manager_rows[model->calendar_manager_selected];
    catalog = malloc(sizeof(*catalog));
    selected = malloc(sizeof(*selected));
    active = malloc(sizeof(*active));
    imported = malloc(sizeof(*imported));
    runtime = malloc(sizeof(*runtime));
    text = malloc(PTC_CALENDAR_IMPORT_MAX_BYTES + 1u);
    if (!catalog || !selected || !active || !imported || !runtime || !text ||
        !ptc_calendar_index_load(ui->client.storage, APP_ROOT, false, catalog,
            model->calendar_pending_catalog_sha256) ||
        !ptc_calendar_index_load(ui->client.storage, APP_ROOT, true, active, NULL)) goto done;
    if (model->calendar_manager_tab == 1) {
        if (!ptc_calendar_file_name_valid(row->id) ||
            snprintf(path, sizeof(path), "%s/calendar-import/%s", APP_ROOT, row->id) >= (int)sizeof(path) ||
            !ui->client.storage->vtable->read_text(ui->client.storage, path, text,
                PTC_CALENDAR_IMPORT_MAX_BYTES + 1u) ||
            !ptc_holiday_calendar_parse_import(text, strlen(text), imported)) {
            snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
                "%s", ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE));
            goto done;
        }
        ptc_calendar_sha256_hex(text, strlen(text), model->calendar_pending_sha256);
        snprintf(model->calendar_pending_file, sizeof(model->calendar_pending_file), "%s", row->id);
        snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
            "%s (%s) - %u", imported->region_name, imported->region_id, imported->year);
        snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
            ptc_ui_text(PTC_UI_T_CALENDAR_DATES_SUMMARY),
            imported->holiday_day_count, imported->workday_count);
        for (i = 0; i < imported->group_count && i < 6u; ++i) {
            const PtcCalendarHolidayGroup *group = &imported->groups[i];
            uint16_t ey;
            uint8_t sm, sd, em, ed;
            if (!ptc_date_from_day_index(group->first_day_index, &ey, &sm, &sd) ||
                !ptc_date_from_day_index(group->last_day_index, &ey, &em, &ed)) continue;
            snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
                "%s  %02u-%02u - %02u-%02u", group->name, sm, sd, em, ed);
        }
        if (imported->group_count > 6u)
            snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
                "+%u", (unsigned int)(imported->group_count - 6u));
        {
            uint16_t first = 0;
            unsigned int shown = 0;
            if (ptc_day_index_from_date(imported->year, 1, 1, &first)) {
                for (uint16_t offset = 0; offset < 366 && shown < 2u &&
                    model->calendar_preview_count < 12; ++offset) {
                    uint16_t y;
                    uint8_t m, d;
                    if (imported->days[offset] != PTC_CALENDAR_DAY_MAKEUP_WORKDAY ||
                        !ptc_date_from_day_index((uint16_t)(first + offset), &y, &m, &d)) continue;
                    snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
                        "%s  %04u-%02u-%02u", ptc_ui_text(PTC_UI_T_MAKEUP_WORKDAY), y, m, d);
                    ++shown;
                }
            }
        }
        goto done;
    }
    if (!ptc_calendar_build_selection(catalog, row->id, selected) ||
        !ptc_calendar_selection_validate(ui->client.storage, APP_ROOT, selected)) {
        snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
            "%s", ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE));
        goto done;
    }
    snprintf(model->calendar_pending_option_id, sizeof(model->calendar_pending_option_id), "%s", row->id);
    if (strcmp(row->id, "builtin-cn") == 0) snprintf(years, sizeof(years), "2026");
    else for (i = 0; i < selected->count; ++i) {
        size_t used = strlen(years);
        snprintf(years + used, sizeof(years) - used, "%s%u",
            used ? ", " : "", selected->entries[i].year);
    }
    snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
        ptc_ui_text(PTC_UI_T_CALENDAR_YEAR_SUMMARY), row->title, years);
    if (!ptc_date_from_day_index(model->day_index, &year, &month, &day) ||
        !ptc_calendar_runtime_from_selection(ui->client.storage, APP_ROOT, selected,
            model->day_index, runtime)) goto done;
    {
        PtcRules rules;
        PtcDayRule today;
        PtcBedtimeEvaluation bedtime;
        char quota[48];
        time_t raw_now = time(NULL);
        struct tm *local_now = localtime(&raw_now);
        uint16_t minute_of_day = local_now ? (uint16_t)(local_now->tm_hour * 60 + local_now->tm_min) : 0;
        ptc_rules_default(&rules);
        memcpy(rules.week, model->current_week, sizeof(rules.week));
        rules.today_override.present = model->today_override_present;
        rules.today_override.day_index = model->day_index;
        rules.today_override.rule = model->today_override_rule;
        rules.scheduled_override = model->scheduled_override;
        rules.holiday_enabled = model->holiday_enabled;
        rules.holiday_rule = model->holiday_rule;
        rules.makeup_workday_rule = model->makeup_workday_rule;
        rules.bedtime = model->bedtime_policy;
        rules.calendar = runtime->builtin ? NULL : &runtime->set;
        today = ptc_rules_today_rule(&rules, model->day_index,
            ptc_weekday_from_day_index(model->day_index));
        bedtime = ptc_bedtime_evaluate(&rules, model->day_index,
            ptc_weekday_from_day_index(model->day_index), minute_of_day);
        if (today.mode == PTC_RULE_MODE_UNLIMITED)
            snprintf(quota, sizeof(quota), "%s", ptc_ui_text(PTC_UI_T_BASIS_UNLIMITED));
        else snprintf(quota, sizeof(quota), ptc_ui_text(PTC_UI_T_U_MIN), today.minutes);
        snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
            ptc_ui_text(PTC_UI_T_CALENDAR_TODAY_QUOTA), quota);
        snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
            ptc_ui_text(PTC_UI_T_CALENDAR_TODAY_BEDTIME),
            bedtime.active ? ptc_ui_text(PTC_UI_T_CALENDAR_BEDTIME_ACTIVE) :
            ptc_ui_text(PTC_UI_T_CALENDAR_BEDTIME_INACTIVE));
    }
    for (i = 0; i < 7u && model->calendar_preview_count < 11; ++i) {
        uint16_t date = (uint16_t)(model->day_index + i), y;
        uint8_t m, d;
        bool covered = false;
        PtcCalendarDayType kind = ptc_holiday_calendar_classify_in(
            runtime->builtin ? NULL : &runtime->set, date, &covered);
        const char *label = !covered ? ptc_ui_text(PTC_UI_T_CALENDAR_UNCOVERED) :
            kind == PTC_CALENDAR_DAY_STATUTORY_HOLIDAY ? ptc_ui_text(PTC_UI_T_STATUTORY_HOLIDAY) :
            kind == PTC_CALENDAR_DAY_MAKEUP_WORKDAY ? ptc_ui_text(PTC_UI_T_MAKEUP_WORKDAY) :
            ptc_ui_text(PTC_UI_T_WEEKLY_PLAN);
        if (!ptc_date_from_day_index(date, &y, &m, &d)) break;
        snprintf(model->calendar_preview_lines[model->calendar_preview_count++], 144,
            "%04u-%02u-%02u  %s", y, m, d, label);
    }
done:
    free(text);
    free(runtime);
    free(imported);
    free(active);
    free(selected);
    free(catalog);
}

void calendar_manager_refresh(UiState *ui)
{
    PtcUiModel *model = &ui->model;
    PtcCalendarIndex *catalog = malloc(sizeof(*catalog));
    PtcCalendarIndex *active = malloc(sizeof(*active));
    size_t i, count = 0;
    char names[95][128];
    char directory[320];
    bool loaded = false;
    model->calendar_manager_count = 0;
    if (!catalog || !active ||
        !ptc_calendar_index_load(ui->client.storage, APP_ROOT, false, catalog, NULL) ||
        !ptc_calendar_index_load(ui->client.storage, APP_ROOT, true, active, NULL)) goto done;
    loaded = true;
    if (model->calendar_manager_tab == 0) {
        PtcUiCalendarRow *row = &model->calendar_manager_rows[model->calendar_manager_count++];
        snprintf(row->id, sizeof(row->id), "builtin-cn");
        snprintf(row->title, sizeof(row->title), "%s", ptc_ui_text(PTC_UI_T_CALENDAR_BUILTIN_CHINA));
        snprintf(row->detail, sizeof(row->detail), "%s",
            strcmp(active->option_id, "builtin-cn") == 0 ?
            ptc_ui_text(PTC_UI_T_CALENDAR_ACTIVE) : "2026");
        for (i = 0; i < catalog->count && model->calendar_manager_count < 96; ++i) {
            const PtcCalendarIndexEntry *entry = &catalog->entries[i];
            PtcUiCalendarRow *candidate;
            size_t j;
            for (j = 1; j < (size_t)model->calendar_manager_count; ++j)
                if (strcmp(model->calendar_manager_rows[j].id, entry->region_id) == 0) break;
            if (j < (size_t)model->calendar_manager_count) continue;
            candidate = &model->calendar_manager_rows[model->calendar_manager_count++];
            snprintf(candidate->id, sizeof(candidate->id), "%s", entry->region_id);
            snprintf(candidate->title, sizeof(candidate->title), "%s (%s)", entry->region_name,
                entry->region_id);
            if (strcmp(active->option_id, entry->region_id) == 0) {
                size_t matched = 0, total = 0, k;
                for (k = 0; k < catalog->count; ++k) {
                    if (strcmp(catalog->entries[k].region_id, entry->region_id) != 0) continue;
                    ++total;
                    for (size_t a = 0; a < active->count; ++a)
                        if (active->entries[a].year == catalog->entries[k].year &&
                            strcmp(active->entries[a].sha256, catalog->entries[k].sha256) == 0)
                            ++matched;
                }
                snprintf(candidate->detail, sizeof(candidate->detail), "%s / %s",
                    matched == total && matched == active->count ?
                    ptc_ui_text(PTC_UI_T_CALENDAR_ACTIVE) :
                    ptc_ui_text(PTC_UI_T_CALENDAR_PENDING),
                    ptc_ui_text(PTC_UI_T_CALENDAR_USER_IMPORT));
            } else snprintf(candidate->detail, sizeof(candidate->detail), "%s",
                ptc_ui_text(PTC_UI_T_CALENDAR_USER_IMPORT));
        }
    } else {
        snprintf(directory, sizeof(directory), "%s/calendar-import", APP_ROOT);
        if (ui->client.storage->vtable->exists(ui->client.storage, directory) &&
            ui->client.storage->vtable->list_json(ui->client.storage, directory, names, 95, &count)) {
            for (i = 0; i < count && model->calendar_manager_count < 96; ++i) {
                PtcUiCalendarRow *row;
                if (!ptc_calendar_file_name_valid(names[i])) continue;
                row = &model->calendar_manager_rows[model->calendar_manager_count++];
                snprintf(row->id, sizeof(row->id), "%.80s", names[i]);
                snprintf(row->title, sizeof(row->title), "%.95s", names[i]);
                snprintf(row->detail, sizeof(row->detail), "%s",
                    ptc_ui_text(PTC_UI_T_CALENDAR_USER_IMPORT));
            }
        }
    }
done:
    free(active);
    free(catalog);
    if (model->calendar_manager_selected >= model->calendar_manager_count)
        model->calendar_manager_selected = model->calendar_manager_count - 1;
    if (model->calendar_manager_selected < 0 && model->calendar_manager_count > 0)
        model->calendar_manager_selected = 0;
    model->calendar_manager_page = model->calendar_manager_selected < 0 ? 0 :
        model->calendar_manager_selected / 6;
    calendar_preview(ui);
    if (!loaded) {
        model->calendar_preview_count = 1;
        snprintf(model->calendar_preview_lines[0], sizeof(model->calendar_preview_lines[0]),
            "%s", ptc_ui_text(PTC_UI_T_CALENDAR_INVALID_FILE));
    }
}

void open_calendar_manager(UiState *ui)
{
    if (!ui || ui->waiting) return;
    ui->model.calendar_manager_tab = 0;
    ui->model.calendar_manager_selected = 0;
    ui->model.overlay = PTC_UI_OVERLAY_CALENDAR_MANAGER;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s",
        ptc_ui_text(PTC_UI_T_REGION_CALENDAR_MANAGER));
    ui->model.overlay_body[0] = '\0';
    calendar_manager_refresh(ui);
    if (!ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL))) submit_status(ui);
}

void calendar_manager_select_tab(UiState *ui, int tab)
{
    if (!ui || tab < 0 || tab > 1) return;
    ui->model.calendar_manager_tab = tab;
    ui->model.calendar_manager_selected = 0;
    calendar_manager_refresh(ui);
}

void calendar_manager_select_row(UiState *ui, int row)
{
    int index;
    if (!ui || row < 0 || row >= 6) return;
    index = ui->model.calendar_manager_page * 6 + row;
    if (index >= ui->model.calendar_manager_count) return;
    ui->model.calendar_manager_selected = index;
    calendar_preview(ui);
}

void calendar_manager_nav(UiState *ui, int action)
{
    PtcUiModel *model;
    if (!ui || ui->waiting) return;
    model = &ui->model;
    if (action == 0 || action == 1) {
        int page = model->calendar_manager_page + (action == 0 ? -1 : 1);
        if (page < 0 || page * 6 >= model->calendar_manager_count) return;
        model->calendar_manager_page = page;
        model->calendar_manager_selected = page * 6;
        calendar_preview(ui);
        return;
    }
    if (action != 2 || model->calendar_manager_selected < 0) return;
    if (model->calendar_manager_tab == 1 && model->calendar_pending_file[0]) {
        open_confirm_overlay(ui, PTC_UI_OPERATION_IMPORT_CALENDAR,
            ptc_ui_text(PTC_UI_T_CALENDAR_IMPORT_ACTION),
            ptc_ui_text(PTC_UI_T_CALENDAR_IMPORT_CONFIRM));
    } else if (model->calendar_manager_tab == 0 && model->calendar_pending_option_id[0]) {
        if (!ptc_ui_status_is_fresh(model, (int64_t)time(NULL))) {
            submit_status(ui);
            return;
        }
        if (model->disable_flag_present) {
            snprintf(model->message, sizeof(model->message), "%s",
                ptc_ui_text(PTC_UI_T_EMERGENCY_DEACTIVATION_IS_ENABLED_AND_LIMIT_MODIFICATION));
            return;
        }
        open_danger_confirm_overlay(ui, PTC_UI_OPERATION_ACTIVATE_CALENDAR,
            ptc_ui_text(PTC_UI_T_CALENDAR_APPLY_ACTION),
            ptc_ui_text(PTC_UI_T_CALENDAR_APPLY_WARNING));
    }
}

void submit_calendar_operation(UiState *ui, PtcUiOperation operation)
{
    char json[640];
    const char *type = operation == PTC_UI_OPERATION_IMPORT_CALENDAR ?
        "import_holiday_calendar" : "activate_holiday_calendar";
    PtcCompanionStatus status;
    if (operation == PTC_UI_OPERATION_ACTIVATE_CALENDAR && ui->model.disable_flag_present) return;
    if (operation == PTC_UI_OPERATION_ACTIVATE_CALENDAR &&
        !ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL))) {
        ui->model.overlay = PTC_UI_OVERLAY_CALENDAR_MANAGER;
        submit_status(ui);
        return;
    }
    /* Re-read the selected source to reject changed files and stale catalog previews. */
    {
        char old_file[81], old_hash[65], old_option[17], old_catalog[65];
        snprintf(old_file, sizeof(old_file), "%s", ui->model.calendar_pending_file);
        snprintf(old_hash, sizeof(old_hash), "%s", ui->model.calendar_pending_sha256);
        snprintf(old_option, sizeof(old_option), "%s", ui->model.calendar_pending_option_id);
        snprintf(old_catalog, sizeof(old_catalog), "%s", ui->model.calendar_pending_catalog_sha256);
        calendar_preview(ui);
        if ((operation == PTC_UI_OPERATION_IMPORT_CALENDAR &&
                (strcmp(old_file, ui->model.calendar_pending_file) != 0 ||
                 strcmp(old_hash, ui->model.calendar_pending_sha256) != 0)) ||
            (operation == PTC_UI_OPERATION_ACTIVATE_CALENDAR &&
                (strcmp(old_option, ui->model.calendar_pending_option_id) != 0 ||
                 strcmp(old_catalog, ui->model.calendar_pending_catalog_sha256) != 0))) {
            snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                ptc_ui_text(PTC_UI_T_CALENDAR_PENDING));
            ui->model.overlay = PTC_UI_OVERLAY_CALENDAR_MANAGER;
            return;
        }
    }
    make_next_request_id(ui->active_request_id, sizeof(ui->active_request_id));
    if (operation == PTC_UI_OPERATION_IMPORT_CALENDAR)
        snprintf(json, sizeof(json),
            "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":{\"file_name\":\"%s\",\"sha256\":\"%s\"}}\n",
            ui->active_request_id, type, (long long)time(NULL),
            ui->model.calendar_pending_file, ui->model.calendar_pending_sha256);
    else
        snprintf(json, sizeof(json),
            "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":{\"option_id\":\"%s\",\"catalog_sha256\":\"%s\"}}\n",
            ui->active_request_id, type, (long long)time(NULL),
            ui->model.calendar_pending_option_id, ui->model.calendar_pending_catalog_sha256);
    status = ptc_companion_transport_submit_json(&ui->transport, ui->active_request_id, json);
    if (status == PTC_COMPANION_OK) {
        ui->model.overlay = PTC_UI_OVERLAY_CALENDAR_MANAGER;
        begin_wait(ui, type, ptc_ui_text(PTC_UI_T_THE_SETTINGS_HAVE_BEEN_SUBMITTED_AND_ARE));
    } else set_message(ui, ptc_ui_text(PTC_UI_T_SETTING_SUBMISSION_FAILED), status);
}
