# Drakensang Mod: Guide and Manual

Русская версия: [README.md](README.md)

A native mod for **Drakensang: The Dark Eye** (2008) and **Drakensang: The River of Time**. It hides the interface for clean screenshots, adds a free-fly camera that passes through walls, lets you travel to any level from a menu, extends draw distance and shows cut content. The 2008 game also gets a lighting panel and a merged city of Ferdok.

The mod loads as a `dinput8.dll` next to the game and doesn't change any game files, so removing it only takes deleting its own files. This project contains no game files: you need your own copy.

**Contents**

- Guide: [Install](#install), [First run](#first-run), [Clean screenshots](#take-a-clean-screenshot), [Free camera](#fly-through-a-level), [Level travel](#travel-to-any-level), [Cut content](#look-at-cut-content), [Whole Ferdok](#see-all-of-ferdok-at-once), [Lighting](#light-a-scene), [Moving the party](#move-the-party), [3D export](#export-3d-models), [Vintage Story](#build-a-model-in-vintage-story)
- Manual: [Supported versions](#supported-versions), [Features by game](#features-by-game), [Controls](#controls), [Settings](#settings), [Installed files](#installed-files), [Uninstall](#uninstall), [Troubleshooting](#troubleshooting), [Known issues](#known-issues)
- Developers: [Build from source](#build-from-source), [Regenerate data](#regenerate-data), [Support a new game build](#support-a-new-game-build), [Command channel](#command-channel), [Project layout](#project-layout)

# Guide

Use a separate save while you experiment. Skipping ahead in the story with level travel can break quests.

## Install

1. In Steam, right-click the game and choose **Manage → Browse local files**.
2. Copy everything inside the matching folder to the game root, next to the `.exe`. Include the `_mod` subfolder.

   | Game | Copy from |
   |---|---|
   | Drakensang (2008) | `dist/drakensang/` |
   | The River of Time | `dist/river-of-time/` |

3. **Linux only:** open **Properties → General → Launch Options** and add:

   ```
   WINEDLLOVERRIDES="dinput8=n,b" %command%
   ```

   Windows doesn't need this step. The mod has only been tested on Linux with Proton, so Windows support is unconfirmed.

River of Time must run as `drakensanga1.exe`, the version with the Phileasson add-on. Its base `drakensang.exe` isn't supported yet.

## First run

Start the game and load a save. A short `Drakensang mod 0.2 loaded` message appears at the top of the screen, and the mod creates `_mod/mod.log`.

Press **F11**, move the mouse and press **W**. If the camera flies, the mod is working. Press **F11** again to return to the normal camera.

If nothing happens, see [Troubleshooting](#troubleshooting).

## Take a clean screenshot

1. Press **F10** to hide the interface.
2. Press **F11** and fly to the shot you want.
3. Press **Q** or **E** to tilt the camera, or **Z** or **X** to change the field of view. **Backspace** resets both.
4. Press **F2** if you want to turn off the fog.
5. Press **F3**.

The PNG goes to `screenshots/` in the game folder. The mod's own messages and menus aren't included in the image.

## Fly through a level

Press **F11**. The camera starts from the current view and ignores walls, buildings and invisible barriers.

Use **W A S D** to move, **Space** and **C** to go up and down, and the mouse to look around. Hold **Shift** to go faster or **Alt** to go slower. The mouse wheel changes the base speed.

The game doesn't receive your input while the free camera is on, so your party stays where it is.

## Travel to any level

1. Press **F7**.
2. Choose a level with **↑ ↓**, **PgUp**, **PgDn**, **Home** or **End**.
3. Choose an entry point with **← →**. The line under the list shows the current one.
4. Press **Enter**.

Press **Esc** to close the menu without travelling.

## Look at cut content

1. Press **F7** and open **★ Cut & unused locations**, the first row.
2. Choose a location and press **Enter**.

The mod places the model far below the current level and moves the free camera to it. In the 2008 game, the list includes the Temple of Angrosch and the prison in Murolosh, the Haunted House in Tallon, early versions of Ferdok interiors and early terrain drafts.

Placing another location replaces the previous one. To remove it, choose **✖ Remove placed model**.

## See all of Ferdok at once

*Drakensang (2008) only.*

Press **F7** and choose **Ferdok — whole city**. The mod loads Praios Square, removes the barriers between districts and adds the buildings of the other three districts. It also plants a forest outside the walls. The extra buildings appear a few seconds after the level loads.

The added districts are visual only, with no collision and no NPCs. Use them for camera flights and screenshots rather than normal play.

## Light a scene

*Drakensang (2008) only.*

1. Press **F1** to open the panel. It has four groups: Sun / moon, Ambient and back light, Image, Fog and sky.
2. Choose a value with **↑ ↓** and change it with **← →**. Hold **Shift** for steps five times larger or **Ctrl** for fine steps.
3. Press **Shift+1** to save your settings as preset 1. Keys 1 to 4 each hold one preset.
4. Press **Esc** to close the panel.

Changes stay active across level changes until you reset them. They aren't kept after you quit the game and presets don't load by themselves, so press **1** to **4** to load one. **Backspace** resets the selected value and **Del** resets everything.

## Move the party

*Drakensang (2008) only.*

1. Press **F11** and fly to the place you want, close to the ground.
2. Press **F4**.

The party moves to the camera position and faces the camera's direction.

## Export 3D models

Export creates a `.glb` file with textures. This is glTF 2.0, a standard format supported by Blender and other 3D tools. Units are metres and Y points up. This has been tested on Drakensang (2008).

You need Python 3 for this and for the Vintage Story converter. The game itself doesn't need them.

```bash
pip install numpy pillow
export DRAKENSANG_DIR="/path/to/Drakensang"

# A whole level
python3 tools/export_gltf.py --level location09 -o murolosh.glb

# One model
python3 tools/export_gltf.py --model _location_09_grounds/dungeon_murolosch_tempel -o temple.glb
```

| Option | What it does |
|---|---|
| `--no-props` | Level export: static environment only |
| `--effects` | Includes fog volumes and matte paintings |
| `--all-lods` | Exports all LOD levels instead of the most detailed one |
| `--no-lightmaps` | Doesn't attach lightmaps |
| `--max-texture N` | Downscales larger textures; 0 keeps the original size, default 1024 |
| `--game DIR` | Game folder, overrides `DRAKENSANG_DIR` |

Level ids are in the first column of `dist/drakensang/drakensang_mod_levels.txt`.

## Build a model in Vintage Story

The converter turns a `.glb` into WorldEdit schematics, choosing blocks by colour and material. This has been tested on Vintage Story 1.22.

```bash
python3 tools/gltf_to_vintagestory.py murolosh.glb --scale 1 -o murolosh
```

Give `-o` a name without an extension. The script writes `murolosh.json` and a `murolosh_preview.png` to check the result. Models larger than 1024 blocks are split into files such as `murolosh_x0_z0.json`.

Copy the `.json` files to `VintagestoryData/WorldEdit/`, which is `~/.config/VintagestoryData/WorldEdit/` on Linux. Then run these commands in the game:

```
/we import murolosh
/we paste
```

| Option | What it does |
|---|---|
| `--scale N` | Blocks per metre, default 1 |
| `--thicken N` | Grows surfaces by N blocks to close thin gaps |
| `--no-water` | Leaves water out |
| `--families stone,wood` | Uses only these block families |
| `--tile N` | Maximum schematic size per axis, default 1024 |
| `--brightness N` | Colour multiplier before matching, default 1.15 |
| `--cut N` | Preview: floor plan of the blocks below this height |
| `--rebuild-palette --assets DIR` | Rebuilds `tools/vs_palette.json` from a Vintage Story `assets` folder |

# Manual

## Supported versions

| Game | Executable | Status |
|---|---|---|
| Drakensang (2008), Steam | `drakensang.exe` | Supported |
| The River of Time with Phileasson, Steam | `drakensanga1.exe` | Supported |
| The River of Time, base game | `drakensang.exe` | Not supported yet |
| Any other build | | Screenshots only |

Game addresses are tied to the exact Steam executables. On startup the mod checks the executable and its code signatures. If they don't match, it turns off all game features instead of risking a crash.

## Features by game

| Feature | Drakensang | River of Time |
|---|---|---|
| Hide interface | Yes | Yes |
| Free camera | Yes | Yes |
| Level menu | Yes | Yes |
| Cut and unused locations | Yes | Yes |
| PNG screenshots | Yes | Yes |
| Fog toggle | Yes | Yes |
| Object and light draw distance, visibility zones off, far clip | Yes | Yes |
| Model and tree distance and detail | Yes | No |
| Lighting panel | Yes | No |
| Whole Ferdok | Yes | No |
| Party to camera | Yes | No |
| Freeze time | Not implemented | Not implemented |

## Controls

All of these can be changed in `drakensang_mod.ini`, except the keys inside the menus.

**Main keys**

| Key | Action | Setting |
|---|---|---|
| F1 | Lighting panel | `lighting_menu` |
| F2 | Fog on or off | `toggle_fog` |
| F3 | Screenshot | `screenshot` |
| F4 | Party to camera, free camera must be on | `party_to_camera` |
| F6 | Reserved for freeze time, only shows a message | `freeze_time` |
| F7 | Level menu | `level_menu` |
| F10 | Interface on or off | `toggle_ui` |
| F11 | Free camera on or off | `toggle_freecam` |

**Free camera**

| Key | Action | Setting |
|---|---|---|
| Mouse | Look around | `mouse_sensitivity`, `invert_y` |
| W, S, A, D | Move | `forward`, `back`, `left`, `right` |
| Space, C | Up, down | `up`, `down` |
| Shift, Alt | Faster, slower | `fast`, `slow` |
| Mouse wheel | Base speed | |
| Q, E | Tilt | `roll_left`, `roll_right` |
| Z, X | Field of view | `fov_in`, `fov_out` |
| Backspace | Reset tilt and field of view | `reset_roll_fov` |

**Level menu**

| Key | Action |
|---|---|
| ↑, ↓, PgUp, PgDn, Home, End | Choose a level |
| ←, → | Choose an entry point |
| Enter | Travel, or open the selected page |
| Esc | Close |

On the cut content page, **←** or **Backspace** returns to the level list.

**Lighting panel**

| Key | Action |
|---|---|
| ↑, ↓, PgUp, PgDn | Choose a value |
| ←, → | Change it; Shift for ×5, Ctrl for fine steps |
| Backspace | Reset the selected value |
| Del | Reset all values |
| 1 to 4 | Load a preset |
| Shift+1 to Shift+4 | Save a preset |
| Esc | Close |

## Settings

`drakensang_mod.ini` is in the game root, and every option has a comment there. The mod reads it at startup, so restart the game after changing it.

Key names include `F1` to `F12`, `A` to `Z`, `0` to `9`, `Space`, `Enter`, `Tab`, `Shift`, `Ctrl`, `Alt`, `Home`, `End`, `PageUp`, `PageDown`, `Numpad0` to `Numpad9`, `MouseMiddle`, `MouseX1`, `MouseX2` and `None`. A virtual key code such as `0x7A` also works.

**`[freecam]`**

| Option | Default | Meaning |
|---|---|---|
| `speed` | 6 | Metres per second |
| `fast_multiplier` | 4 | Speed multiplier while holding `fast` |
| `slow_multiplier` | 0.25 | Speed multiplier while holding `slow` |
| `mouse_sensitivity` | 0.12 | Degrees per unit of mouse movement |
| `invert_y` | 0 | 1 inverts vertical look |
| `roll_speed` | 45 | Tilt speed |
| `fov_speed` | 20 | Field of view change speed |

**`[overlay]`**

| Option | Default | Meaning |
|---|---|---|
| `messages` | 1 | Short messages at the top of the screen |
| `messages_when_ui_hidden` | 1 | Keep those messages while the interface is hidden |
| `font_scale` | 1.0 | Overlay text size |

**`[graphics]`**

| Option | Default | Meaning |
|---|---|---|
| `unlimited_draw_distance` | 1 | Objects, characters and trees don't vanish in the distance or get culled as too small |
| `unlimited_light_distance` | 1 | The same for lights, which the game normally turns off beyond 50 m; can lower FPS |
| `ignore_visibility_zones` | 1 | Draws parts of a level that visibility zones would hide |
| `fog_distance` | 1 | Fog distance multiplier: 1 is the game's value, 3 pushes fog three times further, 0 removes it |
| `far_clip` | 10000 | Camera far plane in metres; the game uses 2500 in Drakensang and 3000 in River of Time; 0 leaves it unchanged |
| `unlimited_model_distance` | 1 | Drakensang only: models stay visible in simplified form beyond 60–150 m |
| `model_detail_distance` | 2 | Drakensang only: how much further models keep full detail; 1 is the game's value |
| `tree_detail_distance` | 3 | Drakensang only: how much further trees stay 3D; applies after a level loads |

**`[display]`**

| Option | Default | Meaning |
|---|---|---|
| `borderless` | 1 | Borderless full-screen window instead of exclusive full screen |

**`[dev]`**

| Option | Default | Meaning |
|---|---|---|
| `log` | 1 | Writes `_mod/mod.log` |
| `dump_image` | 0 | Writes the decrypted game image to `_mod/dump/` |
| `command_file` | 0 | Enables the [command channel](#command-channel) |
| `extra_args` | | Extra engine arguments, for example `-debug` |

## Installed files

| File | Purpose |
|---|---|
| `dinput8.dll` | Loader |
| `_mod/drakensang_mod.dll` | All mod features |
| `drakensang_mod.ini` | Settings |
| `drakensang_mod_levels*.txt` | Level menu list |
| `drakensang_mod_extras*.txt` | Cut and unused locations |
| `drakensang_mod_composites.txt` | Whole Ferdok, Drakensang only |

The mod creates these while running:

| File | Purpose |
|---|---|
| `_mod/mod.log` | Log of the current session |
| `_mod/build/` | Working copy of the mod DLL |
| `_mod/dump/` | Decrypted game image, only with `dump_image=1` |
| `screenshots/` | Your screenshots |
| `drakensang_mod_lighting.ini` | Lighting presets |

## Uninstall

1. Delete `dinput8.dll`, all `drakensang_mod*` files and the `_mod` folder from the game root.
2. Remove the launch option in Steam.

Your screenshots stay in `screenshots/`.

## Troubleshooting

| Problem | What to check |
|---|---|
| No mod message and no `_mod/mod.log` | The launch option is set, and `dinput8.dll` is next to the `.exe` |
| Log says `game profile: unsupported build`, only screenshots work | This game build isn't supported, see [Supported versions](#supported-versions) |
| `This game is not supported by the mod yet (only screenshots)` | The same cause, or River of Time is running its base `drakensang.exe` |
| `Lighting panel is not available in this game yet` | River of Time doesn't have the panel |
| `Level list missing` | The `drakensang_mod_levels*.txt` files aren't in the game root |
| `Free camera unavailable here` | No level is loaded yet, for example in the main menu |
| The game crashes | Keep `_mod/mod.log` and any `.dmp` file from the game folder for the bug report |

## Known issues

- **Freeze time:** F6 isn't implemented.
- **`.dmp` file on exit:** if the level contains objects placed by the mod, such as whole Ferdok, the game can write a `.dmp` file to its folder as it closes. This comes from the game's internal check `this->resources.IsEmpty()`. The likely cause is that the mod holds references to the placed objects until exit. The game is already closing at that point, and you can delete the file.
- **Tallon arrival:** the mod moves the party to the town centre using approximate coordinates, so the spot may need adjusting.
- **F4:** the hotkey itself hasn't been tested directly. The same code has been tested through the `party` developer command.

# Developers

## Build from source

You need clang, lld and Wine with its 32-bit headers and import libraries. On Arch Linux:

```bash
sudo pacman -S clang lld wine
```

| Command | Result |
|---|---|
| `./build.sh` | Builds everything into `build/` and `dist/` |
| `./build.sh core` | Builds only the mod DLL; `loader` builds only `dinput8.dll` |
| `./build.sh all --install "/path/to/game"` | Also installs into a game folder; repeat `--install` for more games |
| `./build.sh core --install "/path/to/game" --reload` | Also reloads the mod DLL in a running game |

`--install` picks the River of Time data if the folder contains `drakensanga1.exe`, and it keeps an existing `drakensang_mod.ini`.

On other distributions, set these environment variables to match your paths:

| Variable | Default |
|---|---|
| `CC` | `clang` |
| `LLD_LINK` | `lld-link` |
| `WINE_INCLUDE` | `/usr/include/wine` |
| `WINE_LIB` | `/usr/lib/wine/i386-windows` |

## Regenerate data

The level lists, cut content lists and whole Ferdok are generated from the game's own files.

```bash
export DRAKENSANG_DIR="/path/to/Drakensang"
export DRAKENSANG_ROT_DIR="/path/to/Drakensang The River of Time"

python3 tools/gen_levels.py d1 --out dist/drakensang
python3 tools/gen_extras.py d1 --out dist/drakensang
python3 tools/gen_composites.py --out dist/drakensang
python3 tools/gen_levels.py rot_a1 --out dist/river-of-time
python3 tools/gen_extras.py rot_a1 --out dist/river-of-time
```

Without `--out`, the files go to the game folder. If `tools/` sits inside `<game>/_mod`, that game is found without `DRAKENSANG_DIR`.

## Support a new game build

1. Set `dump_image=1` in `[dev]` and start the game. The mod writes the decrypted executable to `_mod/dump/`.
2. Install capstone with `pip install capstone`.
3. Find the addresses with the helpers in `tools/re/` and update the tables in `tools/re/gen_profiles.py`.
4. Run `python3 tools/re/gen_profiles.py` to regenerate `src/core/profiles.inc`.

Dumps contain decrypted game code, so don't publish them. `dump/` is already in `.gitignore`.

## Command channel

With `command_file=1`, the mod runs commands from `_mod/cmd.txt` and writes replies to `_mod/cmd_out.txt`. The `help` command lists them. Keep this off in normal play.

## Project layout

```
src/loader/   dinput8.dll: proxy and long-lived hooks
src/core/     drakensang_mod.dll: all features, reloadable while the game runs
src/shared/   settings and log
tools/        data generators, glTF export, Vintage Story converter
tools/re/     dump analysis and game profile generation
dist/         ready-to-install files
```
