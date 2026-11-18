#include "lmu_cockpit_capture.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

namespace og::lmu {
namespace {
std::mutex mutex;
std::map<XrSession, ID3D11Device*> sessions;
std::atomic<XrSession> requested{};
bool attempted{}, submitting{};
XrSession owner{};
struct Buffers {
    std::mutex gpu;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    CockpitDepthCopy copies[2];
    CockpitMsaaResolve resolves[2];
};
std::shared_ptr<Buffers> storage;
uint64_t generation{};
struct Pair {
    CockpitDepthFrame frame{};
    unsigned seen{};
} pair;

std::shared_ptr<Buffers> stop() {
    if (requested.exchange({})) cockpit_observer(nullptr);
    ++generation;
    attempted=false; submitting=false; owner={}; pair={};
    return std::move(storage);
}

bool read_sealed_pair(XrSession session, XrTime time, CockpitDepthFrame& out) {
    out={};
    if (!submitting || requested.load()!=session || time<=0 || time!=pair.frame.time ||
        !pair.frame.eyes[0].view || !pair.frame.eyes[1].view) return false;
    out=pair.frame;
    for (auto& eye:out.eyes) eye.view->AddRef();
    return true;
}

bool rectangle(const D3D11_VIEWPORT& v, const D3D11_TEXTURE2D_DESC& d, CockpitDepthEye& eye) {
    if (!(v.TopLeftX>=0 && v.TopLeftY>=0 && v.Width>=1 && v.Height>=1 &&
          v.TopLeftX+v.Width<=d.Width && v.TopLeftY+v.Height<=d.Height &&
          v.MinDepth>=0 && v.MinDepth<v.MaxDepth && v.MaxDepth<=1)) return false;
    for (float x : {v.TopLeftX,v.TopLeftY,v.Width,v.Height}) if (x!=std::floor(x)) return false;
    eye.rect={{int(v.TopLeftX),int(v.TopLeftY)},{int(v.Width),int(v.Height)}};
    eye.minDepth=v.MinDepth; eye.maxDepth=v.MaxDepth;
    return true;
}

void capture(unsigned index, const CockpitProjection& projection, ID3D11DeviceContext* gameContext,
             ID3D11Texture2D* texture, ID3D11ShaderResourceView* srv, const D3D11_VIEWPORT& viewport) {
    std::unique_lock lock(mutex);
    if (submitting || !requested.load() || !pair.frame.time || index>1 ||
        !storage || gameContext!=storage->context.Get() || !texture) return;
    // A valid native frame renders one ordinary left/right pair in order.
    if (pair.seen!=index) { pair.frame.time=0; return; }
    ++pair.seen;
    const auto epoch=generation;
    auto buffer=storage;
    CockpitDepthEye eye=pair.frame.eyes[index];
    auto* device=buffer->device.Get();
    // Reserve GPU storage without waiting. Entering the immediate context while
    // holding the provider mutex would invert the order for a runtime or game
    // thread which already owns the context and enters an OpenXR call.
    std::unique_lock gpu(buffer->gpu,std::try_to_lock);
    lock.unlock();
    if (!gpu.owns_lock()) return;
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    Microsoft::WRL::ComPtr<ID3D11Device> sourceDevice; texture->GetDevice(&sourceDevice);
    if (sourceDevice.Get()!=device || desc.ArraySize!=1 || desc.MipLevels!=1 ||
        !(desc.BindFlags&D3D11_BIND_DEPTH_STENCIL) || !rectangle(viewport,desc,eye) ||
        !cockpit_mapping(projection,eye)) return;
    if (desc.SampleDesc.Count>1) {
        eye.view=buffer->resolves[index].capture(device,gameContext,srv,texture,desc,eye.rect,eye.invFar>eye.invNear);
        // MSAA capture stores just the eye viewport at native pixel density.
        eye.rect.offset={0,0};
    } else if (buffer->copies[index].capture(device,gameContext,texture))
        eye.view=buffer->copies[index].view();
    lock.lock();
    if (epoch==generation && !submitting) pair.frame.eyes[index]=eye;
    lock.unlock();
}
}

void cockpit_open(XrSession session, ID3D11Device* binding) {
    std::shared_ptr<Buffers> retired;
    std::lock_guard lock(mutex);
    sessions[session]=binding;
    if (sessions.size()!=1) retired=stop();
}
void cockpit_close(XrSession session) {
    std::shared_ptr<Buffers> retired;
    std::lock_guard lock(mutex);
    if (session==owner) retired=stop();
    sessions.erase(session);
}
void cockpit_locate(XrSession session,const XrViewLocateInfo* info,const XrViewState* state,
                    const XrView* views,bool valid) {
    if (requested.load(std::memory_order_acquire)!=session) return;
    std::lock_guard lock(mutex);
    if (session!=owner || submitting) return;
    ++generation;
    pair.frame={}; pair.seen=0;
    constexpr auto tracked=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if (!valid || !info || !state || !views || !info->space || info->displayTime<=0 ||
        info->viewConfigurationType!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ||
        (state->viewStateFlags&tracked)!=tracked) return;
    pair.frame.space=info->space; pair.frame.time=info->displayTime;
    for (unsigned i=0;i<2;++i) pair.frame.eyes[i].pose=views[i].pose;
}
void cockpit_submit(XrSession session,const XrFrameEndInfo* info) {
    if (requested.load(std::memory_order_acquire)!=session) return;
    std::lock_guard lock(mutex);
    if (session!=owner) return;
    ++generation;
    submitting=info!=nullptr;
    if (!info || !info->layers || info->displayTime!=pair.frame.time || pair.seen!=2) {
        pair.frame.time=0; return;
    }
    const XrCompositionLayerProjection* projection=nullptr;
    for (uint32_t i=0;i<info->layerCount;++i) {
        const auto layer=info->layers[i];
        if (!layer || layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
        if (projection) { pair.frame.time=0; return; }
        projection=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
    }
    if (!projection || projection->viewCount!=2 || !projection->views || projection->space!=pair.frame.space) {
        pair.frame.time=0; return;
    }
    for (unsigned i=0;i<2;++i) {
        // Use the pose submitted with these scene pixels in the same locate space.
        pair.frame.eyes[i].pose=projection->views[i].pose;
    }
}

bool cockpit_request(ID3D11Device* binding, XrSession session, XrTime time, bool demand, CockpitDepthFrame* out) {
    std::shared_ptr<Buffers> retired;
    std::lock_guard lock(mutex);
    if (out) *out={};
    const auto it=sessions.find(session);
    if (!binding || sessions.size()!=1 || it==sessions.end() || binding!=it->second) return false;
    if (!demand) { if (session==owner) retired=stop(); return false; }
    if (!attempted) {
        attempted=true; owner=session;
        storage=std::make_shared<Buffers>();
        storage->device=binding;
        binding->GetImmediateContext(&storage->context);
        if (storage->context && cockpit_observer(capture)) requested.store(session,std::memory_order_release);
    }
    return out && read_sealed_pair(session,time,*out);
}

bool cockpit_read(XrSession session, XrTime time, CockpitDepthFrame& out) {
    std::lock_guard lock(mutex);
    return read_sealed_pair(session,time,out);
}
}
