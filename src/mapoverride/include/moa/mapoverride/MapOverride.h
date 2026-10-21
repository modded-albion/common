/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <moa/common/Result.h>

namespace moa::mapoverride
{

// A map override is a folder of plain files with the edited map data, and a manifest. The research mod gives these
// files to the game in place of the data of the game. No file of the game changes. The manifest names the game build
// and the SHA-1 of each original item, so that the mod uses the override only on the data that the editor read.
// The format is in docs/research/mods/map-overrides.md.

/// The version of the manifest format.
constexpr int32_t k_FormatVersion = 1;
/// The name of the manifest in the folder of an override.
constexpr std::string_view k_ManifestFile = "manifest.json";
/// The longest name of an override.
constexpr size_t k_MaxNameLength = 64;

/// The kind of data that an item of an override changes.
enum class ItemKind : uint8_t
{
	Text,         ///< A TextAsset that the game reads as text: the start map, the districts, a flow script, a table.
	Heights,      ///< A rectangle of the height samples of the land.
	CameraFloor,  ///< The image data of the texture of the camera floor (all mip levels).
	CoastMap,     ///< The bytes of the TextAsset of the coast map.
	TreeHolders,  ///< The local heights of the TreeHolder objects of the scene.
	TerrainTrees, ///< The heights of the trees of the land data.
};

[[nodiscard]] std::string_view ItemKindName(ItemKind kind);
[[nodiscard]] std::optional<ItemKind> ParseItemKind(std::string_view name);

/// A rectangle of height samples: the first column and row, and the count of columns and rows.
struct SampleRect
{
	int32_t column = 0;
	int32_t row = 0;
	int32_t width = 0;
	int32_t height = 0;

	[[nodiscard]] size_t Count() const
	{
		return width <= 0 || height <= 0 ? 0 : static_cast<size_t>(width) * static_cast<size_t>(height);
	}
	friend bool operator==(const SampleRect&, const SampleRect&) = default;
};

/// One item of the manifest.
struct ManifestItem
{
	ItemKind kind = ItemKind::Text;
	std::string name;               ///< The name of the object of the game.
	std::vector<std::string> paths; ///< The resource paths that load the object (lower case, with `/`), or none.
	std::string file;               ///< The file of the item in the folder of the override (with `/`).
	std::string originalSha1;       ///< The SHA-1 of the original item, as `OriginalBytes` of the kind makes it.
	std::string sha1;               ///< The SHA-1 of the file.
	int32_t resolution = 0;         ///< The samples on each side of the land (heights only).
	SampleRect rect;                ///< The changed samples (heights only).
	size_t count = 0;               ///< The count of entries (tree items only).

	friend bool operator==(const ManifestItem&, const ManifestItem&) = default;
};

/// The manifest of an override.
struct Manifest
{
	int32_t format = k_FormatVersion;
	std::string buildGuid; ///< The build of the game data that the editor read (`build-guid` of boot.config).
	std::string tool;      ///< The program that wrote the override.
	std::string made;      ///< The time of the write, in UTC (ISO 8601).
	std::vector<ManifestItem> items;

	friend bool operator==(const Manifest&, const Manifest&) = default;
};

// ---- The writer ----

/// The new text of one TextAsset.
struct TextChange
{
	std::string name;
	std::vector<std::string> paths;
	std::string original;
	std::string text;
};

/// The new samples of a rectangle of the land. `before` and `after` have `rect.Count()` samples, row by row.
struct HeightsChange
{
	int32_t resolution = 0;
	SampleRect rect;
	std::vector<int16_t> before;
	std::vector<int16_t> after;
};

/// The new bytes of one object with the same size.
struct BytesChange
{
	std::string name;
	std::vector<std::string> paths;
	std::vector<std::byte> original;
	std::vector<std::byte> changed;
};

/// A new local height for a TreeHolder object, found by the name of its game object.
struct HolderMove
{
	std::string name;
	float before = 0.0f;
	float after = 0.0f;

	friend bool operator==(const HolderMove&, const HolderMove&) = default;
};

/// A new height for a tree of the land data, found by its index. The height is a part of the height scale (0 to 1).
struct TerrainTreeMove
{
	uint32_t index = 0;
	float before = 0.0f;
	float after = 0.0f;

	friend bool operator==(const TerrainTreeMove&, const TerrainTreeMove&) = default;
};

/// All the changes of an override. With no change, the override has only the manifest, with no items.
struct MapOverrideContent
{
	std::string buildGuid;
	std::string tool;
	std::string made;
	std::vector<TextChange> texts;
	std::optional<HeightsChange> heights;
	std::optional<BytesChange> cameraFloor;
	std::optional<BytesChange> coastMap;
	std::vector<HolderMove> holders;
	std::vector<TerrainTreeMove> terrainTrees;
};

/// One file of an override: the path in the folder (with `/`) and the bytes.
struct OverrideFile
{
	std::string path;
	std::vector<std::byte> bytes;
};

/// Make the files of an override, with the manifest last. Return an error if a change is not consistent (for
/// example a rectangle outside the land, or a byte change with two sizes). A text that is equal to the original is
/// not an item.
[[nodiscard]] Result<std::vector<OverrideFile>> BuildMapOverride(const MapOverrideContent& content);

/// Write the files of an override to `mapsFolder/name`. The function writes to a new folder first, and then puts it in
/// place of an older override with the same name. It refuses a folder with that name that is not an override, a name
/// that `IsValidMapName` refuses, and a folder of the game.
[[nodiscard]] Result<std::filesystem::path> WriteMapOverride(const std::filesystem::path& mapsFolder, std::string_view name,
                                                             std::span<const OverrideFile> files);

// ---- The reader and the checks ----

/// Read the manifest. Return an error if the JSON is not correct, the format is not known, or an item is not
/// consistent (a file outside the folder, a bad hash, a rectangle outside the land).
[[nodiscard]] Result<Manifest> ParseManifest(std::string_view json);

/// Return the text of a manifest. `ParseManifest` of the text gives the same manifest.
[[nodiscard]] std::string ManifestJson(const Manifest& manifest);

/// Refuse a manifest that was made for another game build.
[[nodiscard]] Result<void> CheckBuild(const Manifest& manifest, std::string_view buildGuid);

/// Refuse the file of an item if its SHA-1 is not the SHA-1 in the manifest.
[[nodiscard]] Result<void> CheckFile(const ManifestItem& item, std::span<const std::byte> bytes);

/// Refuse an item if the original data of the game now is not the data that the editor read. `original` is the
/// data of the game in the form of `OriginalBytes` of the kind.
[[nodiscard]] Result<void> CheckOriginal(const ManifestItem& item, std::span<const std::byte> original);

/// The samples of a heights file: `rect.Count()` samples of 16 bits, little endian, row by row.
[[nodiscard]] Result<std::vector<int16_t>> ReadHeights(const ManifestItem& item, std::span<const std::byte> bytes);
[[nodiscard]] std::vector<std::byte> HeightBytes(std::span<const int16_t> samples);

/// The tree files: a JSON array of moves.
[[nodiscard]] Result<std::vector<HolderMove>> ParseHolderMoves(std::string_view json);
[[nodiscard]] Result<std::vector<TerrainTreeMove>> ParseTerrainTreeMoves(std::string_view json);

/// The original form of the tree items for `CheckOriginal`: for each move, the name (UTF-8) and a zero byte, or the
/// index (32 bits), and then the height before the edit (a 32-bit float), little endian.
[[nodiscard]] std::vector<std::byte> HolderOriginalBytes(std::span<const HolderMove> moves);
[[nodiscard]] std::vector<std::byte> TerrainTreeOriginalBytes(std::span<const TerrainTreeMove> moves);

/// Return true if a name can be the name of an override folder: 1 to `k_MaxNameLength` letters, digits, `-`, `_` and
/// `.`, and not only dots.
[[nodiscard]] bool IsValidMapName(std::string_view name);

/// Return the `build-guid` of the text of a `boot.config` file, or no value.
[[nodiscard]] std::optional<std::string> ReadBootConfigBuildGuid(std::string_view text);

/// Return a resource path in the form that the game compares: lower case, with `/`.
[[nodiscard]] std::string NormalResourcePath(std::string_view path);

/// The folder of the overrides in the local application data folder of the user.
[[nodiscard]] std::filesystem::path MapsFolderIn(const std::filesystem::path& localAppData);

} // namespace moa::mapoverride
