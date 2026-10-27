# Masters of Albion: common

The shared code of the mod loader and the mods of Masters of Albion.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of Masters of Albion Reforged.

```bat
tools\preset.bat x64-release
```

```sh
git submodule add *repo_url* external/common
```

```cmake
add_subdirectory(external/common)
target_link_libraries(my_mod PRIVATE moa_mod_sdk moa_mod_game)
```

## Tests

 `ctest --preset x64-release`
