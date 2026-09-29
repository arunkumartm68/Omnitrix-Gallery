"""
Alien Museum - the Omnitrix dial's silhouettes (UE 5.7 Python, editor or commandlet).

  1. dump    UnrealEditor-Cmd.exe C:/Games/Ben10/Ben10.uproject -run=pythonscript
                 -script="C:/Games/Ben10/Scripts/omnitrix_silhouettes.py dump" -unattended -nosplash -nullrhi
     Every alien in the museum's collection: its model's triangles (with its ModelParts - Stinkfly's wings),
     turned the way the dial shows it and flattened onto the view -> SourceArt/Omnitrix/<AlienId>.tri
     (float32: x, y of each triangle's three corners; x to the viewer's right, y up).
  2. draw    python Scripts/make_omnitrix_silhouettes.py      (system Python with numpy and Pillow)
     -> SourceArt/Omnitrix/Silhouettes/<AlienId>.png: the shape in white on black, 256 x 256, fitted to the dial.
  3. import  ... -script="C:/Games/Ben10/Scripts/omnitrix_silhouettes.py import" ...
     -> /Game/AlienMuseum/Omnitrix/Silhouettes/T_Silhouette_<AlienId>, set as each alien's OmnitrixSilhouette.

Like the classic show's dial, each silhouette is the alien standing, seen from the front and turned a little
to one side (VIEW_YAW, degrees): a four-legged alien reads best from the side, Four Arms and Stinkfly straight on.
Personal fan project - the silhouettes come from the downloaded models; keep the build private.
"""
import array
import math
import os
import sys

import unreal

COLLECTION = "/Game/AlienMuseum/Data/Models/DA_AlienCollection_Models"
OUT_DIR = r"C:/Games/Ben10/SourceArt/Omnitrix"
FOLDER = "/Game/AlienMuseum/Omnitrix/Silhouettes"
DEFAULT_YAW = 20.0
VIEW_YAW = {
    "Wildmutt": 70.0,
    "Benwolf": 35.0,
    "Stinkfly": 0.0,
    "FourArms": 0.0,
    "XLR8": 45.0,
    "Ripjaws": 25.0,
    "Upchuck": 25.0,
    "Cannonbolt": 15.0,
}

EAL = unreal.EditorAssetLibrary


def alien_key(alien_id):
    """'Model_FourArms_1' -> 'FourArms'."""
    for prefix in ("Model_", "Classic_"):
        if alien_id.startswith(prefix):
            alien_id = alien_id[len(prefix):]
    head, _, tail = alien_id.rpartition("_")
    return head if head and tail.isdigit() else alien_id


def resolve(value):
    """A soft object property as the loaded asset."""
    if value is None or isinstance(value, unreal.Object):
        return value
    path = value.export_text() if hasattr(value, "export_text") else str(value)
    return unreal.load_asset(path) if path and path != "None" else None


def aliens():
    collection = unreal.load_asset(COLLECTION)
    if not collection:
        raise RuntimeError(f"{COLLECTION} is missing")
    return [a for a in collection.get_editor_property("aliens") if a]


def swing(v, hinge, axis, degrees):
    """v turned about the axis through hinge (a model part's swing: Rodrigues' formula, like FQuat)."""
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    px, py, pz = v[0] - hinge[0], v[1] - hinge[1], v[2] - hinge[2]
    kx, ky, kz = axis
    dot = kx * px + ky * py + kz * pz
    cx, cy, cz = ky * pz - kz * py, kz * px - kx * pz, kx * py - ky * px
    return (hinge[0] + px * c + cx * s + kx * dot * (1.0 - c),
            hinge[1] + py * c + cy * s + ky * dot * (1.0 - c),
            hinge[2] + pz * c + cz * s + kz * dot * (1.0 - c))


def mesh_triangles(mesh, forward, right, up, yaw, part=None):
    """The mesh's triangles, turned by the model's fix-up and the view's yaw, as flat (x, y) corners. A model
    part (a wing) is swung out first, as in flight: the dial shows Stinkfly with his wings spread."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    hinge = axis = None
    spread = 0.0
    if part is not None:
        h, k = part.get_editor_property("hinge"), part.get_editor_property("axis")
        length = math.sqrt(k.x * k.x + k.y * k.y + k.z * k.z) or 1.0
        hinge, axis = (h.x, h.y, h.z), (k.x / length, k.y / length, k.z / length)
        spread = part.get_editor_property("flying_offset")  # raised, as in flight
    out = array.array("f")
    for section in range(mesh.get_num_sections(0)):
        vertices, triangles = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, section)[:2]
        flat = []
        for v in vertices:
            if part is not None:
                v = unreal.Vector(*swing((v.x, v.y, v.z), hinge, axis, spread))
            # The model's fix-up rotation (its columns are the turned axes), then the view's yaw about up.
            x = v.x * forward.x + v.y * right.x + v.z * up.x
            y = v.x * forward.y + v.y * right.y + v.z * up.y
            z = v.x * forward.z + v.y * right.z + v.z * up.z
            y2 = x * s + y * c
            # Seen from its front (+X), looking back at it: its left (-Y) is to the viewer's right.
            flat.append((-y2, z))
        for i in range(0, len(triangles) - 2, 3):
            for k in range(3):
                out.extend(flat[triangles[i + k]])
    return out


def dump():
    os.makedirs(OUT_DIR, exist_ok=True)
    for alien in aliens():
        alien_id = str(alien.get_editor_property("alien_id"))
        mesh = resolve(alien.get_editor_property("model_mesh"))
        if not mesh:
            unreal.log_warning(f"SILHOUETTE {alien_id}: no model mesh, skipped")
            continue
        fix = alien.get_editor_property("model_rotation")
        forward, right, up = fix.get_forward_vector(), fix.get_right_vector(), fix.get_up_vector()
        yaw = VIEW_YAW.get(alien_key(alien_id), DEFAULT_YAW)
        data = mesh_triangles(mesh, forward, right, up, yaw)
        for part in alien.get_editor_property("model_parts"):
            part_mesh = resolve(part.get_editor_property("mesh"))
            if part_mesh:
                data.extend(mesh_triangles(part_mesh, forward, right, up, yaw, part))
            else:
                unreal.log_warning(f"SILHOUETTE {alien_id}: a model part's mesh could not be loaded")
        path = os.path.join(OUT_DIR, f"{alien_id}.tri")
        with open(path, "wb") as f:
            data.tofile(f)
        unreal.log(f"SILHOUETTE {alien_id}: {len(data) // 6} triangles, yaw {yaw:.0f} -> {path}")
    unreal.log("SILHOUETTE DUMP DONE")


def import_silhouettes():
    # Headless, Interchange must not try to show the imported assets in a (missing) content browser.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
    EAL.make_directory(FOLDER)
    png_dir = os.path.join(OUT_DIR, "Silhouettes")
    tasks = []
    wanted = {}
    for alien in aliens():
        alien_id = str(alien.get_editor_property("alien_id"))
        png = os.path.join(png_dir, f"{alien_id}.png")
        if not os.path.exists(png):
            unreal.log_warning(f"SILHOUETTE {alien_id}: {png} missing (run make_omnitrix_silhouettes.py)")
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", png)
        task.set_editor_property("destination_path", FOLDER)
        task.set_editor_property("destination_name", f"T_Silhouette_{alien_id}")
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
        wanted[alien_id] = alien
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for alien_id, alien in wanted.items():
        texture = unreal.load_asset(f"{FOLDER}/T_Silhouette_{alien_id}")
        if not texture:
            unreal.log_warning(f"SILHOUETTE {alien_id}: import failed")
            continue
        # A crisp mask for a small disc: sampled as colour (the face material's default is a black texture).
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        EAL.save_loaded_asset(texture)
        alien.set_editor_property("omnitrix_silhouette", texture)
        EAL.save_loaded_asset(alien)
        unreal.log(f"SILHOUETTE {alien_id}: {texture.get_path_name()}")
    unreal.log(f"SILHOUETTE IMPORT DONE: {len(wanted)} aliens")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "dump"
    if mode == "dump":
        dump()
    elif mode == "import":
        import_silhouettes()
    else:
        raise SystemExit(f"unknown mode {mode!r}: dump or import")
