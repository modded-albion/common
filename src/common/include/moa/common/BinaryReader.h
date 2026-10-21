/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <bit>
#include <span>
#include <string>
#include <type_traits>

namespace moa
{

enum class Endian : uint8_t
{
	Little,
	Big,
};

/// A cursor over bytes that checks every read against the end of the data.
///
/// A read past the end does not crash. It returns zero or an empty value and sets the failed flag. Read all the
/// fields, then check `Failed()` one time.
class BinaryReader
{
public:
	explicit BinaryReader(std::span<const std::byte> data, Endian endian = Endian::Little)
	    : _data(data)
	    , _endian(endian)
	{
	}

	void SetEndian(Endian endian) { _endian = endian; }
	[[nodiscard]] Endian GetEndian() const { return _endian; }

	[[nodiscard]] size_t Position() const { return _position; }
	[[nodiscard]] size_t Size() const { return _data.size(); }
	[[nodiscard]] size_t Remaining() const { return _data.size() - _position; }
	[[nodiscard]] bool Failed() const { return _failed; }

	/// Move the cursor to a position. A position after the end sets the failed flag.
	void Seek(size_t position)
	{
		if (position > _data.size())
		{
			_failed = true;
			_position = _data.size();
			return;
		}
		_position = position;
	}

	void Skip(size_t count) { Seek(_position + count); }

	/// Move the cursor forward to the next multiple of the alignment.
	void Align(size_t alignment)
	{
		if (alignment > 1)
		{
			Seek((_position + alignment - 1) / alignment * alignment);
		}
	}

	template <typename T>
	    requires std::is_arithmetic_v<T>
	[[nodiscard]] T Read()
	{
		if (Remaining() < sizeof(T))
		{
			_failed = true;
			_position = _data.size();
			return T {};
		}
		T value;
		std::memcpy(&value, _data.data() + _position, sizeof(T));
		_position += sizeof(T);
		if (NeedsSwap())
		{
			value = Swap(value);
		}
		return value;
	}

	/// Return a view of the next bytes. The view stays valid while the data stays valid.
	[[nodiscard]] std::span<const std::byte> ReadBytes(size_t count)
	{
		if (Remaining() < count)
		{
			_failed = true;
			_position = _data.size();
			return {};
		}
		auto bytes = _data.subspan(_position, count);
		_position += count;
		return bytes;
	}

	/// Read text that ends with a zero byte. The zero byte is not part of the text.
	[[nodiscard]] std::string ReadCString()
	{
		const auto* begin = reinterpret_cast<const char*>(_data.data() + _position);
		const auto* end = static_cast<const char*>(std::memchr(begin, 0, Remaining()));
		if (end == nullptr)
		{
			_failed = true;
			_position = _data.size();
			return {};
		}
		std::string text(begin, end);
		_position += text.size() + 1;
		return text;
	}

	/// Read text that starts with its length as a 32-bit integer, as Unity writes it.
	[[nodiscard]] std::string ReadSizedString()
	{
		const auto length = Read<uint32_t>();
		const auto bytes = ReadBytes(length);
		return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
	}

private:
	[[nodiscard]] bool NeedsSwap() const { return (_endian == Endian::Big) != (std::endian::native == std::endian::big); }

	template <typename T>
	[[nodiscard]] static T Swap(T value)
	{
		if constexpr (sizeof(T) == 1)
		{
			return value;
		}
		else
		{
			using Unsigned =
			    std::conditional_t<sizeof(T) == 2, uint16_t, std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>;
			auto bits = std::bit_cast<Unsigned>(value);
			Unsigned swapped = 0;
			for (size_t i = 0; i < sizeof(T); ++i)
			{
				swapped = static_cast<Unsigned>((swapped << 8) | (bits & 0xff));
				bits = static_cast<Unsigned>(bits >> 8);
			}
			return std::bit_cast<T>(swapped);
		}
	}

	std::span<const std::byte> _data;
	size_t _position = 0;
	Endian _endian;
	bool _failed = false;
};

} // namespace moa
