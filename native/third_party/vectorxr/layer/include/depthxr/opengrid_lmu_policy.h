#pragma once
// OpenGrid integration policy. Modifications remain under MPL-2.0.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace depthxr {
inline bool OpenGridLmuQuadMode(std::string_view executable) {
    const auto equals = [](const char* key, std::string_view expected) {
        const char* value = std::getenv(key);
        return value && expected == value;
    };
    std::string name(executable.substr(executable.find_last_of("/\\") + 1));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return name == "le mans ultimate.exe" && equals("OPENGRIDXR_LMU_INTEGRATED", "1") &&
           equals("LMU_QV_MODE", "quad");
}
inline bool OpenGridLmuQuadProvider(std::string_view executable) {
    const char* provider = std::getenv("OPENGRIDXR_LMU_PROVIDER");
    return OpenGridLmuQuadMode(executable) && provider && std::string_view(provider) == "vectorxr";
}
inline bool OpenGridLmuExternalQuadProvider(std::string_view executable) {
    const char* provider = std::getenv("OPENGRIDXR_LMU_PROVIDER");
    return OpenGridLmuQuadMode(executable) && provider &&
           (std::string_view(provider) == "qvfr" || std::string_view(provider) == "native");
}
} // namespace depthxr
