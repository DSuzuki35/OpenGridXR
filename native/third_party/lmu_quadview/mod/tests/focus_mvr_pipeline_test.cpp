// Exercise the installed D3D11 detours on WARP. The fixture substitutes only the
// NVAPI entry points; the shaders, draw state, MSAA resolves and readback are real.
#define NOMINMAX
#include "../focus_mvr.hpp"
#include "../nv_multiview_backend.hpp"
#include "../vendor/minhook-1.3.4/include/MinHook.h"
#include <d3dcompiler.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <thread>
#include <atomic>

// Exercise the real factory detour, including its trampoline, with a native
// D3D11 factory that can deliberately finish after instance teardown.
namespace {
HANDLE factoryEntered, releaseFactory, twinEntered, releaseTwin;
std::atomic<bool> delayTwin{};
__declspec(noinline) HRESULT WINAPI delayedFactory(IDXGIAdapter* a,D3D_DRIVER_TYPE d,HMODULE sw,UINT flags,
    const D3D_FEATURE_LEVEL* levels,UINT n,UINT sdk,ID3D11Device** dev,D3D_FEATURE_LEVEL* fl,ID3D11DeviceContext** ctx) {
    SetEvent(factoryEntered);
    if(WaitForSingleObject(releaseFactory,10000)!=WAIT_OBJECT_0)return E_ABORT;
    return D3D11CreateDevice(a,d,sw,flags,levels,n,sdk,dev,fl,ctx);
}
}
namespace lmuqv::focus_mvr {
bool install_early_factories(decltype(&D3D11CreateDevice),decltype(&D3D11CreateDeviceAndSwapChain));
}

using Microsoft::WRL::ComPtr;
namespace mvr = lmuqv::focus_mvr;
namespace {
char lastStop[1024]{};
constexpr unsigned outerW = 16, outerH = 12, focusW = 8, focusH = 8;
constexpr DXGI_FORMAT rgba = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr uint32_t red = 0xff0000ff, green = 0xff00ff00, blue = 0xffff0000;
const D3D11_VIEWPORT originalViewport{0, 0, outerW, outerH, 0, 1};
const D3D11_RECT originalScissor{0, 0, outerW, outerH};
bool supported = true, bindingTest = false;
#ifdef LMU_MVR_SHADOW_PROBE
bool matchedShadowResource=false,detectedChangedMatrix=false;
#endif
unsigned twins = 0, enabled = 0, disabled = 0;
ID3D11VertexShader* expectedTwin = nullptr; // owned by the game shader's private data
ID3D11PixelShader* focusMarker = nullptr; // owned by the fixture

void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
NvAPI_Status __cdecl initialize() { return NVAPI_OK; }
NvAPI_Status __cdecl sps(IUnknown*, NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS* p) {
    p->bSinglePassStereoSupported = p->bSinglePassStereoXYZWSupported = supported;
    return NVAPI_OK;
}
NvAPI_Status __cdecl createTwin(ID3D11Device* device, const void* code, SIZE_T bytes, ID3D11ClassLinkage* linkage,
                                const NvAPI_D3D11_CREATE_VERTEX_SHADER_EX* ex, ID3D11VertexShader** out) {
    check(ex->NumCustomSemantics == 2 &&
          ex->pCustomSemantics[0].NVCustomSemanticType == NV_XYZW_RIGHT_SEMANTIC &&
          ex->pCustomSemantics[1].NVCustomSemanticType == NV_VIEWPORT_MASK_SEMANTIC, "missing SPS custom semantics");
    if (FAILED(device->CreateVertexShader(code, bytes, linkage, out))) return NVAPI_ERROR;
    if(delayTwin.load()){
        SetEvent(twinEntered);
        if(WaitForSingleObject(releaseTwin,10000)!=WAIT_OBJECT_0)return NVAPI_ERROR;
    }
    expectedTwin = *out;
    ++twins;
    return NVAPI_OK;
}
NvAPI_Status __cdecl stereoMode(IUnknown* object, NvU32 views, NvU32 offset, NvU8 independent) {
    check(offset == 0 && (views == 2 ? independent != 0 : views == 1 && !independent), "wrong stereo mode");
    if (views == 2) { ++enabled; return NVAPI_OK; }
    if(bindingTest){++disabled;return NVAPI_OK;}
    auto* context = static_cast<ID3D11DeviceContext*>(object);
    D3D11_VIEWPORT vp[2]{};
    D3D11_RECT sc[2]{};
    UINT viewCount = 2, rectCount = 2;
    context->RSGetViewports(&viewCount, vp);
    context->RSGetScissorRects(&rectCount, sc);
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11Buffer> remap;
    context->VSGetShader(&vs, nullptr, nullptr);
    context->VSGetConstantBuffers(13, 1, &remap);
    check(vs.Get() == expectedTwin && remap, "twin shader and clip remap must be bound together");
    check(viewCount == 2 && vp[1].TopLeftX == outerW && vp[1].Width == focusW && vp[1].Height == focusH &&
          rectCount == 2 && sc[1].left == outerW && sc[1].right == outerW + focusW && sc[1].bottom == focusH,
          "focus viewport/scissor must address the atlas's second region");
    // WARP has no hardware broadcast. Replay this fixture's triangle in slot one's
    // viewport, with a marker color to prove which region gets resolved.
    ComPtr<ID3D11PixelShader> originalPixel;
    context->PSGetShader(&originalPixel, nullptr, nullptr);
    context->PSSetShader(focusMarker, nullptr, 0);
    context->RSSetViewports(1, vp + 1);
    context->RSSetScissorRects(1, sc + 1);
    mvr::mask(true);
    context->Draw(3, 0);
    mvr::mask(false);
    context->PSSetShader(originalPixel.Get(), nullptr, 0);
    ++disabled;
    return NVAPI_OK;
}
ComPtr<ID3DBlob> compile(const char* source, const char* target) {
    ComPtr<ID3DBlob> blob, errors;
    check(SUCCEEDED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, &blob, &errors)),
          "fixture shader did not compile");
    return blob;
}
struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> hdr, resolved, focusHdr, focusResolved, depth;
    ComPtr<ID3D11RenderTargetView> rt, focusRt;
    ComPtr<ID3D11DepthStencilView> ds;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps, greenPs, screenSpacePs;
    ComPtr<ID3D11Buffer> appCb;

    ComPtr<ID3D11Texture2D> texture(unsigned w, unsigned h, DXGI_FORMAT fmt, unsigned samples, unsigned bind) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w; desc.Height = h; desc.Format = fmt; desc.ArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = samples; desc.BindFlags = bind;
        ComPtr<ID3D11Texture2D> result;
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &result)), "texture creation failed");
        return result;
    }
    explicit Fixture(bool neutral = false, IDXGIAdapter* adapter = nullptr) {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        check(SUCCEEDED(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
              D3D11_SDK_VERSION, &device, nullptr, &context)), "D3D11 device creation failed");
        supported = false;
        check(!mvr::start(context.Get()) && !mvr::available(), "unsupported driver enabled");
        supported = !neutral;
        check(mvr::start(context.Get(),bindingTest) && mvr::available(), "failed to install scene hooks on WARP");
        const DXGI_FORMAT sceneFormat=bindingTest?DXGI_FORMAT_R16G16B16A16_FLOAT:rgba;
        hdr = texture(outerW + focusW, outerH, sceneFormat, 4, D3D11_BIND_RENDER_TARGET);
        resolved = texture(outerW + focusW, outerH, sceneFormat, 1, D3D11_BIND_SHADER_RESOURCE);
        depth = texture(outerW + focusW, outerH, DXGI_FORMAT_D32_FLOAT, 4, D3D11_BIND_DEPTH_STENCIL);
        focusHdr = texture(focusW, focusH, sceneFormat, 4, D3D11_BIND_RENDER_TARGET);
        focusResolved = texture(focusW, focusH, sceneFormat, 1, D3D11_BIND_SHADER_RESOURCE);
        check(SUCCEEDED(device->CreateRenderTargetView(hdr.Get(), nullptr, &rt)) &&
              SUCCEEDED(device->CreateRenderTargetView(focusHdr.Get(), nullptr, &focusRt)) &&
              SUCCEEDED(device->CreateDepthStencilView(depth.Get(), nullptr, &ds)), "view creation failed");
        auto v = compile("float4 main(uint i:SV_VertexID):SV_Position { return float4(i==1?3:-1,i==2?3:-1,0,1); }", "vs_5_0");
        auto p = compile("float4 main():SV_Target { return float4(1,0,0,1); }", "ps_5_0");
        auto g = compile("float4 main():SV_Target { return float4(0,1,0,1); }", "ps_5_0");
        auto screen = compile("float4 main(float4 pos:SV_Position):SV_Target { return float4(pos.x/16,0,0,1); }", "ps_5_0");
        check(SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs)) && twins == (neutral ? 0u : 1u),
              "game shader did not receive its patched twin");
        ComPtr<ID3D11VertexShader> duplicate;
        check(SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &duplicate)) && twins == (neutral ? 0u : 1u),
              "identical bytecode repeated NVIDIA shader compilation");
        check(SUCCEEDED(device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps)), "pixel shader creation failed");
        check(SUCCEEDED(device->CreatePixelShader(g->GetBufferPointer(), g->GetBufferSize(), nullptr, &greenPs)), "marker shader creation failed");
        check(SUCCEEDED(device->CreatePixelShader(screen->GetBufferPointer(), screen->GetBufferSize(), nullptr, &screenSpacePs)), "screen-space shader creation failed");
        focusMarker = greenPs.Get();
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth = 16; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(SUCCEEDED(device->CreateBuffer(&cb, nullptr, &appCb)), "constant buffer creation failed");
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE; raster.ScissorEnable = TRUE;
        ComPtr<ID3D11RasterizerState> state;
        check(SUCCEEDED(device->CreateRasterizerState(&raster, &state)), "rasterizer creation failed");
        context->RSSetState(state.Get());
        context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, appCb.GetAddressOf());
        context->RSSetViewports(1, &originalViewport); context->RSSetScissorRects(1, &originalScissor);
        check(mvr::prepare(resolved.Get(), hdr.Get(), resolved.Get(), rt.Get(), ds.Get(), focusHdr.Get(), focusResolved.Get(),
                           outerW, outerH, focusW, focusH, {1, 0, 1, 0}), "atlas setup failed");
    }
    ~Fixture() { mvr::stop(); }
    void outerDraw(bool resolve, bool clearDepth = true) {
        const float black[4]{};
        context->OMSetRenderTargets(1, rt.GetAddressOf(), ds.Get());
        mvr::begin_outer();
        context->ClearRenderTargetView(rt.Get(), black);
        if (clearDepth) context->ClearDepthStencilView(ds.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        { mvr::GeometryScope scene(true); context->Draw(3, 0); }
        if (resolve) context->ResolveSubresource(resolved.Get(), 0, hdr.Get(), 0, rgba);
    }
    void preserved(ID3D11Buffer* expectedB13) {
        ComPtr<ID3D11VertexShader> shader;
        ComPtr<ID3D11Buffer> b0, b13;
        ComPtr<ID3D11RenderTargetView> color;
        ComPtr<ID3D11DepthStencilView> z;
        D3D11_VIEWPORT vp[2]{}; D3D11_RECT sc[2]{}; UINT n = 2, m = 2;
        context->VSGetShader(&shader, nullptr, nullptr);
        context->VSGetConstantBuffers(0, 1, &b0); context->VSGetConstantBuffers(13, 1, &b13);
        context->OMGetRenderTargets(1, &color, &z);
        context->RSGetViewports(&n, vp); context->RSGetScissorRects(&m, sc);
        check(shader == vs && b0 == appCb && b13.Get() == expectedB13 && color == rt && z == ds,
              "game bindings changed");
        check(n == 1 && m == 1 && !std::memcmp(vp, &originalViewport, sizeof(originalViewport)) &&
              !std::memcmp(sc, &originalScissor, sizeof(originalScissor)), "game viewport/scissor changed");
    }
    void pixels(ID3D11Texture2D* image, unsigned w, unsigned h, uint32_t expected) {
        D3D11_TEXTURE2D_DESC desc{};
        image->GetDesc(&desc); check(desc.Width == w && desc.Height == h, "wrong output dimensions");
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "staging creation failed");
        context->CopyResource(staging.Get(), image);
        D3D11_MAPPED_SUBRESOURCE m{};
        check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m)), "staging readback failed");
        bool matched = true;
        for (unsigned y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<const uint32_t*>(static_cast<const char*>(m.pData) + y * m.RowPitch);
            for (unsigned x = 0; x < w; ++x) matched &= row[x] == expected;
        }
        context->Unmap(staging.Get(), 0);
        check(matched, "wrong color in resolved image");
    }
    void focusResolve() {
        const float nativeBlue[4]{0, 0, 1, 1};
        context->ClearRenderTargetView(focusRt.Get(), nativeBlue);
        context->ResolveSubresource(focusResolved.Get(), 0, focusHdr.Get(), 0, rgba);
    }
    template<class Change> void rejectChangedBinding(Change change, const char* reason) {
        const auto before = enabled;
        outerDraw(false);
        check(enabled == before + 1, "first material draw was not paired");
        change();
        { mvr::GeometryScope scene(true); context->Draw(3, 0); }
        context->ResolveSubresource(resolved.Get(), 0, hdr.Get(), 0, rgba);
        mvr::end_outer(resolved.Get());
        check(enabled == before + 1 && !mvr::begin_focus(), reason);
        focusResolve();
        check(!mvr::end_focus(), "unsafe binding suppressed native focus");
        pixels(focusResolved.Get(), focusW, focusH, blue);
    }
    void changedBindings() {
        auto code = compile("Texture2D a:register(t0), b:register(t31); SamplerState samp:register(s0);"
                            "float4 main():SV_Target {return a.SampleLevel(samp,.5,0)+b.SampleLevel(samp,.5,0);}", "ps_5_0");
        ComPtr<ID3D11PixelShader> materialPs;
        check(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &materialPs)),
              "material shader creation failed");
        auto ordinary = texture(1, 1, rgba, 1, D3D11_BIND_SHADER_RESOURCE);
        auto writable = texture(1, 1, rgba, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
        ComPtr<ID3D11ShaderResourceView> safe, unsafe;
        check(SUCCEEDED(device->CreateShaderResourceView(ordinary.Get(), nullptr, &safe)) &&
              SUCCEEDED(device->CreateShaderResourceView(writable.Get(), nullptr, &unsafe)), "material SRV creation failed");
        context->PSSetShader(materialPs.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, safe.GetAddressOf());
        context->PSSetShaderResources(30, 1, unsafe.GetAddressOf()); // not consumed by this shader
        context->PSSetShaderResources(31, 1, safe.GetAddressOf());
        rejectChangedBinding([&]{ context->PSSetShaderResources(31, 1, unsafe.GetAddressOf()); },
                             "new screen-texture binding reused the old material classification");
        ID3D11ShaderResourceView* empty[32]{};
        context->PSSetShaderResources(0, 32, empty);
        context->PSSetShader(ps.Get(), nullptr, 0);

        if (device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1) {
            auto unordered = texture(1, 1, DXGI_FORMAT_R32_UINT, 1, D3D11_BIND_UNORDERED_ACCESS);
            ComPtr<ID3D11UnorderedAccessView> uav;
            check(SUCCEEDED(device->CreateUnorderedAccessView(unordered.Get(), nullptr, &uav)), "test UAV creation failed");
            rejectChangedBinding([&]{
                context->OMSetRenderTargetsAndUnorderedAccessViews(1, rt.GetAddressOf(), ds.Get(), 63, 1, uav.GetAddressOf(), nullptr);
            }, "high-slot UAV allowed focus reuse");
            ID3D11UnorderedAccessView* none = nullptr;
            context->OMSetRenderTargetsAndUnorderedAccessViews(1, rt.GetAddressOf(), ds.Get(), 63, 1, &none, nullptr);
        }
    }
    void lightingCube() {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 16;
        desc.ArraySize = 6;
        desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        ComPtr<ID3D11Texture2D> cube;
        ComPtr<ID3D11ShaderResourceView> view;
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &cube)) &&
              SUCCEEDED(device->CreateShaderResourceView(cube.Get(), nullptr, &view)), "lighting cube creation failed");
        auto code = compile("TextureCube irradDiffuse:register(t0); SamplerState samp:register(s0);"
                            "float4 main():SV_Target { return irradDiffuse.SampleLevel(samp,float3(0,1,0),0); }", "ps_5_0");
        ComPtr<ID3D11PixelShader> shader;
        check(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)), "lighting shader creation failed");
        context->PSSetShader(shader.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, view.GetAddressOf());
        outerDraw(true); mvr::end_outer(resolved.Get());
        check(mvr::begin_focus(), "LMU irradiance cubemap prevented focus reuse");
        focusResolve(); check(mvr::end_focus(), "lighting cube focus was not reused");
        pixels(focusResolved.Get(), focusW, focusH, green);
        auto otherCode = compile("TextureCube unrelated:register(t0); SamplerState samp:register(s0);"
                                 "float4 main():SV_Target { return unrelated.SampleLevel(samp,float3(0,1,0),0); }", "ps_5_0");
        ComPtr<ID3D11PixelShader> other;
        check(SUCCEEDED(device->CreatePixelShader(otherCode->GetBufferPointer(), otherCode->GetBufferSize(), nullptr, &other)), "other cube shader creation failed");
        rejectChangedBinding([&]{ context->PSSetShader(other.Get(), nullptr, 0); },
                             "changing shader let an unrelated renderable cube reuse irradiance permission");
        ID3D11ShaderResourceView* empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
        context->PSSetShader(ps.Get(), nullptr, 0);
    }
#ifdef LMU_MVR_SHADOW_PROBE
    void compareNativeShadow() {
        auto src = compile("Texture2D<float> sShad_Cmp_Texture:register(t11);"
                           "cbuffer Shadows:register(b2){row_major float4x4 worldToShadow;};"
                           "float4 main():SV_Target{return sShad_Cmp_Texture.Load(int3(0,0,0))*worldToShadow[0];}", "ps_5_0");
        ComPtr<ID3D11PixelShader> shader;
        auto image=texture(16,16,DXGI_FORMAT_R32_FLOAT,1,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        ComPtr<ID3D11ShaderResourceView> view;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=64;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> matrix;
        check(SUCCEEDED(device->CreatePixelShader(src->GetBufferPointer(),src->GetBufferSize(),nullptr,&shader))&&
              SUCCEEDED(device->CreateShaderResourceView(image.Get(),nullptr,&view))&&
              SUCCEEDED(device->CreateBuffer(&desc,nullptr,&matrix)),"could not create shadow audit inputs");
        context->PSSetShader(shader.Get(),nullptr,0);
        context->PSSetShaderResources(11,1,view.GetAddressOf());
        context->PSSetConstantBuffers(2,1,matrix.GetAddressOf());
        float values[16]{};
        context->UpdateSubresource(matrix.Get(),0,nullptr,values,0,0);
        outerDraw(true);mvr::end_outer(resolved.Get());
        check(!mvr::begin_focus(),"audit permitted an unsafe focus reuse");
        values[0]=2;
        context->UpdateSubresource(matrix.Get(),0,nullptr,values,0,0);
        context->Draw(3,0);
        mvr::finish_shadow_probe();
        check(matchedShadowResource&&detectedChangedMatrix,"probe missed an in-place shadow matrix change");
        context->PSSetShader(ps.Get(),nullptr,0);
    }
#endif
    void bindingPath() {
        const float cyan[4]={0,1,1,1},dimmer[4]={0,.90f,.90f,1};
        D3D11_QUERY_DESC desc{D3D11_QUERY_OCCLUSION,0};
        ComPtr<ID3D11Query> query;
        check(SUCCEEDED(device->CreateQuery(&desc,&query)),"query creation failed");
        ComPtr<ID3D11DeviceContext1> ctx1;
        check(SUCCEEDED(context.As(&ctx1)),"fixture needs D3D11.1 constant-buffer ranges");
        D3D11_BUFFER_DESC bufDesc{};bufDesc.ByteWidth=1024;bufDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> ranged;
        check(SUCCEEDED(device->CreateBuffer(&bufDesc,nullptr,&ranged)),"ranged constant buffer");
        const UINT first=16,count=16;
        auto checkRange=[&](){
            ComPtr<ID3D11Buffer> actual;
            UINT start=0,length=0;
            ctx1->VSGetConstantBuffers1(13,1,&actual,&start,&length);
            check(actual.Get()==ranged.Get()&&start==first&&length==count,"game b13 range changed");
        };
        context->VSSetConstantBuffers(13,1,appCb.GetAddressOf());
        for(int frame=-1;frame<5;++frame){
            mvr::discard();
            context->OMSetRenderTargets(1,rt.GetAddressOf(),ds.Get());
            mvr::begin_outer();
            // Partial viewports and scissors map from the original atlas origin.
            const D3D11_VIEWPORT inset{4,3,8,6,0,1};
            const D3D11_RECT insetRect{4,3,12,9};
            context->RSSetViewports(1,&inset);
            context->RSSetScissorRects(1,&insetRect);
            UINT nv=2,ns=2;D3D11_VIEWPORT viewports[2]{};D3D11_RECT scissors[2]{};
            context->RSGetViewports(&nv,viewports);context->RSGetScissorRects(&ns,scissors);
            check(nv==2 && ns==2 && viewports[1].TopLeftX==18 && viewports[1].TopLeftY==2 &&
                  scissors[1].left==18 && scissors[1].top==2 && scissors[1].right==22 && scissors[1].bottom==6,
                  "partial scene escaped its focus atlas region");
            context->RSSetViewports(1,&originalViewport);
            context->RSSetScissorRects(1,&originalScissor);
            ComPtr<ID3D11Buffer> remap;
            context->VSGetConstantBuffers(13,1,&remap);
            check(remap && remap.Get()!=appCb.Get() && remap.Get()!=ranged.Get(),"occupied b13 excluded a compatible shader");
            ctx1->VSSetConstantBuffers1(13,1,ranged.GetAddressOf(),&first,&count);
            ComPtr<ID3D11Buffer> updated;
            context->VSGetConstantBuffers(13,1,&updated);
            check(updated==remap,"game range update displaced the active remap");
            context->ClearRenderTargetView(rt.Get(),cyan);
            context->ClearDepthStencilView(ds.Get(),D3D11_CLEAR_DEPTH,1,0);
            auto before=disabled;
            context->Begin(query.Get());
            check(disabled==before+1,"occlusion draw must stay single view");
            checkRange();
            context->End(query.Get());
            context->OMSetRenderTargets(0,nullptr,nullptr);
            context->OMSetRenderTargets(1,rt.GetAddressOf(),ds.Get());
            context->Draw(3,0);
            context->ResolveSubresource(resolved.Get(),0,hdr.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
            mvr::end_outer(resolved.Get());
            preserved(ranged.Get());
            checkRange();
            const bool reuse=mvr::begin_focus();
            check(reuse==(frame>=3),"must reject a different reference then pass two same-frame checks");
            if(frame==-1){
                // Complete a qualification pass with no matching native resolve.
                mvr::discard();
                const char* reason=mvr::take_fallback_reason();
                check(reason && std::strstr(reason,"no matching native focus resolve for validation"),
                      "an unobservable validation resolve left MVR waiting with no diagnostic");
                continue;
            }
            if(frame==4){
                // LMU sometimes omits its usual focus resolve. The game will render it again;
                // this eye needs a new validation, but the other eye must remain eligible.
                check(!mvr::end_focus() && mvr::available(),"missing resolve disabled the other eye");
                check(!mvr::candidate(0) && mvr::candidate(1),"missing resolve did not isolate the failed eye");
                break;
            }
            context->ClearRenderTargetView(focusRt.Get(),frame==0?dimmer:cyan);
            context->ResolveSubresource(focusResolved.Get(),0,focusHdr.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
            if(reuse)check(mvr::end_focus(),"matched scene missed focus resolve");
        }
    }
    void missingValidation() {
        for (unsigned frame=0;frame<45;++frame) {
            check(mvr::candidate(0),"qualification backed off before 45 missing resolves");
            outerDraw(true);
            mvr::end_outer(resolved.Get());
            check(!mvr::begin_focus(),"unqualified focus suppressed native drawing");
            mvr::discard();
        }
        check(!mvr::candidate(0)&&mvr::candidate(1),"missing reference should back off only the affected eye");
    }
    void instancedPath() {
        check(mvr::uses_instancing(), "device did not select D3D11 instancing");
        auto v = compile(R"(
            struct Output { float4 position:SV_Position; float2 color:TEXCOORD0; };
            Output main(float value:VALUE, uint vertex:SV_VertexID, uint instance:SV_InstanceID) {
                Output result;
                result.color = float2(value,instance);
                result.position = float4(vertex==1?3:-1,vertex==2?3:-1,0,1);
                return result;
            })","vs_5_0");
        auto p = compile("float4 main(float4 pos:SV_Position, float2 color:TEXCOORD0):SV_Target{return float4(color,0,1);}","ps_5_0");
        ComPtr<ID3D11VertexShader> vertex;
        ComPtr<ID3D11PixelShader> pixel;
        ComPtr<ID3D11InputLayout> layout;
        const D3D11_INPUT_ELEMENT_DESC desc{"VALUE",0,DXGI_FORMAT_R32_FLOAT,0,0,D3D11_INPUT_PER_INSTANCE_DATA,1};
        check(SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex)) &&
              SUCCEEDED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel)) &&
              SUCCEEDED(device->CreateInputLayout(&desc,1,v->GetBufferPointer(),v->GetBufferSize(),&layout)),
              "instanced fixture shader/layout failed");
        const float values[]{.25f,.5f,.75f,1.f};
        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth=sizeof(values);bufferDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{values};
        ComPtr<ID3D11Buffer> buffer;
        check(SUCCEEDED(device->CreateBuffer(&bufferDesc,&data,&buffer)),"instance buffer");
        const UINT stride=sizeof(float),offset=0;
        context->IASetInputLayout(layout.Get());
        context->IASetVertexBuffers(0,1,buffer.GetAddressOf(),&stride,&offset);
        context->VSSetShader(vertex.Get(),nullptr,0);
        context->PSSetShader(pixel.Get(),nullptr,0);
        const uint16_t indices[]{0,1,2};
        bufferDesc.ByteWidth=sizeof(indices);bufferDesc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        data.pSysMem=indices;
        ComPtr<ID3D11Buffer> indexBuffer;
        check(SUCCEEDED(device->CreateBuffer(&bufferDesc,&data,&indexBuffer)),"index buffer");
        context->IASetIndexBuffer(indexBuffer.Get(),DXGI_FORMAT_R16_UINT,0);
        D3D11_DEPTH_STENCIL_DESC z{};
        z.DepthFunc=D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> noDepth;
        check(SUCCEEDED(device->CreateDepthStencilState(&z,&noDepth)),"depth state");
        context->OMSetDepthStencilState(noDepth.Get(),0);

        auto d=D3D11_TEXTURE2D_DESC{};
        resolved->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"atlas staging");
        const float black[4]{},nativeColor[]{.5f,1,0,1};
        auto sample=compile("Texture2D<float4> t:register(t0);float4 main():SV_Target{return t.Load(int3(0,0,0));}","ps_5_0");
        ComPtr<ID3D11PixelShader> material;
        check(SUCCEEDED(device->CreatePixelShader(sample->GetBufferPointer(),sample->GetBufferSize(),nullptr,&material)),"material shader");
        ComPtr<ID3D11ShaderResourceView> views[2];
        const uint16_t texel[]{0x3800,0x3c00,0,0x3c00};
        for(unsigned i=0;i<2;++i) {
            auto t=texture(1,1,DXGI_FORMAT_R16G16B16A16_FLOAT,1,D3D11_BIND_SHADER_RESOURCE | (i?D3D11_BIND_RENDER_TARGET:0));
            context->UpdateSubresource(t.Get(),0,nullptr,texel,sizeof(texel),sizeof(texel));
            check(SUCCEEDED(device->CreateShaderResourceView(t.Get(),nullptr,&views[i])),"material SRV");
        }
        for(unsigned frame=0;frame<9;++frame){
            mvr::discard();
            context->OMSetRenderTargets(1,rt.GetAddressOf(),ds.Get());
            mvr::begin_outer();
            context->ClearRenderTargetView(rt.Get(),black);
            context->ClearDepthStencilView(ds.Get(),D3D11_CLEAR_DEPTH,1,0);
            if(frame==4) {
                // Weather/material changes can introduce a new shader *after*
                // the two reference comparisons qualified this eye.
                auto dxbc=compile("float4 main(float4 p:SV_Position):SV_Target{return float4(p.x/16,0,0,1);}","ps_5_0");
                ComPtr<ID3D11PixelShader> weather;
                check(SUCCEEDED(device->CreatePixelShader(dxbc->GetBufferPointer(),dxbc->GetBufferSize(),nullptr,&weather)),"late screen shader");
                context->PSSetShader(weather.Get(),nullptr,0);
            }else if(frame>=6){
                context->PSSetShader(material.Get(),nullptr,0);
                context->PSSetShaderResources(0,1,views[frame==8].GetAddressOf());
            }else context->PSSetShader(pixel.Get(),nullptr,0);
            if (frame==0) {
                context->Draw(3,0);
                context->Draw(3,0); // reuse the same layout metadata
            }
            else if (frame==1) context->DrawIndexed(3,0,0);
            else if (frame==2) context->DrawInstanced(3,2,0,0);
            else context->DrawIndexedInstanced(3,2,0,0,frame==5 ? 1 : 0);
            ComPtr<ID3D11InputLayout> restored;
            context->IAGetInputLayout(&restored);
            check(restored==layout,"game input layout not restored");
            context->ResolveSubresource(resolved.Get(),0,hdr.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
            mvr::end_outer(resolved.Get());
            context->CopyResource(staging.Get(),resolved.Get());
            D3D11_MAPPED_SUBRESOURCE map{};
            check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"atlas map");
            const auto* row=static_cast<const uint16_t*>(map.pData);
            const uint16_t expectedR=frame<2 ? 0x3400 : 0x3800; // 0.25 or 0.5
            const uint16_t expectedG=frame<2 ? 0 : 0x3c00; // native SV_InstanceID 0 or 1
            if(frame<4 || frame==6 || frame==7){
                for(UINT x=0;x<outerW+focusW;++x){
                    if (row[4*x]!=expectedR || row[4*x+1]!=expectedG)
                        std::fprintf(stderr,"frame %u x %u expected %x,%x got %x,%x\n",frame,x,expectedR,expectedG,row[4*x],row[4*x+1]);
                    check(row[4*x]==expectedR && row[4*x+1]==expectedG,
                          "instanced render changed native instance data or failed to shade both viewports");
                }
            }else if(frame==5){
                check(row[0]==0x3a00 && !row[4*outerW],"unsupported base instance corrupted native draw or paired atlas");
            }else if(frame==8){
                check(row[0]==0x3800 && row[4*outerW]==0,
                      "same shader sampled a newly bound renderable texture in the focus viewport");
            }else{
                check(row[0]!=0 && row[4*outerW]==0,
                      "late SV_Position.xy reader drew in the focus region after qualification");
            }
            context->Unmap(staging.Get(),0);
            // Compare natural scene variation per frame with its own native focus.
            float match[4]{nativeColor[0],nativeColor[1],0,1};
            if(frame<2){match[0]=.25f;match[1]=0;}
            const bool reuse=mvr::begin_focus();
            check(reuse==(frame==2||frame==3),"instancing bypassed validation or failed native fallback");
            context->ClearRenderTargetView(focusRt.Get(),match);
            context->ResolveSubresource(focusResolved.Get(),0,focusHdr.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
            if(reuse)check(mvr::end_focus(),"validated instanced focus was not substituted");
        }
    }
    void run() {
        outerDraw(true); preserved(nullptr);
        pixels(mvr::end_outer(resolved.Get()), outerW, outerH, red);
        check(enabled == 1 && disabled == 1 && mvr::begin_focus() && mvr::skipping(), "complete pass was not reusable");
        focusResolve(); check(mvr::end_focus(), "focus resolve was not replaced"); pixels(focusResolved.Get(), focusW, focusH, green);

        context->VSSetConstantBuffers(13, 1, appCb.GetAddressOf());
        outerDraw(true); preserved(appCb.Get()); mvr::end_outer(resolved.Get());
        check(enabled == 1 && !mvr::begin_focus() && !mvr::skipping(), "occupied b13 was overwritten");
        focusResolve(); check(!mvr::end_focus(), "fallback suppressed native focus"); pixels(focusResolved.Get(), focusW, focusH, blue);
        ID3D11Buffer* empty = nullptr; context->VSSetConstantBuffers(13, 1, &empty);

        context->PSSetShader(screenSpacePs.Get(), nullptr, 0);
        outerDraw(true); mvr::end_outer(resolved.Get());
        check(enabled == 1 && !mvr::begin_focus(), "screen-space shader incorrectly accepted for second viewport");
        focusResolve(); mvr::end_focus(); pixels(focusResolved.Get(), focusW, focusH, blue);
        context->PSSetShader(ps.Get(), nullptr, 0);

        changedBindings();
        lightingCube();
#ifdef LMU_MVR_SHADOW_PROBE
        compareNativeShadow();
#endif
        outerDraw(false); mvr::end_outer(resolved.Get());
        check(!mvr::begin_focus(), "missing outer resolve reused stale focus");
        focusResolve(); mvr::end_focus(); pixels(focusResolved.Get(), focusW, focusH, blue);
        outerDraw(true, false); mvr::end_outer(resolved.Get());
        check(!mvr::begin_focus(), "uncleared depth allowed stale focus reuse");
        focusResolve(); mvr::end_focus(); pixels(focusResolved.Get(), focusW, focusH, blue);
        outerDraw(true); mvr::end_outer(resolved.Get());
        check(mvr::begin_focus() && !mvr::end_focus() && !mvr::available(), "missing focus resolve did not disable reuse");
        check(std::strstr(lastStop,"no focus resolve issued") && std::strstr(lastStop,"eye=0") &&
              std::strstr(lastStop,"outer_passes=") && std::strstr(lastStop,"focus_reused="),
              "missing resolve stopped MVR without its reason and pass totals");
        preserved(nullptr);
    }
};
}
namespace lmuqv {
void log_line(const char* text) {
#ifdef LMU_MVR_SHADOW_PROBE
    if(std::strstr(text,"mvr_shadow_compare")&&std::strstr(text,"same_resource=1 same_srv=1"))matchedShadowResource=true;
    if(std::strstr(text,"name=PS.Shadows.worldToShadow bytes=64 verdict=different"))detectedChangedMatrix=true;
#endif
    if(std::strstr(text,"focus_mvr stopped"))std::snprintf(lastStop,sizeof(lastStop),"%s",text);
    std::puts(text);
}
namespace nv_multiview {
const RenderApi& driver_render_api() {
    static const RenderApi api{{initialize, sps, nullptr}, createTwin, nullptr, stereoMode, nullptr};
    return api;
}
}
}
int main(int argc,char** argv) {
    try {
        check(MH_Initialize() == MH_OK, "MinHook initialization failed");
        if (argc==2 && std::strcmp(argv[1],"--instanced-hardware")==0) {
            ComPtr<IDXGIFactory1> factory;
            check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory");
            unsigned tested=0;
            bindingTest=true;
            for (UINT i=0;;++i) {
                ComPtr<IDXGIAdapter1> adapter;
                auto hr=factory->EnumAdapters1(i,&adapter);
                if(hr==DXGI_ERROR_NOT_FOUND)break;
                check(SUCCEEDED(hr),"DXGI adapter enumeration failed");
                DXGI_ADAPTER_DESC1 desc{};
                check(SUCCEEDED(adapter->GetDesc1(&desc)),"DXGI adapter description");
                if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
                std::printf("Testing viewport instancing on %ls (vendor %04x)\n",desc.Description,desc.VendorId);
                {Fixture test(true,adapter.Get());test.instancedPath();}
                ++tested;
            }
            check(tested>0,"no hardware adapter tested");
            check(MH_Uninitialize()==MH_OK,"MinHook cleanup failed");
            std::printf("Viewport instancing passed on %u physical adapters.\n",tested);
            return 0;
        }
        factoryEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        releaseFactory=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        check(mvr::install_early_factories(delayedFactory,D3D11CreateDeviceAndSwapChain),"early factories did not arm");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        HRESULT factoryResult=E_FAIL;
        std::thread creator([&]{factoryResult=delayedFactory(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);});
        const auto entered=WaitForSingleObject(factoryEntered,10000);
        mvr::stop();
        const bool rearmedDuringFactory=mvr::early();
        SetEvent(releaseFactory);creator.join();
        CloseHandle(factoryEntered);CloseHandle(releaseFactory);
        check(entered==WAIT_OBJECT_0&&SUCCEEDED(factoryResult),"delayed native factory failed");
        check(!rearmedDuringFactory,"rearmed while a previous factory callback was still executing");
        check(mvr::early(),"factory completion reinstalled hooks after teardown");
        mvr::stop();
        check(mvr::start(context.Get(),true),"shader lifetime fixture did not arm");
        twinEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        releaseTwin=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        const auto shader=compile("float4 main(float4 p:POSITION):SV_Position{return p;}","vs_5_0");
        ComPtr<ID3D11VertexShader> gameShader,staleTwin;
        delayTwin.store(true);
        HRESULT shaderResult=E_FAIL;
        std::thread shaderThread([&]{shaderResult=device->CreateVertexShader(shader->GetBufferPointer(),
            shader->GetBufferSize(),nullptr,&gameShader);});
        const auto converting=WaitForSingleObject(twinEntered,10000);
        mvr::stop();
        const bool restartedDuringConversion=mvr::start(context.Get(),true);
        SetEvent(releaseTwin);shaderThread.join();
        delayTwin.store(false);CloseHandle(twinEntered);CloseHandle(releaseTwin);
        check(converting==WAIT_OBJECT_0&&SUCCEEDED(shaderResult),"delayed native shader failed");
        check(!restartedDuringConversion,"rearmed the same device while its expired conversion was still running");
        UINT twinSize=sizeof(ID3D11VertexShader*);
        const GUID twinKey={0x7468456a,0xf4af,0x49b6,{0x88,0x11,0x42,0x93,0xe7,0x0d,0x81,0x22}};
        check(FAILED(gameShader->GetPrivateData(twinKey,&twinSize,staleTwin.GetAddressOf())),
              "expired callback published a shader twin after teardown");
        twins=enabled=disabled=0;
        { Fixture test; test.run(); }
#ifndef LMU_MVR_SHADOW_PROBE
        bindingTest=true;twins=enabled=disabled=0;
        { Fixture test; test.bindingPath(); }
        twins=enabled=disabled=0;
        { Fixture test; test.missingValidation(); }
        twins=enabled=disabled=0;
        { Fixture test(true); test.instancedPath(); }
        supported=true;
#endif
        check(MH_Uninitialize() == MH_OK, "MinHook cleanup failed");
        std::puts("WARP focus pipeline passed: dual-region readback, fallback resolves and D3D11 state preservation.");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
