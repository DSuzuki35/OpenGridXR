#pragma once
#include "overlay_transport.hpp"
#include <openxr/openxr.h>
namespace og {
struct DesktopHostSchedule {
    bool monitorMouse{};
    unsigned waitMs{};
    bool pollUi{};
};
DesktopHostSchedule desktop_host_schedule(const OverlayShared &, bool pendingWork);
unsigned desktop_host_wait(bool monitorMouse, bool pendingWork, bool pollUi = false);
struct DesktopHit {
    float u{}, v{}, distance{};
};
bool desktop_hit(const XrPosef &, float, float, const XrPosef &, DesktopHit &);
bool desktop_pixel(float, float, int, int, int, int, int &, int &);
unsigned desktop_input_events(int, int, int, int, int, int, const char *, std::array<INPUT, 3> &);
int desktop_target_slot(const OverlayShared &, uint64_t);
bool desktop_occluded(const XrPosef &, const XrPosef *, const float *, const float *, unsigned);
bool desktop_target_fresh(const OverlayShared &, const OverlaySlot &, uint64_t);
bool pointer_target_fresh(const OverlayShared &, const OverlaySlot &, uint64_t);
bool pointer_identity_equal(const DesktopPointer &, const DesktopPointer &);
class DesktopInputOwnership {
    uint64_t physical_{};
    bool paused_{};

  public:
    void resume(uint64_t epoch) {
        physical_ = epoch;
        paused_ = false;
    }
    bool observe(uint64_t epoch) {
        if (epoch != physical_) {
            physical_ = epoch;
            paused_ = true;
        }
        return !paused_;
    }
    bool paused() const { return paused_; }
};
struct DesktopShortcut { unsigned key{}, modifiers{}; };
bool desktop_shortcut(const std::string &, DesktopShortcut &);
bool desktop_precision_geometry(XrPosef &, float &, float &, float, float, float);
class DesktopPrecision {
    uint64_t pressId_{}, until_{};
    bool keyDown_{}, keyHeld_{};
  public:
    bool request(uint64_t now, bool held, uint64_t pressId, bool eligible);
    bool update(uint64_t now, bool keyboard, bool eligible);
    void cancel() { until_ = 0; keyHeld_ = false; }
};
} // namespace og
