#include "nro_app_internal.h"

static PtcUiSystemTheme read_system_theme(void)
{
    ColorSetId color_set;
    Result result = setsysInitialize();
    if (R_FAILED(result)) return PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    result = setsysGetColorSetId(&color_set);
    setsysExit();
    if (R_FAILED(result)) return PTC_UI_SYSTEM_THEME_UNAVAILABLE;
    if (color_set == ColorSetId_Light) return PTC_UI_SYSTEM_THEME_LIGHT;
    if (color_set == ColorSetId_Dark) return PTC_UI_SYSTEM_THEME_DARK;
    return PTC_UI_SYSTEM_THEME_UNAVAILABLE;
}

static PtcUiSystemLanguage read_system_language(void)
{
    u64 language_code;
    SetLanguage language;
    if (R_FAILED(setInitialize())) return PTC_UI_SYSTEM_LANGUAGE_UNKNOWN;
    Result result = setGetSystemLanguage(&language_code);
    if (R_SUCCEEDED(result)) result = setMakeLanguage(language_code, &language);
    setExit();
    if (R_FAILED(result)) return PTC_UI_SYSTEM_LANGUAGE_UNKNOWN;
    if (language == SetLanguage_ZHCN || language == SetLanguage_ZHHANS)
        return PTC_UI_SYSTEM_LANGUAGE_SIMPLIFIED;
    if (language == SetLanguage_ZHTW || language == SetLanguage_ZHHANT)
        return PTC_UI_SYSTEM_LANGUAGE_TRADITIONAL;
    if (language == SetLanguage_ENUS || language == SetLanguage_ENGB)
        return PTC_UI_SYSTEM_LANGUAGE_ENGLISH;
    return PTC_UI_SYSTEM_LANGUAGE_ENGLISH;
}

void refresh_language(UiState *ui)
{
    PtcUiLanguagePreference previous;
    if (!ui) return;
    previous = ptc_ui_language_get_resolved();
    ui->system_language = read_system_language();
    ptc_ui_language_set_resolved(ptc_ui_language_resolve(
        ui->language_preference, ui->system_language));
    if (ptc_ui_language_get_resolved() != previous) ptc_ui_graphics_language_changed();
}

void refresh_theme(UiState *ui)
{
    if (!ui) return;
    ui->system_theme = read_system_theme();
    ui->theme_view = ptc_ui_theme_make_view(ui->theme_preference, ui->system_theme);
    ui->theme_refresh_pending = false;
}

void trigger_resume_status_refresh(UiState *ui, int *background_poll_elapsed_ms)
{
    if (!ui || ui->waiting || ui->recovering_redemption) return;
    ui->status_refresh_pending = false;
    refresh_disable_flag(ui);
    refresh_album_restriction(ui);
    refresh_security_state(ui);
    submit_status(ui);
    if (background_poll_elapsed_ms) {
        *background_poll_elapsed_ms = BACKGROUND_POLL_INTERVAL_MS;
    }
}

void applet_hook(AppletHookType hook, void *param)
{
    UiState *ui = (UiState *)param;
    if (ui && (hook == AppletHookType_OnResume ||
               (hook == AppletHookType_OnFocusState && appletGetFocusState() == AppletFocusState_InFocus))) {
        ui->theme_refresh_pending = true;
        ui->status_refresh_pending = true;
    }
}

bool apply_theme_preference(UiState *ui, PtcUiThemePreference preference)
{
    PtcUiThemePreference previous;
    if (!ui) return false;
    previous = ui->theme_preference;
    ui->theme_preference = preference;
    ui->theme_view = ptc_ui_theme_make_view(preference, ui->system_theme);
    if (save_ui_preferences(ui)) return true;
    ui->theme_preference = previous;
    ui->theme_view = ptc_ui_theme_make_view(previous, ui->system_theme);
    return false;
}

bool apply_language_preference(UiState *ui, PtcUiLanguagePreference preference)
{
    PtcUiLanguagePreference previous;
    if (!ui) return false;
    previous = ui->language_preference;
    ui->language_preference = preference;
    ui->model.language_preference = preference;
    if (!save_ui_preferences(ui)) {
        ui->language_preference = previous;
        ui->model.language_preference = previous;
        return false;
    }
    refresh_language(ui);
    return true;
}


void refresh_album_restriction(UiState *ui)
{
    PtcAlbumRestrictionStatus status;
    if (!ui) return;
    if (!ptc_album_restriction_get_status(ui->client.storage, &status)) {
        ui->model.album_restriction_state = PTC_ALBUM_RESTRICTION_UNKNOWN;
        ui->model.album_backup_valid = false;
        snprintf(ui->model.album_restriction_detail,
                 sizeof(ui->model.album_restriction_detail),
                 "状态读取失败，请重新检测");
        return;
    }
    ui->model.album_restriction_state = (int)status.state;
    ui->model.album_backup_valid = status.backup_valid;
    snprintf(ui->model.album_restriction_detail, sizeof(ui->model.album_restriction_detail), "%s", status.detail);
}
