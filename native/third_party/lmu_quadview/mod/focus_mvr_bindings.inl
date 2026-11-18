// Binding-driven same-eye MVR, adapted from Williem3/Sim-QVFR mvr_pass.cpp.
// Only the game's main scene atlas receives the second viewport. Other targets
// and occlusion queries run normally, then pairing resumes on the next binding.
namespace bind_path {
using VsSet = void(STDMETHODCALLTYPE *)(C *, ID3D11VertexShader *, ID3D11ClassInstance *const *, UINT);
using OmSet = void(STDMETHODCALLTYPE *)(C *, UINT, ID3D11RenderTargetView *const *, ID3D11DepthStencilView *);
using OmUav = void(STDMETHODCALLTYPE *)(C *, UINT, ID3D11RenderTargetView *const *, ID3D11DepthStencilView *, UINT,
                                        UINT, ID3D11UnorderedAccessView *const *, const UINT *);
using VpSet = void(STDMETHODCALLTYPE *)(C *, UINT, const D3D11_VIEWPORT *);
using ScSet = void(STDMETHODCALLTYPE *)(C *, UINT, const D3D11_RECT *);
using CbSet = void(STDMETHODCALLTYPE *)(C *, UINT, UINT, ID3D11Buffer *const *);
using CbSet1 = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext1 *, UINT, UINT, ID3D11Buffer *const *, const UINT *,
                                         const UINT *);
using SoSet = void(STDMETHODCALLTYPE *)(C *, UINT, ID3D11Buffer *const *, const UINT *);
template <class T> using StageSet = void(STDMETHODCALLTYPE *)(C *, T *, ID3D11ClassInstance *const *, UINT);
VsSet vsSet{};
OmSet omSet{};
OmUav omUav{};
VpSet vpSet{};
ScSet scSet{};
CbSet cbSet{};
CbSet1 cbSet1{};
ComPtr<ID3D11DeviceContext1> context1;
thread_local bool writingCb = false;
SoSet soSet{};
StageSet<ID3D11GeometryShader> gsSet{};
StageSet<ID3D11HullShader> hsSet{};
StageSet<ID3D11DomainShader> dsSet{};
struct Bindings {
    ComPtr<ID3D11VertexShader> gameVs;
    D3D11_VIEWPORT vp[16]{};
    D3D11_RECT sc[16]{};
    UINT nv{}, ns{}, instances{};
    ComPtr<ID3D11Buffer> gameBuffer;
    UINT firstConstant{}, constantCount{};
    bool scene{}, extraOutputs{}, gs{}, hs{}, ds{}, so{}, active{}, armed{};
} b;
struct Coverage {
    unsigned target{}, query{}, shader{}, stage{}, viewport{}, masked{};
} coverage;
bool watching(C *c) { return outer && b.armed && c == s.ctx.Get(); }
void rememberGameBuffer() {
    if (context1)
        context1->VSGetConstantBuffers1(13, 1, b.gameBuffer.ReleaseAndGetAddressOf(), &b.firstConstant,
                                        &b.constantCount);
    else
        s.ctx->VSGetConstantBuffers(13, 1, b.gameBuffer.ReleaseAndGetAddressOf());
}
void restoreGameBuffer() {
    if (context1 && b.gameBuffer && (b.firstConstant || b.constantCount != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT))
        cbSet1(context1.Get(), 13, 1, b.gameBuffer.GetAddressOf(), &b.firstConstant, &b.constantCount);
    else
        cbSet(s.ctx.Get(), 13, 1, b.gameBuffer.GetAddressOf());
}
void viewport() {
    if (!b.active) {
        vpSet(s.ctx.Get(), b.nv, b.vp);
        return;
    }
    const auto &v = b.vp[0];
    D3D11_VIEWPORT pair[2] = {
        v, {s.w + v.TopLeftX * s.fw / s.w, v.TopLeftY * s.fh / s.h, v.Width * s.fw / s.w, v.Height * s.fh / s.h, v.MinDepth, v.MaxDepth}};
    vpSet(s.ctx.Get(), 2, pair);
}
void scissor() {
    if (!b.active || b.ns != 1) {
        scSet(s.ctx.Get(), b.ns, b.sc);
        return;
    }
    const auto &r = b.sc[0];
    auto x = [&](LONG v) {
        return LONG(std::lround(s.w + double(v) * s.fw / s.w));
    };
    auto y = [&](LONG v) { return LONG(std::lround(double(v) * s.fh / s.h)); };
    D3D11_RECT pair[2] = {r, {x(r.left), y(r.top), x(r.right), y(r.bottom)}};
    scSet(s.ctx.Get(), 2, pair);
}
void update(bool shaderRebound = false) {
    const bool eligible = b.armed && b.scene && s.complete && !b.extraOutputs && !s.broken && !lens && !b.gs && !b.hs && !b.ds && !b.so &&
                          !b.instances && !countingQueries.load(std::memory_order_relaxed) && b.gameVs && b.nv == 1 &&
                          b.ns <= 1 && b.vp[0].Width > 0 && b.vp[0].Height > 0;
    auto *twin = eligible ? vertex_twin(b.gameVs.Get()) : nullptr;
    if (shaderRebound && !twin && s.accepted[s.eye] < 2) {
        if (!b.scene)
            ++coverage.target;
        else if (lens)
            ++coverage.masked;
        else if (b.gs || b.hs || b.ds || b.so || b.instances)
            ++coverage.stage;
        else if (countingQueries.load(std::memory_order_relaxed))
            ++coverage.query;
        else if (b.nv != 1 || b.ns > 1)
            ++coverage.viewport;
        else
            ++coverage.shader;
    }
    const bool want = twin != nullptr;
    if (want == b.active) {
        if (want && shaderRebound) {
            vsSet(s.ctx.Get(), twin, nullptr, 0);
            ++s.paired;
        }
        return;
    }
    if (!instancedBackend.load() && api().stereoMode(s.ctx.Get(), want ? 2 : 1, 0, want ? TRUE : FALSE) != NVAPI_OK) {
        stop_reuse("NVIDIA stereo mode change failed");
        if (b.active)
            api().stereoMode(s.ctx.Get(), 1, 0, FALSE);
    }
    const bool wasActive = b.active;
    b.active = want && !s.broken;
    vsSet(s.ctx.Get(), b.active ? twin : b.gameVs.Get(), nullptr, 0);
    writingCb = true;
    if (b.active) {
        // Shader conversion already rejects shaders that declare b13. An unrelated
        // buffer left bound by the game is safe to borrow for this twin only.
        rememberGameBuffer();
        cbSet(s.ctx.Get(), 13, 1, s.cb.GetAddressOf());
    } else if (wasActive) {
        restoreGameBuffer();
    }
    writingCb = false;
    viewport();
    scissor();
    if (b.active)
        ++s.paired;
}
void STDMETHODCALLTYPE set_vs(C *c, ID3D11VertexShader *vs, ID3D11ClassInstance *const *instances, UINT n) {
    if (!watching(c)) {
        vsSet(c, vs, instances, n);
        return;
    }
    if (n && b.active) {
        b.instances = n;
        update();
    }
    vsSet(c, vs, instances, n);
    b.gameVs = vs;
    b.instances = n;
    update(true);
}
void inspect_extra_outputs(C* c) {
    b.extraOutputs = false;
    if (!b.scene || !instancedBackend.load()) return;
    // A second instance must not write an unrelated color or unordered-access target.
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    c->OMGetRenderTargets(UINT(std::size(targets)),targets,nullptr);
    for (UINT i=0;i<std::size(targets);++i) if(targets[i]){
        b.extraOutputs |= i!=0;
        targets[i]->Release();
    }
    b.extraOutputs |= has_output_uavs(c,s.uavSlots);
}
void target(ID3D11RenderTargetView *color, ID3D11DepthStencilView *depth) {
    b.scene = depth == s.depth.Get() && (!color || color == s.color.Get());
    inspect_extra_outputs(s.ctx.Get());
    update();
}
void STDMETHODCALLTYPE set_om(C *c, UINT n, ID3D11RenderTargetView *const *r, ID3D11DepthStencilView *d) {
    omSet(c, n, r, d);
    if (watching(c))
        target(n && r ? r[0] : nullptr, d);
}
void STDMETHODCALLTYPE set_uav(C *c, UINT n, ID3D11RenderTargetView *const *r, ID3D11DepthStencilView *d, UINT start,
                               UINT count, ID3D11UnorderedAccessView *const *u, const UINT *init) {
    omUav(c, n, r, d, start, count, u, init);
    if (!watching(c)) return;
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) target(n && r ? r[0] : nullptr, d);
    else {
        inspect_extra_outputs(c);
        update();
    }
}
void STDMETHODCALLTYPE set_vp(C *c, UINT n, const D3D11_VIEWPORT *v) {
    if (!watching(c) || n > 16 || (n && !v)) {
        vpSet(c, n, v);
        return;
    }
    b.nv = n;
    if (n)
        std::memcpy(b.vp, v, n * sizeof(*v));
    update();
    viewport();
}
void STDMETHODCALLTYPE set_sc(C *c, UINT n, const D3D11_RECT *r) {
    if (!watching(c) || n > 16 || (n && !r)) {
        scSet(c, n, r);
        return;
    }
    b.ns = n;
    if (n)
        std::memcpy(b.sc, r, n * sizeof(*r));
    update();
    scissor();
}
template <class F> void gameBufferUpdate(C *c, UINT first, UINT n, F forward) {
    if (writingCb || !watching(c) || !b.active || first > 13 || n <= 13 - first) {
        forward();
        return;
    }
    writingCb = true;
    restoreGameBuffer();
    forward();
    rememberGameBuffer();
    cbSet(c, 13, 1, s.cb.GetAddressOf());
    writingCb = false;
}
void STDMETHODCALLTYPE set_cb(C *c, UINT first, UINT n, ID3D11Buffer *const *buffers) {
    gameBufferUpdate(c, first, n, [&] { cbSet(c, first, n, buffers); });
}
void STDMETHODCALLTYPE set_cb1(ID3D11DeviceContext1 *c, UINT first, UINT n, ID3D11Buffer *const *buffers,
                               const UINT *offsets, const UINT *sizes) {
    gameBufferUpdate(c == context1.Get() ? s.ctx.Get() : c, first, n,
                     [&] { cbSet1(c, first, n, buffers, offsets, sizes); });
}
template <class T, StageSet<T> *fn, bool Bindings::*flag>
void STDMETHODCALLTYPE stage(C *c, T *sh, ID3D11ClassInstance *const *instances, UINT n) {
    (*fn)(c, sh, instances, n);
    if (watching(c)) {
        b.*flag = sh != nullptr;
        update();
    }
}
void STDMETHODCALLTYPE set_so(C *c, UINT n, ID3D11Buffer *const *buffers, const UINT *offsets) {
    soSet(c, n, buffers, offsets);
    if (watching(c)) {
        b.so = false;
        for (UINT i = 0; buffers && i < n; ++i)
            b.so |= buffers[i] != nullptr;
        update();
    }
}
void begin() {
    auto *c = s.ctx.Get();
    b = Bindings{};
    coverage = {};
    c->VSGetShader(&b.gameVs, nullptr, &b.instances);
    ComPtr<ID3D11DepthStencilView> d;
    ComPtr<ID3D11RenderTargetView> r;
    c->OMGetRenderTargets(1, &r, &d);
    b.scene = d.Get() == s.depth.Get() && (!r || r.Get() == s.color.Get());
    inspect_extra_outputs(c);
    b.nv = b.ns = 16;
    c->RSGetViewports(&b.nv, b.vp);
    c->RSGetScissorRects(&b.ns, b.sc);
    ComPtr<ID3D11GeometryShader> gs;
    c->GSGetShader(&gs, nullptr, nullptr);
    b.gs = bool(gs);
    ComPtr<ID3D11HullShader> hs;
    c->HSGetShader(&hs, nullptr, nullptr);
    b.hs = bool(hs);
    ComPtr<ID3D11DomainShader> ds;
    c->DSGetShader(&ds, nullptr, nullptr);
    b.ds = bool(ds);
    ID3D11Buffer *streams[4]{};
    c->SOGetTargets(4, streams);
    for (auto *v : streams)
        if (v) {
            b.so = true;
            v->Release();
        }
    b.armed = true;
    update();
}
void end() {
    b.armed = false;
    update();
    b = Bindings{};
}
void reset() {
    b = Bindings{};
    context1.Reset();
}
void STDMETHODCALLTYPE query_begin(C *c, ID3D11Asynchronous *q) {
    if (c == s.ctx.Get() && rendering_query_type(q) >= 0) {
        ++countingQueries;
        if (watching(c))
            update();
    }
    beginQuery(c, q);
}
void STDMETHODCALLTYPE query_end(C *c, ID3D11Asynchronous *q) {
    endQuery(c, q);
    if (c != s.ctx.Get() || rendering_query_type(q) < 0)
        return;
    unsigned n = countingQueries.load();
    while (n && !countingQueries.compare_exchange_weak(n, n - 1)) {
    }
    if (watching(c))
        update();
}
// The vertex shader selects the viewport from instance parity. Both copies
// of a game instance must also fetch identical per-instance vertex data.
template<class Native, class Paired>
void instanced_draw(C* c, UINT instances, UINT firstInstance, Native native, Paired paired) {
    if (!watching(c) || !b.scene || lens || countingQueries.load(std::memory_order_relaxed)) {
        native();
        return;
    }
    ComPtr<ID3D11InputLayout> layout;
    ID3D11InputLayout* replacement = nullptr;
    const char* reason = nullptr;
    if (!b.active || !s.complete) reason = "unsupported instanced scene shader or binding";
    // An earlier image comparison only qualifies those images. A new material
    // or a changed SRV can make this very draw unsafe in an offset viewport.
    else if (!pixel_compatible(c)) reason = "incompatible pixel shader or scene input";
    else if (firstInstance || instances > UINT_MAX / 2) reason = "unsupported instance offset or count";
    else {
        c->IAGetInputLayout(&layout);
        if (layout) {
            // Read immutable creation metadata once across consecutive draws with this layout.
            if (layout != drawMetadata.layout) {
                ComPtr<ID3D11InputLayout> twin;
                UINT tagged = 0, size = sizeof(tagged);
                if (FAILED(layout->GetPrivateData(vertexOnlyId, &size, &tagged)) || !tagged) {
                    size = sizeof(ID3D11InputLayout*);
                    if (FAILED(layout->GetPrivateData(doubledLayoutId, &size, twin.GetAddressOf())) || !twin)
                        reason = "unverified instance input layout";
                }
                if (!reason) {
                    drawMetadata.layout = layout;
                    drawMetadata.doubledLayout = std::move(twin);
                }
            }
            replacement = drawMetadata.doubledLayout.Get();
        }
    }
    if (reason) {
        refuse(reason);
        update();
        native();
        return;
    }
    if (replacement) c->IASetInputLayout(replacement);
    paired(instances * 2);
    if (replacement) c->IASetInputLayout(layout.Get());
}
void STDMETHODCALLTYPE draw_vertices(C* c, UINT vertices, UINT first) {
    instanced_draw(c, 1, 0, [&]{draw(c,vertices,first);},
        [&](UINT n){instanced(c,vertices,n,first,0);});
}
void STDMETHODCALLTYPE draw_indices(C* c, UINT indices, UINT first, INT baseVertex) {
    instanced_draw(c, 1, 0, [&]{indexed(c,indices,first,baseVertex);},
        [&](UINT n){indexedInstanced(c,indices,n,first,baseVertex,0);});
}
void STDMETHODCALLTYPE draw_instances(C* c, UINT vertices, UINT count, UINT first, UINT baseInstance) {
    instanced_draw(c, count, baseInstance, [&]{instanced(c,vertices,count,first,baseInstance);},
        [&](UINT n){instanced(c,vertices,n,first,0);});
}
void STDMETHODCALLTYPE draw_indexed_instances(C* c, UINT indices, UINT count, UINT first, INT baseVertex, UINT baseInstance) {
    instanced_draw(c, count, baseInstance, [&]{indexedInstanced(c,indices,count,first,baseVertex,baseInstance);},
        [&](UINT n){indexedInstanced(c,indices,n,first,baseVertex,0);});
}
void unsupported_draw(C* c) {
    if (watching(c) && b.scene && !lens) {
        refuse("GPU-generated draw count");
        update();
    }
}
void STDMETHODCALLTYPE draw_auto(C* c){unsupported_draw(c);automatic(c);}
void STDMETHODCALLTYPE draw_indirect(C* c,ID3D11Buffer* args,UINT offset){unsupported_draw(c);indirect(c,args,offset);}
void STDMETHODCALLTYPE draw_indexed_indirect(C* c,ID3D11Buffer* args,UINT offset){unsupported_draw(c);indexedIndirect(c,args,offset);}
using ClearStateFn = void(STDMETHODCALLTYPE*)(C*);
using SwapStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3DDeviceContextState*,ID3DDeviceContextState**);
ClearStateFn clearState{};
SwapStateFn swapState{};
void leave_before_state_change(C* c){
    if (watching(c)) {
        refuse("context state changed during paired scene");
        end();
    }
}
void STDMETHODCALLTYPE clear_state(C* c){leave_before_state_change(c);clearState(c);}
void STDMETHODCALLTYPE execute_list(C* c,ID3D11CommandList* list,BOOL restore){
    leave_before_state_change(c);execute(c,list,restore);
}
void STDMETHODCALLTYPE swap_state(ID3D11DeviceContext1* c,ID3DDeviceContextState* state,ID3DDeviceContextState** old){
    if(c==context1.Get())leave_before_state_change(s.ctx.Get());
    swapState(c,state,old);
}
bool install(void **v) {
    if (SUCCEEDED(s.ctx.As(&context1))) {
        auto **v1 = *reinterpret_cast<void ***>(context1.Get());
        if (!hook(v1[119], set_cb1, cbSet1) || !hook(v1[131],swap_state,swapState))
            return false;
    }
    if (instancedBackend.load() &&
        (!hook(v[12],draw_indices,indexed) || !hook(v[13],draw_vertices,draw) ||
         !hook(v[20],draw_indexed_instances,indexedInstanced) || !hook(v[21],draw_instances,instanced) ||
         !hook(v[38],draw_auto,automatic) || !hook(v[39],draw_indexed_indirect,indexedIndirect) ||
         !hook(v[40],draw_indirect,indirect))) return false;
    return hook(v[110],clear_state,clearState) && hook(v[58],execute_list,execute) &&
           hook(v[11], set_vs, vsSet) && hook(v[33], set_om, omSet) && hook(v[34], set_uav, omUav) &&
           hook(v[44], set_vp, vpSet) && hook(v[45], set_sc, scSet) && hook(v[7], set_cb, cbSet) &&
           hook(v[37], set_so, soSet) && hook(v[23], stage<ID3D11GeometryShader, &gsSet, &Bindings::gs>, gsSet) &&
           hook(v[60], stage<ID3D11HullShader, &hsSet, &Bindings::hs>, hsSet) &&
           hook(v[64], stage<ID3D11DomainShader, &dsSet, &Bindings::ds>, dsSet) &&
           hook(v[27], query_begin, beginQuery) && hook(v[28], query_end, endQuery) &&
           hook(v[57], on_resolve, resolve) && hook(v[50], on_clear_color, clearColor) &&
           hook(v[53], on_clear_depth, clearDepth);
}
} // namespace bind_path
