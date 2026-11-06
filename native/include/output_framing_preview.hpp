#pragma once
#include "settings.hpp"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <windows.h>
namespace og {
class OutputFramingPreview {
    HANDLE pipe_{};
    std::string line_;
    double values_[6]{};
    uint64_t refreshed_{};
    bool active_{}, discarding_{};
    bool command(uint64_t now) {
        if (line_ == "R") { active_ = false; return true; }
        std::istringstream input(line_);
        input.imbue(std::locale::classic());
        char command{};
        double next[6]{};
        if (!(input >> command) || command != 'F') return false;
        for (unsigned i = 0; i < 6; ++i)
            if (!(input >> next[i]) || !std::isfinite(next[i]) ||
                next[i] < (i < 4 ? 0 : -100) || next[i] > (i < 4 ? 30 : 100)) return false;
        input >> std::ws;
        if (!input.eof()) return false;
        std::copy(next, next + 6, values_);
        active_ = true;
        refreshed_ = now;
        return false;
    }
  public:
    explicit OutputFramingPreview(HANDLE pipe)
        : pipe_(pipe && pipe != INVALID_HANDLE_VALUE && GetFileType(pipe) == FILE_TYPE_PIPE ? pipe : nullptr) {}
    // Exactly one bounded, nonblocking read per output iteration. Only the
    // explicitly inherited pipe is accepted; there is no public endpoint.
    bool poll(uint64_t now) {
        bool restore = false;
        if (pipe_) {
            DWORD available{};
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr)) {
                pipe_ = nullptr; active_ = false; line_.clear(); restore = true;
            } else if (available) {
                char bytes[4096]; DWORD read{};
                if (!ReadFile(pipe_, bytes, std::min<DWORD>(available, sizeof(bytes)), &read, nullptr) || !read) {
                    pipe_ = nullptr; active_ = false; line_.clear(); restore = true;
                } else for (DWORD i = 0; i < read; ++i) {
                    if (bytes[i] == '\n') {
                        if (!discarding_) restore = command(now) || restore;
                        line_.clear(); discarding_ = false;
                    } else if (!discarding_) {
                        if (line_.size() >= 255) { line_.clear(); discarding_ = true; }
                        else line_ += bytes[i];
                    }
                }
            }
        }
        if (active_ && now - refreshed_ >= 2000) { active_ = false; restore = true; }
        return restore;
    }
    void apply(const Config& saved, Config& output) const {
        output.reserve = active_ ? Edges{values_[0], values_[1], values_[2], values_[3]} : saved.reserve;
        output.offsetX = active_ ? values_[4] : saved.offsetX;
        output.offsetY = active_ ? values_[5] : saved.offsetY;
    }
};
}
