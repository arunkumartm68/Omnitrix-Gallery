"""
Alien Museum - inspects every downloaded model (Blender 5.2, background mode).

    blender -b --factory-startup --python Scripts/blender_inspect_models.py -- <downloads folder> <output folder>

For each sub-folder of <downloads folder>: imports the model, re-links textures that were not found,
and writes <output folder>/<name>.png (front + right view) and <output folder>/inspection.json
(triangles, rig, animations, materials, missing textures, size).
"""
import os
import sys
import traceback

import bpy

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import blender_models_common as common  # noqa: E402


def inspect(folder, out_dir):
    name = os.path.basename(folder)
    path = common.find_model_file(folder)
    info = {"name": name, "file": path}
    if not path:
        info["error"] = "no model file"
        return info

    common.reset_scene()
    info["importer"] = common.import_model(path)
    fixed, missing = common.relink_missing_images(folder)
    info["relinked"] = fixed
    info["missing_images"] = missing

    meshes = common.mesh_objects()
    armatures = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    info["mesh_objects"] = len(meshes)
    info["triangles"] = common.triangle_count(meshes)
    info["armatures"] = [{"name": a.name, "bones": len(a.data.bones)} for a in armatures]
    info["skinned_meshes"] = sum(1 for o in meshes if any(m.type == "ARMATURE" for m in o.modifiers))
    info["actions"] = [{"name": a.name, "frames": list(a.frame_range)} for a in bpy.data.actions]
    info["shape_keys"] = sum(1 for o in meshes if o.data.shape_keys)
    lo, hi = common.world_bounds(meshes)
    info["bounds_min"] = [round(v, 4) for v in lo]
    info["bounds_max"] = [round(v, 4) for v in hi]
    info["size"] = [round(v, 4) for v in (hi - lo)]
    info["materials"] = common.material_report(meshes)
    common.render_views(os.path.join(out_dir, name + ".png"), meshes)
    return info


def main():
    downloads, out_dir = common.args_after_double_dash()[:2]
    os.makedirs(out_dir, exist_ok=True)
    results = []
    for entry in sorted(os.listdir(downloads)):
        folder = os.path.join(downloads, entry)
        if not os.path.isdir(folder):
            continue
        print("INSPECT", entry)
        try:
            info = inspect(folder, out_dir)
        except Exception:
            info = {"name": entry, "error": traceback.format_exc()}
        print("INSPECT result", entry, info.get("triangles"), info.get("error", "")[:300])
        results.append(info)
        common.write_json(os.path.join(out_dir, "inspection.json"), results)


main()
