#include "nro_app_internal.h"

static bool read_pairing_values(UiState *ui, char *device_id, size_t device_size, char *secret, size_t secret_size)
{
    char config_text[4096];
    char credentials_text[512];
    cJSON *config;
    cJSON *credentials;
    const char *device;
    const char *grant_secret;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, config_text, sizeof(config_text)) ||
        !ui->client.storage->vtable->read_text(ui->client.storage, CREDENTIALS_PATH, credentials_text, sizeof(credentials_text))) {
        return false;
    }
    config = cJSON_Parse(config_text);
    credentials = cJSON_Parse(credentials_text);
    device = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(config, "device_id"));
    grant_secret = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(credentials, "grant_secret"));
    if (!device || !grant_secret) {
        cJSON_Delete(config);
        cJSON_Delete(credentials);
        return false;
    }
    if (device_id && device_size) snprintf(device_id, device_size, "%s", device);
    if (secret && secret_size) snprintf(secret, secret_size, "%s", grant_secret);
    cJSON_Delete(config);
    cJSON_Delete(credentials);
    return true;
}
static bool read_pairing_config(UiState *ui, char *base_url, size_t base_size, uint16_t *max_add_minutes)
{
    char text[4096];
    cJSON *root;
    const cJSON *base;
    const cJSON *maximum;
    if (!ui || !base_url || base_size == 0U || !max_add_minutes ||
        !ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    base = cJSON_GetObjectItemCaseSensitive(root, "pairing_base_url");
    maximum = cJSON_GetObjectItemCaseSensitive(root, "max_add_minutes");
    snprintf(base_url, base_size, "%s",
             cJSON_IsString(base) && base->valuestring && ptc_pairing_base_url_valid(base->valuestring)
                 ? base->valuestring : PTC_PAIRING_BASE_URL);
    *max_add_minutes = cJSON_IsNumber(maximum) && maximum->valueint >= 1
        ? (uint16_t)maximum->valueint : PTC_TOKEN_V2_MAX_MINUTES;
    if (*max_add_minutes > PTC_TOKEN_V2_MAX_MINUTES) *max_add_minutes = PTC_TOKEN_V2_MAX_MINUTES;
    cJSON_Delete(root);
    return true;
}
static bool save_pairing_base_url(UiState *ui, const char *base_url)
{
    char text[4096];
    cJSON *root;
    char *rendered;
    bool ok;
    if (!ptc_pairing_base_url_valid(base_url) ||
        !ui->client.storage->vtable->read_text(ui->client.storage, CONFIG_PATH, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_DeleteItemFromObject(root, "pairing_base_url");
    cJSON_AddStringToObject(root, "pairing_base_url", base_url);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, CONFIG_PATH, rendered);
    free(rendered);
    return ok;
}
static uint16_t legal_grant_minutes(uint16_t requested, uint16_t maximum)
{
    uint8_t tier;
    if (maximum > PTC_TOKEN_V2_MAX_MINUTES) maximum = PTC_TOKEN_V2_MAX_MINUTES;
    if (requested > maximum) requested = maximum;
    while (requested > 1U && ptc_token_v2_tier_for_minutes(requested, &tier) != PTC_ERR_OK) --requested;
    return requested > 0U ? requested : 1U;
}
static void load_consumed_nonces(uint16_t day_index, bool used[PTC_TOKEN_V2_MAX_NONCE + 1U])
{
    FILE *file = fopen(LEDGER_PATH, "r");
    char line[256];
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        unsigned int day;
        unsigned int nonce;
        if (sscanf(line, "{\"day_index\":%u,\"nonce\":%u,\"token_version\":2", &day, &nonce) == 2 &&
            day == day_index && nonce <= PTC_TOKEN_V2_MAX_NONCE) used[nonce] = true;
    }
    fclose(file);
}
static bool load_issued_nonces(UiState *ui, uint16_t day_index, bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U])
{
    char text[8192];
    cJSON *root;
    const cJSON *stored_day;
    const cJSON *nonces;
    const cJSON *item;
    if (!ui->client.storage->vtable->exists(ui->client.storage, ISSUED_NONCES_PATH)) return true;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, ISSUED_NONCES_PATH, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    stored_day = cJSON_GetObjectItemCaseSensitive(root, "day_index");
    nonces = cJSON_GetObjectItemCaseSensitive(root, "nonces");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(stored_day) || !cJSON_IsArray(nonces)) {
        cJSON_Delete(root);
        return false;
    }
    if (stored_day->valueint == day_index) {
        cJSON_ArrayForEach(item, nonces) {
            if (cJSON_IsNumber(item) && item->valueint >= 0 &&
                (unsigned int)item->valueint <= PTC_TOKEN_V2_MAX_NONCE) {
                issued[item->valueint] = true;
            }
        }
    }
    cJSON_Delete(root);
    return true;
}
static bool save_issued_nonces(UiState *ui, uint16_t day_index, const bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U])
{
    cJSON *root = cJSON_CreateObject();
    cJSON *nonces = cJSON_AddArrayToObject(root, "nonces");
    char *rendered;
    bool ok;
    unsigned int nonce;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "day_index", day_index);
    for (nonce = 0; nonce <= PTC_TOKEN_V2_MAX_NONCE; ++nonce) {
        if (issued[nonce]) cJSON_AddItemToArray(nonces, cJSON_CreateNumber(nonce));
    }
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, ISSUED_NONCES_PATH, rendered);
    free(rendered);
    return ok;
}
void refresh_security_state(UiState *ui)
{
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    ui->model.demo_secret_enabled = read_pairing_values(ui, NULL, 0, secret, sizeof(secret)) &&
        ptc_grant_secret_is_demo(secret);
}
bool commit_credential(UiState *ui)
{
    const char *path = ui->model.credential_kind == 1 ? CONFIG_PATH : CREDENTIALS_PATH;
    const char *field = ui->model.credential_kind == 1 ? "device_id" : "grant_secret";
    char text[4096];
    cJSON *root;
    char *rendered;
    bool ok;
    if (!ui->client.storage->vtable->read_text(ui->client.storage, path, text, sizeof(text))) return false;
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_DeleteItemFromObject(root, field);
    cJSON_AddStringToObject(root, field, ui->model.credential_new);
    rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = rendered && ui->client.storage->vtable->write_text_atomic(ui->client.storage, path, rendered);
    free(rendered);
    if (!ok) return false;
    snprintf(ui->model.credential_current, sizeof(ui->model.credential_current), "%s", ui->model.credential_new);
    show_grant_manager(ui, ui->model.credential_kind == 1
        ? PTC_UI_GRANT_MANAGER_DEVICE : PTC_UI_GRANT_MANAGER_SECRET);
    refresh_security_state(ui);
    if (ui->model.credential_kind == 1) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 ptc_ui_text(PTC_UI_T_THE_DEVICE_NAME_HAS_BEEN_UPDATED_THE));
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 ptc_ui_text(PTC_UI_T_THE_GRANT_CODE_KEY_HAS_BEEN_UPDATED));
    }
    return true;
}
void open_credential_manager(UiState *ui, int kind)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret))) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_READ_CURRENT_DEVICE_PAIRING_INFORMATION));
        return;
    }
    ui->model.credential_kind = kind;
    ui->model.credential_revealed = false;
    ui->model.credential_new_revealed = kind == 1;
    snprintf(ui->model.credential_current, sizeof(ui->model.credential_current), "%s", kind == 1 ? device : secret);
    snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", kind == 1 ? device : secret);
    ui->model.overlay = PTC_UI_OVERLAY_CREDENTIAL;
    ui->model.overlay_selection = PTC_UI_CREDENTIAL_INPUT;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s",
             kind == 1 ? ptc_ui_text(PTC_UI_T_MANAGES_THE_TIME_CODE_DEVICE_NAME) : ptc_ui_text(PTC_UI_T_MANAGE_GRANT_CODE_KEYS));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
             kind == 1 ? ptc_ui_text(PTC_UI_T_THE_CURRENT_VALUE_IS_READ_ONLY_A) :
                         ptc_ui_text(PTC_UI_T_THE_CURRENT_KEY_IS_BLOCKED_BY_DEFAULT));
}
void edit_credential_input(UiState *ui)
{
    char value[80];
    const char *header = ui->model.credential_kind == 1 ? ptc_ui_text(PTC_UI_T_ENTER_NEW_DEVICE_NAME) : ptc_ui_text(PTC_UI_T_ENTER_NEW_PLUS_TIME_CODE_KEY);
    const char *guide = ui->model.credential_kind == 1
        ? ptc_ui_text(PTC_UI_T_1_TO_32_DIGITS_LETTERS_NUMBERS)
        : ptc_ui_text(PTC_UI_T_RECOMMENDED_RANDOM_GENERATION_MANUALLY_ENTER_32_TO);
    if (!keyboard_input(header, guide, value, sizeof(value), ui->model.credential_kind == 2, false, false)) return;
    snprintf(ui->model.credential_new, sizeof(ui->model.credential_new), "%s", value);
}
void randomize_credential(UiState *ui)
{
    uint8_t bytes[32];
    randomGet(bytes, sizeof(bytes));
    if (ui->model.credential_kind == 1) {
        (void)ptc_random_device_id(bytes, ui->model.credential_new, sizeof(ui->model.credential_new));
    } else {
        (void)ptc_hex_from_random(bytes, sizeof(bytes), ui->model.credential_new, sizeof(ui->model.credential_new));
        ui->model.credential_new_revealed = true;
    }
}
void request_save_credential(UiState *ui)
{
    bool valid = ui->model.credential_kind == 1
        ? ptc_device_id_valid(ui->model.credential_new)
        : ptc_grant_secret_valid(ui->model.credential_new);
    if (!valid) {
        snprintf(ui->model.message, sizeof(ui->model.message), "%s",
                 ui->model.credential_kind == 1
                    ? ptc_ui_text(PTC_UI_T_THE_DEVICE_NAME_MUST_BE_1_TO)
                    : ptc_ui_text(PTC_UI_T_THE_KEY_MUST_BE_32_TO_64));
        return;
    }
    if (strcmp(ui->model.credential_current, ui->model.credential_new) == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_NEW_VALUE_IS_THE_SAME_AS));
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_SAVE_CREDENTIAL;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THIS_APPLICATION_PIN_AGAIN_BEFORE_3))) return;
    if (ui->model.credential_kind == 2 && ptc_grant_secret_is_demo(ui->model.credential_new)) {
        open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_CREDENTIAL, ptc_ui_text(PTC_UI_T_ENABLE_PUBLIC_DEMO_KEY),
            ptc_ui_text(PTC_UI_T_ANYONE_WHO_KNOWS_THE_DEVICE_NAME_CAN));
        return;
    }
    if (!commit_credential(ui)) snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_SAVE_PAIRING_INFORMATION));
}
void show_grant_manager(UiState *ui, int selection)
{
    ui->model.overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
    ui->model.overlay_selection = selection >= 0 && selection < PTC_UI_GRANT_MANAGER_COUNT ? selection : 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_EXTRA_TIME_CODE_GENERATION_MANAGEMENT));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             ptc_ui_text(PTC_UI_T_MANAGES_THE_DEVICE_INFORMATION_EXPORT_CONFIGURATION_AND));
}
void open_grant_manager(UiState *ui)
{
    ui->model.message[0] = '\0';
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_DEVICE);
}
void open_local_grant(UiState *ui)
{
    uint16_t maximum = PTC_TOKEN_V2_MAX_MINUTES;
    if (!read_pairing_config(ui, ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), &maximum)) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_READ_NATIVE_GENERATOR_CONFIGURATION));
        return;
    }
    ui->model.grant_max_minutes = maximum;
    ui->model.grant_minutes = legal_grant_minutes(20U, maximum);
    ui->model.grant_notice[0] = '\0';
    ui->model.grant_has_code = false;
    ui->model.grant_issued_minutes = 0;
    ui->model.grant_estimate_available = false;
    ui->model.grant_estimate_minutes = 0;
    ui->model.grant_estimate_capped = false;
    ui->model.grant_estimate_unrestricted = false;
    ui->model.grant_estimated_at = 0;
    ui->model.grant_status_refresh_failed = false;
    ui->model.grant_code[0] = '\0';
    ui->model.overlay = PTC_UI_OVERLAY_GRANT_LOCAL;
    ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
    ui->model.grant_status_refresh_failed = false;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_NATIVELY_GENERATES_8_BIT_PLUS_TIME_CODE));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             ptc_ui_text(PTC_UI_T_CHOOSE_THE_DURATION_GENERATE_IT_AND_TELL_2));
    submit_status(ui);
    if (!ui->waiting) ui->model.grant_status_refresh_failed = true;
}
void edit_pairing_base_url(UiState *ui)
{
    char value[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
    ui->auth_retry_action = AUTH_RETRY_EDIT_URL;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_BEFORE_MODIFYING_THE_QR_CODE_JUMP_ADDRESS))) return;
    if (!keyboard_input(ptc_ui_text(PTC_UI_T_QR_CODE_JUMP_ADDRESS), ptc_ui_text(PTC_UI_T_FILL_IN_THE_OFFICIAL_OR_TRUSTED_PARENT),
                        value, sizeof(value), false, false, false)) return;
    if (!ptc_pairing_base_url_valid(value)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 ptc_ui_text(PTC_UI_T_INVALID_ADDRESS_256_CHARACTERS_MAXIMUM_NO_ACCOUNT));
        return;
    }
    if (!save_pairing_base_url(ui, value)) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_SAVE_THE_QR_CODE_JUMP));
        return;
    }
    snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", value);
    snprintf(ui->model.message, sizeof(ui->model.message),
             ptc_ui_text(PTC_UI_T_THE_QR_CODE_JUMP_ADDRESS_HAS_BEEN));
}
void apply_default_pairing_base_url(UiState *ui)
{
    if (!save_pairing_base_url(ui, PTC_PAIRING_BASE_URL)) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_RESTORE_THE_OFFICIAL_QR_CODE));
    } else {
        snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", PTC_PAIRING_BASE_URL);
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_THE_DEFAULT_ADDRESS_FOR_QR_CODE_JUMP));
    }
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_RESET_URL);
}
void request_reset_pairing_base_url(UiState *ui)
{
    char body[320];
    ui->auth_retry_action = AUTH_RETRY_RESET_URL;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_BEFORE_RESTORING_THE_DEFAULT_ADDRESS_FOR_QR))) return;
    snprintf(body, sizeof(body), ptc_ui_text(PTC_UI_T_RESTORE_THE_QR_DESTINATION_TO_ITS_DEFAULT), PTC_PAIRING_BASE_URL);
    open_confirm_overlay(ui, PTC_UI_OPERATION_RESET_PAIRING_URL,
                         ptc_ui_text(PTC_UI_T_RESTORE_THE_DEFAULT_ADDRESS_OF_QR_CODE), body);
}
void generate_local_grant_code(UiState *ui)
{
    bool consumed[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    bool issued[PTC_TOKEN_V2_MAX_NONCE + 1U] = {false};
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    char next_code[9];
    uint16_t start;
    uint16_t nonce = 0;
    uint8_t tier;
    bool found = false;
    if (ui->waiting) return;
    ui->model.grant_notice[0] = '\0';
    if (!ui->model.status_loaded) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), ptc_ui_text(PTC_UI_T_UNABLE_TO_CONFIRM_THE_DEVICE_DATE_PLEASE));
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_GENERATE_CODE;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_APPLICATION_PIN_AGAIN_BEFORE))) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret)) ||
        ptc_token_v2_tier_for_minutes(ui->model.grant_minutes, &tier) != PTC_ERR_OK) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), ptc_ui_text(PTC_UI_T_THE_PAIRING_INFORMATION_IS_INVALID_PLEASE_RETURN));
        return;
    }
    load_consumed_nonces(ui->model.day_index, consumed);
    if (!load_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), ptc_ui_text(PTC_UI_T_FAILED_TO_READ_THE_ISSUANCE_RECORD_PLEASE));
        return;
    }
    randomGet(&start, sizeof(start));
    found = ptc_token_v2_find_available_nonce(consumed, issued, start, &nonce);
    if (!found) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice),
                 ptc_ui_text(PTC_UI_T_THE_UPPER_LIMIT_OF_512_COINS_HAS));
        return;
    }
    if (ptc_token_v2_encode(tier, nonce, device, secret, ui->model.day_index, next_code) != PTC_ERR_OK) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), ptc_ui_text(PTC_UI_T_GENERATION_FAILED_PLEASE_GO_BACK_AND_CHECK));
        return;
    }
    issued[nonce] = true;
    if (!save_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice),
                 ptc_ui_text(PTC_UI_T_FAILED_TO_SAVE_PLEASE_GO_BACK_AND));
        return;
    }
    snprintf(ui->model.grant_code, sizeof(ui->model.grant_code), "%s", next_code);
    ui->model.grant_day_index = ui->model.day_index;
    ui->model.grant_has_code = true;
    ui->model.grant_issued_minutes = ui->model.grant_minutes;
    ui->model.grant_estimate_minutes = ptc_ui_grant_estimate_remaining(
        &ui->model, ui->model.grant_minutes, &ui->model.grant_estimate_capped);
    ui->model.grant_estimate_available = ui->model.grant_estimate_minutes >= 0 &&
        ptc_ui_status_is_fresh(&ui->model, (int64_t)time(NULL));
    ui->model.grant_estimate_unrestricted = ui->model.unrestricted_today == 1;
    ui->model.grant_estimated_at = (int64_t)time(NULL);
    ui->model.overlay_selection = PTC_UI_GRANT_LOCAL_GENERATE;
    snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_GENERATED_A_U_MIN_CODE_VALID_TODAY),
             (unsigned int)ui->model.grant_minutes);
}
void show_pairing_qr(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint16_t maximum;
    ui->auth_retry_action = AUTH_RETRY_SHOW_QR;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THE_APPLICATION_PIN_AGAIN_BEFORE_2))) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret)) ||
        !read_pairing_config(ui, ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), &maximum) ||
        !ptc_build_pairing_url_with_base(ui->model.pairing_base_url, device, secret,
                                        ui->model.pairing_url, sizeof(ui->model.pairing_url)) ||
        !qrcodegen_encodeText(ui->model.pairing_url, temp, ui->model.qr_code,
            qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
            qrcodegen_Mask_AUTO, true)) {
        snprintf(ui->model.message, sizeof(ui->model.message), ptc_ui_text(PTC_UI_T_FAILED_TO_GENERATE_MATCHING_QR_CODE));
        return;
    }
    ui->model.overlay = PTC_UI_OVERLAY_QR;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_MOBILE_PHONE_COMPUTER_GENERATES_GRANT_CODE));
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             ptc_ui_text(PTC_UI_T_YOU_CAN_SCAN_THE_QR_CODE_FOR));
}
void export_parent_import(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    cJSON *root;
    char *json;
    bool ok;
    if (ui->model.overlay == PTC_UI_OVERLAY_QR ||
        ui->model.overlay == PTC_UI_OVERLAY_GRANT_MANAGER)
        ui->export_return_overlay = ui->model.overlay;
    if (ui->export_return_overlay == PTC_UI_OVERLAY_NONE)
        ui->export_return_overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
    if (ui->export_return_overlay != PTC_UI_OVERLAY_QR) {
        ui->auth_retry_action = AUTH_RETRY_EXPORT_CONFIG;
        if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THIS_APPLICATION_PIN_AGAIN_BEFORE_4))) return;
    }
    ui->model.parent_export_succeeded = false;
    ui->model.overlay = PTC_UI_OVERLAY_PARENT_EXPORT_RESULT;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), ptc_ui_text(PTC_UI_T_EXPORT_MOBILE_PHONE_COMPUTER_CONFIGURATION));
    ui->model.overlay_body[0] = '\0';
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret))) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            ptc_ui_text(PTC_UI_T_FAILED_TO_READ_DEVICE_NAME_OR_TIME));
        return;
    }
    root = cJSON_CreateObject();
    if (!root) {
        snprintf(ui->model.message, sizeof(ui->model.message),
            ptc_ui_text(PTC_UI_T_INSUFFICIENT_MEMORY_AND_THE_CONFIGURATION_FILE_WAS));
        return;
    }
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "device_id", device);
    cJSON_AddStringToObject(root, "grant_secret", secret);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = json && ui->client.storage->vtable->write_text_atomic(
        ui->client.storage, APP_ROOT "/parent-import.json", json);
    free(json);
    ui->model.parent_export_succeeded = ok;
    snprintf(ui->model.message, sizeof(ui->model.message), "%s",
        ok ? ptc_ui_text(PTC_UI_T_CONFIGURATION_FILE_HAS_BEEN_EXPORTED) : ptc_ui_text(PTC_UI_T_FAILED_TO_WRITE_TO_THE_SD_CARD));
}
void reveal_current_credential(UiState *ui)
{
    if (!ui) return;
    if (ui->model.credential_kind == 1 || ui->model.credential_revealed) {
        ui->model.credential_revealed = !ui->model.credential_revealed;
        ui->model.credential_new_revealed = ui->model.credential_revealed;
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_REVEAL_CREDENTIAL;
    if (!verify_sensitive_pin(ui, ptc_ui_text(PTC_UI_T_PLEASE_ENTER_THIS_APPLICATION_PIN_AGAIN_BEFORE_5))) return;
    ui->model.credential_revealed = true;
    ui->model.credential_new_revealed = true;
}
