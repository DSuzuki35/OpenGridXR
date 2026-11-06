Portable shader conversion research
===================================

This research started with the research 2 game capture. It includes a compiler
component, tests and an in-game private comparison. Normal game draws remain
enabled; no CPU/FPS gain is claimed.

The converter reads legacy SM4.0 vertex/pixel DXBC and emits a stereo variant.
Selected constant buffers are packed into two eye banks in a spare buffer slot.
The vertex shader selects the eye from the instance ID, chooses the corresponding
constants, and exports a constant eye varying. The pixel shader independently
selects its per-eye constants. Array routing uses a capability-selected vertex
output or a generated pass-through geometry shader. These paths use standard
D3D11, without NVIDIA-specific APIs.

The original math, deformation, lighting, material operations and other resource
bindings remain in the program. The converter validates the container checksum,
chunk ranges, token lengths, signature capacity and packed-buffer capacity. It
generates a new valid DXBC container and discards stale reflection/debug metadata.
It rejects unsupported profiles, existing instance/stereo inputs, occupied packed
CB slots, dynamically indexed selected CBs, unsupported control/declarations, and
signatures the initial geometry adapter cannot relay. Sampler declarations are
decoded into a slot mask and every body sampler operand must use a declared,
immediate slot. The checker compares those sampler slots; unused stale bindings
cannot affect the shader. All VS/PS SRV slots remain conservatively checked.
A failed conversion returns
no usable shader or binding plan; callers must retain ordinary rendering.

Successful compilation does not establish that a game pass can be combined. The
caller still must provide matching geometry, visibility, correct current-frame
eye constants, IA instance step rates, compatible array targets/depth, resource
lifetimes and per-eye textures/history. It must suppress duplicate native CPU
preparation only after that complete rendering path is validated. CB packing is
an initial correctness mechanism; copying every selected object CB per draw is
not assumed to improve CPU time. Sharing camera-only data and native preparation
is a separate integration task.

Local checks
------------

    mod\build-msvc.cmd -StereoShaderOnly
    mod\build\msvc\stereo_shader_test.exe --hardware

The synthetic tests compile owned SM4 shaders, then compare one converted stereo
draw against two original draws. They check exact colour, depth tolerance,
asymmetric cameras, two packed VS CB ranges, a separate PS CB, near/far occlusion,
array slices and both routing paths when available. Negative cases cover corrupt
DXBC, existing instance inputs, selected dynamic CBs and a conflicting spare slot.
Both WARP and an RTX 4080 passed. AMD/Intel hardware and MinGW remain untested.

Optionally validate locally captured programs without redistributing them:

    stereo_shader_test.exe --hardware --captured-vs <local-vs.dxbc> --captured-ps <local-ps.dxbc>
    stereo_shader_test.exe --hardware --validate-captured-directory <local-shader-folder>

The selected opaque LMU family uses VS SHA256 beginning a99b2cd689aa55f5 and PS
SHA256 beginning 79bed2230b50db6c or d41d95fc9c8fed23. Converted shaders and the
generated geometry adapter passed D3D11 creation checks on WARP and the RTX 4080.
Raw game shaders, cache files and disassembly remain local development inputs
outside version control.

On 2026-10-31, prototype 5 passed a live private comparison for one matched
4,104-index object using the d41d95fc pixel shader. At 1352x1734 per eye, the
instanced VPRT draw produced 1,100,595 passed samples, matching the two reference
counts combined. No pixels exceeded the color tolerance; maximum color error
was 0.0000610352. Depth matched exactly. The sampled native state was D32 with
GREATER comparison and depth writes enabled. This validates that sampled object
and its captured per-eye inputs, not the complete opaque pass or other programs.
Both native CPU preparation paths and game draws remain enabled.

A separate capture stopped on PS sampler slot 8 differing between eyes. These
two pixel programs declare only s0, s1, s11, s13 and s14: s8 was an irrelevant
stale binding. Source now checks sampler declarations rather than every slot.
Tests allow an unused s8 difference but still refuse it when an owned shader
actually samples through s8. Slot masks are also checked against D3DReflect for
converted local programs. This does not establish that the refused capture
would pass all remaining checks if replayed.

The optional local corpus check also validated all 150 exported program checksums,
including both checksum padding cases. Eighty programs converted and passed D3D11
creation on WARP and the RTX 4080; seventy were refused by the initial support
guards. This checks parsing and driver acceptance, not game-pass eligibility or
image equivalence for those programs.

The research 2 captures contain all used shader/layout definitions, no dropped
draw records, and about 91% matching focus-eye draw signatures in the sampled
scene. The matching signature covers only the first sixteen PS SRV/sampler slots.
Some shaders use slots above that range, so matching is not resource equivalence.
The first chosen opaque family also uses a texture in slot 20; its sharing must
be verified during integration. Reflection/SSR passes require additional per-eye
resource handling and are not selected for the initial conversion.
