# Third-party notices and provenance

This is an unofficial experimental project, not affiliated with the Gothic,
Counter-Strike, Valve or Union teams. Game names identify compatibility.
No game assets or proprietary game executables are distributed here.

## Gameplay reference

Weapon parameters and gameplay behavior were researched from
[ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS). Its contributor license
is retained in `licenses/ReGameDLL_CS.txt`. The gameplay code describes itself
as a separate implementation; movement describes itself as independently
implemented rather than copied from Half-Life SDK `pm_shared`.
No Half-Life SDK sources are vendored.

## Build dependencies

[Union API](https://gitlab.com/union-framework/union-api) and
[Gothic API](https://gitlab.com/union-framework/gothic-api) are downloaded by
`tools/setup-dependencies.ps1` at pinned revisions. Their license texts are
retained in `licenses`. Union API also downloads Detours, zlib, libogg and
libvorbis. Their source distributions contain their own license notices;
retain those notices if distributing built libraries. This repository does
not vendor these libraries or their binaries.

## Plugin template

The plugin scaffold (`plugin/src/DllMain.cpp`, `plugin/src/IppFileTemplate.in`)
and CMake helpers in `plugin/cmake` originate from
[Patrix9999/union-plugin-template](https://github.com/Patrix9999/union-plugin-template).
Credit for these files goes to their original author.

## Assets

Conversion requires the user's own Counter-Strike and Half-Life installations.
Possession of game files is not a license to redistribute converted assets.
The converter's outputs must stay out of source and release packages unless
separate permission has been established. Gothic must also be obtained and
installed separately.
