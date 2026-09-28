"""
Alien Museum - imports the classic Omnitrix, the watch on the player's left wrist (UE 5.7 Python, editor or
commandlet).

    UnrealEditor-Cmd.exe C:/Games/Ben10/Ben10.uproject -run=pythonscript
        -script="C:/Games/Ben10/Scripts/import_omnitrix.py" -unattended -nosplash -nullrhi

Input: SourceArt/Converted/Omnitrix_Band.glb, Omnitrix_Core.glb and Omnitrix_Face.glb, written by
Scripts/blender_convert_omnitrix.py. The three share one frame: the origin is the middle of the wrist hole
and the face points up; turned -90 yaw on import like the aliens, the forearm runs along Y and the side
button faces +X. Output, each in /Game/AlienMuseum/Models/Omnitrix/<Part>/ (no Nanite, no collision):
  Omnitrix_Band  the cuff and its four tubes, with the model's own textured material,
  Omnitrix_Core  the faceplate ring and its four green lights (pops up, turns as the dial): the band's
                 material,
  Omnitrix_Face  the face disc: its slot "OmnitrixFace" gets the watch's face material in the game.

Personal fan project: the Omnitrix belongs to Cartoon Network / Warner Bros. Discovery and the model to
its Sketchfab author. Keep this build private.
"""
import os
import sys

import unreal

SCRIPTS = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else r"C:/Games/Ben10/Scripts"
sys.path.insert(0, SCRIPTS)
import import_downloaded_models as models  # noqa: E402  (import_model, share_materials)

CONVERTED = r"C:/Games/Ben10/SourceArt/Converted"
PARTS = ["Band", "Core", "Face"]


def main():
    meshes = {}
    for part in PARTS:
        glb = os.path.join(CONVERTED, f"Omnitrix_{part}.glb")
        if not os.path.exists(glb):
            raise RuntimeError(f"{glb} is missing: run Scripts/blender_convert_omnitrix.py first")
        meshes[part] = models.import_model({"id": f"Omnitrix/{part}", "glb": glb})
    models.share_materials(meshes["Core"], meshes["Band"])
    for part, mesh in meshes.items():
        box = mesh.get_bounding_box()
        slots = [s.get_editor_property("material_slot_name") for s in mesh.get_editor_property("static_materials")]
        print(f"OMNITRIX {part}: {mesh.get_path_name()} tris={mesh.get_num_triangles(0)} slots={[str(s) for s in slots]} "
              f"min=({box.min.x:.2f}, {box.min.y:.2f}, {box.min.z:.2f}) max=({box.max.x:.2f}, {box.max.y:.2f}, {box.max.z:.2f})")
    print("OMNITRIX DONE")


if __name__ == "__main__":
    main()
