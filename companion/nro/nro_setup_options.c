#include "nro_app_internal.h"

#define GUIDE_PATH APP_ROOT "/setup-guide.json"

static bool object_unique(const cJSON *j)
{
    if (!cJSON_IsObject(j)) return false;
    for (const cJSON *a = j->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) return false;
    return true;
}

static bool guide_valid(const cJSON *j)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "version");
    return object_unique(j) && cJSON_IsNumber(v) && v->valuedouble == 1 &&
        object_unique(cJSON_GetObjectItemCaseSensitive(j, "items"));
}

void load_setup_options(UiState *ui)
{
    char text[4096];
    memset(ui->model.setup_option_versions, 0, sizeof(ui->model.setup_option_versions));
    ui->model.setup_option_skipped = 0;
    ui->model.setup_options_loaded = true;
    ui->model.setup_review_active = false;
    ui->model.setup_review_selection = 0;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, GUIDE_PATH, text, sizeof(text))) return;
    cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
    if (guide_valid(j)) {
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(j, "items");
        for (int i = 0; i < PTC_UI_SETUP_OPTION_COUNT; ++i) {
            const cJSON *row = cJSON_GetObjectItemCaseSensitive(items, ptc_ui_setup_option_key(i));
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(row, "revision");
            const char *choice = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(row, "choice"));
            if (object_unique(row) && cJSON_IsNumber(v) && v->valuedouble >= 1 &&
                v->valuedouble <= UINT32_MAX && v->valuedouble == (uint32_t)v->valuedouble && choice &&
                (!strcmp(choice, "configured") || !strcmp(choice, "skipped"))) {
                ui->model.setup_option_versions[i] = (uint32_t)v->valuedouble;
                if (!strcmp(choice, "skipped")) ui->model.setup_option_skipped |= 1u << i;
            }
        }
    }
    cJSON_Delete(j);
}

static bool save_choice(UiState *ui, int index, bool skip)
{
    char text[4096];
    PtcStorage *s = ui->client.storage;
    PtcStorageMetadata meta;
    cJSON *j = NULL;
    if (!s->vtable->metadata(s, GUIDE_PATH, &meta)) return false;
    if (meta.type == PTC_STORAGE_ENTRY_MISSING) {
        j = cJSON_CreateObject();
        if (!j || !cJSON_AddNumberToObject(j, "version", 1) || !cJSON_AddObjectToObject(j, "items")) {
            cJSON_Delete(j); return false;
        }
    } else if (meta.type == PTC_STORAGE_ENTRY_FILE && s->vtable->read_text(s, GUIDE_PATH, text, sizeof(text))) {
        j = cJSON_ParseWithOpts(text, NULL, true);
    }
    if (!guide_valid(j)) { cJSON_Delete(j); return false; }
    cJSON *items = cJSON_GetObjectItemCaseSensitive(j, "items");
    const char *key = ptc_ui_setup_option_key(index);
    cJSON_DeleteItemFromObjectCaseSensitive(items, key);
    cJSON *row = cJSON_AddObjectToObject(items, key);
    bool ok = row && cJSON_AddNumberToObject(row, "revision", ptc_ui_setup_option_revision(index)) &&
        cJSON_AddStringToObject(row, "choice", skip ? "skipped" : "configured");
    char *rendered = ok ? cJSON_PrintUnformatted(j) : NULL;
    ok = rendered && strlen(rendered) < sizeof(text) && s->vtable->write_text_atomic(s, GUIDE_PATH, rendered);
    free(rendered); cJSON_Delete(j);
    return ok;
}

static void resolve_choice(UiState *ui, int index, bool skip)
{
    bool saved = save_choice(ui, index, skip);
    /* An unavailable SD must not trap this session. Reopening retries the item. */
    ui->model.setup_option_versions[index] = ptc_ui_setup_option_revision(index);
    if (skip) ui->model.setup_option_skipped |= 1u << index;
    else ui->model.setup_option_skipped &= ~(1u << index);
    if (!saved) {
        ptc_ui_setup_record_issue(&ui->model, PTC_UI_SETUP_ISSUE_PROGRESS, PTC_ERR_STORAGE_WRITE_FAILED);
        snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_SETUP_REVIEW_UNSAVED));
    }
}

void complete_setup_option(UiState *ui, int index)
{
    if (index >= 0 && index < PTC_UI_SETUP_OPTION_COUNT &&
        ui->model.setup_option_versions[index] < ptc_ui_setup_option_revision(index))
        resolve_choice(ui, index, false);
}

void setup_review_action(UiState *ui, int action)
{
    int item = ptc_ui_setup_next_option(&ui->model);
    if (!ui->model.setup_review_active || item < 0 || action < 0 || action > 3) return;
    if (action == 0) {
        if (ui->waiting) return;
        /* Upgrades retain the existing parental boundary for settings edits. */
        if (item != PTC_UI_SETUP_OPTION_LANGUAGE && !ui->setup_parent_authorized) {
            if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_SETUP_REVIEW_TITLE))) return;
            ui->setup_parent_authorized = true;
            ui->model.parent_support_only = false;
        }
        switch (item) {
        case PTC_UI_SETUP_OPTION_LANGUAGE: open_language_overlay(ui); break;
        case PTC_UI_SETUP_OPTION_THEME: open_theme_overlay(ui); break;
        case PTC_UI_SETUP_OPTION_SHORTCUT: open_shortcut_manager(ui); break;
        case PTC_UI_SETUP_OPTION_NOTIFICATIONS: open_notification_preferences(ui); break;
        default:
            if (ui->model.parent_support_only || !ui->model.status_loaded) {
                snprintf(ui->model.message, sizeof(ui->model.message), "%s", ptc_ui_text(PTC_UI_T_SETUP_REVIEW_NEEDS_STATUS));
                return;
            }
            ui->model.view = PTC_UI_PARENT;
            ui->model.parent_page = PTC_UI_PARENT_TODAY;
            if (!ptc_ui_open_today_settings(&ui->model, item == PTC_UI_SETUP_OPTION_EYE_CARE ? 8 : 9))
                ui->model.view = PTC_UI_SETUP;
            break;
        }
        return;
    }
    if (action == 3) {
        while ((item = ptc_ui_setup_next_option(&ui->model)) >= 0) resolve_choice(ui, item, true);
    } else resolve_choice(ui, item, action == 2);
    ui->model.setup_review_selection = 0;
    if (ptc_ui_setup_next_option(&ui->model) < 0) {
        ui->model.setup_review_active = false;
        ui->model.view = PTC_UI_CHILD;
    }
}
