#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
namespace og {
struct StreamHelmetStatus {
    bool ready{};
    unsigned uploads{}, draws{};
};
// Output-only flat artwork. No OpenXR state, mask aperture, tracking or grading.
class StreamHelmetRenderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    StreamHelmetRenderer();
    ~StreamHelmetRenderer();
    StreamHelmetRenderer(StreamHelmetRenderer &&) noexcept;
    StreamHelmetRenderer &operator=(StreamHelmetRenderer &&) noexcept;
    StreamHelmetRenderer(const StreamHelmetRenderer &) = delete;
    StreamHelmetRenderer &operator=(const StreamHelmetRenderer &) = delete;
    bool initialize(ID3D11Device *, ID3D11DeviceContext *, const std::string &game,
                    const std::filesystem::path &directory = {});
    void reset();
    // Explicit configuration refresh for hosts/tests; draw polls at most twice
    // per second. Unchanged artwork never decodes/uploads again.
    void refresh();
    void draw(ID3D11RenderTargetView *, uint32_t width, uint32_t height);
    StreamHelmetStatus status() const;
};
} // namespace og
