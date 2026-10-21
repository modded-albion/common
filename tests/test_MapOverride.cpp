/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mapoverride/MapOverride.h>
#include <mapoverride/Sha1.h>

namespace
{
using namespace moa::mapoverride;

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

const OverrideFile* FindFile(const std::vector<OverrideFile>& files, std::string_view path)
{
	for (const auto& file : files)
	{
		if (file.path == path)
		{
			return &file;
		}
	}
	return nullptr;
}

MapOverrideContent SmallContent()
{
	MapOverrideContent content {.buildGuid = k_Build, .tool = "test", .made = "2026-10-20T00:00:00Z"};
	content.texts.push_back({.name = "seedmoa", .paths = {"SeedMoa"}, .original = "{\"a\":1}", .text = "{\"a\":2}"});
	content.texts.push_back({.name = "Same", .paths = {"flows/same"}, .original = "x", .text = "x"});
	content.heights = HeightsChange {.resolution = 9,
	                                 .rect = {.column = 2, .row = 3, .width = 2, .height = 2},
	                                 .before = {10, 11, 12, 13},
	                                 .after = {10, 20, -1, 13}};
	content.coastMap =
	    BytesChange {.name = "coastmap", .paths = {"coastmap"}, .original = Bytes("abcd"), .changed = Bytes("abzd")};
	content.holders = {{.name = "TreeHolder12", .before = 104.63f, .after = 93.69f}};
	content.terrainTrees = {{.index = 7, .before = 0.031f, .after = 0.029f}};
	return content;
}
} // namespace

TEST(MapOverrideSha1, KnownVectors)
{
	EXPECT_EQ(Sha1Hex(std::string_view()), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	EXPECT_EQ(Sha1Hex(std::string_view("abc")), "a9993e364706816aba3e25717850c26c9cd0d89d");
	EXPECT_EQ(Sha1Hex(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
	          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
	Sha1 sha;
	const std::string block(1000, 'a');
	for (int i = 0; i < 1000; ++i)
	{
		sha.Update(block);
	}
	EXPECT_EQ(ToHex(sha.Final()), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(MapOverrideSha1, SplitUpdatesGiveTheSameHash)
{
	const std::string text(200, 'q');
	Sha1 sha;
	sha.Update(std::string_view(text).substr(0, 63));
	sha.Update(std::string_view(text).substr(63, 1));
	sha.Update(std::string_view(text).substr(64));
	EXPECT_EQ(ToHex(sha.Final()), Sha1Hex(std::string_view(text)));
}

TEST(MapOverrideWriter, NoEditGivesAnEmptyManifest)
{
	MapOverrideContent content {.buildGuid = k_Build, .tool = "test", .made = "now"};
	content.texts.push_back({.name = "seedmoa", .paths = {"seedmoa"}, .original = "same", .text = "same"});
	auto files = BuildMapOverride(content);
	ASSERT_TRUE(files) << files.GetError().message;
	ASSERT_EQ(files->size(), 1u);
	EXPECT_EQ(files->front().path, k_ManifestFile);
	auto manifest = ParseManifest(Text(files->front().bytes));
	ASSERT_TRUE(manifest) << manifest.GetError().message;
	EXPECT_EQ(manifest->buildGuid, k_Build);
	EXPECT_TRUE(manifest->items.empty());
}

TEST(MapOverrideWriter, ManifestNamesEachChangedItemWithItsHashes)
{
	const auto content = SmallContent();
	auto files = BuildMapOverride(content);
	ASSERT_TRUE(files) << files.GetError().message;
	ASSERT_EQ(files->back().path, k_ManifestFile);
	auto manifest = ParseManifest(Text(files->back().bytes));
	ASSERT_TRUE(manifest) << manifest.GetError().message;
	// The unchanged text is not an item.
	ASSERT_EQ(manifest->items.size(), 5u);
	EXPECT_EQ(ManifestJson(*manifest), Text(files->back().bytes));

	const auto& text = manifest->items[0];
	EXPECT_EQ(text.kind, ItemKind::Text);
	EXPECT_EQ(text.paths, std::vector<std::string> {"seedmoa"});
	const auto* textFile = FindFile(*files, text.file);
	ASSERT_NE(textFile, nullptr);
	EXPECT_EQ(Text(textFile->bytes), "{\"a\":2}");
	EXPECT_TRUE(CheckFile(text, textFile->bytes));
	EXPECT_TRUE(CheckOriginal(text, Bytes("{\"a\":1}")));
	EXPECT_FALSE(CheckOriginal(text, Bytes("{\"a\":3}")));

	const auto& heights = manifest->items[1];
	EXPECT_EQ(heights.kind, ItemKind::Heights);
	EXPECT_EQ(heights.resolution, 9);
	EXPECT_EQ(heights.rect, (SampleRect {.column = 2, .row = 3, .width = 2, .height = 2}));
	const auto* heightFile = FindFile(*files, heights.file);
	ASSERT_NE(heightFile, nullptr);
	auto samples = ReadHeights(heights, heightFile->bytes);
	ASSERT_TRUE(samples);
	EXPECT_EQ(*samples, (std::vector<int16_t> {10, 20, -1, 13}));
	const std::vector<int16_t> before {10, 11, 12, 13};
	EXPECT_TRUE(CheckOriginal(heights, HeightBytes(before)));

	const auto& coast = manifest->items[2];
	EXPECT_EQ(coast.kind, ItemKind::CoastMap);
	EXPECT_TRUE(CheckOriginal(coast, Bytes("abcd")));

	const auto& holders = manifest->items[3];
	EXPECT_EQ(holders.kind, ItemKind::TreeHolders);
	EXPECT_EQ(holders.count, 1u);
	auto moves = ParseHolderMoves(Text(FindFile(*files, holders.file)->bytes));
	ASSERT_TRUE(moves);
	ASSERT_EQ(moves->size(), 1u);
	// The heights go through the JSON with no change of a bit.
	EXPECT_EQ(moves->front(), content.holders.front());
	EXPECT_TRUE(CheckOriginal(holders, HolderOriginalBytes(*moves)));

	const auto& trees = manifest->items[4];
	auto treeMoves = ParseTerrainTreeMoves(Text(FindFile(*files, trees.file)->bytes));
	ASSERT_TRUE(treeMoves);
	EXPECT_EQ(*treeMoves, content.terrainTrees);
	EXPECT_TRUE(CheckOriginal(trees, TerrainTreeOriginalBytes(*treeMoves)));
}

TEST(MapOverrideWriter, RefusesInconsistentChanges)
{
	auto content = SmallContent();
	content.heights->rect.column = 8;
	EXPECT_FALSE(BuildMapOverride(content));
	content = SmallContent();
	content.heights->after.pop_back();
	EXPECT_FALSE(BuildMapOverride(content));
	content = SmallContent();
	content.coastMap->changed.push_back(std::byte {1});
	EXPECT_FALSE(BuildMapOverride(content));
	content = SmallContent();
	content.texts.front().paths.clear();
	EXPECT_FALSE(BuildMapOverride(content));
}

TEST(MapOverrideChecks, RefusesAnotherBuild)
{
	auto files = BuildMapOverride(SmallContent());
	ASSERT_TRUE(files);
	auto manifest = ParseManifest(Text(files->back().bytes));
	ASSERT_TRUE(manifest);
	EXPECT_TRUE(CheckBuild(*manifest, k_Build));
	EXPECT_FALSE(CheckBuild(*manifest, "2ec5298ae9c544e7a914b0913b1119cc"));
}

TEST(MapOverrideChecks, RefusesAChangedFile)
{
	auto files = BuildMapOverride(SmallContent());
	ASSERT_TRUE(files);
	auto manifest = ParseManifest(Text(files->back().bytes));
	ASSERT_TRUE(manifest);
	auto bytes = FindFile(*files, manifest->items[0].file)->bytes;
	bytes.back() = std::byte {'X'};
	EXPECT_FALSE(CheckFile(manifest->items[0], bytes));
	EXPECT_FALSE(ReadHeights(manifest->items[1], std::vector<std::byte>(3)));
}

TEST(MapOverrideChecks, RefusesBadManifests)
{
	EXPECT_FALSE(ParseManifest("not json"));
	EXPECT_FALSE(ParseManifest(R"({"format": 2, "buildGuid": "x", "items": []})"));
	EXPECT_FALSE(ParseManifest(R"({"format": 1, "items": []})"));
	const std::string hash(40, 'a');
	const auto item = [&](std::string_view file, std::string_view extra) {
		return std::string(R"({"format": 1, "buildGuid": "x", "items": [{"kind": "text", "paths": ["seedmoa"], "file": ")") +
		       std::string(file) + R"(", "sha1": ")" + hash + R"(", "originalSha1": ")" + hash + "\"" + std::string(extra) +
		       "}]}";
	};
	EXPECT_TRUE(ParseManifest(item("text/a.txt", "")));
	EXPECT_FALSE(ParseManifest(item("../a.txt", "")));
	EXPECT_FALSE(ParseManifest(item("C:/a.txt", "")));
	EXPECT_FALSE(ParseManifest(item("text\\\\a.txt", "")));
	EXPECT_FALSE(ParseManifest(item("text/a.txt", R"(, "kind": "nothing")")));
	const std::string upper =
	    R"({"format": 1, "buildGuid": "x", "items": [{"kind": "text", "paths": ["SeedMoa"], "file": "a", "sha1": ")" + hash +
	    R"(", "originalSha1": ")" + hash + "\"}]}";
	EXPECT_FALSE(ParseManifest(upper));
	const std::string heights =
	    R"({"format": 1, "buildGuid": "x", "items": [{"kind": "heights", "file": "h", "resolution": 4, "rect": {"column": 3, "row": 0, "width": 2, "height": 1}, "sha1": ")" +
	    hash + R"(", "originalSha1": ")" + hash + "\"}]}";
	EXPECT_FALSE(ParseManifest(heights));
}

TEST(MapOverrideNames, AcceptsOnlySafeNames)
{
	EXPECT_TRUE(IsValidMapName("test-1_a.b"));
	EXPECT_FALSE(IsValidMapName(""));
	EXPECT_FALSE(IsValidMapName(".."));
	EXPECT_FALSE(IsValidMapName("a/b"));
	EXPECT_FALSE(IsValidMapName("a b"));
	EXPECT_FALSE(IsValidMapName("x.writing"));
	EXPECT_FALSE(IsValidMapName(std::string(k_MaxNameLength + 1, 'a')));
}

TEST(MapOverrideNames, ReadsTheBuildGuidOfBootConfig)
{
	EXPECT_EQ(ReadBootConfigBuildGuid("gfx-enable=1\r\nbuild-guid=8a91370f\r\nother=2\r\n"),
	          std::optional<std::string>("8a91370f"));
	EXPECT_FALSE(ReadBootConfigBuildGuid("gfx-enable=1\n"));
	EXPECT_EQ(NormalResourcePath("Flows\\Chapter1V2/Chapter1Flow"), "flows/chapter1v2/chapter1flow");
}

TEST(MapOverrideFolder, WritesAndReplacesAnOverride)
{
	const auto root = std::filesystem::temp_directory_path() / "moa-map-override-test";
	std::filesystem::remove_all(root);
	auto files = BuildMapOverride(SmallContent());
	ASSERT_TRUE(files);
	auto written = WriteMapOverride(root, "first", *files);
	ASSERT_TRUE(written) << written.GetError().message;
	EXPECT_TRUE(std::filesystem::exists(*written / k_ManifestFile));
	EXPECT_TRUE(std::filesystem::exists(*written / "land/heights.bin"));
	// A second write replaces the files; a file of the older override does not stay.
	MapOverrideContent empty {.buildGuid = k_Build, .tool = "test", .made = "now"};
	auto emptyFiles = BuildMapOverride(empty);
	ASSERT_TRUE(emptyFiles);
	ASSERT_TRUE(WriteMapOverride(root, "first", *emptyFiles));
	EXPECT_FALSE(std::filesystem::exists(root / "first" / "land/heights.bin"));
	EXPECT_TRUE(std::filesystem::exists(root / "first" / k_ManifestFile));
	// A folder that is not an override stays as it is.
	std::filesystem::create_directories(root / "other");
	std::ofstream(root / "other" / "keep.txt") << "keep";
	EXPECT_FALSE(WriteMapOverride(root, "other", *emptyFiles));
	EXPECT_TRUE(std::filesystem::exists(root / "other" / "keep.txt"));
	EXPECT_FALSE(WriteMapOverride(root, "../escape", *emptyFiles));
	EXPECT_FALSE(WriteMapOverride("F:/SteamLibrary/steamapps/common/Masters of Albion", "x", *emptyFiles));
	std::filesystem::remove_all(root);
}
