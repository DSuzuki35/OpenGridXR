"""Original OpenGrid XR driver-eye helmet overlays. Blender 4.5 LTS.

blender --background --python scripts/render-helmets.py -- --style all
All geometry and materials are generated here; no external textures are used.
"""
import argparse
import json
import math
from pathlib import Path
import sys

import bpy

ROOT = Path(__file__).resolve().parents[1]
STYLES = {
    "formula": {"rx": 1.69, "top": .66, "bottom": .62, "power": .58, "accent": (.18, .28, .30, 1)},
    "gt": {"rx": 1.72, "top": .77, "bottom": .73, "power": .66, "accent": (.32, .24, .14, 1)},
    "open": {"rx": 1.79, "top": .84, "bottom": 1.32, "power": .73, "accent": (.24, .28, .32, 1)},
}


def material(name, color, roughness, grain=0, metallic=0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = color
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = color
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if grain:
        tex = nodes.new("ShaderNodeTexNoise")
        tex.inputs["Scale"].default_value = grain
        tex.inputs["Detail"].default_value = 2
        bump = nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = .17
        bump.inputs["Distance"].default_value = .004
        links.new(tex.outputs["Fac"], bump.inputs["Height"])
        links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
        ramp = nodes.new("ShaderNodeValToRGB")
        ramp.color_ramp.elements[0].color = tuple(c * .65 for c in color[:3]) + (1,)
        ramp.color_ramp.elements[1].color = tuple(min(1, c * 1.35) for c in color[:3]) + (1,)
        links.new(tex.outputs["Fac"], ramp.inputs["Fac"])
        links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def point(cfg, angle, expand=0, z=0):
    c, s = math.cos(angle), math.sin(angle)
    x = math.copysign(abs(c) ** cfg["power"], c) * (cfg["rx"] + expand)
    y = math.copysign(abs(s) ** cfg["power"], s) * ((cfg["top"] if s >= 0 else cfg["bottom"]) + expand)
    return (x, y, z)


def ring(name, cfg, cross_section, mat, start=0, end=2 * math.pi):
    """Swept, sculpted rim cross-section with an actually empty eye aperture."""
    steps = 192
    vertices = []
    for i in range(steps + 1):
        angle = start + (end - start) * i / steps
        vertices.extend(point(cfg, angle, width, z) for width, z in cross_section)
    n = len(cross_section)
    faces = []
    for i in range(steps):
        for j in range(n - 1):
            a = i * n + j
            faces.append((a, a + n, a + n + 1, a + 1))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    for face in mesh.polygons:
        face.use_smooth = True
    return obj


def tube(name, points, radius, mat):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 2
    curve.bevel_depth = radius
    curve.bevel_resolution = 3
    spline = curve.splines.new("POLY")
    spline.points.add(len(points) - 1)
    for vertex, co in zip(spline.points, points):
        vertex.co = (*co, 1)
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    return obj


def cushion(name, location, scale, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    obj.data.materials.append(mat)
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj


def light(name, location, power, size, color):
    data = bpy.data.lights.new(name, "AREA")
    data.energy = power
    data.shape = "DISK"
    data.size = size
    data.color = color
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    obj.location = location
    obj.rotation_euler = (-obj.location).to_track_quat("-Z", "Y").to_euler()


def build(style, width, samples):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    cfg = STYLES[style]
    shell = material("Graphite composite shell", (.009, .012, .016, 1), .46, 320)
    fabric = material("Charcoal woven comfort liner", (.024, .029, .034, 1), .91, 520)
    edge = material("Matte elastomer aperture seal", (.006, .008, .011, 1), .72, 180)
    thread = material("Low contrast seam thread", (.07, .08, .09, 1), .82)
    accent = material("Muted model accent", cfg["accent"], .52, 100)
    metal = material("Brushed fasteners", (.17, .19, .20, 1), .33, metallic=.8)
    # This is an eye-space cutaway: outboard surfaces deliberately extend
    # beyond the frame so compositing cannot reveal a floating external helmet.
    start, end = (-.56, math.pi + .56) if style == "open" else (0, 2 * math.pi)
    ring("Outer shell cutaway", cfg, [(.10, -.12), (.17, -.02), (.28, .03), (.49, -.07), (1.5, -.2)], shell, start, end)
    ring("Sculpted continuous comfort liner", cfg, [(.015, -.015), (.045, .065), (.105, .105), (.17, .075), (.25, -.025)], fabric, start, end)
    ring("Rolled visor gasket", cfg, [(-.005, -.05), (0, .005), (.017, .026), (.031, .01), (.045, -.02)], edge, start, end)
    # Twin seam paths and individual short stitch segments follow the liner.
    for width_offset in (.096, .124):
        tube("Recessed stitched seam", [point(cfg, start + (end-start)*i/240, width_offset, .108) for i in range(241)], .0025, edge)
    for i in range(132):
        a = start + (end - start) * (i + .15) / 132
        b = start + (end - start) * (i + .55) / 132
        tube("Comfort liner stitch", [point(cfg, a, .11, .11), point(cfg, b, .11, .11)], .0018, thread)
    # Bulky cheek padding sits outside the sight window, close to the viewer.
    for side in (-1, 1):
        cushion("Left cheek pad" if side < 0 else "Right cheek pad", (side * 1.91, -.47, .08), (.20, .44 if style != "open" else .36, .15), fabric)
        tube("Cheek pad piping", [(side * 1.79, -.19, .17), (side * 1.76, -.38, .20), (side * 1.81, -.63, .18), (side * 1.92, -.81, .10)], .012, edge)
        cushion("Visor pivot recess", (side * 1.92, .04, .03), (.067, .067, .019), edge)
        cushion("Visor pivot screw", (side * 1.92, .04, .052), (.028, .028, .012), metal)
        tube("Fastener slot", [(side * 1.92 - .013, .04, .066), (side * 1.92 + .013, .04, .066)], .003, edge)
    # Small functional accents are kept on the border, away from the road view.
    if style != "open":
        lower = cfg["bottom"]
        cushion("Chinbar inner cushion", (0, -lower - .26, .025), (1.32, .17, .10), fabric)
        for i in range(-4, 5):
            x = i * .065
            tube("Chinbar ventilation groove", [(x, -lower-.20, .13), (x, -lower-.31, .13)], .008, edge)
        # A subtle breath deflector rises at the bottom centre, never at eye level.
        verts = [(-.38, -lower-.11, .14), (-.18, -lower+.015, .14), (0, -lower+.065, .14), (.18, -lower+.015, .14), (.38, -lower-.11, .14), (.30, -1.3, .14), (-.30, -1.3, .14)]
        mesh = bpy.data.meshes.new("Breath deflector")
        mesh.from_pydata(verts, [], [(0, 1, 2, 3, 4, 5, 6)])
        mesh.update()
        obj = bpy.data.objects.new("Flexible breath deflector", mesh)
        bpy.context.collection.objects.link(obj)
        obj.data.materials.append(edge)
        solid = obj.modifiers.new("Moulded thickness", "SOLIDIFY")
        solid.thickness = .015
        bevel = obj.modifiers.new("Soft moulded edges", "BEVEL")
        bevel.width = .015
        bevel.segments = 3
    tube("Brow identification trim", [(-.28, cfg["top"]+.21, .04), (.28, cfg["top"]+.21, .04)], .014, accent)
    light("Large cool frontal softbox", (-1.8, 1.8, 3.0), 160, 3.5, (.82, .89, 1))
    light("Warm low fill", (2, -.5, 2), 90, 2.5, (1, .88, .74))
    light("Top rim daylight", (0, 2, 1), 100, 2, (.85, .93, 1))
    world = bpy.data.worlds.new("Soft ambient illumination")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (.25, .29, .34, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = .25
    bpy.context.scene.world = world
    bpy.ops.object.camera_add(location=(0, 0, 5))
    camera = bpy.context.object
    camera.name = "Driver eye overlay camera"
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 4
    scene = bpy.context.scene
    scene.camera = camera
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x = width
    scene.render.resolution_y = round(width * 9 / 16)
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.image_settings.compression = 60
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.exposure = -.4
    bpy.context.preferences.filepaths.save_version = 0
    scene.render.filepath = f"//../../public/helmets/{style}.png"
    scene["OpenGrid XR asset"] = "Original procedural driver-eye cutaway; no external art or textures."
    scene["Usage"] = "Desktop preview overlay. Native headset compositor integration is separate."
    scene["Style"] = style
    scene["Generator"] = "scripts/render-helmets.py"
    for obj in scene.objects:
        obj.select_set(False)
    camera.select_set(True)
    bpy.context.view_layer.objects.active = camera
    # Make the camera view immediately useful when opening the source.
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.region_3d.view_perspective = "CAMERA"
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "assets" / "helmets" / f"{style}.blend"), compress=True)
    bpy.ops.render.render(write_still=True)


def verify():
    """Check delivered PNG alpha coverage and editable scene integrity."""
    import numpy as np
    results = []
    for style in STYLES:
        bpy.ops.wm.open_mainfile(filepath=str(ROOT / "assets" / "helmets" / f"{style}.blend"))
        scene = bpy.context.scene
        assert scene.camera and scene.camera.type == "CAMERA", f"{style}: missing camera"
        assert scene.render.film_transparent, f"{style}: transparent film disabled"
        assert len(scene.objects) > 100, f"{style}: missing editable geometry"
        img = bpy.data.images.load(str(ROOT / "public" / "helmets" / f"{style}.png"), check_existing=False)
        width, height = img.size
        assert (width, height) == (1920, 1080), f"{style}: unexpected dimensions"
        assert img.channels == 4, f"{style}: missing RGBA channels"
        pixels = np.empty(width * height * 4, dtype=np.float32)
        img.pixels.foreach_get(pixels)
        alpha = pixels.reshape(height, width, 4)[:, :, 3]
        assert float(alpha[height // 2, width // 2]) == 0, f"{style}: blocked central view"
        assert np.all(alpha[int(height*.4):int(height*.6), int(width*.3):int(width*.7)] == 0), f"{style}: central viewing region is not clear"
        assert float(alpha[-1, width // 2]) == 1, f"{style}: missing brow"
        assert float(alpha[0, width // 2]) == (0 if style == "open" else 1), f"{style}: incorrect lower opening"
        clear = float(np.mean(alpha == 0))
        assert .4 < clear < .9, f"{style}: invalid aperture area"
        results.append({"style": style, "size": [width, height], "channels": img.channels, "fullyTransparentPercent": round(clear*100, 2), "centerAlpha": 0, "editableObjects": len(scene.objects)})
    print("HELMET_ASSET_VERIFICATION=" + json.dumps(results))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--style", choices=["all", *STYLES], default="all")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--verify", action="store_true", help="Check the delivered full-size PNGs and Blender scenes without rendering")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    if args.width < 320 or args.width > 8192 or args.samples < 1:
        parser.error("width must be 320..8192 and samples must be positive")
    if args.verify:
        verify()
        sys.exit(0)
    (ROOT / "assets" / "helmets").mkdir(parents=True, exist_ok=True)
    (ROOT / "public" / "helmets").mkdir(parents=True, exist_ok=True)
    for style in STYLES if args.style == "all" else [args.style]:
        build(style, args.width, args.samples)
