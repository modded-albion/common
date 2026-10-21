/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "modzip/ModInfo.h"

#include <charconv>

#include <algorithm>
#include <optional>
#include <set>

#include <nlohmann/json.hpp>

namespace moa::modzip
{

namespace
{
constexpr size_t k_MaxNameLength = 64;
constexpr size_t k_MaxVersionLength = 32;
constexpr size_t k_MaxAuthorLength = 64;
constexpr size_t k_MaxDescriptionLength = 2000;
constexpr size_t k_MaxFolderLength = 64;
constexpr size_t k_BuildGuidLength = 32;
constexpr std::string_view k_MapManifest = "manifest.json";
constexpr std::string_view k_PackFile = "pack.json";

[[nodiscard]] bool IsLowerOrDigit(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

[[nodiscard]] bool IsBuildGuid(std::string_view text)
{
	return text.size() == k_BuildGuidLength &&
	       std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

/// Read an optional text field. Return an error if the field is not a text or is longer than `limit`.
[[nodiscard]] Result<void> ReadText(const nlohmann::json& root, const char* key, std::string& value, size_t limit,
                                    bool required)
{
	const auto found = root.find(key);
	if (found == root.end())
	{
		return required ? Result<void>(MakeError("{}: the field is missing", key)) : Result<void>();
	}
	if (!found->is_string())
	{
		return MakeError("{}: the value must be a text", key);
	}
	value = found->get<std::string>();
	if ((required && value.empty()) || value.size() > limit)
	{
		return MakeError("{}: the text must have {} to {} characters", key, required ? 1 : 0, limit);
	}
	return {};
}

[[nodiscard]] bool EndsWith(std::string_view text, std::string_view end)
{
	return text.size() >= end.size() && text.substr(text.size() - end.size()) == end;
}

constexpr size_t k_MaxVersionParts = 4;
constexpr uint32_t k_MaxVersionNumber = 999999;

/// Read the numbers of a version, or nothing if the text is not a version.
[[nodiscard]] std::optional<std::vector<uint32_t>> VersionNumbers(std::string_view version)
{
	std::vector<uint32_t> numbers;
	size_t start = 0;
	while (true)
	{
		const auto dot = version.find('.', start);
		const auto part = version.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
		uint32_t number = 0;
		const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), number);
		if (part.empty() || error != std::errc() || end != part.data() + part.size() || number > k_MaxVersionNumber ||
		    numbers.size() == k_MaxVersionParts)
		{
			return std::nullopt;
		}
		numbers.push_back(number);
		if (dot == std::string_view::npos)
		{
			return numbers;
		}
		start = dot + 1;
	}
}
} // namespace

bool IsValidModVersion(std::string_view version)
{
	return VersionNumbers(version).has_value();
}

std::strong_ordering CompareModVersions(std::string_view a, std::string_view b)
{
	const auto numbersA = VersionNumbers(a).value_or(std::vector<uint32_t> {});
	const auto numbersB = VersionNumbers(b).value_or(std::vector<uint32_t> {});
	for (size_t index = 0; index < std::max(numbersA.size(), numbersB.size()); ++index)
	{
		const auto valueA = index < numbersA.size() ? numbersA[index] : 0u;
		const auto valueB = index < numbersB.size() ? numbersB[index] : 0u;
		if (valueA != valueB)
		{
			return valueA <=> valueB;
		}
	}
	return std::strong_ordering::equal;
}

bool IsValidModId(std::string_view id)
{
	return !id.empty() && id.size() <= k_MaxModIdLength && IsLowerOrDigit(id.front()) &&
	       std::ranges::all_of(id, [](char c) { return IsLowerOrDigit(c) || c == '-' || c == '_'; });
}

bool IsValidFolderName(std::string_view name)
{
	return !name.empty() && name.size() <= k_MaxFolderLength && std::ranges::any_of(name, [](char c) { return c != '.'; }) &&
	       name.back() != '.' && std::ranges::all_of(name, [](char c) {
		       return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		              c == '.';
	       });
}

Result<ModInfo> ParseModInfo(std::string_view json)
{
	const auto root = nlohmann::json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		return MakeError("the file is not a JSON object");
	}
	static const std::set<std::string, std::less<>> k_Keys {"format",      "id",         "name",     "version", "author",
	                                                        "description", "buildGuids", "contents", "tool",    "made"};
	for (const auto& [key, value] : root.items())
	{
		if (!k_Keys.contains(key))
		{
			return MakeError("{}: the key is not known", key);
		}
	}
	const auto format = root.find("format");
	if (format == root.end() || !format->is_number_integer() || format->get<int64_t>() != k_ModFormat)
	{
		return MakeError("format: the value must be {}", k_ModFormat);
	}
	ModInfo info;
	if (auto id = ReadText(root, "id", info.id, k_MaxModIdLength, true); !id)
	{
		return id.GetError();
	}
	if (!IsValidModId(info.id))
	{
		return MakeError("id: '{}' must have 1 to {} of a-z, 0-9, '-' and '_', and start with a letter or a digit", info.id,
		                 k_MaxModIdLength);
	}
	for (auto read :
	     {ReadText(root, "name", info.name, k_MaxNameLength, true),
	      ReadText(root, "version", info.version, k_MaxVersionLength, true),
	      ReadText(root, "author", info.author, k_MaxAuthorLength, false),
	      ReadText(root, "description", info.description, k_MaxDescriptionLength, false),
	      ReadText(root, "tool", info.tool, k_MaxNameLength, false), ReadText(root, "made", info.made, k_MaxNameLength, false)})
	{
		if (!read)
		{
			return read.GetError();
		}
	}
	if (!IsValidModVersion(info.version))
	{
		return MakeError("version: '{}' must be 1 to {} numbers of 0 to {} with '.' between them, for example 1.2.0",
		                 info.version, k_MaxVersionParts, k_MaxVersionNumber);
	}
	const auto guids = root.find("buildGuids");
	if (guids == root.end() || !guids->is_array() || guids->empty())
	{
		return MakeError("buildGuids: the value must be a list of one or more game builds");
	}
	for (size_t index = 0; index < guids->size(); ++index)
	{
		const auto& guid = (*guids)[index];
		if (!guid.is_string() || !IsBuildGuid(guid.get<std::string>()))
		{
			return MakeError("buildGuids[{}]: the value must be 32 lower-case hex digits", index);
		}
		info.buildGuids.push_back(guid.get<std::string>());
	}
	const auto contents = root.find("contents");
	if (contents == root.end() || !contents->is_object())
	{
		return MakeError("contents: the value must be an object");
	}
	for (const auto& [key, value] : contents->items())
	{
		if (key != "map" && key != "packs")
		{
			return MakeError("contents.{}: the key is not known", key);
		}
	}
	if (const auto map = contents->find("map"); map != contents->end())
	{
		if (!map->is_boolean())
		{
			return MakeError("contents.map: the value must be true or false");
		}
		info.map = map->get<bool>();
	}
	if (const auto packs = contents->find("packs"); packs != contents->end())
	{
		if (!packs->is_array())
		{
			return MakeError("contents.packs: the value must be a list of pack folders");
		}
		for (size_t index = 0; index < packs->size(); ++index)
		{
			const auto& pack = (*packs)[index];
			if (!pack.is_string() || !IsValidFolderName(pack.get<std::string>()))
			{
				return MakeError("contents.packs[{}]: the value must be a folder name of letters, digits, '-', '_' and '.'",
				                 index);
			}
			info.packs.push_back(pack.get<std::string>());
		}
		std::ranges::sort(info.packs);
		if (std::ranges::adjacent_find(info.packs) != info.packs.end())
		{
			return MakeError("contents.packs: a pack folder is in the list two times");
		}
	}
	if (!info.map && info.packs.empty())
	{
		return MakeError("contents: the mod has no map and no packs");
	}
	return info;
}

std::string ModInfoJson(const ModInfo& info)
{
	nlohmann::ordered_json root {
	    {"format", k_ModFormat},
	    {"id", info.id},
	    {"name", info.name},
	    {"version", info.version},
	};
	if (!info.author.empty())
	{
		root["author"] = info.author;
	}
	if (!info.description.empty())
	{
		root["description"] = info.description;
	}
	root["buildGuids"] = info.buildGuids;
	root["contents"] = {{"map", info.map}, {"packs", info.packs}};
	if (!info.tool.empty())
	{
		root["tool"] = info.tool;
	}
	if (!info.made.empty())
	{
		root["made"] = info.made;
	}
	return root.dump(2) + "\n";
}

Result<void> CheckModBuild(const ModInfo& info, std::string_view buildGuid)
{
	if (std::ranges::find(info.buildGuids, buildGuid) == info.buildGuids.end())
	{
		return MakeError("the mod is for the game build {}, and the game is the build {}",
		                 info.buildGuids.empty() ? std::string("(none)") : info.buildGuids.front(), buildGuid);
	}
	return {};
}

Result<void> CheckModContents(const ModInfo& info, std::span<const ZipEntry> entries)
{
	const auto mapPrefix = std::string(k_MapFolder) + "/";
	const auto packsPrefix = std::string(k_PacksFolder) + "/";
	bool hasModFile = false;
	bool hasMapManifest = false;
	std::set<std::string, std::less<>> packFiles;
	for (const auto& entry : entries)
	{
		const std::string_view name = entry.name;
		if (name == k_ModFile)
		{
			hasModFile = true;
		}
		else if (name.starts_with(mapPrefix))
		{
			if (!info.map)
			{
				return MakeError("{}: the zip has a map, but mod.json does not list it", name);
			}
			hasMapManifest = hasMapManifest || name.substr(mapPrefix.size()) == k_MapManifest;
		}
		else if (name.starts_with(packsPrefix))
		{
			const auto rest = name.substr(packsPrefix.size());
			const auto slash = rest.find('/');
			if (slash == std::string_view::npos)
			{
				return MakeError("{}: a file directly in '{}' is not part of a pack", name, packsPrefix);
			}
			const auto folder = std::string(rest.substr(0, slash));
			if (!std::ranges::binary_search(info.packs, folder))
			{
				return MakeError("{}: the pack folder '{}' is not in the list of mod.json", name, folder);
			}
			if (rest.substr(slash + 1) == k_PackFile)
			{
				packFiles.insert(folder);
			}
		}
		else if (name.find('/') != std::string_view::npos || !(EndsWith(name, ".txt") || EndsWith(name, ".md")))
		{
			return MakeError("{}: the file is not part of the format of a mod zip", name);
		}
	}
	if (!hasModFile)
	{
		return MakeError("the zip has no {} at its root", k_ModFile);
	}
	if (info.map && !hasMapManifest)
	{
		return MakeError("mod.json lists a map, but the zip has no {}{}", mapPrefix, k_MapManifest);
	}
	for (const auto& pack : info.packs)
	{
		if (!packFiles.contains(pack))
		{
			return MakeError("mod.json lists the pack '{}', but the zip has no {}{}/{}", pack, packsPrefix, pack, k_PackFile);
		}
	}
	return {};
}

} // namespace moa::modzip
