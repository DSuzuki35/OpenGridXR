"""Retag supplied Artura CAD surfaces for the live display and SimHub output.

Uses only Python's standard library. The reviewed source ZIP contains the exact
supplied muted prototype; all original positions and normals stay unchanged.
Six control bodies receive separate diffuse colors in unused atlas tiles.
CAD body/triangle ranges and physical LED order are recorded alongside
the asset, so this does not depend on an installed CAD tool or execute source code.
"""
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / "assets" / "wheels"


def build():
    mapping = json.loads((DIRECTORY / "ascher-artura.mapping.json").read_text(encoding="utf-8"))
    with zipfile.ZipFile(DIRECTORY / "ascher-artura-source.zip") as archive:
        original = archive.read("muted-prototype.agm")
    if hashlib.sha256(original).hexdigest() != mapping["source_sha256"]:
        raise ValueError("Artura source does not match the reviewed prototype")
    version, vertices, indices, width, height, reserved = struct.unpack_from("<6I", original, 8)
    if (version, vertices, indices, width, height, reserved) != (5, 564060, 564060, 64, 64, 0):
        raise ValueError("Unexpected Artura source geometry")
    source_vertices = bytearray(original[32:32 + vertices * 36])
    groups = set()
    for control in mapping["led_controls"]:
        group = 201 + control["physical_index"]
        if group in groups or not 201 <= group <= 296:
            raise ValueError("Duplicate or unsupported physical LED index")
        groups.add(group)
        for span in control["source_spans"]:
            for vertex in range(span["first_triangle"] * 3, (span["first_triangle"] + span["triangles"]) * 3):
                if struct.unpack_from("<f", source_vertices, vertex * 36 + 32)[0] != 0:
                    raise ValueError("Overlapping Artura control surfaces")
                struct.pack_into("<f", source_vertices, vertex * 36 + 32, group)
    # Keep the original dark glass, then reproduce its rounded CAD surface just
    # in front. With no URL the renderer clips this duplicate, leaving the glass.
    screen = mapping["screen"]
    first, count = screen["first_triangle"] * 3, screen["triangles"] * 3
    xmin, ymin, xmax, ymax = screen["source_rectangle_mm"]
    source_width = mapping["source_width_mm"]
    source_center = mapping["source_center_mm"]
    display = bytearray()
    for vertex in range(first, first + count):
        values = list(struct.unpack_from("<9f", original, 32 + vertex * 36))
        x = values[0] * source_width + source_center[0]
        y = values[1] * source_width + source_center[1]
        values[2] += screen["surface_separation_mm"] / source_width
        values[6:9] = [max(0, min(1, (x - xmin) / (xmax - xmin))),
                       max(0, min(1, (ymax - y) / (ymax - ymin))), 200]
        display.extend(struct.pack("<9f", *values))
    data = bytearray(b"AGMESH1\0")
    data.extend(struct.pack("<6I", 5, vertices + count, indices + count, width, height, 0))
    data.extend(source_vertices)
    data.extend(display)
    data.extend(original[32 + vertices * 36:32 + vertices * 36 + indices * 4])
    data.extend(struct.pack("<" + "I" * count, *range(vertices, vertices + count)))
    atlas = bytearray(original[-width * height * 4:])
    colors = json.loads((DIRECTORY / "ascher-artura.colors.json").read_text(encoding="utf-8"))
    # The 14 supplied material tiles remain intact. These controls use the two
    # previously unused tiles, without changing screen or physical LED tags.
    for color in colors["palette"].values():
        slot, rgb = color["slot"], color["rgb"]
        if slot not in (14, 15) or len(rgb) != 3 or any(type(c) is not int or not 0 <= c <= 255 for c in rgb):
            raise ValueError("Invalid Artura diffuse palette")
        for y in range((slot // 4) * 16, (slot // 4 + 1) * 16):
            for x in range((slot % 4) * 16, (slot % 4 + 1) * 16):
                atlas[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes(rgb + [255])
    painted = set()
    for control in colors["controls"]:
        slot = colors["palette"][control["color"]]["slot"]
        first_vertex = control["first_triangle"] * 3
        last_vertex = first_vertex + control["triangles"] * 3
        if not 0 <= first_vertex < last_vertex <= vertices:
            raise ValueError("Artura color span outside source geometry")
        for vertex in range(first_vertex, last_vertex):
            offset = 32 + vertex * 36
            if vertex in painted or struct.unpack_from("<f", data, offset + 32)[0] != 0:
                raise ValueError("Artura colors overlap a control, display or LED surface")
            painted.add(vertex)
            struct.pack_into("<2f", data, offset + 24,
                             ((slot % 4) * 16 + 8) / width, ((slot // 4) * 16 + 8) / height)
    data.extend(atlas)
    (DIRECTORY / "ascher-artura.agm").write_bytes(data)
    manifest_path = DIRECTORY / "ascher-artura.conversion.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest.update(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                    vertices=vertices + count, indices=indices + count, triangles=(indices + count) // 3)
    manifest["display"] = {"supported": True, "source_body": screen["source_body"],
                           "source_rectangle_mm": screen["source_rectangle_mm"],
                           "surface_separation_mm": screen["surface_separation_mm"],
                           "recommended_capture_size": [1024, 608]}
    manifest["led_mapping"] = {"supported": bool(groups), "physical_device": 6,
                               "physical_pixels": 63, "mapped_controls": len(groups), "mapping": "ascher-artura.mapping.json"}
    manifest["materials"].update(count=16, palette="Supplied muted palette with red/blue control overrides",
                                  control_colors="ascher-artura.colors.json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"sha256": manifest["sha256"], "bytes": len(data), "triangles": manifest["triangles"],
                      "mapped_leds": len(groups)}))


if __name__ == "__main__":
    build()
