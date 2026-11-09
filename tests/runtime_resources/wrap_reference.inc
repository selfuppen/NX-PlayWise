// Frozen prefix-measurement reference from c495fc0fb0d7690a9a6f702124da971a1f9827ed.
static std::vector<std::string> reference_wrap(tsl::gfx::Renderer *renderer,
    const char *value, int size, int width)
{
    char localized[2048], sanitized[2048];
    ptc_overlay_sanitize_utf8(ptc_ui_localize(value, localized, sizeof(localized)),
        sanitized, sizeof(sanitized));
    std::vector<std::string> lines;
    std::string line;
    const std::string text(sanitized);
    auto measured = [&](const std::string &s) {
        return static_cast<int>(renderer->drawString(s.c_str(), false, 0, 0,
            size, tsl::style::color::ColorTransparent).first);
    };
    auto flush = [&] {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
        line.clear();
    };
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') { flush(); ++i; continue; }
        if (text[i] == ' ' && line.empty()) { ++i; continue; }
        size_t end = i + 1;
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 0xc0) {
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
        } else if (c != ' ') {
            while (end < text.size() && static_cast<unsigned char>(text[end]) < 0x80 &&
                text[end] != ' ' && text[end] != '\n') ++end;
        }
        const std::string token = text.substr(i, end - i);
        if (!line.empty() && measured(line + token) > width) flush();
        if (token == " " && line.empty()) { i = end; continue; }
        if (measured(token) > width) {
            for (size_t j = 0; j < token.size();) {
                size_t next = j + 1;
                while (next < token.size() && (static_cast<unsigned char>(token[next]) & 0xc0) == 0x80) ++next;
                const std::string character = token.substr(j, next - j);
                if (!line.empty() && measured(line + character) > width) flush();
                line += character;
                j = next;
            }
        } else line += token;
        i = end;
    }
    if (!line.empty() || lines.empty() || (!text.empty() && text.back() == '\n')) flush();
    return lines;
}

static std::vector<std::string> reference_button_lines(tsl::gfx::Renderer *renderer,
    const char *text, int width, int height, int &size)
{
    const int minimum = std::min(size, static_cast<int>(PTC_OVERLAY_FONT_SECONDARY));
    auto lines = reference_wrap(renderer, text, size, width);
    while (size > minimum && static_cast<int>(lines.size()) * ((size * 6 + 4) / 5) > height) {
        --size;
        lines = reference_wrap(renderer, text, size, width);
    }
    const int count = height / ((size * 6 + 4) / 5);
    if (count <= 0) return {};
    if (static_cast<int>(lines.size()) > count) {
        lines.resize(count);
        auto &last = lines.back();
        while (!last.empty() && static_cast<int>(measure_localized(renderer, (last + "...").c_str(), size).first) > width) {
            size_t end = last.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(last[end]) & 0xc0) == 0x80) --end;
            last.resize(end);
        }
        if (static_cast<int>(measure_localized(renderer, "...", size).first) <= width) last += "...";
    }
    return lines;
}
