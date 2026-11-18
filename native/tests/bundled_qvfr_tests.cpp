#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto directory = fs::temp_directory_path() /
        (L"OpenGrid-QVFR-\u8eca-\u0416-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()));
    const auto library = directory / L"OpenGridXR-QVFR.dll";
    const auto settings = directory / L"Quad-Views-Foveated";
    const auto log = settings / L"Quad-Views-Foveated.log";
    HMODULE module{};
    int result = 1;
    if (!fs::create_directory(directory)) return 2;
    try {
        fs::copy_file(argv[1], library);
        if (!SetEnvironmentVariableW(L"LOCALAPPDATA", directory.c_str()))
            throw std::runtime_error("could not isolate provider data");
        module = LoadLibraryExW(library.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) throw std::runtime_error("bundled provider failed to load");
        auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(
            GetProcAddress(module, "xrNegotiateLoaderApiLayerInterface"));
        XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO, XR_LOADER_INFO_STRUCT_VERSION,
            sizeof(XrNegotiateLoaderInfo), 1, 1, XR_MAKE_VERSION(1,0,0), XR_CURRENT_API_VERSION};
        XrNegotiateApiLayerRequest api{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST, XR_API_LAYER_INFO_STRUCT_VERSION,
            sizeof(XrNegotiateApiLayerRequest)};
        if (!negotiate || negotiate(&loader, "XR_APILAYER_MBUCCHIA_quad_views_foveated", &api) != XR_SUCCESS ||
            !api.getInstanceProcAddr || !api.createApiLayerInstance || !fs::is_regular_file(log))
            throw std::runtime_error("loader negotiation or Unicode provider directory failed");
        std::cout << "Bundled QVFR negotiated from a Unicode path with isolated user data\n";
        result = 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    if (module) FreeLibrary(module);
    std::error_code ignored;
    for (const auto& item : {log, settings, library, directory}) fs::remove(item, ignored);
    return result;
}
