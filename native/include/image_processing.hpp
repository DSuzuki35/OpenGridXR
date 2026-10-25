#pragma once
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace og {
enum class SharpeningMode { Off, Luma, Cas };
struct SharpeningSettings {
    SharpeningMode mode{SharpeningMode::Off};
    float strength{50}, clamp{.035f}, radius{1};
    unsigned pattern{2}; // 1=Fast, 2=Normal, 3=Wider, 4=Pyramid.
};
struct ImageRegion {
    std::uint32_t arraySlice{}, x{}, y{}, width{}, height{};
};
enum class ImageProcessStatus { Skipped, Applied, Unsupported, Invalid, Failed };
struct ImageProcessResult {
    ImageProcessStatus status{ImageProcessStatus::Skipped};
    HRESULT error{S_OK};
};

// Call on the immediate context's owning thread, while the OpenXR image is acquired,
// after raw capture and before downstream release. Regions must be confirmed color
// projection subimages. The caller owns projection classification and call ordering.
// A processor belongs to one session/device; it caches scratch resources. No flush,
// CPU readback, persistent game-state changes, or driver installation is performed.
class ImageProcessor {
public:
    ImageProcessor(ID3D11Device *, ID3D11DeviceContext *);
    ~ImageProcessor();
    ImageProcessor(const ImageProcessor &) = delete;
    ImageProcessor &operator=(const ImageProcessor &) = delete;
    ImageProcessResult process(ID3D11Texture2D *, DXGI_FORMAT typedFormat,
                               const std::vector<ImageRegion> &, const SharpeningSettings &);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace og
