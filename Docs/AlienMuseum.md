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
        ├─ UAlienSoundComponent                its voice, footsteps, move sounds (through UMuseumAudio)
        └─ AAlienAIController                  state machine: Idle / Wander / LookAround / ReactToPlayer /
             │                                 Performing / Held; answers taps on its glass
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
   ├─ ambience sound (its occupant's home world, or a soft hum; heard up close)
   ├─ glass effects (pooled: ripple rings where it is tapped or hit, marks such as a smear or breath)
   └─ spatial anchor component (added at runtime by UMuseumPersistenceComponent)

AMuseumDirector (BP_MuseumDirector, one per level)
   ├─ UMuseumSceneComponent        passthrough, scene permission, MRUK room, surface raycasts, occluders
   └─ UMuseumPersistenceComponent  save game + Meta spatial anchors
AMuseumPawn (BP_MuseumPawn)       camera, controllers, 2 × UMuseumHandInteractor (controller / hand / desktop),
                                  knocks on the glass (fingertip or controller tip)
AAlienCollectionPanel             3D holographic collection UI (cards, buttons)
UMuseumAudio (world subsystem)    plays every sound in 3D (Resonance Audio), glass muffling, who may call out
```

Start-up flow: passthrough → spatial-data permission → MRUK loads the room (launches Space Setup if
no room exists) → occluders built → saved chambers restored from anchors (or a starter chamber on first
run) → Alien Collection panel appears in front of the player.

## Files

| Area | Files |
|---|---|
| Module | `Source/Ben10/Ben10.Build.cs`, `Source/Ben10.Target.cs`, `Source/Ben10Editor.Target.cs` |
| Early XR module | `Source/Ben10XR/` – `Ben10XR` (loading phase *PostConfigInit*), `MuseumOpenXRLayer` (keeps OpenXR frame synthesis off, see *Deploy to Quest 3S*) |
| Core | `Source/Ben10/Core/` – `MuseumDirector`, `MuseumGameMode`, `MuseumAssets`, `MuseumAudio`, `MuseumInteractable`, `MuseumTypes` |
| Data | `Source/Ben10/Data/` – `AlienDataAsset`, `AlienCollectionAsset`, `ChamberHabitatAsset`, `MuseumSoundLibrary` |
| Aliens | `Source/Ben10/Aliens/` – `AlienCharacter`, `AlienAIController`, `AlienAppearanceComponent`, `AlienActionComponent`, `AlienSoundComponent` |
| Chamber | `Source/Ben10/Chamber/` – `AlienChamber`, `ChamberHabitatComponent` |
| Mixed reality | `Source/Ben10/MR/` – `MuseumSceneComponent`, `MuseumPersistenceComponent`, `MuseumSaveGame` |
| Interaction | `Source/Ben10/Interaction/` – `MuseumPawn`, `MuseumHandInteractor` |
| UI | `Source/Ben10/UI/AlienCollectionPanel` |
| Content | `Content/AlienMuseum/` – `Maps/L_AlienMuseum`, `Blueprints/BP_*`, `Data/DA_*`, `Data/Classic/DA_Classic_*`, `Data/Models/DA_Model_*`, `Data/Habitats/HAB_*`, `Models/<Id>/` (imported meshes, materials, textures), `Materials/M_*` |
| Tools | `Scripts/create_classic_aliens.py` – shape-built classic aliens; `Scripts/blender_inspect_models.py`, `Scripts/blender_convert_models.py`, `Scripts/blender_models_common.py` – downloaded models → Unreal-ready `.glb`; `Scripts/blender_pose_models.py` – re-poses rig-less models (Four Arms' flex, relaxed Four Arms and Wildvine); `Scripts/import_downloaded_models.py` – `.glb` → meshes + `DA_Model_*` + collection; `Scripts/blender_convert_omnitrix.py` + `Scripts/import_omnitrix.py` – the classic Omnitrix watch (band / core / face, `Models/Omnitrix/`); `Scripts/create_habitats_and_moves.py` – habitats, signature moves, pose / ball meshes; `Scripts/create_museum_materials.py` – glass, habitat and effect materials; `Scripts/create_input_assets.py` – the player's input actions and mapping context (`/Game/AlienMuseum/Input`); `Scripts/make_museum_sounds.py` (system Python + numpy/scipy) – synthesises every sound; `Scripts/import_museum_sounds.py` – imports them (`/Game/AlienMuseum/Audio`), the ranges' attenuation, `DA_MuseumSounds` and each alien's voice |
| Source art | `SourceArt/` (not in git): `Downloaded/` (unzipped downloads), `Converted/` (`.glb`, previews, `manifest.json`, `poses.json`), `Sounds/` (the synthesised `.wav` + `sounds.json`) |

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
| Right mouse on an alien (gold ring at its feet) | take it out of its case and hold it; Z / C spin it, Q / E zoom out / in; let go over its case = back in, anywhere else = it floats home |
| Hold left mouse on an alien | the same (a quick click shows its case's info panel) |
| Left mouse on a case's red X, twice / Delete twice | remove the chamber (the first press asks "REMOVE?") |
| Tab | open / close the Alien Collection |

Tapping the glass needs a hand or a controller. To try it at the desk, run this in the Output Log's
Python console while playing (it knocks on the front of the first case; 0.3 = a tap, 0.9 = a hard knock):

```python
import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
c = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AlienChamber)[0]
p = c.get_actor_transform().transform_location(unreal.Vector(c.get_inner_size().x / 2, 0, c.get_editor_property('base_height') + 45))
c.tap_glass(p, 0.3)
```

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

Controls on the headset: point + **trigger / pinch** to select, **grip** (hands: a **fist**) or trigger /
pinch on a chamber to carry it, both hands to scale/rotate, thumbstick while carrying to rotate/resize,
**X / Y / B / Menu** (or left-hand pinch-and-hold 1 s) to open the collection. With hand tracking a
closed fist grabs (cases and aliens) and a pinch points and selects.

**Controller input on Quest.** The OpenXR runtime only delivers the Touch controllers' triggers, grips,
sticks and buttons to input actions whose mapping context is listed in `Config/DefaultInput.ini` →
Default Mapping Contexts *and* already loaded when the XR session starts; a mapping context made at run
time never receives them (only hand tracking, which the game reads itself, would work). So the actions
are assets in `/Game/AlienMuseum/Input` (`IMC_Museum` + `IA_Museum_*`, made by
`Scripts/create_input_assets.py`), `IMC_Museum` is registered there, and `AMuseumPawn` loads them in its
constructor. To change a key, edit `MAPPINGS` in the script and re-run it.

**OpenXR frame synthesis is kept off (crash fix).** The Quest runtime offers `XR_FB_space_warp` and
`XR_EXT_frame_synthesis`. UE 5.7 enables one of them whenever it is offered, even with frame synthesis
switched off, and then keeps two motion-vector swapchains. Once a skinned mesh is drawn (the rigged
Wildmutt and Ghostfreak), their image indices drift apart and OpenXRHMD's
`check(MotionVectorIndex == MotionVectorDepthIndex)` closes the app. That happened when paging the
collection, placing Wildmutt or starting with his case saved. The museum doesn't use frame synthesis, so
`FMuseumOpenXRLayer` (an `IOpenXRExtensionPlugin` API layer) removes both extensions from the runtime's
extension list before the engine sees it.

The engine creates its OpenXR instance before the renderer starts, well before the game module loads. So
the layer lives in its own module, `Ben10XR`, with loading phase **PostConfigInit**. A layer registered
from the `Ben10` module comes too late and is silently ignored.

A working build logs:
`LogHMD: IOpenXRExtensionPlugin API layer enabled: AlienMuseum frame-synthesis filter` and
`LogMuseumXR: OpenXR: frame-synthesis extensions hidden from the engine`.

If the extensions are ever enabled anyway, `UAlienAppearanceComponent` warns and shows rigged aliens as
their static models, so they stand still instead of crashing.

**Device logs.** The game log is `/sdcard/Android/data/com.alienmuseum.ben10/files/UnrealGame/Ben10/Ben10/Saved/Logs/Ben10.log`
(older runs: `Ben10-backup-*.log`). Pull it from PowerShell, because Git Bash rewrites the path:
`adb pull <path> C:\Games\Ben10\Saved\DeviceLogs\`. To keep a headset that is off your head running for a test:
`adb shell am broadcast -a com.oculus.vrpowermanager.prox_close`. Undo it afterwards with
`... automation_disable`.

**Holding an alien (like a pet).** Point at an alien (a gold ring appears at its feet and the laser turns
gold) and squeeze the **grip** - or reach into the case and grab it, or make a **fist** at it with hand
tracking, or hold the trigger / pinch on it. It comes out of its case into your hand, looks at you and
keeps breathing. Turn your hand to look at it from every side; the **thumbstick** spins it (left/right)
and zooms it (up/down, 0.5x - 2.5x); with **both hands** on it, spread them to zoom. Let go while it is
over or inside its case and it drops straight back in; let go anywhere else and it floats home in an arc,
lands and does a happy hop. The AI pauses while it is out, and a case being carried or resized can't be
reached into. Tuning: `BP_MuseumPawn` → Near Alien Distance, Examine Spin Speed, Examine Zoom Speed /
Range.

**Removing a chamber.** Point at a case: next to the height knob a red **X** appears. Press it once and
it asks "REMOVE? press again"; press it again within 3 s and the case, its habitat and its alien are
removed (and forgotten by the saved museum). With hand tracking you can also pinch the X directly.

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
rebuilt to fit when the case is resized. The 16 worlds: Khoros (Four Arms), Kinet race track (XLR8),
Petropia (Diamondhead), Galvan B (Upgrade), Anur Phaetos graveyard (Ghostfreak), Piscciss ocean floor
(Ripjaws), Vulpin (Wildmutt), Galvan Prime lab (Grey Matter), Arburia (Cannonbolt), Flors Verdance
(Wildvine), Peptor XI (Upchuck), clone playground (Ditto), Sonorosia (Echo Echo), Pyros lava field
(Heatblast), Lepidopterra swamp (Stinkfly), Luna Lobo moonscape (Benwolf: jagged moon rocks, dead
trees, bones, a moon-glow stone and mist).

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
| Heatblast | **Flare** – his head is always on fire (flickering flames + embers); flames surge and a fireball hits the glass (classic collection only - he is no longer in the museum) |
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
| Stinkfly | **Fly** – takes off, circles the case banking into the turns, lands. His wings are separate meshes that buzz while he hovers and beat in big fast strokes when he flies, moves or is held |
| Benwolf | **Howl** – turns to you, winds up, his muzzle splits open into four and sonic rings pour out at the glass, pushing the props away (his model's own howl animations); also **Pounce** |

Effects are pooled glowing shapes and rings (`M_FXGlow`, `M_FXRing`, max 40 per alien) that stay inside
the glass. Models also lean into turns and when speeding up, turn slightly towards what they look at,
breathe slowly and shift their weight when standing, step when turning on the spot, squash a little on
every step, and leave footprints: dust puffs on sand and soil, ripples in water, embers on lava. Their
feet stand on the case floor (the movement component keeps the capsule about 2 cm up; the body is
lowered by that gap).

Sources for the moves: [Cannonbolt (Ben 10 Wiki)](https://ben10.fandom.com/wiki/Cannonbolt_(Classic)),
[Upchuck (Ben 10 Wiki)](https://ben10.fandom.com/wiki/Upchuck),
[Ranking transformations from Ben 10 Classic](https://ben10.fandom.com/wiki/User_blog:Eye_Lad/Ranking_transformations_from_Ben_10_Classic),
[Game Rant: best Ben 10 aliens](https://gamerant.com/ben-10-best-aliens/),
[CBR: original aliens ranked](https://www.cbr.com/ben-10-original-aliens-ranked/),
[VS Battles: Ben 10 (Classic)](https://vsbattles.fandom.com/wiki/Ben_10_(Classic)).

## Tap the glass

Knock on a case like on an aquarium and its alien answers.

* **The knock** – a fingertip (hand tracking; not while pinching or making a fist) or the front of a
  controller that goes into the glass faster than `TapMinSpeed` (25 cm/s) is a tap; at `TapKnockSpeed`
  (150 cm/s) it is a hard knock. Slower, it is a hand reaching in and nothing happens; the tip has to come
  back out of the glass before it can knock again. A busy hand never knocks (holding or aiming at something,
  resizing, placing a case). The glass ripples from the spot (rings in the case's light colour), rings with
  a tap or a knock, and the controller buzzes.
* **The mood** – a soft tap makes it curious, a hard knock startles it (it flinches with a hop, Grey Matter
  jumps out of his skin, a model with its own flinch animation plays it), and the third tap within 6 s
  annoys it. It turns to the sound, comes over to the spot and answers in character. Busy with a move of its
  own, it only glances over; the tap too many interrupts its answer.
* **The answers** (`TapStyle` on its data asset):

| Alien | Answer |
|---|---|
| Wildmutt | Finds the spot with his nose (he is blind), then he is up on his hind legs like a dog at a window: both front paws land on the glass, scratch at it in turn, and he whines to be let out (snarls, and scratches harder, when annoyed) |
| Benwolf | Snout right up to the glass with a low growl or a sniff, and his breath fogs it; a hard knock gets the sonic howl back |
| Ghostfreak | Glides over and pushes his face right up to the glass - and through it, a little - with a whisper or a laugh |
| Four Arms | Punches the glass back (it shudders, the props jump) - twice when annoyed |
| Grey Matter | Studies the spot, head tilting one way, then the other |
| XLR8 | Is simply there, and taps back twice on your spot before you can blink |
| Upgrade | Green circuit lines race across the glass from the spot |
| Stinkfly | Flies up to the spot and bumps against the glass like a bug at a window |
| Ripjaws | Lunges and snaps his jaws at the glass |
| Cannonbolt | Curls up and rolls at the spot |
| Upchuck | Squashes his face on the glass, rubs it about, and leaves a slimy smear |
| Ditto | His clones come out too |
| Echo Echo | A small sonic ring hits the glass |
| Diamondhead | A little crystal grows on the glass where you tapped |
| Wildvine | A vine reaches out and taps back |

  Annoyed, the others answer with their show-off move (Echo Echo screams, Diamondhead bursts crystals,
  Ghostfreak phases...).
* **Really touching it** – answers land on the glass: a rigged body puts its front paws on it (it rears up
  around its hips and steps forward as far as its paws need, the hind paws planted: `SetFrontReach`), a face
  goes up to it from the head bone, and a rigid model lunges or leans exactly the gap between its front and
  the glass (`GapToGlass`). An alien that can't get to the glass (a prop, or the case too small for it)
  answers from where it is without touching it.

## Sound

Every sound is original: `Scripts/make_museum_sounds.py` synthesises them from oscillators, noise, filters and
envelopes (nothing recorded, downloaded or taken from the show). The aliens' voices are a glottal source
shaped by formants and roughened into growls, snarls, whispers, chirps and hums, one set per species;
footsteps, moves, cases, glass, panel and Omnitrix sounds are impacts, whooshes, struck (modal) objects
and FM tones. Random seeds are fixed, so the same files come out every run.

* **3D** – Resonance Audio (Google's plugin, part of UE 5.7) renders every sound binaurally with HRTF:
  you hear an alien above, behind or beside you, and where in the room its case is. Three ranges
  (`ATT_MuseumRoom` – aliens and moves, full volume within 80 cm, fading out by ~8 m and a little duller
  far away; `ATT_MuseumNear` – footsteps, a case's ambience, Stinkfly's wings, about 2 m; `ATT_MuseumInterface`
  – the panel and the watch).
* **Through the glass** – a sound made inside a case has its highs rolled off and is a little quieter
  (`GlassCutoff`, `GlassVolume`); an alien in your hand, flying home, or a case you have leaned into sounds clear.
* **Voices** – each alien calls out now and then (growls, whispers, chirps: `Calls`), greets a visitor who
  walks up (`Alerts`), strains in its moves (`Efforts`) and squeals, giggles or grumbles when picked up
  (`Held`). To keep the museum from getting noisy, only the `CallingAliens` (2) nearest to you call out
  on their own, never within `CallGap` (4 s) of another call, never beyond `HearingDistance`, and never the
  same clip twice in a row. Footsteps play within `FootstepDistance` only (a splash in water).
* **Moves** – every signature move is scored at the moment its effects fire: Cannonbolt's plates clack as
  he curls, the ball rumbles and bonks off the glass; XLR8 zips and skids; Four Arms strains on every pump
  and his stomp booms; Diamondhead's crystals crackle up; Ghostfreak whispers while he drifts unseen;
  Echo Echo's four screams match his four rings; Benwolf's howl carries eight sonic pulses; Upchuck
  chomps, gulps, gurgles and spits; the pounce sniffs, snarls, leaps and lands (or splashes).
* **Cases** – each case plays its occupant's home world, heard up close: bubbles (ocean, swamp), wind and mist
  (Luna Lobo, the graveyard, Vulpin), crystal chimes (Petropia), insects, tech pulses, embers, or a soft hum
  when empty. Placing, grabbing, resizing and removing a case, and the panel, have their own sounds.
* **Rebuild** after changing the synthesis: `python Scripts/make_museum_sounds.py`, then (editor closed, or in
  the editor's Python) `import_museum_sounds.py`; `--mix-only` just re-applies volumes / ranges / voices.
  Run the import in the editor if the commandlet reports `Decoder for AudioFormat 'BINKA' not found`
  (a handled ensure: the headless commandlet has no audio decoder).

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
  `ModelParts` are extra meshes in the model's frame that swing around a joint (Stinkfly's wings): `Hinge`
  and `Axis` in the mesh's space, and `Amount` / `Offset` / `Speed` (degrees either side of the offset,
  beats per second) at rest and `FlyingAmount` / `FlyingOffset` / `FlyingSpeed` while flying.
* **Rigged models** – `RiggedMesh` (a skinned copy of the model, same shape and frame) is shown instead of
  `ModelMesh` and animated bone by bone from `Rig`: `Legs` (upper / lower / end bone, step phase, front)
  walk with their paws planted by two-bone IK - the elbows and knees bend, a front paw folds back as it
  swings, the body bobs and sways, and it crouches on bent legs for a leap; in the air (leaping or held
  by the player) the legs reach out and paddle. `Spine` breathes (pants when excited), `Neck` / `Head`
  look around and nod with the steps, `Jaw` pants and snarls, `bSniffs` lifts the nose to sniff now and
  then, `Tail` waves (`TailAmount`, `TailSpeed`), `Floating` bones drift. `StrideLength` / `StepHeight`
  in model heights. Wildmutt walks on all fours this way and Ghostfreak's tail waves; the settings live
  in `RIGS` in `Scripts/import_downloaded_models.py`. A rig with front legs can also stand up with its
  front paws on something (`SetFrontReach`: Wildmutt at the glass). Console `Museum.RigTestSpeed 14` makes rigged
  aliens step on the spot as if walking at 14 cm/s (for checking the gait; 0 = off).
* **Animated models** – `Clips` (the model's own hand-made animations, imported with its `RiggedMesh`:
  Benwolf) replace the procedural walk: `Idle` loops while it stands, `Move` blends in while it walks
  (`MoveSpeed` = the ground speed at which it plays in full; slower walks keep a natural cadence and take
  shorter strides instead of playing in slow motion), `Jump` gives the mid-air pose (leaping, held in a
  hand), and moves play `SpecialStart` → `SpecialLoop` (Howl), `Special`, `Hit` or `Attack` on top. The
  `Rig`'s head look and sniffing still work on top of the clips. Clip names come from `CLIPS` in
  `Scripts/import_downloaded_models.py`.
* **Sounds** – `Audio/DA_MuseumSounds`: every shared sound by name (variations, `Volume`, random `Pitch` range,
  `Range`, `MaxPlaying`), the three attenuations, the glass and the voice rules; an alien's `Sounds` on its data
  asset: `Calls` / `Alerts` / `Efforts` / `Held` / `Footsteps`, `Loop`, `CallInterval`, `Volume`, `FootstepVolume`,
  `LoopVolume`, `Pitch` (it also rises a little when its case is scaled down). `Scripts/import_museum_sounds.py`
  (`MIX`, `VOICES`) sets them all.
* **Tapping the glass** – `TapStyle` on an alien's data asset (see *Tap the glass*; `TAP` in
  `Scripts/create_habitats_and_moves.py` sets them), and on `BP_MuseumPawn` `TapMinSpeed` / `TapKnockSpeed`
  (Museum|Glass).
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

The 21 alien downloads (Sketchfab zips: FBX, OBJ, glTF, `.blend`, one `.rar`, a Wii game rip of
Cannonbolt with his ball form, and Benwolf's game model with its animations) go through these scripts,
in this order:

1. **Blender 5.2** – `Scripts/blender_convert_models.py` (settings per download in its `MODELS` table):
   imports the file, re-links textures by name (Sketchfab renames them, e.g. `.psd` → `.tga.png`), turns
   it upright and facing forward, lowers T-pose arms on rigged models and bakes the pose into a plain
   mesh, removes fake glow / transparency / glass / clear-coat / flat metal that FBX/OBJ importers
   invent, rebuilds game-engine shaders (Ghostfreak) as standard materials, reduces meshes above
   60 000 triangles, makes every texture a power of two (≤ 2048 colour, ≤ 1024 other maps), scales it
   to its display height and writes `SourceArt/Converted/<Id>.glb` plus a front/side preview `.png`.
   Moving parts (`parts`: Stinkfly's wings) are kept out of the model and written per side as
   `<Id>_WingL.glb` / `<Id>_WingR.glb` in the same frame, with the joint each swings around (the middle
   and direction of the wing's base) in `manifest.json`. Rigged models (`rigged`: Wildmutt, Ghostfreak)
   keep their skeleton: the stance set up with `pose` and `ik` (two-bone IK - Wildmutt's front paws are
   planted on the ground under his shoulders) becomes the rig's rest pose, meshes that only followed a bone
   get skinned to it, unused tip bones are dropped, and `<Id>_Rig.glb` (skinned) is written next to the
   static `<Id>.glb`. Animated models (`animated`: Benwolf, a game model with 15 hand-made takes) keep
   their rest pose and the takes listed in `clips` (renamed to the game's clip names and exported with
   `<Id>_Rig.glb`); `drop` removes a duplicate body, `bind` skins loose eyes and teeth to the bones that
   move the skin around them (his four-way split jaw), `reduce` thins needlessly dense parts, a mesh
   scale the game export left under the rig is undone (else every joint sits outside the body), and the
   static `<Id>.glb` shows the `still` frame.
   ```
   & "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Scripts\blender_convert_models.py -- SourceArt\Downloaded SourceArt\Converted [Id ...]
   ```
2. **Unreal** (editor closed) – `Scripts/import_downloaded_models.py` imports every `.glb` fresh into
   `/Game/AlienMuseum/Models/<Id>/` (Interchange; no Nanite, no collision, turned to face +X) with its
   moving parts (`PART_MOTION` sets how they swing) and a rigged model's skeletal mesh (`<Id>_Rig/`, using
   the static model's materials, which are flagged *Used with Skeletal Mesh* - without it they render as
   the default grey material), creates the `DA_Model_*` assets (`RIGS` fills in their `Rig`) and
   `DA_AlienCollection_Models`, and makes it the museum's collection. An animated model's clips are
   imported with its skeletal mesh (`Benwolf_RigIdle`, …) and set on its `Clips` from `CLIPS`. Models in
   `RETIRED` (taken out of the museum: Four Arms 2, Cannonbolt 2 and 3, Upgrade 2) have their assets
   deleted.
   ```
   & "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" C:\Games\Ben10\Ben10.uproject -run=pythonscript -script="C:/Games/Ben10/Scripts/import_downloaded_models.py" -unattended -nosplash -nullrhi
   ```
   Add model ids after the script path (inside the quotes) to import only those.
3. **Re-posing** (Blender) – `Scripts/blender_pose_models.py` bends the limbs of rig-less converted
   models around their joints (a hand-made two-bone skin, same frame as the model) and lists the results
   in `SourceArt/Converted/poses.json`: `FourArms_2_Flex` (double biceps, shown during Flex),
   `FourArms_2_Rest` and `Wildvine_Rest` (arms relaxed at their sides; they replace the T-pose models,
   and Four Arms gets taller now that his arms fit the case). Rigged T-pose models get their arms lowered
   by the converter's `pose` setting instead.
   ```
   & "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Scripts\blender_pose_models.py -- SourceArt\Converted [pose id ...]
   ```
4. **Habitats and moves** (Unreal, editor closed) – `Scripts/create_habitats_and_moves.py` creates the
   `HAB_*` habitats, imports the poses from `poses.json` and Cannonbolt's ball (sharing the models'
   materials where they are the same model), sets every alien's habitat, moves and extras, and keeps the
   classic Heatblast and Stinkfly out of the museum collection. Run it after every import.

**Adding another download:** unzip it into `SourceArt/Downloaded/<name>/`, run
`Scripts/blender_inspect_models.py` to see its size, rig, textures and a preview, add a line to `MODELS`
(id, alien name, height, `rotate` if it lies down or faces sideways, `pose` for T-pose arms or a tail that
hangs below the feet (`(degrees, "raise")`), `folder` / `file` for a download with several models, `colors`
for materials that are just flat colours, `parts` for a mirrored pair of moving meshes such as wings,
`rigged` + `ik` to keep the skeleton and animate it), convert
it, add its id to `ORDER` in the import script (and to `EXTRA` if it is a new alien), run the import, add
the alien to `MOVES` in `create_habitats_and_moves.py` and run that too.

**The Omnitrix** (the classic watch from the `classic-omnitrix` download, for the player's wrist):
`Scripts/blender_convert_omnitrix.py` splits it into three parts in one frame - the band with its four
tubes, the core (faceplate ring and green lights, which pops up and turns as the dial) and the face disc
(its own material slot and UVs across the disc, for the game's hourglass / silhouette face) - with the
origin in the middle of the wrist hole, textures shrunk to 1024 px, and writes `Omnitrix_*.glb` plus
`Omnitrix.json` (where the parts sit). `Scripts/import_omnitrix.py` imports them into
`/Game/AlienMuseum/Models/Omnitrix/Band|Core|Face/` (the core shares the band's material). As modelled
the band is only 4.5 × 4.9 cm, so the game scales it to the wrist.
```
& "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Scripts\blender_convert_omnitrix.py -- SourceArt\Downloaded SourceArt\Converted
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" C:\Games\Ben10\Ben10.uproject -run=pythonscript -script="C:/Games/Ben10/Scripts/import_omnitrix.py" -unattended -nosplash -nullrhi
```

## Quest performance choices

Forward shading, multiview, 4× MSAA, dynamic foveation (level High), no Lumen / VSM / ray tracing /
distance fields / Substrate, ASTC-only textures, no dynamic shadows (fake contact shadows), unlit
emissive "lighting" inside chambers, pillars instanced, alien AI thinks at 5 Hz, alien animation only
when visible, max 8 chambers. Imported models: static meshes, except the rigged Wildmutt, Ghostfreak and
Benwolf (skinned meshes with 53 / 44 / 86 bones posed in C++ on the game thread, no animation blueprint;
Benwolf's clips are sampled straight from their compressed animation data, at most four per frame; the
collection's cards show their static twins), ≤ 60 000 triangles
each (8 cases ≈ 480 000 at most), power-of-two textures ≤ 2048, no Nanite, no collision, no shadow casting;
one movable key light without shadows. Habitats: fixed props instanced (one draw call per kind), about
3–10 loose physics props per case (asleep when still), ambient effects as one instanced mesh, animated
only while the case is seen. Moves: pooled effects (≤ 40 per alien), 3 after-images, no particle systems.
Sound: short sounds compressed with Bink Audio and kept in memory (they start instantly), at most one
loop per case / alien, and a loop out of range goes silent for free (virtualised) until you come close;
Resonance renders all sources into one third-order ambisonic mix decoded once for both ears.

## Known limitations

* The sounds are synthesised, not recorded: creature voices are stylised rather than like real
  animals or the show. Better recordings can replace any of them - import them in place of the `V_*` /
  `SFX_*` waves (same names), or point an alien's `Sounds` at others.
* In the editor, a game in the background is silent (Windows mutes an unfocused app): click into the
  viewport, or start the editor with `-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0` (a line added to
  `Saved/Config/WindowsEditor/Engine.ini` by hand does not survive the editor rewriting that file).
* Tapping the glass can't be done with the mouse (see *Test in the editor* for the Python line); the
  fingertip and controller knocks are for the headset. The smear and the breath on the glass are soft
  glowing shapes, not real fluid.
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
* Most models are rigid static meshes: moves bend, squash, lean, hide or swap the whole body (Cannonbolt
  swaps to his ball); their limbs don't move. Stinkfly's wings flap, and Wildmutt and Ghostfreak are
  rigged (legs, spine, head, jaw, tail). Four Arms (1) flexes with the pump and stomp only. Benwolf moves
  with his model's own animations; it has a run but no walk, so his slow prowl in the case is part of
  the run blended over his hunched idle.
* Four Arms (2), Cannonbolt (2), Cannonbolt (3) and Upgrade (2) were taken out of the museum (`RETIRED`).
  A saved case that held one of them comes back empty.
* Heatblast was taken out of the museum (no model was downloaded for him). A saved case that held him
  comes back empty; remove it with its red X or put another alien in. His head flames would attach to a
  future Heatblast model automatically.
* In a small case a wide alien (e.g. Cannonbolt, 50 cm across) has little room to roll or leap - make
  the case bigger with the handles.
