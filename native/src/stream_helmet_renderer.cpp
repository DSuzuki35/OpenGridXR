#include "stream_helmet_renderer.hpp"
#include "helmet_light.hpp"
#include "runtime_paths.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <fstream>
#include <map>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace og {
namespace {
struct Config {
    bool enabled{};
    std::string generation;
    float opacity{1};
    float ambientLight{};
    bool visorHotkey{};
    int visorPosition{};
};
// A deliberately bounded flat JSON schema. No manifest path is ever used to
// open a file: both manifest and PNG names are derived from the trusted game.
bool config(const std::string &json, const std::string &game, Config &out) {
    struct Value {
        bool quoted{};
        std::string text;
    };
    std::map<std::string, Value> fields;
    size_t p = 0;
    auto ws = [&] {
        while (p < json.size() &&
               (json[p] == ' ' || json[p] == '\r' || json[p] == '\n' || json[p] == '\t'))
            ++p;
    };
    auto quote = [&](std::string &s) {
        if (p >= json.size() || json[p++] != '"')
            return false;
        while (p < json.size() && json[p] != '"') {
            unsigned char c = json[p++];
            if (c < 32 || c == '\\' || s.size() >= 128)
                return false;
            s += char(c);
        }
        return p < json.size() && json[p++] == '"';
    };
    ws();
    if (p >= json.size() || json[p++] != '{')
        return false;
    ws();
    while (p < json.size() && json[p] != '}') {
        std::string key;
        if (!quote(key))
            return false;
        ws();
        if (p >= json.size() || json[p++] != ':')
            return false;
        ws();
        Value v;
        if (p < json.size() && json[p] == '"') {
            v.quoted = true;
            if (!quote(v.text))
                return false;
        } else {
            auto begin = p;
            while (p < json.size() && json[p] != ',' && json[p] != '}' && json[p] != ' ' &&
                   json[p] != '\t' && json[p] != '\r' && json[p] != '\n')
                ++p;
            v.text = json.substr(begin, p - begin);
            if (v.text.empty())
                return false;
        }
        if (!fields.emplace(key, v).second || fields.size() > 14)
            return false;
        ws();
        if (p < json.size() && json[p] == ',') {
            ++p;
            ws();
            if (p >= json.size() || json[p] == '}')
                return false;
        } else
            break;
    }
    if (p >= json.size() || json[p++] != '}')
        return false;
    ws();
    if (p != json.size())
        return false;
    auto eq = [&](const char *key, const std::string &value, bool quoted) {
        auto f = fields.find(key);
        return f != fields.end() && f->second.quoted == quoted && f->second.text == value;
    };
    if (!eq("version", "2", false) ||
        (!eq("enabled", "true", false) && !eq("enabled", "false", false)))
        return false;
    out.enabled = eq("enabled", "true", false);
    for (const auto &[key, v] : fields) {
        if (key == "version" || key == "enabled")
            continue;
        if (key == "leftImage" || key == "rightImage" || key == "halfImage" || key == "openImage") {
            const auto variant = key.substr(0, key.size() - 5);
            if (!v.quoted || v.text != "OpenGridXR-stream-helmet-" + game + "-" + variant + ".png")
                return false;
        } else if (key == "generation") {
            if (!v.quoted || v.text.empty() || v.text.size() > 64 ||
                !std::all_of(v.text.begin(), v.text.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-';
                }))
                return false;
            out.generation = v.text;
        } else if (key == "streamOpacity" || key == "ambientLight") {
            // This value is emitted as a JSON decimal in the range 0..100.
            if (v.quoted || v.text.empty())
                return false;
            size_t i = 0;
            auto digit = [&i, &text = v.text] { return i < text.size() && text[i] >= '0' && text[i] <= '9'; };
            if (!digit())
                return false;
            if (v.text[i] == '0')
                ++i;
            else
                while (digit())
                    ++i;
            if (i < v.text.size() && v.text[i] == '.') {
                ++i;
                if (!digit())
                    return false;
                while (digit())
                    ++i;
            }
            if (i < v.text.size() && (v.text[i] == 'e' || v.text[i] == 'E')) {
                ++i;
                if (i < v.text.size() && (v.text[i] == '+' || v.text[i] == '-'))
                    ++i;
                if (!digit())
                    return false;
                while (digit())
                    ++i;
            }
            if (i != v.text.size())
                return false;
            float n = std::strtof(v.text.c_str(), nullptr);
            if (!std::isfinite(n) || n < 0 || n > 100)
                return false;
            if (key == "streamOpacity") out.opacity = n / 100;
            else out.ambientLight = n / 100;
        } else if (key == "visorPosition") {
            if (!v.quoted || (v.text != "closed" && v.text != "half" && v.text != "open"))
                return false;
            out.visorPosition = v.text == "half" ? 1 : v.text == "open" ? 2 : 0;
        } else if (key == "visorHotkey") {
            if (v.quoted || (v.text != "false" && v.text != "true"))
                return false;
            out.visorHotkey = v.text == "true";
        } else if (key == "stereoOffset" || key == "headMotion" || key == "vehicleMotion") {
            if (v.quoted || v.text != "0")
                return false;
        } else
            return false;
    }
    return !out.enabled ||
           (!out.generation.empty() && fields.count("leftImage") && fields.count("rightImage") &&
            (!out.visorHotkey || (fields.count("halfImage") && fields.count("openImage"))));
}
struct Apartment {
    HRESULT hr{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ~Apartment() {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};
bool png(const std::filesystem::path &file, UINT &width, UINT &height,
         std::vector<unsigned char> &pixels) {
    std::error_code ec;
    auto size = std::filesystem::file_size(file, ec);
    if (ec || size < 32 || size > 32 * 1024 * 1024)
        return false;
    Apartment apartment;
    if (FAILED(apartment.hr) && apartment.hr != RPC_E_CHANGED_MODE)
        return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)))
        return false;
    GUID container{};
    UINT count{};
    if (FAILED(decoder->GetContainerFormat(&container)) || container != GUID_ContainerFormatPng ||
        FAILED(decoder->GetFrameCount(&count)) || count != 1 ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || !width ||
        !height || width > 4096 || height > 4096)
        return false;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)))
        return false;
    pixels.resize(size_t(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data())))
        return false;
    // Premultiply once before linear GPU sampling, avoiding transparent fringes.
    for (size_t i = 0; i < pixels.size(); i += 4)
        for (unsigned c = 0; c < 3; ++c)
            pixels[i + c] =
                static_cast<unsigned char>((unsigned(pixels[i + c]) * pixels[i + 3] + 127) / 255);
    return true;
}
constexpr char shader[] = R"(
Texture2D image:register(t0);Texture2D probe:register(t1);SamplerState sample0:register(s0);
cbuffer Settings:register(b0){float opacity;float ambientLight;float2 padding;};
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 ps(V v):SV_Target {
    float4 pixel=image.Sample(sample0,v.uv);
    [branch] if(ambientLight>0) {
        float luma=dot(probe.SampleLevel(sample0,v.uv,0).rgb,float3(.2126,.7152,.0722));
        float gain=clamp(.4+1.5*luma,.4,1.55);
        pixel.rgb=min(pixel.a,pixel.rgb*lerp(1,gain,ambientLight));
    }
    return pixel*opacity;
}
)";
} // namespace
struct StreamHelmetRenderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Buffer> constants;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> images;
    std::array<ULONGLONG, 3> retryAfter{};
    std::filesystem::path directory;
    std::string game, manifest;
    Config current;
    ULONGLONG lastPoll{};
    std::unique_ptr<HelmetLight> light;
    ULONGLONG lastSample{};
    bool freshLight{};
    StreamHelmetStatus report;
    void clear() {
        images = {};
        retryAfter = {};
        current = {};
        report.ready = false;
        report.visorPosition = -1;
        report.opacity = 0;
        freshLight = false;
        lastSample = 0;
    }
    ComPtr<ID3D11ShaderResourceView> sceneProbe(ID3D11Texture2D *rawEye) {
        if (!light || !rawEye) return {};
        const auto now = GetTickCount64();
        const auto elapsed = now - lastSample;
        if (!lastSample || elapsed >= 100) {
            D3D11_TEXTURE2D_DESC eye{};
            rawEye->GetDesc(&eye);
            XrSwapchainSubImage region{};
            region.imageRect.extent = {int32_t(eye.Width), int32_t(eye.Height)};
            const float weight = !freshLight || elapsed > 500 ? 1.f : 1.f - std::exp(-float(elapsed) / 180.f);
            freshLight = light->sample(context.Get(), rawEye, eye.Format, region, weight);
            lastSample = now;
        }
        return freshLight ? light->probe() : nullptr;
    }
    bool graphics() {
        if (state)
            return true;
        ComPtr<ID3D11Device1> device1;
        if (FAILED(device.As(&device1)) || FAILED(context.As(&context1)))
            return false;
        auto feature = device->GetFeatureLevel();
        UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                         ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                         : 0;
        ComPtr<ID3DDeviceContextState> next;
        if (FAILED(device1->CreateDeviceContextState(flags, &feature, 1, D3D11_SDK_VERSION,
                                                     __uuidof(ID3D11Device), nullptr, &next)))
            return false;
        ComPtr<ID3DBlob> v, p;
        if (FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "vs", "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &v, nullptr)) ||
            FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "ps", "ps_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &p, nullptr)) ||
            FAILED(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr,
                                              &vs)) ||
            FAILED(
                device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps)))
            return false;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BLEND_DESC bd{};
        auto &rt = bd.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 16;
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateSamplerState(&sd, &sampler)) ||
            FAILED(device->CreateBlendState(&bd, &blend)) ||
            FAILED(device->CreateRasterizerState(&rd, &raster)) ||
            FAILED(device->CreateBuffer(&cb, nullptr, &constants)))
            return false;
        state = next;
        return true;
    }
    std::string readManifest() const {
        const auto file = directory / ("OpenGridXR-stream-helmet-" + game + ".json");
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec || size == 0 || size > 4096)
            return {};
        std::ifstream input(file, std::ios::binary);
        std::string text(size, '\0');
        if (!input.read(text.data(), std::streamsize(size)) ||
            input.peek() != std::char_traits<char>::eof())
            return {};
        return text;
    }
    void refresh() {
        lastPoll = GetTickCount64();
        if (!device)
            return;
        try {
            auto text = readManifest();
            if (text == manifest)
                return;
            manifest = text;
            Config c;
            if (!config(text, game, c) || !c.enabled || c.opacity <= 0) {
                clear();
                return;
            }
            if (current.generation != c.generation || current.visorHotkey != c.visorHotkey)
                clear();
            current = c;
            if (current.ambientLight <= 0) {
                light.reset();
                freshLight = false;
                lastSample = 0;
            } else if (!light) {
                auto candidate = std::make_unique<HelmetLight>();
                if (candidate->initialize(device.Get())) light = std::move(candidate);
            }
        } catch (...) {
            manifest.clear();
            clear();
        }
    }
    bool load(int slot) {
        const char *variants[]{"left", "half", "open"};
        const auto file = directory / ("OpenGridXR-stream-helmet-" + game + "-" + variants[slot] + ".png");
        UINT width{}, height{};
        std::vector<unsigned char> pixels;
        // A save replaces fixed filenames between disabled and enabled manifests.
        // Verify the generation again after decoding a previously unvisited variant.
        if (!png(file, width, height, pixels) || manifest != readManifest() || !graphics())
            return false;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = width;
        d.Height = height;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
        ComPtr<ID3D11Texture2D> texture;
        return SUCCEEDED(device->CreateTexture2D(&d, &data, &texture)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &images[slot]));
    }
    void select(int liveVisor) {
        if (!current.enabled)
            return;
        const int position = current.visorHotkey && liveVisor >= 0 && liveVisor <= 2 ? liveVisor : current.visorPosition;
        const int slot = current.visorHotkey ? position : 0;
        if (!images[slot]) {
            report.ready = false;
            report.visorPosition = -1;
            report.opacity = 0;
            const auto now = GetTickCount64();
            if (now < retryAfter[slot])
                return;
            retryAfter[slot] = now + 500;
            try {
                if (!load(slot))
                    return;
            } catch (...) {
                return;
            }
            ++report.uploads;
        }
        report.ready = true;
        report.visorPosition = position;
        report.opacity = current.opacity;
    }
};
StreamHelmetRenderer::StreamHelmetRenderer() : impl_(std::make_unique<Impl>()) {}
StreamHelmetRenderer::~StreamHelmetRenderer() = default;
StreamHelmetRenderer::StreamHelmetRenderer(StreamHelmetRenderer &&) noexcept = default;
StreamHelmetRenderer &StreamHelmetRenderer::operator=(StreamHelmetRenderer &&) noexcept = default;
bool StreamHelmetRenderer::initialize(ID3D11Device *device, ID3D11DeviceContext *context,
                                      const std::string &game,
                                      const std::filesystem::path &directory) {
    reset();
    if (!device || !context || (game != "iracing" && game != "lmu"))
        return false;
    auto &p = *impl_;
    p.directory = directory;
    if (p.directory.empty()) {
        const auto file = lmuqv::win::module_path(nullptr);
        if (file.empty())
            return false;
        p.directory = std::filesystem::path(file).parent_path();
    }
    p.device = device;
    p.context = context;
    p.game = game;
    refresh();
    return true;
}
void StreamHelmetRenderer::reset() { impl_ = std::make_unique<Impl>(); }
void StreamHelmetRenderer::refresh() {
    impl_->refresh();
    if (!impl_->current.visorHotkey) impl_->select(-1);
}
StreamHelmetStatus StreamHelmetRenderer::status() const { return impl_->report; }
StreamHelmetStatus StreamHelmetRenderer::prepare(int liveVisor) {
    auto &p = *impl_;
    if (GetTickCount64() - p.lastPoll >= 500)
        p.refresh();
    p.select(liveVisor);
    return p.report;
}
void StreamHelmetRenderer::draw(ID3D11RenderTargetView *target, uint32_t width, uint32_t height,
                                ID3D11Texture2D *rawEye, int liveVisor) {
    prepare(liveVisor);
    drawPrepared(target, width, height, rawEye);
}
void StreamHelmetRenderer::drawPrepared(ID3D11RenderTargetView *target, uint32_t width, uint32_t height,
                                        ID3D11Texture2D *rawEye) {
    auto &p = *impl_;
    if (!p.report.ready || !p.device || !target || !width || !height || width > 16384 || height > 16384)
        return;
    const auto probe = p.sceneProbe(rawEye);
    ComPtr<ID3DDeviceContextState> previous;
    p.context1->SwapDeviceContextState(p.state.Get(), &previous);
    p.context->SetPredication(nullptr, FALSE);
    p.context->OMSetRenderTargets(1, &target, nullptr);
    p.context->OMSetBlendState(p.blend.Get(), nullptr, UINT_MAX);
    p.context->RSSetState(p.raster.Get());
    D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
    p.context->RSSetViewports(1, &viewport);
    p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p.context->VSSetShader(p.vs.Get(), nullptr, 0);
    p.context->PSSetShader(p.ps.Get(), nullptr, 0);
    auto *sampler = p.sampler.Get();
    ID3D11ShaderResourceView *inputs[]{p.images[p.current.visorHotkey ? p.report.visorPosition : 0].Get(), probe.Get()};
    auto *cb = p.constants.Get();
    p.context->PSSetSamplers(0, 1, &sampler);
    p.context->PSSetShaderResources(0, 2, inputs);
    p.context->PSSetConstantBuffers(0, 1, &cb);
    float constants[4]{p.report.opacity, probe ? p.current.ambientLight : 0, 0, 0};
    p.context->UpdateSubresource(cb, 0, nullptr, constants, 0, 0);
    p.context->Draw(3, 0);
    ++p.report.draws;
    ID3D11ShaderResourceView *empty[2]{};
    p.context->PSSetShaderResources(0, 2, empty);
    p.context->OMSetRenderTargets(0, nullptr, nullptr);
    p.context1->SwapDeviceContextState(previous.Get(), nullptr);
}
} // namespace og
