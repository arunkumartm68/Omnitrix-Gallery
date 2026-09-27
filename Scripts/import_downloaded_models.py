"""
Alien Museum - imports the converted Ben 10 models and makes them the museum's aliens.

Personal fan project: Ben 10 and its characters belong to Cartoon Network / Warner Bros. Discovery,
and the models belong to their Sketchfab authors. Keep this build private; do not publish it.

Input: SourceArt/Converted/*.glb + manifest.json, written by Scripts/blender_convert_models.py.
For every model this script:
  1. imports the .glb into /Game/AlienMuseum/Models/<Id>/ (one static mesh + its materials/textures;
     no Nanite, no collision - Quest cannot use Nanite and the alien uses its capsule), its moving
     parts (Stinkfly's wings) into /Game/AlienMuseum/Models/<Id>_<Part><Side>/ and a rigged model's
     skinned copy (<Id>_Rig.glb: Wildmutt, Ghostfreak) into /Game/AlienMuseum/Models/<Id>_Rig/,
  2. creates / updates /Game/AlienMuseum/Data/Models/DA_Model_<Id> (identity, height, behaviour,
     moving parts with their joints and how they swing),
  3. puts them all in DA_AlienCollection_Models and makes that the museum's collection, and deletes
     the assets of models taken out of the museum (RETIRED).
Re-running updates everything in place. Afterwards run create_habitats_and_moves.py (habitats,
signature moves and speeds live there and are reset here).

Run headless (editor closed); list model ids after the script to import only those:
    UnrealEditor-Cmd.exe C:/Games/Ben10/Ben10.uproject -run=pythonscript
        -script="C:/Games/Ben10/Scripts/import_downloaded_models.py [Cannonbolt_3 Stinkfly]" -unattended -nosplash -nullrhi
or in the editor's Python console:
    exec(open(r"C:/Games/Ben10/Scripts/import_downloaded_models.py").read())
"""
import json
import os

import unreal

CONVERTED = r"C:/Games/Ben10/SourceArt/Converted"
MODEL_ROOT = "/Game/AlienMuseum/Models"
DATA_FOLDER = "/Game/AlienMuseum/Data/Models"
CLASSIC_FOLDER = "/Game/AlienMuseum/Data/Classic"
COLLECTION_NAME = "DA_AlienCollection_Models"

# Collection order (the first one fills the starter chamber on a fresh install).
ORDER = ["FourArms_1", "XLR8_1", "XLR8_2", "Diamondhead_1", "Diamondhead_2",
         "Upgrade_1", "Ghostfreak", "Ripjaws", "Wildmutt", "GreyMatter",
         "Cannonbolt_1", "Wildvine", "Upchuck", "Ditto", "EchoEcho", "Stinkfly"]

# Taken out of the museum by the owner (the downloads stay in SourceArt; add an id back to ORDER to
# bring it back). Their imported assets are deleted, so they are not cooked into the app.
RETIRED = ["FourArms_2", "Cannonbolt_2", "Cannonbolt_3", "Upgrade_2"]

# The classic ten already have identity text and a chamber colour.
CLASSIC_ASSETS = {
    "Four Arms": "DA_Classic_FourArms", "XLR8": "DA_Classic_XLR8", "Diamondhead": "DA_Classic_Diamondhead",
    "Upgrade": "DA_Classic_Upgrade", "Ghostfreak": "DA_Classic_Ghostfreak", "Ripjaws": "DA_Classic_Ripjaws",
    "Wildmutt": "DA_Classic_Wildmutt", "Grey Matter": "DA_Classic_GreyMatter", "Stinkfly": "DA_Classic_Stinkfly",
}

# Aliens without a classic data asset.
EXTRA = {
    "Cannonbolt": dict(species="Arburian Pelarota", planet="Arburia", chamber=(1.0, 0.8, 0.2),
                       desc="Rolls up into an armoured ball and smashes through anything in its way."),
    "Wildvine": dict(species="Florauna", planet="Flors Verdance", chamber=(0.3, 1.0, 0.35),
                     desc="A walking plant with stretchy vine limbs that throws exploding seed pods."),
    "Upchuck": dict(species="Gourmand", planet="Peptos XI", chamber=(0.5, 1.0, 0.3),
                    desc="Swallows almost anything with its four tongues and spits it back out as explosive energy."),
    "Ditto": dict(species="Splixson", planet="Hathor", chamber=(0.85, 0.95, 1.0),
                  desc="Splits into identical copies of itself - whatever one copy feels, they all feel."),
    "Echo Echo": dict(species="Sonorosian", planet="Sonorosia", chamber=(0.4, 1.0, 0.5),
                      desc="A living amplifier that clones itself and blasts ear-splitting sonic screams."),
}

HOVERS = {"Ghostfreak", "Stinkfly"}

# How a model's moving parts swing (AlienModelPart), by part name (blender_convert_models.py parts).
# Stinkfly's wings beat outwards from where they meet over his back (never through each other): a
# buzz while he hovers, big fast strokes when he flies, moves or is held.
PART_MOTION = {
    "Wing": dict(amount=15.0, offset=17.0, speed=6.0, flying_amount=32.0, flying_offset=34.0, flying_speed=12.0),
}

# How a rigged model's bones move (AlienRig; bone names from the model's skeleton).
# legs: (upper, lower, end, phase in the step cycle, front leg). A four-legged walk steps left hind,
# left fore, right hind, right fore - a quarter of the cycle apart.
RIGS = {
    # Wildmutt walks on all fours like the classic cartoon: elbows bending, paws planted, the head low,
    # sniffing the air (he has no eyes) and panting / snarling when excited.
    "Wildmutt": dict(
        legs=[("bip_hip_L", "bip_knee_L", "bip_foot_L", 0.0, False),
              ("bip_upperArm_L", "bip_lowerArm_L", "bip_hand_L", 0.25, True),
              ("bip_hip_R", "bip_knee_R", "bip_foot_R", 0.5, False),
              ("bip_upperArm_R", "bip_lowerArm_R", "bip_hand_R", 0.75, True)],
        spine=["bip_pelvis", "bip_spine_0", "bip_spine_1", "bip_spine_2"],
        neck="bip_neck", head="bip_head", jaw="bip_Jaw",
        stride_length=0.3, step_height=0.1, sniffs=True),
    # Ghostfreak's ghostly tail waves all the time; his long arms drift.
    "Ghostfreak": dict(
        spine=["bip_pelvis", "bip_spine_0", "bip_spine_1"], neck="bip_neck", head="bip_head",
        tail=[f"bip_tail_{i}" for i in range(8)],
        floating=["bip_upperArm_L", "bip_lowerArm_L", "bip_upperArm_R", "bip_lowerArm_R"],
        tail_amount=16.0, tail_speed=0.7),
}

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def load_or_create(folder, asset_name, cls):
    path = f"{folder}/{asset_name}"
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    return AT.create_asset(asset_name, folder, cls, factory)


def import_model(entry):
    """Imports one .glb as a single static mesh. Returns the StaticMesh."""
    dest = f"{MODEL_ROOT}/{entry['id']}"
    # Headless runs have no Content Browser to show the new assets in (it would crash).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
    # Start clean: a re-import over existing assets keeps the old materials, so textures added to
    # the model since the last import would never show up.
    if EAL.does_directory_exist(dest):
        EAL.delete_directory(dest)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    # Blender's front (-Y) arrives as Unreal +Y through glTF; turn it so the model faces +X
    # (the alien's forward), which is what AlienDataAsset::ModelMesh expects.
    pipeline.set_editor_property("import_offset_rotation", unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_static_meshes", True)
    mesh.set_editor_property("combine_static_meshes", True)
    mesh.set_editor_property("import_skeletal_meshes", False)
    mesh.set_editor_property("build_nanite", False)          # Quest has no Nanite
    mesh.set_editor_property("collision", False)             # the alien's capsule does collision
    # Always make this model's own materials: several models use the same material names
    # ("Omnitrix", "Eye", ...) and must not pick up each other's.
    materials = pipeline.get_editor_property("material_pipeline")
    materials.set_editor_property("search_location", unreal.InterchangeMaterialSearchLocation.DO_NOT_SEARCH)
    common = pipeline.get_editor_property("common_meshes_properties")
    static_type = getattr(unreal.InterchangeForceMeshType, "IFMT_STATIC_MESH", None)
    if static_type is not None:
        common.set_editor_property("force_all_mesh_as_type", static_type)
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(pipeline)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", entry["glb"])
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", stack)
    AT.import_asset_tasks([task])

    meshes = []
    for path in EAL.list_assets(dest, recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            meshes.append(asset)
    if not meshes:
        raise RuntimeError(f"no static mesh imported for {entry['id']}")
    return max(meshes, key=lambda m: m.get_num_triangles(0))


def model_mesh_of(model_id):
    """The static mesh imported for a converted model (None if it was never imported)."""
    folder = f"{MODEL_ROOT}/{model_id}"
    if not EAL.does_directory_exist(folder):
        return None
    meshes = [a for a in (unreal.load_asset(p) for p in EAL.list_assets(folder, recursive=True, include_folder=False))
              if isinstance(a, unreal.StaticMesh)]
    return max(meshes, key=lambda m: m.get_num_triangles(0)) if meshes else None


def share_materials(mesh, source_mesh):
    """A mesh made from the same model (a pose, the other wing) uses the source mesh's materials;
    the duplicates imported with it are deleted."""
    source_slots = source_mesh.get_editor_property("static_materials")
    slot_count = len(mesh.get_editor_property("static_materials"))
    for index in range(slot_count):
        material = source_slots[min(index, len(source_slots) - 1)].get_editor_property("material_interface")
        mesh.set_material(index, material)  # edits the slot in place (the struct list is a copy)
    EAL.save_loaded_asset(mesh)
    folder = mesh.get_path_name().rsplit("/", 1)[0]
    for path in EAL.list_assets(folder, recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        if isinstance(asset, (unreal.MaterialInterface, unreal.Texture)):
            EAL.delete_asset(path)
    used = [mesh.get_material(i) for i in range(slot_count)]
    print(f"{mesh.get_name()} uses", [m.get_path_name() if m else None for m in used])


def import_rigged(entry, static_mesh):
    """Imports a rigged model's skinned copy (<Id>_Rig.glb) as a skeletal mesh with its skeleton into
    Models/<Id>_Rig, using the static model's materials. Returns the SkeletalMesh."""
    dest = f"{MODEL_ROOT}/{entry['id']}_Rig"
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
    if EAL.does_directory_exist(dest):
        EAL.delete_directory(dest)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("import_offset_rotation", unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))  # faces +X, like the static one
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_static_meshes", False)
    mesh.set_editor_property("import_skeletal_meshes", True)
    mesh.set_editor_property("import_morph_targets", False)
    mesh.set_editor_property("create_physics_asset", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    pipeline.get_editor_property("material_pipeline").set_editor_property(
        "search_location", unreal.InterchangeMaterialSearchLocation.DO_NOT_SEARCH)
    skeletal_type = getattr(unreal.InterchangeForceMeshType, "IFMT_SKELETAL_MESH", None)
    if skeletal_type is not None:
        pipeline.get_editor_property("common_meshes_properties").set_editor_property("force_all_mesh_as_type", skeletal_type)
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(pipeline)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", entry["rig_glb"])
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", stack)
    AT.import_asset_tasks([task])

    skeletal = None
    for path in EAL.list_assets(dest, recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.SkeletalMesh):
            skeletal = asset
    if skeletal is None:
        raise RuntimeError(f"no skeletal mesh imported for {entry['id']}")
    # Same model: the static mesh's materials (matched by slot name), the imported copies deleted.
    by_name = {}
    for slot in static_mesh.get_editor_property("static_materials"):
        by_name[str(slot.get_editor_property("material_slot_name"))] = slot.get_editor_property("material_interface")
    fallback = static_mesh.get_material(0)
    materials = []  # iterating the property's array hands out copies: collect the edited ones
    for slot in skeletal.get_editor_property("materials"):
        slot.set_editor_property("material_interface", by_name.get(str(slot.get_editor_property("material_slot_name")), fallback))
        materials.append(slot)
    skeletal.set_editor_property("materials", materials)
    EAL.save_loaded_asset(skeletal)
    # A material drawn on a skinned mesh must say so, or it renders as the default grey material
    # (and a cooked build never compiles the skinned version).
    for slot in materials:
        material = slot.get_editor_property("material_interface")
        base = material.get_base_material() if material else None
        if base and not base.get_editor_property("used_with_skeletal_mesh"):
            base.set_editor_property("used_with_skeletal_mesh", True)
            unreal.MaterialEditingLibrary.recompile_material(base)
            EAL.save_loaded_asset(base)
    for path in EAL.list_assets(dest, recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        if isinstance(asset, (unreal.MaterialInterface, unreal.Texture, unreal.PhysicsAsset)):
            EAL.delete_asset(path)
    skeleton = skeletal.get_editor_property("skeleton")
    bounds = skeletal.get_bounds()
    print(f"RIG {entry['id']}: {skeletal.get_path_name()} bones={len(skeleton.get_editor_property('bone_tree')) if skeleton else '?'} "
          f"bounds origin={bounds.origin} extent={bounds.box_extent} (static {static_mesh.get_bounding_box()})")
    return skeletal


def make_rig(model_id):
    """AlienRig for a rigged model from RIGS (an empty rig otherwise)."""
    spec = RIGS.get(model_id, {})
    rig = unreal.AlienRig()
    legs = []
    for upper, lower, end, phase, front in spec.get("legs", []):
        leg = unreal.AlienRigLeg()
        leg.set_editor_property("upper", upper)
        leg.set_editor_property("lower", lower)
        leg.set_editor_property("end", end)
        leg.set_editor_property("phase", phase)
        leg.set_editor_property("front", front)
        legs.append(leg)
    rig.set_editor_property("legs", legs)
    for key in ("spine", "tail", "floating"):
        rig.set_editor_property(key, [unreal.Name(n) for n in spec.get(key, [])])
    for key in ("neck", "head", "jaw"):
        rig.set_editor_property(key, unreal.Name(spec.get(key, "None")))
    for key in ("stride_length", "step_height", "tail_amount", "tail_speed", "sniffs"):
        if key in spec:
            rig.set_editor_property(key, spec[key])
    return rig


def delete_retired():
    """Deletes the assets of models taken out of the museum (their data asset, meshes and poses)."""
    for model_id in RETIRED:
        folders = [f.rstrip("/") for f in EAL.list_assets(MODEL_ROOT, recursive=False, include_folder=True)
                   if f.rstrip("/").split("/")[-1] == model_id or f.rstrip("/").split("/")[-1].startswith(model_id + "_")]
        data = f"{DATA_FOLDER}/DA_Model_{model_id}"
        if EAL.does_asset_exist(data):
            EAL.delete_asset(data)
            print("RETIRED", data)
        for folder in folders:
            if EAL.does_directory_exist(folder):
                EAL.delete_directory(folder)
                print("RETIRED", folder)


def import_parts(entry):
    """Imports a model's moving parts (one mesh per side, in the model's frame). Returns AlienModelParts."""
    parts = []
    first = None
    for part in entry.get("parts", []):
        mesh = import_model(part)
        if first is None:
            first = mesh
        else:
            share_materials(mesh, first)  # a mirrored pair: one set of materials
        name = part["id"][len(entry["id"]) + 1:-1]  # Stinkfly_WingL -> Wing
        model_part = unreal.AlienModelPart()
        model_part.set_editor_property("mesh", mesh)
        model_part.set_editor_property("hinge", unreal.Vector(*part["hinge"]))
        model_part.set_editor_property("axis", unreal.Vector(*part["axis"]))
        for key, value in PART_MOTION.get(name, {}).items():
            model_part.set_editor_property(key, value)
        parts.append(model_part)
        print(f"PART {part['id']}: tris={mesh.get_num_triangles(0)} hinge={part['hinge']} axis={part['axis']}")
    return parts


def identity_for(alien):
    """(species, planet, description, chamber colour) from the classic asset or EXTRA."""
    if alien in CLASSIC_ASSETS:
        classic = unreal.load_asset(f"{CLASSIC_FOLDER}/{CLASSIC_ASSETS[alien]}")
        if classic:
            return (classic.get_editor_property("species"), classic.get_editor_property("home_planet"),
                    classic.get_editor_property("description"), classic.get_editor_property("chamber_light_color"))
    extra = EXTRA.get(alien, dict(species="Unknown", planet="Unknown", desc="", chamber=(0.3, 0.9, 1.0)))
    return (unreal.Text(extra["species"]), unreal.Text(extra["planet"]), unreal.Text(extra["desc"]),
            unreal.LinearColor(*extra["chamber"], 1.0))


def build_alien(entry, mesh, display_name, parts=(), rigged=None):
    da = load_or_create(DATA_FOLDER, f"DA_Model_{entry['id']}", unreal.AlienDataAsset)
    species, planet, desc, chamber = identity_for(entry["alien"])
    da.set_editor_property("alien_id", unreal.Name(f"Model_{entry['id']}"))
    da.set_editor_property("display_name", unreal.Text(display_name))
    da.set_editor_property("species", species)
    da.set_editor_property("home_planet", planet)
    da.set_editor_property("description", desc)
    da.set_editor_property("model_mesh", mesh)
    da.set_editor_property("model_parts", list(parts))
    da.set_editor_property("rigged_mesh", rigged)
    da.set_editor_property("rig", make_rig(entry["id"]) if rigged else unreal.AlienRig())
    da.set_editor_property("model_rotation", unreal.Rotator(0.0, 0.0, 0.0))
    da.set_editor_property("model_credit", unreal.Text(f"Model: {entry['source_folder']} (Sketchfab download)"))
    da.set_editor_property("height", float(entry["height_cm"]))
    da.set_editor_property("hovers", entry["alien"] in HOVERS)
    da.set_editor_property("chamber_light_color", chamber)
    # Rigid figures read best moving calmly: slower walks, longer idles, more looking around.
    da.set_editor_property("walk_speed", 14.0)
    da.set_editor_property("idle_duration", unreal.Vector2D(2.5, 6.0))
    da.set_editor_property("look_around_chance", 0.45)
    da.set_editor_property("curiosity", 0.6)
    da.set_editor_property("notice_player_distance", 170.0)
    da.set_editor_property("energy", 0.35)
    EAL.save_loaded_asset(da)
    return da


def main(only=()):
    """Imports every model in ORDER, or only the named ids (the others keep their assets as they are)."""
    with open(os.path.join(CONVERTED, "manifest.json"), encoding="utf-8") as handle:
        manifest = {m["id"]: m for m in json.load(handle)}
    missing = [i for i in ORDER if i not in manifest]
    if missing:
        unreal.log_warning(f"Not converted yet: {missing}")

    EAL.make_directory(MODEL_ROOT)
    EAL.make_directory(DATA_FOLDER)
    counts = {}
    for model_id in ORDER:
        if model_id in manifest:
            counts[manifest[model_id]["alien"]] = counts.get(manifest[model_id]["alien"], 0) + 1

    assets = []
    numbers = {}
    for model_id in ORDER:
        entry = manifest.get(model_id)
        if not entry:
            continue
        alien = entry["alien"]
        numbers[alien] = numbers.get(alien, 0) + 1
        display = f"{alien} ({numbers[alien]})" if counts[alien] > 1 else alien
        existing = f"{DATA_FOLDER}/DA_Model_{model_id}"
        if only and model_id not in only and EAL.does_asset_exist(existing):
            da = unreal.load_asset(existing)
            da.set_editor_property("display_name", unreal.Text(display))  # numbering may have shifted
            EAL.save_loaded_asset(da)
            assets.append(da)
            continue
        # The old skinned copy goes first: it uses the static model's materials, and deleting it after
        # they were replaced makes the engine load a mesh whose materials are gone (it asserts).
        rig_folder = f"{MODEL_ROOT}/{model_id}_Rig"
        if EAL.does_directory_exist(rig_folder):
            EAL.delete_directory(rig_folder)
        mesh = import_model(entry)
        rigged = import_rigged(entry, mesh) if entry.get("rig_glb") else None
        da = build_alien(entry, mesh, display, import_parts(entry), rigged)
        assets.append(da)
        folder = f"{MODEL_ROOT}/{model_id}"
        textures = [p for p in EAL.list_assets(folder, recursive=True, include_folder=False)
                    if isinstance(unreal.load_asset(p), unreal.Texture2D)]
        # A texture that no material uses was imported but not wired up.
        unused = [p for p in textures if not EAL.find_package_referencers_for_asset(p.split(".")[0], False)]
        print(f"MODEL {model_id}: tris={mesh.get_num_triangles(0)} materials={len(mesh.get_editor_property('static_materials'))} "
              f"textures={len(textures)} unused={len(unused)} height={entry['height_cm']} -> {da.get_name()}")

    collection = load_or_create(DATA_FOLDER, COLLECTION_NAME, unreal.AlienCollectionAsset)
    collection.set_editor_property("aliens", assets)
    EAL.save_loaded_asset(collection)
    delete_retired()  # now that the collection no longer holds them

    # The museum now shows the downloaded models (the classic / original collections stay available).
    director_bp = unreal.load_asset("/Game/AlienMuseum/Blueprints/BP_MuseumDirector")
    unreal.get_default_object(director_bp.generated_class()).set_editor_property("collection", collection)
    unreal.BlueprintEditorLibrary.compile_blueprint(director_bp)
    EAL.save_loaded_asset(director_bp)
    print(f"MODELS DONE: {len(assets)} aliens in {collection.get_path_name()}")


if __name__ == "__main__":  # also imported by create_habitats_and_moves.py for import_model()
    import sys
    main(set(sys.argv[1:]))  # e.g. -script="import_downloaded_models.py Cannonbolt_3 Stinkfly"

