"""
Alien Museum - converts the downloaded Ben 10 models into Unreal-ready .glb files (Blender 5.2).

    blender -b --factory-startup --python Scripts/blender_convert_models.py -- <downloads> <output> [model folder ...]

<downloads> holds one extracted folder per downloaded zip (SourceArt/Downloaded). For every model in
MODELS below (or only the named ones) this script:
  1. imports it and re-links textures that were not found,
  2. fixes orientation (faces Blender -Y = Unreal +X after import), textures and materials,
  3. resets the rig, lowers T-pose arms and bakes the pose into a plain static mesh,
  4. reduces the triangle count to the Quest budget and the texture size to MAX_BASE / MAX_OTHER,
  5. scales it so it is `height` cm tall (smaller if its pose is wider than MAX_SPAN), feet at the origin,
  6. writes <output>/<Id>.glb, <output>/<Id>.png (front + right preview) and <output>/manifest.json.
"""
import math
import os
import sys
import traceback

import bpy
from mathutils import Euler, Matrix, Vector

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import blender_models_common as common  # noqa: E402

MAX_TRIANGLES = 60000   # per model; 8 chambers stay well inside the Quest 3S budget
MAX_BASE = 2048         # base colour textures
MAX_OTHER = 1024        # normal / roughness / specular / emissive textures
MAX_SPAN = 56.0         # cm: widest pose that still fits the default chamber with room to turn

# One entry per downloaded zip. height = display height in cm (at chamber scale 1).
# rotate = degrees (X, Y, Z) so the model stands up and faces Blender's front (-Y).
# pose = upper-arm bone -> degrees to lower it (baked T-pose -> relaxed pose).
MODELS = {
    "ben-10-cannonbolt": dict(id="Cannonbolt_1", alien="Cannonbolt", height=50),
    "cannonbolt": dict(id="Cannonbolt_2", alien="Cannonbolt", height=50,
                       textures={"CANONBOLT.001": "CANNONBOLT_shaddddd.png"}),
    "ben-10-wildwine": dict(id="Wildvine", alien="Wildvine", height=60),
    "ben-10-xlr8-version-2": dict(id="XLR8_1", alien="XLR8", height=55),
    "xlr8-ben-10": dict(id="XLR8_2", alien="XLR8", height=55, pose={"braço": 60, "braço_2": 60}),
    "classic-diamondhead-thanhtp-edit": dict(id="Diamondhead_1", alien="Diamondhead", height=60),
    "diamondhead-ben-10": dict(id="Diamondhead_2", alien="Diamondhead", height=60),
    "clone-idem-ditto-ben-10": dict(id="Ditto", alien="Ditto", height=38, pose={"Braço": 60, "Braço_2": 60}),
    "fourarm-ben10-classic": dict(id="FourArms_1", alien="Four Arms", height=65,
                                  textures={"standardSurface4SG": "smoothfull3Shape_baseColor.png",
                                            "standardSurface2SG": "pCylinderShape1_standardSurface2_baseColor.png",
                                            "standardSurface3SG": "pCylinderShape1_standardSurface3_baseColor.png"},
                                  colors={"standardSurface5SG": (1.0, 0.78, 0.12), "standardSurface6SG": (0.04, 0.04, 0.045)}),
    "fourarms-ben-10": dict(id="FourArms_2", alien="Four Arms", height=65, rotate=(90, 0, 0), no_emission=True),
    "funko-pop-ben10-echo-echo-free": dict(id="EchoEcho", alien="Echo Echo", height=35, max_triangles=50000),
    "ghostfreak": dict(id="Ghostfreak", alien="Ghostfreak", height=58, rotate=(0, 0, -90), rebuild_materials=True,
                       pose={"bip_upperArm_L": 60, "bip_upperArm_R": 60}),
    "glutao-upchuck": dict(id="Upchuck", alien="Upchuck", height=38, pose={"Braço": 60, "Braço_2": 60}),
    "grey-matter-ben-10-vilgax-attacks-fan-model": dict(id="GreyMatter", alien="Grey Matter", height=20),
    "ripjaws-ben-10": dict(id="Ripjaws", alien="Ripjaws", height=55),
    "upgrade-ben-10-classic": dict(id="Upgrade_1", alien="Upgrade", height=52, rotate=(0, 0, -90),
                                   pose={"bip_upperarm_L": 60, "bip_upperarm_R": 60}),
    "upgrade-ben-10-vilgax-attacks-fan-model": dict(id="Upgrade_2", alien="Upgrade", height=52),
    "wildmutt": dict(id="Wildmutt", alien="Wildmutt", height=40, rotate=(0, 0, -90),
                     pose={"bip_upperArm_L": (50, "forward"), "bip_upperArm_R": (50, "forward")}),
}


# ---------------------------------------------------------------------------------------------
# Materials and textures
# ---------------------------------------------------------------------------------------------

def principled(mat):
    """The Principled BSDF the material actually outputs (falls back to any Principled node)."""
    if not mat or not mat.use_nodes or not mat.node_tree:
        return None
    _out, surface = surface_node(mat)
    if surface is not None and surface.type == "BSDF_PRINCIPLED":
        return surface
    return next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)


def assign_texture(mat, image_path):
    bsdf = principled(mat)
    if not bsdf:
        print("  no Principled BSDF in", mat.name)
        return
    tree = mat.node_tree
    node = tree.nodes.new("ShaderNodeTexImage")
    node.image = bpy.data.images.load(image_path, check_existing=True)
    tree.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
    print("  texture", os.path.basename(image_path), "->", mat.name)


def surface_node(mat):
    """The shader node plugged into the active material output (what the glTF exporter reads)."""
    out = next((n for n in mat.node_tree.nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output), None)
    if out is None or not out.inputs["Surface"].is_linked:
        return out, None
    return out, out.inputs["Surface"].links[0].from_node


def rebuild_simple(mat):
    """Replaces the material's shader with a plain Principled BSDF (base colour texture or colour).
    Used for game-engine shader groups (e.g. Source VertexLitGeneric) the glTF exporter cannot read."""
    tree = mat.node_tree
    out, old = surface_node(mat)
    if out is None:
        out = tree.nodes.new("ShaderNodeOutputMaterial")
    image_node = None
    colour = tuple(mat.diffuse_color)
    if old is not None:
        for socket in old.inputs:
            for link in socket.links:
                node = link.from_node
                if image_node is None and node.type == "TEX_IMAGE" and node.image and common.image_ok(node.image):
                    image_node = node
        if old.type == "BSDF_PRINCIPLED":
            colour = tuple(old.inputs["Base Color"].default_value)
    bsdf = tree.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.6
    if image_node is not None:
        tree.links.new(image_node.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = colour
    tree.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    if old is not None and old != bsdf:
        tree.nodes.remove(old)
    print("  rebuilt material", mat.name, "->", image_node.image.name if image_node else tuple(round(c, 2) for c in colour[:3]))


def fix_materials(cfg, folder):
    index = {}
    for root, _dirs, files in os.walk(folder):
        for name in files:
            index.setdefault(name.lower(), os.path.join(root, name))
    for mat in bpy.data.materials:
        if mat.use_nodes and mat.node_tree:
            _out, surface = surface_node(mat)
            if cfg.get("rebuild_materials") or (surface is not None and surface.type != "BSDF_PRINCIPLED"):
                rebuild_simple(mat)
        if mat.name in cfg.get("textures", {}):
            assign_texture(mat, index[cfg["textures"][mat.name].lower()])
        if mat.name in cfg.get("colors", {}):
            bsdf = principled(mat)
            if bsdf:
                bsdf.inputs["Base Color"].default_value = (*cfg["colors"][mat.name], 1.0)
        bsdf = principled(mat)
        if not bsdf:
            continue
        tree = mat.node_tree
        # Images that could not be found (author's local paths): drop them, keep the material colour.
        for node in list(tree.nodes):
            if node.type == "TEX_IMAGE" and (node.image is None or not common.image_ok(node.image)):
                print("  removing missing image node", node.image.name if node.image else "-", "in", mat.name)
                tree.nodes.remove(node)
        emission = bsdf.inputs.get("Emission Color")
        strength = bsdf.inputs.get("Emission Strength")
        if emission is not None:
            if cfg.get("no_emission"):
                for link in list(emission.links):
                    tree.links.remove(link)
            # A flat emission colour without a texture is an exporter default (FBX EmissiveColor), not a
            # glow the artist painted: it would wash the whole model out. Emission maps are kept.
            if not emission.is_linked and max(emission.default_value[:3]) > 0.0:
                print("  removing flat emission", tuple(round(v, 2) for v in emission.default_value[:3]), "in", mat.name)
                emission.default_value = (0, 0, 0, 1)
            if emission.is_linked:
                print("  keeps emission map in", mat.name)
        # Characters are not glass, lacquer or velvet: transmission / coat / sheen become Unreal's
        # thin-translucent / clear-coat shading (dark, see-through, dithered, and costly on Quest).
        for name in ("Transmission Weight", "Coat Weight", "Sheen Weight"):
            socket = bsdf.inputs.get(name)
            if socket is not None and (socket.is_linked or socket.default_value > 0.0):
                for link in list(socket.links):
                    tree.links.remove(link)
                print("  removing", name.lower(), round(socket.default_value, 2), "in", mat.name)
                socket.default_value = 0.0
        # A flat metallic value without a map is usually an FBX "reflection factor", not real metal.
        # Metal needs something to reflect; in passthrough there is nothing, so it renders dark.
        metallic = bsdf.inputs.get("Metallic")
        if metallic is not None and not metallic.is_linked and metallic.default_value > 0.0:
            print("  removing flat metallic", round(metallic.default_value, 2), "in", mat.name)
            metallic.default_value = 0.0
        alpha = bsdf.inputs.get("Alpha")
        if alpha is not None:
            for link in list(alpha.links):
                node = link.from_node
                if node.type == "TEX_IMAGE" and node.image and common.image_ok(node.image) and alpha_is_opaque(node.image):
                    # Texture alpha that is (almost) fully opaque would still make the material translucent.
                    tree.links.remove(link)
                    alpha.default_value = 1.0
                    print("  dropped opaque texture alpha in", mat.name)
            if alpha.is_linked or alpha.default_value < 0.999:
                print("  note: material really uses alpha", mat.name)


def alpha_is_opaque(image, threshold=0.8):
    """True when no pixel of the image is more transparent than the threshold."""
    if image.channels < 4:
        return True
    import numpy as np
    alpha = np.array(image.pixels[:], dtype=np.float32).reshape(-1, 4)[:, 3]
    return float(alpha.min()) >= threshold


def image_roles():
    """image -> largest allowed size, from the sockets each image node feeds."""
    limits = {}
    for mat in bpy.data.materials:
        if not mat.use_nodes or not mat.node_tree:
            continue
        for node in mat.node_tree.nodes:
            if node.type != "TEX_IMAGE" or node.image is None:
                continue
            limit = MAX_OTHER
            for out in node.outputs:
                for link in out.links:
                    if link.to_socket.name == "Base Color":
                        limit = MAX_BASE
            limits[node.image] = max(limits.get(node.image, 0), limit)
    return limits


def power_of_two_size(width, height, limit):
    """Nearest power-of-two size for each side (Unreal's glTF import and Quest mipmaps need it),
    halved together until the longer side fits the limit."""
    def nearest(n):
        return 1 << max(2, int(round(math.log2(max(n, 1)))))
    new_w, new_h = nearest(width), nearest(height)
    while max(new_w, new_h) > limit:
        new_w, new_h = max(4, new_w // 2), max(4, new_h // 2)
    return new_w, new_h


def shrink_textures(texture_dir):
    os.makedirs(texture_dir, exist_ok=True)
    count = 0
    for image, limit in image_roles().items():
        if not common.image_ok(image):
            continue
        width, height = image.size
        if width == 0 or height == 0:
            continue
        new_w, new_h = power_of_two_size(width, height, limit)
        if (new_w, new_h) != (width, height):
            image.scale(new_w, new_h)
        # Always write a PNG next to the output so the exporter never needs PSD/TGA/packed sources.
        safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in os.path.splitext(image.name)[0])
        path = os.path.join(texture_dir, "%s_%d.png" % (safe, count))
        image.filepath_raw = path
        image.file_format = "PNG"
        image.save()
        if image.packed_file is not None:
            # Textures embedded in the FBX stay packed, and the glTF exporter would write the packed
            # original (full size). Swap in the resized file everywhere the packed image was used.
            fresh = bpy.data.images.load(path, check_existing=False)
            fresh.colorspace_settings.name = image.colorspace_settings.name
            image.user_remap(fresh)
        count += 1
    return count


# ---------------------------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------------------------

def top_level_objects():
    return [o for o in bpy.context.scene.objects if o.parent is None]


def apply_rotation(degrees):
    if not degrees or not any(degrees):
        return
    rot = Euler([math.radians(d) for d in degrees], "XYZ").to_matrix().to_4x4()
    for obj in top_level_objects():
        obj.matrix_world = rot @ obj.matrix_world
    bpy.context.view_layer.update()


def reset_rigs():
    for obj in bpy.context.scene.objects:
        obj.animation_data_clear()
        if obj.type == "MESH" and obj.data.shape_keys:
            obj.data.shape_keys.animation_data_clear()
        if obj.type == "ARMATURE":
            for pb in obj.pose.bones:
                pb.location = (0, 0, 0)
                pb.rotation_quaternion = (1, 0, 0, 0)
                pb.rotation_euler = (0, 0, 0)
                pb.rotation_axis_angle = (0, 0, 1, 0)
                pb.scale = (1, 1, 1)
    bpy.context.view_layer.update()


def pose_arms(pose, meshes):
    """pose: bone -> degrees (lower the limb towards the body) or (degrees, "forward") (swing it
    forward around the vertical axis, keeping its height). The character faces -Y, its left is +X."""
    if not pose:
        return
    lo, hi = common.world_bounds(meshes)
    center_x = (lo.x + hi.x) * 0.5
    for arm in [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]:
        to_arm = arm.matrix_world.to_3x3().inverted()
        front_back = (to_arm @ Vector((0, 1, 0))).normalized()
        vertical = (to_arm @ Vector((0, 0, 1))).normalized()
        for bone_name, setting in pose.items():
            degrees, mode = setting if isinstance(setting, tuple) else (setting, "lower")
            pb = arm.pose.bones.get(bone_name)
            if pb is None:
                print("  pose: bone not found", bone_name)
                continue
            side = 1.0 if (arm.matrix_world @ pb.head).x > center_x else -1.0
            if mode == "forward":
                rot = Matrix.Rotation(-math.radians(degrees) * side, 4, vertical)
            else:
                rot = Matrix.Rotation(math.radians(degrees) * side, 4, front_back)
            head = pb.head.copy()
            pb.matrix = Matrix.Translation(head) @ rot @ Matrix.Translation(-head) @ pb.matrix
            bpy.context.view_layer.update()
            print("  pose:", bone_name, mode, degrees, "deg")


def bake_to_static_meshes():
    """Replaces every mesh with its evaluated result (pose, shape keys, modifiers) in world space."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    baked = []
    for obj in common.mesh_objects():
        evaluated = obj.evaluated_get(depsgraph)
        mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=depsgraph)
        matrix = evaluated.matrix_world.copy()
        mesh.transform(matrix)
        if matrix.determinant() < 0:
            mesh.flip_normals()
        baked.append(bpy.data.objects.new(obj.name + "_baked", mesh))
    for obj in list(bpy.context.scene.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for obj in baked:
        bpy.context.scene.collection.objects.link(obj)
    bpy.context.view_layer.update()
    return baked


def join(objects):
    if len(objects) == 1:
        return objects[0]
    target = max(objects, key=lambda o: len(o.data.polygons))
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = target
    with bpy.context.temp_override(active_object=target, object=target, selected_objects=objects,
                                   selected_editable_objects=objects):
        bpy.ops.object.join()
    return target


def decimate(obj, max_triangles):
    tris = common.triangle_count([obj])
    if tris <= max_triangles:
        return tris
    mod = obj.modifiers.new("Decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = max_triangles / float(tris)
    mod.use_collapse_triangulate = True
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), preserve_all_data_layers=True, depsgraph=depsgraph)
    old = obj.data
    obj.modifiers.remove(mod)
    obj.data = mesh
    bpy.data.meshes.remove(old)
    after = common.triangle_count([obj])
    print("  decimated", tris, "->", after)
    return after


def normalize(obj, target_height_cm):
    """Feet at the origin, centred, target height (smaller if the pose is too wide). Returns cm height."""
    lo, hi = common.world_bounds([obj])
    size = hi - lo
    span = max(size.x, size.y)
    height_cm = min(target_height_cm, MAX_SPAN * size.z / span) if span > 0 else target_height_cm
    scale = (height_cm / 100.0) / size.z
    obj.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z))))
    obj.data.transform(Matrix.Scale(scale, 4))
    obj.data.update()
    bpy.context.view_layer.update()
    return height_cm


# ---------------------------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------------------------

def export_glb(path):
    props = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
    wanted = dict(
        filepath=path, export_format="GLB", use_selection=False, export_apply=True,
        export_animations=False, export_skins=False, export_morph=False, export_cameras=False,
        export_lights=False, export_yup=True, export_texcoords=True, export_normals=True,
        export_tangents=False, export_materials="EXPORT", export_image_format="AUTO",
        export_draco_mesh_compression_enable=False, export_extras=False,
    )
    kwargs = {k: v for k, v in wanted.items() if k in props}
    skipped = sorted(set(wanted) - set(kwargs))
    if skipped:
        print("  exporter does not know:", skipped)
    bpy.ops.export_scene.gltf(**kwargs)


def convert(folder_name, cfg, downloads, out_dir):
    folder = os.path.join(downloads, folder_name)
    path = common.find_model_file(folder)
    common.reset_scene()
    importer = common.import_model(path)
    common.relink_missing_images(folder)
    fix_materials(cfg, folder)

    apply_rotation(cfg.get("rotate"))
    reset_rigs()
    pose_arms(cfg.get("pose"), common.mesh_objects())
    meshes = bake_to_static_meshes()
    obj = join(meshes)
    obj.name = cfg["id"]
    obj.data.name = cfg["id"]
    triangles = decimate(obj, cfg.get("max_triangles", MAX_TRIANGLES))
    height_cm = normalize(obj, cfg["height"])
    textures = shrink_textures(os.path.join(out_dir, "textures", cfg["id"]))

    glb = os.path.join(out_dir, cfg["id"] + ".glb")
    export_glb(glb)
    common.render_views(os.path.join(out_dir, cfg["id"] + ".png"), [obj])
    return {
        "id": cfg["id"], "alien": cfg["alien"], "height_cm": round(height_cm, 1),
        "glb": glb, "source_folder": folder_name, "source_file": os.path.basename(path), "importer": importer,
        "triangles": triangles, "materials": len(obj.data.materials), "textures": textures,
    }


def main():
    args = common.args_after_double_dash()
    downloads, out_dir = args[0], args[1]
    only = set(args[2:])
    os.makedirs(out_dir, exist_ok=True)
    manifest_path = os.path.join(out_dir, "manifest.json")
    manifest = {}
    if os.path.exists(manifest_path):
        import json
        with open(manifest_path, encoding="utf-8") as handle:
            manifest = {m["id"]: m for m in json.load(handle)}
    for folder_name, cfg in MODELS.items():
        if only and folder_name not in only and cfg["id"] not in only:
            continue
        print("CONVERT", folder_name, "->", cfg["id"])
        try:
            manifest[cfg["id"]] = convert(folder_name, cfg, downloads, out_dir)
            print("CONVERT ok", cfg["id"], manifest[cfg["id"]]["triangles"], "tris", manifest[cfg["id"]]["height_cm"], "cm")
        except Exception:
            print("CONVERT FAILED", cfg["id"], traceback.format_exc())
        common.write_json(manifest_path, sorted(manifest.values(), key=lambda m: m["id"]))


if __name__ == "__main__":
    main()
