#include "desktop_interaction.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace og {
unsigned desktop_host_wait(bool monitorMouse, bool pendingWork, bool pollUi) {
    return pendingWork ? 2 : monitorMouse ? 8 : pollUi ? 33 : 100;
}
DesktopHostSchedule desktop_host_schedule(const OverlayShared &d, bool pendingWork) {
    bool monitor = false, ui = false;
    for (const auto &s : d.slots)
        if (s.enabled && s.desktopWidth > 0 &&
            s.desktopHeight > 0) {
            monitor = monitor || s.desktopMode == 1;
            ui = ui || s.desktopMode == 2;
        }
    return {monitor, desktop_host_wait(monitor, pendingWork, ui), ui};
}
namespace {
XrQuaternionf mul(XrQuaternionf a, XrQuaternionf b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrVector3f rotate(XrQuaternionf q, XrVector3f p) {
    auto r = mul(mul(q, {p.x, p.y, p.z, 0}), {-q.x, -q.y, -q.z, q.w});
    return {r.x, r.y, r.z};
}
bool finite(XrPosef p) {
    auto q = p.orientation;
    return std::isfinite(p.position.x) && std::isfinite(p.position.y) &&
           std::isfinite(p.position.z) && std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z) && std::isfinite(q.w) &&
           std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1) < .01f;
}
} // namespace
bool desktop_hit(const XrPosef &quad, float width, float height, const XrPosef &ray,
                 DesktopHit &hit) {
    if (!finite(quad) || !finite(ray) || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0)
        return false;
    auto q = quad.orientation;
    XrQuaternionf inverse{-q.x, -q.y, -q.z, q.w};
    auto o = rotate(inverse, {ray.position.x - quad.position.x, ray.position.y - quad.position.y,
                              ray.position.z - quad.position.z});
    auto d = rotate(inverse, rotate(ray.orientation, {0, 0, -1}));
    // Only the visible front face; a ray behind the quad cannot select it.
    if (d.z >= -.00001f || o.z <= 0)
        return false;
    float t = -o.z / d.z, u = (o.x + t * d.x) / width + .5f, v = .5f - (o.y + t * d.y) / height;
    if (!std::isfinite(t) || t <= 0 || u < 0 || u > 1 || v < 0 || v > 1)
        return false;
    hit = {u, v, t};
    return true;
}
bool desktop_pixel(float u, float v, int left, int top, int width, int height, int &x, int &y) {
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1 || width < 1 ||
        height < 1 || width > 131072 || height > 131072)
        return false;
    int64_t px = int64_t(left) + std::min(width - 1, int(std::floor(u * width))),
            py = int64_t(top) + std::min(height - 1, int(std::floor(v * height)));
    if (px < INT32_MIN || px > INT32_MAX || py < INT32_MIN || py > INT32_MAX)
        return false;
    x = int(px);
    y = int(py);
    return true;
}
unsigned desktop_input_events(int x, int y, int left, int top, int width, int height,
                              const char *button, std::array<INPUT, 3> &events) {
    events = {};
    if (!button || width < 1 || height < 1 || width > 65536 || height > 65536 ||
        int64_t(x) < left || int64_t(y) < top || int64_t(x) >= int64_t(left) + width ||
        int64_t(y) >= int64_t(top) + height)
        return 0;
    bool click = *button != 0, right = std::strcmp(button, "right") == 0;
    if (click && !right && std::strcmp(button, "left") != 0)
        return 0;
    for (auto &i : events) {
        i.type = INPUT_MOUSE;
        i.mi.dwExtraInfo = 0x4f474450;
    }
    // Use pixel centers: Windows maps absolute coordinates using a 65536-wide
    // domain, rather than the 65535/(size-1) endpoint approximation.
    events[0].mi.dx = LONG(((int64_t(x) - left) * 2 + 1) * 65536 / (int64_t(width) * 2));
    events[0].mi.dy = LONG(((int64_t(y) - top) * 2 + 1) * 65536 / (int64_t(height) * 2));
    events[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    if (click) {
        events[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        events[2].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    }
    return click ? 3 : 1;
}
int desktop_target_slot(const OverlayShared &d, uint64_t now) {
    for (unsigned i = overlay_count; i-- > 0;)
        if (pointer_target_fresh(d, d.slots[i], now))
            return int(i);
    return -1;
}
bool desktop_occluded(const XrPosef &ray, const XrPosef *poses, const float *widths,
                      const float *heights, unsigned count) {
    DesktopHit hit;
    for (unsigned i = 0; i < count; ++i)
        if (desktop_hit(poses[i], widths[i], heights[i], ray, hit))
            return true;
    return false;
}
bool desktop_target_fresh(const OverlayShared &d, const OverlaySlot &s, uint64_t now) {
    return s.desktopMode == 1 && pointer_target_fresh(d, s, now);
}
bool pointer_identity_equal(const DesktopPointer &a, const DesktopPointer &b) {
    return a.session == b.session && a.focusEpoch == b.focusEpoch &&
           a.configRevision == b.configRevision && a.sourceRevision == b.sourceRevision &&
           a.sourceGeneration == b.sourceGeneration && a.recenter == b.recenter;
}
bool pointer_target_fresh(const OverlayShared &d, const OverlaySlot &s, uint64_t now) {
    const auto &p = s.pointer;
    return d.inputFocused && p.focusEpoch == d.focusEpoch && s.enabled &&
           (s.desktopMode == 1 || s.desktopMode == 2) &&
           p.source == 1 && s.desktopWidth > 0 && s.desktopHeight > 0 && p.valid &&
           d.consumerPid && now >= d.consumerBeat && now - d.consumerBeat <= 200 && now >= p.tick &&
           now - p.tick <= 200 && p.session == d.session && p.configRevision == s.revision &&
           p.sourceRevision == s.sourceRevision && p.recenter == s.recenter &&
           p.sourceGeneration == s.frames[s.current < 2 ? s.current : 0].generation &&
           p.sourceGeneration && std::isfinite(p.u) && std::isfinite(p.v) && p.u >= 0 && p.u <= 1 &&
           p.v >= 0 && p.v <= 1;
}
bool DesktopPrecision::request(uint64_t now, bool held, uint64_t id, bool eligible) {
    if (!id || id < pressId_)
        return false;
    if (id > pressId_) {
        pressId_ = id;
        until_ = held && eligible ? now + 300 : 0;
    } else if (!held)
        until_ = 0;
    else if (until_ && now <= until_ && eligible)
        until_ = now + 300;
    return until_ != 0;
}
bool DesktopPrecision::update(uint64_t now, bool keyboard, bool eligible) {
    if (keyboard && !keyDown_) keyHeld_ = eligible;
    keyDown_ = keyboard;
    if (!keyboard) keyHeld_ = false;
    if (!eligible) cancel();
    if (until_ && now > until_) until_ = 0;
    return eligible && (until_ || keyHeld_);
}
bool desktop_shortcut(const std::string &text, DesktopShortcut &out) {
    out = {};
    if (text.empty())
        return true;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('+', start);
        auto token = text.substr(start, end - start);
        unsigned bit = token == "Ctrl" ? 1 : token == "Alt" ? 2 : token == "Shift" ? 4 : 0;
        if (bit) {
            if (out.modifiers & bit || out.key)
                return false;
            out.modifiers |= bit;
        } else {
            if (out.key || token.size() < 2 || token[0] != 'F') return false;
            unsigned n = 0;
            for (size_t i = 1; i < token.size(); ++i) {
                if (token[i] < '0' || token[i] > '9')
                    return false;
                n = n * 10 + token[i] - '0';
            }
            if (n < 1 || n > 12 || token != "F" + std::to_string(n)) return false;
            out.key = VK_F1 + n - 1;
        }
        if (end == std::string::npos) break;
        start = end + 1;
        if (start == text.size())
            return false;
    }
    return out.key != 0;
}
bool desktop_precision_geometry(XrPosef &pose, float &width, float &height, float scale, float u, float v) {
    if (!finite(pose) || !std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
        height <= 0 || !std::isfinite(scale) || scale < 1 || scale > 3 || !std::isfinite(u) ||
        !std::isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1)
        return false;
    auto offset = rotate(pose.orientation, {(1-scale)*(u-.5f)*width, (1-scale)*(.5f-v)*height, 0});
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    width *= scale;
    height *= scale;
    return finite(pose) && std::isfinite(width) && std::isfinite(height);
}
} // namespace og
