# Validation

Checked on Windows with MSVC x86, Python and the pinned dependencies:

- Clean CMake/Ninja configuration and build of GothicCS.dll and UnionAPI.dll.
- Offline movement and weapon tests passed without any game assets.
- Optional surf test compiled; not run against a game world dump.
- Python converter modules compiled and CLI help was checked.
- Source tree scanned for local user paths, likely credentials and game/binary assets.

Not yet verified: bot pathfinding, loose-DLL loading in a fresh Gothic
installation, and full asset conversion on another machine.
