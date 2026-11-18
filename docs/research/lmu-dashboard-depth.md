# Native cockpit depth for web dashboards

REA investigation, 2026-11-10. The installed LMU executable has SHA-256
`66942337813b301b87d12edfa330f33354a2a4b8cd9b048885cbf77eb6f940cf`.
The instructions below were checked in the existing reconstructed code image
for that build, SHA-256 `0ddb49b677bc3c1d2446ba11f530f2e5490b6f37296024e8019648cd16badcc0`.
The on-disk executable has different bytes at the scene call site. An engine
adapter must verify the loaded code and data layout before using these RVAs.

## What the renderer exposes

| RVA | Observation in the reconstructed x64 code |
| --- | --- |
| `0x5be262–0x5be274` | LMU passes the literal `$VR Z Buffer` and the address of the global wrapper slot `0x1b1c758` to its resource factory. |
| `0x5cbb80–0x5cbbb6` | The depth getter returns that slot for renderer modes 1 and 2. Other modes select a different slot. |
| `0xcd15e1–0xcd15f5`, `0xcd175e`, `0xcd17c1–0xcd17ca` | LMU passes eye indices 0 and 1 to its camera update and calls scene rendering inside the loop. The late-pass boolean alone is insufficient to identify a view under Quad Views. |
| `0x5bbe41–0x5bbe48` | The engine attaches a resource container to the wrapper at `renderer+0x82cb0`. OpenGrid's resource-bank code identifies it as the single-sample depth target. |
| `0x5ace57–0x5acedb` | Post-processing passes the getter result as the source and the single-sample wrapper as the destination. Both branch call sites reach `0x5c1a20`, one via the jump at `0x5c2e20`. This makes those two specific returns candidates for observing the result while its eye is known. |

To reproduce a block locally, use REA with its Rizin provider against the
reconstructed image. For example, `rea execute-rizin-command lmu-code-analysis.bin "pd 35 @ 0x1405ace57"`.
The image base is `0x140000000`.

## First cockpit measurement

1. In ordinary stereo, observe the two post-processing call sites inside
   the verified main eye render. Record the frame, eye, active viewport,
   projection, render target and depth texture descriptors. The depth
   wrapper is shared across eye passes, so retain each eye's contents on
   the GPU before reuse. A retained COM pointer alone cannot freeze pixels.
2. Capture a single synchronized stereo pair with the virtual driver visible.
   Compare the raw scene depth with the post-processing result and the color
   image while turning the wheel. Check the gloves and rim in each eye.
   Inspect sample layout, depth direction and units against the active
   projection before selecting a source. Resource names alone cannot establish
   the presence of hands or the encoding of the post-processing result.
3. Place a flat test dashboard behind the gloves and in front of the in-game
   instruments. Each eye should see the correct silhouette as the wheel and
   head move. Move the dashboard in front of the gloves and verify that
   the occlusion reverses. Repeat with MSAA, then with Quad Views, including
   the focus region and its boundary. MVR can pack multiple views into an
   atlas, so each verified region needs its own projection and viewport.
4. If either eye lacks valid current-frame depth, submit the existing plain
   dashboard for both eyes and report unavailable depth. Keep diagnostic
   readback confined to explicit captures. Measure disabled and enabled
   frametimes in the same cockpit.

## Implemented capture

The stereo adapter observes the entry to `ApplyPostProcess` at RVA `0x5acb50`.
At this point it can check the bound D3D11 depth-stencil view against the
active VR depth texture and use the eye's scene projection directly. This
avoids inferring a depth encoding from the later post-processing result.
For MSAA, a compute pass selects the nearest sample using the projection's
depth direction. A submitted pair must match the XR frame and space. The
cockpit test above, particularly the glove silhouette, still needs a headset.

iRacing uses the depth swapchain submitted with a stereo projection, copied
before release. Both providers supply the same per-eye dashboard mask. A
static webpage keeps its texture while the mask updates with new game frames.
The off path skips the depth copies and mask draws.
