/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa/mapoverride/MapOverride.h"

#include <cctype>
#include <cstring>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <fstream>
#include <limits>
#include <set>
#include <utility>

#include <moa/common/ExportPath.h>
#include <nlohmann/json.hpp>

#include "moa/mapoverride/Sha1.h"

namespace moa::mapoverride
{

namespace
{
using Json = nlohmann::json;

constexpr std::array<std::pair<ItemKind, std::string_view>, 6> k_KindNames {{
    {ItemKind::Text, "text"},
    {ItemKind::Heights, "heights"},
    {ItemKind::CameraFloor, "cameraFloor"},
    {ItemKind::CoastMap, "coastMap"},
    {ItemKind::TreeHolders, "treeHolders"},
    {ItemKind::TerrainTrees, "terrainTrees"},
}};

/// The largest land that an override can change: the samples on each side.
constexpr int32_t k_MaxResolution = 1 << 15;

[[nodiscard]] std::vector<std::byte> ToBytes(std::string_view text)
{
	const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
	return {bytes.begin(), bytes.end()};
}

void AppendFloat(std::vector<std::byte>& bytes, float value)
{
	const auto bits = std::bit_cast<uint32_t>(value);
	for (int shift = 0; shift < 32; shift += 8)
	{
		bytes.push_back(static_cast<std::byte>((bits >> shift) & 0xffu));
	}
}

void AppendUint32(std::vector<std::byte>& bytes, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
	{
		bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
	}
}

[[nodiscard]] bool IsHexHash(std::string_view text)
{
	return text.size() == 40 &&
	       std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

/// A file of an item stays in the folder of the override: a relative path with no `..`, no drive and no `\`.
[[nodiscard]] bool IsSafeRelativePath(std::string_view path)
{
	if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos ||
	    path.find(':') != std::string_view::npos)
	{
		return false;
	}
	size_t start = 0;
	while (start <= path.size())
	{
		const auto end = std::min(path.find('/', start), path.size());
		const auto part = path.substr(start, end - start);
		if (part.empty() || part == "." || part == "..")
		{
			return false;
		}
		start = end + 1;
	}
	return true;
}

/// Make a file name from an object name: letters, digits, `-`, `_` and `.`; other characters become `_`.
[[nodiscard]] std::string FileStem(std::string_view name)
{
	std::string stem;
	for (const char c : name.substr(0, k_MaxNameLength))
	{
		const auto u = static_cast<unsigned char>(c);
		stem.push_back(std::isalnum(u) != 0 || c == '-' || c == '_' ? c : '_');
	}
	return stem.empty() ? std::string("item") : stem;
}

[[nodiscard]] Json ItemJson(const ManifestItem& item)
{
	Json json {
	    {"kind", ItemKindName(item.kind)},   {"name", item.name}, {"paths", item.paths}, {"file", item.file},
	    {"originalSha1", item.originalSha1}, {"sha1", item.sha1},
	};
	if (item.kind == ItemKind::Heights)
	{
		json["resolution"] = item.resolution;
		json["rect"] = {
		    {"column", item.rect.column}, {"row", item.rect.row}, {"width", item.rect.width}, {"height", item.rect.height}};
	}
	if (item.kind == ItemKind::TreeHolders || item.kind == ItemKind::TerrainTrees)
	{
		json["count"] = item.count;
	}
	return json;
}

[[nodiscard]] Result<void> CheckRect(int32_t resolution, const SampleRect& rect)
{
	if (resolution <= 0 || resolution > k_MaxResolution)
	{
		return MakeError("the land has a resolution of {}", resolution);
	}
	if (rect.width <= 0 || rect.height <= 0 || rect.column < 0 || rect.row < 0 || rect.column > resolution - rect.width ||
	    rect.row > resolution - rect.height)
	{
		return MakeError("the rectangle ({}, {}, {} x {}) is not in the land of {} samples", rect.column, rect.row, rect.width,
		                 rect.height, resolution);
	}
	return {};
}

[[nodiscard]] Result<void> CheckItem(const ManifestItem& item)
{
	if (!IsSafeRelativePath(item.file))
	{
		return MakeError("the file '{}' is not a path in the folder of the override", item.file);
	}
	if (!IsHexHash(item.sha1) || !IsHexHash(item.originalSha1))
	{
		return MakeError("the item '{}' has a SHA-1 that is not 40 hexadecimal digits", item.file);
	}
	for (const auto& path : item.paths)
	{
		if (path.empty() || path != NormalResourcePath(path))
		{
			return MakeError("the item '{}' has the resource path '{}', which is not in lower case with '/'", item.file, path);
		}
	}
	switch (item.kind)
	{
	case ItemKind::Text:
	case ItemKind::CameraFloor:
	case ItemKind::CoastMap:
		if (item.paths.empty())
		{
			return MakeError("the item '{}' has no resource path", item.file);
		}
		break;
	case ItemKind::Heights:
		if (auto rect = CheckRect(item.resolution, item.rect); !rect)
		{
			return MakeError("the heights item '{}': {}", item.file, rect.GetError().message);
		}
		break;
	case ItemKind::TreeHolders:
	case ItemKind::TerrainTrees:
		break;
	}
	return {};
}

template <typename T>
[[nodiscard]] bool Read(const Json& object, const char* key, T& value)
{
	const auto found = object.find(key);
	if (found == object.end())
	{
		return false;
	}
	try
	{
		value = found->get<T>();
		return true;
	}
	catch (const Json::exception&)
	{
		return false;
	}
}

[[nodiscard]] std::string ItemFile(ItemKind kind, size_t index, std::string_view name)
{
	switch (kind)
	{
	case ItemKind::Text:
		return std::format("text/{:03}-{}.txt", index, FileStem(name));
	case ItemKind::Heights:
		return "land/heights.bin";
	case ItemKind::CameraFloor:
		return "land/camera-floor.bin";
	case ItemKind::CoastMap:
		return "land/coast-map.bin";
	case ItemKind::TreeHolders:
		return "land/tree-holders.json";
	case ItemKind::TerrainTrees:
		return "land/terrain-trees.json";
	}
	return "item.bin";
}

[[nodiscard]] std::vector<std::string> NormalPaths(std::span<const std::string> paths)
{
	std::vector<std::string> normal;
	for (const auto& path : paths)
	{
		auto value = NormalResourcePath(path);
		if (!value.empty() && std::ranges::find(normal, value) == normal.end())
		{
			normal.push_back(std::move(value));
		}
	}
	return normal;
}

[[nodiscard]] Result<std::vector<std::byte>> ReadFile(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
	{
		return MakeError("{} does not open", path.string());
	}
	std::vector<char> data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	std::vector<std::byte> bytes(data.size());
	std::memcpy(bytes.data(), data.data(), data.size());
	return bytes;
}
} // namespace

std::string_view ItemKindName(ItemKind kind)
{
	for (const auto& [value, name] : k_KindNames)
	{
		if (value == kind)
		{
			return name;
		}
	}
	return "unknown";
}

std::optional<ItemKind> ParseItemKind(std::string_view name)
{
	for (const auto& [value, text] : k_KindNames)
	{
		if (text == name)
		{
			return value;
		}
	}
	return std::nullopt;
}

std::vector<std::byte> HeightBytes(std::span<const int16_t> samples)
{
	std::vector<std::byte> bytes;
	bytes.reserve(samples.size() * 2);
	for (const auto sample : samples)
	{
		const auto bits = static_cast<uint16_t>(sample);
		bytes.push_back(static_cast<std::byte>(bits & 0xffu));
		bytes.push_back(static_cast<std::byte>(bits >> 8));
	}
	return bytes;
}

std::vector<std::byte> HolderOriginalBytes(std::span<const HolderMove> moves)
{
	std::vector<std::byte> bytes;
	for (const auto& move : moves)
	{
		const auto name = ToBytes(move.name);
		bytes.insert(bytes.end(), name.begin(), name.end());
		bytes.push_back(std::byte {0});
		AppendFloat(bytes, move.before);
	}
	return bytes;
}

std::vector<std::byte> TerrainTreeOriginalBytes(std::span<const TerrainTreeMove> moves)
{
	std::vector<std::byte> bytes;
	for (const auto& move : moves)
	{
		AppendUint32(bytes, move.index);
		AppendFloat(bytes, move.before);
	}
	return bytes;
}

Result<std::vector<OverrideFile>> BuildMapOverride(const MapOverrideContent& content)
{
	Manifest manifest {
	    .format = k_FormatVersion, .buildGuid = content.buildGuid, .tool = content.tool, .made = content.made, .items = {}};
	std::vector<OverrideFile> files;
	const auto add = [&](ManifestItem item, std::vector<std::byte> bytes, std::span<const std::byte> original) {
		item.sha1 = Sha1Hex(bytes);
		item.originalSha1 = Sha1Hex(original);
		files.push_back({.path = item.file, .bytes = std::move(bytes)});
		manifest.items.push_back(std::move(item));
	};
	for (const auto& text : content.texts)
	{
		if (text.text == text.original)
		{
			continue;
		}
		auto paths = NormalPaths(text.paths);
		if (paths.empty())
		{
			return MakeError("the text '{}' has no resource path", text.name);
		}
		ManifestItem item {.kind = ItemKind::Text,
		                   .name = text.name,
		                   .paths = std::move(paths),
		                   .file = ItemFile(ItemKind::Text, manifest.items.size(), text.name)};
		const auto original = ToBytes(text.original);
		add(std::move(item), ToBytes(text.text), original);
	}
	if (content.heights)
	{
		const auto& heights = *content.heights;
		if (auto rect = CheckRect(heights.resolution, heights.rect); !rect)
		{
			return rect.GetError();
		}
		if (heights.before.size() != heights.rect.Count() || heights.after.size() != heights.rect.Count())
		{
			return MakeError("the heights have {} and {} samples, not {}", heights.before.size(), heights.after.size(),
			                 heights.rect.Count());
		}
		if (heights.before != heights.after)
		{
			ManifestItem item {.kind = ItemKind::Heights,
			                   .name = "heights",
			                   .file = ItemFile(ItemKind::Heights, 0, {}),
			                   .resolution = heights.resolution,
			                   .rect = heights.rect};
			add(std::move(item), HeightBytes(heights.after), HeightBytes(heights.before));
		}
	}
	for (const auto& [change, kind] :
	     {std::pair {&content.cameraFloor, ItemKind::CameraFloor}, std::pair {&content.coastMap, ItemKind::CoastMap}})
	{
		if (!*change || (*change)->original == (*change)->changed)
		{
			continue;
		}
		const auto& bytes = **change;
		if (bytes.original.size() != bytes.changed.size())
		{
			return MakeError("the new '{}' has {} bytes, not {}", bytes.name, bytes.changed.size(), bytes.original.size());
		}
		auto paths = NormalPaths(bytes.paths);
		if (paths.empty())
		{
			return MakeError("the object '{}' has no resource path", bytes.name);
		}
		ManifestItem item {.kind = kind, .name = bytes.name, .paths = std::move(paths), .file = ItemFile(kind, 0, {})};
		add(std::move(item), bytes.changed, bytes.original);
	}
	if (!content.holders.empty())
	{
		Json list = Json::array();
		for (const auto& move : content.holders)
		{
			list.push_back({{"name", move.name}, {"before", move.before}, {"after", move.after}});
		}
		ManifestItem item {.kind = ItemKind::TreeHolders,
		                   .name = "TreeHolder",
		                   .file = ItemFile(ItemKind::TreeHolders, 0, {}),
		                   .count = content.holders.size()};
		add(std::move(item), ToBytes(list.dump(1) + "\n"), HolderOriginalBytes(content.holders));
	}
	if (!content.terrainTrees.empty())
	{
		Json list = Json::array();
		for (const auto& move : content.terrainTrees)
		{
			list.push_back({{"index", move.index}, {"before", move.before}, {"after", move.after}});
		}
		ManifestItem item {.kind = ItemKind::TerrainTrees,
		                   .name = "trees",
		                   .file = ItemFile(ItemKind::TerrainTrees, 0, {}),
		                   .count = content.terrainTrees.size()};
		add(std::move(item), ToBytes(list.dump(1) + "\n"), TerrainTreeOriginalBytes(content.terrainTrees));
	}
	files.push_back({.path = std::string(k_ManifestFile), .bytes = ToBytes(ManifestJson(manifest))});
	return files;
}

std::string ManifestJson(const Manifest& manifest)
{
	Json items = Json::array();
	for (const auto& item : manifest.items)
	{
		items.push_back(ItemJson(item));
	}
	const Json root {
	    {"format", manifest.format},
	    {"buildGuid", manifest.buildGuid},
	    {"tool", manifest.tool},
	    {"made", manifest.made},
	    {"items", items},
	};
	return root.dump(2) + "\n";
}

Result<std::filesystem::path> WriteMapOverride(const std::filesystem::path& mapsFolder, std::string_view name,
                                               std::span<const OverrideFile> files)
{
	if (!IsValidMapName(name))
	{
		return MakeError("'{}' is not a name for a map override: use 1 to {} letters, digits, '-', '_' and '.'", name,
		                 k_MaxNameLength);
	}
	const auto target = mapsFolder / std::string(name);
	if (PathIsInGameFolder(target))
	{
		return MakeError("{} is in a game folder; the tools never write there", target.string());
	}
	std::error_code error;
	if (std::filesystem::exists(target, error) && !std::filesystem::exists(target / std::string(k_ManifestFile), error))
	{
		return MakeError("{} exists and is not a map override; the editor does not replace it", target.string());
	}
	for (const auto& file : files)
	{
		if (!IsSafeRelativePath(file.path))
		{
			return MakeError("the file '{}' is not a path in the folder of the override", file.path);
		}
	}
	const auto staging = mapsFolder / std::format("{}.writing", name);
	std::filesystem::remove_all(staging, error);
	std::filesystem::create_directories(staging, error);
	if (error)
	{
		return MakeError("the folder {} cannot be made: {}", staging.string(), error.message());
	}
	for (const auto& file : files)
	{
		const auto path = staging / std::filesystem::path(std::u8string(file.path.begin(), file.path.end()));
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream.write(reinterpret_cast<const char*>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
		if (!stream)
		{
			std::filesystem::remove_all(staging, error);
			return MakeError("{} cannot be written", path.string());
		}
	}
	const auto old = mapsFolder / std::format("{}.old", name);
	std::filesystem::remove_all(old, error);
	if (std::filesystem::exists(target, error))
	{
		std::filesystem::rename(target, old, error);
		if (error)
		{
			std::filesystem::remove_all(staging, error);
			return MakeError("the older override {} cannot be moved: {}", target.string(), error.message());
		}
	}
	std::filesystem::rename(staging, target, error);
	if (error)
	{
		return MakeError("{} cannot be put in place: {}", target.string(), error.message());
	}
	std::filesystem::remove_all(old, error);
	return target;
}

Result<Manifest> ParseManifest(std::string_view json)
{
	const auto root = Json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		return MakeError("the manifest is not a JSON object");
	}
	Manifest manifest;
	if (!Read(root, "format", manifest.format))
	{
		return MakeError("the manifest has no format");
	}
	if (manifest.format != k_FormatVersion)
	{
		return MakeError("the manifest has the format {}; this program reads the format {}", manifest.format, k_FormatVersion);
	}
	if (!Read(root, "buildGuid", manifest.buildGuid) || manifest.buildGuid.empty())
	{
		return MakeError("the manifest has no build guid");
	}
	(void)Read(root, "tool", manifest.tool);
	(void)Read(root, "made", manifest.made);
	const auto items = root.find("items");
	if (items == root.end() || !items->is_array())
	{
		return MakeError("the manifest has no list of items");
	}
	std::set<std::string> files;
	for (const auto& entry : *items)
	{
		if (!entry.is_object())
		{
			return MakeError("an item of the manifest is not a JSON object");
		}
		ManifestItem item;
		std::string kind;
		if (!Read(entry, "kind", kind) || !Read(entry, "file", item.file) || !Read(entry, "sha1", item.sha1) ||
		    !Read(entry, "originalSha1", item.originalSha1))
		{
			return MakeError("an item of the manifest has no kind, file or SHA-1");
		}
		const auto parsed = ParseItemKind(kind);
		if (!parsed)
		{
			return MakeError("the item '{}' has the kind '{}', which this program does not know", item.file, kind);
		}
		item.kind = *parsed;
		(void)Read(entry, "name", item.name);
		if (entry.contains("paths") && !Read(entry, "paths", item.paths))
		{
			return MakeError("the item '{}' has paths that are not a list of texts", item.file);
		}
		if (item.kind == ItemKind::Heights)
		{
			const auto rect = entry.find("rect");
			if (!Read(entry, "resolution", item.resolution) || rect == entry.end() || !rect->is_object() ||
			    !Read(*rect, "column", item.rect.column) || !Read(*rect, "row", item.rect.row) ||
			    !Read(*rect, "width", item.rect.width) || !Read(*rect, "height", item.rect.height))
			{
				return MakeError("the heights item '{}' has no resolution or rectangle", item.file);
			}
		}
		if (item.kind == ItemKind::TreeHolders || item.kind == ItemKind::TerrainTrees)
		{
			(void)Read(entry, "count", item.count);
		}
		if (auto check = CheckItem(item); !check)
		{
			return check.GetError();
		}
		if (!files.insert(item.file).second)
		{
			return MakeError("two items have the file '{}'", item.file);
		}
		manifest.items.push_back(std::move(item));
	}
	// One kind of land item can occur only one time, because each one changes the whole object.
	for (const auto kind :
	     {ItemKind::Heights, ItemKind::CameraFloor, ItemKind::CoastMap, ItemKind::TreeHolders, ItemKind::TerrainTrees})
	{
		if (std::ranges::count(manifest.items, kind, &ManifestItem::kind) > 1)
		{
			return MakeError("the manifest has more than one item of the kind '{}'", ItemKindName(kind));
		}
	}
	return manifest;
}

Result<void> CheckBuild(const Manifest& manifest, std::string_view buildGuid)
{
	if (manifest.buildGuid != buildGuid)
	{
		return MakeError("the override is for the game build {}, but the game is the build {}", manifest.buildGuid, buildGuid);
	}
	return {};
}

Result<void> CheckFile(const ManifestItem& item, std::span<const std::byte> bytes)
{
	if (const auto sha1 = Sha1Hex(bytes); sha1 != item.sha1)
	{
		return MakeError("the file '{}' has the SHA-1 {}, but the manifest has {}", item.file, sha1, item.sha1);
	}
	return {};
}

Result<void> CheckOriginal(const ManifestItem& item, std::span<const std::byte> original)
{
	if (const auto sha1 = Sha1Hex(original); sha1 != item.originalSha1)
	{
		return MakeError("the original data of '{}' in the game has the SHA-1 {}, but the editor read {}", item.file, sha1,
		                 item.originalSha1);
	}
	return {};
}

Result<std::vector<int16_t>> ReadHeights(const ManifestItem& item, std::span<const std::byte> bytes)
{
	if (item.kind != ItemKind::Heights)
	{
		return MakeError("the item '{}' is not a heights item", item.file);
	}
	if (bytes.size() != item.rect.Count() * 2)
	{
		return MakeError("the heights file '{}' has {} bytes, not {}", item.file, bytes.size(), item.rect.Count() * 2);
	}
	std::vector<int16_t> samples(item.rect.Count());
	for (size_t i = 0; i < samples.size(); ++i)
	{
		const auto low = std::to_integer<uint16_t>(bytes[i * 2]);
		const auto high = std::to_integer<uint16_t>(bytes[i * 2 + 1]);
		samples[i] = static_cast<int16_t>(static_cast<uint16_t>(low | (high << 8)));
	}
	return samples;
}

Result<std::vector<HolderMove>> ParseHolderMoves(std::string_view json)
{
	const auto root = Json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_array())
	{
		return MakeError("the TreeHolder file is not a JSON array");
	}
	std::vector<HolderMove> moves;
	for (const auto& entry : root)
	{
		HolderMove move;
		double before = 0.0;
		double after = 0.0;
		if (!entry.is_object() || !Read(entry, "name", move.name) || !Read(entry, "before", before) ||
		    !Read(entry, "after", after) || move.name.empty())
		{
			return MakeError("an entry of the TreeHolder file has no name, before or after");
		}
		move.before = static_cast<float>(before);
		move.after = static_cast<float>(after);
		moves.push_back(std::move(move));
	}
	return moves;
}

Result<std::vector<TerrainTreeMove>> ParseTerrainTreeMoves(std::string_view json)
{
	const auto root = Json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_array())
	{
		return MakeError("the tree file is not a JSON array");
	}
	std::vector<TerrainTreeMove> moves;
	for (const auto& entry : root)
	{
		TerrainTreeMove move;
		double before = 0.0;
		double after = 0.0;
		if (!entry.is_object() || !Read(entry, "index", move.index) || !Read(entry, "before", before) ||
		    !Read(entry, "after", after))
		{
			return MakeError("an entry of the tree file has no index, before or after");
		}
		move.before = static_cast<float>(before);
		move.after = static_cast<float>(after);
		moves.push_back(move);
	}
	return moves;
}

bool IsValidMapName(std::string_view name)
{
	if (name.empty() || name.size() > k_MaxNameLength || std::ranges::all_of(name, [](char c) { return c == '.'; }))
	{
		return false;
	}
	if (name.ends_with(".writing") || name.ends_with(".old"))
	{
		return false;
	}
	return std::ranges::all_of(name, [](char c) {
		const auto u = static_cast<unsigned char>(c);
		return std::isalnum(u) != 0 || c == '-' || c == '_' || c == '.';
	});
}

std::optional<std::string> ReadBootConfigBuildGuid(std::string_view text)
{
	constexpr std::string_view k_Key = "build-guid=";
	size_t start = 0;
	while (start < text.size())
	{
		auto end = text.find('\n', start);
		if (end == std::string_view::npos)
		{
			end = text.size();
		}
		auto line = text.substr(start, end - start);
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
		{
			line.remove_suffix(1);
		}
		if (line.starts_with(k_Key) && line.size() > k_Key.size())
		{
			return std::string(line.substr(k_Key.size()));
		}
		start = end + 1;
	}
	return std::nullopt;
}

std::string NormalResourcePath(std::string_view path)
{
	std::string normal(path);
	for (auto& c : normal)
	{
		c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return normal;
}

std::filesystem::path MapsFolderIn(const std::filesystem::path& localAppData)
{
	return localAppData / "MastersOfAlbionReforged" / "maps";
}

} // namespace moa::mapoverride
