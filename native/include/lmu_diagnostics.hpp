#pragma once
#include "../third_party/lmu_quadview/mod/win_strings.hpp"
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
enum class MvrState { off, requested, armed, unavailable };
struct DiagnosticSnapshot {
    std::string state{"starting"}, reason{"No engine hook success reported"}, cpuAffinity{"not attempted"};
    std::string warning,mvrReason;
    uint64_t reentryCount{}, outerTimeoutCount{}, nativeOuterRefusalCount{}, timestampMs{};
    uint32_t pid{};
    bool mvrInstanced{};
    MvrState mvrState{MvrState::off};
    std::array<uint64_t,2> mvrReused{}, mvrNative{};
};
namespace diagnostic_detail {
inline std::mutex guard;
inline DiagnosticSnapshot current;
inline std::atomic<uint64_t> reentries{}, outerTimeouts{}, nativeOuterRefusals{};
inline std::atomic<bool> mvrInstanced{false};
inline std::atomic<MvrState> mvrState{MvrState::off};
inline std::array<std::atomic<uint64_t>,2> mvrReused{}, mvrNative{};
inline std::array<char,513> nativeOuterWarning{},mvrReason{};
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
inline void copy_reason(std::array<char,513>& output,const char* input) noexcept {
    size_t i=0;
    for(;input&&i+1<output.size()&&input[i];++i){
        const auto c=static_cast<unsigned char>(input[i]);
        output[i]=c>=32&&c<127?char(c):' ';
    }
    output[i]='\0';
}
inline std::string quoted(const std::string& input) {
    std::string result{"\""};
    for (const char c:input) {if(c=='"'||c=='\\')result+='\\';result+=c;}
    return result+'"';
}
inline std::filesystem::path directory() {
    const auto overridePath = lmuqv::win::environment(L"OPENGRIDXR_STATUS_DIR");
    if (!overridePath.empty()) return overridePath;
    // Ordinary machine installation and explicit launches share this owned
    // per-user folder. Never write next to the game or installed executable.
    const auto local = lmuqv::win::environment(L"LOCALAPPDATA");
    return !local.empty() ? std::filesystem::path(local)/L"OpenGridXR"/L"runtime" : std::filesystem::path{};
}
inline DiagnosticSnapshot snapshot_locked() {
    auto result=current;
    result.reentryCount=reentries.load(std::memory_order_relaxed);
    result.outerTimeoutCount=outerTimeouts.load(std::memory_order_relaxed);
    result.nativeOuterRefusalCount=nativeOuterRefusals.load(std::memory_order_relaxed);
    result.warning=nativeOuterWarning.data();
    result.mvrReason=mvrReason.data();
    result.pid=GetCurrentProcessId();
    result.mvrInstanced=mvrInstanced.load(std::memory_order_relaxed);
    result.mvrState=mvrState.load(std::memory_order_relaxed);
    if(result.mvrState!=MvrState::off)for(unsigned eye=0;eye<2;++eye){
        result.mvrReused[eye]=mvrReused[eye].load(std::memory_order_relaxed);
        result.mvrNative[eye]=mvrNative[eye].load(std::memory_order_relaxed);
    }
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
       <<",\"nativeOuterRefusalCount\":"<<value.nativeOuterRefusalCount;
    if(value.mvrState!=MvrState::off){
        const char* state=value.mvrState==MvrState::requested?"requested":value.mvrState==MvrState::armed?"armed":"unavailable";
        out<<",\"nvidiaMvr\":{\"state\":\""<<state<<"\"";
        if(!value.mvrReason.empty())out<<",\"reason\":"<<diagnostic_detail::quoted(value.mvrReason);
        if(value.mvrState==MvrState::armed)out<<",\"backend\":\""<<(value.mvrInstanced?"instanced":"nvidia")<<"\"";
        out<<",\"reused\":["<<value.mvrReused[0]<<','<<value.mvrReused[1]
           <<"],\"native\":["<<value.mvrNative[0]<<','<<value.mvrNative[1]<<"]}";
    }
    out<<'}';
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
        if(requested=="starting"){
            diagnostic_detail::mvrReason[0]='\0';
            const bool mvr=lmuqv::win::environment(L"LMU_QV_NVIDIA_MVR")==L"1" &&
                lmuqv::win::environment(L"LMU_QV_MODE")==L"quad";
            for(unsigned eye=0;eye<2;++eye){
                diagnostic_detail::mvrReused[eye].store(0,std::memory_order_relaxed);
                diagnostic_detail::mvrNative[eye].store(0,std::memory_order_relaxed);
            }
            diagnostic_detail::mvrState.store(mvr?MvrState::requested:MvrState::off,std::memory_order_relaxed);
        }else if(diagnostic_detail::current.state=="unavailable" &&
                 diagnostic_detail::mvrState.load(std::memory_order_relaxed)!=MvrState::off){
            diagnostic_detail::mvrState.store(MvrState::unavailable,std::memory_order_relaxed);
        }
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
// Copy only on an actual refusal. Render-thread updates never touch disk.
inline void note_mvr_reason(const char* reason,bool available) noexcept {
    try {
        std::lock_guard<std::mutex> lock(diagnostic_detail::guard);
        if(diagnostic_detail::mvrState.load(std::memory_order_relaxed)==MvrState::off)return;
        diagnostic_detail::copy_reason(diagnostic_detail::mvrReason,reason);
        if(!available)diagnostic_detail::mvrState.store(MvrState::unavailable,std::memory_order_relaxed);
    } catch (...) {}
}
inline void note_mvr_start(bool armed, bool instanced = false) noexcept {
    if(diagnostic_detail::mvrState.load(std::memory_order_relaxed)==MvrState::requested) {
        diagnostic_detail::mvrInstanced.store(armed && instanced,std::memory_order_relaxed);
        diagnostic_detail::mvrState.store(armed?MvrState::armed:MvrState::unavailable,std::memory_order_relaxed);
    }
}
// Called once per completed focus traversal only when MVR was requested.
inline void note_mvr_focus(unsigned eye,bool reused,bool available) noexcept {
    auto& counts=reused?diagnostic_detail::mvrReused:diagnostic_detail::mvrNative;
    counts[eye].fetch_add(1,std::memory_order_relaxed);
    if(!available && diagnostic_detail::mvrState.load(std::memory_order_relaxed)==MvrState::armed)
        diagnostic_detail::mvrState.store(MvrState::unavailable,std::memory_order_relaxed);
}
// A recoverable full-resolution fallback is not an engine hook failure. Keep
// this render-path notification bounded: no file IO, logging or allocations.
inline void note_native_outer_refusal(const char* reason) noexcept {
    diagnostic_detail::nativeOuterRefusals.fetch_add(1,std::memory_order_relaxed);
    try {
        std::lock_guard<std::mutex> lock(diagnostic_detail::guard);
        diagnostic_detail::copy_reason(diagnostic_detail::nativeOuterWarning,reason);
    } catch (...) {}
}
}
