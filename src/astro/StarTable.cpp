/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "astro/StarTable.h"

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string_view>

namespace moa::astro
{

namespace
{
constexpr std::string_view k_Magic = "MOASTARS";
constexpr uint32_t k_Version = 1;
constexpr int16_t k_NoColour = std::numeric_limits<int16_t>::min();

/// Read a little endian value at `offset`. The caller checks the size.
template <typename T>
[[nodiscard]] T ReadLittle(std::span<const std::byte> bytes, size_t offset)
{
	std::array<std::byte, sizeof(T)> raw {};
	std::memcpy(raw.data(), bytes.data() + offset, sizeof(T));
	if constexpr (std::endian::native == std::endian::big)
	{
		std::ranges::reverse(raw);
	}
	return std::bit_cast<T>(raw);
}
} // namespace

std::optional<std::vector<CatalogueStar>> ReadStarTable(std::span<const std::byte> bytes)
{
	if (bytes.size() < k_StarTableHeaderSize || std::memcmp(bytes.data(), k_Magic.data(), k_Magic.size()) != 0 ||
	    ReadLittle<uint32_t>(bytes, 8) != k_Version)
	{
		return std::nullopt;
	}
	const auto count = ReadLittle<uint32_t>(bytes, 12);
	if ((bytes.size() - k_StarTableHeaderSize) / k_StarTableRecordSize != count ||
	    (bytes.size() - k_StarTableHeaderSize) % k_StarTableRecordSize != 0)
	{
		return std::nullopt;
	}
	std::vector<CatalogueStar> stars;
	stars.reserve(count);
	for (size_t index = 0; index < count; ++index)
	{
		const auto offset = k_StarTableHeaderSize + index * k_StarTableRecordSize;
		const auto colour = ReadLittle<int16_t>(bytes, offset + 10);
		stars.push_back({
		    .position = {.rightAscension = ReadLittle<float>(bytes, offset),
		                 .declination = ReadLittle<float>(bytes, offset + 4)},
		    .magnitude = static_cast<float>(ReadLittle<int16_t>(bytes, offset + 8)) / 100.0f,
		    .colourIndex = colour == k_NoColour ? std::nullopt : std::optional(static_cast<float>(colour) / 1000.0f),
		});
	}
	return stars;
}

} // namespace moa::astro
