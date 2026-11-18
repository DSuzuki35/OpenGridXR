#include "overlay_protocol.hpp"
#include "desktop_interaction.hpp"
#include <charconv>
#include "wheel_model.hpp"
#include <cmath>
#include <cstdlib>
namespace og {
std::string OverlayCommand::text(const char *key, const std::string &fallback) const {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}
double OverlayCommand::number(const char *key, double fallback) const {
    auto it = values.find(key);
    if (it == values.end())
        return fallback;
    char *end{};
    auto v = strtod(it->second.c_str(), &end);
    return end == it->second.c_str() + it->second.size() && std::isfinite(v) ? v : fallback;
}
bool OverlayCommand::boolean(const char *key, bool fallback) const {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second == "true";
}
bool overlay_handle(const std::string &s, std::uint64_t &n) {
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    return !s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size() && n != 0;
}
bool parse_overlay_command(const std::string &s, OverlayCommand &out, std::string &error) {
    out.values.clear();
    out.strings.clear();
    out.receivedTarget = false;
    out.receivedTick = 0;
    out.clickTarget = {};
    if (s.size() > 4096) {
        error = "Command too large";
        return false;
    }
    size_t p = 0;
    auto ws = [&] {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\r' || s[p] == '\t'))
            ++p;
    };
    auto quoted = [&](std::string &value, size_t limit=128) {
        if (p >= s.size() || s[p++] != '"')
            return false;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' || static_cast<unsigned char>(s[p]) < 32)
                return false;
            value += s[p++];
            if (value.size() > limit)
                return false;
        }
        if (p >= s.size())
            return false;
        ++p;
        return true;
    };
    ws();
    if (p >= s.size() || s[p++] != '{') {
        error = "Expected object";
        return false;
    }
    ws();
    while (p < s.size() && s[p] != '}') {
        std::string key, value;
        if (!quoted(key) || key.empty()) {
            error = "Invalid key";
            return false;
        }
        ws();
        if (p >= s.size() || s[p++] != ':') {
            error = "Expected colon";
            return false;
        }
        ws();
        if (p < s.size() && s[p] == '"') {
            out.strings.insert(key);
            if (!quoted(value, key == "rgb" ? 576 : 128)) {
                error = "Invalid string";
                return false;
            }
        } else {
            auto start = p;
            while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ' ' && s[p] != '\r' &&
                   s[p] != '\t')
                ++p;
            value = s.substr(start, p - start);
            if (value != "true" && value != "false") {
                char *end{};
                auto v = strtod(value.c_str(), &end);
                if (value.empty() || end != value.c_str() + value.size() || !std::isfinite(v)) {
                    error = "Invalid scalar";
                    return false;
                }
            }
        }
        if (!out.values.emplace(key, value).second || out.values.size() > 32) {
            error = "Duplicate or excess keys";
            return false;
        }
        ws();
        if (p < s.size() && s[p] == ',') {
            ++p;
            ws();
            if (p >= s.size() || s[p] == '}') {
                error = "Trailing comma";
                return false;
            }
        } else
            break;
    }
    if (p >= s.size() || s[p++] != '}') {
        error = "Expected closing brace";
        return false;
    }
    ws();
    if (p != s.size()) {
        error = "Trailing content";
        return false;
    }
    auto op = out.text("op");
    if (out.values.count("uiToken") ||
        (op == "configure" && out.number("desktopMode", 0) == 2 && out.boolean("enabled"))) {
        const auto token = out.text("uiToken");
        bool valid = out.strings.count("uiToken") && token.size() == 36;
        for (size_t i = 0; valid && i < token.size(); ++i)
            valid = (i == 8 || i == 13 || i == 18 || i == 23) ? token[i] == '-' :
                    ((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f') ||
                     (token[i] >= 'A' && token[i] <= 'F'));
        if (!valid) { error = "Invalid internal UI token"; return false; }
        if (op == "click") {
            for (auto key : {"session", "focusEpoch", "configRevision", "sourceRevision", "recenter"}) {
                const auto value = out.text(key);
                uint64_t identity{};
                const auto result = std::from_chars(value.data(), value.data() + value.size(), identity);
                if (!out.strings.count(key) || value.empty() || result.ec != std::errc() ||
                    result.ptr != value.data() + value.size() || value != std::to_string(identity)) {
                    error = "Invalid internal UI click identity"; return false;
                }
            }
        }
    }
    if (op != "frame" && op != "configure" && op != "clear" && op != "recenter" &&
        op != "shutdown" && op != "click" && op != "resume" && op != "precision" &&
        op != "wheelLights" && op != "place" && op != "placementPreview" && op != "recenterShortcut") {
        error = "Unknown operation";
        return false;
    }
    if (op == "recenterShortcut") {
        DesktopShortcut shortcut;
        if (!out.strings.count("shortcut") || !desktop_shortcut(out.text("shortcut"), shortcut, true)) {
            error = "Invalid overlay recenter shortcut";
            return false;
        }
    }
    if (op == "place" || op == "placementPreview") {
        const auto slot = out.number("slot", -1);
        const auto mode = out.text("mode");
        if (out.strings.count("slot") || slot < 0 || slot >= overlay_count || slot != std::floor(slot) ||
            (op == "place" && (!out.strings.count("mode") || (mode != "start" && mode != "freeze" && mode != "cancel" && mode != "reset")))) {
            error = "Invalid dashboard placement command";
            return false;
        }
        if (out.values.count("requestId")) {
            const auto id = out.number("requestId", -1);
            if (out.strings.count("requestId") || id < 1 || id > 9007199254740991. || id != std::floor(id)) {
                error = "Invalid dashboard placement request ID";
                return false;
            }
        }
    }
    if ((op=="configure" && out.values.count("wheelModel")) || op=="wheelLights") {
        auto name=out.text("wheelModel");
        auto device=out.number("wheelDevice",-1);
        if(!wheel_model_id(name) || device<0 || device>5 || device!=std::floor(device)) {
            error="Invalid wheel source"; return false;
        }
        if (out.values.count("wheelAmbientLight")) {
            const auto strength = out.number("wheelAmbientLight", -1);
            if (out.strings.count("wheelAmbientLight") || strength < 0 || strength > 100) {
                error = "Invalid wheel lighting strength";
                return false;
            }
        }
        if(op=="wheelLights") {
            auto rgb=out.text("rgb");
            if(!out.strings.count("rgb") || (!rgb.empty() && rgb.size()!=6*wheel_pixel_count(unsigned(device))) ||
               rgb.find_first_not_of("0123456789abcdef")!=std::string::npos) {
                error="Invalid wheel LED frame"; return false;
            }
        }
    }
    if (op == "precision") {
        const auto id = out.number("pressId", -1), slot = out.number("slot", -1);
        auto held = out.text("held");
        if (id < 1 || id > 9007199254740991. || id != std::floor(id) || slot < 0 || slot >= 15 || slot != std::floor(slot) || (held != "true" && held != "false") || out.strings.count("held") || out.strings.count("slot") || out.strings.count("pressId")) {
            error = "Invalid precision lease"; return false;
        }
    }
    return true;
}
} // namespace og
