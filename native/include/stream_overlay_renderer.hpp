#pragma once
#include <cstdint>
#include <memory>
#include <string>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
namespace vrst {
struct Warp;
}
namespace og {
struct SharedFrame;
// Composites selected SDR, premultiplied overlays into the encoded UNORM canvas.
// All metadata and GPU ownership acquisition is nonblocking. The game renderer
// and this consumer have independent shared-texture rings.
class StreamOverlayRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    StreamOverlayRenderer();
    ~StreamOverlayRenderer();
    StreamOverlayRenderer(StreamOverlayRenderer &&) noexcept;
    StreamOverlayRenderer &operator=(StreamOverlayRenderer &&) noexcept;
    bool initialize(ID3D11Device *, ID3D11DeviceContext *, const std::string &game);
    void draw(ID3D11RenderTargetView *, std::uint32_t width, std::uint32_t height,
              const SharedFrame &, const vrst::Warp &);
    void reset();
};
} // namespace og
