#pragma once
#include "cockpit_depth.hpp"
#include <array>

namespace og::lmu {
using CockpitProjection = std::array<float, 16>;
using CockpitCapture = void (*)(unsigned, const CockpitProjection&, ID3D11DeviceContext*,
                                ID3D11Texture2D*, ID3D11ShaderResourceView*, const D3D11_VIEWPORT&);
bool cockpit_observer(CockpitCapture observer);
void cockpit_open(XrSession, ID3D11Device*);
void cockpit_close(XrSession);
void cockpit_locate(XrSession, const XrViewLocateInfo*, const XrViewState*, const XrView*, bool valid);
void cockpit_submit(XrSession, const XrFrameEndInfo*);
bool cockpit_request(ID3D11Device*, XrSession, XrTime, bool, CockpitDepthFrame*);
bool cockpit_read(XrSession, XrTime, CockpitDepthFrame&);
}
