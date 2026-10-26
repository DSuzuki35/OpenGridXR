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
from mathutils import Matrix

WRAP_RADIUS = 2.9


def helmet_surface(x, y, depth=0):
    """Bend developed helmet coordinates around the same axis as the visor.

    Positive Z faces the eyes. The sides wrap back towards the wearer; depth
    preserves the sculpted rim cross-section and the visor-to-gasket spacing.
    """
    theta = x / WRAP_RADIUS
    return (WRAP_RADIUS * math.sin(theta), y,
            depth + WRAP_RADIUS * (1 - math.cos(theta)) + .035 * (y / 1.35) ** 2)


def wrap_helmet_geometry():
    """Apply one continuous deformation to every shell detail, in world space."""
    import bmesh
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    for obj in list(bpy.context.scene.objects):
        if obj.type not in ("MESH", "CURVE"):
            continue
        evaluated = obj.evaluated_get(deps)
        mesh = bpy.data.meshes.new_from_object(evaluated, depsgraph=deps)
        transform = obj.matrix_world.copy()
        for vertex in mesh.vertices:
            vertex.co = transform @ vertex.co
        # Long faces (notably the deflector) also need vertices between their
        # endpoints, otherwise a bent outline still contains flat triangles.
        bm = bmesh.new()
        bm.from_mesh(mesh)
        for _ in range(5):
            long_edges = [edge for edge in bm.edges if edge.calc_length() > .08]
            if not long_edges:
                break
            bmesh.ops.subdivide_edges(bm, edges=long_edges, cuts=1, use_grid_fill=True)
        for vertex in bm.verts:
            vertex.co = helmet_surface(*vertex.co)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(mesh)
        bm.free()
        replacement = bpy.data.objects.new(obj.name + " wrapped", mesh)
        bpy.context.collection.objects.link(replacement)
        name = obj.name
        bpy.data.objects.remove(obj, do_unlink=True)
        replacement.name = name
        replacement.matrix_world = Matrix.Identity(4)
        replacement["Surface"] = "Shared visor-axis wrap, radius 2.9 model units"

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


def visor_surface(u, v, lift=0):
    """Interior face of a wrapped polycarbonate visor, in real scene units."""
    y = 1.35 - v * 2.7
    return helmet_surface((u - .5) * 1.6 * WRAP_RADIUS, y, -.26 + lift)


def build_visor():
    vertices, faces, uv_coords = [], [], []
    columns, rows = 192, 48
    for j in range(rows + 1):
        for i in range(columns + 1):
            u, v = i / columns, j / rows
            vertices.append(visor_surface(u, v))
            uv_coords.append((u, 1 - v))
    for j in range(rows):
        for i in range(columns):
            a = j * (columns + 1) + i
            faces.append((a, a + 1, a + columns + 2, a + columns + 1))
    mesh = bpy.data.meshes.new("Curved visor UV surface")
    mesh.from_pydata(vertices, [], faces)
    uv = mesh.uv_layers.new(name="Printed visor coordinates")
    for polygon in mesh.polygons:
        polygon.use_smooth = True
        for loop_index in polygon.loop_indices:
            uv.data[loop_index].uv = uv_coords[mesh.loops[loop_index].vertex_index]
    visor = bpy.data.objects.new("Curved polycarbonate visor - actual projection surface", mesh)
    bpy.context.collection.objects.link(visor)
    glass = material("Clear polycarbonate visor interior", (.85, .92, 1, 1), .045)
    bsdf = glass.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Transmission Weight"].default_value = 1
    bsdf.inputs["IOR"].default_value = 1.58
    visor.data.materials.append(glass)
    visor.hide_render = True
    visor["Usage"] = "Genuine curved UV surface. Offline per-eye map projects customizable sunstrip and print; hidden in base-shell PNG."
    # Editable, physically wrapped sample print proves the intended inside view.
    strip_vertices, strip_faces = [], []
    for i in range(columns + 1):
        u = i / columns
        for v in (0, .30 + .12 * (1 - (2 * u - 1) ** 2)):
            strip_vertices.append(visor_surface(u, v, .002))
    for i in range(columns):
        a = i * 2
        strip_faces.append((a, a + 2, a + 3, a + 1))
    strip_mesh = bpy.data.meshes.new("Curved upper sunstrip geometry")
    strip_mesh.from_pydata(strip_vertices, [], strip_faces)
    strip = bpy.data.objects.new("White sunstrip - editable reference geometry", strip_mesh)
    bpy.context.collection.objects.link(strip)
    strip.data.materials.append(material("White sunstrip print", (.88, .87, .83, 1), .48))
    strip.hide_render = True
    bpy.ops.object.text_add()
    lettering = bpy.context.object
    lettering.name = "OPEN GRID mirrored interior print - conformed to visor"
    lettering.data.body = "OPEN GRID"
    lettering.data.align_x = "CENTER"
    lettering.data.align_y = "CENTER"
    lettering.data.size = .27
    lettering.data.extrude = 0
    bpy.context.view_layer.update()
    bpy.ops.object.convert(target="MESH")
    lettering = bpy.context.object
    max_x = max(abs(vertex.co.x) for vertex in lettering.data.vertices)
    for vertex in lettering.data.vertices:
        u = .5 - vertex.co.x / (max_x * 2) * .52
        v = .38 - vertex.co.y / 2.7
        vertex.co = visor_surface(u, v, .004)
    lettering.data.materials.append(material("Red exterior lettering seen from inside", (.6, .008, .015, 1), .5))
    lettering.hide_render = True
    return visor


def render_visor_maps(scene, visor, stereo, style):
    """Two RGBA8 maps: R/G pack 16-bit U or V, A is visor coverage.
    Float rendering plus lossless packing preserves coordinates. The app samples these maps
    only when preparing PNGs, never as a runtime mesh or shader dependency.
    """
    hidden = [(obj, obj.hide_render) for obj in scene.objects]
    for obj, _ in hidden:
        if obj.type not in ("CAMERA", "LIGHT"):
            obj.hide_render = obj != visor
    original_material = visor.data.materials[0]
    mat = bpy.data.materials.new("Float visor UV export - Raw")
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    emission = nodes.new("ShaderNodeEmission")
    coords = nodes.new("ShaderNodeTexCoord")
    separate = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords.outputs["UV"], separate.inputs[0])
    multiply = nodes.new("ShaderNodeMath"); multiply.operation = "MULTIPLY"; multiply.inputs[1].default_value = 255
    links.new(separate.outputs["X"], multiply.inputs[0])
    floor = nodes.new("ShaderNodeMath"); floor.operation = "FLOOR"
    fract = nodes.new("ShaderNodeMath"); fract.operation = "FRACT"
    links.new(multiply.outputs[0], floor.inputs[0]); links.new(multiply.outputs[0], fract.inputs[0])
    divide = nodes.new("ShaderNodeMath"); divide.operation = "DIVIDE"; divide.inputs[1].default_value = 255
    links.new(floor.outputs[0], divide.inputs[0])
    invert = nodes.new("ShaderNodeMath"); invert.operation = "SUBTRACT"; invert.inputs[0].default_value = 1
    links.new(separate.outputs["Y"], invert.inputs[1])
    combine = nodes.new("ShaderNodeCombineColor")
    links.new(divide.outputs[0], combine.inputs["Red"]); links.new(fract.outputs[0], combine.inputs["Green"]); links.new(invert.outputs[0], combine.inputs["Blue"])
    links.new(combine.outputs[0], emission.inputs["Color"]); links.new(emission.outputs[0], output.inputs["Surface"])
    visor.data.materials[0] = mat
    original_transform, original_exposure, samples = scene.view_settings.view_transform, scene.view_settings.exposure, scene.cycles.samples
    scene.view_settings.view_transform = "Raw"; scene.view_settings.exposure = 0
    scene.cycles.samples = 8; scene.cycles.use_denoising = False
    # Render unquantized UVs first. Packing before sample filtering or PNG
    # dithering would introduce whole high-byte jumps (visible speckled print).
    for link in list(combine.inputs["Red"].links): links.remove(link)
    for link in list(combine.inputs["Green"].links): links.remove(link)
    links.new(separate.outputs["X"], combine.inputs["Red"])
    links.new(invert.outputs[0], combine.inputs["Green"])
    import numpy as np
    import struct, zlib
    def write_coordinate_png(file, values, alpha):
        coordinate = np.rint(np.clip(values, 0, 1) * 65535).astype(np.uint16)
        rgba = np.zeros((*values.shape, 4), dtype=np.uint8)
        rgba[:, :, 0] = coordinate >> 8
        rgba[:, :, 1] = coordinate & 255
        rgba[:, :, 3] = np.rint(np.clip(alpha, 0, 1) * 255).astype(np.uint8)
        h, w = values.shape
        raw = b''.join(b'\0' + row.tobytes() for row in rgba[::-1])
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
        file.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    old_format, old_depth = scene.render.image_settings.file_format, scene.render.image_settings.color_depth
    scene.render.image_settings.file_format = "OPEN_EXR"; scene.render.image_settings.color_depth = "32"
    (ROOT / '.local').mkdir(exist_ok=True)
    for eye, camera in stereo:
        scene.camera = camera
        temporary = ROOT / '.local' / f'visor-{style}-{eye}.exr'
        scene.render.filepath = str(temporary)
        bpy.ops.render.render(write_still=True)
        rendered = bpy.data.images.load(str(temporary), check_existing=False)
        w, h = rendered.size
        pixels = np.empty(w * h * 4, dtype=np.float32); rendered.pixels.foreach_get(pixels)
        pixels = pixels.reshape(h, w, 4)
        for component, suffix in ((0, 'uv'), (1, 'v')):
            write_coordinate_png(ROOT / 'public' / 'helmets' / f'{style}-{eye}-visor-{suffix}.png', pixels[:, :, component], pixels[:, :, 3])
        bpy.data.images.remove(rendered); temporary.unlink()
    scene.render.image_settings.file_format = old_format; scene.render.image_settings.color_depth = old_depth
    scene.view_settings.view_transform = original_transform; scene.view_settings.exposure = original_exposure
    scene.cycles.samples = samples; scene.cycles.use_denoising = True
    visor.data.materials[0] = original_material
    for obj, was_hidden in hidden:
        obj.hide_render = was_hidden


def build(style, width, samples, stereo_only=False):
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
    wrap_helmet_geometry()
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
    # Retain the original orthographic mono composition. Stereo is rendered
    # from real geometry through parallel-axis perspective cameras: a modest
    # 0.12 model-unit baseline, with zero parallax at the aperture plane (z=0).
    # Off-axis sensor shift, rather than toe-in, avoids vertical disparity.
    stereo = []
    camera_distance, frame_width, baseline = 2.0, 4.0, .12
    for eye, sign in (("left", -1), ("right", 1)):
        offset = sign * baseline / 2
        bpy.ops.object.camera_add(location=(offset, 0, camera_distance))
        eye_camera = bpy.context.object
        eye_camera.name = f"Driver {eye} eye stereo camera"
        eye_camera.data.type = "PERSP"
        eye_camera.data.sensor_fit = "HORIZONTAL"
        eye_camera.data.sensor_width = 36
        eye_camera.data.lens = 36 * camera_distance / frame_width
        eye_camera.data.shift_x = -offset / frame_width
        eye_camera.data.clip_start = .05
        eye_camera.data.clip_end = 100
        eye_camera["Eye"] = eye
        eye_camera["Convergence plane Z"] = 0.0
        stereo.append((eye, eye_camera))
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
    scene["Usage"] = "Offline mono and stereo RGBA overlays. Native compositor uses per-eye PNGs; no mesh is drawn at runtime."
    scene["Stereo baseline"] = baseline
    scene["Stereo projection"] = "Parallel-axis perspective; off-axis shift converges at z=0; no toe-in."
    scene["Style"] = style
    scene["Generator"] = "scripts/render-helmets.py"
    scene["Helmet wrap radius"] = WRAP_RADIUS
    visor = build_visor()
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
    if not stereo_only:
        bpy.ops.render.render(write_still=True)
    for eye, eye_camera in stereo:
        scene.camera = eye_camera
        scene.render.filepath = str(ROOT / "public" / "helmets" / f"{style}-{eye}.png")
        bpy.ops.render.render(write_still=True)
    render_visor_maps(scene, visor, stereo, style)
    scene.camera = camera
    scene.render.filepath = f"//../../public/helmets/{style}.png"
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "assets" / "helmets" / f"{style}.blend"), compress=True)


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
        stereo_pixels = []
        for eye in ("left", "right"):
            camera = scene.objects.get(f"Driver {eye} eye stereo camera")
            assert camera and camera.data.type == "PERSP", f"{style}: missing perspective {eye} camera"
            assert tuple(camera.rotation_euler) == (0.0, 0.0, 0.0), f"{style}: stereo camera must not toe in"
            eye_img = bpy.data.images.load(str(ROOT / "public" / "helmets" / f"{style}-{eye}.png"), check_existing=False)
            assert tuple(eye_img.size) == (1920, 1080) and eye_img.channels == 4
            eye_pixels = np.empty(width * height * 4, dtype=np.float32)
            eye_img.pixels.foreach_get(eye_pixels)
            eye_pixels = eye_pixels.reshape(height, width, 4)
            assert np.all(eye_pixels[int(height*.4):int(height*.6), int(width*.3):int(width*.7), 3] == 0), f"{style}: stereo opening obstructed"
            stereo_pixels.append(eye_pixels)
        assert float(np.max(np.abs(stereo_pixels[0] - stereo_pixels[1]))) > .1, f"{style}: eye renders are identical"
        assert scene.objects.get("Curved polycarbonate visor - actual projection surface"), f"{style}: missing curved visor geometry"
        assert scene.objects.get("OPEN GRID mirrored interior print - conformed to visor"), f"{style}: missing physical reference lettering"
        for eye in ("left", "right"):
            for coordinate in ("uv", "v"):
                uv_img = bpy.data.images.load(str(ROOT / "public" / "helmets" / f"{style}-{eye}-visor-{coordinate}.png"), check_existing=False)
                assert tuple(uv_img.size) == (1920, 1080) and uv_img.channels == 4
            assert len(scene.objects["Curved polycarbonate visor - actual projection surface"].data.vertices) > 9000
    print("HELMET_ASSET_VERIFICATION=" + json.dumps(results))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--style", choices=["all", *STYLES], default="all")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--stereo-only", action="store_true", help="Preserve delivered mono PNGs; render the new per-eye perspective pair")
    parser.add_argument("--maps-only", action="store_true", help="Update UV projection maps from existing editable Blender scenes")
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
        if args.maps_only:
            bpy.ops.wm.open_mainfile(filepath=str(ROOT / "assets" / "helmets" / f"{style}.blend"))
            scene = bpy.context.scene
            stereo = [(eye, scene.objects[f"Driver {eye} eye stereo camera"]) for eye in ("left", "right")]
            render_visor_maps(scene, scene.objects["Curved polycarbonate visor - actual projection surface"], stereo, style)
        else:
            build(style, args.width, args.samples, args.stereo_only)
