"""
Alien Museum - imports the converted Ben 10 models and makes them the museum's aliens.

Personal fan project: Ben 10 and its characters belong to Cartoon Network / Warner Bros. Discovery,
and the models belong to their Sketchfab authors. Keep this build private; do not publish it.

Input: SourceArt/Converted/*.glb + manifest.json, written by Scripts/blender_convert_models.py.
For every model this script:
  1. imports the .glb into /Game/AlienMuseum/Models/<Id>/ (one static mesh + its materials/textures;
     no Nanite, no collision - Quest cannot use Nanite and the alien uses its capsule),
  2. creates / updates /Game/AlienMuseum/Data/Models/DA_Model_<Id> (identity, height, behaviour),
  3. puts them all in DA_AlienCollection_Models and makes that the museum's collection.
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
ORDER = ["FourArms_1", "FourArms_2", "XLR8_1", "XLR8_2", "Diamondhead_1", "Diamondhead_2",
         "Upgrade_1", "Upgrade_2", "Ghostfreak", "Ripjaws", "Wildmutt", "GreyMatter",
         "Cannonbolt_1", "Cannonbolt_2", "Cannonbolt_3", "Wildvine", "Upchuck", "Ditto", "EchoEcho",
         "Stinkfly"]

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


def build_alien(entry, mesh, display_name):
    da = load_or_create(DATA_FOLDER, f"DA_Model_{entry['id']}", unreal.AlienDataAsset)
    species, planet, desc, chamber = identity_for(entry["alien"])
    da.set_editor_property("alien_id", unreal.Name(f"Model_{entry['id']}"))
    da.set_editor_property("display_name", unreal.Text(display_name))
    da.set_editor_property("species", species)
    da.set_editor_property("home_planet", planet)
    da.set_editor_property("description", desc)
    da.set_editor_property("model_mesh", mesh)
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
    # Headless runs have no Content Browser to show the new assets in (it would crash).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
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
        mesh = import_model(entry)
        da = build_alien(entry, mesh, display)
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

    # The museum now shows the downloaded models (the classic / original collections stay available).
    director_bp = unreal.load_asset("/Game/AlienMuseum/Blueprints/BP_MuseumDirector")
    unreal.get_default_object(director_bp.generated_class()).set_editor_property("collection", collection)
    unreal.BlueprintEditorLibrary.compile_blueprint(director_bp)
    EAL.save_loaded_asset(director_bp)
    print(f"MODELS DONE: {len(assets)} aliens in {collection.get_path_name()}")


if __name__ == "__main__":  # also imported by create_habitats_and_moves.py for import_model()
    import sys
    main(set(sys.argv[1:]))  # e.g. -script="import_downloaded_models.py Cannonbolt_3 Stinkfly"

