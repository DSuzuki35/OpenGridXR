// Compiled in engine_bridge.cpp to use the same verified loaded-image layout.
namespace lmuqv {
namespace {
using PostProcess = std::uintptr_t (*)(void*, ViewSnapshot*);
PostProcess postProcessOriginal{};
std::atomic<og::lmu::CockpitCapture> cockpitCapture{};
constexpr std::uintptr_t postProcessRva = 0x5acb50;

void observeCockpit(og::lmu::CockpitCapture capture, void* renderer, ViewSnapshot* view) {
    if (view != *address<ViewSnapshot*>(0x1a51370) || renderer != *address<void*>(0x1b12be8) ||
        !readable(view, sizeof(*view)) || view->eye_index < 0 || view->eye_index > 1 ||
        !(view->flags & 0x1000) || (view->flags & 0xc0000000u)) return;
    // A mirror or a child camera can also run postprocessing. Require both the
    // main native view invocation and the original two-eye VR scene dispatch.
    void* trace[48]{};
    const auto count = CaptureStackBackTrace(0, UINT(std::size(trace)), trace, nullptr);
    const auto has = [&](std::uintptr_t rva) {
        return std::find(trace, trace + count, address<void>(rva)) != trace + count;
    };
    if (!has(0x5de951) || !has(0xcd1763)) return;
    const auto& passes = view->main_passes;
    if (passes.end < passes.begin || passes.end-passes.begin != 0x94 ||
        !readable(reinterpret_cast<const void*>(passes.begin), 0x94) ||
        *reinterpret_cast<const uint8_t*>(passes.begin)) return;

    auto* texture = *address<ID3D11Texture2D*>(0x1b1c6f8);
    auto* dsv = *address<ID3D11DepthStencilView*>(0x1b1c700);
    auto* srv = *address<ID3D11ShaderResourceView*>(0x1b1c708);
    auto** contextSlot = reinterpret_cast<ID3D11DeviceContext**>(static_cast<char*>(renderer)+0x76f68);
    if (!readable(contextSlot, sizeof(void*)) || !readable(*contextSlot,sizeof(void*)) ||
        !readable(texture,sizeof(void*)) || !readable(dsv,sizeof(void*))) return;
    if (!readable(srv,sizeof(void*))) srv=nullptr;
    auto* context = *contextSlot;
    // Check what geometry actually wrote. A desktop/standby DSV, a different
    // depth bank, or a detached target cannot attest to fresh VR pixels.
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> bound;
    context->OMGetRenderTargets(0, nullptr, &bound);
    if (bound.Get() != dsv) return;
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    dsv->GetResource(&resource);
    if (resource.Get() != texture) return;
    UINT countViewports = 1;
    D3D11_VIEWPORT viewport{};
    context->RSGetViewports(&countViewports, &viewport);
    if (countViewports != 1) return;
    capture(unsigned(view->eye_index), view->projection, context, texture, srv, viewport);
}

std::uintptr_t cockpitPostProcess(void* renderer, ViewSnapshot* view) {
    auto capture = cockpitCapture.load(std::memory_order_acquire);
    if (capture && reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS) == base + 0x5e0a39)
        observeCockpit(capture, renderer, view);
    return postProcessOriginal(renderer, view);
}

bool setCockpitObserver(og::lmu::CockpitCapture callback) {
    if (!callback) {
        if (cockpitCapture.exchange(nullptr)) MH_DisableHook(address<void>(postProcessRva));
        return true;
    }
    if (!postProcessOriginal) {
        // engine_start already checked the pristine image if stereo VRS is running.
        if (!installed.load()) {
            base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!correctProcess() || !data_abi::validate(base, readable).supported) return false;
        }
        constexpr std::array<uint8_t,19> prologue{0x48,0x8b,0xc4,0x48,0x89,0x50,0x10,0x55,0x53,0x56,
            0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
        constexpr std::array<uint8_t,12> postCall{0x48,0x8b,0x01,0x49,0x8b,0xd7,0xff,0x90,0x00,0x0b,0,0};
        constexpr std::array<uint8_t,5> eyeCall{0xe8,0x0d,0x73,0x08,0x00};
        constexpr std::array<uint8_t,5> viewCall{0xe8,0x9f,0x0d,0x00,0x00};
        if (!hookTargetPlausible(postProcessRva) || !matches(postProcessRva, prologue) ||
            !matches(0x5e0a2d, postCall) || !matches(0xcd175e, eyeCall) || !matches(0x5de94c, viewCall)) return false;
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
        // A thread already in the detour can still need its trampoline after
        // disable. Retain the single trampoline and its code module for process life.
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&cockpitPostProcess), &pinned)) return false;
        if (MH_CreateHook(address<void>(postProcessRva), reinterpret_cast<void*>(&cockpitPostProcess),
                          reinterpret_cast<void**>(&postProcessOriginal)) != MH_OK) return false;
    }
    const auto status = MH_EnableHook(address<void>(postProcessRva));
    if (status != MH_OK && status != MH_ERROR_ENABLED) return false;
    cockpitCapture.store(callback, std::memory_order_release);
    return true;
}
}
}
namespace og::lmu {
bool cockpit_observer(CockpitCapture callback) { return lmuqv::setCockpitObserver(callback); }
}
