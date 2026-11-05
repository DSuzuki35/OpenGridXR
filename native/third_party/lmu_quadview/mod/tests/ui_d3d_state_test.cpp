// Real D3D11 WARP state round-trip. Does not load or access LMU.
#include "../ui_d3d_state.hpp"
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
template<class T> class Com {
    T* value{};
public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (value) value->Release(); }
    T* get() const { return value; }
    T* operator->() const { return value; }
    T** put() { return &value; }
    void attach(T* pointer) { value = pointer; }
};

void require(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}

void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed: 0x" << std::hex
                << static_cast<unsigned long>(result);
        throw std::runtime_error(message.str());
    }
}

void compile(const char* source, const char* entry, const char* target,
             Com<ID3DBlob>& bytecode) {
    Com<ID3DBlob> errors;
    const HRESULT result = D3DCompile(source, std::strlen(source), "state-test",
        nullptr, nullptr, entry, target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION,
        0, bytecode.put(), errors.put());
    if (FAILED(result) && errors.get())
        std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                        static_cast<std::streamsize>(errors->GetBufferSize()));
    check(result, entry);
}

struct Fixture {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    std::array<Com<ID3D11Texture2D>, 2> colors;
    std::array<Com<ID3D11RenderTargetView>, 2> targets;
    Com<ID3D11Texture2D> depthTexture, sourceTexture;
    Com<ID3D11DepthStencilView> depth;
    Com<ID3D11ShaderResourceView> resource;
    Com<ID3D11Buffer> vertex, constant;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11ClassLinkage> linkage;
    Com<ID3D11ClassInstance> shaderClass;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11BlendState> blend;
    Com<ID3D11RasterizerState> raster;
    const UINT stride = 32, offset = 16, sampleMask = 0x13579bdf;
    const std::array<FLOAT, 4> factors{{0.125f, 0.25f, 0.5f, 0.75f}};
    const std::array<D3D11_VIEWPORT, 2> viewports{{
        {1.0f, 2.0f, 20.0f, 21.0f, 0.125f, 0.875f},
        {3.0f, 4.0f, 22.0f, 23.0f, 0.25f, 0.75f}}};
    const std::array<D3D11_RECT, 2> scissors{{{1, 2, 20, 21}, {3, 4, 22, 23}}};
    const D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;

    Fixture() {
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            requested, 1, D3D11_SDK_VERSION, device.put(), &level, context.put()),
            "D3D11CreateDevice(WARP)");
        require(level == D3D_FEATURE_LEVEL_11_0, "WARP feature level changed");

        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = texture.Height = 32;
        texture.MipLevels = texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            check(device->CreateTexture2D(&texture, nullptr, colors[i].put()),
                  "Create render target texture");
            check(device->CreateRenderTargetView(colors[i].get(), nullptr,
                                                targets[i].put()), "Create RTV");
        }
        texture.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        check(device->CreateTexture2D(&texture, nullptr, depthTexture.put()),
              "Create depth texture");
        check(device->CreateDepthStencilView(depthTexture.get(), nullptr,
                                            depth.put()), "Create DSV");
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&texture, nullptr, sourceTexture.put()),
              "Create separate SRV texture");
        check(device->CreateShaderResourceView(sourceTexture.get(), nullptr,
                                              resource.put()), "Create SRV");

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 256;
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        check(device->CreateBuffer(&buffer, nullptr, vertex.put()), "Create VB");
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&buffer, nullptr, constant.put()), "Create CB");

        // A real interface slot checks the class-instance arrays and their refs.
        constexpr const char* shader = R"(
            interface IOperation { float4 Apply(float4 value); };
            class IdentityOperation : IOperation {
                float4 Apply(float4 value) { return value; }
            };
            IOperation operation;
            float4 VSMain(float4 value : POSITION) : SV_Position {
                return operation.Apply(value);
            }
            float4 PSMain(float4 value : SV_Position) : SV_Target {
                return operation.Apply(value);
            }
        )";
        Com<ID3DBlob> vsCode, psCode;
        compile(shader, "VSMain", "vs_5_0", vsCode);
        compile(shader, "PSMain", "ps_5_0", psCode);
        check(device->CreateClassLinkage(linkage.put()), "CreateClassLinkage");
        check(device->CreateVertexShader(vsCode->GetBufferPointer(),
            vsCode->GetBufferSize(), linkage.get(), vs.put()), "Create VS");
        check(device->CreatePixelShader(psCode->GetBufferPointer(),
            psCode->GetBufferSize(), linkage.get(), ps.put()), "Create PS");
        check(linkage->CreateClassInstance("IdentityOperation", 0, 0, 0, 0,
                                          shaderClass.put()), "CreateClassInstance");
        const D3D11_INPUT_ELEMENT_DESC element = {
            "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
            D3D11_INPUT_PER_VERTEX_DATA, 0};
        check(device->CreateInputLayout(&element, 1, vsCode->GetBufferPointer(),
            vsCode->GetBufferSize(), layout.put()), "CreateInputLayout");

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.IndependentBlendEnable = TRUE;
        for (auto& target : blendDesc.RenderTarget) {
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
            target.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D11_BLEND_ONE;
            target.DestBlendAlpha = D3D11_BLEND_ZERO;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        }
        check(device->CreateBlendState(&blendDesc, blend.put()), "CreateBlendState");
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_WIREFRAME;
        rasterDesc.CullMode = D3D11_CULL_FRONT;
        rasterDesc.FrontCounterClockwise = TRUE;
        rasterDesc.DepthBias = 2;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.ScissorEnable = TRUE;
        check(device->CreateRasterizerState(&rasterDesc, raster.put()),
              "CreateRasterizerState");
    }

    void bind() const {
        auto* ctx = context.get();
        ID3D11RenderTargetView* rtvs[] = {targets[0].get(), targets[1].get()};
        ctx->OMSetRenderTargets(2, rtvs, depth.get());
        ID3D11ClassInstance* instance = shaderClass.get();
        ctx->VSSetShader(vs.get(), &instance, 1);
        ctx->PSSetShader(ps.get(), &instance, 1);
        ID3D11Buffer* vb = vertex.get();
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetInputLayout(layout.get());
        ctx->IASetPrimitiveTopology(topology);
        ID3D11Buffer* cb = constant.get();
        ctx->VSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* srv = resource.get();
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->OMSetBlendState(blend.get(), factors.data(), sampleMask);
        ctx->RSSetState(raster.get());
        ctx->RSSetViewports(static_cast<UINT>(viewports.size()), viewports.data());
        ctx->RSSetScissorRects(static_cast<UINT>(scissors.size()), scissors.data());
    }

    void verify() const {
        auto* ctx = context.get();
        std::array<ID3D11RenderTargetView*, 8> rawTargets{};
        std::array<Com<ID3D11RenderTargetView>, 8> actualTargets;
        Com<ID3D11DepthStencilView> actualDepth;
        ctx->OMGetRenderTargets(static_cast<UINT>(rawTargets.size()),
                               rawTargets.data(), actualDepth.put());
        for (std::size_t i = 0; i < rawTargets.size(); ++i)
            actualTargets[i].attach(rawTargets[i]);
        for (std::size_t i = 0; i < rawTargets.size(); ++i)
            require(actualTargets[i].get() == (i < 2 ? targets[i].get() : nullptr),
                    "RTV slot mismatch");
        require(actualDepth.get() == depth.get(), "DSV mismatch");

        Com<ID3D11VertexShader> actualVs;
        Com<ID3D11PixelShader> actualPs;
        std::array<ID3D11ClassInstance*, 256> rawVsClasses{}, rawPsClasses{};
        std::array<Com<ID3D11ClassInstance>, 256> actualVsClasses, actualPsClasses;
        UINT vsCount = 256, psCount = 256;
        ctx->VSGetShader(actualVs.put(), rawVsClasses.data(), &vsCount);
        ctx->PSGetShader(actualPs.put(), rawPsClasses.data(), &psCount);
        for (UINT i = 0; i < vsCount; ++i) actualVsClasses[i].attach(rawVsClasses[i]);
        for (UINT i = 0; i < psCount; ++i) actualPsClasses[i].attach(rawPsClasses[i]);
        require(actualVs.get() == vs.get(), "VS mismatch");
        require(actualPs.get() == ps.get(), "PS mismatch");
        require(vsCount == 1 && actualVsClasses[0].get() == shaderClass.get(),
                "VS class instance/count mismatch");
        require(psCount == 1 && actualPsClasses[0].get() == shaderClass.get(),
                "PS class instance/count mismatch");

        Com<ID3D11Buffer> actualVertex, actualConstant;
        UINT actualStride = 0, actualOffset = 0;
        ctx->IAGetVertexBuffers(0, 1, actualVertex.put(), &actualStride, &actualOffset);
        require(actualVertex.get() == vertex.get() && actualStride == stride &&
                actualOffset == offset, "VB/stride/offset mismatch");
        ctx->VSGetConstantBuffers(0, 1, actualConstant.put());
        require(actualConstant.get() == constant.get(), "VS CB0 mismatch");
        Com<ID3D11InputLayout> actualLayout;
        ctx->IAGetInputLayout(actualLayout.put());
        require(actualLayout.get() == layout.get(), "InputLayout mismatch");
        D3D11_PRIMITIVE_TOPOLOGY actualTopology{};
        ctx->IAGetPrimitiveTopology(&actualTopology);
        require(actualTopology == topology, "Topology mismatch");
        Com<ID3D11ShaderResourceView> actualResource;
        ctx->PSGetShaderResources(0, 1, actualResource.put());
        require(actualResource.get() == resource.get(), "PS SRV0 mismatch");
        Com<ID3D11BlendState> actualBlend;
        std::array<FLOAT, 4> actualFactors{};
        UINT actualMask = 0;
        ctx->OMGetBlendState(actualBlend.put(), actualFactors.data(), &actualMask);
        require(actualBlend.get() == blend.get() && actualFactors == factors &&
                actualMask == sampleMask, "Blend state/factors/mask mismatch");
        Com<ID3D11RasterizerState> actualRaster;
        ctx->RSGetState(actualRaster.put());
        require(actualRaster.get() == raster.get(), "RasterizerState mismatch");

        std::array<D3D11_VIEWPORT, 16> actualViewports{};
        std::array<D3D11_RECT, 16> actualScissors{};
        UINT viewportCount = 16, scissorCount = 16;
        ctx->RSGetViewports(&viewportCount, actualViewports.data());
        ctx->RSGetScissorRects(&scissorCount, actualScissors.data());
        require(viewportCount == viewports.size(), "Viewport count mismatch");
        require(scissorCount == scissors.size(), "Scissor count mismatch");
        for (std::size_t i = 0; i < viewports.size(); ++i) {
            const auto& a = actualViewports[i];
            const auto& b = viewports[i];
            require(a.TopLeftX == b.TopLeftX && a.TopLeftY == b.TopLeftY &&
                a.Width == b.Width && a.Height == b.Height &&
                a.MinDepth == b.MinDepth && a.MaxDepth == b.MaxDepth,
                "Viewport contents mismatch");
            const auto& c = actualScissors[i];
            const auto& d = scissors[i];
            require(c.left == d.left && c.top == d.top &&
                c.right == d.right && c.bottom == d.bottom,
                "Scissor contents mismatch");
        }
    }
};
}

int main() {
    try {
        Fixture fixture;
        fixture.bind();
        // Verify the fixture itself before testing the guard, including nonnull
        // dynamic linkage objects. ClearState destroys every captured family.
        fixture.verify();
        {
            lmuqv::UiD3dState state(fixture.context.get());
            fixture.context->ClearState();
        }
        fixture.verify();
        fixture.context->ClearState();
        fixture.context->Flush();
        std::cout << "D3D11 WARP UI state round-trip passed (including shader classes)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
