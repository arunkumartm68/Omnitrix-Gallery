# Alien Museum – developer guide

Mixed-reality alien museum for **Meta Quest 3S** (UE 5.7.4, Epic Native OpenXR + Meta XR plugin 1.205, MRUK).
The player sees their real room through passthrough, places tall glass display cases on the floor, on
furniture or floating in mid-air, resizes them with handles, and fills them with autonomous aliens chosen
from a holographic collection - imported 3D models (the downloaded Ben 10 aliens) or shape-built figures.

## Architecture

```
UAlienDataAsset (DA_Model_*, DA_Classic_*, DA_Alien_*)   identity, look, behaviour, habitat, signature moves
   └─ AAlienCharacter (BP_AlienCharacter)      body (UAlienAppearanceComponent), movement, containment
        ├─ UAlienActionComponent               signature moves + effects (head flames, speed trails)
        └─ AAlienAIController                  state machine: Idle / Wander / LookAround / ReactToPlayer /
             │                                 Performing / Held
             └─ CharacterMovementComponent     direct steering inside the chamber (no navmesh needed)

AAlienChamber (BP_AlienChamber)                   Shape: Box (tall display case, default) or Round (pod)
   ├─ Base, FloorGlow, Glass, TopCap, LightPanel, FramePillars (1 instanced draw call)
   ├─ ContainmentWalls (4 boxes / 8 round) + Ceiling   thick, block the alien and loose props, never the pointer
   ├─ UChamberHabitatComponent                     the occupant's home world (UChamberHabitatAsset)
   ├─ ObstacleRock / ObstacleCrystal               optional (off by default: big model aliens need the room)
   ├─ MovementBounds, SpawnPoint, SelectBox
   ├─ 4 resize handles (top edge = height, left/right = width, base front = depth) + SizeLabel
   ├─ HoverGlow (anti-gravity glow, only while floating in the air)
   ├─ InfoRoot (holographic info panel)
   ├─ InteriorLight (optional real light, off by default)
   └─ spatial anchor component (added at runtime by UMuseumPersistenceComponent)

AMuseumDirector (BP_MuseumDirector, one per level)
   ├─ UMuseumSceneComponent        passthrough, scene permission, MRUK room, surface raycasts, occluders
   └─ UMuseumPersistenceComponent  save game + Meta spatial anchors
AMuseumPawn (BP_MuseumPawn)       camera, controllers, 2 × UMuseumHandInteractor (controller / hand / desktop)
AAlienCollectionPanel             3D holographic collection UI (cards, buttons)
```

Start-up flow: passthrough → spatial-data permission → MRUK loads the room (launches Space Setup if
no room exists) → occluders built → saved chambers restored from anchors (or a starter chamber on first
run) → Alien Collection panel appears in front of the player.

## Files

| Area | Files |
|---|---|
| Module | `Source/Ben10/Ben10.Build.cs`, `Source/Ben10.Target.cs`, `Source/Ben10Editor.Target.cs` |
| Core | `Source/Ben10/Core/` – `MuseumDirector`, `MuseumGameMode`, `MuseumAssets`, `MuseumInteractable`, `MuseumTypes` |
| Data | `Source/Ben10/Data/` – `AlienDataAsset`, `AlienCollectionAsset`, `ChamberHabitatAsset` |
| Aliens | `Source/Ben10/Aliens/` – `AlienCharacter`, `AlienAIController`, `AlienAppearanceComponent`, `AlienActionComponent` |
| Chamber | `Source/Ben10/Chamber/` – `AlienChamber`, `ChamberHabitatComponent` |
| Mixed reality | `Source/Ben10/MR/` – `MuseumSceneComponent`, `MuseumPersistenceComponent`, `MuseumSaveGame` |
| Interaction | `Source/Ben10/Interaction/` – `MuseumPawn`, `MuseumHandInteractor` |
| UI | `Source/Ben10/UI/AlienCollectionPanel` |
| Content | `Content/AlienMuseum/` – `Maps/L_AlienMuseum`, `Blueprints/BP_*`, `Data/DA_*`, `Data/Classic/DA_Classic_*`, `Data/Models/DA_Model_*`, `Data/Habitats/HAB_*`, `Models/<Id>/` (imported meshes, materials, textures), `Materials/M_*` |
| Tools | `Scripts/create_classic_aliens.py` – shape-built classic aliens; `Scripts/blender_inspect_models.py`, `Scripts/blender_convert_models.py`, `Scripts/blender_models_common.py` – downloaded models → Unreal-ready `.glb`; `Scripts/blender_pose_fourarms.py` – Four Arms' flex pose; `Scripts/import_downloaded_models.py` – `.glb` → meshes + `DA_Model_*` + collection; `Scripts/create_habitats_and_moves.py` – habitats, signature moves, pose / ball meshes; `Scripts/create_museum_materials.py` – glass, habitat and effect materials |
| Source art | `SourceArt/` (not in git): `Downloaded/` (unzipped downloads), `Converted/` (`.glb`, previews, `manifest.json`) |

## Build

* **Editor build:** close the editor, then open the generated Visual Studio solution and build
  `Development Editor | Win64`, or run:
  `"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" Ben10Editor Win64 Development -Project="C:\Games\Ben10\Ben10.uproject" -WaitMutex`
* Generate the solution with right-click `Ben10.uproject` → *Generate Visual Studio project files*.
* Header / UPROPERTY changes need an editor restart; body-only changes can use Live Coding (Ctrl+Alt+F11).

## Test in the editor (no headset)

Press **Play** in `L_AlienMuseum`. Without a headset the pawn runs in *desktop test mode* and the
level's editor-only furniture (tag `MuseumEditorRoom`) stands in for your room.

| Input | Action |
|---|---|
| Mouse | look |
| W A S D | move |
| Left mouse | select UI / place / click a chamber (info panel) / hold on a chamber to carry it |
| Right mouse | grab / carry a chamber |
| Z / C | rotate carried chamber |
| Q / E | shrink / grow carried chamber; while placing a new chamber in the air: closer / farther |
| Left mouse on a resize handle | drag to change the case's height / width / depth (look to move it) |
| Tab | open / close the Alien Collection |

## Deploy to Quest 3S

1. Android support for UE 5.7 (Epic Games Launcher → UE 5.7 → *Options* → **Android**), the Android SDK
   and NDK r27c (27.2.12479018) must be installed.
2. Headset: enable Developer Mode (Meta Horizon app), connect USB-C, accept *Allow USB debugging*.
   `adb devices` must list the headset.
3. Close the editor, then package from a terminal (the system `JAVA_HOME` is JDK 8, so point it at
   Android Studio's JDK 17 for this command only):
   ```
   $env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
   $env:NDKROOT = "$env:LOCALAPPDATA\Android\Sdk\ndk\27.2.12479018"
   $env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
   & "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="C:\Games\Ben10\Ben10.uproject" -noP4 -platform=Android -cookflavor=ASTC -clientconfig=Development -build -nocompileeditor -cook -stage -pak -package -archive -archivedirectory="C:\Games\Ben10\Saved\Packaged"
   adb install -r -g C:\Games\Ben10\Saved\Packaged\Android_ASTC\Ben10-arm64.apk
   ```
4. First launch: allow the **spatial data** permission. If the room has not been scanned, Space Setup opens.

Controls on the headset: point + **trigger / pinch** to select, **grip** or pinch on a chamber to carry it,
both hands to scale/rotate, thumbstick while carrying to rotate/resize, **Y / B / Menu** (or left-hand
pinch-and-hold 1 s) to open the collection.

**Floating chambers (no gravity).** A carried chamber stays exactly where it is let go, also in mid-air
(a glowing disc appears under it). It is only set down when released within 5 cm above a real surface
or partly inside one, and it never goes below the floor. When placing a new chamber, pointing at a floor or
table puts it there; pointing into open space shows the ghost in the air 1.2 m along the ray (thumbstick
forward/back changes the distance). Floating chambers may be stacked above each other. Turn the whole
feature off with `BP_MuseumDirector` → *Chambers Float* (chambers then drop onto the surface below).
A click on a chamber (info panel) does not move it and keeps its spatial anchor; only a real move
re-anchors it.

**Tall cases and manual resizing.** A new case is 80 cm wide, 64 cm deep and 105 cm tall inside
(about 121 cm overall). Point at a case and four knobs with arrows appear: on the top edge facing you
(**height**), on the left and right sides (**width**) and at the front of the base (**depth**). The knobs
stay for a moment after the pointer leaves the case, so the ray can cross the gap onto one. Pinch / pull
the trigger on a knob and drag: width and depth grow on both sides, height grows upwards, and a label
shows the size in centimetres. With hand tracking you can also pinch the knob directly. The alien keeps its size (the case
just gets roomier) and a case can never be made smaller than its alien. Resizing keeps the chamber's
spatial anchor and the size is saved. Uniform scaling of the whole exhibit (alien included) still works
with the thumbstick or two hands.

**Lighting.** The level's key light follows the viewer (it shines from over your shoulder), so the alien
you look at is always lit from the front. Turn it off with `BP_MuseumDirector` → *Key Light Follows Viewer*.

**Glass.** `M_MuseumGlass` is an unlit translucent material (cheap on Quest): almost clear face-on,
brighter and more reflective towards the edges (Fresnel), a soft sky reflection and a small moving
highlight, tinted with the occupant's chamber colour.

## Habitats (each alien's home world)

When an alien moves in, its case gets the alien's home world (`UAlienDataAsset::Habitat` →
`Data/Habitats/HAB_*`, built by `UChamberHabitatComponent`):

* **Ground** – sand, rock, soil, grass, organic, crystal, tech, **lava** (glowing flowing cracks) or a
  **water** pool (Ripjaws' ocean 40 cm deep, Stinkfly's swamp) the alien walks through.
* **Fixed props** – rocks, pillars, crystals, tombstones, seaweed, reeds, plants (one instanced mesh per
  kind, plants sway). Some block the alien like real obstacles.
* **Loose props with real physics** (Chaos) – boulders, traffic cones, skulls, bowling pins, fruit, seed
  pods, pebbles… They fall, roll, bounce off the glass and each other and sink slowly in water. The alien
  shoves them aside when it walks into them, and its moves throw them (stomps, rolls, screams, bursts).
  Upchuck eats them (they grow back after a while). While a case is carried they ride along frozen.
* **Ambient effect** – embers, bubbles, mist, sparkles, spores / dust or tech pulses.

Props are placed at random but the same way every time for a case, never on top of the alien, and are
rebuilt to fit when the case is resized. The 15 worlds: Khoros (Four Arms), Kinet race track (XLR8),
Petropia (Diamondhead), Galvan B (Upgrade), Anur Phaetos graveyard (Ghostfreak), Piscciss ocean floor
(Ripjaws), Vulpin (Wildmutt), Galvan Prime lab (Grey Matter), Arburia (Cannonbolt), Flors Verdance
(Wildvine), Peptor XI (Upchuck), clone playground (Ditto), Sonorosia (Echo Echo), Pyros lava field
(Heatblast), Lepidopterra swamp (Stinkfly).

Physics rules that keep it stable: loose props *overlap* the alien (its movement component's repulsion
force shoves them away) instead of blocking it, so a prop can never wedge the alien into the glass; the
case's invisible walls are thick; props are capped at 2.5 m/s and ease apart instead of popping.

## Signature moves

`UAlienActionComponent` plays each alien's moves (`SignatureActions`, the first is its show-off move for
visitors). The AI performs one now and then (`ActionChance` when an idle period ends) and shows off when
you walk up to its glass. Moves follow the cartoon:

| Alien | Move |
|---|---|
| Cannonbolt | **Roll** – curls up into his armoured ball (the Wii model's ball form), rolls fast, bounces off the glass, bowls props over |
| XLR8 | **Dash** – super-speed zig-zag with blue after-images and speed streaks; also leaves after-images whenever he runs fast, and walks at 70 cm/s |
| Heatblast | **Flare** – his head is always on fire (flickering flames + embers); flames surge and a fireball hits the glass |
| Four Arms | **Flex** – turns to you, strikes a double-biceps pose with all four arms, pumps (power rings), then stomps a shockwave that makes the props jump; also **Pounce** |
| Diamondhead | **Crystal burst** – crystal spikes burst out of the ground around him |
| Ghostfreak | **Phase** – fades into mist, drifts unseen through everything, reappears |
| Echo Echo | **Scream** (sonic rings that push props away) and **Clone** |
| Ditto | **Clone** – two copies step out and merge back |
| Upchuck | **Spit** – eats a loose prop, swells, spits a bouncing energy ball that bursts |
| Wildmutt, Ripjaws | **Pounce** – sniffs, crouches and leaps across the case (Ripjaws splashes out of his pool) |
| Wildvine | **Vines** – vines lash out to the glass, then an exploding seed pod |
| Upgrade | **Melt** – melts into a liquid-metal puddle, slides away leaving glowing circuit lines, re-forms |
| Grey Matter | **Scurry** – tiny quick zig-zag dashes with hops |
| Stinkfly | **Fly** – takes off, circles the case banking into the turns, lands |

Effects are pooled glowing shapes and rings (`M_FXGlow`, `M_FXRing`, max 40 per alien) that stay inside
the glass. Models also lean into turns and when speeding up, and turn slightly towards what they look at.

Sources for the moves: [Cannonbolt (Ben 10 Wiki)](https://ben10.fandom.com/wiki/Cannonbolt_(Classic)),
[Upchuck (Ben 10 Wiki)](https://ben10.fandom.com/wiki/Upchuck),
[Ranking transformations from Ben 10 Classic](https://ben10.fandom.com/wiki/User_blog:Eye_Lad/Ranking_transformations_from_Ben_10_Classic),
[Game Rant: best Ben 10 aliens](https://gamerant.com/ben-10-best-aliens/),
[CBR: original aliens ranked](https://www.cbr.com/ben-10-original-aliens-ranked/),
[VS Battles: Ben 10 (Classic)](https://vsbattles.fandom.com/wiki/Ben_10_(Classic)).

## Tuning without code

* **Aliens** – `Data/DA_Alien_*` and `Data/Classic/DA_Classic_*`: colours, shape, eyes, height, walk speed,
  idle time, curiosity, energy, chamber colour.
  Add a new alien: duplicate a data asset, give it a unique `AlienId`, add it to a collection asset.
  Real alien art later: make a Blueprint child of `AAlienCharacter` with a skeletal mesh and set it as the
  data asset's `CharacterClass` (the placeholder body is skipped automatically).
* **Body parts** – set `BodyShape = Custom` and build the creature from the `Parts` list. Each part is a
  sphere / cylinder / cone / cube with an offset, rotation and size in *height units* (1 = the alien's
  height; X forward, Y right, Z up), attached to Feet, Body (bobs) or Head (looks around). Colour slots:
  Skin, Accent, Dark, Eye (glows + blinks), Glow, Custom. `Mirror` adds the other-side copy. Motions:
  WalkSwing (limbs), SwayRoll (wings), SwayYaw (tails), SwayPitch, Flicker (flames), Pulse, Spin, rotating
  around `PivotOffset` (the joint). `SkinStyle` Crystal / Ghost makes the skin see-through.
* **Classic aliens** – shape-built versions generated by `Scripts/create_classic_aliens.py`
  (`DA_AlienCollection_Classic`). The museum now uses the downloaded models (`DA_AlienCollection_Models`);
  switch collections with `BP_MuseumDirector` → Collection (`DA_AlienCollection` = original placeholders).
* **Model aliens** – `Data/Models/DA_Model_*`: `ModelMesh` (the imported static mesh), `ModelRotation`
  (fix-up if a model does not face +X), `ModelCredit` (shown on the info panel), `Height`, `Hovers`,
  behaviour and chamber colour. A model is scaled to `Height`, stood on its lowest point and centred.
* **Moves** – on any alien data asset: `Habitat`, `SignatureActions`, `ActionChance`, `ActionColor`,
  `bHeadFlames`, `bSpeedTrail`, `PoseMesh` (a second pose of the model shown during Flex) and `BallMesh`
  (the rolled-up form for Roll). `Scripts/create_habitats_and_moves.py` sets them all.
* **Habitats** – `Data/Habitats/HAB_*`: ground type, colours and depth; ambient effect, colour and count;
  a list of props (shape, colour, look Lit / Glow / Crystal, count, size range, stretch, placement
  Scatter / Edges / Corners / Back, tilt, and whether it has physics, blocks the alien or sways).

> **Fan content:** Ben 10 and its aliens belong to Cartoon Network / Warner Bros. Discovery, and the
> downloaded models belong to their Sketchfab authors. The classic figures and the imported models are
> for private use only. Do not publish or distribute the app with them (for example on the Meta Horizon
> Store) without permission - use original aliens for anything public.
* **Chamber** – `BP_AlienChamber`: shape (Box / Round), `Width`, `Depth`, `GlassHeight`, `BaseHeight`,
  `SizeRange` (resize limits), `ScaleRange` (uniform scale), colours incl. `HandleColor`, obstacles,
  real interior light.
* **Director** – `BP_MuseumDirector` in the level: collection, starter chamber, max chambers, chambers float,
  surface snap distance, restored-alien delay, key light follows viewer (elevation, side angle),
  scene options (occluder labels, debug room view, passthrough), persistence options.
* **Pawn** – `BP_MuseumPawn`: grab distance, click timing, rotate/scale speeds, mid-air placement distance
  and range, optional input assets.

## Downloaded models (how the Ben 10 aliens got in)

The 20 downloads (Sketchfab zips: FBX, OBJ, glTF, `.blend`, one `.rar`, plus a Wii game rip of
Cannonbolt with his ball form) go through these scripts, in this order:

1. **Blender 5.2** – `Scripts/blender_convert_models.py` (settings per download in its `MODELS` table):
   imports the file, re-links textures by name (Sketchfab renames them, e.g. `.psd` → `.tga.png`), turns
   it upright and facing forward, lowers T-pose arms on rigged models and bakes the pose into a plain
   mesh, removes fake glow / transparency / glass / clear-coat / flat metal that FBX/OBJ importers
   invent, rebuilds game-engine shaders (Ghostfreak) as standard materials, reduces meshes above
   60 000 triangles, makes every texture a power of two (≤ 2048 colour, ≤ 1024 other maps), scales it
   to its display height and writes `SourceArt/Converted/<Id>.glb` plus a front/side preview `.png`.
   ```
   & "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Scripts\blender_convert_models.py -- SourceArt\Downloaded SourceArt\Converted [Id ...]
   ```
2. **Unreal** (editor closed) – `Scripts/import_downloaded_models.py` imports every `.glb` fresh into
   `/Game/AlienMuseum/Models/<Id>/` (Interchange; no Nanite, no collision, turned to face +X), creates the
   `DA_Model_*` assets and `DA_AlienCollection_Models`, and makes it the museum's collection.
   ```
   & "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" C:\Games\Ben10\Ben10.uproject -run=pythonscript -script="C:/Games/Ben10/Scripts/import_downloaded_models.py" -unattended -nosplash -nullrhi
   ```
   Add model ids after the script path (inside the quotes) to import only those.
3. **Four Arms' flex pose** (Blender, optional) – `Scripts/blender_pose_fourarms.py` bends the T-pose
   model's four forearms around the elbows (double biceps + lower arms across the belly) and writes
   `SourceArt/Converted/FourArms_2_Flex.glb` in the same frame as the normal model.
4. **Habitats and moves** (Unreal, editor closed) – `Scripts/create_habitats_and_moves.py` creates the
   `HAB_*` habitats, imports the flex pose and Cannonbolt's ball (sharing the models' materials where they
   are the same model), and sets every alien's habitat, moves and extras. Run it after every import.

**Adding another download:** unzip it into `SourceArt/Downloaded/<name>/`, run
`Scripts/blender_inspect_models.py` to see its size, rig, textures and a preview, add a line to `MODELS`
(id, alien name, height, `rotate` if it lies down or faces sideways, `pose` for T-pose arms, `folder` /
`file` for a download with several models, `colors` for materials that are just flat colours), convert
it, add its id to `ORDER` in the import script (and to `EXTRA` if it is a new alien), run the import, add
the alien to `MOVES` in `create_habitats_and_moves.py` and run that too.

## Quest performance choices

Forward shading, multiview, 4× MSAA, dynamic foveation (level High), no Lumen / VSM / ray tracing /
distance fields / Substrate, ASTC-only textures, no dynamic shadows (fake contact shadows), unlit
emissive "lighting" inside chambers, pillars instanced, alien AI thinks at 5 Hz, alien animation only
when visible, max 8 chambers. Imported models: static meshes (no skeletal animation), ≤ 60 000 triangles
each (8 cases ≈ 480 000 at most), power-of-two textures ≤ 2048, no Nanite, no collision, no shadow casting;
one movable key light without shadows. Habitats: fixed props instanced (one draw call per kind), about
3–10 loose physics props per case (asleep when still), ambient effects as one instanced mesh, animated
only while the case is seen. Moves: pooled effects (≤ 40 per alien), 3 after-images, no particle systems.

## Known limitations

* **Depth-API occlusion is not available** with the launcher engine in Native OpenXR mode: in Meta XR 1.205
  `StartEnvironmentDepth` / `SetXROcclusionsMode` only work on Meta's UE fork (`WITH_OCULUS_BRANCH`).
  Occlusion therefore uses the MRUK room model (walls and furniture become Alpha-Holdout occluders).
  People and hands do not occlude aliens.
* UE 5.7.4 hard-codes `quest2|questpro|quest3` in the Android manifest when *Package for Meta Quest* is on;
  the Meta plugin appends Quest 3S, so the final manifest lists `quest3s` (with duplicates, harmless).
* Visual Studio 2026 uses MSVC 14.51; UE 5.7 is validated with 14.44. Builds work; installing the
  *MSVC v14.44 (17.14)* component is recommended.
* Features that can only be verified on the headset: passthrough, room loading, occluders, anchors,
  hand tracking and pinch. Everything else was tested in Play-In-Editor.
* The models are rigid static meshes: moves bend, squash, lean, hide or swap the whole body (Four Arms
  swaps to his flex pose, Cannonbolt to his ball), limbs are not animated. Only FourArms_2 (the T-pose
  download) has a flex pose; FourArms_1 flexes with the pump and stomp only.
* Heatblast has no downloaded model: the shape-built classic Heatblast is in the collection (his head
  flames also attach to a future Heatblast model automatically).
* In a small case a wide alien (e.g. Cannonbolt, 50 cm across) has little room to roll or leap - make
  the case bigger with the handles.
