#pragma once
#include "settings.hpp"
#include <openxr/openxr.h>
namespace og {
// Composes owned raw eye images only; never touches runtime swapchain images.
class LmuStream {
public:
    LmuStream(ID3D11Device*, ID3D11DeviceContext*);
    ~LmuStream();
    ID3D11Texture2D* compose(ID3D11Texture2D* outer, ID3D11Texture2D* focus,
        XrFovf outerFov, XrFovf focusFov, XrPosef outerPose, XrPosef focusPose,
        XrRect2Di focusRect, unsigned outputWidth, unsigned outputHeight);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
