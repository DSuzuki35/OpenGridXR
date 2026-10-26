# Stereo helmet interiors and curved visors

OpenGrid XR includes original Formula, GT and Open helmet interiors built from
editable Blender geometry. They are physically inspired driver-eye cutaways, not
measured replicas or optically calibrated models of a particular headset. The
geometry, generated artwork and scripts use this repository's GPL-2.0 license.
No manufacturer photographs, logos, downloaded meshes or external textures are
embedded in the supplied assets.

## Geometry and stereo

Each style has a padded shell, gasket, stitched seams, cheek pads, fasteners and
an actual curved polycarbonate visor surface. Editable reference sunstrip and
mirrored `OPEN GRID` lettering meshes follow that surface. They can be inspected
from inside and outside in the `.blend` source; these cutaways are not complete
wearable helmets for arbitrary viewpoints.

| Asset | Location | Purpose |
| --- | --- | --- |
| Editable source | `assets/helmets/<style>.blend` | Geometry, sample print, lights and three cameras |
| Original mono render | `public/helmets/<style>.png` | Preserved orthographic reference |
| Stereo shell pair | `public/helmets/<style>-left.png`, `-right.png` | Separate perspective renders of real geometry |
| Curved projection maps | `public/helmets/<style>-<eye>-visor-uv.png`, `-visor-v.png` | High-precision coordinates for custom print |

Images are 1920 × 1080 RGBA. The stereo cameras are parallel, with a 0.12
model-unit baseline and off-axis shifts converging at the aperture plane. The
pair has real geometric parallax, not a translated flat picture. These artistic
model dimensions are not a user's measured IPD.

## Customization and saved headset images

The editor projects strip color, text and uploaded raster graphics through the
Blender-rendered maps. Lettering bends with the surface and is mirrored by
default, like exterior print viewed from inside. White/red and dark/white presets
are starting points; text width, size, position, opacity and softness remain
editable. Full visor tint color and strength are independent of the upper strip.
PNG, JPEG and WebP uploads are decoded and normalized to embedded PNGs of at most
192 KB and 1024 × 512 pixels. Exported profiles carry their graphics.

The preview and profile-save preparation use the same canvas compositor. Saving
bakes two finished PNGs in the installed runtime directory. The hidden renderer
is sandboxed, denies navigation/network requests and is destroyed afterward.
Unchanged appearances reuse cached images. A generation-tagged sidecar is
disabled before replacing either image, then enabled after both are ready. The
native loader rechecks it after decoding to reject mixed generations. Failed
preparation leaves that helmet disabled.

The headset layer submits the raster pair. It does not render Blender meshes,
reproject text, or run Blender during a race. Helmets can remain on in driving
and streaming modes; the clean stream excludes headset composition layers.
Desktop screenshots do not establish headset alignment, comfort or an active
OpenXR session.

## Genuine scene adjustments

Brightness, contrast and saturation process the game scene separately from the
alpha-overlay tint. They run in that order on display-encoded color in the native
D3D11 image-processing path, before helmet, HUD and spatial layers. This includes
D3D11 games using the frame-generation bridge. Native D3D12 game sessions do not
support these adjustments. Clean stream capture remains before grading. Neutral
values are 100%; save and restart the XR session after changes. The editor uses
the same adjustment order.

## Crop and motion

`Fit crop to visor opening` explicitly updates the existing per-eye contour and
shared crop. It derives a conservative convex envelope containing every
nonopaque shell pixel, adding configured mask padding and a small raster margin.
Translucent visor tint and print never justify hiding scenery behind them.
Semi-transparent shells consequently offer little or no safe crop.

Optional head and vehicle responses default to zero. Enabling either adds a 3%
motion allowance; the native compositor caps movement at 3% of each image
dimension. Vehicle response requires live telemetry. Refit after changing shell
scale, position, opacity, padding or motion. Saving does not silently overwrite
crop. Crop/contour stay inactive in streaming even when the helmet remains on.
Cropping reduces visible/rendered image area; it does not change optical FOV.

## Reproduce the assets

Blender 4.5.15 LTS is an authoring dependency; end users do not need Blender.
From the repository root:

```powershell
blender --background --python scripts/render-helmets.py -- --style all --samples 48
blender --background --python scripts/render-helmets.py -- --verify
```

`--stereo-only` preserves mono PNGs while regenerating scenes, stereo shells and
maps. `--maps-only` regenerates maps from saved scenes without rebuilding the
shell. Use `--style formula`, `gt` or `open` for one style. A draft can use
`--width 960 --samples 24`; verification expects final 1920 × 1080 assets.

UV export first renders floating-point coordinates to temporary EXRs, then packs
each coordinate into the red/green bytes of a lossless PNG. Packing after sample
filtering avoids dither artifacts. Temporary EXRs are removed; only static PNG
maps ship. Base shell renders hide visor/reference print while retaining their
geometry in the source. Regeneration overwrites matching `.blend` and PNG files;
update the generator to preserve authoring changes reproducibly.

Verification covers source geometry, stereo cameras, per-eye differences, RGBA
dimensions, clear central view and coordinate maps. Electron tests exercise real
saved PNGs, alpha, generations, caching, profile round-trips, fitting and presets.

## Design references

Geometry is original. Construction references include [Bell's official HP77 and
SmartLock description](https://www.bellracing.com/press-release/bell-racing-helmets-launches-the-innovative-smartlock-visor-locking-system/),
the [FIA HP77 presentation form](https://www.fia.com/sites/default/files/ah.008.19-c-abp_-_presentation_form_v14_0.pdf),
and [Arai's car-helmet feature overview](https://www.araihelmet.eu/storage/uploads/blocks/816/block_2/1/arai-2024-features-overview-car-eng.pdf).
These references do not certify the simulated artwork as an exact optical model.
