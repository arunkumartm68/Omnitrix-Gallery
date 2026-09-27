"""
Alien Museum - the player's input actions and mapping context as assets (UE 5.7 Python, editor or commandlet).

    UnrealEditor-Cmd.exe C:/Games/Ben10/Ben10.uproject -run=pythonscript
        -script="C:/Games/Ben10/Scripts/create_input_assets.py" -unattended -nosplash -nullrhi

On Quest the OpenXR runtime only delivers controller buttons, triggers and thumbsticks to input
actions whose mapping context is listed in Project Settings > Enhanced Input > Default Mapping
Contexts (Config/DefaultInput.ini) before the XR session starts - a mapping context created at run
time never receives them. So the museum's actions live in /Game/AlienMuseum/Input:
IMC_Museum maps them (the same keys AMuseumPawn::CreateDefaultInput used to build at run time) and
AMuseumPawn loads them. Re-running rebuilds the mappings in place.
"""
import unreal

FOLDER = "/Game/AlienMuseum/Input"
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
T = unreal.InputActionValueType

ACTIONS = {
    "IA_Museum_SelectLeft": T.AXIS1D,
    "IA_Museum_SelectRight": T.AXIS1D,
    "IA_Museum_GrabLeft": T.AXIS1D,
    "IA_Museum_GrabRight": T.AXIS1D,
    "IA_Museum_Menu": T.BOOLEAN,
    "IA_Museum_Remove": T.BOOLEAN,
    "IA_Museum_Adjust": T.AXIS2D,
    "IA_Museum_Look": T.AXIS2D,
    "IA_Museum_Move": T.AXIS2D,
}

# (action, key, modifiers): "deadzone", "swizzle" (X -> Y), "negate".
MAPPINGS = [
    # Touch controllers (OpenXR key names).
    ("IA_Museum_SelectLeft", "OculusTouch_Left_Trigger_Axis", []),
    ("IA_Museum_SelectRight", "OculusTouch_Right_Trigger_Axis", []),
    ("IA_Museum_GrabLeft", "OculusTouch_Left_Grip_Axis", []),
    ("IA_Museum_GrabRight", "OculusTouch_Right_Grip_Axis", []),
    ("IA_Museum_Menu", "OculusTouch_Left_X_Click", []),
    ("IA_Museum_Menu", "OculusTouch_Left_Y_Click", []),
    ("IA_Museum_Menu", "OculusTouch_Right_B_Click", []),
    ("IA_Museum_Menu", "OculusTouch_Left_Menu_Click", []),
    ("IA_Museum_Adjust", "OculusTouch_Left_Thumbstick_2D", ["deadzone"]),
    ("IA_Museum_Adjust", "OculusTouch_Right_Thumbstick_2D", ["deadzone"]),
    # Desktop testing.
    ("IA_Museum_SelectRight", "LeftMouseButton", []),
    ("IA_Museum_GrabRight", "RightMouseButton", []),
    ("IA_Museum_Menu", "Tab", []),
    ("IA_Museum_Remove", "Delete", []),
    ("IA_Museum_Adjust", "C", []),                        # rotate +
    ("IA_Museum_Adjust", "Z", ["negate"]),                # rotate -
    ("IA_Museum_Adjust", "E", ["swizzle"]),               # bigger
    ("IA_Museum_Adjust", "Q", ["swizzle", "negate"]),     # smaller
    ("IA_Museum_Look", "Mouse2D", []),
    ("IA_Museum_Move", "W", ["swizzle"]),
    ("IA_Museum_Move", "S", ["swizzle", "negate"]),
    ("IA_Museum_Move", "D", []),
    ("IA_Museum_Move", "A", ["negate"]),
]

MODIFIERS = {
    "deadzone": unreal.InputModifierDeadZone,
    "swizzle": unreal.InputModifierSwizzleAxis,
    "negate": unreal.InputModifierNegate,
}


def load_or_create(name, cls, factory):
    path = f"{FOLDER}/{name}"
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    return AT.create_asset(name, FOLDER, cls, factory)


def main():
    EAL.make_directory(FOLDER)
    actions = {}
    for name, value_type in ACTIONS.items():
        action = load_or_create(name, unreal.InputAction, unreal.InputAction_Factory())
        action.set_editor_property("value_type", value_type)
        EAL.save_loaded_asset(action)
        actions[name] = action

    context = load_or_create("IMC_Museum", unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    context.set_editor_property("context_description", unreal.Text("Alien Museum"))
    mappings = []
    for action_name, key_name, modifier_names in MAPPINGS:
        key = unreal.Key()
        key.import_text(key_name)
        mapping = unreal.EnhancedActionKeyMapping()
        mapping.set_editor_property("action", actions[action_name])
        mapping.set_editor_property("key", key)
        mapping.set_editor_property("modifiers", [unreal.new_object(MODIFIERS[m], outer=context) for m in modifier_names])
        mappings.append(mapping)
    data = unreal.InputMappingContextMappingData()
    data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", data)
    EAL.save_loaded_asset(context)

    saved = context.get_editor_property("default_key_mappings").get_editor_property("mappings")
    for m in saved:
        unreal.log(f"INPUT {m.get_editor_property('action').get_name():24s} {m.get_editor_property('key').export_text():36s} "
                   f"{[type(x).__name__ for x in m.get_editor_property('modifiers')]}")
    unreal.log(f"INPUT DONE: {len(actions)} actions, {len(saved)} mappings in {context.get_path_name()}")


if __name__ == "__main__":
    main()
