"""
Alien Museum - shared Blender helpers for turning downloaded models into Unreal-ready .glb files.

Used by Scripts/blender_inspect_models.py and Scripts/blender_convert_models.py (Blender 5.2, run with
blender -b --factory-startup --python <script> -- <args>).
"""
import json
import math
import os
import re

import bpy
import numpy as np
from mathutils import Vector

MODEL_EXTENSIONS = (".fbx", ".obj", ".gltf", ".glb", ".blend")
IMAGE_EXTENSIONS = (".png", ".jpg", ".jpeg", ".tga", ".bmp", ".tif", ".tiff")


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def find_model_file(folder):
    """The model file inside an extracted download (prefers the 'source' folder)."""
    candidates = []
    for root, _dirs, files in os.walk(folder):
        for name in files:
            if name.lower().endswith(MODEL_EXTENSIONS):
                candidates.append(os.path.join(root, name))
    candidates.sort(key=lambda p: (os.sep + "source" + os.sep not in p, p))
    return candidates[0] if candidates else None


def import_model(path):
    """Imports any supported file into the current scene. Returns the importer that worked."""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".fbx":
        try:
            bpy.ops.import_scene.fbx(filepath=path, use_image_search=True, automatic_bone_orientation=True)
            return "import_scene.fbx"
        except Exception as exc:  # ASCII / very old FBX: the C++ importer handles those
            print("  python FBX importer failed:", exc)
            bpy.ops.wm.fbx_import(filepath=path)
            return "wm.fbx_import"
    if ext == ".obj":
        bpy.ops.wm.obj_import(filepath=path)
        return "wm.obj_import"
    if ext in (".gltf", ".glb"):
        bpy.ops.import_scene.gltf(filepath=path)
        return "import_scene.gltf"
    if ext == ".blend":
        with bpy.data.libraries.load(path, link=False) as (data_from, data_to):
            data_to.objects = list(data_from.objects)
        for obj in data_to.objects:
            if obj is not None and obj.type in {"MESH", "ARMATURE", "EMPTY", "CURVE"}:
                bpy.context.scene.collection.objects.link(obj)
        bpy.ops.file.find_missing_files(directory=os.path.dirname(path))
        return "blend append"
    raise RuntimeError("unsupported file " + path)


def _image_index(folder):
    """basename (lower case) -> full path for every image below the download folder."""
    index = {}
    for root, _dirs, files in os.walk(folder):
        for name in files:
            if name.lower().endswith(IMAGE_EXTENSIONS):
                index.setdefault(name.lower(), os.path.join(root, name))
    return index


def _candidate_names(image):
    raw = image.filepath_raw or image.filepath or image.name
    base = re.split(r"[\\/]", raw)[-1].lower()
    stem = os.path.splitext(base)[0]
    names = [base, base + ".png", stem + ".png", image.name.lower(), image.name.lower() + ".png"]
    return [n for n in names if n]


def image_ok(image):
    if image.packed_file is not None:
        return True
    try:
        return image.has_data or (image.size[0] > 0 and image.size[1] > 0)
    except Exception:
        return False


def relink_missing_images(download_folder):
    """Points images whose file was not found at a same-named file anywhere in the download."""
    index = _image_index(download_folder)
    fixed, missing = [], []
    for image in bpy.data.images:
        if image.source != "FILE" or image_ok(image):
            continue
        names = _candidate_names(image)
        # Sketchfab renames converted textures, e.g. "skin.psd" -> "skin.tga.png".
        stem = os.path.splitext(names[0])[0]
        names += sorted(k for k in index if k.startswith(stem + ".") and k.endswith(".png"))
        for name in names:
            if name in index:
                image.filepath = index[name]
                image.reload()
                if image_ok(image):
                    fixed.append((image.name, os.path.basename(index[name])))
                    break
        else:
            missing.append(image.filepath_raw or image.name)
    return fixed, missing


def mesh_objects():
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def triangle_count(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    total = 0
    for obj in objects:
        mesh = obj.evaluated_get(depsgraph).data
        total += sum(len(p.vertices) - 2 for p in mesh.polygons)
    return total


def world_bounds(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    lo = Vector((math.inf,) * 3)
    hi = Vector((-math.inf,) * 3)
    for obj in objects:
        evaluated = obj.evaluated_get(depsgraph)
        for corner in evaluated.bound_box:
            p = evaluated.matrix_world @ Vector(corner)
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
    return lo, hi


def material_report(objects):
    report = []
    seen = set()
    for obj in objects:
        for slot in obj.material_slots:
            mat = slot.material
            if mat is None or mat.name in seen:
                continue
            seen.add(mat.name)
            images = []
            if mat.use_nodes and mat.node_tree:
                for node in mat.node_tree.nodes:
                    if node.type == "TEX_IMAGE" and node.image:
                        images.append("%s%s" % (node.image.name, "" if image_ok(node.image) else " [MISSING]"))
            report.append({"material": mat.name, "images": images})
    return report


def _set_engine(scene, wanted):
    try:
        scene.render.engine = wanted
    except TypeError as exc:
        print("  engine", wanted, "not accepted:", exc)


def render_views(out_path, objects, views=("FRONT", "RIGHT"), size=420):
    """Workbench renders (textured) from Blender's Front (-Y) and Right (+X) views, side by side."""
    scene = bpy.context.scene
    _set_engine(scene, "BLENDER_WORKBENCH")
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "TEXTURE"
    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.film_transparent = False
    if scene.world is None:
        scene.world = bpy.data.worlds.new("PreviewWorld")
    scene.world.color = (0.32, 0.34, 0.38)
    scene.render.image_settings.file_format = "PNG"

    lo, hi = world_bounds(objects)
    center = (lo + hi) * 0.5
    dims = hi - lo
    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.type = "ORTHO"
    cam = bpy.data.objects.new("PreviewCam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam_data.clip_start = 0.001
    cam_data.clip_end = max(dims) * 20 + 10

    tiles = []
    tmp = out_path + ".tmp.png"
    for view in views:
        if view == "FRONT":
            cam.rotation_euler = (math.radians(90), 0, 0)
            cam.location = center + Vector((0, -max(dims) * 3 - 1, 0))
            cam_data.ortho_scale = max(dims.x, dims.z) * 1.15
        elif view == "RIGHT":
            cam.rotation_euler = (math.radians(90), 0, math.radians(90))
            cam.location = center + Vector((max(dims) * 3 + 1, 0, 0))
            cam_data.ortho_scale = max(dims.y, dims.z) * 1.15
        elif view == "TOP":
            cam.rotation_euler = (0, 0, 0)
            cam.location = center + Vector((0, 0, max(dims) * 3 + 1))
            cam_data.ortho_scale = max(dims.x, dims.y) * 1.15
        scene.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp)
        pixels = np.array(img.pixels[:], dtype=np.float32).reshape(size, size, 4)
        bpy.data.images.remove(img)
        tiles.append(pixels)
    sheet = np.concatenate(tiles, axis=1)
    out = bpy.data.images.new("PreviewSheet", width=sheet.shape[1], height=sheet.shape[0], alpha=True)
    out.pixels = sheet.ravel()
    out.filepath_raw = out_path
    out.file_format = "PNG"
    out.save()
    bpy.data.images.remove(out)
    bpy.data.objects.remove(cam)
    if os.path.exists(tmp):
        os.remove(tmp)


def args_after_double_dash():
    import sys
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def write_json(path, data):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
