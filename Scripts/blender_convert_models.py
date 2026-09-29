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
Moving parts (Stinkfly's wings) are exported as their own .glb files in the model's frame, one per
side, with the joint they swing around, so the game can flap them.
Rigged models (`rigged`: Wildmutt, Ghostfreak) keep their skeleton: the pose / stance set up here
becomes the rig's rest pose, and besides the static <Id>.glb (same shape and frame) the skinned
<Id>_Rig.glb is written, which the game animates bone by bone (walk cycle, tail...).
Animated models (`animated`: Benwolf) come with hand-made animations: their rest pose stays as it
is, the chosen clips are exported with the skinned <Id>_Rig.glb, and the static <Id>.glb shows the
first clip's first frame (the pose the museum's cards and after-images use).
"""
import math
import os
import sys
import traceback

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import blender_models_common as common  # noqa: E402

MAX_TRIANGLES = 60000   # per model; 8 chambers stay well inside the Quest 3S budget
MAX_BASE = 2048         # base colour textures
MAX_OTHER = 1024        # normal / roughness / specular / emissive textures
MAX_SPAN = 56.0         # cm: widest pose that still fits the default chamber with room to turn

# One entry per downloaded zip. height = display height in cm (at chamber scale 1).
# rotate = degrees (X, Y, Z) so the model stands up and faces Blender's front (-Y).
# pose = bone -> degrees to lower it (baked T-pose -> relaxed pose), or (degrees, "forward") to swing
#        it forward around the vertical axis, or (degrees, "raise") to pitch it up (a tail).
# folder / file = a download holding more than one model (the key is then just a name).
# colors = material -> linear RGB, replacing whatever the material had (texture or colour).
# parts = mesh object -> part name: a mirrored pair (wings) kept out of the model and exported as
#         <Id>_<name>L / <Id>_<name>R (the character's left / right), each swinging at its base.
# ik = limbs placed by two-bone IK, given for the +X (left) side and mirrored: bones (upper, lower,
#      end), target = where the end bone's joint goes, pole = where the middle joint bends towards
#      (both in height units from the model's centre / floor), aim = direction the end bone points,
#      aim_bones = other bones of the limb (a thumb) -> the direction they point.
# drop = objects left out of the model: a name, or a prefix ending in * (blend-shape leftovers, ribbons).
# rigged = also export the skinned model (<Id>_Rig.glb) with the pose above as its rest pose.
# animated = the model's own animations: clips = game clip name -> the file's take; still = (clip,
#            frame) for the static model; drop = objects left out; bind = unskinned object -> bones
#            its vertices follow (each vertex the nearest one); reduce = object -> share of its
#            triangles kept (small parts that are needlessly dense).
MODELS = {
    "ben-10-cannonbolt": dict(id="Cannonbolt_1", alien="Cannonbolt", height=50),
    "cannonbolt": dict(id="Cannonbolt_2", alien="Cannonbolt", height=50,
                       textures={"CANONBOLT.001": "CANNONBOLT_shaddddd.png"}),
    "ben-10-wildwine": dict(id="Wildvine", alien="Wildvine", height=60),
    "ben-10-xlr8-version-2": dict(id="XLR8_1", alien="XLR8", height=55, pose={"upper_arm.L": 20, "upper_arm.R": 20}),
    "xlr8-ben-10": dict(id="XLR8_2", alien="XLR8", height=55, pose={"braço": 60, "braço_2": 60}),
    "classic-diamondhead-thanhtp-edit": dict(id="Diamondhead_1", alien="Diamondhead", height=60,
                                             pose={"LeftUpperArm": 15, "RightUpperArm": 15}),
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
                       pose={"bip_upperArm_L": 60, "bip_upperArm_R": 60}, rigged=True),
    # His tail hung below his shoes (the lowest point), so he floated: raised, it lies on the ground.
    "glutao-upchuck": dict(id="Upchuck", alien="Upchuck", height=38,
                           pose={"Braço": 60, "Braço_2": 60, "Cauda": (22, "raise")}),
    "grey-matter-ben-10-vilgax-attacks-fan-model": dict(id="GreyMatter", alien="Grey Matter", height=20),
    "ripjaws-ben-10": dict(id="Ripjaws", alien="Ripjaws", height=55),
    "upgrade-ben-10-classic": dict(id="Upgrade_1", alien="Upgrade", height=52, rotate=(0, 0, -90),
                                   pose={"bip_upperarm_L": 60, "bip_upperarm_R": 60}),
    "upgrade-ben-10-vilgax-attacks-fan-model": dict(id="Upgrade_2", alien="Upgrade", height=52,
                                                    pose={"LeftUpperArm": 30, "RightUpperArm": 30}),
    # Wildmutt stands like the show's: on all fours like a gorilla, leaning forward onto long arms planted wide, paws
    # flat on the floor - palm down, claws forward - exactly level with his hind feet (the lowest point decides where
    # a model stands: a claw below the feet would float him). The game walks him with IK from this stance.
    # max_span keeps him 40 cm tall with his wide stance (1.6 times his height across).
    "wildmutt": dict(id="Wildmutt", alien="Wildmutt", height=40, max_span=110.0, rotate=(0, 0, -90), rigged=True,
                     colors={"Body": (0.9, 0.28, 0.03), "Body_1": (0.9, 0.28, 0.03)},  # the cartoon's orange, not yellow
                     gorilla=dict(arm=("bip_upperArm_L", "bip_lowerArm_L", "bip_hand_L"),
                                  paw=["bip_hand_L", "bip_thumb_0_L", "bip_thumb_1_L", "bip_thumb_2_L",
                                       "bip_index_0_L", "bip_index_1_L", "bip_index_2_L", "bip_middle_0_L",
                                       "bip_middle_1_L", "bip_middle_2_L", "bip_pinky_0_L", "bip_pinky_1_L", "bip_pinky_2_L"],
                                  claws=["bip_index_2_L", "bip_middle_2_L", "bip_pinky_2_L"],
                                  feet=["bip_foot_L", "bip_toe_index_L", "bip_toe_middle_L", "bip_toe_pinky_L"],
                                  lean_bone="bip_spine_1", lean=8.0, outward=0.32, straight=0.95, paw_yaw=15.0)),
    # Ben 10: Protector of Earth (Wii) Cannonbolt: the standing figure and his rolled-up ball form.
    "cannonbolt-wii": dict(id="Cannonbolt_3", alien="Cannonbolt", height=50,
                           folder="ben-10-cannonbolt-and-ball", file="Model.obj"),
    "cannonbolt-wii-ball": dict(id="CannonboltBall", alien="Cannonbolt ball", height=30,
                                folder="ben-10-cannonbolt-and-ball", file="Model (2).obj"),
    # Source-engine model: tiny single-colour textures behind shader groups, so set the colours directly.
    # Faces +X like the other Source models; arms out in a T; the wings are their own mesh.
    "stinkfly": dict(id="Stinkfly", alien="Stinkfly", height=50, rotate=(0, 0, -90), rebuild_materials=True,
                     max_triangles=50000, textures={"Glass": "Glass.png"},
                     pose={"bip_upperArm_L": 50, "bip_upperArm_R": 50}, parts={"Wings.dmx": "Wing"},
                     colors={"Body_1": (0.122, 0.156, 0.009), "Black_Body": (0.02, 0.02, 0.022),
                             "White_Body": (0.85, 0.85, 0.85), "Glow": (0.991, 0.283, 0.025),
                             "Teeth": (0.776, 0.799, 0.361), "Tongue": (0.089, 0.136, 0.041),
                             "Mouth": (0.15, 0.02, 0.02), "omni black": (0.02, 0.02, 0.02),
                             "omni black.1": (0.02, 0.02, 0.02), "omni black.2": (0.02, 0.02, 0.02),
                             "Mat.2": (0.2, 1.0, 0.2)}),
    # Game model ("Lobisben") with its own skeleton and hand-made animations, kept as they are: the game
    # plays his clips (hunched idle, run on all fours, jump, the sonic howl that opens his four-way jaw).
    # His body is in the file twice (whole, and cut to sit under his clothes): the whole one goes.
    # Eyes and teeth were never skinned: they follow the head and the four jaw quarters.
    # A 7 ft werewolf: bigger than the default case allows for his tail and long arms (max_span), so his
    # case grows to fit him when he moves in.
    "ben10-benwolf": dict(id="Benwolf", alien="Benwolf", height=56, max_span=70.0, animated=True,
                          file="inner/source/model/11_Default_BenWolf.fbx",
                          drop=["Corpo_Inteiro_"],
                          bind={"Olho": ["Cabeça"], "Dentecim": ["FucinhoD", "FucinhoE"],
                                "Dentebaixo": ["BocaD", "BocaE"]},
                          reduce={"Garras_do_pé": 0.3, "Dentecim": 0.5, "Dentebaixo": 0.5},
                          clips={"Idle": "Idle_Lobisben", "Run": "Run_Lobisben", "Jump": "Jump_Lobisben",
                                 "HowlStart": "SpecialStart", "HowlLoop": "SpecialLoop", "Howl": "Special_Lobisben",
                                 "Hit": "TakeDamage_Lobisben", "Attack": "Heavy_01_Lobisben"},
                          still=("Idle", 1)),
    # The classic series' Heatblast: a low-poly Pyronite in a T-pose, arms lowered. His head flames are the
    # game's own effect (create_habitats_and_moves.py), not part of the model.
    "heatblast-ben-10": dict(id="Heatblast", alien="Heatblast", height=55, pose={"Left arm": 70, "Right arm": 70}),
    # Benvicktor, the classic series' Frankenstein monster (later Frankenstrike): a Mixamo rig in a T-pose.
    "ben-viktor-frankenstrike": dict(id="Benvicktor", alien="Benvicktor", height=66,
                                     pose={"mixamorig_LeftArm": 70, "mixamorig_RightArm": 70}),
    # Eye Guy (Mega Olhos): Mixamo rig, arms a little below a T-pose; dense, so it is reduced to the budget.
    "eye-guy-mega-olhos-olhudo-ben10": dict(id="EyeGuy", alien="Eye Guy", height=60,
                                            pose={"mixamorig_LeftArm": 62, "mixamorig_RightArm": 62}),
    # Buzzshock (Chocante): the file also holds its blend shapes as separate, 100x oversized meshes (Pose_*,
    # under Poses__* empties) - they are not part of him.
    "chocante-e-megawatt-buzzshock-and-megawatt": dict(id="Buzzshock", alien="Buzzshock", height=26,
                                                       drop=["Pose_*", "Poses_*"], pose={"Braço": 60, "Braço_2": 60}),
    # Benmummy (Snare-oh): two long bandage ribbons (Faixas) hang from his arms to below his feet, which would
    # lift him off the ground; they are left out - his bandages lash out in his move instead.
    "ben-mumia-benmummy-snare-oh-ben-10": dict(id="Benmummy", alien="Benmummy", height=60, drop=["Faixas"],
                                               pose={"Braco": 70, "Braco_2": 70}),
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
                for link in list(bsdf.inputs["Base Color"].links):
                    mat.node_tree.links.remove(link)
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
        lateral = (to_arm @ Vector((1, 0, 0))).normalized()
        for bone_name, setting in pose.items():
            degrees, mode = setting if isinstance(setting, tuple) else (setting, "lower")
            pb = arm.pose.bones.get(bone_name)
            if pb is None:
                print("  pose: bone not found", bone_name)
                continue
            side = 1.0 if (arm.matrix_world @ pb.head).x > center_x else -1.0
            if mode == "forward":
                rot = Matrix.Rotation(-math.radians(degrees) * side, 4, vertical)
            elif mode == "raise":
                rot = Matrix.Rotation(math.radians(degrees), 4, lateral)  # pitches a backward-pointing bone up
            else:
                rot = Matrix.Rotation(math.radians(degrees) * side, 4, front_back)
            head = pb.head.copy()
            pb.matrix = Matrix.Translation(head) @ rot @ Matrix.Translation(-head) @ pb.matrix
            bpy.context.view_layer.update()
            print("  pose:", bone_name, mode, degrees, "deg")


def armature():
    """The rig the meshes are skinned to (the first armature in the scene)."""
    for obj in common.mesh_objects():
        for mod in obj.modifiers:
            if mod.type == "ARMATURE" and mod.object:
                return mod.object
    return next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)


def mirror_name(name):
    for left, right in (("_L", "_R"), (".L", ".R"), ("Left", "Right")):
        if left in name:
            return name.replace(left, right)
    return name


def turn_bone(arm, pb, pivot, rotation):
    """Turns a pose bone (and with it its children) by a world-space rotation around a world point."""
    world = arm.matrix_world @ pb.matrix
    turned = Matrix.Translation(pivot) @ rotation.to_matrix().to_4x4() @ Matrix.Translation(-pivot) @ world
    pb.matrix = arm.matrix_world.inverted() @ turned
    bpy.context.view_layer.update()


def aim_rotation(current, wanted):
    """Turns `current` to `wanted`: first around the vertical (heading), then up / down (elevation),
    so a paw keeps its palm down instead of rolling over."""
    def heading(v):
        return math.atan2(v.y, v.x)

    def elevation(v):
        return math.atan2(v.z, math.hypot(v.x, v.y))
    yaw = Quaternion((0.0, 0.0, 1.0), heading(wanted) - heading(current))
    turned = yaw @ current
    level = Vector((turned.x, turned.y, 0.0)).normalized()
    pitch = Quaternion(level.cross(Vector((0.0, 0.0, 1.0))), elevation(wanted) - elevation(turned))
    return pitch @ yaw


def two_bone_ik(arm, names, target, pole, aim):
    """Puts the end bone's joint at `target` by turning the upper and lower bones; the middle joint
    bends towards `pole`. Then the end bone points along `aim` (all world space)."""
    upper, lower, end = (arm.pose.bones.get(n) for n in names)
    if not (upper and lower and end):
        print("  ik: bones not found", names)
        return
    mw = arm.matrix_world
    root, joint, tip = mw @ upper.head, mw @ lower.head, mw @ end.head
    a, b = (joint - root).length, (tip - joint).length
    to_target = target - root
    reach = min(to_target.length, (a + b) * 0.999)
    u = to_target.normalized()
    v = pole - root
    v = (v - u * v.dot(u)).normalized()
    cos_a = max(-1.0, min(1.0, (a * a + reach * reach - b * b) / (2.0 * a * reach)))
    new_joint = root + (u * cos_a + v * math.sqrt(1.0 - cos_a * cos_a)) * a
    new_tip = root + u * reach
    turn_bone(arm, upper, root, (joint - root).rotation_difference(new_joint - root))
    joint_now, tip_now = mw @ lower.head, mw @ end.head
    turn_bone(arm, lower, joint_now, (tip_now - joint_now).rotation_difference(new_tip - joint_now))
    if aim is not None:
        tip_now = mw @ end.head
        turn_bone(arm, end, tip_now, aim_rotation((mw @ end.tail) - tip_now, aim))
    print(f"  ik: {names[2]} at {tuple(round(c, 3) for c in (mw @ end.head))} (wanted {tuple(round(c, 3) for c in target)}), "
          f"elbow bent {math.degrees(math.acos(cos_a)):.0f} deg")


def evaluated_points(bone_names, min_weight=0.5):
    """World positions, as the rig deforms them now, of the vertices mostly moved by these bones."""
    dg = bpy.context.evaluated_depsgraph_get()
    points = []
    for obj in common.mesh_objects():
        groups = {g.index: g.name for g in obj.vertex_groups}
        wanted = {i for i, n in groups.items() if n in bone_names}
        if not wanted:
            continue
        evaluated = obj.evaluated_get(dg)
        mesh = evaluated.to_mesh()
        for vertex, moved in zip(obj.data.vertices, mesh.vertices):
            if sum(g.weight for g in vertex.groups if g.group in wanted) >= min_weight:
                points.append(evaluated.matrix_world @ moved.co)
        evaluated.to_mesh_clear()
    return points


def plant_front_paws(spec, lo, hi):
    """Front legs gorilla style (Wildmutt in the show): the chest leans forward `lean` degrees, each arm reaches down
    and `outward` (x height) to the ground, `straight` of its full reach (a slight bend, the elbow bowing out), and its
    paw lies flat - palm down, claws forward and `paw_yaw` degrees out - with its lowest point exactly on the floor
    the hind feet stand on. The left side is given; the right one is mirrored."""
    arm = armature()
    if not spec or arm is None:
        return
    mw = arm.matrix_world
    height = hi.z - lo.z
    side_name = lambda name, side: name if side > 0 else mirror_name(name)
    feet = {side_name(n, side) for n in spec["feet"] for side in (1.0, -1.0)}
    ground = min(p.z for p in evaluated_points(feet, 0.3))

    lean_bone = arm.pose.bones.get(spec.get("lean_bone", ""))
    if lean_bone and spec.get("lean"):
        conv_axis = Vector((1.0, 0.0, 0.0))  # the character faces -Y: turning around +X tips the chest forward and down
        turn_bone(arm, lean_bone, mw @ lean_bone.head, Quaternion(conv_axis, math.radians(spec["lean"])))

    def paw_frame(side):
        """The paw's long axis (wrist -> claw tips) and an axis fixed to it (the hand bone's Z), as it is now."""
        hand = arm.pose.bones[side_name(spec["arm"][2], side)]
        wrist = mw @ hand.head
        tips = [mw @ arm.pose.bones[side_name(n, side)].tail for n in spec["claws"] if arm.pose.bones.get(side_name(n, side))]
        long_axis = (sum(tips, Vector()) / len(tips) - wrist).normalized()
        fixed = (mw.to_3x3() @ hand.matrix.to_3x3() @ Vector((0.0, 0.0, 1.0))).normalized()
        return long_axis, fixed

    def basis(x_axis, up):
        x = x_axis.normalized()
        z = (up - x * up.dot(x)).normalized()
        return Matrix((x, z.cross(x), z)).transposed()

    # One forward distance for both paws (the arms differ by a hair): as far as leaves the shorter arm `straight`.
    forwards = []
    for side in (1.0, -1.0):
        upper, lower, end = (arm.pose.bones[side_name(n, side)] for n in spec["arm"])
        shoulder = mw @ upper.head
        reach = (lower.head - upper.head).length + (end.head - lower.head).length
        drop = shoulder.z - (ground + 0.075 * height)
        level = math.sqrt(max(0.0, (spec["straight"] * reach) ** 2 - drop ** 2))
        forwards.append(math.sqrt(max(0.0, level ** 2 - (spec["outward"] * height) ** 2)))
    forward = min(forwards)

    yaw = math.radians(spec.get("paw_yaw", 15.0))
    paw_bones = spec["paw"]
    for side in (1.0, -1.0):
        names = tuple(side_name(n, side) for n in spec["arm"])
        shoulder = mw @ arm.pose.bones[names[0]].head
        pole = shoulder + Vector((side * 0.6, 0.35, -0.1)) * height  # the elbow bows out and back
        want_long = Vector((side * math.sin(yaw), -math.cos(yaw), -0.06)).normalized()
        want = basis(want_long, Vector((0.0, 0.0, 1.0)))

        def pose(wrist_height):
            target = Vector((shoulder.x + side * spec["outward"] * height, shoulder.y - forward, ground + wrist_height))
            two_bone_ik(arm, names, target, pole, None)
            hand = arm.pose.bones[names[2]]
            long_axis, fixed = paw_frame(side)
            # turn the paw flat: its claws along want_long, the back of the paw (the hand bone's Z) up - palm down
            turn = (want @ basis(long_axis, fixed).inverted()).to_quaternion()
            turn_bone(arm, hand, mw @ hand.head, turn)

        # The wrist so high that the flat paw's lowest point touches the floor (measured on the skinned mesh).
        wrist_height = 0.1 * height
        paw = {side_name(n, side) for n in paw_bones}
        for _ in range(4):
            pose(wrist_height)
            wrist_height -= min(p.z for p in evaluated_points(paw)) - ground
        pose(wrist_height)
        heights = sorted(p.z - ground for p in evaluated_points(paw))
        print(f"  gorilla: {names[2]} lowest {heights[0] * 100:+.2f} cm (model units x100), "
              f"{sum(1 for h in heights if h < 0.02 * height) * 100 // len(heights)}% of the paw within 2% of the floor")


def place_limbs(specs, lo, hi):
    """Two-bone IK for the cfg's `ik` limbs, the +X side as given and the -X side mirrored."""
    arm = armature()
    if not specs or arm is None:
        return
    height = hi.z - lo.z
    cx, cy = (lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5

    def point(v, side):
        return Vector((cx + side * v[0] * height, cy + v[1] * height, lo.z + v[2] * height))
    for spec in specs:
        for side in (1.0, -1.0):
            names = spec["bones"] if side > 0 else tuple(mirror_name(n) for n in spec["bones"])
            aim = Vector((side * spec["aim"][0], spec["aim"][1], spec["aim"][2])) if "aim" in spec else None
            two_bone_ik(arm, names, point(spec["target"], side), point(spec["pole"], side), aim)
            # Other bones of the limb (a thumb) pointed along a direction of their own.
            for bone_name, direction in spec.get("aim_bones", {}).items():
                pb = arm.pose.bones.get(bone_name if side > 0 else mirror_name(bone_name))
                if pb is None:
                    print("  ik: bone not found", bone_name)
                    continue
                head = arm.matrix_world @ pb.head
                wanted = Vector((side * direction[0], direction[1], direction[2]))
                turn_bone(arm, pb, head, aim_rotation((arm.matrix_world @ pb.tail) - head, wanted))


def skin_loose_meshes(arm):
    """Meshes that follow the rig by being parented (to a bone) rather than skinned get a vertex group
    for that bone - or for the bone that moves the skin they sit on (the nearest skinned vertices) -
    so they keep following it once the pose becomes the rest pose."""
    from mathutils.kdtree import KDTree
    skinned = [o for o in common.mesh_objects() if any(m.type == "ARMATURE" for m in o.modifiers)]
    points = []
    for obj in skinned:
        names = {g.index: g.name for g in obj.vertex_groups}
        for vertex in obj.data.vertices:
            if vertex.groups:
                strongest = max(vertex.groups, key=lambda g: g.weight)
                points.append((obj.matrix_world @ vertex.co, names.get(strongest.group)))
    tree = KDTree(len(points))
    for i, (co, _name) in enumerate(points):
        tree.insert(co, i)
    tree.balance()
    for obj in common.mesh_objects():
        if any(m.type == "ARMATURE" for m in obj.modifiers):
            continue
        bone = obj.parent_bone if obj.parent == arm and obj.parent_type == "BONE" else ""
        if not bone and points:
            votes = {}
            for vertex in obj.data.vertices:
                _co, index, _dist = tree.find(obj.matrix_world @ vertex.co)
                name = points[index][1]
                votes[name] = votes.get(name, 0) + 1
            bone = max(votes, key=votes.get)
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = world
        group = obj.vertex_groups.new(name=bone)
        group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
        obj.modifiers.new("Armature", "ARMATURE").object = arm
        print("  skinned loose mesh", obj.name, "to", bone)


def bake_shape_keys(obj):
    """Keeps the current shape-key mix as the plain mesh (modifiers can't be applied under shape keys)."""
    if not obj.data.shape_keys:
        return
    mix = obj.shape_key_add(name="__mix", from_mix=True)
    co = [0.0] * (len(obj.data.vertices) * 3)
    mix.data.foreach_get("co", co)
    obj.shape_key_clear()
    obj.data.vertices.foreach_set("co", co)
    obj.data.update()


def make_pose_rest(arm):
    """The current pose becomes the rig's rest pose: every skinned mesh takes the posed shape and keeps
    its vertex weights, and the armature's rest pose moves to match."""
    for obj in common.mesh_objects():
        bake_shape_keys(obj)
        for mod in list(obj.modifiers):
            with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj], selected_editable_objects=[obj]):
                bpy.ops.object.modifier_apply(modifier=mod.name)
    for obj in bpy.context.scene.objects:
        obj.select_set(obj == arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.armature_apply(selected=False)
    bpy.ops.object.mode_set(mode="OBJECT")


def remove_unused_bones(arm, body):
    """Drops leaf bones that move no vertex (finger and toe tip markers): fewer bones to skin on Quest."""
    groups = {g.index: g.name for g in body.vertex_groups}
    used = set()
    for vertex in body.data.vertices:
        for g in vertex.groups:
            if g.weight > 0.0:
                used.add(groups.get(g.group))
    for obj in bpy.context.scene.objects:
        obj.select_set(obj == arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    removed = 0
    while True:
        leaves = [eb for eb in arm.data.edit_bones if not eb.children and eb.name not in used]
        if not leaves:
            break
        for eb in leaves:
            arm.data.edit_bones.remove(eb)
            removed += 1
    bpy.ops.object.mode_set(mode="OBJECT")
    print("  removed", removed, "unused bones,", len(arm.data.bones), "left")


def normalize_rigged(arm, body, target_height_cm, max_span=MAX_SPAN):
    """normalize() for a skinned mesh and its armature: the same move and scale on both, applied."""
    lo, hi = common.world_bounds([body])
    size = hi - lo
    span = max(size.x, size.y)
    height_cm = min(target_height_cm, max_span * size.z / span) if span > 0 else target_height_cm
    scale = (height_cm / 100.0) / size.z
    move = Matrix.Scale(scale, 4) @ Matrix.Translation(Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z)))
    for obj in (body, arm):
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = move @ world
    for obj in bpy.context.scene.objects:
        obj.select_set(obj in (arm, body))
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return height_cm


def finish_rigged(cfg, out_dir):
    """Rigged path of convert(): the posed rig becomes the rest pose; writes <Id>_Rig.glb (skinned)
    and <Id>.glb (the same shape as a static mesh). Returns (static object, rig glb, triangles, bones)."""
    arm = armature()
    make_pose_rest(arm)
    body = join(common.mesh_objects())
    body.name = body.data.name = cfg["id"] + "_Skin"
    triangles = decimate(body, cfg.get("max_triangles", MAX_TRIANGLES))
    remove_unused_bones(arm, body)
    normalize_rigged(arm, body, cfg["height"], cfg.get("max_span", MAX_SPAN))
    arm.name = cfg["id"] + "_Rig"
    body.parent = arm  # both transforms are identity now; glTF wants the skin under its armature
    body.modifiers.new("Armature", "ARMATURE").object = arm
    shrink_textures(os.path.join(out_dir, "textures", cfg["id"]))
    rig_glb = os.path.join(out_dir, cfg["id"] + "_Rig.glb")
    export_glb(rig_glb, [arm, body], skinned=True)
    # The same shape as a plain mesh: the museum's ModelMesh (bounds, after-images, fallback).
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(body.evaluated_get(depsgraph), preserve_all_data_layers=True, depsgraph=depsgraph)
    mesh.transform(body.matrix_world)
    static = bpy.data.objects.new(cfg["id"], mesh)
    mesh.name = cfg["id"]
    bpy.context.scene.collection.objects.link(static)
    body.hide_render = True
    return static, rig_glb, triangles, len(arm.data.bones)


# ---------------------------------------------------------------------------------------------
# Animated models (their own clips)
# ---------------------------------------------------------------------------------------------

def action_fcurves(action):
    """Every F-curve of an action (Blender 4.4+ keeps them per layer / strip / slot)."""
    if hasattr(action, "layers"):
        for layer in action.layers:
            for strip in layer.strips:
                for slot in action.slots:
                    bag = strip.channelbag(slot)
                    if bag is not None:
                        yield from bag.fcurves
    else:
        yield from action.fcurves


def show_clip(arm, action, frame):
    """Poses the rig as the clip is at that frame."""
    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = action
    if hasattr(arm.animation_data, "action_slot") and arm.animation_data.action_slot is None and len(action.slots):
        arm.animation_data.action_slot = action.slots[0]
    arm.data.pose_position = "POSE"
    bpy.context.scene.frame_set(frame)
    bpy.context.view_layer.update()


def keep_clips(arm, clips):
    """Keeps the rig's takes named in `clips`, renamed to the clip names, and deletes every other action
    (unused takes, the IK helpers' animation). Returns clip name -> action."""
    kept = {}
    for clip, take in clips.items():
        action = next((a for a in bpy.data.actions if a.name.startswith(f"{arm.name}|{take}|")), None)
        if action is None:
            raise RuntimeError(f"take {take} not found in {[a.name for a in bpy.data.actions]}")
        kept[clip] = action
    for action in list(bpy.data.actions):
        if action not in kept.values():
            bpy.data.actions.remove(action)
    for clip, action in kept.items():
        action.name = clip
        action.use_fake_user = True
        start, end = action.frame_range
        print(f"  clip {clip}: frames {start:.0f}-{end:.0f}")
    return kept


def bind_rigid_parts(arm, bind):
    """Unskinned meshes (eyes, teeth) follow the skin around them: each vertex takes the listed bone that
    moves the nearest skinned vertex (that vertex's strongest bone or one of its parents), so teeth open
    with each quarter of a split jaw exactly like the lips around them. (Bone positions are no guide:
    a game rig's bones need not sit inside the mesh.)"""
    from mathutils.kdtree import KDTree
    skinned = [o for o in common.mesh_objects() if o.name not in bind and len(o.vertex_groups) > 0]
    for obj_name, bone_names in bind.items():
        obj = bpy.data.objects.get(obj_name)
        if obj is None or not all(arm.data.bones.get(n) for n in bone_names):
            raise RuntimeError(f"bind: {obj_name} or one of {bone_names} not found")
        owner = {}  # bone -> the listed bone it belongs to (itself or its nearest listed parent)
        for name in bone_names:
            owner[name] = name
            for child in arm.data.bones[name].children_recursive:
                if child.name not in bone_names:
                    owner.setdefault(child.name, name)
        points = []
        for source in skinned:
            groups = {g.index: g.name for g in source.vertex_groups}
            for vertex in source.data.vertices:
                if vertex.groups:
                    strongest = groups.get(max(vertex.groups, key=lambda g: g.weight).group)
                    if strongest in owner:
                        points.append((source.matrix_world @ vertex.co, owner[strongest]))
        if not points:
            raise RuntimeError(f"bind: no skin around {obj_name} moves with {bone_names}")
        tree = KDTree(len(points))
        for i, (co, _bone) in enumerate(points):
            tree.insert(co, i)
        tree.balance()
        members = {name: [] for name in bone_names}
        for vertex in obj.data.vertices:
            _co, index, _dist = tree.find(obj.matrix_world @ vertex.co)
            members[points[index][1]].append(vertex.index)
        obj.vertex_groups.clear()
        for name, indices in members.items():
            if indices:
                obj.vertex_groups.new(name=name).add(indices, 1.0, "REPLACE")
        if not any(m.type == "ARMATURE" for m in obj.modifiers):
            obj.modifiers.new("Armature", "ARMATURE").object = arm
        print("  bound", obj_name, "to", {n: len(m) for n, m in members.items()})


def scale_clip_moves(actions, factor):
    """The rig was scaled (applied): bone moves in the clips (the hips' bob, a jump) scale with it."""
    for action in actions:
        for fcurve in action_fcurves(action):
            if fcurve.data_path.endswith("location"):
                for key in fcurve.keyframe_points:
                    key.co[1] *= factor
                    key.handle_left[1] *= factor
                    key.handle_right[1] *= factor
                fcurve.update()


def match_rig_scale(arm):
    """A game export can leave its meshes with a scale of their own under the rig (Benwolf's: 0.75), which
    games ignore when they skin them. Blender keeps it, so the body is drawn smaller than the skeleton that
    bends it: every joint sits outside the body (the neck above the head) and every pose comes out warped.
    The meshes are scaled back to the rig's size around the rig's origin."""
    origin = arm.matrix_world.translation.copy()
    rig_scale = arm.matrix_world.to_scale().x
    for obj in common.mesh_objects():
        factor = rig_scale / obj.matrix_world.to_scale().x
        if abs(factor - 1.0) > 1e-3:
            obj.matrix_world = Matrix.Translation(origin) @ Matrix.Scale(factor, 4) @ Matrix.Translation(-origin) @ obj.matrix_world
            print(f"  {obj.name}: scaled x{factor:.3f} to match the rig")
    bpy.context.view_layer.update()


def finish_animated(cfg, out_dir):
    """Animated path of convert(): keeps the rest pose and the chosen clips, writes <Id>_Rig.glb (skinned,
    with the clips) and <Id>.glb (the still pose as a plain mesh). Returns (static, rig glb, tris, bones)."""
    arm = armature()
    match_rig_scale(arm)
    for name in cfg.get("drop", []):
        obj = bpy.data.objects.get(name)
        if obj is not None:
            bpy.data.objects.remove(obj, do_unlink=True)
            print("  dropped", name)
    for obj in [o for o in bpy.context.scene.objects if o.type not in ("MESH", "ARMATURE")]:
        bpy.data.objects.remove(obj, do_unlink=True)  # the rig's IK targets and poles
    clips = keep_clips(arm, cfg["clips"])
    arm.data.pose_position = "REST"  # geometry work below happens in the rest (bind) pose
    bind_rigid_parts(arm, cfg.get("bind", {}))
    for name, share in cfg.get("reduce", {}).items():
        obj = bpy.data.objects.get(name)
        if obj is not None:
            decimate(obj, int(common.triangle_count([obj]) * share))
    for obj in common.mesh_objects():
        bake_shape_keys(obj)
    body = join(common.mesh_objects())
    body.name = body.data.name = cfg["id"] + "_Skin"
    triangles = decimate(body, cfg.get("max_triangles", MAX_TRIANGLES))
    remove_unused_bones(arm, body)

    # Size and place it by the still pose (what stands in the case), then apply that to rig and skin.
    still_clip, still_frame = cfg["still"]
    show_clip(arm, clips[still_clip], still_frame)
    lo, hi = common.world_bounds([body])
    size = hi - lo
    span = max(size.x, size.y)
    max_span = cfg.get("max_span", MAX_SPAN)
    height_cm = min(cfg["height"], max_span * size.z / span) if span > 0 else cfg["height"]
    scale = (height_cm / 100.0) / size.z
    rig_scale = arm.matrix_world.to_scale().x * scale  # what the bones grow by once applied
    move = Matrix.Scale(scale, 4) @ Matrix.Translation(Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z)))
    for obj in (body, arm):
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = move @ world
    for obj in bpy.context.scene.objects:
        obj.select_set(obj in (arm, body))
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    scale_clip_moves(clips.values(), rig_scale)
    arm.name = cfg["id"] + "_Rig"
    body.parent = arm  # both transforms are identity now; glTF wants the skin under its armature
    for mod in body.modifiers:
        if mod.type == "ARMATURE":
            mod.object = arm
    print(f"  scaled by {rig_scale:.5f}: {height_cm:.1f} cm in the still pose")

    shrink_textures(os.path.join(out_dir, "textures", cfg["id"]))
    rig_glb = os.path.join(out_dir, cfg["id"] + "_Rig.glb")
    export_glb(rig_glb, [arm, body], skinned=True, animations=True)
    # The still pose as a plain mesh: the museum's ModelMesh (size, after-images, card previews).
    show_clip(arm, clips[still_clip], still_frame)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(body.evaluated_get(depsgraph), preserve_all_data_layers=True, depsgraph=depsgraph)
    mesh.transform(body.matrix_world)
    static = bpy.data.objects.new(cfg["id"], mesh)
    mesh.name = cfg["id"]
    bpy.context.scene.collection.objects.link(static)
    body.hide_render = True
    return static, rig_glb, triangles, len(arm.data.bones), sorted(clips)


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
    for obj in bpy.context.scene.objects:
        obj.select_set(obj in objects)
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


def normalize(objects, target_height_cm):
    """Feet at the origin, centred, target height (smaller if the pose is too wide), measured over
    the whole figure (model and parts get the same transform). Returns the height in cm."""
    lo, hi = common.world_bounds(objects)
    size = hi - lo
    span = max(size.x, size.y)
    height_cm = min(target_height_cm, MAX_SPAN * size.z / span) if span > 0 else target_height_cm
    scale = (height_cm / 100.0) / size.z
    for obj in objects:
        obj.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z))))
        obj.data.transform(Matrix.Scale(scale, 4))
        obj.data.update()
    bpy.context.view_layer.update()
    return height_cm


def split_sides(obj, name):
    """A mirrored pair in one mesh -> {"L": faces on +X (the character's left), "R": faces on -X}."""
    import bmesh
    pieces = {}
    for suffix, keep_left in (("L", True), ("R", False)):
        piece = obj.copy()
        piece.data = obj.data.copy()
        bpy.context.scene.collection.objects.link(piece)
        bm = bmesh.new()
        bm.from_mesh(piece.data)
        other_side = [f for f in bm.faces if (f.calc_center_median().x > 0.0) != keep_left]
        bmesh.ops.delete(bm, geom=other_side, context="FACES")
        bm.to_mesh(piece.data)
        bm.free()
        piece.name = piece.data.name = name + suffix
        pieces[suffix] = piece
    bpy.data.objects.remove(obj, do_unlink=True)
    return pieces


def to_unreal(v, scale=100.0):
    """Blender metres -> the imported Unreal mesh's space (cm): glTF import plus the importer's -90 yaw."""
    return [round(float(-v[1]) * scale, 3), round(float(-v[0]) * scale, 3), round(float(v[2]) * scale, 3)]


def part_joint(obj):
    """Where a wing-like part joins the body and the line it swings around: the middle of the
    vertices closest to the centre line (its base), and the direction that base runs along the
    body, pointing forwards (-Y)."""
    import numpy as np
    count = len(obj.data.vertices)
    co = np.zeros(count * 3)
    obj.data.vertices.foreach_get("co", co)
    co = co.reshape(count, 3)
    base = co[np.argsort(np.abs(co[:, 0]))[:max(8, count // 10)]]
    hinge = base.mean(axis=0)
    spread = base[:, 1:] - base[:, 1:].mean(axis=0)
    _values, vectors = np.linalg.eigh(spread.T @ spread)
    axis = np.array([0.0, vectors[0, -1], vectors[1, -1]])
    if axis[1] > 0.0:
        axis = -axis
    return hinge, axis / max(np.linalg.norm(axis), 1e-6)


# ---------------------------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------------------------

def export_glb(path, objects=None, skinned=False, animations=False):
    """Writes the scene (or only the given objects) as a binary glTF; `skinned` keeps the armature
    and the vertex weights, `animations` also writes every action of the armature as a clip."""
    if objects is not None:
        for obj in bpy.context.scene.objects:
            obj.select_set(obj in objects)
    props = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
    wanted = dict(
        filepath=path, export_format="GLB", use_selection=objects is not None, export_apply=not skinned,
        export_animations=animations, export_skins=skinned, export_morph=False, export_cameras=False,
        export_def_bones=False, export_rest_position_armature=True, export_leaf_bone=False,
        export_lights=False, export_yup=True, export_texcoords=True, export_normals=True,
        export_tangents=False, export_materials="EXPORT", export_image_format="AUTO",
        export_draco_mesh_compression_enable=False, export_extras=False,
    )
    if animations:
        # One glTF animation per action, sampled every frame, each starting at 0.
        wanted.update(export_animation_mode="ACTIONS", export_force_sampling=True, export_frame_step=1,
                      export_anim_slide_to_zero=True, export_reset_pose_bones=True,
                      export_optimize_animation_size=True, export_anim_single_armature=True)
    kwargs = {k: v for k, v in wanted.items() if k in props}
    skipped = sorted(set(wanted) - set(kwargs))
    if skipped:
        print("  exporter does not know:", skipped)
    bpy.ops.export_scene.gltf(**kwargs)


def drop_objects(names):
    """Removes objects that are not part of the character: exact names, or prefixes ending in '*'."""
    for obj in list(bpy.data.objects):
        if obj.name in bpy.data.objects and any(obj.name == n or (n.endswith("*") and obj.name.startswith(n[:-1])) for n in names):
            print("  dropped", obj.name)
            bpy.data.objects.remove(obj, do_unlink=True)


def convert(folder_name, cfg, downloads, out_dir):
    folder_name = cfg.get("folder", folder_name)
    folder = os.path.join(downloads, folder_name)
    path = os.path.join(folder, "source", cfg["file"]) if "file" in cfg else common.find_model_file(folder)
    common.reset_scene()
    importer = common.import_model(path)
    common.relink_missing_images(folder)
    fix_materials(cfg, folder)

    drop_objects(cfg.get("drop", []))
    apply_rotation(cfg.get("rotate"))
    if cfg.get("animated"):
        static, rig_glb, triangles, bones, clips = finish_animated(cfg, out_dir)
        glb = os.path.join(out_dir, cfg["id"] + ".glb")
        export_glb(glb, [static])
        common.render_views(os.path.join(out_dir, cfg["id"] + ".png"), [static])
        lo, hi = common.world_bounds([static])
        return {
            "id": cfg["id"], "alien": cfg["alien"], "height_cm": round((hi.z - lo.z) * 100.0, 1),
            "glb": glb, "rig_glb": rig_glb, "bones": bones, "clips": clips, "source_folder": folder_name,
            "source_file": os.path.basename(path), "importer": importer, "triangles": triangles,
            "materials": len(static.data.materials), "textures": 0,
        }
    reset_rigs()
    rest_lo, rest_hi = common.world_bounds(common.mesh_objects())
    if cfg.get("rigged"):
        skin_loose_meshes(armature())
    pose_arms(cfg.get("pose"), common.mesh_objects())
    place_limbs(cfg.get("ik"), rest_lo, rest_hi)
    plant_front_paws(cfg.get("gorilla"), rest_lo, rest_hi)
    if cfg.get("rigged"):
        static, rig_glb, triangles, bones = finish_rigged(cfg, out_dir)
        glb = os.path.join(out_dir, cfg["id"] + ".glb")
        export_glb(glb, [static])
        common.render_views(os.path.join(out_dir, cfg["id"] + ".png"), [static])
        lo, hi = common.world_bounds([static])
        return {
            "id": cfg["id"], "alien": cfg["alien"], "height_cm": round((hi.z - lo.z) * 100.0, 1),
            "glb": glb, "rig_glb": rig_glb, "bones": bones, "source_folder": folder_name,
            "source_file": os.path.basename(path), "importer": importer, "triangles": triangles,
            "materials": len(static.data.materials), "textures": 0,
        }
    meshes = bake_to_static_meshes()

    # Moving parts stay out of the model: their baked objects are named after the source object.
    part_objects = {}
    for source, name in cfg.get("parts", {}).items():
        found = [o for o in meshes if o.name == source + "_baked"]
        if not found:
            raise RuntimeError(f"part {source} not found in {[o.name for o in meshes]}")
        part_objects[name] = join(found)
    obj = join([o for o in meshes if o not in part_objects.values()])
    obj.name = cfg["id"]
    obj.data.name = cfg["id"]
    part_triangles = common.triangle_count(list(part_objects.values()))
    triangles = decimate(obj, cfg.get("max_triangles", MAX_TRIANGLES) - part_triangles) + part_triangles
    normalize([obj] + list(part_objects.values()), cfg["height"])
    textures = shrink_textures(os.path.join(out_dir, "textures", cfg["id"]))

    glb = os.path.join(out_dir, cfg["id"] + ".glb")
    export_glb(glb, [obj])
    everything = [obj]
    parts = []
    for name, part in part_objects.items():
        sides = split_sides(part, f"{cfg['id']}_{name}")
        _hinge, left_axis = part_joint(sides["L"])
        for suffix, piece in sides.items():
            hinge, _axis = part_joint(piece)
            # The right one swings around the mirrored line the other way round: the pair flaps together.
            axis = left_axis if suffix == "L" else -left_axis
            part_glb = os.path.join(out_dir, piece.name + ".glb")
            export_glb(part_glb, [piece])
            parts.append({"id": piece.name, "glb": part_glb, "hinge": to_unreal(hinge), "axis": to_unreal(axis, 1.0)})
            print("  part", piece.name, "hinge", parts[-1]["hinge"], "axis", parts[-1]["axis"])
            everything.append(piece)
    common.render_views(os.path.join(out_dir, cfg["id"] + ".png"), everything)
    # The model's own height (the game scales ModelMesh to it); parts use the same frame and scale.
    lo, hi = common.world_bounds([obj])
    entry = {
        "id": cfg["id"], "alien": cfg["alien"], "height_cm": round((hi.z - lo.z) * 100.0, 1),
        "glb": glb, "source_folder": folder_name, "source_file": os.path.basename(path), "importer": importer,
        "triangles": triangles, "materials": len(obj.data.materials), "textures": textures,
    }
    if parts:
        entry["parts"] = parts
    return entry


def main():
    args = common.args_after_double_dash()
    # Absolute: Blender saves images (previews, resized textures) relative to the drive root otherwise.
    downloads, out_dir = os.path.abspath(args[0]), os.path.abspath(args[1])
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
