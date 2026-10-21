find_package(GTest CONFIG REQUIRED)

add_executable(
  moa_common_tests
  test_Astro.cpp
  test_AstroSky.cpp
  test_BinaryReader.cpp
  test_CrashDumpRules.cpp
  test_Hotkey.cpp
  test_MapOverride.cpp
  test_ModCore.cpp
  test_ModHost.cpp
  test_ModZip.cpp
  test_SettingsFields.cpp
  test_SettingsFile.cpp
  test_StartupGuard.cpp
)
target_link_libraries(
  moa_common_tests
  PRIVATE moa_common
          moa_astro
          moa_modzip
          moa_mapoverride
          moa_mod_core
          moa_mod_sdk
          nlohmann_json::nlohmann_json
          GTest::gtest_main
)
moa_set_warnings(moa_common_tests)

include(GoogleTest)
gtest_discover_tests(moa_common_tests DISCOVERY_MODE PRE_TEST)
