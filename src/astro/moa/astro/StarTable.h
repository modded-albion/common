/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <optional>
#include <span>
#include <vector>

#include <moa/astro/Moon.h>

namespace moa::astro
{

/// One star of the star table of the sky mod (`tools/unity/moa_sky/Data/make_stars.py` writes the table from the
/// Yale Bright Star Catalogue).
struct CatalogueStar
{
	Equatorial position;              ///< J2000, degrees.
	float magnitude = 0.0f;           ///< The visual magnitude V.
	std::optional<float> colourIndex; ///< B-V, if the catalogue has it.
};

/// The size of the header and of one record of the star table, in bytes.
inline constexpr size_t k_StarTableHeaderSize = 16;
inline constexpr size_t k_StarTableRecordSize = 12;

/// Read a star table. Return nothing if the bytes are not a star table of version 1 or the size is wrong.
[[nodiscard]] std::optional<std::vector<CatalogueStar>> ReadStarTable(std::span<const std::byte> bytes);

} // namespace moa::astro
