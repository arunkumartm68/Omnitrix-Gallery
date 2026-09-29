"""
Alien Museum - builds the glass, habitat and effect materials (UE 5.7 Python, editor or commandlet).

    UnrealEditor-Cmd.exe C:/Games/Ben10/Ben10.uproject -run=pythonscript
        -script="C:/Games/Ben10/Scripts/create_museum_materials.py" -unattended -nosplash -nullrhi

All materials are unlit or simply lit and use only engine noise textures, so they stay cheap on Quest.
Parameter names match MuseumAssets::Params in the C++ code. Re-running rebuilds the graphs in place.

  M_MuseumGlass  clear display-case glass: Fresnel edges, sky reflection, a sliding highlight
  M_FXGlow       soft glowing translucent effect (flames, rings, trails, crystals, embers)
  M_EnvLit       lit habitat material with subtle noise variation (sand, rock, soil, props)
  M_EnvLava      glowing lava with slowly flowing cracks
  M_EnvWater     translucent shimmering water
  M_EnvMist      soft translucent mist puffs (edges fade out)
  M_FXRing       glowing ring on the engine Plane (shockwaves, sonic rings, splashes, impact rings)
  M_OmnitrixFace the Omnitrix's face, drawn from its UVs: the hourglass, or the dialled alien's silhouette,
                 a ring of time around it, and a flash (parameters set by AOmnitrixWatch)

Pass material names to rebuild only those, e.g. -script="...create_museum_materials.py M_OmnitrixFace".
"""
import math
import sys

import unreal

FOLDER = "/Game/AlienMuseum/Materials"
NOISE = "/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


class Graph:
    """Small helper that lays nodes out left to right and wires them."""

    def __init__(self, material):
        self.m = material
        self.x = -1400
        self.y = 0

    def node(self, cls, **props):
        expr = MEL.create_material_expression(self.m, cls, self.x, self.y)
        self.y += 140
        if self.y > 900:
            self.y = 0
            self.x += 260
        for key, value in props.items():
            expr.set_editor_property(key, value)
        return expr

    def scalar(self, name, value):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(value))

    def vector(self, name, rgb):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                         default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def const3(self, rgb):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def op(self, cls, a, b=None, a_out="", b_out="", a_in="A", b_in="B", **props):
        expr = self.node(cls, **props)
        MEL.connect_material_expressions(a, a_out, expr, a_in)
        if b is not None:
            MEL.connect_material_expressions(b, b_out, expr, b_in)
        return expr

    def mul(self, a, b, a_out="", b_out=""):
        return self.op(unreal.MaterialExpressionMultiply, a, b, a_out, b_out)

    def add(self, a, b, a_out="", b_out=""):
        return self.op(unreal.MaterialExpressionAdd, a, b, a_out, b_out)

    def lerp(self, a, b, alpha, alpha_out=""):
        expr = self.node(unreal.MaterialExpressionLinearInterpolate)
        MEL.connect_material_expressions(a, "", expr, "A")
        MEL.connect_material_expressions(b, "", expr, "B")
        MEL.connect_material_expressions(alpha, alpha_out, expr, "Alpha")
        return expr

    def single(self, cls, a, a_out="", **props):
        expr = self.node(cls, **props)
        MEL.connect_material_expressions(a, a_out, expr, "")
        return expr

    def fresnel(self, exponent, base):
        return self.node(unreal.MaterialExpressionFresnel, exponent=float(exponent), base_reflect_fraction=float(base))

    def noise(self, uv):
        tex = self.node(unreal.MaterialExpressionTextureSample, texture=unreal.load_asset(NOISE))
        MEL.connect_material_expressions(uv, "", tex, "UVs")
        return tex

    def panner(self, uv, speed_x, speed_y):
        pan = self.node(unreal.MaterialExpressionPanner, speed_x=float(speed_x), speed_y=float(speed_y))
        MEL.connect_material_expressions(uv, "", pan, "Coordinate")
        return pan

    def uv(self, tiling):
        return self.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=float(tiling), v_tiling=float(tiling))

    def out(self, expr, prop, expr_out=""):
        MEL.connect_material_property(expr, expr_out, prop)


def material(name, blend, shading, two_sided=False, instanced=False):
    path = f"{FOLDER}/{name}"
    m = unreal.load_asset(path) if EAL.does_asset_exist(path) else AT.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    # UE 5.7's delete_all removes from the list it iterates, so each pass only deletes about half.
    for _ in range(32):
        if MEL.get_num_material_expressions(m) == 0:
            break
        MEL.delete_all_material_expressions(m)
    m.set_editor_property("blend_mode", blend)
    m.set_editor_property("shading_model", shading)
    m.set_editor_property("two_sided", two_sided)
    if instanced:
        m.set_editor_property("used_with_instanced_static_meshes", True)
    return m, Graph(m)


def finish(m):
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    print("MATERIAL", m.get_name(), "expressions", MEL.get_num_material_expressions(m))


def build_glass():
    m, g = material("M_MuseumGlass", unreal.BlendMode.BLEND_TRANSLUCENT, unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    tint = g.vector("Tint", (0.3, 0.85, 1.0))
    edge_color = g.vector("EdgeColor", (0.3, 0.85, 1.0))
    edge_glow = g.scalar("EdgeGlow", 1.2)
    base_opacity = g.scalar("BaseOpacity", 0.05)
    edge_opacity = g.scalar("EdgeOpacity", 0.45)
    reflect = g.scalar("ReflectStrength", 1.1)
    highlight_strength = g.scalar("HighlightStrength", 2.5)
    sharpness = g.scalar("HighlightSharpness", 120.0)

    fres = g.fresnel(4.0, 0.04)
    rvec = g.node(unreal.MaterialExpressionReflectionVectorWS)
    up = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    MEL.connect_material_expressions(rvec, "", up, "")
    up01 =g.single(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionAdd, g.mul(up, g.node(unreal.MaterialExpressionConstant, r=0.5)), g.node(unreal.MaterialExpressionConstant, r=0.5)))
    sky = g.lerp(g.const3((0.05, 0.06, 0.07)), g.const3((0.62, 0.72, 0.82)), up01)
    reflection = g.mul(g.mul(sky, fres), reflect)

    light_dir = g.single(unreal.MaterialExpressionNormalize, g.const3((0.45, -0.35, 0.82)))
    dot = g.op(unreal.MaterialExpressionDotProduct, rvec, light_dir)
    glint = g.mul(g.op(unreal.MaterialExpressionPower, g.single(unreal.MaterialExpressionSaturate, dot), sharpness, a_in="Base", b_in="Exp"), highlight_strength)

    edge = g.mul(g.mul(edge_color, fres), g.mul(edge_glow, g.node(unreal.MaterialExpressionConstant, r=0.3)))
    faint_tint = g.mul(tint, g.node(unreal.MaterialExpressionConstant, r=0.035))
    emissive = g.add(g.add(reflection, glint), g.add(edge, faint_tint))
    g.out(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = g.single(unreal.MaterialExpressionSaturate,
                       g.add(g.add(base_opacity, g.mul(fres, edge_opacity)), g.mul(glint, g.node(unreal.MaterialExpressionConstant, r=0.5))))
    g.out(opacity, unreal.MaterialProperty.MP_OPACITY)
    finish(m)


def build_fx_glow():
    m, g = material("M_FXGlow", unreal.BlendMode.BLEND_TRANSLUCENT, unreal.MaterialShadingModel.MSM_UNLIT, instanced=True)
    color = g.vector("Color", (1.0, 0.5, 0.1))
    intensity = g.scalar("Intensity", 2.0)
    opacity = g.scalar("Opacity", 0.9)
    softness = g.scalar("Softness", 1.5)
    fres = g.node(unreal.MaterialExpressionFresnel, base_reflect_fraction=0.0)
    MEL.connect_material_expressions(softness, "", fres, "ExponentIn")
    core = g.single(unreal.MaterialExpressionOneMinus, fres)
    g.out(g.mul(g.mul(color, intensity), g.add(core, g.node(unreal.MaterialExpressionConstant, r=0.35))), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.single(unreal.MaterialExpressionSaturate, g.mul(core, opacity)), unreal.MaterialProperty.MP_OPACITY)
    finish(m)


def build_env_lit():
    m, g = material("M_EnvLit", unreal.BlendMode.BLEND_OPAQUE, unreal.MaterialShadingModel.MSM_DEFAULT_LIT, instanced=True)
    color = g.vector("Color", (0.6, 0.5, 0.35))
    rough = g.scalar("Roughness", 0.85)
    illum = g.scalar("SelfIllum", 0.12)
    tiling = g.scalar("Tiling", 2.0)
    uv = g.mul(g.node(unreal.MaterialExpressionTextureCoordinate), tiling)
    n = g.noise(uv)
    variation = g.lerp(g.node(unreal.MaterialExpressionConstant, r=0.72), g.node(unreal.MaterialExpressionConstant, r=1.12), n, alpha_out="R")
    base = g.mul(color, variation)
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mul(base, illum), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(m)


def build_env_lava():
    m, g = material("M_EnvLava", unreal.BlendMode.BLEND_OPAQUE, unreal.MaterialShadingModel.MSM_UNLIT, instanced=True)
    rock = g.vector("Color", (0.05, 0.025, 0.02))
    lava = g.vector("GlowColor", (1.0, 0.35, 0.05))
    intensity = g.scalar("Intensity", 2.2)
    sharp = g.scalar("CrackSharpness", 8.0)
    tiling = g.scalar("Tiling", 1.0)   # the code scales it with the case, so cracks keep their size
    uv = g.mul(g.node(unreal.MaterialExpressionTextureCoordinate), tiling)
    # Two slowly drifting, strongly magnified noise layers: big crust plates with sparse glowing veins.
    n1 = g.noise(g.panner(g.mul(uv, g.node(unreal.MaterialExpressionConstant, r=0.35)), 0.004, 0.0015))
    n2 = g.noise(g.panner(g.mul(uv, g.node(unreal.MaterialExpressionConstant, r=0.6)), -0.0025, 0.003))
    mix = g.mul(n1, n2, a_out="R", b_out="R")
    cracks = g.op(unreal.MaterialExpressionPower, g.mul(mix, g.node(unreal.MaterialExpressionConstant, r=2.4)), sharp, a_in="Base", b_in="Exp")
    crust = g.mul(rock, g.add(g.node(unreal.MaterialExpressionConstant, r=0.6), g.mul(n1, g.node(unreal.MaterialExpressionConstant, r=0.8), a_out="R")))
    g.out(g.add(crust, g.mul(g.mul(lava, intensity), cracks)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(m)


def build_env_water():
    m, g = material("M_EnvWater", unreal.BlendMode.BLEND_TRANSLUCENT, unreal.MaterialShadingModel.MSM_UNLIT)
    color = g.vector("Color", (0.08, 0.4, 0.65))
    opacity = g.scalar("Opacity", 0.55)
    shimmer_strength = g.scalar("Shimmer", 0.35)
    n = g.noise(g.panner(g.uv(3.0), 0.02, 0.013))
    n2 = g.noise(g.panner(g.uv(4.3), -0.017, 0.01))
    shimmer = g.mul(g.mul(n, n2, a_out="R", b_out="R"), shimmer_strength)
    fres = g.fresnel(3.0, 0.05)
    emissive = g.add(g.mul(color, g.add(shimmer, g.node(unreal.MaterialExpressionConstant, r=0.9))), g.mul(fres, g.const3((0.35, 0.5, 0.6))))
    g.out(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.single(unreal.MaterialExpressionSaturate, g.add(opacity, g.mul(fres, g.node(unreal.MaterialExpressionConstant, r=0.3)))), unreal.MaterialProperty.MP_OPACITY)
    finish(m)


def build_env_mist():
    m, g = material("M_EnvMist", unreal.BlendMode.BLEND_TRANSLUCENT, unreal.MaterialShadingModel.MSM_UNLIT, instanced=True)
    color = g.vector("Color", (0.6, 0.55, 0.75))
    opacity = g.scalar("Opacity", 0.35)
    fres = g.fresnel(1.2, 0.0)
    core = g.single(unreal.MaterialExpressionOneMinus, fres)
    soft = g.mul(core, core)
    g.out(color, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.mul(soft, opacity), unreal.MaterialProperty.MP_OPACITY)
    finish(m)


def build_fx_ring():
    m, g = material("M_FXRing", unreal.BlendMode.BLEND_TRANSLUCENT, unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    color = g.vector("Color", (0.6, 0.9, 1.0))
    intensity = g.scalar("Intensity", 2.0)
    opacity = g.scalar("Opacity", 0.9)
    thickness = g.scalar("Thickness", 0.12)
    fill = g.scalar("Fill", 0.1)
    center = g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
    dist = g.op(unreal.MaterialExpressionDistance, g.node(unreal.MaterialExpressionTextureCoordinate), center)
    radius = g.mul(dist, g.node(unreal.MaterialExpressionConstant, r=2.0))  # 0 at the centre, 1 at the plane's edge
    offset = g.op(unreal.MaterialExpressionSubtract, radius, g.node(unreal.MaterialExpressionConstant, r=0.8))
    band = g.op(unreal.MaterialExpressionDivide, g.single(unreal.MaterialExpressionAbs, offset), thickness)
    ring = g.single(unreal.MaterialExpressionSaturate, g.single(unreal.MaterialExpressionOneMinus, band))
    inside = g.single(unreal.MaterialExpressionSaturate, g.single(unreal.MaterialExpressionOneMinus, radius))
    mask = g.single(unreal.MaterialExpressionSaturate, g.add(g.mul(ring, ring), g.mul(inside, fill)))
    g.out(g.mul(g.mul(color, intensity), mask), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.mul(mask, opacity), unreal.MaterialProperty.MP_OPACITY)
    finish(m)


def build_omnitrix_face():
    """The face disc's UVs span 0..1 with the centre at 0.5 (blender_convert_omnitrix.py): U runs along the forearm,
    V across it towards the side button - up, as the wearer looks at the watch."""
    m, g = material("M_OmnitrixFace", unreal.BlendMode.BLEND_OPAQUE, unreal.MaterialShadingModel.MSM_UNLIT)
    color = g.vector("Color", (0.25, 1.0, 0.2))
    brightness = g.scalar("Brightness", 1.0)
    mode = g.scalar("Mode", 0.0)                 # 0 the hourglass, 1 the silhouette
    across = g.scalar("HourglassAcross", 1.0)    # 1: its triangles point across the arm, 0: along it
    flash = g.scalar("Flash", 0.0)
    flash_color = g.vector("FlashColor", (1.0, 1.0, 1.0))
    ring = g.scalar("Ring", 0.0)                 # share of the time ring lit
    ring_color = g.vector("RingColor", (0.25, 1.0, 0.2))

    def const(value):
        return g.node(unreal.MaterialExpressionConstant, r=float(value))

    def sub(a, b):
        return g.op(unreal.MaterialExpressionSubtract, a, b)

    def sharp(expr, gain):  # a crisp 0..1 edge
        return g.single(unreal.MaterialExpressionSaturate, g.mul(expr, const(gain)))

    def channel(expr, index):  # one component (0 = R/U, 1 = G/V)
        mask = g.node(unreal.MaterialExpressionComponentMask, r=index == 0, g=index == 1)
        MEL.connect_material_expressions(expr, "", mask, "")
        return mask

    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    centre = g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
    p = g.mul(sub(uv, centre), const(2.0))                                     # -1..1 across the disc
    r = g.mul(g.op(unreal.MaterialExpressionDistance, uv, centre), const(2.0))  # 0 centre .. 1 rim
    pu, pv = channel(p, 0), channel(p, 1)
    au, av = g.single(unreal.MaterialExpressionAbs, pu), g.single(unreal.MaterialExpressionAbs, pv)

    disc = sharp(sub(const(0.97), r), 40.0)
    inner = sharp(sub(const(0.8), r), 40.0)
    # The hourglass: two triangles meeting at the centre, inside the inner circle.
    hourglass = g.mul(g.lerp(sharp(sub(g.mul(au, const(0.92)), av), 25.0), sharp(sub(g.mul(av, const(0.92)), au), 25.0), across), inner)
    # The silhouette: black on green. Its image stands upright; the face's V runs up as you look, U the other way.
    flipped = sub(g.node(unreal.MaterialExpressionConstant2Vector, r=1.0, g=1.0), uv)
    texture = g.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="Silhouette",
                     texture=unreal.load_asset("/Engine/EngineResources/Black"))
    MEL.connect_material_expressions(flipped, "", texture, "UVs")
    silhouette = g.mul(g.single(unreal.MaterialExpressionOneMinus, channel(texture, 0)), sharp(sub(const(0.86), r), 40.0))
    lit = g.mul(g.mul(color, brightness), g.lerp(hourglass, silhouette, mode))
    # The ring of time around the edge, clockwise from the top.
    angle = g.node(unreal.MaterialExpressionArctangent2)
    MEL.connect_material_expressions(pu, "", angle, "Y")
    MEL.connect_material_expressions(pv, "", angle, "X")
    turn = g.add(g.mul(angle, const(1.0 / (2.0 * math.pi))), const(0.5))
    band = g.mul(sharp(sub(r, const(0.85)), 40.0), sharp(sub(const(0.94), r), 40.0))
    ring_lit = g.mul(g.mul(ring_color, const(1.4)), g.mul(band, sharp(sub(ring, turn), 200.0)))
    glow = g.add(g.add(lit, ring_lit), g.add(g.mul(g.mul(flash_color, flash), const(3.0)), const(0.015)))
    g.out(g.mul(glow, disc), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(m)


BUILDS = {
    "M_MuseumGlass": build_glass,
    "M_FXGlow": build_fx_glow,
    "M_EnvLit": build_env_lit,
    "M_EnvLava": build_env_lava,
    "M_EnvWater": build_env_water,
    "M_EnvMist": build_env_mist,
    "M_FXRing": build_fx_ring,
    "M_OmnitrixFace": build_omnitrix_face,
}
WANTED = [name for name in sys.argv[1:] if name in BUILDS] or list(BUILDS)
for name in WANTED:
    BUILDS[name]()

# The existing emissive material is now also used on instanced habitat props.
emissive = unreal.load_asset(f"{FOLDER}/M_MuseumEmissive")
if len(WANTED) == len(BUILDS) and emissive and not emissive.get_editor_property("used_with_instanced_static_meshes"):
    emissive.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.recompile_material(emissive)
    EAL.save_loaded_asset(emissive)
    print("MATERIAL M_MuseumEmissive now usable on instanced meshes")
print("MATERIALS DONE")
