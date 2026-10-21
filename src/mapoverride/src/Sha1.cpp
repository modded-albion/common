/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "moa/mapoverride/Sha1.h"

#include <cstring>

#include <algorithm>
#include <bit>

namespace moa::mapoverride
{

namespace
{
constexpr std::array<uint32_t, 5> k_InitialState {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
constexpr size_t k_BlockSize = 64;
/// The block that holds the length must keep 8 bytes for the length after the end mark.
constexpr size_t k_LengthOffset = k_BlockSize - 8;
} // namespace

Sha1::Sha1()
    : _state(k_InitialState)
{
}

void Sha1::Block(const uint8_t* block)
{
	std::array<uint32_t, 80> words {};
	for (size_t i = 0; i < 16; ++i)
	{
		words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
		           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
	}
	for (size_t i = 16; i < words.size(); ++i)
	{
		words[i] = std::rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
	}
	auto [a, b, c, d, e] = _state;
	for (size_t i = 0; i < words.size(); ++i)
	{
		uint32_t f = 0;
		uint32_t k = 0;
		if (i < 20)
		{
			f = (b & c) | (~b & d);
			k = 0x5A827999u;
		}
		else if (i < 40)
		{
			f = b ^ c ^ d;
			k = 0x6ED9EBA1u;
		}
		else if (i < 60)
		{
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDCu;
		}
		else
		{
			f = b ^ c ^ d;
			k = 0xCA62C1D6u;
		}
		const uint32_t next = std::rotl(a, 5) + f + e + k + words[i];
		e = d;
		d = c;
		c = std::rotl(b, 30);
		b = a;
		a = next;
	}
	_state[0] += a;
	_state[1] += b;
	_state[2] += c;
	_state[3] += d;
	_state[4] += e;
}

void Sha1::Update(std::span<const std::byte> bytes)
{
	const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
	size_t size = bytes.size();
	_length += size;
	if (_buffered > 0)
	{
		const size_t take = std::min(size, k_BlockSize - _buffered);
		std::memcpy(_buffer.data() + _buffered, data, take);
		_buffered += take;
		data += take;
		size -= take;
		if (_buffered < k_BlockSize)
		{
			return;
		}
		Block(_buffer.data());
		_buffered = 0;
	}
	for (; size >= k_BlockSize; data += k_BlockSize, size -= k_BlockSize)
	{
		Block(data);
	}
	if (size > 0)
	{
		std::memcpy(_buffer.data(), data, size);
		_buffered = size;
	}
}

void Sha1::Update(std::string_view text)
{
	Update(std::as_bytes(std::span(text.data(), text.size())));
}

std::array<uint8_t, 20> Sha1::Final()
{
	const uint64_t bits = _length * 8;
	_buffer[_buffered++] = 0x80;
	if (_buffered > k_LengthOffset)
	{
		std::fill(_buffer.begin() + static_cast<std::ptrdiff_t>(_buffered), _buffer.end(), uint8_t {0});
		Block(_buffer.data());
		_buffered = 0;
	}
	std::fill(_buffer.begin() + static_cast<std::ptrdiff_t>(_buffered), _buffer.begin() + k_LengthOffset, uint8_t {0});
	for (size_t i = 0; i < 8; ++i)
	{
		_buffer[k_LengthOffset + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
	}
	Block(_buffer.data());
	std::array<uint8_t, 20> hash {};
	for (size_t i = 0; i < _state.size(); ++i)
	{
		for (size_t j = 0; j < 4; ++j)
		{
			hash[i * 4 + j] = static_cast<uint8_t>(_state[i] >> (24 - 8 * j));
		}
	}
	return hash;
}

std::string ToHex(const std::array<uint8_t, 20>& hash)
{
	constexpr std::string_view k_Digits = "0123456789abcdef";
	std::string text;
	text.reserve(hash.size() * 2);
	for (const auto value : hash)
	{
		text.push_back(k_Digits[value >> 4]);
		text.push_back(k_Digits[value & 0x0f]);
	}
	return text;
}

std::string Sha1Hex(std::span<const std::byte> bytes)
{
	Sha1 sha;
	sha.Update(bytes);
	return ToHex(sha.Final());
}

std::string Sha1Hex(std::string_view text)
{
	Sha1 sha;
	sha.Update(text);
	return ToHex(sha.Final());
}

} // namespace moa::mapoverride
