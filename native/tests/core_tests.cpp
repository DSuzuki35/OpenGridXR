#include "core.hpp"
#include "settings.hpp"
#include "stabilization.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
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
        std::cout << "Native geometry, process selection, mask winding, and "
                     "stabilization passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
