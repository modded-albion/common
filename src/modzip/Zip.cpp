/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "modzip/Zip.h"

#include <cstring>

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include <miniz.h>

namespace moa::modzip
{

namespace
{
/// The type bits of a Unix file mode, and the value of a symbolic link.
constexpr uint32_t k_UnixTypeMask = 0170000;
constexpr uint32_t k_UnixSymbolicLink = 0120000;
/// The compression methods that the reader accepts: "store" and "deflate".
constexpr uint16_t k_MethodStore = 0;
constexpr uint16_t k_MethodDeflate = 8;

[[nodiscard]] std::string LowerAscii(std::string_view text)
{
	std::string lower(text);
	std::ranges::transform(lower, lower.begin(),
	                       [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
	return lower;
}

[[nodiscard]] std::string ZipError(const mz_zip_archive& zip)
{
	return mz_zip_get_error_string(mz_zip_get_last_error(const_cast<mz_zip_archive*>(&zip)));
}
} // namespace

Result<std::string> CheckEntryName(std::string_view name)
{
	if (name.empty())
	{
		return MakeError("an entry has no name");
	}
	if (name.size() > k_MaxEntryName)
	{
		return MakeError("the name of an entry has {} bytes; the limit is {}", name.size(), k_MaxEntryName);
	}
	for (const char c : name)
	{
		const auto code = static_cast<unsigned char>(c);
		if (code < 0x20 || code == 0x7F)
		{
			return MakeError("the entry name '{}' has a control character", name);
		}
		if (c == '\\' || c == ':')
		{
			return MakeError("the entry name '{}' has a '{}'; a zip of a mod uses only '/' between the parts", name, c);
		}
	}
	if (name.front() == '/')
	{
		return MakeError("the entry name '{}' is an absolute path", name);
	}
	auto path = name;
	if (path.back() == '/')
	{
		path.remove_suffix(1);
	}
	size_t start = 0;
	while (start <= path.size())
	{
		const auto end = std::min(path.find('/', start), path.size());
		const auto part = path.substr(start, end - start);
		if (part.empty() || part == "." || part == "..")
		{
			return MakeError("the entry name '{}' has an empty, '.' or '..' part", name);
		}
		if (part.back() == '.' || part.back() == ' ')
		{
			return MakeError("a part of the entry name '{}' ends with '.' or a space", name);
		}
		start = end + 1;
	}
	return std::string(path);
}

struct ZipArchive::Impl
{
	std::vector<std::byte> bytes;
	mz_zip_archive zip {};
	bool open = false;
	std::vector<ZipEntry> entries;
	std::map<std::string, uint32_t, std::less<>> indexOf;
	/// The reader of miniz keeps state in `zip`, so one read runs at a time.
	mutable std::mutex mutex;

	Impl() = default;
	Impl(const Impl&) = delete;
	Impl& operator=(const Impl&) = delete;
	~Impl()
	{
		if (open)
		{
			mz_zip_reader_end(&zip);
		}
	}
};

ZipArchive::ZipArchive(std::unique_ptr<Impl> impl)
    : _impl(std::move(impl))
{
}

ZipArchive::ZipArchive(ZipArchive&& other) noexcept = default;
ZipArchive& ZipArchive::operator=(ZipArchive&& other) noexcept = default;
ZipArchive::~ZipArchive() = default;

Result<ZipArchive> ZipArchive::Open(std::vector<std::byte> bytes, const ZipLimits& limits)
{
	if (bytes.size() > limits.maxArchiveBytes)
	{
		return MakeError("the zip has {} bytes; the limit is {}", bytes.size(), limits.maxArchiveBytes);
	}
	auto impl = std::make_unique<Impl>();
	impl->bytes = std::move(bytes);
	if (!mz_zip_reader_init_mem(&impl->zip, impl->bytes.data(), impl->bytes.size(), 0))
	{
		return MakeError("the file is not a zip: {}", ZipError(impl->zip));
	}
	impl->open = true;
	const auto count = mz_zip_reader_get_num_files(&impl->zip);
	if (count > limits.maxEntries)
	{
		return MakeError("the zip has {} entries; the limit is {}", count, limits.maxEntries);
	}
	std::set<std::string, std::less<>> lowerNames;
	std::set<std::string, std::less<>> folders;
	uint64_t total = 0;
	for (mz_uint index = 0; index < count; ++index)
	{
		std::array<char, 1024> buffer {};
		const auto length = mz_zip_reader_get_filename(&impl->zip, index, buffer.data(), static_cast<mz_uint>(buffer.size()));
		if (length == 0 || length >= buffer.size())
		{
			return MakeError("the entry {} of the zip has no name or a name that is too long", index);
		}
		const std::string_view rawName(buffer.data(), length - 1);
		mz_zip_archive_file_stat stat {};
		if (!mz_zip_reader_file_stat(&impl->zip, index, &stat))
		{
			return MakeError("the entry '{}' cannot be read: {}", rawName, ZipError(impl->zip));
		}
		auto name = CheckEntryName(rawName);
		if (!name)
		{
			return name.GetError();
		}
		const auto unixMode = stat.m_external_attr >> 16;
		if ((unixMode & k_UnixTypeMask) == k_UnixSymbolicLink)
		{
			return MakeError("the entry '{}' is a symbolic link", *name);
		}
		if (stat.m_is_encrypted)
		{
			return MakeError("the entry '{}' is encrypted", *name);
		}
		if (!lowerNames.insert(LowerAscii(*name)).second)
		{
			return MakeError("the zip has two entries with the name '{}'", *name);
		}
		if (stat.m_is_directory)
		{
			folders.insert(*name);
			continue;
		}
		if (!stat.m_is_supported || (stat.m_method != k_MethodStore && stat.m_method != k_MethodDeflate))
		{
			return MakeError("the entry '{}' uses a compression that the reader does not know", *name);
		}
		if (stat.m_uncomp_size > limits.maxEntryBytes)
		{
			return MakeError("the entry '{}' has {} bytes; the limit is {}", *name, stat.m_uncomp_size, limits.maxEntryBytes);
		}
		total += stat.m_uncomp_size;
		if (total > limits.maxTotalBytes)
		{
			return MakeError("the files of the zip have more than {} bytes", limits.maxTotalBytes);
		}
		impl->indexOf.emplace(*name, index);
		impl->entries.push_back(
		    ZipEntry {.name = std::move(*name), .size = stat.m_uncomp_size, .compressedSize = stat.m_comp_size});
	}
	std::ranges::sort(impl->entries, {}, &ZipEntry::name);
	// A file must not have the name of a folder: the folder of one entry, or an entry that ends with "/".
	for (const auto& entry : impl->entries)
	{
		for (auto slash = entry.name.find('/'); slash != std::string::npos; slash = entry.name.find('/', slash + 1))
		{
			folders.insert(entry.name.substr(0, slash));
		}
	}
	for (const auto& entry : impl->entries)
	{
		if (folders.contains(entry.name))
		{
			return MakeError("the zip has a file and a folder with the name '{}'", entry.name);
		}
	}
	return ZipArchive(std::move(impl));
}

std::span<const ZipEntry> ZipArchive::Entries() const
{
	return _impl->entries;
}

bool ZipArchive::Contains(std::string_view name) const
{
	return _impl->indexOf.contains(name);
}

Result<std::vector<std::byte>> ZipArchive::Read(std::string_view name) const
{
	const auto found = _impl->indexOf.find(name);
	if (found == _impl->indexOf.end())
	{
		return MakeError("the zip has no file '{}'", name);
	}
	const std::scoped_lock lock(_impl->mutex);
	mz_zip_archive_file_stat stat {};
	if (!mz_zip_reader_file_stat(&_impl->zip, found->second, &stat))
	{
		return MakeError("the entry '{}' cannot be read: {}", name, ZipError(_impl->zip));
	}
	std::vector<std::byte> bytes(static_cast<size_t>(stat.m_uncomp_size));
	if (bytes.empty())
	{
		return bytes;
	}
	// miniz writes not more than the size of the buffer, and checks the CRC-32 of the data.
	if (!mz_zip_reader_extract_to_mem(&_impl->zip, found->second, bytes.data(), bytes.size(), 0))
	{
		return MakeError("the entry '{}' is not correct: {}", name, ZipError(_impl->zip));
	}
	return bytes;
}

Result<std::vector<std::byte>> WriteZip(std::span<const ZipFile> files)
{
	std::set<std::string, std::less<>> lowerNames;
	for (const auto& file : files)
	{
		auto name = CheckEntryName(file.name);
		if (!name)
		{
			return name.GetError();
		}
		if (*name != file.name)
		{
			return MakeError("the file name '{}' ends with '/'", file.name);
		}
		if (!lowerNames.insert(LowerAscii(file.name)).second)
		{
			return MakeError("two files have the name '{}'", file.name);
		}
	}
	mz_zip_archive zip {};
	if (!mz_zip_writer_init_heap(&zip, 0, 0))
	{
		return MakeError("the zip writer does not start: {}", ZipError(zip));
	}
	const auto fail = [&zip](std::string message) -> Result<std::vector<std::byte>> {
		mz_zip_writer_end(&zip);
		return Error {std::move(message)};
	};
	for (const auto& file : files)
	{
		if (!mz_zip_writer_add_mem(&zip, file.name.c_str(), file.bytes.data(), file.bytes.size(),
		                           static_cast<mz_uint>(MZ_DEFAULT_LEVEL)))
		{
			return fail(std::format("the file '{}' does not go into the zip: {}", file.name, ZipError(zip)));
		}
	}
	void* buffer = nullptr;
	size_t size = 0;
	if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size))
	{
		return fail(std::format("the zip does not complete: {}", ZipError(zip)));
	}
	const std::unique_ptr<void, decltype(&mz_free)> owner(buffer, &mz_free);
	const auto* begin = static_cast<const std::byte*>(buffer);
	std::vector<std::byte> bytes(begin, begin + size);
	mz_zip_writer_end(&zip);
	return bytes;
}

} // namespace moa::modzip
