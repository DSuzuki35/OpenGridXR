# Original helmet overlays

OpenGrid XR includes three original driver-eye interior overlays, rendered from
editable Blender geometry. They contain no photographs, commercial helmet art,
logos, downloaded models, or external texture dependencies. The generated assets
and generator are distributed under this repository's GPL-2.0 license.

| Style | Render | Editable scene | Appearance |
| --- | --- | --- | --- |
| Formula | `public/helmets/formula.png` | `assets/helmets/formula.blend` | Tighter visor opening, padded chin bar and breath deflector |
| GT | `public/helmets/gt.png` | `assets/helmets/gt.blend` | Taller visor opening and warm brow accent |
| Open | `public/helmets/open.png` | `assets/helmets/open.blend` | Open lower view with brow and cheek padding |

Each PNG is 1920 × 1080, 8-bit RGBA, with a fully transparent central viewing
area and antialiased aperture edges. Lighting, stitched seams, molded seals,
cheek padding, and fasteners are rendered into the border. These are original
stylized interior cutaways, not replicas of any helmet manufacturer.

## Reproduce or edit

Created and checked with Blender 4.5.15 LTS using Cycles, 64 samples and denoising.
Download Blender from its official distribution; a global installation is not
required. Blender is an authoring dependency only, not an app runtime dependency.

From the repository root, with Blender on the executable path:

```powershell
blender --background --python scripts/render-helmets.py -- --style all
blender --background --python scripts/render-helmets.py -- --verify
```

Use `--style formula`, `--style gt`, or `--style open` for a single asset. For a
quick draft use `--width 960 --samples 24`; the verification command intentionally
requires the delivered 1920 × 1080 assets. Rendering overwrites the matching PNG
and `.blend` source. The normal default command restores final-size outputs.

The generator defines aperture proportions in `STYLES`, builds named mesh and
curve objects, creates procedural materials and studio lights, saves the scene,
and renders the image. Open any `.blend` in Blender to edit individual parts;
the camera view is configured for the overlay. Render paths are relative to the
source file. Changes made directly in Blender are overwritten by regeneration,
so update the generator when the change needs to remain reproducible.

## Compositing

Serve the PNGs from `helmets/<style>.png` relative to the app base URL. Keep the
16:9 aspect ratio and center the image over the scene. Use the existing helmet
opacity, scale and position preferences to transform the whole layer. Apply
visor tint to a separate layer behind the helmet; the PNG itself deliberately
leaves the aperture clear. The contour mask remains an independent feature and
does not describe the precise rendered aperture.

These assets support the desktop helmet preview. Shipping the source and PNGs
does not implement native headset composition, stereo alignment, field-of-view
calibration or head tracking. A runtime compositor must handle those separately.
The `.blend` files are camera-facing interior cutaways intended for rendering
these overlays, not complete wearable helmet models for arbitrary viewpoints.

## Verification

`--verify` loads every source scene and its PNG, checks editable geometry and
camera presence, dimensions, RGBA channels, a fully clear central viewing
rectangle, opaque brow, and the expected lower opening for each style. It also
reports transparent-pixel coverage. All three final PNGs were visually inspected
after rendering. The Blender user-extension cache can emit a permission warning
in restricted environments; the asset saves and renders do not require it.
