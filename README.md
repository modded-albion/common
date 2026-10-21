# Masters of Albion mods: common

The shared code of the mod loader and the mods of Masters of Albion (22cans, Unity 6000.5.7f1, IL2CPP). The mod
loader (`mod_loader`) and each mod (`improved_camera`, `better_skybox`) include this repository as a git submodule.

The code comes from the research mod of Masters of Albion Reforged (`tools/research_mod` and the pure components of
that repository). That repository stays as it is.

## What is in it

| Library | Folder | What it does |
|---|---|---|
| `moa_common` | `src/common` | `moa::Result`, the binary reader and the export paths (headers only) |
| `moa_astro` | `src/astro` | The moon, the sun and the star table |
| `moa_modzip` | `src/modzip` | The mod zips: a zip reader and writer in memory (miniz) and `mod.json` |
| `moa_mapoverride` | `src/mapoverride` | The map overrides of the editor and their checks |
| `moa_mod_core` | `src/mod_core` | Pure logic: frame callbacks, main-thread tasks, the slot swap of a code pointer, hotkeys, the settings file, the menu field tables, the start-up guard, the crash dump rules and the Unity maths |
| `moa_mod_api` | `src/mod_api` | `mod_api/ModApi.h`: the C interface between the loader and a mod DLL |
| `moa_mod_sdk` | `src/mod_sdk` | The C++ side of that interface in a mod DLL (`ModHost`, `BindFields`, the log sink) |
| `moa_mod_game` | `src/mod_game` | Windows only. The parts that run in the game: IL2CPP, managed objects, method hooks, the hotkey input, the messages of the game, crash dumps and the mod status |

Each library has one folder under `src`, with its headers and sources directly in it. The folder name is the
target name without `moa_`. Each library puts `src` on the include path, so an include line names the library folder
and the file, for example `#include <common/Result.h>`, `#include <modzip/ModInfo.h>` or
`#include <mod_core/Hotkey.h>`. The folder names of common start with `mod_` where a mod could have the same name
(`core`, `game`), so an include path never finds two files. The C++ namespaces (`moa`, `moa::astro`, `moa_mod`, ...)
do not follow the folders.

## The interface between the loader and a mod

`src/mod_api/ModApi.h` is a versioned C interface (`MOA_MOD_API_VERSION`). Only plain C types go
across it, because the loader and each mod have their own C runtime and their own copy of the standard library.

A mod DLL exports `MoaModApiVersion`, `MoaModGetInfo`, `MoaModInit` and `MoaModShutdown`. The loader refuses a mod
with a different interface version. If `MoaModInit` returns 0, the loader removes the registrations of the mod,
unloads it, writes the reason to the log, and the game continues.

The table of the loader (`MoaHostApi`) gives the log, method callbacks (before or after a managed method), tasks on
the main thread, the sections of `mods.json`, menu fields, actions with hotkeys, readouts, a status reader, the
messages of the game and the inspector queries.

### Inspector queries

A mod can add named queries (for example `sky.state`) with `addQuery`: a C function that gets the parameters as JSON
text and gives the answer as JSON text through `MoaText`. The loader calls a query on the main thread. The inspector
mod (`inspector`) lists the queries of all mods with `listQueries` and calls one with `callQuery`, so the state of
each mod stays in its own DLL. In C++, `ModHost::AddQuery`, `ListQueries` and `CallQuery` do this.

The query functions came after the first loader of version 1, so `MOA_MOD_API_VERSION` stays 1. They are at the end
of `MoaHostApi`, and a mod checks `size` with `MOA_HOST_API_HAS` before it uses them. `ModHost::Check` accepts a table
of the first loader (`MOA_HOST_API_FIRST_SIZE`); `ModHost::HasQueries` then returns false and `AddQuery` refuses. An
older mod works with a newer loader with no change.

Each mod finds the IL2CPP exports itself with `moa_mod_game`. Those exports are the C interface of the game and have
no state of the loader. The loader owns each hook: a mod asks for a callback on a managed method, and the loader
installs one hook for each method and calls the callbacks of all mods in order. Two mods thus never hook the same
function blindly.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of Masters of Albion Reforged.

```bat
tools\preset.bat x64-release
```

The script prepares the MSVC x64 environment, then configures, compiles and tests the preset `x64-release`
(Ninja, MSVC, RelWithDebInfo). The steps are also available alone: `tools\preset.bat x64-release configure`,
`compile` or `test`.

The dependencies come from vcpkg in manifest mode (`vcpkg.json`): nlohmann-json, spdlog (with fmt) and GoogleTest,
with the triplet `x64-windows-static`. Every module that goes into the game folder uses the static C runtime, so no
other DLL goes there. The presets use:

- `VCPKG_ROOT` = `D:/masters_of_albion/masters_of_albion_reforged/vcpkg` (the vcpkg checkout of the reforged
  repository; this repository has no vcpkg submodule),
- `VCPKG_DEFAULT_BINARY_CACHE` = `D:/masters_of_albion/cache/vcpkg-archives`,
- `VCPKG_DOWNLOADS` = `D:/masters_of_albion/cache/vcpkg-downloads`.

miniz compiles from its source archive (`cmake/Miniz.cmake`), from the vcpkg download cache, with a fixed SHA-512.

## Use in a mod project

```sh
git -c protocol.file.allow=always submodule add D:/masters_of_albion/projects/common external/common
```

```cmake
add_subdirectory(external/common)
target_link_libraries(my_mod PRIVATE moa_mod_sdk moa_mod_game)
```

The submodule URL is the absolute local path `D:/masters_of_albion/projects/common`. Git 2.43 refuses local file
submodules by default, so each submodule command needs `-c protocol.file.allow=always`. The project of the mod sets
`CMAKE_MSVC_RUNTIME_LIBRARY` to the static C runtime and uses the same vcpkg triplet.

## Tests

The tests (GoogleTest, `tests/`) cover the pure parts with fakes, never with game data. `ctest --preset x64-release`
runs them.
