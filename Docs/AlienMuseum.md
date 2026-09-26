# Alien Museum – developer guide

Mixed-reality alien museum for **Meta Quest 3S** (UE 5.7.4, Epic Native OpenXR + Meta XR plugin 1.205, MRUK).
The player sees their real room through passthrough, places square glass display cases on the floor, on
furniture or floating in mid-air, and fills them with autonomous aliens chosen from a holographic collection.

## Architecture

```
UAlienDataAsset (DA_Alien_*)          identity, look, behaviour tuning
   └─ AAlienCharacter (BP_AlienCharacter)      body (procedural placeholder), movement, containment
        └─ AAlienAIController                  state machine: Idle / Wander / LookAround / ReactToPlayer / Held
             └─ CharacterMovementComponent     direct steering inside the chamber (no navmesh needed)

AAlienChamber (BP_AlienChamber)                   Shape: Square (display case, default) or Round (pod)
   ├─ Base, FloorGlow, Glass, TopCap, LightPanel, FramePillars (1 instanced draw call)
   ├─ ContainmentWalls (4 boxes square / 8 round) + Ceiling   block only the alien, never the pointer
   ├─ ObstacleRock / ObstacleCrystal               things the alien walks around
   ├─ MovementBounds, SpawnPoint, SelectBox
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
| Data | `Source/Ben10/Data/` – `AlienDataAsset`, `AlienCollectionAsset` |
| Aliens | `Source/Ben10/Aliens/` – `AlienCharacter`, `AlienAIController`, `AlienAppearanceComponent` |
| Chamber | `Source/Ben10/Chamber/AlienChamber` |
| Mixed reality | `Source/Ben10/MR/` – `MuseumSceneComponent`, `MuseumPersistenceComponent`, `MuseumSaveGame` |
| Interaction | `Source/Ben10/Interaction/` – `MuseumPawn`, `MuseumHandInteractor` |
| UI | `Source/Ben10/UI/AlienCollectionPanel` |
| Content | `Content/AlienMuseum/` – `Maps/L_AlienMuseum`, `Blueprints/BP_*`, `Data/DA_*`, `Data/Classic/DA_Classic_*`, `Materials/M_*` |
| Tools | `Scripts/create_classic_aliens.py` – generates the classic alien data assets |

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
| Tab | open / close the Alien Collection |

## Deploy to Quest 3S

1. **Install Android support for UE 5.7** (currently missing): Epic Games Launcher → Library → UE 5.7 →
   ▼ → *Options* → tick **Android** → Apply. If 5.7 does not appear in the Launcher library, reinstall
   5.7 through the Launcher with Android ticked.
2. Headset: enable Developer Mode (Meta Horizon app), connect USB-C, accept *Allow USB debugging*.
   `adb devices` must list the headset.
3. Editor: Platforms → Android → *Package Project* (or Quick Launch to the device).
   The package folder contains the APK and an `Install_*.bat` that installs it with adb
   (or run `adb install -r <apk>` yourself).
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
* **Classic aliens** – generated by `Scripts/create_classic_aliens.py` (run it in the editor's Python
  console to rebuild them); the museum uses `DA_AlienCollection_Classic`. Switch back to the original
  placeholder aliens by setting `BP_MuseumDirector` → Collection to `DA_AlienCollection`.

> **Fan content:** Ben 10 and its aliens belong to Cartoon Network / Warner Bros. Discovery. The classic
> aliens are simple fan-made figures for private use. Do not publish or distribute the app with them
> (for example on the Meta Horizon Store) without a licence – use original aliens instead.
* **Chamber** – `BP_AlienChamber`: shape (Square / Round), half-width (`Radius`), heights, scale range,
  colours, obstacles, real interior light.
* **Director** – `BP_MuseumDirector` in the level: collection, starter chamber, max chambers, chambers float,
  surface snap distance, restored-alien delay, scene options (occluder labels, debug room view,
  passthrough), persistence options.
* **Pawn** – `BP_MuseumPawn`: grab distance, click timing, rotate/scale speeds, mid-air placement distance
  and range, optional input assets.

## Quest performance choices

Forward shading, multiview, 4× MSAA, dynamic foveation (level High), no Lumen / VSM / ray tracing /
distance fields / Substrate, ASTC-only textures, no dynamic shadows (fake contact shadows), unlit
emissive "lighting" inside chambers, pillars instanced, alien AI thinks at 5 Hz, alien animation only
when visible, max 8 chambers.

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
