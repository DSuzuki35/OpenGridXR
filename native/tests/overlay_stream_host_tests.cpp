// Exercise the production helper's GPU path without a desktop application or game.
#define main overlay_host_program_main
#include "../src/overlay_host.cpp"
#undef main
#include <stdexcept>
#include <vector>

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
og::OverlayCommand parse(const std::string &json) {
    og::OverlayCommand command;
    std::string error;
    check(og::parse_overlay_command(json, command, error), "fixture command parses");
    return command;
}
uint32_t read(Host &host, const og::OverlayFrame &frame) {
    ComPtr<ID3D11Texture2D> source, staging;
    ComPtr<IDXGIKeyedMutex> key;
    check(SUCCEEDED(host.device1->OpenSharedResource1(reinterpret_cast<HANDLE>(frame.handle),
                                                      IID_PPV_ARGS(&source))) &&
              SUCCEEDED(source.As(&key)),
          "independent stream shared texture opens");
    check(key->AcquireSync(1, 1000) == S_OK, "stream owns its independent consumer key");
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.BindFlags = desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(host.device->CreateTexture2D(&desc, nullptr, &staging)),
          "stream GPU readback staging");
    host.context->CopyResource(staging.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(host.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
          "stream GPU pixel readback");
    auto pixel = *static_cast<uint32_t *>(mapped.pData);
    host.context->Unmap(staging.Get(), 0);
    key->ReleaseSync(0);
    return pixel;
}
int main(int argc, char **argv) {
    try {
        Host host;
        host.game = "iracing";
        host.parent = GetCurrentProcessId();
        host.testNamespace = true;
        check(host.initialize(), "isolated helper initializes");
        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;
        check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "test adapter factory");
        const std::string wanted = argc > 1 ? argv[1] : "";
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            if (factory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 info{};
            candidate->GetDesc1(&info);
            const std::wstring wideName = info.Description;
            const std::string name(wideName.begin(), wideName.end());
            if (!(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                (wanted.empty() || name.find(wanted) != std::string::npos)) {
                adapter = candidate;
                std::cout << "Adapter: " << name << '\n';
                break;
            }
        }
        check(bool(adapter), "requested test hardware adapter");
        DXGI_ADAPTER_DESC1 adapterDesc{};
        adapter->GetDesc1(&adapterDesc);
        check(host.channel.lock(), "receiver fixture lock");
        auto *data = host.channel.data();
        data->consumerPid = GetCurrentProcessId();
        data->consumerBeat = GetTickCount64();
        data->adapterLow = adapterDesc.AdapterLuid.LowPart;
        data->adapterHigh = adapterDesc.AdapterLuid.HighPart;
        data->slots = {};
        data->streamConsumerPid = 0;
        data->streamConsumerBeat = 0;
        host.channel.unlock();
        check(host.connect(), "helper uses receiver GPU");
        host.command(parse(R"({"op":"configure","slot":0,"enabled":true,"showInStream":false})"));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 32;
        desc.Height = 16;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        std::vector<uint32_t> pixels(32 * 16, 0x80402010);
        D3D11_SUBRESOURCE_DATA initial{pixels.data(), 32 * 4, 0};
        ComPtr<ID3D11Texture2D> source;
        ComPtr<IDXGIResource1> resource;
        HANDLE handle{};
        check(SUCCEEDED(host.device->CreateTexture2D(&desc, &initial, &source)) &&
                  SUCCEEDED(source.As(&resource)) &&
                  SUCCEEDED(resource->CreateSharedHandle(
                      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                      &handle)),
              "Electron-like GPU NT texture");
        host.command(parse("{\"op\":\"frame\",\"slot\":0,\"frameId\":17,\"handle\":\"" +
                           std::to_string(reinterpret_cast<uint64_t>(handle)) +
                           "\",\"width\":32,\"height\":16,\"format\":\"bgra\"}"));
        for (unsigned i = 0; i < 200 && host.pending[0]; ++i) {
            host.tick();
            Sleep(2);
        }
        check(!host.pending[0], "helper releases Electron source after GPU completion");
        CloseHandle(handle);
        check(!host.streamBuffers[0][0] && !host.streamBuffers[0][1],
              "unselected overlay has zero stream allocation");
        auto vrFrame = data->slots[0].frames[data->slots[0].current];
        check(read(host, vrFrame) == 0x80402010,
              "VR consumer gets original encoded premultiplied bytes");
        host.command(parse(R"({"op":"configure","slot":0,"enabled":true,"showInStream":true})"));
        host.tick();
        check(!host.streamBuffers[0][0] && !host.streamBuffers[0][1],
              "no Clean Output heartbeat means zero stream allocation");
        check(host.channel.lock(), "Output heartbeat lock");
        data->streamConsumerPid = GetCurrentProcessId();
        data->streamConsumerBeat = GetTickCount64();
        host.channel.unlock();
        for (unsigned i = 0;
             i < 200 && !data->slots[0].streamFrames[data->slots[0].streamCurrent].generation;
             ++i) {
            host.tick();
            Sleep(2);
        }
        auto streamFrame = data->slots[0].streamFrames[data->slots[0].streamCurrent];
        check(streamFrame.frameId == 17 && streamFrame.handle != vrFrame.handle,
              "static selection publishes independent stream ring without new Chromium paint");
        check(read(host, streamFrame) == 0x80402010,
              "stream receives exact encoded premultiplied pixels");
        check(host.buffers[0][data->slots[0].current]->key->AcquireSync(0, 0) == S_OK,
              "stream copy preserves prior VR key ownership");
        host.buffers[0][data->slots[0].current]->key->ReleaseSync(0);
        auto generation = host.generation;
        for (unsigned i = 0; i < 10; ++i)
            host.tick();
        check(host.generation == generation && !host.streamPending[0],
              "static overlay causes no repeated stream GPU copies");
        check(host.channel.lock(), "Output expiry lock");
        data->streamConsumerBeat = 0;
        host.channel.unlock();
        host.tick();
        check(!host.streamBuffers[0][0] && !host.streamBuffers[0][1] &&
                  data->slots[0].frames[data->slots[0].current].generation == vrFrame.generation,
              "expired Output heartbeat retires stream resources while VR remains alive");
        check(host.channel.lock(), "Output restart heartbeat lock");
        data->streamConsumerBeat = GetTickCount64();
        host.channel.unlock();
        for (unsigned i = 0;
             i < 200 && !data->slots[0].streamFrames[data->slots[0].streamCurrent].generation;
             ++i) {
            host.tick();
            Sleep(2);
        }
        check(read(host, data->slots[0].streamFrames[data->slots[0].streamCurrent]) == 0x80402010,
              "Output restart receives static pixels without new producer frame");
        host.command(parse(R"({"op":"configure","slot":0,"enabled":true,"showInStream":false})"));
        host.tick();
        check(!host.streamBuffers[0][0] && !host.streamBuffers[0][1] &&
                  data->slots[0].streamFrames[0].generation == 0 &&
                  data->slots[0].streamFrames[1].generation == 0,
              "stream deselection retires all stream buffers");
        check(data->slots[0].frames[data->slots[0].current].generation == vrFrame.generation,
              "stream deselection leaves VR independent");
        host.command(parse(R"({"op":"configure","slot":0,"enabled":true,"showInStream":true})"));
        host.tick();
        check(bool(host.streamPending[0]), "static reselect begins retained-pixel copy");
        host.command(parse(R"({"op":"clear","slot":0})"));
        for (unsigned i = 0; i < 200 && host.streamPending[0]; ++i) {
            host.tick();
            Sleep(2);
        }
        check(!host.streamPending[0] && !host.streamBuffers[0][0] && !host.streamBuffers[0][1] &&
                  data->slots[0].streamFrames[0].generation == 0 &&
                  data->slots[0].streamFrames[1].generation == 0,
              "clear during GPU copy cannot publish old stream content");
        check(host.channel.lock(), "fixture shutdown");
        data->producerPid = 0;
        data->consumerPid = 0;
        data->streamConsumerPid = 0;
        host.channel.unlock();
        std::cout << "Helper independent stream ring GPU regression passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
