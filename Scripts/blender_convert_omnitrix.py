"""
Alien Museum - converts the downloaded classic Omnitrix (SourceArt/Downloaded/classic-omnitrix) into the
three parts of the player's wrist watch (Blender 5.2).

    blender -b --factory-startup --python Scripts/blender_convert_omnitrix.py -- <downloads> <output>

Writes to <output>:
  Omnitrix_Band.glb  the cuff and its four tubes (fixed on the wrist),
  Omnitrix_Core.glb  the faceplate ring with its four green lights (pops up, turns as the dial),
  Omnitrix_Face.glb  the face disc, with its own material slot "OmnitrixFace" and UVs that span the disc
                     (0..1, centre 0.5): the game draws the hourglass / diamond / silhouette on it,
  Omnitrix.json      where the parts sit (cm, Unreal mesh space) and how big the face is,
  Omnitrix.png       preview.
All three share one frame: the origin is the middle of the wrist hole, 1 unit = 1 cm, the face points up.
In Unreal (glTF import turned -90 yaw like the aliens) the forearm runs along Y, the side button faces +X.
The four thin plates in the file (Cube, Cube.001) never render in the original either and are left out.

Personal fan project: the Omnitrix belongs to Cartoon Network / Warner Bros. Discovery and the model to
its Sketchfab author. Keep this build private.
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import blender_models_common as common  # noqa: E402
import blender_convert_models as convert  # noqa: E402  (textures, joining, export)

SOURCE = os.path.join("classic-omnitrix", "source", "Classic OmnitrixSF.fbx")
TEXTURES = os.path.join("classic-omnitrix", "textures")
# The download shortened the texture names the FBX asks for.
SHORT_NAMES = {"BaseColor": "BaseCol", "Emissive": "Emissiv", "Metallic": "Metalli", "Normal": "Normal", "Roughness": "Roughne"}
PARTS = {"Band": ["Cylinder", "NurbsPath"], "Core": ["Cylinder.001"], "Face": ["Cylinder.002"]}
LEFT_OUT = ["Cube", "Cube.001"]
CM_PER_UNIT = 1.0  # the model is authored in centimetres


def relink_textures(downloads):
    for image in bpy.data.images:
        for full, short in SHORT_NAMES.items():
            if image.name.endswith(full + ".png"):
                image.filepath = os.path.join(downloads, TEXTURES, f"Classic_Omnitrix_Omnitrix_Material_{short}.png")
                image.reload()
                print("  texture", image.name, tuple(image.size))


def wrist_hole_centre(objects):
    """The middle of the wrist hole: rays from inside the band out to its inner wall, in the x = 0 plane."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    trees = []
    for obj in objects:
        mesh = obj.evaluated_get(depsgraph).to_mesh()
        trees.append(BVHTree.FromPolygons([obj.matrix_world @ v.co for v in mesh.vertices], [tuple(p.vertices) for p in mesh.polygons]))
        obj.evaluated_get(depsgraph).to_mesh_clear()
    lo, hi = common.world_bounds(objects)
    start = Vector((0.0, (lo.y + hi.y) * 0.5, lo.z + (hi.z - lo.z) * 0.45))
    hits = []
    for k in range(32):
        angle = 2.0 * math.pi * k / 32
        direction = Vector((0.0, math.cos(angle), math.sin(angle)))
        nearest = min((t.ray_cast(start, direction, 100.0) for t in trees), key=lambda h: h[3] if h[0] else 1e9)
        if nearest[0] is not None:
            hits.append(nearest[0])
    return sum(hits, Vector()) / len(hits)


def face_uvs(face):
    """Planar UVs across the face disc: (0, 0) .. (1, 1), the centre at (0.5, 0.5), V running along -Y
    (Unreal +X: towards the side button)."""
    lo, hi = common.world_bounds([face])
    centre = (lo + hi) * 0.5
    radius = max(hi.x - lo.x, hi.y - lo.y) * 0.5
    layer = face.data.uv_layers.active or face.data.uv_layers.new(name="UVMap")
    for loop in face.data.loops:
        co = face.matrix_world @ face.data.vertices[loop.vertex_index].co
        layer.data[loop.index].uv = (0.5 + (co.x - centre.x) / (2.0 * radius), 0.5 - (co.y - centre.y) / (2.0 * radius))
    material = bpy.data.materials.new("OmnitrixFace")
    face.data.materials.clear()
    face.data.materials.append(material)
    return centre, radius


def to_unreal(v):
    return convert.to_unreal(v, 100.0)


def main():
    args = common.args_after_double_dash()
    downloads, out_dir = os.path.abspath(args[0]), os.path.abspath(args[1])
    os.makedirs(out_dir, exist_ok=True)
    common.reset_scene()
    common.import_model(os.path.join(downloads, SOURCE))
    relink_textures(downloads)
    for name in LEFT_OUT:
        obj = bpy.data.objects.get(name)
        if obj is not None:
            bpy.data.objects.remove(obj, do_unlink=True)
            print("  left out", name)
    # Everything baked into world space (keeps each part where it is), then parts joined.
    for obj in convert.bake_to_static_meshes():
        obj.name = obj.name.replace("_baked", "")
    objects = {o.name: o for o in common.mesh_objects()}
    parts = {part: convert.join([objects[n] for n in names]) for part, names in PARTS.items()}
    for part, obj in parts.items():
        obj.name = obj.data.name = "Omnitrix_" + part

    # Origin in the middle of the wrist hole, centimetres -> metres (glTF).
    centre = wrist_hole_centre([parts["Band"]])
    move = Matrix.Scale(CM_PER_UNIT / 100.0, 4) @ Matrix.Translation(-centre)
    for obj in parts.values():
        obj.data.transform(move)
        obj.data.update()
    bpy.context.view_layer.update()
    face_centre, face_radius = face_uvs(parts["Face"])
    convert.shrink_textures(os.path.join(out_dir, "textures", "Omnitrix"))

    for part, obj in parts.items():
        convert.export_glb(os.path.join(out_dir, obj.name + ".glb"), [obj])
    band_lo, band_hi = common.world_bounds([parts["Band"]])
    core_lo, core_hi = common.world_bounds([parts["Core"]])
    band = parts["Band"]
    button = min((band.matrix_world @ v.co for v in band.data.vertices if abs((band.matrix_world @ v.co).x) < 0.006), key=lambda p: p.y)
    info = {
        "band": {"glb": os.path.join(out_dir, "Omnitrix_Band.glb"), "min": to_unreal(band_lo), "max": to_unreal(band_hi)},
        "core": {"glb": os.path.join(out_dir, "Omnitrix_Core.glb"), "base": to_unreal(Vector((0.0, (core_lo.y + core_hi.y) * 0.5, core_lo.z))),
                 "top": round((core_hi.z) * 100.0, 3)},
        "face": {"glb": os.path.join(out_dir, "Omnitrix_Face.glb"), "centre": to_unreal(face_centre), "radius": round(face_radius * 100.0, 3)},
        "button": to_unreal(button),
        "triangles": {p: common.triangle_count([o]) for p, o in parts.items()},
    }
    common.write_json(os.path.join(out_dir, "Omnitrix.json"), info)
    common.render_views(os.path.join(out_dir, "Omnitrix.png"), list(parts.values()), views=("TOP", "FRONT", "RIGHT"))
    print("OMNITRIX", json.dumps(info))


if __name__ == "__main__":
    main()
