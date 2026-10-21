/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <moa_mod/SettingsFile.h>
#include <nlohmann/json.hpp>

using namespace moa_mod;

namespace
{

/// A folder for the files of one test. The destructor removes it.
class TemporaryFolder
{
public:
	TemporaryFolder()
	    : _path(std::filesystem::temp_directory_path() /
	            ("moa-common-settings-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name())))
	{
		std::filesystem::remove_all(_path);
		std::filesystem::create_directories(_path);
	}
	~TemporaryFolder()
	{
		std::error_code ignored;
		std::filesystem::remove_all(_path, ignored);
	}
	TemporaryFolder(const TemporaryFolder&) = delete;
	TemporaryFolder& operator=(const TemporaryFolder&) = delete;

	[[nodiscard]] const std::filesystem::path& Path() const { return _path; }

private:
	std::filesystem::path _path;
};

void WriteFile(const std::filesystem::path& path, const std::string& text)
{
	std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

} // namespace

TEST(SettingsFile, PatchKeepsTheOtherFields)
{
	const auto text = PatchSettingsText(R"({"note": "mine", "camera": {"enabled": false, "zoomSmoothing": 0.5}})",
	                                    {{"camera", {{"enabled", true}}}});
	ASSERT_TRUE(text) << text.GetError().message;
	const auto root = nlohmann::json::parse(*text);
	EXPECT_EQ(root["note"], "mine");
	EXPECT_EQ(root["camera"]["enabled"], true);
	EXPECT_EQ(root["camera"]["zoomSmoothing"], 0.5);
}

TEST(SettingsFile, PatchOfAnEmptyFileMakesAnObject)
{
	const auto text = PatchSettingsText("  \n", {{"moon", {{"enabled", true}}}});
	ASSERT_TRUE(text);
	EXPECT_EQ(nlohmann::json::parse(*text)["moon"]["enabled"], true);
}

TEST(SettingsFile, PatchRefusesABadFile)
{
	EXPECT_FALSE(PatchSettingsText("{ not json", {{"camera", {{"enabled", true}}}}));
	EXPECT_FALSE(PatchSettingsText("[1, 2]", {{"camera", {{"enabled", true}}}}));
}

TEST(SettingsFile, AtomicWriteReplacesTheFileAndLeavesNoTemporaryFile)
{
	const TemporaryFolder folder;
	const auto path = folder.Path() / "mods.json";
	ASSERT_TRUE(WriteTextFileAtomically(path, "{\"first\": 1}\n"));
	ASSERT_TRUE(WriteTextFileAtomically(path, "{\"second\": 2}\n"));
	EXPECT_EQ(ReadTextFile(path), "{\"second\": 2}\n");
	auto temporary = path;
	temporary += ".tmp";
	EXPECT_FALSE(std::filesystem::exists(temporary));
}

TEST(SettingsFile, AtomicWriteFailsInAFolderThatDoesNotExist)
{
	const TemporaryFolder folder;
	EXPECT_FALSE(WriteTextFileAtomically(folder.Path() / "missing" / "mods.json", "{}"));
}

TEST(SettingsFile, TheMonitorFindsAChangeOneTime)
{
	const TemporaryFolder folder;
	const auto path = folder.Path() / "mods.json";
	FileChangeMonitor monitor(path);
	EXPECT_FALSE(monitor.Changed());
	WriteFile(path, "{}");
	EXPECT_TRUE(monitor.Changed());
	EXPECT_FALSE(monitor.Changed());
	WriteFile(path, R"({"frameHook": true})");
	EXPECT_TRUE(monitor.Changed());
	std::filesystem::remove(path);
	EXPECT_TRUE(monitor.Changed());
	EXPECT_FALSE(ReadFileStamp(path).exists);
}
