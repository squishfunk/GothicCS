# GothicCS

Counter-Strike 1.6-style movement and combat inside Gothic I. Play as the
Nameless Hero with first-person weapons, bunnyhopping, a buy menu, grenades,
C4 and round-based fights against Gothic NPCs.

**Experimental Windows/x86 Union plugin.** This is a source distribution,
not a complete game or a ready-made asset pack. Bot navigation and full
in-game compatibility still need testing. No Gothic or Counter-Strike assets
are included. See [third-party notices](THIRD_PARTY_NOTICES.md) before redistribution.

## Requirements

- Gothic I, with a compatible **1.08k_mod executable and Union** installed.
  Gothic II and other executable versions are not supported by this build.
- Your own installed Counter-Strike 1.6 files (`cstrike`) and the accompanying
  Half-Life shared files (`valve`). These are required for models, audio and HUD.
- Windows, Git, Visual Studio 2022 Build Tools with C++ desktop tools and a
  Windows SDK, CMake 3.21+ and Ninja.
- Python 3.10+ with NumPy and Pillow.

Use a separate game installation and back up saves before testing. Do not run
GothicCS alongside other plugins that take over movement or combat.

## Build and install

Open **x86 Native Tools Command Prompt for VS 2022**, start `powershell`, and
change to the repository directory. All commands below run from that directory.
The paths below are examples: replace them with your own installations.

### 1. Download pinned build dependencies

```powershell
powershell -ExecutionPolicy Bypass -File tools/setup-dependencies.ps1
```

This downloads Union API and Gothic API at fixed commits and initializes their
submodules. Downloaded sources remain ignored by this repository.

### 2. Build the 32-bit plugin

```powershell
cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release -DGOTHIC_API_G1=ON
cmake --build build/plugin
powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1
```

The default installation uses loose DLLs; GothicVDFS is not required.
The offline tests cover movement and weapon behavior, not Gothic integration.
The optional viewmodel test is skipped without an explicit converted model path.

### 3. Convert your local CS assets

Close Gothic before conversion or installation.

```powershell
py -m venv .venv
.venv/Scripts/python.exe -m pip install -r requirements.txt
.venv/Scripts/python.exe tools/convert_cs.py --gothic "C:/Games/Gothic" --cstrike "C:/Games/Half-Life/cstrike" --valve "C:/Games/Half-Life/valve"
```

The converter reads CS files and writes generated assets under Gothic's
`_work/Data` directory. It does not modify the CS installation. Do not upload
these outputs, game files, or converted models/audio/textures to this repository.
`--valve` may be omitted when `valve` is next to `cstrike`.

### 4. Install the plugin

```powershell
powershell -ExecutionPolicy Bypass -File tools/install.ps1 -GothicPath "C:/Games/Gothic"
```

The script copies `GothicCS.dll` and `UnionAPI.dll` to `System/Autorun` and backs
up existing DLLs before replacement. Union must already be installed.
Disable old `GothicCS.vdf`, GothicSkate and GothicMetin packages first; the
installer rejects these active packages to avoid loading two movement plugins.

Start Gothic, load a save, then press **Insert** to enter CS mode.

## Controls

| Key | Action |
| --- | --- |
| Insert | Toggle CS mode |
| WASD / mouse | Move / look |
| Space | Jump; hold for automatic bunnyhopping |
| Left Ctrl / Left Shift | Crouch / walk |
| Left / right mouse | Fire / alternate action (silencer, burst, zoom) |
| R | Reload |
| 1–5 | Primary, pistol, knife, grenades, C4 |
| Q / G | Previous weapon / drop weapon |
| B / number keys / 0 | Buy menu / select / back or close |
| Comma / period | Buy primary / secondary ammunition |
| E | Hold to defuse as CT |
| M | Switch T/CT |
| K | Spawn an enemy bot |
| End | Set bombsite A, then B, then clear both |
| Home | Toggle debug overlay |
| Page Up or O | Toggle godmode; indicator shown only in debug |
| Numpad 8 | Add $1000, capped at $16000; Num Lock must be on |
| V | Toggle noclip |
| Ctrl + Page Up / Page Down | Increase / decrease mouse sensitivity |
| Shift + Page Up | Cycle weapon rendering mode while debug is visible |

Godmode, noclip, money grants and bot spawning are development aids and are
currently available without a separate cheat mode.

## Bomb objective and guards

The player starts as T with C4. Select slot 5 and hold primary fire for three
seconds while on the ground. The fuse lasts 45 seconds.

For an Old Camp attack, set a site with End inside the camp. Exit CS mode,
move to your desired attacker spawn, and enter CS mode again. Up to three
NPC defenders are placed around each saved site where ground and line of sight
checks succeed. Sites are saved per world in `System/GothicCS_bombsites.ini`.

Guards react after 0.45–0.8 seconds, have 100 health and 50 armor without a
helmet, pursue the last seen position briefly, and return to their posts.
They approach planted C4 and require ten uninterrupted seconds to defuse.
With a configured site, eliminating the defenders does not replace the bombing
objective. The CT player mode is experimental: enemy bots do not plant bombs.

**Navigation limitation:** bots move directly toward destinations; they do not
plan paths around buildings. They can get stuck, and distant bots are limited
by the collision region around the player. This is not a finished tactical AI.

## Configuration

Create or edit `System/GothicCS.ini` while the game is closed:

```ini
[GothicCS]
Sensitivity=3.0
StartMoney=4000
BombAnywhere=1
GodMode=0
AutoBunnyhop=1
```

`BombAnywhere=1` allows planting anywhere on the ground. Set it to `0` to
require a configured site (the source default). Settings load on game startup.
Defaults include a six-second freeze period, five-minute rounds and a
90-second buy period near the attacker spawn.

## Troubleshooting and removal

- **Nothing happens on Insert:** check the executable version, Union installation
  and the two DLLs in `System/Autorun`. Avoid duplicate VDF and loose DLL installs.
- **Missing weapons/audio/HUD:** verify CS files and rerun the complete converter,
  without `--only`. The repository does not include replacement game assets.
- **Wrong mod starts:** disable competing VDF packages; restarting the game is required.
- **Deaths when landing:** fall damage is enabled. Page Up toggles godmode for testing.
- **No defenders:** save a site and re-enter CS mode as T; K can spawn one manually.

To remove the plugin, close Gothic and remove only `System/Autorun/GothicCS.dll`.
Do not remove a shared UnionAPI.dll required by other plugins; restore its backup
if needed. To remove generated assets:

```powershell
.venv/Scripts/python.exe tools/convert_cs.py --gothic "C:/Games/Gothic" --uninstall
```

This removes files with this converter's reserved prefixes and its GothicCS
asset directory. It does not remove your saved configuration.

## Repository layout

- `plugin/src`: engine integration and engine-independent gameplay code.
- `plugin/cmake`: build helpers inherited from the Union plugin template.
- `tools`: dependency setup, conversion and installation.
- `tests`: offline movement, weapon and optional world-geometry tests.
- `licenses`: retained third-party license texts.

No game files, converted assets, binaries, personal configuration or build
outputs belong in commits. See [validation](VALIDATION.md) for the checks run
and known limitations.

## License

GothicCS is released under the [MIT License](LICENSE). Third-party components
keep their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
