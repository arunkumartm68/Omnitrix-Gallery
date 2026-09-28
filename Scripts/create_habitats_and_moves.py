"""
Alien Museum - home-world habitats and signature moves for the aliens (UE 5.7 Python, editor or commandlet).

    UnrealEditor-Cmd.exe C:/Games/Ben10/Ben10.uproject -run=pythonscript
        -script="C:/Games/Ben10/Scripts/create_habitats_and_moves.py" -unattended -nosplash -nullrhi

Run it after import_downloaded_models.py (that script resets the model aliens' data). It
  1. creates / updates one ChamberHabitatAsset per home world in /Game/AlienMuseum/Data/Habitats,
  2. imports the re-posed meshes listed in SourceArt/Converted/poses.json (blender_pose_models.py):
     Four Arms' flex pose, and the relaxed arms-down Four Arms and Wildvine that replace their
     T-pose models - plus Cannonbolt's ball form (CannonboltBall.glb, from the Wii download),
  3. gives every model and classic alien its habitat, signature moves, effect colour and extras
     (Heatblast's head flames, XLR8's speed trail and top speed, Cannonbolt's ball),
  4. keeps the classic Heatblast and the classic Stinkfly out of the model collection (the
     downloaded Stinkfly replaces the classic one; Heatblast was taken out of the museum).

Moves follow what the aliens do in Ben 10 (Cartoon Network): Cannonbolt rolls into a ball, XLR8 runs
at super speed, Heatblast is on fire, Four Arms shows off his strength, Diamondhead grows crystals,
Ghostfreak phases, Echo Echo screams and splits, Ditto clones, Upchuck eats and spits, Wildmutt and
Ripjaws pounce, Wildvine lashes vines and seed bombs, Upgrade melts into machines, Grey Matter
scurries, Stinkfly flies, Benwolf howls with his four-way jaw. Private fan project - not for publishing.
"""
import os
import sys

import unreal

SCRIPTS = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else r"C:/Games/Ben10/Scripts"
sys.path.insert(0, SCRIPTS)
import import_downloaded_models as models  # noqa: E402  (only its import_model() is used)

HABITAT_FOLDER = "/Game/AlienMuseum/Data/Habitats"
MODEL_FOLDER = "/Game/AlienMuseum/Data/Models"
CLASSIC_FOLDER = "/Game/AlienMuseum/Data/Classic"
COLLECTION = f"{MODEL_FOLDER}/DA_AlienCollection_Models"
POSES_JSON = r"C:/Games/Ben10/SourceArt/Converted/poses.json"
BALL_GLB = r"C:/Games/Ben10/SourceArt/Converted/CannonboltBall.glb"
LEFT_OUT = {"DA_Classic_Stinkfly", "DA_Classic_Heatblast"}

EAL = unreal.EditorAssetLibrary
S = unreal.AlienPartShape
G = unreal.HabitatGround
A = unreal.HabitatAmbient
P = unreal.HabitatPlacement
L = unreal.HabitatPropLook
M = unreal.AlienAction


def color(rgb):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)


def prop(shape, rgb, count, size, stretch=(1, 1, 1), place=P.SCATTER, look=L.LIT, tilt=0.0,
         physics=False, blocks=False, sway=False):
    p = unreal.HabitatProp()
    p.set_editor_property("shape", shape)
    p.set_editor_property("color", color(rgb))
    p.set_editor_property("look", look)
    p.set_editor_property("count", count)
    p.set_editor_property("size", unreal.Vector2D(size[0], size[1]))
    p.set_editor_property("stretch", unreal.Vector(*stretch))
    p.set_editor_property("placement", place)
    p.set_editor_property("tilt", float(tilt))
    p.set_editor_property("physics", physics)
    p.set_editor_property("blocks_alien", blocks)
    p.set_editor_property("sway", sway)
    return p


# name: (display name, ground, ground colour, glow / water colour, depth cm, ambient, ambient colour, count, props)
HABITATS = {
    "Khoros": ("Khoros desert", G.SAND, (0.62, 0.42, 0.22), (1.0, 0.6, 0.2), 1.5, A.SPORES, (0.9, 0.75, 0.5), 10, [
        prop(S.SPHERE, (0.45, 0.28, 0.15), 3, (8, 14), (1.3, 1, 0.6), P.EDGES, tilt=10, blocks=True),     # sandstone
        prop(S.CYLINDER, (0.55, 0.40, 0.25), 2, (5, 7), (1, 1, 3), P.CORNERS, tilt=6, blocks=True),        # ruined pillars
        prop(S.SPHERE, (0.50, 0.35, 0.20), 3, (5, 8), (1, 1, 0.8), physics=True),                          # boulders
        prop(S.CUBE, (0.40, 0.30, 0.20), 3, (2.5, 4), tilt=30, physics=True),                              # rubble
    ]),
    "Kinet": ("Kinet race track", G.TECH, (0.05, 0.06, 0.08), (0.2, 0.6, 1.0), 1.0, A.PULSES, (0.3, 0.7, 1.0), 12, [
        prop(S.CYLINDER, (0.3, 0.7, 1.0), 4, (1.2, 1.5), (1, 1, 14), P.CORNERS, look=L.GLOW),               # light posts
        prop(S.CUBE, (0.12, 0.13, 0.16), 2, (8, 10), (1.4, 1, 0.35), P.EDGES, blocks=True),                # ramps
        prop(S.CONE, (1.0, 0.35, 0.05), 4, (5, 6), (1, 1, 1.4), physics=True),                             # traffic cones
    ]),
    "Petropia": ("Petropia crystal field", G.CRYSTAL, (0.04, 0.08, 0.07), (0.2, 1.0, 0.7), 1.0, A.SPARKLES, (0.5, 1.0, 0.85), 14, [
        prop(S.CONE, (0.3, 1.0, 0.75), 5, (5, 8), (1, 1, 3.5), P.EDGES, look=L.CRYSTAL, tilt=18, blocks=True),
        prop(S.CONE, (0.45, 0.9, 1.0), 6, (2, 3.5), (1, 1, 3), look=L.CRYSTAL, tilt=25),
        prop(S.CUBE, (0.3, 1.0, 0.8), 4, (2.5, 4), (1, 0.6, 1.8), look=L.CRYSTAL, tilt=40, physics=True),  # loose shards
    ]),
    "GalvanB": ("Galvan B circuitry", G.TECH, (0.02, 0.025, 0.02), (0.2, 1.0, 0.35), 1.0, A.PULSES, (0.2, 1.0, 0.4), 14, [
        prop(S.CUBE, (0.06, 0.07, 0.07), 2, (8, 10), (0.8, 1, 2.8), P.BACK, blocks=True),                  # server towers
        prop(S.CYLINDER, (0.2, 1.0, 0.4), 5, (3, 4), (1, 1, 0.25), look=L.GLOW),                           # circuit nodes
        prop(S.CUBE, (0.08, 0.09, 0.10), 4, (4, 6), physics=True),                                         # tech cubes
    ]),
    "AnurPhaetos": ("Anur Phaetos graveyard", G.SOIL, (0.08, 0.06, 0.08), (0.5, 0.3, 0.9), 1.2, A.MIST, (0.6, 0.55, 0.8), 10, [
        prop(S.CUBE, (0.35, 0.35, 0.38), 3, (7, 9), (1, 0.3, 1.3), P.BACK, tilt=8, blocks=True),            # tombstones
        prop(S.CYLINDER, (0.15, 0.10, 0.08), 2, (2, 3), (1, 1, 9), P.CORNERS, tilt=12),                    # dead trees
        prop(S.SPHERE, (0.6, 0.3, 1.0), 4, (1.5, 2.5), (1, 1, 0.6), P.EDGES, look=L.GLOW),                 # ghost mushrooms
        prop(S.SPHERE, (0.85, 0.83, 0.75), 3, (3.5, 4.5), (1.1, 0.9, 0.9), physics=True),                  # skulls
    ]),
    "Piscciss": ("Piscciss ocean floor", G.WATER, (0.75, 0.68, 0.5), (0.05, 0.35, 0.6), 40.0, A.BUBBLES, (0.8, 0.95, 1.0), 16, [
        prop(S.CYLINDER, (0.10, 0.50, 0.15), 6, (1.5, 2.5), (1, 1, 14), P.EDGES, sway=True),               # seaweed
        prop(S.SPHERE, (1.0, 0.4, 0.45), 3, (6, 9), (1, 1, 0.8), P.CORNERS, blocks=True),                  # coral
        prop(S.SPHERE, (0.55, 0.55, 0.60), 5, (2.5, 4), (1.2, 1, 0.7), physics=True),                      # pebbles
        prop(S.CONE, (1.0, 0.85, 0.75), 2, (3, 4), tilt=60, physics=True),                                 # shells
    ]),
    "Vulpin": ("Vulpin wilds", G.SOIL, (0.25, 0.16, 0.08), (0.8, 0.5, 0.2), 1.2, A.SPORES, (0.8, 0.65, 0.4), 8, [
        prop(S.SPHERE, (0.30, 0.25, 0.20), 3, (7, 12), (1.3, 1, 0.7), P.EDGES, blocks=True),               # rocks
        prop(S.CONE, (0.20, 0.45, 0.10), 4, (4, 6), (1, 1, 2), P.CORNERS, sway=True),                      # ferns
        prop(S.CYLINDER, (0.90, 0.87, 0.78), 3, (1.5, 2), (1, 1, 5), tilt=60, physics=True),               # bones
    ]),
    "GalvanPrime": ("Galvan Prime lab", G.TECH, (0.08, 0.08, 0.10), (0.3, 0.6, 1.0), 1.0, A.SPARKLES, (0.4, 0.7, 1.0), 12, [
        prop(S.CUBE, (0.25, 0.27, 0.32), 2, (7, 9), (1.2, 0.8, 1), P.BACK, blocks=True),                   # lab machines
        prop(S.SPHERE, (0.3, 1.0, 0.5), 3, (2.5, 3.5), place=P.EDGES, look=L.GLOW),                              # glowing flasks
        prop(S.CYLINDER, (0.75, 0.75, 0.80), 4, (2, 2.5), (1, 1, 2), physics=True),                        # batteries
        prop(S.CUBE, (0.60, 0.55, 0.30), 3, (1.5, 2.5), physics=True),                                     # brass parts
    ]),
    "Arburia": ("Arburia rolling grounds", G.ROCK, (0.35, 0.30, 0.28), (0.8, 0.7, 0.5), 1.0, A.SPORES, (0.85, 0.8, 0.7), 8, [
        prop(S.SPHERE, (0.30, 0.28, 0.26), 2, (8, 10), (1.3, 1, 0.6), P.CORNERS, blocks=True),             # rocks
        prop(S.CYLINDER, (0.95, 0.95, 0.95), 6, (2.5, 3), (1, 1, 3.2), P.BACK, physics=True),              # bowling pins
        prop(S.SPHERE, (0.20, 0.30, 0.80), 2, (5, 6), physics=True),                                       # balls
    ]),
    "Flors": ("Flors Verdance", G.GRASS, (0.12, 0.35, 0.08), (0.9, 0.8, 0.3), 1.2, A.SPORES, (0.95, 0.9, 0.4), 14, [
        prop(S.CONE, (0.10, 0.50, 0.10), 6, (3, 5), (1, 1, 4), P.EDGES, sway=True),                        # tall plants
        prop(S.SPHERE, (1.0, 0.4, 0.7), 5, (2, 3), look=L.GLOW),                                           # flowers
        prop(S.SPHERE, (0.45, 0.35, 0.10), 4, (3, 4), (1, 1, 1.3), physics=True),                          # seed pods
    ]),
    "Peptor": ("Peptor XI swamp feast", G.ORGANIC, (0.30, 0.45, 0.12), (0.7, 1.0, 0.3), 1.2, A.SPORES, (0.7, 1.0, 0.4), 10, [
        prop(S.SPHERE, (0.35, 0.30, 0.25), 2, (8, 11), (1.2, 1, 0.7), P.EDGES, blocks=True),               # rocks
        prop(S.CYLINDER, (0.90, 0.85, 0.70), 3, (2, 3), (1, 1, 1.5), P.CORNERS),                           # mushrooms
        prop(S.SPHERE, (0.95, 0.25, 0.10), 3, (3.5, 5), physics=True),                                     # red fruit
        prop(S.SPHERE, (1.0, 0.80, 0.10), 3, (3, 4.5), physics=True),                                      # yellow fruit
    ]),
    "Playground": ("Clone playground", G.GRASS, (0.20, 0.45, 0.12), (0.9, 0.9, 0.9), 1.0, A.SPORES, (0.95, 0.95, 0.9), 6, [
        prop(S.SPHERE, (0.15, 0.40, 0.10), 3, (7, 10), (1, 1, 0.7), P.EDGES, blocks=True),                 # bushes
        prop(S.SPHERE, (0.20, 0.50, 1.0), 2, (4, 5), physics=True),                                        # blue balls
        prop(S.SPHERE, (1.0, 0.30, 0.20), 2, (4, 5), physics=True),                                        # red balls
        prop(S.CUBE, (1.0, 0.85, 0.10), 3, (3, 4), physics=True),                                          # toy blocks
    ]),
    "Sonorosia": ("Sonorosia sound stage", G.TECH, (0.12, 0.12, 0.14), (0.6, 0.8, 1.0), 1.0, A.PULSES, (0.7, 0.85, 1.0), 10, [
        prop(S.CUBE, (0.05, 0.05, 0.06), 2, (8, 10), (0.8, 0.9, 1.4), P.BACK, blocks=True),                # speakers
        prop(S.CONE, (0.7, 0.8, 1.0), 4, (3, 5), (1, 1, 3), P.EDGES, look=L.CRYSTAL),                      # sound crystals
        prop(S.CYLINDER, (0.80, 0.80, 0.85), 4, (4, 5), (1, 1, 0.25), physics=True),                       # discs
    ]),
    "Pyros": ("Pyros lava field", G.LAVA, (0.06, 0.03, 0.02), (1.0, 0.35, 0.05), 2.0, A.EMBERS, (1.0, 0.5, 0.1), 16, [
        prop(S.CYLINDER, (0.08, 0.06, 0.06), 4, (5, 7), (1, 1, 2.5), P.EDGES, blocks=True),                # basalt columns
        prop(S.CONE, (1.0, 0.45, 0.08), 3, (3, 4), (1, 1, 0.8), look=L.GLOW),                              # vents
        prop(S.SPHERE, (0.15, 0.08, 0.05), 4, (3, 5), (1.2, 1, 0.8), physics=True),                        # lava rocks
    ]),
    "Lepidopterra": ("Lepidopterra swamp", G.WATER, (0.25, 0.22, 0.12), (0.2, 0.35, 0.15), 4.0, A.SPORES, (0.6, 0.9, 0.3), 12, [
        prop(S.CYLINDER, (0.35, 0.45, 0.15), 8, (0.8, 1.2), (1, 1, 20), P.EDGES, sway=True),               # reeds
        prop(S.SPHERE, (0.25, 0.25, 0.20), 2, (7, 9), (1.3, 1, 0.6), P.CORNERS, blocks=True),              # rocks
        prop(S.CYLINDER, (0.30, 0.20, 0.10), 2, (2.5, 3), (1, 1, 4), tilt=70, physics=True),               # driftwood
    ]),
    # Benwolf's home: the Loboans' moon, pale blue under the light of Anur Transyl.
    "LunaLobo": ("Luna Lobo moonscape", G.ROCK, (0.16, 0.17, 0.22), (0.55, 0.65, 1.0), 1.0, A.MIST, (0.7, 0.78, 1.0), 10, [
        prop(S.CONE, (0.20, 0.21, 0.27), 4, (6, 9), (1, 1, 1.8), P.EDGES, tilt=14, blocks=True),           # jagged moon rocks
        prop(S.CYLINDER, (0.10, 0.08, 0.08), 2, (2, 3), (1, 1, 10), P.CORNERS, tilt=14),                   # dead trees
        prop(S.SPHERE, (0.75, 0.82, 1.0), 1, (7, 8), place=P.BACK, look=L.GLOW),                           # moon-glow stone
        prop(S.CYLINDER, (0.90, 0.87, 0.78), 3, (1.5, 2), (1, 1, 5), tilt=60, physics=True),               # bones
        prop(S.SPHERE, (0.25, 0.26, 0.32), 3, (3, 5), (1.2, 1, 0.8), physics=True),                        # moon pebbles
    ]),
}

# Alien -> (habitat, moves (first = show-off), effect colour, chance per idle, extras)
MOVES = {
    "FourArms": ("Khoros", [M.FLEX, M.POUNCE], (1.0, 0.25, 0.15), 0.35, {}),
    "XLR8": ("Kinet", [M.DASH], (0.25, 0.6, 1.0), 0.5, {"speed_trail": True, "walk_speed": 70.0, "energy": 0.9}),
    "Diamondhead": ("Petropia", [M.CRYSTAL_BURST], (0.3, 1.0, 0.75), 0.35, {}),
    "Upgrade": ("GalvanB", [M.MELT], (0.2, 1.0, 0.35), 0.4, {}),
    "Ghostfreak": ("AnurPhaetos", [M.PHASE], (0.55, 0.35, 0.85), 0.4, {}),
    "Ripjaws": ("Piscciss", [M.POUNCE], (0.4, 0.8, 1.0), 0.4, {}),
    "Wildmutt": ("Vulpin", [M.POUNCE], (1.0, 0.55, 0.15), 0.45, {}),
    "GreyMatter": ("GalvanPrime", [M.SCURRY], (0.4, 0.8, 1.0), 0.45, {}),
    "Cannonbolt": ("Arburia", [M.ROLL], (0.95, 0.88, 0.55), 0.55, {}),
    "Wildvine": ("Flors", [M.VINES], (0.95, 0.35, 0.45), 0.4, {}),
    "Upchuck": ("Peptor", [M.SPIT], (0.35, 1.0, 0.25), 0.45, {}),
    "Ditto": ("Playground", [M.CLONE], (0.9, 0.95, 1.0), 0.4, {}),
    "EchoEcho": ("Sonorosia", [M.SCREAM, M.CLONE], (0.6, 0.85, 1.0), 0.45, {}),
    "Heatblast": ("Pyros", [M.FLARE], (1.0, 0.45, 0.08), 0.4, {"head_flames": True}),
    "Stinkfly": ("Lepidopterra", [M.FLY], (0.6, 1.0, 0.3), 0.45, {}),
    # The sonic howl is his show-off move; he prowls a little quicker than the static models walk.
    "Benwolf": ("LunaLobo", [M.HOWL, M.POUNCE], (0.5, 1.0, 0.75), 0.4, {"walk_speed": 45.0, "energy": 0.6}),
}


def build_habitat(name, spec):
    title, ground, ground_rgb, glow_rgb, depth, ambient, ambient_rgb, ambient_count, props = spec
    asset = models.load_or_create(HABITAT_FOLDER, f"HAB_{name}", unreal.ChamberHabitatAsset)
    asset.set_editor_property("display_name", unreal.Text(title))
    asset.set_editor_property("ground", ground)
    asset.set_editor_property("ground_color", color(ground_rgb))
    asset.set_editor_property("ground_glow_color", color(glow_rgb))
    asset.set_editor_property("ground_depth", float(depth))
    asset.set_editor_property("ambient", ambient)
    asset.set_editor_property("ambient_color", color(ambient_rgb))
    asset.set_editor_property("ambient_count", ambient_count)
    asset.set_editor_property("props", props)
    EAL.save_loaded_asset(asset)
    return asset


def alien_key(asset_name):
    """DA_Model_FourArms_2 -> FourArms, DA_Classic_Heatblast -> Heatblast."""
    stem = asset_name.replace("DA_Model_", "").replace("DA_Classic_", "")
    parts = stem.split("_")
    return parts[0] if len(parts) > 1 and parts[-1].isdigit() else stem


def apply_moves(da, habitats, poses, ball_mesh):
    key = alien_key(da.get_name())
    if key not in MOVES:
        print("NO MOVES FOR", da.get_name())
        return False
    habitat, moves, rgb, chance, extras = MOVES[key]
    da.set_editor_property("habitat", habitats[habitat])
    da.set_editor_property("signature_actions", moves)
    da.set_editor_property("action_color", color(rgb))
    da.set_editor_property("action_chance", float(chance))
    da.set_editor_property("head_flames", extras.get("head_flames", False))
    da.set_editor_property("speed_trail", extras.get("speed_trail", False))
    for prop_name in ("walk_speed", "energy"):
        if prop_name in extras:
            da.set_editor_property(prop_name, extras[prop_name])
    posed = poses.get(da.get_name().replace("DA_Model_", ""), {})
    if "pose" in posed:
        da.set_editor_property("pose_mesh", posed["pose"][0])
    if "model" in posed:
        mesh, height = posed["model"]
        da.set_editor_property("model_mesh", mesh)  # relaxed arms-down version of the T-pose model
        if height:
            da.set_editor_property("height", float(height))
    if key == "Cannonbolt" and ball_mesh:
        da.set_editor_property("ball_mesh", ball_mesh)  # rolls up into the Wii model's ball
    EAL.save_loaded_asset(da)
    print("MOVES", da.get_name(), key, habitat, [str(m) for m in moves])
    return True


def import_poses():
    """The re-posed meshes (poses.json) -> {model id: {"pose" / "model": (mesh, height cm)}}. They are
    the same models, so they use the converted model's materials."""
    import json
    if not os.path.exists(POSES_JSON):
        print("POSES missing - run blender_pose_models.py first (Four Arms flexes and T-pose models stay as they are)")
        return {}
    with open(POSES_JSON, encoding="utf-8") as handle:
        listing = json.load(handle)
    poses = {}
    for pose in listing:
        if pose["source"] not in models.ORDER:
            continue  # its model was taken out of the museum
        source = models.model_mesh_of(pose["source"])
        if source is None:
            print("POSE", pose["id"], "skipped:", pose["source"], "was not imported")
            continue
        mesh = models.import_model(pose)
        models.share_materials(mesh, source)
        poses.setdefault(pose["source"], {})[pose["use"]] = (mesh, pose.get("height_cm"))
        print("POSE MESH", pose["id"], pose["use"], mesh.get_num_triangles(0), "triangles, height", pose.get("height_cm"))
    return poses


def main():
    habitats = {name: build_habitat(name, spec) for name, spec in HABITATS.items()}
    print(f"HABITATS {len(habitats)}")

    poses = import_poses()

    ball_mesh = None
    if os.path.exists(BALL_GLB):
        ball_mesh = models.import_model({"id": "CannonboltBall", "glb": BALL_GLB})
        print("BALL MESH", ball_mesh.get_path_name(), ball_mesh.get_num_triangles(0), "triangles")
    else:
        print("BALL MESH missing - Cannonbolt rolls up into the built-in ball")

    aliens = []
    for folder in (MODEL_FOLDER, CLASSIC_FOLDER):
        for path in EAL.list_assets(folder, recursive=False, include_folder=False):
            asset = unreal.load_asset(path)
            if isinstance(asset, unreal.AlienDataAsset) and apply_moves(asset, habitats, poses, ball_mesh):
                aliens.append(asset)

    # The museum shows the downloaded models only: the classic Stinkfly is replaced by the downloaded
    # one, and the classic Heatblast (no model was downloaded for him) was taken out.
    collection = unreal.load_asset(COLLECTION)
    members = [a for a in collection.get_editor_property("aliens") if a and a.get_name() not in LEFT_OUT]
    collection.set_editor_property("aliens", members)
    EAL.save_loaded_asset(collection)
    print(f"HABITATS AND MOVES DONE: {len(aliens)} aliens, collection has {len(members)}")


if __name__ == "__main__":
    main()
