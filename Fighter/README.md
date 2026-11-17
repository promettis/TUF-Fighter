# TUF FIGHTER

A 3D fighting game played on a 2D plane (Street Fighter / Mortal Kombat style), written in C++17.

Run **TUF_Fighter.exe**. It must stay next to the `Assets` folder.

## Controls

|                      | 1 player            | 2 players - P1 | 2 players - P2  |
|----------------------|---------------------|----------------|-----------------|
| Move / jump / crouch | W A S D (or arrows) | W A S D        | Arrow keys      |
| Light punch          | U                   | F              | I  (Num 4)      |
| Heavy punch          | I                   | G              | O  (Num 5)      |
| Special              | O                   | H              | P  (Num 6)      |
| Light kick           | J                   | C              | K  (Num 1)      |
| Heavy kick           | K                   | V              | L  (Num 2)      |
| Super (full meter)   | L                   | B              | ;  (Num 3)      |

Gamepads work too: d-pad / stick, X Y A B = punches and kicks, RB = special, RT = super.

Menus: W/S or arrows, Enter to confirm, Esc to go back / pause.
F1 collision boxes, F3 frame rate, F11 full screen, M mute.

## Moves

| Input                          | Move                                                    |
|--------------------------------|---------------------------------------------------------|
| LP / HP / LK / HK              | Jab, straight, snap kick, roundhouse (knocks down)      |
| Down + LP / HP / LK / HK       | Low jab, uppercut (launcher), low kick, sweep           |
| Forward + HP                   | Overhead smash - must be blocked standing               |
| Forward + HK                   | Push kick - big knock back                              |
| LP / HP or LK / HK in the air  | Jumping punch / kick                                    |
| Special                        | Energy shot (projectile, different per fighter)         |
| Forward + Special              | Rush punch                                              |
| Down + Special                 | Rising upper (invincible on the way up)                 |
| Special in the air             | Dive kick                                               |
| Super                          | Beam super - needs a full super meter                   |

* Block by holding **back**. Low kicks and sweeps must be blocked crouching, jump attacks and
  the overhead standing. Energy shots and the beam do a little damage through a block.
* Combos: when an attack connects, cancel it into a stronger one:
  `LP > LK > HP > HK`, or end with a special / the super: `LP > HP > Special > Super`.
* The super meter fills when you hit, get hit or block.

## Building

Requirements: Visual Studio (Desktop development with C++) and CMake.

Double click **build.bat**, or:

    cmake -S . -B build
    cmake --build build --config Release

`build/TUF_Fighter.sln` can also be opened in Visual Studio. The graphics library (raylib) is
compiled from the sources in `third_party`, nothing has to be installed.

## Source code map (src/)

| File                 | What it does                                                                 |
|----------------------|------------------------------------------------------------------------------|
| `main.cpp`           | Window, screens (loading, title, character select, versus, fight), input     |
| `game.cpp/.h`        | A match: rounds, hit detection, projectiles, super beam, CPU player, camera, effects, HUD |
| `fighter.cpp/.h`     | Fighter state machine, move table with frame data, hit / hurt boxes          |
| `anims.cpp/.h`       | Every animation, authored in code as key poses                               |
| `pose.cpp/.h`        | Pose -> skeleton solver (two-bone inverse kinematics), skinning matrices     |
| `model.cpp/.h`       | .glb loading on a worker thread, mesh simplification, skeleton               |
| `render.cpp/.h`      | Shaders: lighting, GPU skinning, shadow map, sky, ground, menu backdrop      |
| `stage_bloc.cpp`     | The University Bloc stage, built in code after photos of the real building   |
| `ui.cpp/.h`          | 2D drawing helpers and fonts                                                 |
| `audio.cpp/.h`       | Sound effects and music synthesised in code (replaceable with files)         |

How the animation works: the character models only contain a "Running" clip, so all fighting
animations are procedural. A `Pose` says where hands and feet are and how hips, chest and head
are turned; `Rig::solve` turns that into bone transforms with inverse kinematics, so the same
animation works on every character. Hit boxes are taken from the animated bones each frame.

## Adding content

* **New fighter**: drop a rigged `.glb` (Mixamo bone names) into
  `Assets/Rigged 3D character models`. It appears on the select screen automatically.
  A portrait `Assets/Portraits/<Name>.png` (or a photo with the same name next to the model)
  is used for the select screen and HUD. Stats and effect colours are set in
  `applyKnownStats()` in `main.cpp`.
* **Stage**: The University Bloc is generated in code (`stage_bloc.cpp`: building, sign, glass entrance,
  palms, flags, trees) and shaded procedurally in `render.cpp`. The scanned `.glb` and the reference
  photos in `Assets/Stages` are kept but no longer loaded. Any other `.glb` dropped into `Assets/Stages`
  is loaded as a model stage.
* **Sounds / music**: see `Assets/Sounds/README.txt`.
* **New move**: add a row to the move table in `fighter.cpp` and its key poses in `anims.cpp`.

## Credits

* [raylib](https://www.raylib.com) (zlib licence) - window, OpenGL, input, audio
* [meshoptimizer](https://github.com/zeux/meshoptimizer) (MIT) - mesh simplification
* Barlow Condensed font (SIL Open Font Licence)
