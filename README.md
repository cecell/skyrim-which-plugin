# Which Plugin - Console Search

An SKSE plugin that adds the console command `wp`: find loaded plugins, load-order slots and records
without leaving the game. Type `wp help` in the console for the full syntax.

```
wp                               list every loaded plugin
wp patch                         plugin name contains, or slot starts with (if hex)
wp 00012EB7                      which plugin owns a form ID; master, last edit, editor ID
wp edid~"^Iron.*Sword$"          search records by editor ID
wp type=WEAP && plugin-name:dawn combine searches with && || !
wp -d keyword:WeapTypeSword > swords    write one aligned row each to a file
```

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Optional: [powerofthree's Tweaks](https://www.nexusmods.com/skyrimspecialedition/mods/51073) with
  editor ID loading on. Without it, searching by editor ID finds only the few record types the game
  keeps editor IDs for.

Developed and tested on Skyrim 1.6.1170. The DLL also targets other SE and AE versions, but they
haven't been tested. VR isn't supported.

## Building

MSVC 14.44, CMake, Ninja and vcpkg, from a `vcvars64.bat -vcvars_ver=14.44` prompt:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DVCPKG_HOST_TRIPLET=x64-windows-static-md ^
  -DVCPKG_OVERLAY_TRIPLETS=triplets
cmake --build build
```

CommonLibSSE-NG is downloaded at the commit pinned in `CMakeLists.txt`. Set `COMMONLIB_DIR` to use a
local checkout instead.

## Releases and changelog

The version lives in `CMakeLists.txt`. Each version's changelog is `release_notes/<version>.md`: a
summary line, then `## ` sections. Pushing a `v<version>` tag builds, scans and publishes it to GitHub
and Nexus Mods, and the Nexus changelog comes from that file.

## License

[GPL-3.0](LICENSE). Third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
