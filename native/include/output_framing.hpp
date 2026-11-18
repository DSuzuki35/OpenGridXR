#pragma once
#include "settings.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>
#include <windows.h>
namespace og {
// Output-process only. A saved profile changes framing without rebuilding the
// renderer or changing geometry negotiated by the running XR session.
class OutputFramingReload {
    struct FileStamp {
        LARGE_INTEGER size{};
        FILETIME writeTime{};
        FILE_ID_INFO id{};
        bool identified{};

        bool inspect(HANDLE file) {
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(file, &info))
                return GetFileSizeEx(file, &size) != 0;
            size.HighPart = info.nFileSizeHigh;
            size.LowPart = info.nFileSizeLow;
            writeTime = info.ftLastWriteTime;
            const FILE_ID_128 empty{};
            identified = (writeTime.dwLowDateTime || writeTime.dwHighDateTime)
                && GetFileInformationByHandleEx(file, FileIdInfo, &id, sizeof(id))
                && id.VolumeSerialNumber && std::memcmp(&id.FileId, &empty, sizeof(empty)) != 0;
            return true;
        }

        bool unchanged(const FileStamp& saved, uint64_t age) const {
            if (size.QuadPart != saved.size.QuadPart || CompareFileTime(&writeTime, &saved.writeTime)
                || identified != saved.identified) return false;
            // Some filesystems cannot supply a stable ID. A same-size rename
            // can preserve the timestamp, so sample the contents every 5 s there.
            if (!identified) return age < 5000;
            return id.VolumeSerialNumber == saved.id.VolumeSerialNumber &&
                std::memcmp(&id.FileId, &saved.id.FileId, sizeof(id.FileId)) == 0;
        }
    };
    std::wstring path_;
    std::string game_;
    uint64_t lastPoll_{};
    uint64_t lastRead_{};
    std::optional<FileStamp> savedStamp_;
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
        if (file == INVALID_HANDLE_VALUE) {
            savedStamp_.reset();
            return false;
        }
        FileStamp stamp;
        if (!stamp.inspect(file) || stamp.size.QuadPart <= 0 || stamp.size.QuadPart > 1024 * 1024) {
            savedStamp_.reset();
            CloseHandle(file);
            return false;
        }
        if (!force && savedStamp_ && stamp.unchanged(*savedStamp_, now - lastRead_)) {
            CloseHandle(file);
            return false;
        }
        savedStamp_.reset(); // Only a completely read and validated snapshot can be cached.
        std::string content(static_cast<size_t>(stamp.size.QuadPart), '\0');
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
        savedStamp_ = stamp;
        lastRead_ = now;
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
