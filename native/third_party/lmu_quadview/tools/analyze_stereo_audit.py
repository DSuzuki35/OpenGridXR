"""Summarize a local research capture. Matching signatures do not prove replay safety."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path


def summarize(data):
    if data.get("schema") != 1 or data.get("rendering") != "legacy":
        raise ValueError("Unsupported stereo audit schema/rendering mode")
    frequency = data["frequency"]
    if frequency <= 0:
        raise ValueError("Invalid timestamp frequency")
    shaders = {s["id"]: s for s in data["shaders"]}
    names = ("outer_left", "outer_right", "focus_left", "focus_right")
    passes = []
    draws_by_slot = defaultdict(list)
    families = Counter()
    simple = 0
    for d in data["draws"]:
        draws_by_slot[d["slot"]].append(d)
        families[(d["group"], *d["shaders"])] += 1
        simple += bool(d["kind"] <= 3 and d["layout_known"] and not any(d["shaders"][2:])
                       and not d["blend"] and not d["class_instances"] and not d["predicate"][0]
                       and d["rt"][3] == 1 and all(s in shaders for s in d["shaders"][:2]))
    for p in data["passes"]:
        if not p["draw_calls"] and not p["wall_ticks"]:
            continue
        passes.append({
            "frame": p["slot"] // 4, "view": names[p["slot"] % 4],
            "draw_calls": p["draw_calls"], "recorded": len(draws_by_slot[p["slot"]]),
            "dropped": p["dropped"],
            "diagnostic_wall_ms": p["wall_ticks"] * 1000 / frequency,
            "snapshot_overhead_ms": p["snapshot_ticks"] * 1000 / frequency,
            "draw_api_wall_ms": p["api_ticks"] * 1000 / frequency,
            "maps": p["maps"], "updates": p["updates"], "copies": p["copies"],
            "dispatches": p["dispatches"], "command_lists": p["command_lists"],
            "native_mesh_preparations": p.get("native_mesh_preparations", p.get("native_mesh_binds", 0)),
            "native_preparation_ms": p.get("native_prepare_ticks", p.get("native_bind_ticks", 0)) * 1000 / frequency,
        })

    def signatures(slot):
        # CB values and object identity are deliberately not inferred from these
        # signatures. Instanced/indirect scheduling needs separate validation.
        return Counter((d["kind"], tuple(d["args"]), d["group"], tuple(d["shaders"]),
                        d["layout"], d["topology"], d["vertex_signature"], tuple(d["index"]),
                        d["ps_srv_signature"], d["ps_sampler_signature"], tuple(d["predicate"]),
                        d["buffer_epoch"])
                       for d in draws_by_slot[slot] if d["kind"] <= 3)

    comparisons = []
    for frame in range(2):
        for a, b, label in ((2, 3, "focus_left_vs_right"), (0, 2, "outer_vs_focus_left"),
                            (1, 3, "outer_vs_focus_right")):
            left, right = signatures(frame * 4 + a), signatures(frame * 4 + b)
            comparisons.append({"frame": frame, "comparison": label,
                                "matching_signatures": sum((left & right).values()),
                                "left_recorded_direct_draws": sum(left.values()),
                                "right_recorded_direct_draws": sum(right.values())})
    return {
        "rendering": "legacy", "reason": data["reason"], "scene_frames": data["scene_frames"],
        "capabilities": data["capabilities"], "recorded_draws": len(data["draws"]),
        "known_shader_definitions": len(shaders),
        "metadata_cache": data.get("metadata_cache", {}),
        "shader_definitions_dropped": data.get("shader_definitions_dropped", 0),
        "layout_definitions_dropped": data.get("layout_definitions_dropped", 0),
        "shader_export": data.get("shader_export", {"enabled": 0}),
        "stereo_pair_proof": data.get("stereo_pair_proof", {"requested": 0}),
        "local_shader_program_files": sorted({s["bytecode_file"] for s in shaders.values() if s.get("bytecode_file")}),
        "shader_ids_without_definitions": sorted({s for d in data["draws"] for s in d["shaders"] if s and s not in shaders}),
        "simple_pipeline_draws_for_further_inspection": simple,
        "passes": passes, "signature_comparisons": comparisons,
        "top_families": [{"group": k[0], "shader_ids": list(k[1:]), "recorded_draws": n}
                         for k, n in families.most_common(20)],
        "limitations": [
            "Diagnostic snapshots/hooks inflate frame times. These are not performance benchmark results.",
            "Matching signatures do not prove common object identity, reusable constants or stereo eligibility.",
            "Mapped buffer contents are never read; observed write generations are not content hashes.",
            "Only VS/PS constant bindings and the first 16 PS SRV/sampler slots are recorded.",
            "Other-stage resources, UAV/RT writes and worker-context writes need separate dependency analysis.",
            "Indirect/DrawAuto arguments are not read; shader definitions created before hooks can be missing.",
            "Dropped records and incomplete/partial scopes limit coverage; no game draw is suppressed.",
        ],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    result = summarize(json.loads(args.capture.read_text(encoding="utf-8-sig")))
    print(json.dumps(result, indent=2))
