#include "depthxr/config_parser.h"
#include "depthxr/config_path.h"
#include "depthxr/osd_renderer.h"
#include "depthxr/settings_resolver.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>
void check(bool x, const char *m) {
    if (!x)
        throw std::runtime_error(m);
}
int main() {
    try {
        _wputenv_s(L"VECTORXR_CONFIG_PATH", L"C:\\must-not-read-vectorxr.json");
        _wputenv_s(L"OPENGRIDXR_ENHANCEMENTS_CONFIG", L"");
        auto normal = depthxr::ResolveConfigPath();
        check(normal.filename() == L"OpenGridXR-enhancements-disabled.json",
              "unknown executable has disabled OpenGrid config path");
        _wputenv_s(L"OPENGRIDXR_ENHANCEMENTS_CONFIG", L"C:\\fixture\\own.json");
        check(depthxr::ResolveConfigPath() == L"C:\\fixture\\own.json",
              "own fixture override resolves");
        _wputenv_s(L"OPENGRIDXR_ENHANCEMENTS_CONFIG", L"");
        check(depthxr::ResolveLogPath().wstring().find(L"VectorXR") == std::wstring::npos,
              "log path never uses VectorXR directory");
        auto parsed = depthxr::LoadConfigFromFile(std::filesystem::path(__FILE__).parent_path() /
                                                  "opengrid-config-fixture.json");
        check(parsed.ok, parsed.error.c_str());
        auto runtime = depthxr::ResolveRuntimeConfig(parsed.document, "iRacingSim64DX11.exe");
        check(!runtime.depthxr.enabled && !runtime.pivotxr.enabled && !runtime.quadviews.enabled &&
                  !runtime.turbo.enabled,
              "unrelated modules forced off in native engine");
        check(runtime.framegen.enabled && runtime.core.osd.enabled,
              "frame generation and OSD remain available");
        check(runtime.core.osd.toolkit_summary == "Configured: driving | SS 100% | Crop 0/0/0/0%",
              "desktop toolkit summary parsed");
        const auto parse_osd = [](const std::string& osd) {
            return depthxr::ParseConfig(R"({"version":3,"core":{"osd":)" + osd +
                R"(},"applications":[],"modules":{"depthxr":{"enabled":false,"defaults":{"stereoBoost":1,"convergence":0},"bindings":{"toggleEnabled":{"type":"none"}},"profiles":[]},"pivotxr":{"enabled":false,"defaults":{},"profiles":[]}}})");
        };
        const auto resolution_config = parse_osd(R"({"showResolution":true,"configuredRenderScale":135})");
        check(resolution_config.ok, resolution_config.error.c_str());
        check(resolution_config.document.core.osd.show_resolution &&
                  resolution_config.document.core.osd.configured_render_scale == 135,
              "render resolution toggle and configured percent are parsed independently");
        check(!parse_osd(R"({"showResolution":"yes"})").ok &&
              !parse_osd(R"({"configuredRenderScale":999})").ok,
              "render resolution settings reject malformed types and unsupported scale");
        parsed.document.depthxr.enabled = true;
        parsed.document.pivotxr.enabled = true;
        parsed.document.quadviews.enabled = true;
        parsed.document.turbo.enabled = true;
        runtime = depthxr::ResolveRuntimeConfig(parsed.document, "iRacingSim64DX11.exe");
        check(!runtime.depthxr.enabled && !runtime.pivotxr.enabled && !runtime.quadviews.enabled &&
                  !runtime.turbo.enabled,
              "unrelated settings cannot reenable native modules");
        depthxr::OsdTelemetry telemetry;
        depthxr::OsdSnapshot snapshot;
        snapshot.modules = runtime.core.osd.toolkit_summary;
        auto bitmap = depthxr::RasterizeOsd(runtime.core.osd, false, snapshot, telemetry, false);
        check(bitmap.Valid() && bitmap.height <= 768, "toolkit summary renders inside OSD texture");
        std::ifstream fixture(std::filesystem::path(__FILE__).parent_path() /
                              "opengrid-config-fixture.json");
        std::string source((std::istreambuf_iterator<char>(fixture)), {});
        size_t at = 0;
        const std::string disabled = "\"enabled\": false";
        while ((at = source.find(disabled, at)) != std::string::npos) {
            source.replace(at, disabled.size(), "\"enabled\": true");
            at += 15;
        }
        auto forced = depthxr::ParseConfig(source);
        check(forced.ok && !forced.document.depthxr.enabled && !forced.document.pivotxr.enabled &&
                  !forced.document.quadviews.enabled && !forced.document.turbo.enabled,
              "config parser sanitizes unrelated module enable requests");
        std::cout << "OpenGrid enhancements path isolation and module policy passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
