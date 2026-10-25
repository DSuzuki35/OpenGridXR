#include "overlay_protocol.hpp"
#include <charconv>
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
    if (s.size() > 4096) {
        error = "Command too large";
        return false;
    }
    size_t p = 0;
    auto ws = [&] {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\r' || s[p] == '\t'))
            ++p;
    };
    auto quoted = [&](std::string &value) {
        if (p >= s.size() || s[p++] != '"')
            return false;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' || static_cast<unsigned char>(s[p]) < 32)
                return false;
            value += s[p++];
            if (value.size() > 128)
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
            if (!quoted(value)) {
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
        if (!out.values.emplace(key, value).second || out.values.size() > 24) {
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
    if (op != "frame" && op != "configure" && op != "clear" && op != "recenter" &&
        op != "shutdown") {
        error = "Unknown operation";
        return false;
    }
    return true;
}
} // namespace og
