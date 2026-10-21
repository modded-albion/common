/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <compare>

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>
#include <modzip/Zip.h>

namespace moa::modzip
{

/// The version of the format of `mod.json`.
constexpr int k_ModFormat = 1;
/// The file at the root of a mod zip that tells what the mod is.
constexpr std::string_view k_ModFile = "mod.json";
/// The folder of the map override in a mod zip: the manifest and the files of the override.
constexpr std::string_view k_MapFolder = "map";
/// The folder of the custom object packs in a mod zip: one folder for each pack, with its `pack.json`.
constexpr std::string_view k_PacksFolder = "packs";
/// The longest id of a mod.
constexpr size_t k_MaxModIdLength = 32;

/// The contents of `mod.json`, format 1:
///
///   {
///     "format": 1,
///     "id": "example-mod",                     (required: 1 to 32 of a-z, 0-9, "-" and "_"; it starts with a letter
///                                               or a digit)
///     "name": "Example mod",                   (required text, 1 to 64 characters)
///     "version": "1.0.0",                      (required: 1 to 4 numbers with "." between them; of two zips with
///                                               the same id, the newer version loads)
///     "author": "...",                         (optional text, up to 64 characters)
///     "description": "...",                    (optional text, up to 2000 characters)
///     "buildGuids": ["8a91370f..."],           (required: the game builds that the mod is for, 32 hex digits each)
///     "contents": {"map": true, "packs": ["broken_coffin"]},
///                                              (required: the map override in "map/", and the pack folders in
///                                               "packs/"; at least one of them)
///     "tool": "moa_editor", "made": "2026-10-20T12:00:00Z"  (optional text: what made the zip, and when)
///   }
///
/// The parser refuses a key that it does not know, at each level.
struct ModInfo
{
	std::string id;
	std::string name;
	std::string version;
	std::string author;
	std::string description;
	std::vector<std::string> buildGuids;
	bool map = false;
	std::vector<std::string> packs; ///< The names of the pack folders, sorted.
	std::string tool;
	std::string made;

	friend bool operator==(const ModInfo&, const ModInfo&) = default;
};

/// Return true if the text can be the id of a mod.
[[nodiscard]] bool IsValidModId(std::string_view id);

/// Return true if the text can be the version of a mod: 1 to 4 numbers of 0 to 999999, with "." between them, for
/// example "1", "1.2" or "1.2.10".
[[nodiscard]] bool IsValidModVersion(std::string_view version);

/// Compare two versions that `IsValidModVersion` accepts, number by number. A missing number is 0, so "1.2" is equal
/// to "1.2.0", and "1.10" is newer than "1.9".
[[nodiscard]] std::strong_ordering CompareModVersions(std::string_view a, std::string_view b);

/// Return true if the text can be the name of a pack folder in a mod zip: 1 to 64 letters, digits, "-", "_" and ".",
/// and not only dots.
[[nodiscard]] bool IsValidFolderName(std::string_view name);

/// Read and check the text of `mod.json`. An error message starts with the field, for example `buildGuids[0]: ...`.
[[nodiscard]] Result<ModInfo> ParseModInfo(std::string_view json);

/// Return the text of `mod.json`. `ParseModInfo` of the text gives the same value.
[[nodiscard]] std::string ModInfoJson(const ModInfo& info);

/// Refuse a mod that does not name this game build.
[[nodiscard]] Result<void> CheckModBuild(const ModInfo& info, std::string_view buildGuid);

/// Compare the files of a zip with `mod.json`. Refuse the zip if:
/// - `map` is true and `map/manifest.json` is missing, or `map` is false and the zip has files in `map/`,
/// - a pack of the list has no `packs/<pack>/pack.json`, or the zip has a folder in `packs/` that is not in the list,
/// - a file at the root is not `mod.json`, a ".txt" file or a ".md" file, or a file is directly in `packs/`.
[[nodiscard]] Result<void> CheckModContents(const ModInfo& info, std::span<const ZipEntry> entries);

} // namespace moa::modzip
