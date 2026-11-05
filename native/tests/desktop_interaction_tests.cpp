#include "desktop_interaction.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
int main() {
    try {
        og::OverlayShared schedule;
        auto &configured = schedule.slots[0];
        configured.enabled = 1;
        check(!og::desktop_host_schedule(schedule, false).monitorMouse &&
                  og::desktop_host_schedule(schedule, false).waitMs == 100,
              "URL-only host keeps baseline idle and no mouse hook");
        configured.desktopWidth = 1920;
        configured.desktopHeight = 1080;
        check(!og::desktop_host_schedule(schedule, false).monitorMouse,
              "physical mouse desktop requires no monitoring hook");
        configured.desktopMode = 1;
        check(og::desktop_host_schedule(schedule, false).monitorMouse &&
                  og::desktop_host_schedule(schedule, false).waitMs == 8,
              "enabled head desktop activates bounded input polling");
        configured.enabled = 0;
        check(!og::desktop_host_schedule(schedule, false).monitorMouse &&
                  og::desktop_host_schedule(schedule, false).waitMs == 100,
              "clearing final synthetic source restores idle cadence");
        check(og::desktop_host_schedule(schedule, true).waitMs == 2,
              "pending GPU work preserves existing two millisecond cadence");
        check(og::desktop_host_wait(false, false) == 100 &&
                  og::desktop_host_wait(true, false) == 8 && og::desktop_host_wait(true, true) == 2,
              "worker wait policy preserves idle, input and pending work cadences");
        og::DesktopHit hit;
        XrPosef quad{{0, 0, 0, 1}, {0, 0, -2}}, ray{{0, 0, 0, 1}, {.25f, .25f, 0}};
        check(og::desktop_hit(quad, 2, 1, ray, hit), "front ray hits posed quad");
        check(std::abs(hit.u - .625f) < .0001f && std::abs(hit.v - .25f) < .0001f,
              "UV has upper-left origin");
        ray.position.x = 2;
        check(!og::desktop_hit(quad, 2, 1, ray, hit), "outside ray rejected");
        ray.position = {0, 0, -3};
        check(!og::desktop_hit(quad, 2, 1, ray, hit), "behind ray rejected");
        quad = {{0, .70710678f, 0, .70710678f}, {-2, 0, 0}};
        ray = {{0, .70710678f, 0, .70710678f}, {0, 0, 0}};
        check(og::desktop_hit(quad, 2, 1, ray, hit) && std::abs(hit.u - .5f) < .0001f,
              "rotated ray and quad share space");
        int x{}, y{};
        check(og::desktop_pixel(.5f, 1, -1920, -200, 1920, 1080, x, y) && x == -960 && y == 879,
              "negative physical monitor coordinates and edge clamp");
        og::OverlayShared d;
        d.consumerPid = 1;
        d.inputFocused = 1;
        d.consumerBeat = 1000;
        d.session = 9;
        auto &s = d.slots[0];
        s.enabled = 1;
        s.desktopMode = 1;
        s.desktopWidth = 1920;
        s.desktopHeight = 1080;
        s.revision = 2;
        s.sourceRevision = 3;
        s.frames[0].generation = 4;
        s.recenter = 5;
        s.pointer = {1, 1, 1000, 9, 2, 3, 4, 5, .5f, .5f, 2};
        check(og::desktop_target_fresh(d, s, 1050), "fresh displayed target accepted");
        d.inputFocused = 0;
        check(!og::desktop_target_fresh(d, s, 1050),
              "focus loss immediately rejects head and fallback samples");
        d.inputFocused = 1;
        check(!og::desktop_target_fresh(d, s, 1201), "stale target rejected");
        check(!og::desktop_target_fresh(d, s, 999), "future sample cannot become current input");
        ++s.frames[0].generation;
        check(!og::desktop_target_fresh(d, s, 1050), "replacement texture invalidates old target");
        --s.frames[0].generation;
        ++s.sourceRevision;
        check(!og::desktop_target_fresh(d, s, 1050), "source revision invalidates old target");
        --s.sourceRevision;
        s.desktopMode = 0;
        check(!og::desktop_target_fresh(d, s, 1050),
              "physical mouse mode never permits synthetic click");
        s.desktopMode = 1;
        ++s.recenter;
        check(!og::desktop_target_fresh(d, s, 1050), "recenter immediately invalidates target");
        --s.recenter;
        ++s.revision;
        check(!og::desktop_target_fresh(d, s, 1050),
              "config revision immediately invalidates target");
        --s.revision;
        s.enabled = 0;
        check(!og::desktop_target_fresh(d, s, 1050), "hidden rejects input");
        s.enabled = 1;
        d.slots[1] = s;
        check(og::desktop_target_slot(d, 1050) == 1, "later composed valid desktop owns input");
        d.slots[1].enabled = 0;
        check(og::desktop_target_slot(d, 1050) == 0, "disabled upper desktop releases ownership");
        XrPosef occluder{{0, 0, 0, 1}, {0, 0, -1}}, centerRay{{0, 0, 0, 1}, {}};
        float w = 1, h = 1;
        check(og::desktop_occluded(centerRay, &occluder, &w, &h, 1),
              "later visible noninteractive quad blocks desktop ray");
        occluder.position.x = 2;
        check(!og::desktop_occluded(centerRay, &occluder, &w, &h, 1),
              "nonoverlapping quad leaves desktop input available");
        ++d.session;
        check(!og::desktop_target_fresh(d, s, 1050), "session replacement rejects input");
        std::array<INPUT, 3> events{};
        check(og::desktop_input_events(-960, 500, -1920, -200, 4480, 1440, "left", events) == 3,
              "left click emits move, down and up together");
        check(events[1].mi.dwFlags == MOUSEEVENTF_LEFTDOWN &&
                  events[2].mi.dwFlags == MOUSEEVENTF_LEFTUP,
              "left click cannot leave a held button");
        check(int64_t(events[0].mi.dx) * 4480 / 65536 - 1920 == -960 &&
                  int64_t(events[0].mi.dy) * 1440 / 65536 - 200 == 500,
              "absolute Windows mapping preserves exact negative desktop point");
        check(og::desktop_input_events(0, 0, -1920, -200, 4480, 1440, "right", events) == 3 &&
                  events[1].mi.dwFlags == MOUSEEVENTF_RIGHTDOWN &&
                  events[2].mi.dwFlags == MOUSEEVENTF_RIGHTUP,
              "right click emits matching release");
        check(og::desktop_input_events(0, 0, -1920, -200, 4480, 1440, "", events) == 1,
              "movement never presses buttons");
        check(!og::desktop_input_events(-1921, 0, -1920, -200, 4480, 1440, "left", events),
              "off-desktop input rejected");
        check(!og::desktop_input_events(0, 0, -1920, -200, 4480, 1440, "middle", events),
              "unsupported button rejected");
        og::DesktopInputOwnership ownership;
        ownership.resume(7);
        check(ownership.observe(7), "explicit resume allows synthetic source");
        check(!ownership.observe(8) && !ownership.observe(8),
              "physical activity pauses until explicit resume");
        ownership.resume(8);
        check(ownership.observe(8), "explicit resume restores ownership");
        s.desktopMode = 2;
        --d.session;
        check(og::desktop_target_slot(d, 1050) == 0, "internal UI head target participates in arbitration");
        check(!og::desktop_target_fresh(d, s, 1050), "internal UI target never permits OS desktop input");
        d.slots[1] = s;
        d.slots[1].desktopMode = 1;
        check(og::desktop_target_slot(d, 1050) == 1, "top desktop blocks internal UI selection");
        d.slots[1].enabled = 0;
        ++d.focusEpoch;
        check(og::desktop_target_slot(d, 1050) == -1, "old UI focus epoch fails after focus return");
        --d.focusEpoch;
        check(og::desktop_target_slot(d, 1201) == -1, "internal UI target expires at 200ms");
        configured.enabled = 1; configured.desktopMode = 2;
        check(!og::desktop_host_schedule(schedule, false).monitorMouse, "internal UI never enables input hook");
        check(og::desktop_host_schedule(schedule, false).pollUi &&
              og::desktop_host_schedule(schedule, false).waitMs == 33, "internal UI uses bounded polling without mouse ownership");
        configured.enabled = 0;
        check(!og::desktop_host_schedule(schedule, false).pollUi &&
              og::desktop_host_schedule(schedule, false).waitMs == 100, "hidden menu stops pointer polling");
        og::DesktopShortcut shortcut;
        check(og::desktop_shortcut("Ctrl+Alt+Shift+F12", shortcut) && shortcut.key == VK_F12 && shortcut.modifiers == 7, "function shortcut parses all modifiers");
        for (auto invalid : {"F0", "F13", "F01", "Ctrl+Ctrl+F9", "F9+Shift", "Ctrl+", "A"})
            check(!og::desktop_shortcut(invalid, shortcut), "invalid keyboard shortcut rejected");
        og::DesktopPrecision precision;
        check(precision.request(1000, true, 1, true), "fresh joystick press accepted");
        check(precision.update(1000, false, true), "joystick activates precision");
        check(precision.request(1100, true, 1, true) && precision.update(1350, false, true), "same press renews lease");
        check(!precision.update(1401, false, true), "expired lease restores panel");
        precision.request(1500, true, 1, true);
        check(!precision.update(1500, false, true), "expired old press cannot reactivate");
        precision.request(1600, true, 2, true);
        check(precision.update(1600, true, true), "keyboard and joystick union activates");
        precision.request(1650, false, 2, false);
        check(precision.update(1650, true, true), "joystick release preserves keyboard hold");
        check(!precision.update(1700, true, false), "focus hide or physical takeover cancels hold");
        precision.request(1750, true, 2, true);
        check(!precision.update(1750, true, true), "old held sources cannot reactivate after cancellation");
        precision.update(1800, false, true);
        check(precision.update(1850, true, true), "new keyboard edge can reactivate");
        check(!precision.update(1900, false, true), "keyboard release restores panel");
        precision.request(2000, true, 3, false);
        check(!precision.update(2000, false, true), "press without target stays latched out");
        quad = {{0, 0, 0, 1}, {0, 0, -2}};
        float zoomWidth = 2, zoomHeight = 1;
        check(og::desktop_precision_geometry(quad, zoomWidth, zoomHeight, 2, .625f, .25f), "precision transforms geometry");
        ray = {{0, 0, 0, 1}, {.25f, .25f, 0}};
        check(og::desktop_hit(quad, zoomWidth, zoomHeight, ray, hit) && std::abs(hit.u - .625f) < .0001f && std::abs(hit.v - .25f) < .0001f, "prepress world point remains stationary");
        ray.position.x += .2f;
        check(og::desktop_hit(quad, zoomWidth, zoomHeight, ray, hit) && std::abs(hit.u - .675f) < .0001f, "zoom reduces UV motion gain");
        check(!og::desktop_precision_geometry(quad, zoomWidth, zoomHeight, NAN, .5f, .5f), "nonfinite scale rejected");
        std::cout << "Desktop head geometry, freshness, manual clicks and precision tests passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
