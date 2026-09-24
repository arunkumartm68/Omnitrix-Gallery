"""
Alien Museum - creates the ten classic Ben 10 aliens as AlienDataAssets (personal fan project).

Ben 10 and its characters belong to Cartoon Network / Warner Bros. Discovery. These are simple
fan-made placeholder figures built from basic shapes, for private use only - do not publish them.

Run inside the Unreal Editor (Output Log > Python, or the Unreal MCP):
    exec(open(r"C:/Games/Ben10/Scripts/create_classic_aliens.py").read())

Re-running updates the existing assets in place. You can also edit every value afterwards in the
data assets under /Game/AlienMuseum/Data/Classic.

Part numbers are in "height units": 1.0 = the alien's full Height. X = forward, Y = right, Z = up.
Rotations below are written as (pitch, yaw, roll). Cones point up (+Z) before rotation.
"""
import unreal

FOLDER = "/Game/AlienMuseum/Data/Classic"
COLLECTION_NAME = "DA_AlienCollection_Classic"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

OMNITRIX_GREEN = (0.3, 1.0, 0.25)
TOOTH = (0.95, 0.95, 0.88)
WING = (0.35, 0.8, 0.2)


def part(shape, attach, offset, size, color="SKIN", rot=(0, 0, 0), mirror=False,
         motion="NONE", amount=10.0, speed=1.0, phase=0.0, pivot=(0, 0, 0),
         custom=None, glows=False):
    """One body part. rot = (pitch, yaw, roll) in degrees."""
    p = unreal.AlienBodyPart()
    p.set_editor_property("shape", getattr(unreal.AlienPartShape, shape))
    p.set_editor_property("attach_to", getattr(unreal.AlienPartAttach, attach))
    p.set_editor_property("offset", unreal.Vector(*offset))
    p.set_editor_property("rotation", unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]))
    p.set_editor_property("size", unreal.Vector(*size))
    p.set_editor_property("color", getattr(unreal.AlienPartColor, color))
    if custom is not None:
        p.set_editor_property("custom_color", unreal.LinearColor(custom[0], custom[1], custom[2], 1.0))
        p.set_editor_property("custom_glows", glows)
    p.set_editor_property("mirror", mirror)
    p.set_editor_property("motion", getattr(unreal.AlienPartMotion, motion))
    p.set_editor_property("motion_amount", float(amount))
    p.set_editor_property("motion_speed", float(speed))
    p.set_editor_property("motion_phase", float(phase))
    p.set_editor_property("pivot_offset", unreal.Vector(*pivot))
    return p


def badge(x, z, d=0.07, y=0.0, attach="BODY"):
    """The Omnitrix symbol: a dark disc with a glowing green centre, facing forward."""
    return [
        part("CYLINDER", attach, (x, y, z), (d, d, 0.015), "DARK", rot=(90, 0, 0)),
        part("CYLINDER", attach, (x + 0.007, y, z), (d * 0.64, d * 0.64, 0.015), "CUSTOM",
             rot=(90, 0, 0), custom=OMNITRIX_GREEN, glows=True),
    ]


# ---------------------------------------------------------------------------------------------
# The classic ten
# ---------------------------------------------------------------------------------------------

ALIENS = []

# 1. Heatblast - magma-rock body, flaming head
ALIENS.append(dict(
    asset="DA_Classic_Heatblast", id="Heatblast", name="Heatblast", species="Pyronite", planet="Pyros",
    desc="A living fire elemental with a magma-rock body. Hurls fireballs, flies on jets of flame and melts through almost anything.",
    height=40, skin=(0.35, 0.08, 0.03), accent=(0.10, 0.04, 0.03), eye=(1.0, 0.95, 0.45), glow=(1.0, 0.45, 0.05),
    rim=(1.0, 0.35, 0.05), head=(0, 0, 0.64), radius=0.20, chamber=(1.0, 0.40, 0.10),
    walk=24, idle=(1.0, 3.0), look=0.30, curiosity=0.65, notice=170, energy=0.70,
    parts=[
        part("SPHERE", "BODY", (0, 0, 0.47), (0.22, 0.30, 0.34)),                                    # torso
        part("SPHERE", "BODY", (0, 0, 0.31), (0.18, 0.24, 0.14), "ACCENT"),                          # hips
        part("CUBE", "BODY", (0.095, 0.07, 0.44), (0.01, 0.03, 0.16), "GLOW", rot=(0, 0, 25)),        # lava cracks
        part("CUBE", "BODY", (0.093, -0.06, 0.41), (0.01, 0.03, 0.12), "GLOW", rot=(0, 0, -30)),
        part("CYLINDER", "BODY", (0, 0.07, 0.15), (0.09, 0.09, 0.30), mirror=True,
             motion="WALK_SWING", amount=25, pivot=(0, 0, 0.15)),                                    # legs
        part("SPHERE", "BODY", (0.03, 0.07, 0.025), (0.14, 0.10, 0.06), "ACCENT", mirror=True,
             motion="WALK_SWING", amount=25, pivot=(-0.03, 0, 0.275)),                               # feet
        part("CYLINDER", "BODY", (0, 0.19, 0.46), (0.075, 0.075, 0.26), rot=(0, 0, -10), mirror=True,
             motion="WALK_SWING", amount=22, phase=0.5, pivot=(0, -0.023, 0.128)),                   # arms
        part("SPHERE", "BODY", (0, 0.215, 0.32), (0.075, 0.075, 0.075), "GLOW", mirror=True,
             motion="WALK_SWING", amount=22, phase=0.5, pivot=(0, -0.048, 0.268)),                   # burning hands
        part("SPHERE", "BODY", (0, 0.14, 0.60), (0.11, 0.12, 0.08), "ACCENT", mirror=True),          # shoulder rocks
        part("SPHERE", "HEAD", (0, 0, 0.10), (0.19, 0.19, 0.20)),                                    # head
        part("SPHERE", "HEAD", (0.085, 0.04, 0.11), (0.025, 0.05, 0.03), "EYE", mirror=True),       # eyes
        part("CUBE", "HEAD", (0.088, 0, 0.05), (0.012, 0.06, 0.012), "GLOW"),                        # glowing mouth
        part("CONE", "HEAD", (-0.01, 0, 0.27), (0.15, 0.15, 0.24), "GLOW",
             motion="FLICKER", amount=25, speed=2.0),                                                # flame crown
        part("CONE", "HEAD", (-0.02, 0.055, 0.23), (0.09, 0.09, 0.17), "CUSTOM", rot=(0, 0, 25), mirror=True,
             custom=(1.0, 0.8, 0.15), glows=True, motion="FLICKER", amount=30, speed=2.6, phase=0.3),
        part("CONE", "HEAD", (-0.06, 0, 0.21), (0.10, 0.10, 0.18), "GLOW", rot=(30, 0, 0),
             motion="FLICKER", amount=30, speed=2.2, phase=0.6),
    ] + badge(0.105, 0.53),
))

# 2. Wildmutt - eyeless orange quadruped
ALIENS.append(dict(
    asset="DA_Classic_Wildmutt", id="Wildmutt", name="Wildmutt", species="Vulpimancer", planet="Vulpin",
    desc="An eyeless orange beast that senses everything through its nose and neck gills. Strong, fast and an amazing climber.",
    height=24, skin=(0.90, 0.28, 0.02), accent=(0.40, 0.10, 0.02), eye=(1.0, 0.6, 0.2), glow=(1.0, 0.6, 0.2),
    rim=(1.0, 0.55, 0.15), head=(0.36, 0, 0.52), radius=0.45, chamber=(1.0, 0.55, 0.15),
    walk=32, idle=(0.8, 2.5), look=0.45, curiosity=0.85, notice=170, energy=0.90,
    parts=[
        part("SPHERE", "BODY", (-0.04, 0, 0.44), (0.70, 0.40, 0.38)),                                # body
        part("SPHERE", "BODY", (0.18, 0, 0.47), (0.36, 0.42, 0.42)),                                 # chest
        part("CYLINDER", "BODY", (0.20, 0.12, 0.19), (0.10, 0.10, 0.36), mirror=True,
             motion="WALK_SWING", amount=28, pivot=(0, 0, 0.18)),                                    # front legs
        part("CYLINDER", "BODY", (-0.28, 0.12, 0.19), (0.11, 0.11, 0.36), mirror=True,
             motion="WALK_SWING", amount=28, phase=0.5, pivot=(0, 0, 0.18)),                         # back legs
        part("SPHERE", "BODY", (0.23, 0.12, 0.025), (0.13, 0.11, 0.05), "ACCENT", mirror=True,
             motion="WALK_SWING", amount=28, pivot=(-0.03, 0, 0.345)),                               # front paws
        part("SPHERE", "BODY", (-0.25, 0.12, 0.025), (0.13, 0.11, 0.05), "ACCENT", mirror=True,
             motion="WALK_SWING", amount=28, phase=0.5, pivot=(-0.03, 0, 0.345)),                    # back paws
        part("SPHERE", "HEAD", (0.06, 0, 0), (0.28, 0.30, 0.24)),                                    # head (no eyes)
        part("SPHERE", "HEAD", (0.15, 0, -0.03), (0.16, 0.20, 0.13)),                                # snout
        part("CUBE", "HEAD", (0.19, 0, -0.06), (0.08, 0.18, 0.02), "DARK"),                          # wide mouth
        part("CONE", "HEAD", (0.205, 0.045, -0.045), (0.025, 0.025, 0.045), "CUSTOM", rot=(180, 0, 0),
             mirror=True, custom=TOOTH),                                                             # fangs
        part("CUBE", "HEAD", (0.0, 0.136, 0.0), (0.05, 0.01, 0.07), "DARK", mirror=True),            # neck gills
        part("CONE", "BODY", (0.12, 0, 0.66), (0.07, 0.09, 0.14), "ACCENT", rot=(25, 0, 0)),          # back spikes
        part("CONE", "BODY", (-0.06, 0, 0.64), (0.07, 0.09, 0.15), "ACCENT", rot=(35, 0, 0)),
        part("CONE", "BODY", (-0.24, 0, 0.61), (0.06, 0.08, 0.13), "ACCENT", rot=(45, 0, 0)),
        part("CONE", "BODY", (-0.46, 0, 0.50), (0.07, 0.07, 0.20), "ACCENT", rot=(70, 0, 0),
             motion="SWAY_YAW", amount=25, speed=1.5, pivot=(0.094, 0, -0.034)),                     # wagging tail
    ] + badge(0.355, 0.42, d=0.07),
))

# 3. Diamondhead - green crystal body, pointed crystal head
ALIENS.append(dict(
    asset="DA_Classic_Diamondhead", id="Diamondhead", name="Diamondhead", species="Petrosapien", planet="Petropia",
    desc="A living crystal who grows razor-sharp shards from his body and bounces energy blasts right back at his foes.",
    height=42, skin=(0.15, 0.85, 0.55), accent=(0.03, 0.03, 0.035), eye=(0.35, 1.0, 0.5), glow=(0.5, 1.0, 0.75),
    rim=(0.75, 1.0, 0.85), style="CRYSTAL", head=(0, 0, 0.66), radius=0.20, chamber=(0.25, 1.0, 0.60),
    walk=18, idle=(2.0, 4.5), look=0.45, curiosity=0.60, notice=160, energy=0.40,
    parts=[
        part("CUBE", "BODY", (0, 0, 0.48), (0.22, 0.22, 0.30), rot=(0, 45, 0)),                       # faceted torso
        part("CUBE", "BODY", (0, 0, 0.31), (0.16, 0.22, 0.10), "ACCENT"),                            # black shorts
        part("CYLINDER", "BODY", (0, 0.065, 0.14), (0.085, 0.085, 0.28), mirror=True,
             motion="WALK_SWING", amount=25, pivot=(0, 0, 0.14)),                                    # legs
        part("CYLINDER", "BODY", (0, 0.19, 0.46), (0.075, 0.075, 0.26), rot=(0, 0, -8), mirror=True,
             motion="WALK_SWING", amount=20, phase=0.5, pivot=(0, -0.018, 0.129)),                   # arms
        part("CONE", "BODY", (-0.02, 0.15, 0.66), (0.07, 0.07, 0.18), rot=(10, 0, 35), mirror=True),  # shoulder shards
        part("CONE", "BODY", (-0.10, 0, 0.60), (0.06, 0.12, 0.20), rot=(40, 0, 0)),                   # back shard
        part("CUBE", "HEAD", (0, 0, 0.09), (0.13, 0.13, 0.16), rot=(0, 45, 0)),                       # diamond head
        part("CONE", "HEAD", (-0.02, 0, 0.25), (0.05, 0.14, 0.26), rot=(15, 0, 0)),                   # head crystal
        part("SPHERE", "HEAD", (0.07, 0.033, 0.10), (0.02, 0.035, 0.022), "EYE", mirror=True),      # eyes
    ] + badge(0.15, 0.53, d=0.07),
))

# 4. XLR8 - blue raptor speedster with visor and ball feet
ALIENS.append(dict(
    asset="DA_Classic_XLR8", id="XLR8", name="XLR8", species="Kineceleran", planet="Kinet",
    desc="The speedster of the classic ten. Zips around on ball-shaped feet at hundreds of miles per hour.",
    height=36, skin=(0.05, 0.12, 0.50), accent=(0.02, 0.02, 0.03), eye=(0.3, 0.9, 1.0), glow=(0.05, 0.45, 1.0),
    rim=(0.35, 0.7, 1.0), head=(0.10, 0, 0.60), radius=0.26, chamber=(0.20, 0.55, 1.0),
    walk=70, idle=(0.4, 1.4), look=0.15, curiosity=0.90, notice=200, energy=1.00,
    parts=[
        part("SPHERE", "BODY", (0.02, 0, 0.45), (0.18, 0.22, 0.30), rot=(-20, 0, 0)),                # torso, leaning forward
        part("CYLINDER", "BODY", (-0.02, 0.07, 0.22), (0.08, 0.08, 0.20), rot=(20, 0, 0), mirror=True,
             motion="WALK_SWING", amount=35, pivot=(-0.034, 0, 0.094)),                             # thighs
        part("CYLINDER", "BODY", (0.0, 0.07, 0.08), (0.06, 0.06, 0.16), "ACCENT", rot=(-25, 0, 0), mirror=True,
             motion="WALK_SWING", amount=35, pivot=(-0.054, 0, 0.234)),                              # shins
        part("SPHERE", "BODY", (-0.03, 0.07, 0.03), (0.08, 0.07, 0.07), mirror=True,
             motion="WALK_SWING", amount=35, pivot=(-0.024, 0, 0.284)),                              # ball feet
        part("CYLINDER", "BODY", (0.08, 0.14, 0.44), (0.055, 0.055, 0.18), rot=(40, 0, -10), mirror=True,
             motion="WALK_SWING", amount=25, phase=0.5, pivot=(-0.057, -0.016, 0.068)),             # arms
        part("CONE", "BODY", (-0.24, 0, 0.31), (0.08, 0.10, 0.32), rot=(100, 0, 0),
             motion="SWAY_YAW", amount=18, speed=1.2, pivot=(0.158, 0, 0.028)),                      # long tail
        part("SPHERE", "HEAD", (0.05, 0, 0.06), (0.22, 0.15, 0.14), rot=(-10, 0, 0)),                 # helmet
        part("CONE", "HEAD", (-0.06, 0, 0.10), (0.05, 0.06, 0.16), rot=(75, 0, 0)),                   # helmet crest
        part("CUBE", "HEAD", (0.14, 0, 0.075), (0.05, 0.14, 0.03), "GLOW", rot=(-10, 0, 0)),          # visor
        part("SPHERE", "HEAD", (0.13, 0, 0.03), (0.07, 0.10, 0.05), "ACCENT"),                        # black face
    ] + badge(0.13, 0.50, d=0.065),
))

# 5. Grey Matter - tiny grey genius with big green eyes
ALIENS.append(dict(
    asset="DA_Classic_GreyMatter", id="GreyMatter", name="Grey Matter", species="Galvan", planet="Galvan Prime",
    desc="Only a few inches tall, but one of the smartest beings in the galaxy. Small enough to fix machines from the inside.",
    height=16, skin=(0.40, 0.42, 0.45), accent=(0.22, 0.23, 0.25), eye=(0.3, 1.0, 0.3), glow=(0.3, 1.0, 0.3),
    rim=None, head=(0, 0, 0.46), radius=0.28, chamber=(0.35, 1.0, 0.45),
    walk=14, idle=(1.5, 3.5), look=0.60, curiosity=0.90, notice=150, energy=0.70,
    parts=[
        part("SPHERE", "BODY", (0, 0, 0.30), (0.24, 0.28, 0.28)),                                    # body
        part("SPHERE", "BODY", (0.055, 0, 0.28), (0.14, 0.20, 0.20), "CUSTOM", custom=(0.62, 0.64, 0.66)),  # pale belly
        part("CYLINDER", "BODY", (0, 0.06, 0.09), (0.06, 0.06, 0.18), mirror=True,
             motion="WALK_SWING", amount=25, pivot=(0, 0, 0.09)),                                    # legs
        part("SPHERE", "BODY", (0.04, 0.06, 0.02), (0.12, 0.08, 0.04), mirror=True,
             motion="WALK_SWING", amount=25, pivot=(-0.04, 0, 0.16)),                                # frog feet
        part("CYLINDER", "BODY", (0, 0.15, 0.30), (0.045, 0.045, 0.18), rot=(0, 0, -20), mirror=True,
             motion="WALK_SWING", amount=20, phase=0.5, pivot=(0, -0.031, 0.085)),                   # arms
        part("SPHERE", "HEAD", (0, 0, 0.21), (0.44, 0.50, 0.40)),                                    # huge head
        part("SPHERE", "HEAD", (0.17, 0.12, 0.26), (0.10, 0.15, 0.13), "EYE", mirror=True),         # big green eyes
        part("CUBE", "HEAD", (0.195, 0, 0.12), (0.01, 0.16, 0.01), "DARK"),                          # mouth
    ] + badge(0.11, 0.36, d=0.07),
))

# 6. Four Arms - red giant with four arms and four eyes
ALIENS.append(dict(
    asset="DA_Classic_FourArms", id="FourArms", name="Four Arms", species="Tetramand", planet="Khoros",
    desc="A four-armed, four-eyed giant. Strong enough to lift a truck and to clap out shockwaves.",
    height=45, skin=(0.80, 0.12, 0.08), accent=(0.03, 0.03, 0.03), eye=(1.0, 0.8, 0.2), glow=(1.0, 0.8, 0.2),
    rim=(1.0, 0.45, 0.35), head=(0, 0, 0.70), radius=0.30, chamber=(1.0, 0.25, 0.20),
    walk=18, idle=(2.0, 4.0), look=0.35, curiosity=0.50, notice=180, energy=0.50,
    parts=[
        part("SPHERE", "BODY", (0, 0, 0.53), (0.32, 0.48, 0.34)),                                    # huge chest
        part("SPHERE", "BODY", (0, 0, 0.37), (0.24, 0.32, 0.18)),                                    # waist
        part("CUBE", "BODY", (0, 0, 0.28), (0.22, 0.30, 0.10), "ACCENT"),                            # pants
        part("CYLINDER", "BODY", (0, 0.085, 0.13), (0.12, 0.12, 0.26), "ACCENT", mirror=True,
             motion="WALK_SWING", amount=20, pivot=(0, 0, 0.13)),                                    # legs
        part("SPHERE", "BODY", (0.04, 0.085, 0.025), (0.14, 0.12, 0.05), mirror=True,
             motion="WALK_SWING", amount=20, pivot=(-0.04, 0, 0.235)),                               # feet
        part("CYLINDER", "BODY", (0, 0.29, 0.54), (0.10, 0.10, 0.24), rot=(0, 0, -25), mirror=True,
             motion="WALK_SWING", amount=18, phase=0.5, pivot=(0, -0.051, 0.109)),                   # upper arms
        part("SPHERE", "BODY", (0, 0.345, 0.415), (0.10, 0.10, 0.10), mirror=True,
             motion="WALK_SWING", amount=18, phase=0.5, pivot=(0, -0.106, 0.234)),                   # upper fists
        part("CYLINDER", "BODY", (0.02, 0.25, 0.41), (0.09, 0.09, 0.22), rot=(0, 0, -15), mirror=True,
             motion="WALK_SWING", amount=18, pivot=(0, -0.028, 0.106)),                              # lower arms
        part("SPHERE", "BODY", (0.02, 0.28, 0.29), (0.09, 0.09, 0.09), mirror=True,
             motion="WALK_SWING", amount=18, pivot=(0, -0.058, 0.226)),                              # lower fists
        part("SPHERE", "HEAD", (0.01, 0, 0.08), (0.17, 0.18, 0.17)),                                 # head
        part("SPHERE", "HEAD", (0.075, 0.035, 0.11), (0.02, 0.032, 0.02), "EYE", mirror=True),      # upper eyes
        part("SPHERE", "HEAD", (0.08, 0.035, 0.075), (0.02, 0.032, 0.02), "EYE", mirror=True),      # lower eyes
        part("CUBE", "HEAD", (0.086, 0, 0.04), (0.01, 0.05, 0.008), "DARK"),                         # mouth
    ] + badge(0.148, 0.60, d=0.07),
))

# 7. Stinkfly - green flying insect with eye stalks and four wings
ALIENS.append(dict(
    asset="DA_Classic_Stinkfly", id="Stinkfly", name="Stinkfly", species="Lepidopterran", planet="Lepidopterra",
    desc="A buzzing insect alien that flies on four wings and shoots sticky goo from its eye stalks.",
    height=32, skin=(0.40, 0.70, 0.10), accent=(0.85, 0.80, 0.15), eye=(0.95, 0.9, 0.3), glow=WING,
    rim=None, hovers=True, head=(0.12, 0, 0.52), radius=0.30, chamber=(0.55, 1.0, 0.25),
    walk=32, idle=(0.8, 2.5), look=0.30, curiosity=0.75, notice=170, energy=0.85,
    parts=[
        part("SPHERE", "BODY", (0, 0, 0.44), (0.24, 0.24, 0.24)),                                    # thorax
        part("SPHERE", "BODY", (-0.20, 0, 0.36), (0.14, 0.16, 0.34), rot=(-55, 0, 0)),               # abdomen
        part("SPHERE", "BODY", (-0.24, 0, 0.33), (0.15, 0.17, 0.04), "ACCENT", rot=(-55, 0, 0)),      # yellow band
        part("CONE", "BODY", (-0.37, 0, 0.23), (0.05, 0.05, 0.12), "ACCENT", rot=(125, 0, 0)),       # stinger
        part("CYLINDER", "BODY", (0.03, 0.07, 0.29), (0.025, 0.025, 0.14), "ACCENT", rot=(0, 0, -15), mirror=True,
             motion="SWAY_PITCH", amount=10, speed=0.7, pivot=(0, -0.018, 0.068)),                   # dangling legs
        part("SPHERE", "BODY", (-0.04, 0.20, 0.56), (0.20, 0.34, 0.015), "CUSTOM", rot=(0, 15, 15), mirror=True,
             custom=WING, glows=True, motion="SWAY_ROLL", amount=28, speed=5.0,
             pivot=(0.0425, -0.1586, -0.044)),                                                       # upper wings
        part("SPHERE", "BODY", (-0.12, 0.16, 0.50), (0.14, 0.26, 0.012), "CUSTOM", rot=(0, 35, 5), mirror=True,
             custom=WING, glows=True, motion="SWAY_ROLL", amount=22, speed=5.0, phase=0.15,
             pivot=(0.0742, -0.106, -0.0113)),                                                       # lower wings
        part("SPHERE", "HEAD", (0.06, 0, 0.03), (0.16, 0.18, 0.14)),                                 # head
        part("CONE", "HEAD", (0.15, 0, -0.02), (0.03, 0.04, 0.08), "ACCENT", rot=(-115, 0, 0)),      # mouth parts
        part("CYLINDER", "HEAD", (0.07, 0.045, 0.13), (0.02, 0.02, 0.13), rot=(0, 0, 25), mirror=True,
             motion="SWAY_ROLL", amount=8, speed=0.8, pivot=(0, -0.0275, -0.0589)),                  # eye stalks
        part("SPHERE", "HEAD", (0.07, 0.075, 0.195), (0.045, 0.045, 0.045), "EYE", mirror=True,
             motion="SWAY_ROLL", amount=8, speed=0.8, pivot=(0, -0.0575, -0.124)),                   # stalk eyes
    ] + badge(0.113, 0.40, d=0.06),
))

# 8. Ripjaws - deep-sea fish man with huge jaws and a glowing lure (shown swimming)
ALIENS.append(dict(
    asset="DA_Classic_Ripjaws", id="Ripjaws", name="Ripjaws", species="Piscciss Volann", planet="Piscciss",
    desc="A deep-sea predator with huge jaws and a glowing lure. Unstoppable underwater, but dries out fast on land.",
    height=36, skin=(0.45, 0.55, 0.55), accent=(0.85, 0.88, 0.82), eye=(1.0, 0.9, 0.3), glow=(0.4, 1.0, 0.9),
    rim=None, hovers=True, head=(0.02, 0, 0.64), radius=0.22, chamber=(0.10, 0.45, 1.0),
    walk=26, idle=(1.0, 3.0), look=0.35, curiosity=0.70, notice=170, energy=0.60,
    parts=[
        part("SPHERE", "BODY", (0, 0, 0.50), (0.24, 0.30, 0.30)),                                    # torso
        part("SPHERE", "BODY", (0.07, 0, 0.46), (0.14, 0.22, 0.26), "ACCENT"),                       # pale belly
        part("CONE", "BODY", (-0.02, 0, 0.24), (0.20, 0.24, 0.32), rot=(180, 0, 0),
             motion="SWAY_YAW", amount=12, speed=0.8, pivot=(0, 0, 0.16)),                           # fish tail
        part("SPHERE", "BODY", (-0.04, 0, 0.07), (0.22, 0.04, 0.12), rot=(25, 0, 0),
             motion="SWAY_YAW", amount=12, speed=0.8, pivot=(0.02, 0, 0.33)),                        # tail fin
        part("CYLINDER", "BODY", (0.01, 0.18, 0.47), (0.065, 0.065, 0.22), rot=(0, 0, -20), mirror=True,
             motion="SWAY_PITCH", amount=15, speed=0.7, pivot=(0, -0.0376, 0.1034)),                 # swimming arms
        part("CONE", "BODY", (-0.12, 0, 0.60), (0.14, 0.03, 0.18), rot=(35, 0, 0)),                  # dorsal fin
        part("SPHERE", "HEAD", (0.04, 0, 0.06), (0.22, 0.22, 0.17)),                                 # head
        part("SPHERE", "HEAD", (0.09, 0, -0.02), (0.18, 0.22, 0.08)),                                # big lower jaw
        part("CUBE", "HEAD", (0.13, 0, 0.015), (0.08, 0.17, 0.025), "DARK"),                         # mouth
        part("CONE", "HEAD", (0.165, 0.045, 0.028), (0.018, 0.018, 0.04), "CUSTOM", rot=(180, 0, 0),
             mirror=True, custom=TOOTH),                                                             # upper fangs
        part("CONE", "HEAD", (0.17, 0, 0.028), (0.018, 0.018, 0.04), "CUSTOM", rot=(180, 0, 0), custom=TOOTH),
        part("CONE", "HEAD", (0.165, 0.025, 0.0), (0.015, 0.015, 0.03), "CUSTOM", mirror=True, custom=TOOTH),  # lower fangs
        part("SPHERE", "HEAD", (0.105, 0.065, 0.10), (0.03, 0.04, 0.035), "EYE", mirror=True),      # eyes
        part("CYLINDER", "HEAD", (0.07, 0, 0.19), (0.015, 0.015, 0.14), rot=(-35, 0, 0),
             motion="SWAY_PITCH", amount=10, speed=0.7, pivot=(-0.040, 0, -0.057)),                  # lure stalk
        part("SPHERE", "HEAD", (0.115, 0, 0.25), (0.045, 0.045, 0.045), "GLOW",
             motion="SWAY_PITCH", amount=10, speed=0.7, pivot=(-0.085, 0, -0.117)),                  # glowing lure
    ] + badge(0.112, 0.56, d=0.065),
))

# 9. Upgrade - black liquid-metal body with green circuit lines and one eye
ALIENS.append(dict(
    asset="DA_Classic_Upgrade", id="Upgrade", name="Upgrade", species="Galvanic Mechamorph", planet="Galvan B",
    desc="Living liquid technology. Merges with any machine it touches and upgrades it from the inside.",
    height=38, skin=(0.02, 0.02, 0.025), accent=(0.08, 0.08, 0.09), eye=(0.35, 1.0, 0.4), glow=(0.2, 1.0, 0.3),
    rim=(0.15, 0.85, 0.3), head=(0, 0, 0.66), radius=0.20, chamber=(0.20, 1.0, 0.35),
    walk=20, idle=(1.2, 3.5), look=0.40, curiosity=0.75, notice=170, energy=0.60,
    parts=[
        part("SPHERE", "BODY", (0, 0, 0.48), (0.22, 0.28, 0.34)),                                    # torso
        part("SPHERE", "BODY", (0, 0, 0.48), (0.228, 0.018, 0.348), "GLOW"),                         # circuit line
        part("SPHERE", "BODY", (0, 0, 0.53), (0.216, 0.272, 0.016), "GLOW"),                         # circuit ring
        part("CYLINDER", "BODY", (0, 0.065, 0.15), (0.085, 0.085, 0.30), mirror=True,
             motion="WALK_SWING", amount=22, pivot=(0, 0, 0.15)),                                    # legs
        part("CUBE", "BODY", (0.044, 0.065, 0.15), (0.008, 0.015, 0.26), "GLOW", mirror=True,
             motion="WALK_SWING", amount=22, pivot=(-0.044, 0, 0.15)),                               # leg lines
        part("CYLINDER", "BODY", (0, 0.18, 0.47), (0.065, 0.065, 0.28), rot=(0, 0, -8), mirror=True,
             motion="WALK_SWING", amount=18, phase=0.5, pivot=(0, -0.0195, 0.1386)),                 # arms
        part("CUBE", "BODY", (0.034, 0.18, 0.47), (0.008, 0.012, 0.24), "GLOW", rot=(0, 0, -8), mirror=True,
             motion="WALK_SWING", amount=18, phase=0.5, pivot=(-0.034, -0.0195, 0.1386)),            # arm lines
        part("SPHERE", "HEAD", (0, 0, 0.10), (0.19, 0.19, 0.20)),                                    # head
        part("CYLINDER", "HEAD", (0.074, 0, 0.11), (0.11, 0.11, 0.02), "GLOW", rot=(90, 0, 0)),       # eye ring
        part("SPHERE", "HEAD", (0.078, 0, 0.11), (0.05, 0.075, 0.075), "EYE"),                       # single eye
    ] + badge(0.09, 0.60, d=0.06),
))

# 10. Ghostfreak - pale see-through ghost with black stripes and one purple eye
ALIENS.append(dict(
    asset="DA_Classic_Ghostfreak", id="Ghostfreak", name="Ghostfreak", species="Ectonurite", planet="Anur Phaetos",
    desc="A spooky ghost-like alien that turns invisible, passes through walls and possesses others. Keep an eye on this one!",
    height=42, skin=(0.80, 0.80, 0.78), accent=(0.02, 0.02, 0.03), eye=(0.75, 0.2, 1.0), glow=(0.6, 0.3, 0.9),
    rim=(0.95, 0.95, 1.0), style="GHOST", hovers=True, head=(0, 0, 0.72), radius=0.22, chamber=(0.55, 0.20, 0.90),
    walk=18, idle=(2.0, 5.0), look=0.50, curiosity=0.95, notice=220, energy=0.35,
    parts=[
        part("SPHERE", "BODY", (0, 0, 0.55), (0.22, 0.28, 0.32)),                                    # torso
        part("CONE", "BODY", (-0.02, 0, 0.28), (0.20, 0.24, 0.34), rot=(170, 0, 0),
             motion="SWAY_YAW", amount=10, speed=0.5, pivot=(0.0296, 0, 0.167)),                     # wispy tail
        part("SPHERE", "BODY", (0, 0, 0.55), (0.24, 0.016, 0.326), "ACCENT", rot=(0, 30, 0), mirror=True),  # black stripes
        part("SPHERE", "BODY", (0, 0, 0.62), (0.205, 0.26, 0.016), "ACCENT"),                        # chest band
        part("CYLINDER", "BODY", (0.02, 0.18, 0.50), (0.055, 0.055, 0.28), rot=(0, 0, -25), mirror=True,
             motion="SWAY_PITCH", amount=15, speed=0.5, pivot=(0, -0.059, 0.127)),                   # long arms
        part("CONE", "BODY", (0.02, 0.245, 0.34), (0.03, 0.03, 0.08), "ACCENT", rot=(180, 0, 0), mirror=True,
             motion="SWAY_PITCH", amount=15, speed=0.5, pivot=(0, -0.124, 0.287)),                   # claws
        part("SPHERE", "HEAD", (0, 0, 0.10), (0.19, 0.21, 0.21)),                                    # head
        part("SPHERE", "HEAD", (0.075, 0, 0.11), (0.04, 0.12, 0.12), "ACCENT"),                      # dark eye socket
        part("SPHERE", "HEAD", (0.085, 0, 0.11), (0.03, 0.08, 0.08), "EYE"),                         # purple eye
        part("CUBE", "HEAD", (0.085, 0, 0.045), (0.01, 0.06, 0.012), "DARK"),                        # mouth
    ] + badge(0.108, 0.58, d=0.065),
))


# ---------------------------------------------------------------------------------------------
# Asset creation
# ---------------------------------------------------------------------------------------------

def _color(rgb, alpha=1.0):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], alpha)


def load_or_create(asset_name, cls):
    path = f"{FOLDER}/{asset_name}"
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    return AT.create_asset(asset_name, FOLDER, cls, factory)


def build_alien(a):
    da = load_or_create(a["asset"], unreal.AlienDataAsset)
    da.set_editor_property("alien_id", unreal.Name(a["id"]))
    da.set_editor_property("display_name", unreal.Text(a["name"]))
    da.set_editor_property("species", unreal.Text(a["species"]))
    da.set_editor_property("home_planet", unreal.Text(a["planet"]))
    da.set_editor_property("description", unreal.Text(a["desc"]))
    da.set_editor_property("body_shape", unreal.AlienBodyShape.CUSTOM)
    da.set_editor_property("skin_style", getattr(unreal.AlienSkinStyle, a.get("style", "ORGANIC")))
    da.set_editor_property("skin_color", _color(a["skin"]))
    da.set_editor_property("accent_color", _color(a["accent"]))
    da.set_editor_property("eye_color", _color(a["eye"]))
    da.set_editor_property("glow_color", _color(a["glow"]))
    rim = a.get("rim")
    da.set_editor_property("rim_color", _color(rim) if rim else unreal.LinearColor(0, 0, 0, 0))
    da.set_editor_property("eye_count", 0)
    da.set_editor_property("has_antennae", False)
    da.set_editor_property("hovers", a.get("hovers", False))
    da.set_editor_property("height", float(a["height"]))
    da.set_editor_property("custom_head_pivot", unreal.Vector(*a["head"]))
    da.set_editor_property("custom_radius", float(a["radius"]))
    da.set_editor_property("parts", a["parts"])
    da.set_editor_property("chamber_light_color", _color(a["chamber"]))
    da.set_editor_property("walk_speed", float(a["walk"]))
    da.set_editor_property("idle_duration", unreal.Vector2D(*a["idle"]))
    da.set_editor_property("look_around_chance", a["look"])
    da.set_editor_property("curiosity", a["curiosity"])
    da.set_editor_property("notice_player_distance", float(a["notice"]))
    da.set_editor_property("energy", a["energy"])
    EAL.save_loaded_asset(da)
    return da


def build_all():
    EAL.make_directory(FOLDER)
    assets = [build_alien(a) for a in ALIENS]
    collection = load_or_create(COLLECTION_NAME, unreal.AlienCollectionAsset)
    collection.set_editor_property("aliens", assets)
    EAL.save_loaded_asset(collection)
    return assets, collection


def use_classic_collection_in_museum():
    """Points the museum director at the classic collection and gives the panel an Omnitrix look."""
    collection = unreal.load_asset(f"{FOLDER}/{COLLECTION_NAME}")
    director_bp = unreal.load_asset("/Game/AlienMuseum/Blueprints/BP_MuseumDirector")
    unreal.get_default_object(director_bp.generated_class()).set_editor_property("collection", collection)
    panel_bp = unreal.load_asset("/Game/AlienMuseum/Blueprints/BP_AlienCollectionPanel")
    panel_cdo = unreal.get_default_object(panel_bp.generated_class())
    panel_cdo.set_editor_property("panel_title", unreal.Text("OMNITRIX ALIEN COLLECTION"))
    panel_cdo.set_editor_property("accent_color", unreal.LinearColor(0.25, 1.0, 0.3, 1.0))
    for bp in (director_bp, panel_bp):
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        EAL.save_loaded_asset(bp)


created, classic_collection = build_all()
use_classic_collection_in_museum()
print("Classic aliens:", [a.get_name() for a in created])
print("Parts per alien:", {a.get_name(): len(a.get_editor_property("parts")) for a in created})
