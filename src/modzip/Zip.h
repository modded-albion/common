/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>

namespace moa::modzip
{

/// The limits of a zip that the readers accept. A zip that goes above one of them is refused before any entry is
/// extracted, so a small zip cannot fill the memory (a zip bomb).
struct ZipLimits
{
	uint64_t maxArchiveBytes = 1024ull * 1024 * 1024; ///< The size of the zip file.
	size_t maxEntries = 4096;                         ///< The count of entries, with the folders.
	uint64_t maxEntryBytes = 256ull * 1024 * 1024;    ///< The extracted size of one file.
	uint64_t maxTotalBytes = 1024ull * 1024 * 1024;   ///< The extracted size of all files.
};

/// One file in a zip. Folders are not entries.
struct ZipEntry
{
	std::string name; ///< The path in the zip, with "/" between the parts.
	uint64_t size = 0;
	uint64_t compressedSize = 0;
};

/// Check the name of an entry. Return the name of the file or folder with no "/" at the end, or an error if the name
/// can point outside the root of the zip or is not a portable name:
/// - an empty name, a name that starts with "/", an empty part, or a part that is "." or "..",
/// - a "\" or a ":" (a drive letter or a stream), or a control character,
/// - a part that ends with "." or a space (Windows removes them),
/// - a name that is longer than `k_MaxEntryName` bytes.
[[nodiscard]] Result<std::string> CheckEntryName(std::string_view name);

/// The longest name of an entry, in bytes.
constexpr size_t k_MaxEntryName = 240;

/// A zip archive in memory. `Open` checks all the entries; `Read` extracts one file.
///
/// `Open` refuses the whole zip if one entry is not safe: a name that `CheckEntryName` refuses, a symbolic link, an
/// encrypted entry, a compression other than "store" and "deflate", two entries with the same name (the case of
/// ASCII letters is ignored), a file with the name of a folder, or a size above the limits. `Read` checks the size
/// and the CRC-32 of the data.
class ZipArchive
{
public:
	[[nodiscard]] static Result<ZipArchive> Open(std::vector<std::byte> bytes, const ZipLimits& limits = {});

	ZipArchive(ZipArchive&& other) noexcept;
	ZipArchive& operator=(ZipArchive&& other) noexcept;
	ZipArchive(const ZipArchive&) = delete;
	ZipArchive& operator=(const ZipArchive&) = delete;
	~ZipArchive();

	/// The files, sorted by name.
	[[nodiscard]] std::span<const ZipEntry> Entries() const;

	/// Return true if the zip has a file with this name (the exact name).
	[[nodiscard]] bool Contains(std::string_view name) const;

	/// Extract one file. Return an error if the zip has no such file or the data is not correct.
	[[nodiscard]] Result<std::vector<std::byte>> Read(std::string_view name) const;

private:
	struct Impl;
	explicit ZipArchive(std::unique_ptr<Impl> impl);

	std::unique_ptr<Impl> _impl;
};

/// One file for `WriteZip`.
struct ZipFile
{
	std::string name; ///< The path in the zip, with "/" between the parts. `CheckEntryName` must accept it.
	std::vector<std::byte> bytes;
};

/// Make a zip in memory with the files in this order, compressed with "deflate". Each entry gets the same date, so
/// the same files make the same zip. Return an error if a name is not correct or two names are the same.
[[nodiscard]] Result<std::vector<std::byte>> WriteZip(std::span<const ZipFile> files);

} // namespace moa::modzip
