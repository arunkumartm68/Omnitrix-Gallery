"""
Alien Museum - Quest fix for the real-world occluder material.

On mobile (Quest) an Alpha Holdout material only compiles when its translucency pass is
"Before DOF"; otherwise the cook falls back to the default grey material, which draws the
scanned room as solid grey surfaces over passthrough.
"""
import unreal

material = unreal.load_asset("/Game/AlienMuseum/Materials/M_MuseumOccluder")
material.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)
print("M_MuseumOccluder translucency pass:", material.get_editor_property("translucency_pass"))
