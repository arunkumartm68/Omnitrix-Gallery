"""
Alien Museum - makes Four Arms' flex pose (Blender 5.x CLI, no rig needed).

    blender -b --factory-startup --python Scripts/blender_pose_fourarms.py -- <rest.glb> <pose.glb> <preview.png>

Takes the converted T-pose model (SourceArt/Converted/FourArms_2.glb) and bends its four forearms
around the elbows: the upper pair up into a double-biceps pose, the lower pair forwards across the
belly. Vertices near an elbow blend smoothly between the upper arm and the forearm (a two-bone skin
done by hand), so the mesh stays in one piece. The result keeps the rest model's frame (feet at the
origin, same centre and units), so the game can swap the two meshes without moving or rescaling.

The joint positions below are fractions of the model's height, measured on FourArms_2 (arms along
X, model facing -Y, Z up). Private fan use only - the model belongs to its Sketchfab author.
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

# Elbow (x, y, z) in height units on the +X side; the -X side is mirrored.
UPPER_ELBOW = (0.4715, 0.011, 0.8476)
LOWER_ELBOW = (0.3508, 0.017, 0.6315)
ARM_SPLIT_Z = 0.7437     # beyond the torso: above = upper arm, below = lower arm
BLEND = 0.035            # half width of the smooth bend around each elbow
UPPER_CURL = 110.0       # upper forearms: up and slightly in, over the shoulders
LOWER_RAISE = 40.0       # lower forearms: lifted to just above horizontal...
LOWER_SWING = 115.0      # ...then swung forwards and in, across the belly


def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def main():
    args = common.args_after_double_dash()
    rest_path, pose_path, preview = args[0], args[1], args[2]
    common.reset_scene()
    bpy.ops.import_scene.gltf(filepath=rest_path)
    meshes = common.mesh_objects()
    if len(meshes) != 1:
        raise SystemExit(f"expected one mesh, got {len(meshes)}")
    obj = meshes[0]
    mesh = obj.data
    lo, hi = common.world_bounds([obj])
    height = hi.z - lo.z
    print(f"POSE rest height {height:.4f} m, span {hi.x - lo.x:.4f} m, custom normals {mesh.has_custom_normals}")

    count = len(mesh.vertices)
    co = np.zeros(count * 3)
    mesh.vertices.foreach_get("co", co)
    co = co.reshape(count, 3)

    # One bend per forearm: (vertex weights, elbow pivot, full rotation). The masks don't overlap.
    split = ARM_SPLIT_Z * height
    bends = []
    for side in (1.0, -1.0):
        on_side = (co[:, 0] * side) > 0.0
        upper = on_side & (co[:, 2] > split)
        lower = on_side & (co[:, 2] <= split) & (co[:, 2] > 0.3 * height)
        curl = Quaternion((0.0, 1.0, 0.0), math.radians(-side * UPPER_CURL))
        swing = Quaternion((0.0, 0.0, 1.0), math.radians(-side * LOWER_SWING)) @ Quaternion((0.0, 1.0, 0.0), math.radians(-side * LOWER_RAISE))
        for mask, elbow, rotation in ((upper, UPPER_ELBOW, curl), (lower, LOWER_ELBOW, swing)):
            along = co[:, 0] * side
            start = (elbow[0] - BLEND) * height
            weights = smoothstep((along - start) / (2.0 * BLEND * height)) * mask
            pivot = Vector((side * elbow[0] * height, elbow[1] * height, elbow[2] * height))
            bends.append((weights, pivot, rotation))

    rotations = {}
    posed = co.copy()
    for weights, pivot, rotation in bends:
        for i in np.nonzero(weights > 0.0)[0]:
            q = Quaternion().slerp(rotation, float(weights[i]))  # identity -> full bend
            posed[i] = np.array(pivot + q @ (Vector(co[i]) - pivot))
            rotations[int(i)] = q
    print(f"POSE bent {len(rotations)} of {count} vertices")

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
    print(f"POSE bounds x {lo2.x:.4f}..{hi2.x:.4f}  y {lo2.y:.4f}..{hi2.y:.4f}  z {lo2.z:.4f}..{hi2.z:.4f}")
    convert.export_glb(pose_path)
    common.render_views(preview, [obj])
    print("POSE DONE", pose_path)


if __name__ == "__main__":
    main()
