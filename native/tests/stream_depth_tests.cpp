#include "channel.hpp"
#include "cockpit_depth.hpp"
#include "stream_depth.hpp"
#include <wrl/client.h>
#include <cmath>
#include <iostream>
#include <vector>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
void check(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}
struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    explicit Gpu(UINT flags = 0) {
        check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
              D3D11_SDK_VERSION, &device, nullptr, &context)), "create WARP device");
    }
};
ComPtr<ID3D11ShaderResourceView> makeSource(const Gpu& gpu) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 4; desc.Height = 3; desc.ArraySize = 3;
    desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R16_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const uint16_t pixels[3][12] = {
        {}, {}, {0,0,0,0, 0,8192,16384,0, 0,32768,65535,0}
    };
    D3D11_SUBRESOURCE_DATA data[3]{};
    for (UINT i=0; i<3; ++i) data[i] = {pixels[i], 8, 0};
    ComPtr<ID3D11Texture2D> tex;
    check(SUCCEEDED(gpu.device->CreateTexture2D(&desc, data, &tex)), "create depth array");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = 1; srv.Texture2DArray.FirstArraySlice = 1;
    srv.Texture2DArray.ArraySize = 2;
    ComPtr<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(gpu.device->CreateShaderResourceView(tex.Get(), &srv, &view)), "create slice range");
    return view;
}
void checkPixels(const Gpu& gpu, ID3D11ShaderResourceView* view) {
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> tex;
    check(SUCCEEDED(resource.As(&tex)), "consumer view is a texture");
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    check(d.Width == 2 && d.Height == 2 && d.Format == DXGI_FORMAT_R32_FLOAT && !d.MiscFlags,
          "consumer must own a private R32_FLOAT crop");
    d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.Usage = D3D11_USAGE_STAGING;
    ComPtr<ID3D11Texture2D> staging;
    check(SUCCEEDED(gpu.device->CreateTexture2D(&d, nullptr, &staging)), "create staging");
    gpu.context->CopyResource(staging.Get(), tex.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    check(SUCCEEDED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "map result");
    const auto* first = static_cast<const float*>(map.pData);
    const auto* second = reinterpret_cast<const float*>(static_cast<const char*>(map.pData) + map.RowPitch);
    const bool matches = std::abs(first[0] - 8192.f/65535) < 1e-6f &&
        std::abs(first[1] - 16384.f/65535) < 1e-6f &&
        std::abs(second[0] - 32768.f/65535) < 1e-6f && second[1] == 1;
    gpu.context->Unmap(staging.Get(), 0);
    check(matches, "wrong eye, crop, or depth normalization");
}
}
int main() try {
    Gpu producer, consumer;
    auto source = makeSource(producer);
    og::CockpitDepthEye eye{};
    eye.view = source.Get(); eye.slice = 1; eye.rect = {{1,1},{2,2}};
    eye.fov = {-.7f, .5f, .6f, -.4f}; eye.minDepth = .1f; eye.maxDepth = .9f;
    eye.invNear = 0; eye.invFar = 10;
    producer.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    producer.context->PSSetShaderResources(3, 1, source.GetAddressOf());
    og::StreamDepthPublisher writer;
    og::StreamDepthReader reader;
    og::CapturedDepth metadata{};
    auto publish = [&] { return writer.publish(producer.device.Get(), producer.context.Get(), eye, metadata); };
    auto consume = [&](const og::CapturedDepth& frame, bool restart = false) {
        return reader.consume(consumer.device.Get(), consumer.context.Get(), frame, restart);
    };
    check(publish(), "publish first crop");
    check(metadata.texture && metadata.generation && metadata.width == 2 && metadata.height == 2 &&
          std::abs(metadata.fov[0] - std::tan(-.7f)) < 1e-6f &&
          std::abs(metadata.fov[2] - std::tan(.6f)) < 1e-6f && metadata.invNear == 0 &&
          metadata.invFar == 10 && metadata.minDepth == .1f && metadata.maxDepth == .9f,
          "wrong stream projection/range metadata");
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    ComPtr<ID3D11ShaderResourceView> saved;
    producer.context->IAGetPrimitiveTopology(&topology);
    producer.context->PSGetShaderResources(3, 1, &saved);
    check(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST && saved.Get() == source.Get(), "changed producer state");
    check(consume(metadata, true) && reader.view(), "reader opens cross-device texture");
    checkPixels(consumer, reader.view());
    check(!consume(metadata) && !reader.view(), "mutex timeout leaked prior depth");
    check(publish(), "publisher reuses consumed texture");
    const auto outstanding = metadata;
    ComPtr<ID3D11Texture2D> pinned;
    ComPtr<IDXGIKeyedMutex> held;
    check(SUCCEEDED(consumer.device->OpenSharedResource(reinterpret_cast<HANDLE>(metadata.texture),
          IID_PPV_ARGS(&pinned))) && SUCCEEDED(pinned.As(&held)) &&
          held->AcquireSync(1, 0) == S_OK, "hold in-flight depth");
    check(!publish() && !metadata.texture, "publisher must drop busy depth");
    check(!publish(), "held depth must not cause a replacement allocation");
    check(held->ReleaseSync(0) == S_OK, "consumer releases held depth");
    check(publish() && metadata.generation == outstanding.generation, "reuse released allocation");
    check(publish() && metadata.generation == outstanding.generation, "reclaim unconsumed sample");
    auto wrongSize = metadata; ++wrongSize.width;
    check(!consume(wrongSize) && !reader.view(), "accepting mismatched texture dimensions");
    check(consume(metadata), "consume recovered texture");
    check(!consume({}) && !reader.view(), "missing metadata reused prior depth");
    check(publish(), "publish before abandoned frame");
    writer.reset();
    check(publish() && consume(metadata, true), "restart after undisplayed frame");
    reader.reset();
    check(!reader.view(), "reader reset retains view");
    eye.slice = 2;
    check(!publish() && !metadata.texture, "invalid relative slice was published");
    Gpu singleThreaded(D3D11_CREATE_DEVICE_SINGLETHREADED);
    auto otherSource = makeSource(singleThreaded);
    eye.slice = 1; eye.view = otherSource.Get();
    check(writer.publish(singleThreaded.device.Get(), singleThreaded.context.Get(), eye, metadata) &&
          consume(metadata, true), "single-threaded game device");
    checkPixels(consumer, reader.view());
    // Supersampled eye with a nonzero crop offset. The last two pixels of the
    // downsampled image address source texels 2558 and 2560, respectively.
    D3D11_TEXTURE2D_DESC large{};
    large.Width = 2562; large.Height = 4; large.ArraySize = large.MipLevels = large.SampleDesc.Count = 1;
    large.Format = DXGI_FORMAT_R16_UNORM; large.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<uint16_t> samples(large.Width * large.Height);
    for (UINT y = 0; y < large.Height; ++y) samples[y * large.Width + 2560] = 65535;
    D3D11_SUBRESOURCE_DATA data{samples.data(), large.Width * 2, 0};
    ComPtr<ID3D11Texture2D> texture;
    check(SUCCEEDED(producer.device->CreateTexture2D(&large, &data, &texture)), "large depth image");
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = large.Format; desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    desc.Texture2DArray.MipLevels = desc.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> largeView;
    check(SUCCEEDED(producer.device->CreateShaderResourceView(texture.Get(), &desc, &largeView)), "large eye view");
    eye.view = largeView.Get(); eye.slice = 0; eye.rect = {{1,0},{2560,4}};
    check(publish() && metadata.width == 1280 && metadata.height == 2 && consume(metadata,true), "bounded depth transfer");
    ComPtr<ID3D11Resource> copied;
    reader.view()->GetResource(&copied);
    D3D11_TEXTURE2D_DESC readback{};
    readback.Width = 2; readback.Height = readback.MipLevels = readback.ArraySize = readback.SampleDesc.Count = 1;
    readback.Format = DXGI_FORMAT_R32_FLOAT; readback.Usage = D3D11_USAGE_STAGING;
    readback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(SUCCEEDED(consumer.device->CreateTexture2D(&readback,nullptr,&staging)), "readback allocation");
    D3D11_BOX box{1278,0,0,1280,1,1};
    consumer.context->CopySubresourceRegion(staging.Get(),0,0,0,0,copied.Get(),0,&box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(consumer.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)), "downsample readback");
    const auto* result = static_cast<const float*>(mapped.pData);
    const bool aligned = result[0] == 0 && result[1] == 1;
    consumer.context->Unmap(staging.Get(),0);
    check(aligned, "scaled depth covers the entire offset source rectangle");
    std::cout << "Stream depth crop, conversions, handoff, reset and fallback passed\n";
    return 0;
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
