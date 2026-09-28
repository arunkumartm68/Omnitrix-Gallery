"""
Alien Museum - imports the museum's sounds and wires them up (UE 5.7 Python, editor or commandlet).

    UnrealEditor-Cmd.exe C:/Games/Ben10/Ben10.uproject -run=pythonscript
        -script="C:/Games/Ben10/Scripts/import_museum_sounds.py" -unattended -nosplash -nullrhi

Input: SourceArt/Sounds/*.wav + sounds.json, made by Scripts/make_museum_sounds.py (original sounds,
synthesised - nothing recorded or copied). It
  1. imports every sound into /Game/AlienMuseum/Audio/Voices/<Alien>/ or /Game/AlienMuseum/Audio/SFX/<Group>/
     (loops loop; short sounds stay in memory so they start instantly on the Quest),
  2. makes the three ranges' attenuation (ATT_MuseumRoom / Near / Interface): 3D, spatialised by the
     project's plugin (Resonance Audio's HRTF on the Quest), fading with distance and duller far away,
  3. fills DA_MuseumSounds (UMuseumSoundLibrary): every shared sound by name with its volume, pitch
     range, range and limit - UMuseumAudio loads it by path,
  4. gives every Ben 10 alien (DA_Model_*, DA_Classic_*) its voice from VOICES.
Run it again after changing the sounds; it replaces what it made before.
"""
import json
import os
import re

import unreal

SOUNDS_DIR = r"C:/Games/Ben10/SourceArt/Sounds"
AUDIO_ROOT = "/Game/AlienMuseum/Audio"
DATA_FOLDERS = ["/Game/AlienMuseum/Data/Models", "/Game/AlienMuseum/Data/Classic"]

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
R = unreal.MuseumSoundRange

# Library sounds: name prefix -> (volume, range, pitch range, max playing). The first matching prefix wins.
MIX = [
    ("Alien.Materialize", 0.8, R.ROOM, (0.98, 1.02), 2),
    ("Alien.", 0.6, R.ROOM, (0.95, 1.05), 0),
    ("Step.Water", 1.0, R.NEAR, (0.9, 1.1), 3),
    ("Step.", 1.0, R.NEAR, (0.92, 1.08), 0),
    ("Move.Howl", 1.4, R.ROOM, (0.98, 1.02), 1),
    ("Move.HowlStart", 1.2, R.ROOM, (0.97, 1.03), 1),
    ("Move.Stomp", 1.3, R.ROOM, (0.95, 1.05), 1),
    ("Move.Scream", 1.2, R.ROOM, (0.97, 1.03), 1),
    ("Move.Roll.Loop", 0.7, R.ROOM, (0.95, 1.05), 1),
    ("Move.Roll.Bounce", 1.1, R.ROOM, (0.9, 1.1), 2),
    ("Move.Pounce.Sniff", 0.9, R.ROOM, (0.95, 1.05), 1),
    ("Move.Dash.Zip", 0.9, R.ROOM, (0.9, 1.15), 2),
    ("Move.Scurry.Hop", 0.8, R.ROOM, (0.9, 1.15), 2),
    ("Move.", 1.0, R.ROOM, (0.95, 1.05), 0),
    ("Case.Hum", 0.45, R.NEAR, (1.0, 1.0), 0),
    ("Case.Place", 0.9, R.ROOM, (0.97, 1.03), 2),
    ("Case.Remove", 0.9, R.ROOM, (0.97, 1.03), 2),
    ("Case.Arm", 0.6, R.ROOM, (1.0, 1.0), 1),
    ("Case.", 0.55, R.ROOM, (0.95, 1.05), 0),
    ("Amb.", 0.5, R.NEAR, (1.0, 1.0), 0),
    ("Loop.", 0.5, R.NEAR, (1.0, 1.0), 0),
    ("UI.Hover", 0.2, R.INTERFACE, (0.98, 1.02), 1),
    ("UI.", 0.35, R.INTERFACE, (1.0, 1.0), 2),
    ("Glass.", 1.0, R.ROOM, (0.94, 1.06), 3),
    ("Omnitrix.Beep", 0.5, R.INTERFACE, (1.0, 1.0), 1),
    ("Omnitrix.", 0.6, R.INTERFACE, (1.0, 1.0), 2),
]

# Each alien's voice: kinds (from make_museum_sounds.py voice_set) per use, its footsteps (a library
# Step.* set), a loop, seconds between its calls, and levels.
VOICES = {
    "Wildmutt": dict(calls=["Growl", "Sniff"], alerts=["Snarl", "Bark"], efforts=["Effort"], held=["Whine"],
                     steps="Step.Claw", interval=(14, 28)),
    "Benwolf": dict(calls=["Growl", "Sniff"], alerts=["Snarl"], efforts=["Effort"], held=["Huff"],
                    steps="Step.Claw", interval=(16, 32)),
    "Ghostfreak": dict(calls=["Whisper", "Moan", "Laugh"], alerts=["Hiss"], efforts=["Hiss"], held=["Laugh", "Moan"],
                       interval=(15, 30), volume=1.2),
    "FourArms": dict(calls=["Grunt", "Chuckle"], alerts=["Roar"], efforts=["Effort"], held=["Chuckle"],
                     steps="Step.Heavy", interval=(18, 35), step_volume=0.6),
    "XLR8": dict(calls=["Chirp"], alerts=["Screech"], efforts=["Effort"], held=["Chirp"], steps="Step.Light", interval=(12, 26)),
    "Diamondhead": dict(calls=["Hum"], alerts=["Clang"], efforts=["Effort"], held=["Hum"], steps="Step.Crystal", interval=(18, 36)),
    "Upgrade": dict(calls=["Bleeps"], alerts=["Bwoop"], efforts=["Glitch"], held=["Happy"], steps="Step.Metal", interval=(14, 28)),
    "GreyMatter": dict(calls=["Chatter", "Query"], alerts=["Eep"], efforts=["Eep"], held=["Eep", "Chatter"],
                       steps="Step.Light", interval=(12, 24), step_volume=0.3),
    "Stinkfly": dict(calls=["Chitter"], alerts=["Squeal"], efforts=["Buzz"], held=["Buzz", "Squeal"],
                     loop="Loop.Stinkfly.Wings", loop_volume=0.8, interval=(14, 28)),
    "Ripjaws": dict(calls=["Gurgle", "Hiss"], alerts=["Snap"], efforts=["Effort"], held=["Gasp"], steps="Step.Medium", interval=(15, 30)),
    "Cannonbolt": dict(calls=["Hmm"], alerts=["Whoa"], efforts=["Oof"], held=["Whoa", "Oof"], steps="Step.Heavy",
                       interval=(18, 34), step_volume=0.55),
    "Wildvine": dict(calls=["Creak", "Rustle"], alerts=["Rustle"], efforts=["Effort"], held=["Groan"], steps="Step.Rustle", interval=(16, 32)),
    "Upchuck": dict(calls=["Mmm", "Burp", "Rumble"], alerts=["Huh"], efforts=["Effort"], held=["Giggle"], steps="Step.Squish", interval=(14, 28)),
    "Ditto": dict(calls=["Hey", "Giggle"], alerts=["Whoa"], efforts=["Effort"], held=["Giggle"], steps="Step.Light", interval=(12, 26)),
    "EchoEcho": dict(calls=["Chirp"], alerts=["Squeal"], efforts=["Blip"], held=["Down"], steps="Step.Metal",
                     interval=(14, 28), step_volume=0.35),
    "Heatblast": dict(calls=["Crackle"], alerts=["Roar"], efforts=["Roar"], held=["Crackle"], steps="Step.Medium", interval=(16, 30)),
}


def mix_for(name):
    for prefix, volume, rng, pitch, limit in MIX:
        if name.startswith(prefix):
            return volume, rng, pitch, limit
    return 0.8, R.ROOM, (0.96, 1.04), 0


def folder_for(entry):
    if entry["group"] == "voice":
        return f"{AUDIO_ROOT}/Voices/{entry['alien']}"
    return f"{AUDIO_ROOT}/SFX/{entry['name'].split('.')[0]}"


def enum(enum_name, value_name):
    """An engine enum value, or None if this engine version names it differently."""
    value = getattr(getattr(unreal, enum_name, None), value_name, None)
    if value is None:
        unreal.log_warning(f"unreal.{enum_name}.{value_name} not found")
    return value


def set_if(obj, prop, value):
    """Sets a property (or the first of several spellings of it); warns instead of failing if this engine
    version names it differently."""
    if value is None:
        return False
    for name in (prop if isinstance(prop, (list, tuple)) else [prop]):
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception:
            continue
    unreal.log_warning(f"{type(obj).__name__}: could not set {prop}")
    return False


def import_sounds(manifest):
    """Imports every .wav (replacing earlier imports). Returns file name -> SoundWave."""
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
    tasks = []
    for entry in manifest:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(SOUNDS_DIR, entry["file"] + ".wav"))
        task.set_editor_property("destination_path", folder_for(entry))
        task.set_editor_property("destination_name", entry["file"])
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    AT.import_asset_tasks(tasks)

    waves = {}
    for entry in manifest:
        path = f"{folder_for(entry)}/{entry['file']}"
        wave = unreal.load_asset(path)
        if not isinstance(wave, unreal.SoundWave):
            raise RuntimeError(f"{path} was not imported as a sound")
        loop = bool(entry.get("loop"))
        set_if(wave, "looping", loop)
        if loop:
            # Out of range (a case across the room) it goes quiet for free and starts again when you come close.
            set_if(wave, "virtualization_mode", enum("VirtualizationMode", "RESTART"))
        else:
            # Short sounds stay in memory: a growl or a click starts the moment it is asked for.
            set_if(wave, "loading_behavior", enum("SoundWaveLoadingBehavior", "RETAIN_ON_LOAD"))
        EAL.save_loaded_asset(wave, only_if_is_dirty=False)
        waves[entry["file"]] = wave
    return waves


def make_attenuation(name, inner, falloff, far_lpf):
    path = f"{AUDIO_ROOT}/{name}"
    asset = unreal.load_asset(path) if EAL.does_asset_exist(path) else AT.create_asset(name, AUDIO_ROOT, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = asset.get_editor_property("attenuation")
    set_if(settings, "attenuate", True)
    set_if(settings, "spatialize", True)
    # HRTF: rendered by the project's spatialization plugin (Resonance Audio) - above / behind you, not just left / right.
    set_if(settings, "spatialization_algorithm", enum("SoundSpatializationAlgorithm", "SPATIALIZATION_HRTF"))
    set_if(settings, "attenuation_shape", enum("AttenuationShape", "SPHERE"))
    set_if(settings, "attenuation_shape_extents", unreal.Vector(inner, 0.0, 0.0))
    set_if(settings, "falloff_distance", falloff)
    set_if(settings, "distance_algorithm", enum("AttenuationDistanceModel", "NATURAL_SOUND"))
    set_if(settings, ["d_b_attenuation_at_max", "db_attenuation_at_max", "decibel_attenuation_at_max"], -60.0)
    if far_lpf:
        # Air: a sound far across the room is a little duller than one next to you.
        set_if(settings, "attenuate_with_lpf", True)
        set_if(settings, "lpf_radius_min", inner + falloff * 0.2)
        set_if(settings, "lpf_radius_max", inner + falloff)
        set_if(settings, "lpf_frequency_at_min", 20000.0)
        set_if(settings, "lpf_frequency_at_max", far_lpf)
    asset.set_editor_property("attenuation", settings)
    EAL.save_loaded_asset(asset, only_if_is_dirty=False)
    return asset


def make_library(manifest, waves, ranges):
    path = f"{AUDIO_ROOT}/DA_MuseumSounds"
    if EAL.does_asset_exist(path):
        library = unreal.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.MuseumSoundLibrary)
        library = AT.create_asset("DA_MuseumSounds", AUDIO_ROOT, unreal.MuseumSoundLibrary, factory)
    by_name = {}
    for entry in manifest:
        if entry["group"] != "voice":
            by_name.setdefault(entry["name"], []).append(waves[entry["file"]])
    sounds = []
    for name in sorted(by_name):
        volume, rng, pitch, limit = mix_for(name)
        sound = unreal.MuseumSound()
        sound.set_editor_property("name", unreal.Name(name))
        sound.set_editor_property("variations", by_name[name])
        sound.set_editor_property("volume", volume)
        sound.set_editor_property("pitch", unreal.Vector2D(pitch[0], pitch[1]))
        sound.set_editor_property("range", rng)
        sound.set_editor_property("max_playing", limit)
        sounds.append(sound)
    library.set_editor_property("sounds", sounds)
    library.set_editor_property("room_attenuation", ranges["room"])
    library.set_editor_property("near_attenuation", ranges["near"])
    library.set_editor_property("interface_attenuation", ranges["interface"])
    # Real display-case glass: the highs roll off and it is a little quieter - but you still hear them clearly.
    library.set_editor_property("glass_cutoff", 3000.0)
    library.set_editor_property("glass_volume", 0.8)
    EAL.save_loaded_asset(library, only_if_is_dirty=False)
    print(f"LIBRARY {len(sounds)} sounds -> {library.get_path_name()}")
    return by_name


def set_voices(manifest, waves, library_sounds):
    """Gives every Ben 10 alien data asset (DA_Model_<Alien>[_n], DA_Classic_<Alien>) its voice."""
    voice_files = {}
    for entry in manifest:
        if entry["group"] == "voice":
            voice_files.setdefault((entry["alien"], entry["kind"]), []).append(waves[entry["file"]])
    done = 0
    for folder in DATA_FOLDERS:
        for path in EAL.list_assets(folder, recursive=False, include_folder=False):
            match = re.match(r"^DA_(?:Model|Classic)_([A-Za-z0-9]+?)(?:_\d+)?$", path.split("/")[-1].split(".")[0])
            spec = VOICES.get(match.group(1)) if match else None
            if not spec:
                continue
            alien = match.group(1)
            data = unreal.load_asset(path)
            if not isinstance(data, unreal.AlienDataAsset):
                continue

            def gather(kinds):
                found = []
                for kind in kinds:
                    files = voice_files.get((alien, kind))
                    if not files:
                        raise RuntimeError(f"no {alien} {kind} sounds - regenerate with make_museum_sounds.py")
                    found += files
                return found

            sounds = unreal.AlienSounds()
            sounds.set_editor_property("calls", gather(spec["calls"]))
            sounds.set_editor_property("alerts", gather(spec["alerts"]))
            sounds.set_editor_property("efforts", gather(spec["efforts"]))
            sounds.set_editor_property("held", gather(spec["held"]))
            sounds.set_editor_property("footsteps", list(library_sounds.get(spec["steps"], [])) if spec.get("steps") else [])
            sounds.set_editor_property("loop", library_sounds[spec["loop"]][0] if spec.get("loop") else None)
            sounds.set_editor_property("call_interval", unreal.Vector2D(*spec["interval"]))
            sounds.set_editor_property("volume", spec.get("volume", 1.3))
            sounds.set_editor_property("footstep_volume", spec.get("step_volume", 0.5))
            sounds.set_editor_property("loop_volume", spec.get("loop_volume", 0.5))
            sounds.set_editor_property("pitch", spec.get("pitch", 1.0))
            data.set_editor_property("sounds", sounds)
            EAL.save_loaded_asset(data, only_if_is_dirty=False)
            done += 1
            print(f"VOICE {data.get_name()}: {alien} calls={len(sounds.get_editor_property('calls'))} "
                  f"steps={spec.get('steps')} loop={spec.get('loop')}")
    return done


def existing_sounds(manifest):
    """The sounds as imported last time (to change only the mix), or None if any is missing."""
    waves = {}
    for entry in manifest:
        wave = unreal.load_asset(f"{folder_for(entry)}/{entry['file']}")
        if not isinstance(wave, unreal.SoundWave):
            return None
        waves[entry["file"]] = wave
    return waves


def main(mix_only=False):
    """mix_only: keep the imported sounds, redo the ranges, the library and the voices."""
    with open(os.path.join(SOUNDS_DIR, "sounds.json"), encoding="utf-8") as handle:
        manifest = json.load(handle)
    EAL.make_directory(AUDIO_ROOT)
    waves = (existing_sounds(manifest) if mix_only else None) or import_sounds(manifest)
    ranges = {
        "room": make_attenuation("ATT_MuseumRoom", 80.0, 700.0, 4500.0),
        "near": make_attenuation("ATT_MuseumNear", 30.0, 180.0, None),
        "interface": make_attenuation("ATT_MuseumInterface", 100.0, 250.0, None),
    }
    library_sounds = make_library(manifest, waves, ranges)
    aliens = set_voices(manifest, waves, library_sounds)
    print(f"SOUNDS DONE: {len(waves)} sounds{' (kept as imported)' if mix_only else ' imported'}, {aliens} aliens have a voice")


if __name__ == "__main__":
    import sys
    main(mix_only="--mix-only" in sys.argv[1:])  # -script="import_museum_sounds.py --mix-only"
