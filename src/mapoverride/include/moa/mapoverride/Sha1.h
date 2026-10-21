/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <span>
#include <string>
#include <string_view>

namespace moa::mapoverride
{

/// The SHA-1 hash (FIPS 180-4). A map override names each original item of the game by its SHA-1, so that the mod can
/// find a different game build or different data before it changes anything.
class Sha1
{
public:
	Sha1();

	/// Add bytes to the hash.
	void Update(std::span<const std::byte> bytes);
	void Update(std::string_view text);

	/// Return the hash of all bytes that `Update` received. Call it one time.
	[[nodiscard]] std::array<uint8_t, 20> Final();

private:
	void Block(const uint8_t* block);

	std::array<uint32_t, 5> _state {};
	std::array<uint8_t, 64> _buffer {};
	size_t _buffered = 0;
	uint64_t _length = 0;
};

/// Return the SHA-1 of the bytes as 40 hexadecimal digits in lower case.
[[nodiscard]] std::string Sha1Hex(std::span<const std::byte> bytes);
[[nodiscard]] std::string Sha1Hex(std::string_view text);

/// Return 40 hexadecimal digits in lower case for a hash.
[[nodiscard]] std::string ToHex(const std::array<uint8_t, 20>& hash);

} // namespace moa::mapoverride
