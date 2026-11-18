#pragma once
#include <cstdint>
#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace og {
struct CapturedDepth;
struct CockpitDepthEye;

// The frame channel lock pairs the copied depth with the accepted color frame.
class StreamDepthPublisher {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint64_t generation_{};
  public:
    StreamDepthPublisher();
    ~StreamDepthPublisher();
    bool publish(ID3D11Device*, ID3D11DeviceContext*, const CockpitDepthEye&, CapturedDepth&);
    void discardTexture();
    void reset();
};

class StreamDepthReader {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool valid_{};
  public:
    StreamDepthReader();
    ~StreamDepthReader();
    StreamDepthReader(StreamDepthReader&&) noexcept;
    StreamDepthReader& operator=(StreamDepthReader&&) noexcept;
    bool consume(ID3D11Device*, ID3D11DeviceContext*, const CapturedDepth&, bool newSession);
    ID3D11ShaderResourceView* view() const;
    void reset();
};
} // namespace og
