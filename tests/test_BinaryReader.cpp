/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cstddef>

#include <array>

#include <gtest/gtest.h>
#include <moa/common/BinaryReader.h>

namespace
{
template <typename... Values>
constexpr std::array<std::byte, sizeof...(Values)> Bytes(Values... values)
{
	return {static_cast<std::byte>(values)...};
}
} // namespace

TEST(BinaryReader, ReadsLittleAndBigEndian)
{
	constexpr auto data = Bytes(0x01, 0x02, 0x03, 0x04);
	moa::BinaryReader little(data, moa::Endian::Little);
	EXPECT_EQ(little.Read<uint32_t>(), 0x04030201u);
	moa::BinaryReader big(data, moa::Endian::Big);
	EXPECT_EQ(big.Read<uint32_t>(), 0x01020304u);
	EXPECT_FALSE(big.Failed());
}

TEST(BinaryReader, ReadPastTheEndSetsTheFailedFlag)
{
	constexpr auto data = Bytes(0x01, 0x02);
	moa::BinaryReader reader(data);
	EXPECT_EQ(reader.Read<uint32_t>(), 0u);
	EXPECT_TRUE(reader.Failed());
	EXPECT_EQ(reader.Remaining(), 0u);
}

TEST(BinaryReader, ReadsTextAndAligns)
{
	constexpr auto data = Bytes('a', 'b', 0, 0x7f, 3, 0, 0, 0, 'x', 'y', 'z');
	moa::BinaryReader reader(data);
	EXPECT_EQ(reader.ReadCString(), "ab");
	reader.Align(4);
	EXPECT_EQ(reader.Position(), 4u);
	EXPECT_EQ(reader.ReadSizedString(), "xyz");
	EXPECT_FALSE(reader.Failed());
}

TEST(BinaryReader, TextWithNoEndSetsTheFailedFlag)
{
	constexpr auto data = Bytes('a', 'b');
	moa::BinaryReader reader(data);
	EXPECT_EQ(reader.ReadCString(), "");
	EXPECT_TRUE(reader.Failed());
}

TEST(BinaryReader, ReadsFloats)
{
	constexpr auto data = Bytes(0x00, 0x00, 0x80, 0x3f); // 1.0 in little-endian order
	moa::BinaryReader reader(data);
	EXPECT_FLOAT_EQ(reader.Read<float>(), 1.0f);
}
