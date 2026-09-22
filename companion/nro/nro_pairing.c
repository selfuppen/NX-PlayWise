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
                 "设备名已更新；使用更新前设备名签发的加时码不可用，请重新生成配对二维码。");
    } else {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "加时码密钥已更新；当前网页配对和使用原密钥签发的加时码已失效，请重新配对。");
    }
    return true;
}
void open_credential_manager(UiState *ui, int kind)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret))) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取当前设备配对信息失败。");
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
             kind == 1 ? "管理加时码设备名" : "管理加时码密钥");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
             kind == 1 ? "当前值只读；可手工输入或随机生成新设备名。" :
                         "当前密钥默认遮挡；建议使用随机生成的 64 位十六进制密钥。");
}
void edit_credential_input(UiState *ui)
{
    char value[80];
    const char *header = ui->model.credential_kind == 1 ? "输入新设备名" : "输入新加时码密钥";
    const char *guide = ui->model.credential_kind == 1
        ? "1到32 位：字母、数字、-、_"
        : "建议随机生成；手工输入 32到64 个非空白 ASCII 字符";
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
                    ? "设备名必须为 1到32 位，只能包含字母、数字、- 和 _。"
                    : "密钥必须为 32到64 个非空白可打印 ASCII 字符；建议使用随机生成。");
        return;
    }
    if (strcmp(ui->model.credential_current, ui->model.credential_new) == 0) {
        snprintf(ui->model.message, sizeof(ui->model.message), "新值与当前值相同，无需保存。");
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_SAVE_CREDENTIAL;
    if (!verify_sensitive_pin(ui, "保存设备配对信息前，请再次输入本应用 PIN")) return;
    if (ui->model.credential_kind == 2 && ptc_grant_secret_is_demo(ui->model.credential_new)) {
        open_confirm_overlay(ui, PTC_UI_OPERATION_SAVE_CREDENTIAL, "启用公共演示密钥",
            "任何知道设备名的人都能生成多个有效加时码，并可把当天额度累计到 1440 分钟。\n启用后家长页面会持续显示红色警告。");
        return;
    }
    if (!commit_credential(ui)) snprintf(ui->model.message, sizeof(ui->model.message), "保存配对信息失败。");
}
void show_grant_manager(UiState *ui, int selection)
{
    ui->model.overlay = PTC_UI_OVERLAY_GRANT_MANAGER;
    ui->model.overlay_selection = selection >= 0 && selection < PTC_UI_GRANT_MANAGER_COUNT ? selection : 0;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "加时码生成管理");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "管理生成加时码所需的设备信息、导出配置和二维码网页地址。");
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
        snprintf(ui->model.message, sizeof(ui->model.message), "读取本机生成器配置失败。");
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
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "本机生成 8 位加时码");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "选时长、生成，再把代码告诉孩子。");
    submit_status(ui);
    if (!ui->waiting) ui->model.grant_status_refresh_failed = true;
}
void edit_pairing_base_url(UiState *ui)
{
    char value[PTC_PAIRING_BASE_URL_MAX_LEN + 1];
    ui->auth_retry_action = AUTH_RETRY_EDIT_URL;
    if (!verify_sensitive_pin(ui, "修改二维码跳转地址前，请再次输入本应用 PIN")) return;
    if (!keyboard_input("二维码跳转地址", "填写官方或可信的家长网页；自定义页面可读取加时码密钥",
                        value, sizeof(value), false, false, false)) return;
    if (!ptc_pairing_base_url_valid(value)) {
        snprintf(ui->model.message, sizeof(ui->model.message),
                 "地址无效：最长 256 字符，不得含账号、控制字符或 #；HTTP 仅限本机和私有网络。");
        return;
    }
    if (!save_pairing_base_url(ui, value)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "保存二维码跳转地址失败。");
        return;
    }
    snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", value);
    snprintf(ui->model.message, sizeof(ui->model.message),
             "二维码跳转地址已更新；页面须支持二维码配对和导入配置文件，请仅使用可信的家长网页。");
}
void apply_default_pairing_base_url(UiState *ui)
{
    if (!save_pairing_base_url(ui, PTC_PAIRING_BASE_URL)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "恢复官方二维码地址失败。");
    } else {
        snprintf(ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), "%s", PTC_PAIRING_BASE_URL);
        snprintf(ui->model.message, sizeof(ui->model.message), "已恢复二维码跳转默认地址。");
    }
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_RESET_URL);
}
void request_reset_pairing_base_url(UiState *ui)
{
    char body[320];
    ui->auth_retry_action = AUTH_RETRY_RESET_URL;
    if (!verify_sensitive_pin(ui, "恢复二维码跳转默认地址前，请再次输入本应用 PIN")) return;
    snprintf(body, sizeof(body), "将当前二维码跳转地址恢复为默认地址：\n%s", PTC_PAIRING_BASE_URL);
    open_confirm_overlay(ui, PTC_UI_OPERATION_RESET_PAIRING_URL,
                         "恢复二维码跳转默认地址", body);
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
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "无法确认设备日期，请先关闭弹层并刷新设备状态。");
        return;
    }
    ui->auth_retry_action = AUTH_RETRY_GENERATE_CODE;
    if (!verify_sensitive_pin(ui, "本机生成加时码前，请再次输入本应用 PIN")) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret)) ||
        ptc_token_v2_tier_for_minutes(ui->model.grant_minutes, &tier) != PTC_ERR_OK) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "配对信息无效，请返回加时码生成管理检查配置。");
        return;
    }
    load_consumed_nonces(ui->model.day_index, consumed);
    if (!load_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "读取签发记录失败，请返回检查 SD 卡后重试。");
        return;
    }
    randomGet(&start, sizeof(start));
    found = ptc_token_v2_find_available_nonce(consumed, issued, start, &nonce);
    if (!found) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice),
                 "今日已达 512 枚上限，请明日再试；旧码未撤销。");
        return;
    }
    if (ptc_token_v2_encode(tier, nonce, device, secret, ui->model.day_index, next_code) != PTC_ERR_OK) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice), "生成失败，请返回检查配置后重试。");
        return;
    }
    issued[nonce] = true;
    if (!save_issued_nonces(ui, ui->model.day_index, issued)) {
        snprintf(ui->model.grant_notice, sizeof(ui->model.grant_notice),
                 "保存失败，请返回检查 SD 卡空间后重试；旧码未撤销。");
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
    snprintf(ui->model.message, sizeof(ui->model.message), "已在本机生成今天有效的 %u 分钟加时码。",
             (unsigned int)ui->model.grant_minutes);
}
void show_pairing_qr(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint16_t maximum;
    ui->auth_retry_action = AUTH_RETRY_SHOW_QR;
    if (!verify_sensitive_pin(ui, "显示包含加时码密钥的二维码前，请再次输入本应用 PIN")) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret)) ||
        !read_pairing_config(ui, ui->model.pairing_base_url, sizeof(ui->model.pairing_base_url), &maximum) ||
        !ptc_build_pairing_url_with_base(ui->model.pairing_base_url, device, secret,
                                        ui->model.pairing_url, sizeof(ui->model.pairing_url)) ||
        !qrcodegen_encodeText(ui->model.pairing_url, temp, ui->model.qr_code,
            qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
            qrcodegen_Mask_AUTO, true)) {
        snprintf(ui->model.message, sizeof(ui->model.message), "生成配对二维码失败。");
        return;
    }
    ui->model.overlay = PTC_UI_OVERLAY_QR;
    snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "手机/电脑生成加时码");
    snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body),
             "可扫描二维码自动配对，也可手动打开当前家长网页并导入配置文件。");
}
void export_parent_import(UiState *ui)
{
    char device[PTC_DEVICE_ID_MAX_LEN + 1];
    char secret[PTC_GRANT_SECRET_MAX_LEN + 1];
    cJSON *root;
    char *json;
    bool ok;
    ui->auth_retry_action = AUTH_RETRY_EXPORT_CONFIG;
    if (!verify_sensitive_pin(ui, "导出包含加时码密钥的配置前，请再次输入本应用 PIN")) return;
    if (!read_pairing_values(ui, device, sizeof(device), secret, sizeof(secret))) {
        snprintf(ui->model.message, sizeof(ui->model.message), "读取家长网页导入信息失败。");
        return;
    }
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "device_id", device);
    cJSON_AddStringToObject(root, "grant_secret", secret);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = json && ui->client.storage->vtable->write_text_atomic(
        ui->client.storage, APP_ROOT "/parent-import.json", json);
    free(json);
    show_grant_manager(ui, PTC_UI_GRANT_MANAGER_EXPORT);
    snprintf(ui->model.message, sizeof(ui->model.message), "%s",
             ok ? "已导出到 " APP_ROOT "/parent-import.json；把文件导入家长网页。文件包含密钥，请妥善保管。"
                : "生成家长网页导入文件失败。");
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
    if (!verify_sensitive_pin(ui, "显示当前加时码密钥前，请再次输入本应用 PIN")) return;
    ui->model.credential_revealed = true;
    ui->model.credential_new_revealed = true;
}
