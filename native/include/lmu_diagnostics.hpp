#pragma once
#include <windows.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace og::lmu {
struct DiagnosticSnapshot {
    std::string state{"starting"}, reason{"No engine hook success reported"}, cpuAffinity{"not attempted"};
    std::string warning;
    uint64_t reentryCount{}, outerTimeoutCount{}, nativeOuterRefusalCount{}, timestampMs{};
    uint32_t pid{};
};
namespace diagnostic_detail {
inline std::mutex guard;
inline DiagnosticSnapshot current;
inline std::atomic<uint64_t> reentries{}, outerTimeouts{}, nativeOuterRefusals{};
inline std::array<char,513> nativeOuterWarning{};
inline uint64_t unix_ms() {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    return ((uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime)/10000-11644473600000ULL;
}
inline std::string text(const char* input) {
    std::string result;
    if (input) for (size_t i=0; i<512 && input[i]; ++i) {
        const auto c=static_cast<unsigned char>(input[i]);
        result+=c>=32 && c<127 ? char(c) : ' ';
    }
    return result;
}
inline std::string quoted(const std::string& input) {
    std::string result{"\""};
    for (const char c:input) {if(c=='"'||c=='\\')result+='\\';result+=c;}
    return result+'"';
}
inline std::filesystem::path directory() {
    wchar_t value[32768]{};
    auto size=GetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",value,32768);
    if(size&&size<32768)return value;
    // Ordinary machine installation and explicit launches share this owned
    // per-user folder. Never write next to the game or installed executable.
    size=GetEnvironmentVariableW(L"LOCALAPPDATA",value,32768);
    return size&&size<32768 ? std::filesystem::path(value)/L"OpenGridXR"/L"runtime" : std::filesystem::path{};
}
inline DiagnosticSnapshot snapshot_locked() {
    auto result=current;
    result.reentryCount=reentries.load(std::memory_order_relaxed);
    result.outerTimeoutCount=outerTimeouts.load(std::memory_order_relaxed);
    result.nativeOuterRefusalCount=nativeOuterRefusals.load(std::memory_order_relaxed);
    result.warning=nativeOuterWarning.data();
    result.pid=GetCurrentProcessId();
    return result;
}
}
inline std::string diagnostic_json(const DiagnosticSnapshot& value) {
    std::ostringstream out;
    out<<"{\"version\":1,\"pid\":"<<value.pid<<",\"timestampMs\":"<<value.timestampMs
       <<",\"state\":"<<diagnostic_detail::quoted(value.state)<<",\"reason\":"<<diagnostic_detail::quoted(value.reason)
       <<",\"cpuAffinity\":"<<diagnostic_detail::quoted(value.cpuAffinity)<<",\"reentryCount\":"<<value.reentryCount
       <<",\"outerTimeoutCount\":"<<value.outerTimeoutCount
       <<",\"warning\":"<<diagnostic_detail::quoted(value.warning)
       <<",\"nativeOuterRefusalCount\":"<<value.nativeOuterRefusalCount<<'}';
    return out.str();
}
namespace diagnostic_detail {
inline void persist_locked() {
    const auto folder=directory();if(folder.empty())return;
    std::error_code error;std::filesystem::create_directories(folder,error);if(error)return;
    const auto file=folder/L"OpenGridXR-lmu-diagnostics.json";
    auto temp=file;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return;
    out<<diagnostic_json(snapshot_locked());out.close();
    if(!out||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        std::filesystem::remove(temp,error);
}
}
inline DiagnosticSnapshot diagnostic_snapshot() {
    std::lock_guard<std::mutex> lock(diagnostic_detail::guard);
    return diagnostic_detail::snapshot_locked();
}
// Lifecycle/error/CPU calls persist one bounded replacement independent of
// verbose diagnostics. Never call these from a frame or tracking hot path.
inline void report_diagnostic(const char* state,const char* reason) noexcept {
    try {
        std::lock_guard<std::mutex> lock(diagnostic_detail::guard);
        const std::string requested=diagnostic_detail::text(state);
        diagnostic_detail::current.state=requested=="starting"||requested=="ready"?requested:"unavailable";
        diagnostic_detail::current.reason=diagnostic_detail::text(reason);
        diagnostic_detail::current.timestampMs=diagnostic_detail::unix_ms();
        diagnostic_detail::persist_locked();
    } catch (...) {} // Observability must not change game/runtime control flow.
}
inline void report_cpu_affinity(const char* reason) noexcept {
    try {
        std::lock_guard<std::mutex> lock(diagnostic_detail::guard);
        diagnostic_detail::current.cpuAffinity=diagnostic_detail::text(reason);
        diagnostic_detail::current.timestampMs=diagnostic_detail::unix_ms();
        diagnostic_detail::persist_locked();
    } catch (...) {}
}
inline void note_reentry() noexcept {diagnostic_detail::reentries.fetch_add(1,std::memory_order_relaxed);}
inline void note_outer_timeout() noexcept {diagnostic_detail::outerTimeouts.fetch_add(1,std::memory_order_relaxed);}
// A recoverable full-resolution fallback is not an engine hook failure. Keep
// this render-path notification bounded: no file IO, logging or allocations.
inline void note_native_outer_refusal(const char* reason) noexcept {
    diagnostic_detail::nativeOuterRefusals.fetch_add(1,std::memory_order_relaxed);
    try {
        std::lock_guard<std::mutex> lock(diagnostic_detail::guard);
        size_t index=0;
        if(reason)for(;index<512&&reason[index];++index){
            const auto c=static_cast<unsigned char>(reason[index]);
            diagnostic_detail::nativeOuterWarning[index]=c>=32&&c<127?char(c):' ';
        }
        diagnostic_detail::nativeOuterWarning[index]='\0';
    } catch (...) {}
}
}
