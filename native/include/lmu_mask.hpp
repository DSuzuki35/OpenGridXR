#pragma once
#include "settings.hpp"
#include <openxr/openxr.h>
namespace og {
// Final-color aperture. Geometry is defined in the full eye's tangent space;
// the render projection determines where those rays land in outer/focus images.
class LmuMask {
public:
    LmuMask(ID3D11Device*,ID3D11DeviceContext*);
    ~LmuMask();
    ImageProcessStatus draw(ID3D11Texture2D*,DXGI_FORMAT,const Config&,unsigned,
                            XrFovf fullEye,XrFovf rendered);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
