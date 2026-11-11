// Host capacity model for libultrahand 856ddbddd796fc4a59ad2e0bf939c5963e6f9dd2:
// title layout: 448px, 4 body lines, 26px text; pending icon reserves 88px.
// Uses the pinned documentation font, NOT the Switch font or receiver renderer.
// It guards content capacity; actual visibility/lifecycle still needs a device.
#define STB_TRUETYPE_IMPLEMENTATION
#include "companion/overlay/vendor/libtesla/include/stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
extern "C" {
#include "sysmodule/reason_notification.h"
#include "companion/notification_text.h"
}

static stbtt_fontinfo font;
static float metric_scale = 1.f;
static float width(const std::string& text) {
    float result = 0;
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, 26.f) * metric_scale;
    for (size_t i = 0; i < text.size();) {
        unsigned cp = static_cast<unsigned char>(text[i++]);
        if (cp >= 0xc0) {
            const unsigned count = cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
            cp &= (1u << (6-count)) - 1;
            for (unsigned j=0;j<count && i<text.size();++j)
                cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(cp), &advance, &bearing);
        result += std::ceil(advance * scale);
    }
    return result;
}

static std::vector<std::string> wrap(const std::string& body, bool cjk, int max_width) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(body);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::vector<std::string> tokens;
        if (cjk) {
            for (size_t i=0;i<paragraph.size();) {
                unsigned ch=static_cast<unsigned char>(paragraph[i]);
                size_t count=ch<0x80 ? 1 : ch<0xe0 ? 2 : ch<0xf0 ? 3 : 4;
                tokens.push_back(paragraph.substr(i,count)); i+=count;
            }
        } else {
            std::istringstream words(paragraph); std::string word;
            while(words>>word) tokens.push_back(word);
        }
        std::string current;
        for (const auto& token:tokens) {
            std::string candidate=current+(!cjk && !current.empty() ? " " : "")+token;
            if(width(candidate)>max_width && !current.empty()) {
                lines.push_back(current); current=token;
            } else current=candidate;
        }
        if(!current.empty()) lines.push_back(current);
    }
    return lines;
}

static std::string escape(const std::string& value) {
    std::string result;
    for (char c:value) {
        if(c=='&') result+="&amp;";
        else if(c=='<') result+="&lt;";
        else if(c=='>') result+="&gt;";
        else result+=c;
    }
    return result;
}

int main(int argc, char **argv) {
    if(argc!=3) return 2;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)),{});
    if(data.empty() || !stbtt_InitFont(&font,data.data(),stbtt_GetFontOffsetForIndex(data.data(),0))) return 2;
    std::ofstream html(argv[2]);
    html << "<!doctype html><meta charset=utf-8><title>Host notice capacity preview</title>"
        "<style>body{background:#eee;font-family:sans-serif}.card{display:inline-block;vertical-align:top;margin:12px}"
        "svg{background:#18202c;color:white}text{fill:white;font-family:sans-serif}</style>"
        "<h1>HOST PREVIEW / SAMPLE DATA</h1><p>Content capacity model, not a Switch screenshot. "
        "26px text; fixed receiver width; pending icon reserve; pinned Noto font metrics. Device visibility pending.</p>";
    unsigned cases=0,max_lines=0,failures=0;
    for(int lang=PTC_UI_LANGUAGE_SIMPLIFIED;lang<=PTC_UI_LANGUAGE_ENGLISH;++lang)
    for(int reason=PTC_REASON_DAILY;reason<=PTC_REASON_DOCK;++reason)
    for(int kind=PTC_REASON_TIME_UNKNOWN;kind<=PTC_REASON_TIME_CONNECT_TV;++kind)
    for(unsigned seconds:{0u,1u,59u,60u,61u,3599u,3600u,3601u,86340u,86399u,86400u,86401u})
    for(const char *combo:{"","L＋↓","ZR＋ZL＋LS＋RS"}) {
        PtcReasonObservation s={};
        s.reliable=s.restricted=true; s.reason=static_cast<PtcRestrictionReason>(reason);
        s.captured_at=100; s.instance=1; s.time_kind=static_cast<PtcReasonTime>(kind); s.remaining_seconds=seconds;
        char body[512];
        if(!ptc_reason_notification_body(&s,100,static_cast<PtcUiLanguagePreference>(lang),combo,body,sizeof(body))) return 1;
        for(float stress:{1.f,1.1f}) for(int capacity:{329,396}) {
            metric_scale=stress;
            const auto lines=wrap(body,lang!=PTC_UI_LANGUAGE_ENGLISH,capacity);
            max_lines=std::max(max_lines,static_cast<unsigned>(lines.size()));
            bool fits=lines.size()<=4;
            for(const auto& line:lines) fits= fits && width(line)<=capacity;
            if(!fits) {
                std::fprintf(stderr,"FAIL notice capacity lang=%d reason=%d kind=%d seconds=%u combo=%s stress=%.1f width=%d lines=%zu\n%s\n",
                    lang,reason,kind,seconds,combo,stress,capacity,lines.size(),body); ++failures;
            }
            // A small representative gallery, including longest valid duration.
            if(stress==1.f && capacity==329 && kind==(reason==PTC_REASON_DAILY ? PTC_REASON_TIME_DAILY_ZERO :
                reason==PTC_REASON_DOCK ? PTC_REASON_TIME_CONNECT_TV : PTC_REASON_TIME_REMAINING) &&
                seconds==(reason==PTC_REASON_EYE ? 61u : 86340u) && !combo[0]) {
                const int height=88+static_cast<int>(lines.size()-1)*42;
                html << "<div class=card><p>Language " << lang << ", reason " << reason << ", modeled height " << height << "px</p>"
                    "<svg width=448 height=" << height << "><text x=20 y=24 font-size=18>" << escape(ptc_notification_text(PTC_UI_T_NOTIFY_TITLE,static_cast<PtcUiLanguagePreference>(lang))) << "</text>";
                for(size_t i=0;i<lines.size();++i) html << "<text x=20 y=" << 62+i*42 << " font-size=26>" << escape(lines[i]) << "</text>";
                html << "</svg></div>";
            }
            ++cases;
        }
    }
    std::printf("{\"schema_version\":1,\"cases\":%u,\"max_body_lines\":%u,\"line_cap\":4,\"font_size\":26,\"failures\":%u,\"device_verified\":false}\n",cases,max_lines,failures);
    return failures ? 1 : 0;
}
