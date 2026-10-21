/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cstdint>

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <moa/modzip/ModInfo.h>
#include <moa/modzip/Zip.h>

namespace
{
using namespace moa::modzip;

constexpr auto k_Build = "8a91370f9f7845a695fb431c39155434";

std::vector<std::byte> Bytes(std::string_view text)
{
	const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
	return {bytes.begin(), bytes.end()};
}

std::string Text(const std::vector<std::byte>& bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

uint32_t Crc32(std::span<const std::byte> bytes)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (const auto byte : bytes)
	{
		crc ^= static_cast<uint8_t>(byte);
		for (int bit = 0; bit < 8; ++bit)
		{
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

/// One entry of a zip that the test writes byte by byte, so that it can have names and flags that a normal writer
/// refuses.
struct RawEntry
{
	std::string name;
	std::string data;
	uint32_t externalAttributes = 0;
	uint16_t flags = 0;
	uint16_t method = 0;
};

void Put16(std::vector<std::byte>& out, uint32_t value)
{
	out.push_back(static_cast<std::byte>(value & 0xFF));
	out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void Put32(std::vector<std::byte>& out, uint32_t value)
{
	Put16(out, value & 0xFFFF);
	Put16(out, value >> 16);
}

void PutText(std::vector<std::byte>& out, std::string_view text)
{
	const auto bytes = Bytes(text);
	out.insert(out.end(), bytes.begin(), bytes.end());
}

/// A zip with stored entries: the local headers and data, the central directory, and its end record.
std::vector<std::byte> RawZip(const std::vector<RawEntry>& entries)
{
	std::vector<std::byte> out;
	std::vector<uint32_t> offsets;
	for (const auto& entry : entries)
	{
		offsets.push_back(static_cast<uint32_t>(out.size()));
		const auto data = Bytes(entry.data);
		Put32(out, 0x04034b50);
		Put16(out, 20);
		Put16(out, entry.flags);
		Put16(out, entry.method);
		Put16(out, 0);
		Put16(out, 0x21);
		Put32(out, Crc32(data));
		Put32(out, static_cast<uint32_t>(data.size()));
		Put32(out, static_cast<uint32_t>(data.size()));
		Put16(out, static_cast<uint32_t>(entry.name.size()));
		Put16(out, 0);
		PutText(out, entry.name);
		out.insert(out.end(), data.begin(), data.end());
	}
	const auto directory = static_cast<uint32_t>(out.size());
	for (size_t index = 0; index < entries.size(); ++index)
	{
		const auto& entry = entries[index];
		const auto data = Bytes(entry.data);
		Put32(out, 0x02014b50);
		Put16(out, (3u << 8) | 20u);
		Put16(out, 20);
		Put16(out, entry.flags);
		Put16(out, entry.method);
		Put16(out, 0);
		Put16(out, 0x21);
		Put32(out, Crc32(data));
		Put32(out, static_cast<uint32_t>(data.size()));
		Put32(out, static_cast<uint32_t>(data.size()));
		Put16(out, static_cast<uint32_t>(entry.name.size()));
		Put16(out, 0);
		Put16(out, 0);
		Put16(out, 0);
		Put16(out, 0);
		Put32(out, entry.externalAttributes);
		Put32(out, offsets[index]);
		PutText(out, entry.name);
	}
	const auto size = static_cast<uint32_t>(out.size()) - directory;
	Put32(out, 0x06054b50);
	Put16(out, 0);
	Put16(out, 0);
	Put16(out, static_cast<uint32_t>(entries.size()));
	Put16(out, static_cast<uint32_t>(entries.size()));
	Put32(out, size);
	Put32(out, directory);
	Put16(out, 0);
	return out;
}

std::string OpenError(std::vector<std::byte> bytes, const ZipLimits& limits = {})
{
	auto zip = ZipArchive::Open(std::move(bytes), limits);
	return zip ? std::string() : zip.GetError().message;
}

ModInfo SmallInfo()
{
	return ModInfo {.id = "test-mod",
	                .name = "Test mod",
	                .version = "1.0.0",
	                .author = "Tester",
	                .description = "A mod of the tests.",
	                .buildGuids = {k_Build},
	                .map = true,
	                .packs = {"broken_coffin"},
	                .tool = "test",
	                .made = "2026-10-20T00:00:00Z"};
}

std::vector<ZipEntry> Entries(std::initializer_list<std::string_view> names)
{
	std::vector<ZipEntry> entries;
	for (const auto name : names)
	{
		entries.push_back({.name = std::string(name), .size = 1, .compressedSize = 1});
	}
	return entries;
}
} // namespace

TEST(ModZipNames, AcceptsRelativePaths)
{
	EXPECT_EQ(*CheckEntryName("mod.json"), "mod.json");
	EXPECT_EQ(*CheckEntryName("map/text/000-seedmoa.txt"), "map/text/000-seedmoa.txt");
	EXPECT_EQ(*CheckEntryName("packs/"), "packs");
	EXPECT_EQ(*CheckEntryName("a.b/c..d"), "a.b/c..d");
}

TEST(ModZipNames, RefusesNamesThatLeaveTheRoot)
{
	for (const auto* name : {"", "/etc/passwd", "../x", "a/../b", "a/..", "./a", "a//b", "C:/x", "C:x", "a\\b", "a\\..\\b",
	                         "file.txt:stream", "a./b", "a /b", "b.", "x\ty", "//server/share"})
	{
		EXPECT_FALSE(CheckEntryName(name)) << name;
	}
	EXPECT_FALSE(CheckEntryName(std::string(k_MaxEntryName + 1, 'a')));
	EXPECT_FALSE(CheckEntryName(std::string("a\0b", 3)));
}

TEST(ModZip, WritesAndReadsBack)
{
	const std::vector<ZipFile> files {
	    {.name = "mod.json", .bytes = Bytes("{}")},
	    {.name = "map/manifest.json", .bytes = Bytes(std::string(5000, 'x'))},
	    {.name = "map/empty.bin", .bytes = {}},
	};
	auto bytes = WriteZip(files);
	ASSERT_TRUE(bytes) << bytes.GetError().message;
	EXPECT_EQ(*bytes, *WriteZip(files)) << "the same files make the same zip";
	auto zip = ZipArchive::Open(std::move(*bytes));
	ASSERT_TRUE(zip) << zip.GetError().message;
	ASSERT_EQ(zip->Entries().size(), 3u);
	EXPECT_EQ(zip->Entries()[0].name, "map/empty.bin");
	EXPECT_EQ(zip->Entries()[1].name, "map/manifest.json");
	EXPECT_EQ(zip->Entries()[1].size, 5000u);
	EXPECT_LT(zip->Entries()[1].compressedSize, 5000u);
	EXPECT_EQ(zip->Entries()[2].name, "mod.json");
	EXPECT_TRUE(zip->Contains("mod.json"));
	EXPECT_FALSE(zip->Contains("MOD.JSON"));
	EXPECT_EQ(Text(*zip->Read("mod.json")), "{}");
	EXPECT_EQ(Text(*zip->Read("map/manifest.json")), std::string(5000, 'x'));
	EXPECT_TRUE(zip->Read("map/empty.bin")->empty());
	EXPECT_FALSE(zip->Read("map"));
	EXPECT_FALSE(zip->Read("other"));
}

TEST(ModZip, WriterRefusesBadNames)
{
	EXPECT_FALSE(WriteZip(std::vector<ZipFile> {{.name = "../x", .bytes = {}}}));
	EXPECT_FALSE(WriteZip(std::vector<ZipFile> {{.name = "a/", .bytes = {}}}));
	EXPECT_FALSE(WriteZip(std::vector<ZipFile> {{.name = "A.txt", .bytes = {}}, {.name = "a.TXT", .bytes = {}}}));
}

TEST(ModZip, ReadsAStoredZipThatTheTestWrote)
{
	auto zip = ZipArchive::Open(RawZip({{.name = "packs/", .data = ""}, {.name = "packs/a/pack.json", .data = "{}"}}));
	ASSERT_TRUE(zip) << zip.GetError().message;
	ASSERT_EQ(zip->Entries().size(), 1u) << "a folder is not an entry";
	EXPECT_EQ(Text(*zip->Read("packs/a/pack.json")), "{}");
}

TEST(ModZip, RefusesUnsafeEntries)
{
	EXPECT_NE(OpenError(RawZip({{.name = "../evil.txt", .data = "x"}})), "");
	EXPECT_NE(OpenError(RawZip({{.name = "C:/evil.txt", .data = "x"}})), "");
	EXPECT_NE(OpenError(RawZip({{.name = "map\\evil.txt", .data = "x"}})), "");
	EXPECT_NE(OpenError(RawZip({{.name = "/evil.txt", .data = "x"}})), "");
	// A symbolic link: the Unix mode in the high 16 bits of the external attributes.
	EXPECT_NE(OpenError(RawZip({{.name = "link", .data = "/etc", .externalAttributes = 0120777u << 16}})).find("symbolic"),
	          std::string::npos);
	// A normal Unix file is accepted.
	EXPECT_EQ(OpenError(RawZip({{.name = "file.txt", .data = "x", .externalAttributes = 0100644u << 16}})), "");
	EXPECT_NE(OpenError(RawZip({{.name = "secret.txt", .data = "x", .flags = 1}})).find("encrypted"), std::string::npos);
	EXPECT_NE(OpenError(RawZip({{.name = "a.txt", .data = "x"}, {.name = "A.TXT", .data = "y"}})).find("two entries"),
	          std::string::npos);
	EXPECT_NE(OpenError(RawZip({{.name = "a", .data = "x"}, {.name = "a/b", .data = "y"}})).find("a file and a folder"),
	          std::string::npos);
	EXPECT_NE(OpenError(RawZip({{.name = "a/", .data = ""}, {.name = "a", .data = "y"}})), "");
	EXPECT_NE(OpenError(RawZip({{.name = "bzip2.bin", .data = "x", .method = 12}})), "");
	EXPECT_NE(OpenError(Bytes("this is not a zip")), "");
}

TEST(ModZip, RefusesZipsAboveTheLimits)
{
	const auto zip = RawZip({{.name = "a.bin", .data = std::string(1000, 'a')}, {.name = "b.bin", .data = "bb"}});
	EXPECT_EQ(OpenError(zip), "");
	EXPECT_NE(OpenError(zip, ZipLimits {.maxArchiveBytes = 100}), "");
	EXPECT_NE(OpenError(zip, ZipLimits {.maxEntries = 1}), "");
	EXPECT_NE(OpenError(zip, ZipLimits {.maxEntryBytes = 999}), "");
	EXPECT_NE(OpenError(zip, ZipLimits {.maxTotalBytes = 1001}), "");
}

TEST(ModZip, RefusesDataThatIsLongerThanItsSize)
{
	// A compressed file of 100000 bytes that says it has 10 bytes: the reader must not write more than 10 bytes.
	auto bytes = *WriteZip(std::vector<ZipFile> {{.name = "bomb.bin", .bytes = std::vector<std::byte>(100000)}});
	const auto patch = [&bytes](uint32_t signature, size_t offset) {
		for (size_t at = 0; at + 4 <= bytes.size(); ++at)
		{
			const auto value = static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
			                   (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
			if (value == signature)
			{
				bytes[at + offset] = std::byte {10};
				bytes[at + offset + 1] = std::byte {0};
				bytes[at + offset + 2] = std::byte {0};
				bytes[at + offset + 3] = std::byte {0};
				return;
			}
		}
		FAIL() << "no signature";
	};
	patch(0x04034b50, 22);
	patch(0x02014b50, 24);
	auto zip = ZipArchive::Open(std::move(bytes));
	ASSERT_TRUE(zip) << zip.GetError().message;
	EXPECT_EQ(zip->Entries()[0].size, 10u);
	EXPECT_FALSE(zip->Read("bomb.bin"));
}

TEST(ModZip, RefusesDataWithABadCrc)
{
	auto bytes = RawZip({{.name = "a.txt", .data = "hello"}});
	// The data of the first entry follows its local header (30 bytes) and its name (5 bytes).
	bytes[35] = std::byte {'j'};
	auto zip = ZipArchive::Open(std::move(bytes));
	ASSERT_TRUE(zip);
	EXPECT_FALSE(zip->Read("a.txt"));
}

TEST(ModInfo, ParsesAndWritesTheFile)
{
	const auto info = SmallInfo();
	const auto text = ModInfoJson(info);
	auto parsed = ParseModInfo(text);
	ASSERT_TRUE(parsed) << parsed.GetError().message;
	EXPECT_EQ(*parsed, info);
	auto minimal = ParseModInfo(R"({"format": 1, "id": "m", "name": "M", "version": "1", "buildGuids": [")" +
	                            std::string(k_Build) + R"("], "contents": {"packs": ["b", "a"]}})");
	ASSERT_TRUE(minimal) << minimal.GetError().message;
	EXPECT_FALSE(minimal->map);
	EXPECT_EQ(minimal->packs, (std::vector<std::string> {"a", "b"}));
}

TEST(ModInfo, RefusesBadFiles)
{
	const std::string guid = std::string("[\"") + k_Build + "\"]";
	const auto make = [&guid](std::string_view id, std::string_view guids, std::string_view contents, std::string_view extra) {
		return std::format(R"({{"format": 1, "id": "{}", "name": "N", "version": "1", "buildGuids": {}, "contents": {}{}}})",
		                   id, guids, contents, extra);
	};
	const std::string packs = R"({"packs": ["a"]})";
	ASSERT_TRUE(ParseModInfo(make("ok", guid, packs, "")));
	EXPECT_FALSE(ParseModInfo("not json"));
	EXPECT_FALSE(ParseModInfo(make("Upper", guid, packs, "")));
	EXPECT_FALSE(ParseModInfo(make("-start", guid, packs, "")));
	EXPECT_FALSE(ParseModInfo(make("dot.id", guid, packs, "")));
	EXPECT_FALSE(ParseModInfo(make(std::string(k_MaxModIdLength + 1, 'a'), guid, packs, "")));
	EXPECT_FALSE(ParseModInfo(make("ok", "[]", packs, "")));
	EXPECT_FALSE(ParseModInfo(make("ok", R"(["8A91370F9F7845A695FB431C39155434"])", packs, "")));
	EXPECT_FALSE(ParseModInfo(make("ok", R"(["short"])", packs, "")));
	EXPECT_FALSE(ParseModInfo(make("ok", guid, "{}", "")));
	EXPECT_FALSE(ParseModInfo(make("ok", guid, R"({"map": false, "packs": []})", "")));
	EXPECT_FALSE(ParseModInfo(make("ok", guid, R"({"packs": ["../x"]})", "")));
	EXPECT_FALSE(ParseModInfo(make("ok", guid, R"({"packs": ["a", "a"]})", "")));
	EXPECT_FALSE(ParseModInfo(make("ok", guid, R"({"packs": ["a"], "scripts": true})", "")));
	EXPECT_FALSE(ParseModInfo(make("ok", guid, packs, R"(, "unknown": 1)")));
	EXPECT_FALSE(ParseModInfo(make("ok", guid, packs, R"(, "author": 3)")));
	EXPECT_FALSE(ParseModInfo(R"({"format": 2, "id": "ok"})"));
	auto error = ParseModInfo(make("ok", R"(["x"])", packs, ""));
	ASSERT_FALSE(error);
	EXPECT_TRUE(error.GetError().message.starts_with("buildGuids[0]:")) << error.GetError().message;
}

TEST(ModInfo, ChecksAndComparesVersions)
{
	for (const auto* version : {"1", "1.2", "1.2.10", "0.0.0.1", "999999"})
	{
		EXPECT_TRUE(IsValidModVersion(version)) << version;
	}
	for (const auto* version : {"", "v1", "1.", ".1", "1..2", "1.2.3.4.5", "1.0-beta", "-1", "+1", "1 2", "1000000"})
	{
		EXPECT_FALSE(IsValidModVersion(version)) << version;
	}
	EXPECT_EQ(CompareModVersions("1.10", "1.9"), std::strong_ordering::greater);
	EXPECT_EQ(CompareModVersions("1.2", "1.2.0"), std::strong_ordering::equal);
	EXPECT_EQ(CompareModVersions("1.02", "1.2"), std::strong_ordering::equal);
	EXPECT_EQ(CompareModVersions("1.2.0", "1.2.0.1"), std::strong_ordering::less);
	EXPECT_EQ(CompareModVersions("2", "1.99.99"), std::strong_ordering::greater);

	auto info = SmallInfo();
	info.version = "1.0-beta";
	auto parsed = ParseModInfo(ModInfoJson(info));
	ASSERT_FALSE(parsed);
	EXPECT_TRUE(parsed.GetError().message.starts_with("version:")) << parsed.GetError().message;
}

TEST(ModInfo, ChecksTheGameBuild)
{
	const auto info = SmallInfo();
	EXPECT_TRUE(CheckModBuild(info, k_Build));
	EXPECT_FALSE(CheckModBuild(info, "2ec5298a000000000000000000000000"));
}

TEST(ModInfo, ComparesTheContentsWithTheZip)
{
	const auto info = SmallInfo();
	EXPECT_TRUE(CheckModContents(info, Entries({"README.md", "map/manifest.json", "map/text/000-seedmoa.txt", "mod.json",
	                                            "packs/broken_coffin/pack.json", "packs/broken_coffin/models/a.moamesh"})));
	// No mod.json, no map manifest, no pack file.
	EXPECT_FALSE(CheckModContents(info, Entries({"map/manifest.json", "packs/broken_coffin/pack.json"})));
	EXPECT_FALSE(CheckModContents(info, Entries({"mod.json", "map/other.json", "packs/broken_coffin/pack.json"})));
	EXPECT_FALSE(CheckModContents(info, Entries({"mod.json", "map/manifest.json", "packs/broken_coffin/models/a.bin"})));
	// A pack that mod.json does not list, a file directly in packs/, and a file of another kind at the root.
	EXPECT_FALSE(CheckModContents(
	    info, Entries({"mod.json", "map/manifest.json", "packs/broken_coffin/pack.json", "packs/other/pack.json"})));
	EXPECT_FALSE(
	    CheckModContents(info, Entries({"mod.json", "map/manifest.json", "packs/broken_coffin/pack.json", "packs/pack.json"})));
	EXPECT_FALSE(
	    CheckModContents(info, Entries({"mod.json", "map/manifest.json", "packs/broken_coffin/pack.json", "version.dll"})));
	EXPECT_FALSE(
	    CheckModContents(info, Entries({"mod.json", "map/manifest.json", "packs/broken_coffin/pack.json", "scripts/run.txt"})));
	auto noMap = info;
	noMap.map = false;
	EXPECT_FALSE(CheckModContents(noMap, Entries({"mod.json", "map/manifest.json", "packs/broken_coffin/pack.json"})));
	EXPECT_TRUE(CheckModContents(noMap, Entries({"mod.json", "packs/broken_coffin/pack.json"})));
}
