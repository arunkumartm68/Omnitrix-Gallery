"""
Alien Museum - re-poses converted models that have no usable rig (Blender 5.x CLI).

    blender -b --factory-startup --python Scripts/blender_pose_models.py -- <converted dir> [pose id ...]

Reads <converted dir>/<source>.glb (from blender_convert_models.py) and writes <pose id>.glb and a
front + right preview <pose id>.png next to it, for every pose below (or only the named ones), and
lists them in <converted dir>/poses.json for create_habitats_and_moves.py:

  FourArms_2_Flex   Four Arms' double-biceps show-off (the Flex move): upper forearms up over the
                    shoulders, lower forearms across the belly
  FourArms_2_Rest   Four Arms standing relaxed: all four arms down at his sides
  Wildvine_Rest     Wildvine's vine arms hanging down instead of stretched out

Each bend turns one limb around a joint. Vertices near the joint blend smoothly between the still
body and the turned limb (a two-bone skin done by hand), so the mesh stays in one piece, and the
imported corner normals turn with their vertices so the shading stays the same. The result keeps
the source model's frame (feet at the origin, same centre and units), so the game can use it in
place of the source model or swap between them without moving or rescaling anything.

Joints are fractions of the model's height, measured on the converted models (T-pose arms along X,
model facing -Y, Z up), for the +X side; the -X side is mirrored. Private fan use only - the models
belong to their Sketchfab authors.
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_models_common as common  # noqa: E402
import blender_convert_models as convert  # noqa: E402

BLEND = 0.035   # half width (height units) of the smooth bend around each joint

# use: "pose" = shown during the alien's show-off move (AlienDataAsset.PoseMesh), "model" = replaces
# the model itself (ModelMesh). joint: (x, y, z) on the +X side. band: the heights (low, high] the
# limb occupies - only vertices in it, beyond the joint, move. turns: (axis, degrees) applied in
# order, for the +X side.
POSES = {
    "FourArms_2_Flex": dict(source="FourArms_2", use="pose", bends=[
        # Upper forearms curl up and slightly in, over the shoulders (at the elbows).
        dict(joint=(0.4715, 0.011, 0.8476), band=(0.7437, None), turns=[("Y", -110.0)]),
        # Lower forearms: lifted to just above horizontal, then swung forwards and in across the belly.
        dict(joint=(0.3508, 0.017, 0.6315), band=(0.3, 0.7437), turns=[("Y", -40.0), ("Z", -115.0)]),
    ]),
    "FourArms_2_Rest": dict(source="FourArms_2", use="model", bends=[
        # Upper arms down at the sides (at the shoulders)...
        dict(joint=(0.21, 0.011, 0.856), band=(0.7437, None), turns=[("Y", 62.0)]),
        # ...and the lower pair hanging inside and below them, clear of the upper arms.
        dict(joint=(0.21, 0.017, 0.674), band=(0.3, 0.7437), turns=[("Y", 40.0)]),
    ]),
    "Wildvine_Rest": dict(source="Wildvine", use="model", bends=[
        # The long vine arms droop from the shoulders towards the ground.
        dict(joint=(0.15, -0.066, 0.705), band=(0.58, 0.78), turns=[("Y", 45.0)]),
    ]),
}

AXES = {"X": (1.0, 0.0, 0.0), "Y": (0.0, 1.0, 0.0), "Z": (0.0, 0.0, 1.0)}


def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def side_rotation(turns, side):
    """The limb's full turn on one side. Mirroring across X keeps turns about X and reverses the
    ones about Y and Z."""
    rotation = Quaternion()
    for axis, degrees in turns:
        angle = math.radians(degrees if axis == "X" else degrees * side)
        rotation = Quaternion(AXES[axis], angle) @ rotation
    return rotation


def make_pose(pose_id, spec, folder):
    source = os.path.join(folder, spec["source"] + ".glb")
    common.reset_scene()
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = common.mesh_objects()
    if len(meshes) != 1:
        raise SystemExit(f"{source}: expected one mesh, got {len(meshes)}")
    obj = meshes[0]
    mesh = obj.data
    lo, hi = common.world_bounds([obj])
    height = hi.z - lo.z
    print(f"POSE {pose_id} from {spec['source']}: height {height:.4f} m, span {hi.x - lo.x:.4f} m, custom normals {mesh.has_custom_normals}")

    count = len(mesh.vertices)
    co = np.zeros(count * 3)
    mesh.vertices.foreach_get("co", co)
    co = co.reshape(count, 3)

    # One bend per limb and side: (vertex weights, joint, full rotation). The bands don't overlap.
    bends = []
    for bend in spec["bends"]:
        low, high = bend["band"]
        joint = bend["joint"]
        for side in (1.0, -1.0):
            mask = ((co[:, 0] * side) > 0.0) & (co[:, 2] > low * height)
            if high is not None:
                mask &= co[:, 2] <= high * height
            start = (joint[0] - BLEND) * height
            weights = smoothstep((co[:, 0] * side - start) / (2.0 * BLEND * height)) * mask
            pivot = Vector((side * joint[0] * height, joint[1] * height, joint[2] * height))
            bends.append((weights, pivot, side_rotation(bend["turns"], side)))

    rotations = {}
    posed = co.copy()
    for weights, pivot, rotation in bends:
        for i in np.nonzero(weights > 0.0)[0]:
            q = Quaternion().slerp(rotation, float(weights[i]))  # identity -> full bend
            posed[i] = np.array(pivot + q @ (Vector(co[i]) - pivot))
            rotations[int(i)] = q
    print(f"POSE {pose_id}: bent {len(rotations)} of {count} vertices")

    # Keep the imported shading: turn each corner normal with its vertex.
    if mesh.has_custom_normals:
        normals = np.zeros(len(mesh.loops) * 3)
        mesh.corner_normals.foreach_get("vector", normals)
        normals = normals.reshape(len(mesh.loops), 3)
        loop_vertex = np.zeros(len(mesh.loops), dtype=np.int64)
        mesh.loops.foreach_get("vertex_index", loop_vertex)
        for loop, vertex in enumerate(loop_vertex):
            q = rotations.get(int(vertex))
            if q is not None:
                normals[loop] = np.array(q @ Vector(normals[loop]))
        mesh.vertices.foreach_set("co", posed.reshape(-1))
        mesh.normals_split_custom_set([tuple(n) for n in normals])
    else:
        mesh.vertices.foreach_set("co", posed.reshape(-1))
    mesh.update()
    bpy.context.view_layer.update()

    lo2, hi2 = common.world_bounds([obj])
    print(f"POSE {pose_id} bounds x {lo2.x:.4f}..{hi2.x:.4f}  y {lo2.y:.4f}..{hi2.y:.4f}  z {lo2.z:.4f}..{hi2.z:.4f}")
    out = os.path.join(folder, pose_id + ".glb")
    convert.export_glb(out)
    common.render_views(os.path.join(folder, pose_id + ".png"), [obj])
    print("POSE DONE", out)

    # With its limbs in, a relaxed model can be shown taller: the converter's height, unless the
    # pose is still wider than the case allows (the same rule as the converter's).
    target = next((cfg["height"] for cfg in convert.MODELS.values() if cfg["id"] == spec["source"]), None)
    span = max(hi2.x - lo2.x, hi2.y - lo2.y)
    height_cm = None
    if spec["use"] == "model" and target:
        height_cm = round(min(target, convert.MAX_SPAN * (hi2.z - lo2.z) / span) if span > 0 else target, 1)
    return {"id": pose_id, "source": spec["source"], "use": spec["use"], "glb": out, "height_cm": height_cm}


def main():
    import json
    args = common.args_after_double_dash()
    folder = os.path.abspath(args[0])
    only = set(args[1:])
    listing_path = os.path.join(folder, "poses.json")
    listing = {}
    if os.path.exists(listing_path):
        with open(listing_path, encoding="utf-8") as handle:
            listing = {p["id"]: p for p in json.load(handle)}
    for pose_id, spec in POSES.items():
        if not only or pose_id in only:
            listing[pose_id] = make_pose(pose_id, spec, folder)
    common.write_json(listing_path, sorted(listing.values(), key=lambda p: p["id"]))


if __name__ == "__main__":
    main()
