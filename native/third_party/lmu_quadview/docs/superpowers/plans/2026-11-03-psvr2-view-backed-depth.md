# PSVR2 native outer resource repair

The four 0.3.0 tester logs reproduce a precheck refusal with FSR and a role 5
resource refusal with Bilinear, both without and with MSAA. The pinned executable's
0x5bbcf5..0x5bbe61 construction installs SRV/RTV views into the DepthResolve
container at renderer+0x82cb0, without calling the direct resource setter.
An empty +0x20 therefore does not prove that the texture is absent.

Implementation:

1. Reproduce a view-owned R32_FLOAT DepthResolve target with real D3D11 WARP
   views and the synthetic native wrapper ABI. Test MSAA on/off, binding restore,
   cached reuse, view replacement, mismatched views and absent resources.
2. Resolve the texture via ID3D11View::GetResource when the direct slot is empty.
   Preserve all view/resource validation and COM ownership. Cache the accepted
   resource/view pointer set so replacing a view invalidates the bank.
3. Only start preparation on a located, renderable XR frame. Log individual
   precheck failures (including unsupported upscaler scale); allow a bounded
   retry of resource preparation without disabling normal quad fallback.
4. Run native tests/build, independently review the repair, sign and package a
   local diagnostic build. Preserve the registry-rename workaround and settings.

Validation boundary: WARP tests establish resource ownership, resizing, cache and
fallback behavior. PSVR2 performance and image correctness require a tester run.
The separate headset-information dialog occurs during XR startup and is not
evidence of the role 5 failure.

## Verification and OpenGrid comparison

- WARP regression failed before repair with `view-backed DepthResolve refused`;
  passes with the repair for primary samples 1 and 2. Empty/mismatched resources
  remain rejected; returning views permit recovery. Original wrapper restoration
  and cache invalidation are checked.
- Real native-view hook regression failed before the gate because non-renderable
  frames consumed warmup and disabled the bank; now passes including cooldown,
  three-attempt exhaustion, preserved quad fallback and FSR diagnostics.
- Full MSVC native suite passed, including XR frame integration. Independent
  read-only review found no blocking issue. No PSVR2 performance claim yet.
- Read-only comparison with OpenGrid XR: desktop/lmu-steam.cjs uses a Steam
  `%command%` wrapper, preserves the protected launcher and sets child environment
  before launch. desktop/runtime.cjs also registers an implicit HKCU layer.
  native/src/layer.cpp reuses downstream QVFR or injects an isolated local copy
  below its adapter if none is downstream. Our bootstrap instead reconstructs
  its downstream chain and loads the selected provider. No speculative startup
  rewrite was made from this comparison.
- Local 16:59 implicit-start log stops between foveation selection and runtime
  recommendation logging, before native outer preparation. Add the previously
  missing query error/count and downstream session failure diagnostics. This
  improves diagnosis; it is not proof that the headset-start dialog is fixed.
