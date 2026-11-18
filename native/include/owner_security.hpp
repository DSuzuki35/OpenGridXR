#pragma once
// Explicit security for the named channel objects (sections and their mutexes): full access
// for the current user and SYSTEM only, a protected DACL with nothing inherited. This is the
// same rule LMU Quad View's benchmark channel uses (mod/benchmark_native.hpp) instead of
// whatever default DACL the creating process token happens to carry.
// Windows applies it only when the object is created; opening an existing object ignores it.
// If the descriptor is unavailable, the creator must retry before creating a named object.
#include <windows.h>
#include <sddl.h>
#include <cwchar>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
namespace og {
class OwnerSecurity {
    PSECURITY_DESCRIPTOR descriptor_{};
    SECURITY_ATTRIBUTES attributes_{};

  public:
    OwnerSecurity() {
        HANDLE token{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return;
        alignas(TOKEN_USER) unsigned char buffer[512];
        DWORD size{};
        LPWSTR sid{};
        if (GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size) &&
            ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer)->User.Sid, &sid)) {
            wchar_t sddl[256]{};
            if (std::swprintf(sddl, 256, L"D:P(A;;GA;;;%ls)(A;;GA;;;SY)", sid) > 0 &&
                ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1,
                                                                     &descriptor_, nullptr)) {
                attributes_.nLength = sizeof(attributes_);
                attributes_.lpSecurityDescriptor = descriptor_;
                attributes_.bInheritHandle = FALSE;
            } else
                descriptor_ = nullptr;
            LocalFree(sid);
        }
        CloseHandle(token);
    }
    ~OwnerSecurity() {
        if (descriptor_)
            LocalFree(descriptor_);
    }
    OwnerSecurity(const OwnerSecurity &) = delete;
    OwnerSecurity &operator=(const OwnerSecurity &) = delete;
    SECURITY_ATTRIBUTES *get() { return descriptor_ ? &attributes_ : nullptr; }
};
} // namespace og
