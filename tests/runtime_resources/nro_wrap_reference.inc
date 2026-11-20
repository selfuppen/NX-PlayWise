extern const char *ptc_resource_next_wrapped_line(const char *, int, int, const char **);

/* Frozen pre-optimization split policy; fixture input is valid UTF-8. */
static const char *reference_nro_line(const char *cursor, int size, int max_width, const char **line_end)
{
    const char *end = cursor;
    const char *newline = strchr(cursor, '\n');
    const char *last_space = NULL;
    int width = 0;
    while (*end && end != newline) {
        if (*end == ' ') last_space = end;
        const char *next = end + 1;
        while (((unsigned char)*next & 0xc0) == 0x80) ++next;
        char glyph[5] = {0};
        memcpy(glyph, end, (size_t)(next - end));
        int advance = measure_text(glyph, size);
        if (end > cursor && width + advance > max_width) {
            if (last_space && last_space > cursor) end = last_space;
            break;
        }
        width += advance;
        end = next;
    }
    if (end == cursor) {
        end = cursor + 1;
        while (((unsigned char)*end & 0xc0) == 0x80) ++end;
    }
    *line_end = end;
    return *end == '\n' ? end + 1 : (*end == ' ' ? end + 1 : end);
}

static void check_nro_wrapping(void)
{
    char long_text[4001]; memset(long_text, 'W', sizeof(long_text) - 1); long_text[4000] = 0;
    const char *texts[] = {"", "\n", "\n\nA", " A  B ", "one two three\nfour", "A\n\nB\n",
        "你好測試 wrap 🎮 •", "Supercalifragilisticexpialidocious", long_text};
    const int widths[] = {-1, 0, 10, 45, 120, 4096};
    for (int language = PTC_UI_LANGUAGE_SIMPLIFIED; language <= PTC_UI_LANGUAGE_ENGLISH; ++language) {
        ptc_ui_language_set_resolved((PtcUiLanguagePreference)language);
        ui_glyph_cache_clear();
        for (unsigned ti = 0; ti < sizeof(texts) / sizeof(texts[0]); ++ti) {
            char localized[4096];
            const char *text = ptc_ui_localize(texts[ti], localized, sizeof(localized));
            for (unsigned wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
                const char *cursor = text;
                int lines = 0;
                while (*cursor) {
                    const char *expected_end, *actual_end;
                    const char *expected = reference_nro_line(cursor, 24, widths[wi], &expected_end);
                    const char *actual = ptc_resource_next_wrapped_line(cursor, 24, widths[wi], &actual_end);
                    check(actual == expected && actual_end == expected_end, "wrapping retains byte boundaries and whitespace policy");
                    if (actual != expected || actual_end != expected_end) break;
                    cursor = expected;
                    ++lines;
                }
                check(count_wrapped_lines(texts[ti], 24, widths[wi], 5000) == lines, "wrapped count matches old split policy");
                check(count_wrapped_lines(texts[ti], 24, widths[wi], 2) == (lines < 2 ? lines : 2), "wrapped count respects line limit");
                check(count_wrapped_lines(texts[ti], 24, widths[wi], 0) == 0, "zero line limit remains empty");
            }
        }
    }
    ui_glyph_cache_clear();
}
