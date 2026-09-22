#include "nro_app_internal.h"

u64 shortcut_preset_mask(int index)
{
    static const u64 masks[] = {
        HidNpadButton_L | HidNpadButton_R,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Up,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Down,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Left,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Right,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Plus,
        HidNpadButton_L | HidNpadButton_R | HidNpadButton_Minus,
        HidNpadButton_ZL | HidNpadButton_ZR,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Up,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Down,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Left,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Right,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus,
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Minus
    };
    if (index < 0 || index >= PTC_UI_SHORTCUT_PRESET_COUNT) {
        return masks[0];
    }
    return masks[index];
}

bool shortcut_mask_valid(u64 mask)
{
    return mask != 0 && (mask & ~((u64)CUSTOM_SHORTCUT_VALID_MASK)) == 0;
}

static bool parse_shortcut_mask(const char *text, u64 *out)
{
    char *end = NULL;
    unsigned long long value;
    if (!text || !text[0] || !out) {
        return false;
    }
    value = strtoull(text, &end, 16);
    if (end == text || *end != '\0' || value == 0) {
        return false;
    }
    if (!shortcut_mask_valid((u64)value)) {
        return false;
    }
    *out = (u64)value;
    return true;
}

static void append_shortcut_token(char *out, size_t out_size, bool *first, const char *token)
{
    size_t length;
    if (!out || !first || !token || !token[0]) {
        return;
    }
    length = strlen(out);
    snprintf(out + length, out_size > length ? out_size - length : 0,
             "%s%s", *first ? "" : " + ", token);
    *first = false;
}

static void format_shortcut_label(u64 mask, char *out, size_t out_size)
{
    static const struct {
        u64 mask;
        const char *label;
    } buttons[] = {
        {HidNpadButton_B, "B"},
        {HidNpadButton_L, "L"},
        {HidNpadButton_ZL, "ZL"},
        {HidNpadButton_R, "R"},
        {HidNpadButton_ZR, "ZR"},
        {HidNpadButton_Up, "上"},
        {HidNpadButton_Down, "下"},
        {HidNpadButton_Left, "左"},
        {HidNpadButton_Right, "右"},
        {HidNpadButton_X, "X"},
        {HidNpadButton_Y, "Y"},
        {HidNpadButton_Plus, "Plus(+)"},
        {HidNpadButton_Minus, "Minus(-)"}
    };
    bool first = true;
    size_t index;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    for (index = 0; index < sizeof(buttons) / sizeof(buttons[0]); ++index) {
        if ((mask & buttons[index].mask) != 0) {
            append_shortcut_token(out, out_size, &first, buttons[index].label);
        }
    }
    if (first) {
        snprintf(out, out_size, "未设置");
    }
}

void refresh_custom_shortcut_label(UiState *ui)
{
    if (!ui) {
        return;
    }
    format_shortcut_label(ui->model.custom_shortcut_mask,
                          ui->model.custom_shortcut_label,
                          sizeof(ui->model.custom_shortcut_label));
}

void refresh_shortcut_draft_label(UiState *ui)
{
    if (!ui) return;
    format_shortcut_label(ui->model.shortcut_draft_mask,
                          ui->model.shortcut_draft_label,
                          sizeof(ui->model.shortcut_draft_label));
}

void load_ui_preferences(UiState *ui)
{
    char text[RESULT_TEXT_SIZE];
    u64 mask;
    cJSON *root;
    const cJSON *item;
    if (!ui) {
        return;
    }
    ui->model.custom_shortcut_mask = shortcut_preset_mask(PTC_UI_SHORTCUT_PRESET_LR);
    ui->model.custom_shortcut_enabled = false;
    ui->model.show_parent_shortcut_hint = true;
    ui->model.setup_step = 0;
    ui->model.setup_shortcut_index = PTC_UI_SHORTCUT_PRESET_LR;
    ui->model.setup_theme_index = PTC_UI_THEME_SYSTEM;
    ui->model.setup_zone_index = 1;
    ui->theme_preference = PTC_UI_THEME_SYSTEM;
    ui->system_theme = PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    ui->theme_view = ptc_ui_theme_make_view(ui->theme_preference, ui->system_theme);
    ui->model.shortcut_draft_mask = ui->model.custom_shortcut_mask;
    ui->model.shortcut_draft_enabled = false;
    ui->model.shortcut_draft_show_hint = true;
    refresh_shortcut_draft_label(ui);
    if (!ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) {
        refresh_custom_shortcut_label(ui);
        return;
    }
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        refresh_custom_shortcut_label(ui);
        return;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "parent_shortcut_mask");
    if (cJSON_IsString(item) && parse_shortcut_mask(item->valuestring, &mask)) {
        ui->model.custom_shortcut_mask = mask;
        /* Existing installations predate the explicit enabled flag. */
        ui->model.custom_shortcut_enabled = true;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "custom_shortcut_enabled");
    if (cJSON_IsBool(item)) {
        ui->model.custom_shortcut_enabled = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "show_parent_shortcut_hint");
    if (cJSON_IsBool(item)) {
        ui->model.show_parent_shortcut_hint = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "theme");
    if (cJSON_IsString(item)) {
        PtcUiThemePreference preference;
        if (ptc_ui_theme_parse_preference(item->valuestring, &preference)) {
            ui->theme_preference = preference;
        }
    }
    ui->theme_view = ptc_ui_theme_make_view(ui->theme_preference, ui->system_theme);
    item = cJSON_GetObjectItemCaseSensitive(root, "setup_wizard_step");
    if (cJSON_IsNumber(item)) {
        cJSON *wizard_version = cJSON_GetObjectItemCaseSensitive(root, "setup_wizard_version");
        ui->model.setup_step = ptc_ui_migrate_setup_step(
            item->valueint, cJSON_IsNumber(wizard_version) ? wizard_version->valueint : 1);
    }
    for (int index = 0; index < PTC_UI_SHORTCUT_PRESET_COUNT; ++index) {
        if (ui->model.custom_shortcut_mask == shortcut_preset_mask(index)) {
            ui->model.setup_shortcut_index = index;
            break;
        }
    }
    cJSON_Delete(root);
    refresh_custom_shortcut_label(ui);
    ui->model.shortcut_draft_mask = ui->model.custom_shortcut_mask;
    ui->model.shortcut_draft_enabled = ui->model.custom_shortcut_enabled;
    ui->model.shortcut_draft_show_hint = ui->model.show_parent_shortcut_hint;
    refresh_shortcut_draft_label(ui);
}

bool save_ui_preferences(UiState *ui)
{
    char text[RESULT_TEXT_SIZE];
    char mask_text[32];
    cJSON *root;
    char *rendered;
    bool ok;
    if (!ui || !shortcut_mask_valid(ui->model.custom_shortcut_mask)) {
        return false;
    }
    if (ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) {
        root = cJSON_Parse(text);
        if (!cJSON_IsObject(root)) {
            cJSON_Delete(root);
            return false;
        }
    } else {
        root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "version", 1);
    }
    snprintf(mask_text, sizeof(mask_text), "%llx", (unsigned long long)ui->model.custom_shortcut_mask);
    cJSON_DeleteItemFromObject(root, "parent_shortcut_mask");
    cJSON_AddStringToObject(root, "parent_shortcut_mask", mask_text);
    cJSON_DeleteItemFromObject(root, "custom_shortcut_enabled");
    cJSON_AddBoolToObject(root, "custom_shortcut_enabled", ui->model.custom_shortcut_enabled);
    cJSON_DeleteItemFromObject(root, "show_parent_shortcut_hint");
    cJSON_AddBoolToObject(root, "show_parent_shortcut_hint", ui->model.show_parent_shortcut_hint);
    cJSON_DeleteItemFromObject(root, "theme");
    cJSON_AddStringToObject(root, "theme", ptc_ui_theme_preference_name(ui->theme_preference));
    cJSON_DeleteItemFromObject(root, "setup_wizard_step");
    cJSON_AddNumberToObject(root, "setup_wizard_step", ui->model.setup_step);
    cJSON_DeleteItemFromObject(root, "setup_wizard_version");
    cJSON_AddNumberToObject(root, "setup_wizard_version", 4);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, CONFIG_PATH, rendered);
    free(rendered);
    return ok;
}

bool save_setup_step(UiState *ui, int step)
{
    int previous;
    if (!ui || step < 0 || step > PTC_UI_SETUP_ZONE) {
        return false;
    }
    previous = ui->model.setup_step;
    ui->model.setup_step = step;
    if (save_ui_preferences(ui)) {
        return true;
    }
    ui->model.setup_step = previous;
    snprintf(ui->model.message, sizeof(ui->model.message), "无法保存首次设置进度，请确认 SD 卡可写。");
    return false;
}
