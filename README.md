# Which Plugin - Console Search

An SKSE plugin that adds the console command `wp`: find loaded plugins, load-order slots and records
without leaving the game. It looks at the game as it is running, so it also finds records that no
plugin lists, such as ones created by other SKSE plugins or scripts.

Download: [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/198515) or
[GitHub releases](https://github.com/cecell/skyrim-which-plugin/releases).

## Examples

```
wp                                     list every loaded plugin
wp patch                               plugins whose name contains "patch"
wp 00012EB7                            which plugin owns a form ID; master, last edit, editor ID
wp edid~"^Iron.*Sword$"                records whose editor ID matches a regex
wp type=WEAP && plugin-name:dawn       weapons last edited by a plugin with "dawn" in its name
wp type=SPEL && !plugin-name~"."       spells no plugin lists (made at runtime)
wp name:"iron sword"                   records by in-game name, no editor IDs needed
wp refs:IronSword                      iron swords placed in the loaded cells
wp contents:LItemWeaponSword           what a leveled list can give, nested lists too
wp -d keyword:WeapTypeSword > swords   one aligned row each, written to a file
wp open last                           open that file
```

## Full syntax

This is what `wp help` prints in the console:

```
WhichPlugin (wp) v0.91 - find loaded plugins, load-order slots and records
  wp                              list every loaded plugin
  wp patch                        plugin name contains, or slot starts with (if hex)
  wp FE                           light plugins + names containing "fe"
  wp 00012EB7                     which plugin owns a form ID; master, last edit, editor ID
Combine:  && = AND   || = OR (AND binds first)   ! = NOT   e.g. wp "cc" && !"esl"
Fields and how they match  (: contains   = exact   ~ regex):
  plugin-name:dawn                plugin name / for records: last plugin to edit it
  plugin-master:"Dawnguard"       plugin that has it as a master / for records: the master
  lobytes:FE0                     slot / form ID starts with (FE0 = FE000-FE0FF)
  formid:12EB7   formid~"^0A"     a form ID (leading zeros optional)
  edid:iron   edid=IronSword   edid~"^Iron.*Sword$"
  type=WEAP   type~"^(ARMO|WEAP)$"   record type
  keyword:WeapTypeSword   keyword:0001E711   keyword~"^WeapType"
  name:"iron sword"   name~"^Daedric"   in-game name (works without editor IDs)
  refs:IronSword                  placed references of a record (loaded cells only)
  base:00000014                   the base record of a reference (14 = the player)
  contents:LItemWeaponSword       what a leveled list or form list holds, nested lists too
Using formid, edid, name, type, keyword, refs, base or contents searches records;
  plugin fields then filter them.
  e.g. wp edid~"^Iron" && plugin-master:dawnguard
Options (first on the line):  -c/--concise  -d/--detail  -l/--lo-agnostic  --int   (-- ends options)
  -c  IDs only      -d  one aligned row each      -l  0x00E8F0, "Dawnguard.esm"      --int  decimal
Quotes: "..." is taken literally. In '...' write \' for an apostrophe.
Output (at the end of the line):
  > name     write to My Games\Skyrim Special Edition\wp\name(.txt), text file types only
  >! name    replace the file even when bOverwriteNeedsConfirm is on
  >> name    append to that file;  >> alone appends to the last file used
  >>>        copy the output to the clipboard
  |          pipe into text tools (grep, sed, sort... coming soon)
Actions:
  wp open          the output folder           wp open last   the last file written
  wp help --web    this help on GitHub         wp json        a JSON checker in the browser
  wp uesp WEAP     UESP's page on a record type (or give a form ID)
  To search for plugins named like an action, quote it: wp "open"
Settings: Data\SKSE\Plugins\WhichPlugin.ini
```

`wp` also works in bat files run with `bat`, and alongside Console Command Extender.

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Optional: [powerofthree's Tweaks](https://www.nexusmods.com/skyrimspecialedition/mods/51073) with
  editor ID loading on. Without it, searching by editor ID finds only the few record types the game
  keeps editor IDs for.

Developed and tested on Skyrim 1.6.1170. The DLL also targets other SE and AE versions, but they
haven't been tested. VR isn't supported.

## Installing

Install the release zip with a mod manager, or copy its `SKSE` folder into the game's `Data` folder.
It holds `SKSE/Plugins/WhichPlugin.dll`, its `.pdb` (so crash loggers can name functions), the
settings file `WhichPlugin.ini` and a `WhichPlugin` folder with the licences. There is no plugin file. The log is
`Documents\My Games\Skyrim Special Edition\SKSE\WhichPlugin.log`.

## Settings

`Data\SKSE\Plugins\WhichPlugin.ini` ([the default](config/WhichPlugin.ini)) is read when the game starts:

| Setting | Default | What it does |
|---|---|---|
| `sShortCommand` | `wp` | The short console command, if another mod already uses `wp`. `WhichPlugin` always works |
| `sLogLevel` | `info` | How much goes into `WhichPlugin.log` |
| `iMaxConsoleLines` | `100` | Result lines the console shows. Saving to a file gets every line |
| `sDefaultLayout` | `normal` | `normal`, `concise` (like `-c`) or `detail` (like `-d`) |
| `bWriteFileAnywhere` | `0` | `1` allows full paths and `..` after `>` |
| `bOverwriteNeedsConfirm` | `0` | `1`: `>` won't replace an existing file; `>!` does |
| `bAllowOpenFolders` | `1` | `wp open` and `wp open last` |
| `bAllowOpenURL` | `1` | `wp help --web`, `wp uesp` and `wp json` |

Output files go to `Documents\My Games\Skyrim Special Edition\wp`, and only text file types can be
written.

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
summary line, then `## ` sections. Pushing a `v<version>` tag makes GitHub Actions build the DLL,
package and hash it, scan it on VirusTotal, publish a GitHub release with those exact files and upload
the zip to Nexus Mods as the new version of the page, with the changelog taken from that file.

## License

[GPL-3.0](LICENSE). Third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
