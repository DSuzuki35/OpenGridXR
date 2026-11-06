#include "core.hpp"
#include "settings.hpp"
#include "stabilization.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <windows.h>
void check(bool x, const char *message) {
    if (!x)
        throw std::runtime_error(message);
}
int main() {
    try {
        og::Config c;
        c.enabled = true;
        c.crop = {10, 20, 5, 15};
        auto p = og::adjust_projection({-1, 1, 1, -1}, c, true);
        check(std::abs(p.left + .8) < 1e-8 && std::abs(p.right - .6) < 1e-8,
              "crop uses tangent-space percentages");
        auto f = og::dimension_factors(c, true);
        check(std::abs(f[0] - .7) < 1e-8 && std::abs(f[1] - .8) < 1e-8,
              "pixel recommendation preserves density");
        auto fixed = og::adjust_projection({-1, 1, 1, -1}, c, false);
        check(fixed.left == -1 && fixed.right == 1, "immutable FOV passes through");
        c.renderScale = 150;
        f = og::dimension_factors(c, false);
        check(f[0] == 1.5 && f[1] == 1.5, "immutable FOV permits independent render scale");
        c.streaming = true;
        c.extraLeft = 20;
        c.extraRight = 10;
        auto wide = og::adjust_projection({-1, 1, 1, -1}, c, true);
        check(wide.left == -1.4 && wide.right == 1.2,
              "stream expansion preserves whole source rectangle");
        check(og::game_from_executable(L"C:\\Games\\iRacingSim64DX11.exe") == "iracing",
              "detect iRacing executable");
        check(og::game_from_executable(L"Le Mans Ultimate.exe") == "lmu", "detect LMU executable");
        check(og::game_from_executable(L"unrelated.exe").empty(), "unknown processes pass through");
        std::vector<og::Point> contour{{.2f, .2f}, {.8f, .2f}, {.8f, .8f}, {.2f, .8f}};
        check(og::valid_polygon(contour), "valid aperture");
        check(!og::valid_polygon({{0, 0}, {1, 1}, {1, 0}, {0, 1}}), "crossed aperture rejected");
        auto mesh = og::hidden_mesh(contour, 0, {-1, 1, 1, -1});
        check(!mesh.indices.empty(), "aperture builds exterior mask");
        double area = 0;
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            auto a = mesh.vertices[mesh.indices[i]], b = mesh.vertices[mesh.indices[i + 1]],
                 d = mesh.vertices[mesh.indices[i + 2]];
            double cross = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
            check(cross > 0, "mask triangles have required CCW winding");
            area += cross / 2;
            double x = (a.x + b.x + d.x) / 3, y = (a.y + b.y + d.y) / 3;
            check(!(x > -.6 && x < .6 && y > -.6 && y < .6), "mask never hides aperture interior");
        }
        check(area > 2.3 && area < 2.6, "mask covers conservative exterior");
        auto padded = og::hidden_mesh(contour, 10, {-1, 1, 1, -1});
        double pa = 0;
        for (size_t i = 0; i < padded.indices.size(); i += 3) {
            auto a = padded.vertices[padded.indices[i]], b = padded.vertices[padded.indices[i + 1]],
                 d = padded.vertices[padded.indices[i + 2]];
            pa += ((b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x)) / 2;
        }
        check(pa < area, "padding expands aperture");
        auto correction = vrst::horizon_correction(vrst::axis_angle({0, 0, 1}, .2),
                                                   vrst::axis_angle({0, 0, 1}, .2), 100);
        check(vrst::angle_between(correction, vrst::axis_angle({0, 0, 1}, -.2)) < 1e-6,
              "horizon core levels roll");
        vrst::Settings s;
        s.automatic_fov = true;
        s.stabilization_crop = 0;
        s.edge_reserve = {true, 10, 10, 10, 10};
        auto w =
            vrst::make_warp(2000, 2000, s, vrst::axis_angle({0, 1, 0}, .5), vrst::Projection{});
        check(vrst::corners_inside(w), "stabilization respects available render reserves");
        og::UprightReference upright;
        auto inverted = vrst::axis_angle({0, 0, 1}, vrst::pi);
        check(vrst::angle_between(upright.correction(inverted, inverted, 100), {}) < 1e-6,
              "negative Y layer convention does not tilt an upright headset");
        auto tilted = vrst::multiply(inverted, vrst::axis_angle({0, 0, 1}, .2));
        check(vrst::angle_between(upright.correction(tilted, tilted, 100),
                                  vrst::axis_angle({0, 0, 1}, -.2)) < 1e-6,
              "negative Y reference levels subsequent roll correctly");
        upright.reset();
        check(vrst::angle_between(upright.correction({}, {}, 100), {}) < 1e-6,
              "upright reference resets for a new layer space");
        const auto smallRoll = vrst::axis_angle({0, 0, 1}, 5 * vrst::pi / 180);
        const auto adaptive = upright.correction(smallRoll, smallRoll, 50, true, 0);
        const auto linear = upright.correction(smallRoll, smallRoll, 50);
        check(vrst::angle_between({}, adaptive) > vrst::angle_between({}, linear) * 1.7,
              "adaptive leveling suppresses small roll more strongly than linear leveling");
        check(vrst::angle_between(upright.correction(smallRoll, smallRoll, 0, true, 0), {}) < 1e-6,
              "adaptive leveling respects zero horizon strength");
        check(vrst::angle_between(upright.correction(smallRoll, smallRoll, 100, true, 0),
                                  vrst::axis_angle({0, 0, 1}, -5 * vrst::pi / 180)) < 1e-6,
              "adaptive leveling fully levels at maximum strength");
        upright.reset();
        upright.correction({}, {}, 0, false, 100);
        const auto pitch = vrst::axis_angle({1, 0, 0}, 15 * vrst::pi / 180);
        const auto held = upright.correction(pitch, pitch, 0, false, 100);
        check(vrst::angle_between(held, vrst::axis_angle({1, 0, 0}, -7.5 * vrst::pi / 180)) < 1e-6,
              "pitch lock dampens small movements with a bounded correction");
        check(vrst::angle_between(held, upright.correction(pitch, pitch, 0, false, 100)) < 1e-6,
              "repeated frame draws cannot drift the pitch anchor");
        upright.reset();
        check(vrst::angle_between(upright.correction(pitch, pitch, 0, false, 100), {}) < 1e-6,
              "recenter establishes a new pitch anchor");
        upright.reset();
        auto vertical = vrst::axis_angle({1, 0, 0}, vrst::pi / 2);
        check(vrst::angle_between(upright.correction(vertical, vertical, 100, true, 100), {}) < 1e-6,
              "vertical startup defers ambiguous stabilization references");
        check(vrst::angle_between(upright.correction(pitch, pitch, 0, false, 100), {}) < 1e-6,
              "pitch anchor initializes on first nonvertical view");
        upright.reset();
        upright.correction(inverted, inverted, 50, true, 0);
        check(vrst::angle_between(upright.correction(vrst::multiply(inverted, smallRoll),
                                  vrst::multiply(inverted, smallRoll), 50, true, 0), adaptive) < 1e-6,
              "adaptive leveling respects negative Y layer convention");
        vrst::Warp budget;
        const auto turn = vrst::axis_angle({0, 1, 0}, .5);
        og::limit_stream_correction(budget, turn, true, 10);
        check(budget.correction_fraction > 0 && budget.correction_fraction < 1,
              "finite mirror allowance limits large corrections");
        for (double u : {0., 1.}) for (double v : {0., 1.}) {
            auto uv = vrst::map_uv(budget, u, v);
            check(uv[0] >= -.100001 && uv[0] <= 1.100001 &&
                      uv[1] >= -.100001 && uv[1] <= 1.100001,
                  "final corrected corners fit the finite mirrored border");
        }
        const double bounded = budget.correction_fraction;
        og::limit_stream_correction(budget, turn, true, -1);
        check(budget.correction_fraction > bounded, "omitted mirror budget preserves legacy allowance");
        og::limit_stream_correction(budget, turn, true, 0);
        check(vrst::corners_inside(budget), "zero mirror allowance keeps corners on real image");
        const auto ini = std::filesystem::temp_directory_path() /
                         ("OpenGridXR-stream-settings-" + std::to_string(GetCurrentProcessId()) + ".ini");
        { std::ofstream out(ini); out << "[iracing]\nMode=streaming\nMirror=1\n"; }
        auto parsed = og::read_config(ini.wstring(), "iracing");
        check(!parsed.lmu.cpuCacheAffinity&&!parsed.lmu.cpuUploadReuse&&!parsed.lmu.cpuUploadBatch&&!parsed.lmu.cpuOuterReuse&&!parsed.lmu.outerSsr,"absent LMU settings leave CPU affinity and experiments off");
        { std::ofstream out(ini);out << "[lmu]\nLmuMode=quad\n"; }
        check(!og::read_config(ini.wstring(),"lmu").lmu.cpuCacheAffinity,"LMU INI without affinity preference stays opt-in");
        { std::ofstream out(ini);out << "[lmu]\nLmuCpuCacheAffinity=1\n"; }
        check(og::read_config(ini.wstring(),"lmu").lmu.cpuCacheAffinity,"explicit LMU affinity opt-in is preserved");
        { std::ofstream out(ini);out << "[lmu]\nLmuMode=quad\nLmuCpuUploadReuse=1\nLmuCpuUploadBatch=1\nLmuCpuOuterReuse=1\nLmuOuterSsr=1\nLmuCpuCacheAffinity=0\nLmuSmoothing=3\n"; }
        const auto lmuConfig=og::read_config(ini.wstring(),"lmu");
        check(lmuConfig.lmu.cpuUploadReuse&&lmuConfig.lmu.cpuUploadBatch&&lmuConfig.lmu.cpuOuterReuse&&lmuConfig.lmu.outerSsr&&!lmuConfig.lmu.cpuCacheAffinity,"LMU optimization and local CPU preference parse independently");
        check(parsed.mirrorLimit == -1 && !parsed.adaptiveHorizon && parsed.pitchLock == 0,
              "old INI keeps unlimited mirror and disabled new stabilization");
        { std::ofstream out(ini); out << "[iracing]\nMirror=1\nMirrorLimit=-1\n"; }
        parsed = og::read_config(ini.wstring(), "iracing");
        check(parsed.mirrorLimit == -1,
              "serialized legacy mirror sentinel preserves unlimited reflection");
        for (const char *invalidLimit : {"-2", "-0.5"}) {
            { std::ofstream out(ini); out << "[iracing]\nMirrorLimit=" << invalidLimit << "\n"; }
            parsed = og::read_config(ini.wstring(), "iracing");
            check(parsed.mirrorLimit == 0,
                  "negative mirror budgets other than the legacy sentinel clamp to zero");
        }
        { std::ofstream out(ini); out << "[iracing]\nAdaptiveHorizon=1\nPitchLock=150\n"
                                       "MirrorLimit=70\nMirrorSoftness=nan\nMirrorStretch=65\n"; }
        parsed = og::read_config(ini.wstring(), "iracing");
        std::filesystem::remove(ini);
        check(parsed.adaptiveHorizon && parsed.pitchLock == 100 && parsed.mirrorLimit == 50 &&
                  parsed.mirrorSoftness == 0 && parsed.mirrorStretch == 65,
              "stream settings parse, clamp, and reject nonfinite values");
        std::cout << "Native geometry, process selection, mask winding, and "
                     "stabilization passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
