#include "settings.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <sstream>
#include <windows.h>
namespace og {
namespace {
double cross(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool segments(Point a, Point b, Point c, Point d) {
    return cross(a, b, c) * cross(a, b, d) <= 0 && cross(c, d, a) * cross(c, d, b) <= 0 &&
           std::max(a.x, b.x) >= std::min(c.x, d.x) && std::max(c.x, d.x) >= std::min(a.x, b.x) &&
           std::max(a.y, b.y) >= std::min(c.y, d.y) && std::max(c.y, d.y) >= std::min(a.y, b.y);
}
bool inside(Point p, const std::vector<Point> &v) {
    bool in = false;
    for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++)
        if ((v[i].y > p.y) != (v[j].y > p.y) &&
            p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x)
            in = !in;
    return in;
}
} // namespace
bool valid_polygon(const std::vector<Point> &v) {
    if (v.size() < 3 || v.size() > 64)
        return false;
    double area = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        auto a = v[i], b = v[(i + 1) % v.size()];
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || a.x < 0 || a.y < 0 || a.x > 1 ||
            a.y > 1 || std::hypot(a.x - b.x, a.y - b.y) < 1e-5)
            return false;
        area += a.x * b.y - b.x * a.y;
        for (size_t j = i + 1; j < v.size(); ++j) {
            if (j == (i + 1) % v.size() || i == (j + 1) % v.size())
                continue;
            if (segments(a, b, v[j], v[(j + 1) % v.size()]))
                return false;
        }
    }
    return std::abs(area) >= .08;
}
Config read_config(const std::wstring &path, const std::string &game) {
    Config c;
    if (game != "iracing" && game != "lmu")
        return c;
    std::wstring section(game.begin(), game.end());
    auto str = [&](const wchar_t *k) {
        wchar_t b[8192]{};
        GetPrivateProfileStringW(section.c_str(), k, L"", b, 8192, path.c_str());
        return std::wstring(b);
    };
    auto number = [&](const wchar_t *k, double def, double lo, double hi) {
        auto t = str(k);
        if (t.empty())
            return def;
        wchar_t *end{};
        double n = wcstod(t.c_str(), &end);
        return end == t.c_str() + t.size() && std::isfinite(n) ? std::clamp(n, lo, hi) : def;
    };
    auto mode = str(L"Mode");
    auto sharpening = str(L"SharpeningMode");
    c.sharpening.mode = sharpening == L"luma"  ? SharpeningMode::Luma
                        : sharpening == L"cas" ? SharpeningMode::Cas
                                               : SharpeningMode::Off;
    c.sharpening.strength = float(number(L"SharpeningStrength", 50, 0, 100));
    c.sharpening.clamp = float(number(L"LumaClamp", .035, 0, .1));
    c.sharpening.radius = float(number(L"LumaRadius", 1, .1, 6));
    c.sharpening.pattern = unsigned(number(L"LumaPattern", 2, 1, 4));
    c.enhancements = number(L"EnhancementsEnabled", 0, 0, 1) == 1;
    c.enabled = number(L"Enabled", 1, 0, 1) == 1 &&
                (mode == L"driving" || mode == L"streaming" || mode.empty());
    c.streaming = mode == L"streaming";
    c.mask = number(L"MaskEnabled", 0, 0, 1) == 1 && !c.streaming;
    c.padding = number(L"MaskPadding", 0, 0, 10);
    c.renderScale = number(L"RenderScale", 100, 50, 200);
    c.crop = {number(L"CropLeft", 0, 0, 40), number(L"CropRight", 0, 0, 40),
              number(L"CropTop", 0, 0, 40), number(L"CropBottom", 0, 0, 40)};
    if (c.crop.left + c.crop.right > 70 || c.crop.top + c.crop.bottom > 70)
        c.crop = {};
    c.reserve = {number(L"ReserveLeft", 0, 0, 30), number(L"ReserveRight", 0, 0, 30),
                 number(L"ReserveTop", 0, 0, 30), number(L"ReserveBottom", 0, 0, 30)};
    c.extraLeft = number(L"ExtraLeft", 0, 0, 30);
    c.extraRight = number(L"ExtraRight", 0, 0, 30);
    c.eye = unsigned(number(L"StreamEye", 0, 0, 1));
    c.smoothing = number(L"Smoothing", 0, 0, 100);
    c.horizon = number(L"HorizonLock", 0, 0, 100);
    c.offsetX = number(L"OffsetX", 0, -100, 100);
    c.offsetY = number(L"OffsetY", 0, -100, 100);
    c.width = unsigned(number(L"OutputWidth", 1920, 640, 7680));
    c.height = unsigned(number(L"OutputHeight", 1080, 360, 4320));
    c.fps = unsigned(number(L"OutputFps", 60, 30, 120));
    c.mirror = number(L"Mirror", 0, 0, 1) == 1;
    for (unsigned i = 0; i < 2; ++i) {
        std::wistringstream parts(str(i ? L"MaskRight" : L"MaskLeft"));
        std::wstring part;
        while (std::getline(parts, part, L';')) {
            std::wistringstream item(part);
            Point p;
            wchar_t sep;
            if (!(item >> p.x >> sep >> p.y) || sep != L',') {
                c.contours[i].clear();
                break;
            }
            item >> std::ws;
            if (!item.eof()) {
                c.contours[i].clear();
                break;
            }
            c.contours[i].push_back(p);
        }
        if (!valid_polygon(c.contours[i]))
            c.contours[i].clear();
    }
    return c;
}
std::string game_from_executable(std::wstring name) {
    auto slash = name.find_last_of(L"/\\");
    if (slash != std::wstring::npos)
        name = name.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(), towlower);
    if (name == L"iracingsim64dx11.exe" || name == L"iracingsim64dx11_test.exe")
        return "iracing";
    if (name == L"le mans ultimate.exe" || name == L"lemansultimate.exe")
        return "lmu";
    return {};
}
Projection adjust_projection(Projection p, const Config &c, bool mutableFov) {
    if (!c.enabled || !mutableFov)
        return p;
    double w = p.right - p.left, h = p.up - p.down;
    if (c.streaming) {
        p.left -= w * c.extraLeft / 100;
        p.right += w * c.extraRight / 100;
    } else {
        p.left += w * c.crop.left / 100;
        p.right -= w * c.crop.right / 100;
        p.up -= h * c.crop.top / 100;
        p.down += h * c.crop.bottom / 100;
    }
    return p;
}
std::array<double, 2> dimension_factors(const Config &c, bool mutableFov) {
    if (!c.enabled)
        return {1, 1};
    double x = 1, y = 1;
    if (mutableFov) {
        if (c.streaming)
            x += (c.extraLeft + c.extraRight) / 100;
        else {
            x -= (c.crop.left + c.crop.right) / 100;
            y -= (c.crop.top + c.crop.bottom) / 100;
        }
    }
    return {x * c.renderScale / 100, y * c.renderScale / 100};
}
Mesh hidden_mesh(const std::vector<Point> &v, double padding, Projection p) {
    Mesh m;
    if (!valid_polygon(v))
        return m;
    constexpr int n = 128;
    float pad = float(std::clamp(padding, 0., 10.) / 100);
    // Conservative cell classification: any cell touching the aperture or its
    // padded boundary is retained. Merge runs to keep the static mesh small.
    auto hidden = [&](int x, int y) {
        float l = float(x) / n - pad, r = float(x + 1) / n + pad, t = float(y) / n - pad,
              b = float(y + 1) / n + pad;
        Point corners[4]{{l, t}, {r, t}, {r, b}, {l, b}};
        if (inside({(l + r) / 2, (t + b) / 2}, v))
            return false;
        for (auto q : corners)
            if (inside(q, v))
                return false;
        for (size_t i = 0; i < v.size(); ++i) {
            auto a = v[i], z = v[(i + 1) % v.size()];
            if (a.x >= l && a.x <= r && a.y >= t && a.y <= b)
                return false;
            for (int k = 0; k < 4; ++k)
                if (segments(a, z, corners[k], corners[(k + 1) % 4]))
                    return false;
        }
        return true;
    };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n;) {
            if (!hidden(x, y)) {
                ++x;
                continue;
            }
            int start = x;
            while (x < n && hidden(x, y))
                ++x;
            float l = float(p.left + (p.right - p.left) * start / n),
                  r = float(p.left + (p.right - p.left) * x / n),
                  top = float(p.up + (p.down - p.up) * y / n),
                  bottom = float(p.up + (p.down - p.up) * (y + 1) / n);
            auto i = uint32_t(m.vertices.size());
            m.vertices.insert(m.vertices.end(), {{l, bottom}, {r, bottom}, {r, top}, {l, top}});
            m.indices.insert(m.indices.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
        }
    return m;
}
} // namespace og
