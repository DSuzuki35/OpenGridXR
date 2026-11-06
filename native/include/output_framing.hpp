#pragma once
#include "settings.hpp"
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <windows.h>
namespace og {
// Output-process only. A saved profile changes framing without rebuilding the
// renderer or changing geometry negotiated by the running XR session.
class OutputFramingReload {
    std::wstring path_;
    std::string game_;
    uint64_t lastPoll_{};
    bool polled_{};

    static std::string trim(const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r");
        return first == std::string::npos ? std::string{} :
            text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    }

  public:
    OutputFramingReload(const std::wstring& path, const std::string& game)
        : path_(path), game_(game) {}

    bool poll(Config& config, uint64_t now, bool force = false) {
        if (!force && polled_ && now - lastPoll_ < 500) return false;
        polled_ = true;
        lastPoll_ = now;
        if (game_ != "lmu" && game_ != "iracing") return false;
        // Share delete so the desktop's atomic rename can proceed, but refuse
        // concurrent in-place writes. Parse one complete, bounded file snapshot.
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
            CloseHandle(file);
            return false;
        }
        std::string content(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read{};
        const bool complete = ReadFile(file, content.data(), DWORD(content.size()), &read, nullptr)
                              && read == content.size();
        CloseHandle(file);
        if (!complete || content.back() != '\n' || content.find('\0') != std::string::npos)
            return false;
        const char* keys[]{"ReserveLeft", "ReserveRight", "ReserveTop", "ReserveBottom",
                           "OffsetX", "OffsetY"};
        double values[6]{};
        bool seen[6]{};
        bool selected = false;
        std::istringstream input(content);
        std::string line;
        while (std::getline(input, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line[0] == '[') {
                selected = line == "[" + game_ + "]";
                continue;
            }
            if (!selected) continue;
            const auto equal = line.find('=');
            if (equal == std::string::npos) return false;
            const auto key = trim(line.substr(0, equal));
            for (unsigned i = 0; i < 6; ++i) {
                if (key != keys[i]) continue;
                if (seen[i]) return false;
                const auto text = trim(line.substr(equal + 1));
                char* end{};
                const auto value = std::strtod(text.c_str(), &end);
                if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value) ||
                    value < (i < 4 ? 0 : -100) || value > (i < 4 ? 30 : 100)) return false;
                values[i] = value;
                seen[i] = true;
            }
        }
        for (bool present : seen) if (!present) return false;
        const bool changed = config.reserve.left != values[0] || config.reserve.right != values[1] ||
            config.reserve.top != values[2] || config.reserve.bottom != values[3] ||
            config.offsetX != values[4] || config.offsetY != values[5];
        config.reserve = {values[0], values[1], values[2], values[3]};
        config.offsetX = values[4];
        config.offsetY = values[5];
        return changed;
    }
};
}
