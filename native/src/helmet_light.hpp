#pragma once
#include <d3d11.h>
#include <openxr/openxr.h>
#include <wrl/client.h>

namespace og {
// All resources belong to the session graphics device. The 2x2 probe stays on
// the GPU, and a private deferred context isolates both draws from game state.
class HelmetLight {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> commands_;
    Ptr<ID3D11VertexShader> vertex_;
    Ptr<ID3D11PixelShader> sample_, sampleMs_, shade_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11BlendState> smooth_;
    Ptr<ID3D11SamplerState> linear_;
    Ptr<ID3D11RenderTargetView> probeTarget_;
    Ptr<ID3D11ShaderResourceView> probe_;
    bool draw(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11RenderTargetView*,
              UINT width, UINT height, const float (&constants)[8],
              ID3D11ShaderResourceView* const*, UINT count, bool smooth);
public:
    Ptr<ID3D11ShaderResourceView> probe() const {return probe_;}
    bool initialize(ID3D11Device*);
    bool sample(ID3D11DeviceContext*, ID3D11Texture2D*, DXGI_FORMAT, const XrSwapchainSubImage&, float weight);
    bool shade(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11ShaderResourceView* art,
               UINT width, UINT height, float strength);
};
}
