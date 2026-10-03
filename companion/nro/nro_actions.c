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
    if (ptc_ui_language_get_resolved() != previous) {
        ptc_ui_graphics_language_changed();
        /* Feedback is formatted at result time; discard it after a language change. */
        ui->model.message[0] = '\0';
        ui->model.feedback_detail[0] = '\0';
        ptc_ui_refresh_recent_event_labels(&ui->model);
        ui->model.grant_notice[0] = '\0';
        ui->model.album_restriction_detail[0] = '\0';
        if (ui->model.overlay == PTC_UI_OVERLAY_LANGUAGE) {
            snprintf(ui->model.overlay_title, sizeof(ui->model.overlay_title), "%s", ptc_ui_text(PTC_UI_T_UI_LANGUAGE));
            snprintf(ui->model.overlay_body, sizeof(ui->model.overlay_body), "%s",
                     ptc_ui_text(PTC_UI_T_THE_HOST_APPLICATION_SHARES_THIS_SETTING_WITH));
        } else if (ui->model.overlay != PTC_UI_OVERLAY_NONE) {
            ptc_ui_cancel_overlay(&ui->model);
        }
    }
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
                 ptc_ui_text(PTC_UI_T_STATUS_READING_FAILED_PLEASE_CHECK_AGAIN));
        return;
    }
    ui->model.album_restriction_state = (int)status.state;
    ui->model.album_backup_valid = status.backup_valid;
    snprintf(ui->model.album_restriction_detail, sizeof(ui->model.album_restriction_detail), "%s", status.detail);
}
