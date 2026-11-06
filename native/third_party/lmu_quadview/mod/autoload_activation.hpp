#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace lmuqv::autoload {
inline std::wstring activation_file(const std::wstring& path) {
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){
        auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return {};
        throw std::runtime_error("Cannot read the automatic-start activation file.");
    }
    wchar_t root[32768]{};
    const auto length=GetPrivateProfileStringW(L"AutoStart",L"PackageRoot",L"",root,32768,path.c_str());
    if(!length||length>=32767||!std::filesystem::path(root).is_absolute())
        throw std::runtime_error("Invalid automatic-start activation file. Reinstall from Setup.");
    return root;
}
}
