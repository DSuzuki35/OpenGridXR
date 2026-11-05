#pragma once
#include "image_processing.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <windows.h>
namespace og {
enum class ColorPreset : uint32_t { Neutral, Sun, Night, Custom };
const char *color_preset_name(ColorPreset);
bool color_shortcut_keys(const std::string &, std::vector<WORD> &);
struct ColorConfig {
    bool enabled{}, configured{};
    ColorPreset preset{ColorPreset::Neutral};
    std::string shortcut{"Ctrl+Alt+F6"};
    std::array<SceneColorSettings, 4> presets{{{true, 100, 100, 100, 100},
                                               {true, 100, 75, 95, 100},
                                               {true, 95, 110, 95, 135},
                                               {true, 100, 100, 100, 100}}};
};
class ColorRuntime {
    ColorConfig config_;
    ColorPreset preset_;
    uint64_t frame_{std::numeric_limits<uint64_t>::max()};
    bool down_{}, sampled_{};
    SceneColorSettings frameColor_;

  public:
    explicit ColorRuntime(const ColorConfig &config = {})
        : config_(config), preset_(config.preset) {}
    void cycle();
    void suppressHeld(bool down) {
        down_ = down;
        sampled_ = true;
    }
    void frame(uint64_t, bool focused, bool shortcutDown);
    ColorPreset preset() const { return preset_; }
    const SceneColorSettings &frameColor() const { return frameColor_; }
};
constexpr uint32_t colorMagic = 0x4f474343, colorVersion = 1;
struct ColorShared {
    uint32_t magic{colorMagic}, version{colorVersion}, size{sizeof(ColorShared)}, pid{};
    uint64_t session{}, heartbeat{}, focusEpoch{};
    uint32_t running{}, focused{}, supported{}, enabled{}, activePreset{}, effectActive{};
    uint64_t command{}, commandSession{}, commandEpoch{}, expires{}, ack{};
    uint32_t accepted{};
};
// Pure protocol validation: every request is consumed exactly once, including rejects.
bool consume_color_command(ColorShared &, uint64_t now);
class ColorChannel {
    HANDLE mapping_{}, mutex_{};
    ColorShared *data_{};
    uint64_t ownerSession_{}, focusEpoch_{};
    bool focused_{}, focusKnown_{};

  public:
    ColorChannel() = default;
    ColorChannel(const ColorChannel &) = delete;
    ColorChannel &operator=(const ColorChannel &) = delete;
    ~ColorChannel();
    bool open(const std::string &game, bool owner = false, uint64_t session = 0);
    bool lock(DWORD timeout = 0);
    void unlock();
    ColorShared *data() { return data_; }
    bool owns() const { return ownerSession_ != 0; }
    void publish(ColorRuntime &, bool running, bool focused, bool supported, bool enabled,
                 bool effectActive, uint64_t now, bool serviceCommands = false);
};
struct ColorStatus {
    bool available{}, supported{}, enabled{}, effectActive{};
    ColorPreset activePreset{ColorPreset::Neutral};
    std::string error;
};
ColorStatus color_control(const std::string &game, bool cycle);
std::string color_status_json(const ColorStatus &);
} // namespace og
