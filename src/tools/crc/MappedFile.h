#pragma once

#include <cstddef>
#include <functional>

#include <wx/string.h>

#include "third_party/crc/crc.h"

typedef std::function<bool(const u8* data, size_t length)> MappedChunkVisitor;

// Visits a file through bounded read-only memory mappings. Bounded views keep
// this usable in a 32-bit process even when the input file is very large.
bool VisitMappedFile(const wxString& path,
                     const MappedChunkVisitor& visitor,
                     u64& totalLength,
                     wxString& error);

