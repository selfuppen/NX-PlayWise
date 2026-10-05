// Shared production Overlay colors, text and view types.
constexpr tsl::Color PANEL_COLOR{ 0x1, 0x1, 0x1, 0xE };
constexpr tsl::Color CARD_COLOR{ 0x2, 0x2, 0x2, 0xF };
constexpr tsl::Color KEY_COLOR{ 0x2, 0x2, 0x3, 0xF };
constexpr tsl::Color FOCUS_BG{ 0x0, 0x5, 0x6, 0xF };
constexpr tsl::Color FOCUS_BORDER{ 0x0, 0xD, 0xC, 0xF };
constexpr tsl::Color TEXT_COLOR{ 0xF, 0xF, 0xF, 0xF };
constexpr tsl::Color DARK_TEXT_COLOR{ 0x0, 0x1, 0x1, 0xF };
constexpr tsl::Color MUTED_COLOR{ 0x8, 0x9, 0xA, 0xF };
constexpr tsl::Color DISABLED_COLOR{ 0x2, 0x2, 0x2, 0xF };
constexpr tsl::Color SUCCESS_COLOR{ 0x1, 0xD, 0x7, 0xF };
constexpr tsl::Color ERROR_COLOR{ 0xF, 0x3, 0x4, 0xF };
constexpr tsl::Color WAITING_COLOR{ 0xF, 0x9, 0x1, 0xF };
constexpr tsl::Color DANGER_BG{ 0x3, 0x1, 0x1, 0xF };
constexpr tsl::Color WARNING_BG{ 0x3, 0x2, 0x1, 0xF };
constexpr tsl::Color BACKSPACE_BORDER{ 0xC, 0x7, 0x2, 0xF };
constexpr tsl::Color CLEAR_BORDER{ 0xC, 0x3, 0x3, 0xF };

static std::pair<u32, u32> draw_localized(tsl::gfx::Renderer *renderer,
    const char *value, bool monospace, s32 x, s32 y, float font_size,
    tsl::Color color, ssize_t max_width = 0)
{
    char localized[2048];
    return renderer->drawString(ptc_ui_localize(value, localized, sizeof(localized)),
        monospace, x, y, font_size, color, max_width);
}

// The offline code MAC binds the day index reported by status, so the console date
// must come from the summary. A local-clock guess could advertise a day the
// sysmodule would reject.
static void format_console_date(const PtcCompanionResultSummary &summary, char *out, size_t out_size)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    if (!out || out_size == 0) return;
    if (!summary.valid || summary.day_index < 0 || summary.day_index > 0xffff ||
        !ptc_date_from_day_index(static_cast<uint16_t>(summary.day_index), &year, &month, &day)) {
        std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CONSOLE_DATE_PENDING_REFRESH));
        return;
    }
    std::snprintf(out, out_size, ptc_ui_text(PTC_UI_T_CONSOLE_TODAY_04U_02U_02U),
                  static_cast<unsigned int>(year), static_cast<unsigned int>(month),
                  static_cast<unsigned int>(day));
}

enum class OverlayRequestKind {
    None,
    Status,
    OverlayReady,
    PreviewOfflineCode,
    OfflineCode,
    ClaimDailyBuffer,
    AddTodayMinutes,
    DisableTodayLimit,
    SkipBedtime,
    SkipEyeCare,
    ClearBedtimeSkip,
    DisableBedtime,
    RestoreInstallSnapshot,
};

enum class ParentView {
    Child,
    Pin,
    Actions,
    Confirm,
    Result,
};
