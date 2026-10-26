"""Blender regression check: the shell must wrap with the visor, not form a plate.

Run: blender --background --python-exit-code 1 --python tests/helmet-geometry.py
"""
from pathlib import Path
import bpy

root = Path(__file__).resolve().parents[1]
for style in ("formula", "gt", "open"):
    bpy.ops.wm.open_mainfile(filepath=str(root / "assets/helmets" / f"{style}.blend"))
    def coordinates(name):
        obj = bpy.context.scene.objects[name]
        evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = evaluated.to_mesh()
        points = [obj.matrix_world @ v.co for v in mesh.vertices]
        evaluated.to_mesh_clear()
        return points

    for name, minimum_depth in (("Outer shell cutaway", .9),
                                ("Rolled visor gasket", .35),
                                ("Sculpted continuous comfort liner", .4),
                                ("Recessed stitched seam", .4)):
        points = coordinates(name)
        depth = max(p.z for p in points) - min(p.z for p in points)
        assert depth > minimum_depth, f"{style}: {name} is flat (depth={depth:.3f})"
        center = [p.z for p in points if abs(p.x) < .2]
        sides = [p.z for p in points if abs(p.x) > 1.5]
        assert center and sides and sum(sides)/len(sides) > sum(center)/len(center) + .3, f"{style}: shell bends away from the visor"
    for name in ("Left cheek pad", "Right cheek pad", "Visor pivot screw"):
        points = coordinates(name)
        assert sum(p.z for p in points)/len(points) > .45, f"{style}: {name} left behind on flat plane"
    print(f"PASS: {style} curved shell, seal, liner, seams and attached details")
