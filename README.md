# Alien Museum (Ben 10 fan project)

A mixed-reality alien museum for **Meta Quest 3S**, built with **Unreal Engine 5.7.4**.

You see your real room through passthrough, place glass display cases on the floor, on furniture or floating
in mid-air, and fill them with Ben 10 aliens picked from a holographic collection. Each alien lives in its
case: it walks around, looks at you, performs its signature moves and reacts when you tap on the glass.
Where the room allows, every alien stands **life size** in a case made just for it.

> Unofficial, non-commercial fan project. Ben 10 and its characters belong to Cartoon Network /
> Warner Bros. Discovery. This project is not affiliated with or endorsed by them. The 3D models were made
> by their original creators and are used here for fan purposes only.

## Features

- **Mixed reality**: passthrough, the room scanned by Meta's MRUK (walls, ceiling, furniture), occlusion
  behind real objects, and cases kept in place with spatial anchors between sessions.
- **20 aliens**, including Four Arms, Heatblast, Diamondhead, XLR8, Wildmutt, Ghostfreak, Stinkfly, Upgrade,
  Ripjaws, Cannonbolt, Grey Matter, Benwolf, Benmummy, Benvicktor, Upchuck, Ditto, Eye Guy, Wildvine,
  Echo Echo and Buzzshock.
- **Life size**: each alien is shown at its real height. It is scaled down only when the ceiling, walls,
  furniture or other cases leave no room, and the info panel says why.
- **Living aliens**: they wander, look around and react to you. Rigged aliens (Wildmutt, Ghostfreak) walk
  with procedural IK legs, and Benwolf uses his model's own animations.
- **Signature moves**: Heatblast's flames, XLR8's speed dash, Cannonbolt rolling into a ball, Stinkfly's
  wings, Wildmutt's pounce and more.
- **Tap the glass**: every alien answers in its own way. Wildmutt paws at the glass, Upchuck smears it and
  Four Arms bangs on it.
- **Home-world habitats** inside each case, with ambient sound and synthesised voices, footsteps and effects
  played in 3D.
- **Hands or controllers**: point and pinch or trigger to select, a fist or grip to grab, and two hands to
  resize and rotate. You can also take an alien out of its case and hold it.
- **Cases**: placed anywhere, carried, pushed and pulled, resized with handles, and removed.
- **Desktop test mode**: play in the editor without a headset.

## Requirements

- Windows 10/11 with **Unreal Engine 5.7.4** and Visual Studio 2022/2026 (C++ game development workload).
- [Git LFS](https://git-lfs.com) (the Unreal assets are stored with LFS).
- To run it on the headset:
  - a Meta Quest 3 / 3S with Developer Mode on;
  - UE's Android support, Android SDK and NDK r27c (27.2.12479018);
  - the JDK 17 bundled with Android Studio.
- The Meta XR plugin (1.205) installed in the engine.

## Getting started

```bash
git lfs install
git clone <this repo's URL>
```

1. Right-click `Ben10.uproject` and choose **Generate Visual Studio project files**.
2. Build `Development Editor | Win64`, or run:
   ```
   "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" Ben10Editor Win64 Development -Project="<path>\Ben10.uproject" -WaitMutex
   ```
3. Open `Ben10.uproject`, then open the `Content/AlienMuseum/Maps/L_AlienMuseum` level.

## How to use

### In the editor (no headset)

Press **Play**. The level's editor-only furniture stands in for your room.

| Input | Action |
|---|---|
| Mouse / W A S D | look / move |
| Tab | open or close the Alien Collection |
| Left mouse | select, place a case, or show a case's info panel |
| Right mouse (or hold left mouse) | carry a case, or take an alien out and hold it |
| Z / C | rotate what you carry |
| Q / E | shrink or grow what you carry (up to life size) |
| Left mouse on a handle | drag to change the case's height, width or depth |
| Delete twice | remove a case |

### On the Quest

Package and install the app with the headset connected by USB:

```powershell
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$env:NDKROOT = "$env:LOCALAPPDATA\Android\Sdk\ndk\27.2.12479018"
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="<path>\Ben10.uproject" -noP4 -platform=Android -cookflavor=ASTC -clientconfig=Development -build -nocompileeditor -cook -stage -pak -package -archive -archivedirectory="<path>\Saved\Packaged"
adb install -r -g "<path>\Saved\Packaged\Android_ASTC\Ben10-arm64.apk"
```

The first time the app launches, allow the **spatial data** permission. If your room hasn't been scanned
yet, Space Setup opens.

- **Open the collection**: press X, Y, B or Menu. With hand tracking, look at your **left** palm and pinch.
- **Pick an alien**: point at it and pinch or pull the trigger, then point where the case should go (a
  floor, a table or mid-air) and confirm.
- **Move a case**: grab it with a fist or the grip. Reach out to push it away and pull your hand in to bring
  it closer.
- **Resize a case**: use both hands, the thumbstick while carrying, or the handles on the case.
- **Tap the glass** with a fingertip or a controller to get the alien's attention.

## Project layout

| Folder | Contents |
|---|---|
| `Source/Ben10/` | the C++ game code: aliens, cases, the museum director, mixed reality, interaction and UI |
| `Source/Ben10XR/` | an early-loading OpenXR layer (a Quest crash fix) |
| `Content/AlienMuseum/` | the level, blueprints, alien data, habitats, imported models, materials and sounds |
| `Scripts/` | the Blender and Unreal Python pipeline that turns downloaded models into museum aliens |
| `Docs/AlienMuseum.md` | the full developer guide: architecture, tuning, the model pipeline and known limitations |

The raw downloaded models (`SourceArt/`) and all build output are not in the repository. The imported assets
in `Content/` are all you need to run the project.
