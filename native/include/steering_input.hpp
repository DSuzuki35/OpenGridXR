#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace og::steering {
inline std::optional<double> normalize_axis(std::int32_t value, std::int32_t minimum,
                                             std::int32_t maximum) {
    // DirectInput uses the extreme LONG values to report an unrestricted range.
    if (minimum >= maximum || minimum == (std::numeric_limits<std::int32_t>::min)() ||
        maximum == (std::numeric_limits<std::int32_t>::max)()) return std::nullopt;
    return std::clamp(2.0 * (double(value) - double(minimum)) /
                          (double(maximum) - double(minimum)) - 1.0, -1.0, 1.0);
}
inline float radians(double normalized, double lockDegrees, bool invert) {
    if (!std::isfinite(normalized) || !std::isfinite(lockDegrees) ||
        lockDegrees < 90 || lockDegrees > 2520) return 0;
    // DirectInput increases right; the renderer's positive angle is counterclockwise.
    return float(std::clamp(normalized, -1.0, 1.0) * lockDegrees *
                 (3.14159265358979323846 / 360.0) * (invert ? 1.0 : -1.0));
}
inline bool fresh(std::uint64_t sampledTick, std::uint64_t now) {
    return sampledTick <= now && now - sampledTick <= 250;
}

#ifdef _WIN32
inline constexpr wchar_t MappingName[] = L"Local\\OpenGridXR.SteeringInput.v1";
struct alignas(8) SharedSample {
    volatile LONG sequence;
    std::uint32_t version;
    std::uint64_t sampledTick;
    float radians;
    std::uint32_t valid;
    std::uint32_t token;
    std::uint32_t reserved;
};
static_assert(sizeof(SharedSample) == 32, "Steering mapping ABI must remain 32 bytes");
class Writer {
    HANDLE mapping{};
    SharedSample *sample{};
  public:
    explicit Writer(const wchar_t *name=MappingName) {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     sizeof(SharedSample), name);
        if (mapping) sample = static_cast<SharedSample *>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(SharedSample)));
        if (sample) {
            // Recover even if the previous process exited in the middle of a write.
            InterlockedExchange(&sample->sequence, 1);
            sample->version=1; sample->sampledTick=0; sample->radians=0;
            sample->valid=0; sample->token=0; sample->reserved=0;
            InterlockedExchange(&sample->sequence, 2);
        }
    }
    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;
    bool available() const { return sample != nullptr; }
    void publish(float angle, std::uint32_t token, bool valid, std::uint64_t now=GetTickCount64()) {
        if (!sample) return;
        InterlockedIncrement(&sample->sequence);
        sample->sampledTick=now; sample->radians=std::isfinite(angle) ? angle : 0;
        sample->token=token; sample->valid=valid && token && std::isfinite(angle);
        InterlockedIncrement(&sample->sequence);
    }
    ~Writer() {
        publish(0,0,false);
        if(sample) UnmapViewOfFile(sample);
        if(mapping) CloseHandle(mapping);
    }
};
class Reader {
    HANDLE mapping{};
    const volatile SharedSample *sample{};
    std::uint64_t nextOpen{};
    std::wstring name;
  public:
    explicit Reader(const wchar_t *mappingName=MappingName):name(mappingName) {}
    Reader(const Reader &)=delete;
    Reader &operator=(const Reader &)=delete;
    std::optional<float> read(std::uint32_t expectedToken, std::uint64_t now=GetTickCount64()) {
        if(!expectedToken) return std::nullopt;
        if(!sample && now>=nextOpen) {
            nextOpen=now+1000;
            mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
            if(mapping) {
                sample=static_cast<const volatile SharedSample *>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(SharedSample)));
                if(!sample) {CloseHandle(mapping);mapping=nullptr;}
            }
        }
        if(!sample) return std::nullopt;
        // A bounded seqlock read never stalls the render thread on the input helper.
        LONG before=sample->sequence;
        if(before & 1) return std::nullopt;
        MemoryBarrier();
        const auto version=sample->version, token=sample->token, valid=sample->valid;
        const auto tick=sample->sampledTick;
        const auto angle=sample->radians;
        MemoryBarrier();
        if(before!=sample->sequence || version!=1 || !valid || token!=expectedToken ||
           !fresh(tick,now) || !std::isfinite(angle) || std::abs(angle)>22.0f) return std::nullopt;
        return angle;
    }
    void reset() {
        if(sample) UnmapViewOfFile(const_cast<const SharedSample *>(sample));
        if(mapping) CloseHandle(mapping);
        sample=nullptr;mapping=nullptr;nextOpen=0;
    }
    ~Reader() { reset(); }
};
#endif
} // namespace og::steering
