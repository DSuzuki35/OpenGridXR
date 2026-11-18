#pragma once
#include "overlay_transport.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <set>
namespace og {
struct OverlayCommand {
    std::map<std::string, std::string> values;
    std::set<std::string> strings;
    // Native receipt snapshot, never populated from caller-supplied JSON.
    DesktopPointer clickTarget;
    uint64_t receivedTick{};
    bool receivedTarget{};
    std::string text(const char *key, const std::string &fallback = {}) const;
    double number(const char *key, double fallback) const;
    bool boolean(const char *key, bool fallback = false) const;
};
bool parse_overlay_command(const std::string &, OverlayCommand &, std::string &error);
bool overlay_handle(const std::string &, std::uint64_t &);
} // namespace og
