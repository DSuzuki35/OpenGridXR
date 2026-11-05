// Standalone capability probe. No process attachment, session, GPU work, or game patch.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_NO_PROTOTYPES
#include <windows.h>
#include "openxr/openxr.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Fields = std::vector<std::pair<std::string, std::string>>;
std::string quote(const std::string& value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + '"';
}
std::string array(const std::vector<std::string>& values) {
    std::string out = "[";
    for (const auto& v : values) { if (out.size() > 1) out += ','; out += v; }
    return out + ']';
}
std::string object(const Fields& fields) {
    std::string out = "{";
    for (const auto& [key, value] : fields) {
        if (out.size() > 1) out += ',';
        out += quote(key) + ':' + value;
    }
    return out + '}';
}
std::string boolean(bool v) { return v ? "true" : "false"; }
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                               int(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid UTF-16 path");
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()),
                        result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring environment(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return {};
    std::wstring value(n, L'\0');
    DWORD size = GetEnvironmentVariableW(name, value.data(), n);
    if (size >= n) throw std::runtime_error("Environment changed during probe");
    value.resize(size);
    return value;
}
std::wstring active_runtime() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0,
                     KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value), type = 0;
    LSTATUS result = RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type,
                                    reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t)) return {};
    value[32767] = 0;
    return value;
}
void check(XrResult result, const char* operation) {
    if (XR_FAILED(result))
        throw std::runtime_error(std::string(operation) + " failed: XrResult " + std::to_string(result));
}
template<class T, class Call, class Init>
std::vector<T> enumerate(Call call, Init init, const char* name) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t count = 0;
        check(call(0, &count, static_cast<T*>(nullptr)), name);
        if (count > 65536) throw std::runtime_error("Unreasonable enumeration count");
        std::vector<T> values(count);
        if (!count) return values;
        for (uint32_t i = 0; i < count; ++i) init(values[i], i, count);
        uint32_t written = count;
        XrResult result = call(count, &written, values.data());
        if (result == XR_ERROR_SIZE_INSUFFICIENT) continue;
        check(result, name);
        if (written > count) throw std::runtime_error("Enumeration returned an invalid count");
        values.resize(written);
        return values;
    }
    throw std::runtime_error(std::string(name) + " count kept changing");
}
struct Module {
    HMODULE handle;
    explicit Module(const std::wstring& path) {
        handle = LoadLibraryExW(path.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle) throw std::runtime_error("LoadLibraryExW failed: " + std::to_string(GetLastError()));
    }
    ~Module() { if (handle) FreeLibrary(handle); }
};
struct Api {
    PFN_xrGetInstanceProcAddr get;
    explicit Api(HMODULE module) {
        auto raw = GetProcAddress(module, "xrGetInstanceProcAddr");
        static_assert(sizeof(get) == sizeof(raw));
        std::memcpy(&get, &raw, sizeof(get));
        if (!get) throw std::runtime_error("Loader has no xrGetInstanceProcAddr");
    }
    template<class T> T load(XrInstance instance, const char* name) const {
        PFN_xrVoidFunction result = nullptr;
        check(get(instance, name, &result), name);
        if (!result) throw std::runtime_error(std::string("Null function: ") + name);
        return reinterpret_cast<T>(result);
    }
};
struct Instance {
    XrInstance handle = XR_NULL_HANDLE;
    PFN_xrDestroyInstance destroy = nullptr;
    ~Instance() { if (handle && destroy) destroy(handle); }
};
std::string version(XrVersion v) {
    return std::to_string(XR_VERSION_MAJOR(v)) + '.' + std::to_string(XR_VERSION_MINOR(v))
        + '.' + std::to_string(XR_VERSION_PATCH(v));
}
void self_test() {
    int phase = 0;
    auto values = enumerate<int>([&](uint32_t cap, uint32_t* count, int* out) {
        if (!cap) { *count = phase ? 4 : 2; return XR_SUCCESS; }
        if (!phase++) { *count = 4; return XR_ERROR_SIZE_INSUFFICIENT; }
        if (cap != 4 || out[3] != 13) throw std::runtime_error("Initialization/retry failed");
        *count = 4;
        return XR_SUCCESS;
    }, [](int& v, uint32_t i, uint32_t) { v = int(i) + 10; }, "test enumeration");
    if (values.size() != 4 || values[2] != 12) throw std::runtime_error("Retry test failed");
    bool rejected = false;
    try {
        enumerate<int>([](uint32_t cap, uint32_t* count, int*) {
            *count = cap ? cap + 1 : 2; return XR_SUCCESS;
        }, [](int&, uint32_t, uint32_t) {}, "invalid count");
    } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Invalid count accepted");
    if (quote("a\n\"\\") != "\"a\\u000a\\\"\\\\\"") throw std::runtime_error("JSON escape test failed");
    std::cout << "{\"self_test\":\"passed\",\"cases\":3}\n";
}
void probe(const std::wstring& loader, Fields& report) {
    report.emplace_back("registered_runtime", quote(utf8(active_runtime())));
    Fields env;
    for (const auto* name : {L"XR_RUNTIME_JSON", L"XR_API_LAYER_PATH", L"XR_ENABLE_API_LAYERS"})
        env.emplace_back(utf8(name), quote(utf8(environment(name))));
    report.emplace_back("environment", object(env));
    Module module(loader);
    Api api(module.handle);
    auto getLayers = api.load<PFN_xrEnumerateApiLayerProperties>(XR_NULL_HANDLE, "xrEnumerateApiLayerProperties");
    auto layers = enumerate<XrApiLayerProperties>(getLayers,
        [](auto& v, uint32_t, uint32_t) { v.type = XR_TYPE_API_LAYER_PROPERTIES; }, "enumerate layers");
    std::vector<std::string> layerNames;
    for (const auto& layer : layers) layerNames.push_back(quote(layer.layerName));
    report.emplace_back("available_api_layers_not_proof_of_activation", array(layerNames));
    auto getExtensions = api.load<PFN_xrEnumerateInstanceExtensionProperties>(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties");
    auto extensions = enumerate<XrExtensionProperties>(
        [&](uint32_t cap, uint32_t* count, auto* out) { return getExtensions(nullptr, cap, count, out); },
        [](auto& v, uint32_t, uint32_t) { v.type = XR_TYPE_EXTENSION_PROPERTIES; }, "enumerate extensions");
    std::vector<std::string> names;
    for (const auto& e : extensions) names.push_back(quote(e.extensionName));
    report.emplace_back("advertised_extensions", array(names));
    auto has = [&](const char* name) {
        return std::any_of(extensions.begin(), extensions.end(), [&](auto& e) { return !std::strcmp(e.extensionName, name); });
    };
    bool quad = has(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
    bool foveated = quad && has(XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME);
    std::vector<const char*> enabled;
    if (has("XR_KHR_D3D11_enable")) enabled.push_back("XR_KHR_D3D11_enable");
    if (quad) enabled.push_back(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
    if (foveated) enabled.push_back(XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME);
    names.clear();
    for (auto name : enabled) names.push_back(quote(name));
    report.emplace_back("requested_extensions", array(names));
    XrInstanceCreateInfo info{};
    info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    std::strcpy(info.applicationInfo.applicationName, "LMU QuadView Probe");
    std::strcpy(info.applicationInfo.engineName, "Standalone diagnostic");
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = uint32_t(enabled.size());
    info.enabledExtensionNames = enabled.data();
    auto create = api.load<PFN_xrCreateInstance>(XR_NULL_HANDLE, "xrCreateInstance");
    Instance instance;
    check(create(&info, &instance.handle), "xrCreateInstance");
    instance.destroy = api.load<PFN_xrDestroyInstance>(instance.handle, "xrDestroyInstance");
    auto getInstanceProperties = api.load<PFN_xrGetInstanceProperties>(instance.handle, "xrGetInstanceProperties");
    XrInstanceProperties ip{};
    ip.type = XR_TYPE_INSTANCE_PROPERTIES;
    check(getInstanceProperties(instance.handle, &ip), "xrGetInstanceProperties");
    report.emplace_back("runtime", quote(ip.runtimeName));
    report.emplace_back("runtime_version", quote(version(ip.runtimeVersion)));
    auto getSystem = api.load<PFN_xrGetSystem>(instance.handle, "xrGetSystem");
    XrSystemGetInfo si{};
    si.type = XR_TYPE_SYSTEM_GET_INFO;
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrResult result = getSystem(instance.handle, &si, &system);
    report.emplace_back("get_system_result", std::to_string(result));
    if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
        report.emplace_back("status", quote("headset_unavailable"));
        return;
    }
    check(result, "xrGetSystem");
    XrSystemFoveatedRenderingPropertiesVARJO fp{};
    fp.type = XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO;
    XrSystemProperties sp{};
    sp.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (foveated) sp.next = &fp;
    auto getSystemProperties = api.load<PFN_xrGetSystemProperties>(instance.handle, "xrGetSystemProperties");
    check(getSystemProperties(instance.handle, system, &sp), "xrGetSystemProperties");
    report.emplace_back("system_name", quote(sp.systemName));
    report.emplace_back("supports_foveated_rendering", foveated ? boolean(fp.supportsFoveatedRendering == XR_TRUE) : "null");
    auto getConfigs = api.load<PFN_xrEnumerateViewConfigurations>(instance.handle, "xrEnumerateViewConfigurations");
    auto configs = enumerate<XrViewConfigurationType>(
        [&](uint32_t cap, uint32_t* count, auto* out) { return getConfigs(instance.handle, system, cap, count, out); },
        [](auto&, uint32_t, uint32_t) {}, "enumerate configurations");
    auto getViews = api.load<PFN_xrEnumerateViewConfigurationViews>(instance.handle, "xrEnumerateViewConfigurationViews");
    std::vector<std::string> configJson;
    bool quadEnumerated = false;
    for (auto config : configs) {
        bool isQuad = config == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
        for (int mode = 0; mode < (isQuad && foveated && fp.supportsFoveatedRendering ? 2 : 1); ++mode) {
            std::vector<XrFoveatedViewConfigurationViewVARJO> chains;
            auto views = enumerate<XrViewConfigurationView>(
                [&](uint32_t cap, uint32_t* count, auto* out) {
                    return getViews(instance.handle, system, config, cap, count, out);
                }, [&](auto& v, uint32_t index, uint32_t count) {
                    v.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                    if (mode) {
                        if (!index) chains.assign(count, {});
                        chains[index].type = XR_TYPE_FOVEATED_VIEW_CONFIGURATION_VIEW_VARJO;
                        chains[index].foveatedRenderingActive = XR_TRUE;
                        v.next = &chains[index];
                    }
                }, "enumerate views");
            std::vector<std::string> viewJson;
            for (uint32_t i = 0; i < views.size(); ++i) {
                const auto& v = views[i];
                Fields fields{{"index", std::to_string(i)},
                    {"recommended_width", std::to_string(v.recommendedImageRectWidth)},
                    {"recommended_height", std::to_string(v.recommendedImageRectHeight)},
                    {"max_width", std::to_string(v.maxImageRectWidth)},
                    {"max_height", std::to_string(v.maxImageRectHeight)},
                    {"recommended_samples", std::to_string(v.recommendedSwapchainSampleCount)},
                    {"max_samples", std::to_string(v.maxSwapchainSampleCount)}};
                if (isQuad && i < 4) {
                    fields.emplace_back("eye", quote(i % 2 ? "right" : "left"));
                    fields.emplace_back("region", quote(i < 2 ? "outer" : "inner"));
                }
                viewJson.push_back(object(fields));
            }
            if (isQuad && views.size() == 4) quadEnumerated = true;
            configJson.push_back(object({{"type", std::to_string(config)},
                {"foveated_size_query", boolean(mode != 0)}, {"views", array(viewJson)}}));
        }
    }
    report.emplace_back("view_configurations", array(configJson));
    report.emplace_back("four_quad_views_enumerated", boolean(quadEnumerated));
    report.emplace_back("status", quote("capability_query_complete"));
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring loader = L"C:\\Program Files\\Pimax\\PVRHome\\Engine\\Binaries\\ThirdParty\\OpenXR\\win64\\openxr_loader.dll";
    std::filesystem::path output;
    Fields report{{"tool", quote("LMU QuadView capability probe")},
        {"scope", quote("Independent OpenXR instance; no session, LocateViews, rendering or LMU integration. Application-specific runtime/layer behavior may differ.")}};
    int exitCode = 0;
    try {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--self-test") { self_test(); return 0; }
            if ((arg == L"--loader" || arg == L"--output") && i + 1 < argc) {
                if (arg == L"--loader") loader = argv[++i];
                else output = argv[++i];
            } else throw std::runtime_error("Usage: openxr_probe [--loader absolute.dll] [--output report.json] | --self-test");
        }
        if (!std::filesystem::path(loader).is_absolute()) throw std::runtime_error("Loader path must be absolute");
        report.emplace_back("loader", quote(utf8(loader)));
        probe(loader, report);
    } catch (const std::exception& e) {
        report.emplace_back("status", quote("error"));
        report.emplace_back("error", quote(e.what()));
        exitCode = 1;
    }
    std::string json = object(report) + '\n';
    if (!output.empty()) {
        std::ofstream file(output, std::ios::binary);
        file << json;
        if (!file) { std::cerr << "Cannot write report\n"; return 2; }
    }
    std::cout << json;
    return exitCode;
}
