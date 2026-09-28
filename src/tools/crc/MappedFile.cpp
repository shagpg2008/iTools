#include "tools/crc/MappedFile.h"

#include <algorithm>

#include "core/Localization.h"

#ifdef __WXMSW__
#include <windows.h>
#include <wx/msw/winundef.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
const u64 kViewSize = CRC_U64_C(64) * 1024 * 1024;
}

#ifdef __WXMSW__
bool VisitMappedFile(const wxString& path,
                     const MappedChunkVisitor& visitor,
                     u64& totalLength,
                     wxString& error)
{
    totalLength = 0;
    HANDLE file = ::CreateFileW(path.wc_str(), GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = wxString::Format(ITOOL_TR("Unable to open file: %s (error %lu)"),
                                 path, ::GetLastError());
        return false;
    }

    LARGE_INTEGER size;
    if (!::GetFileSizeEx(file, &size) || size.QuadPart < 0)
    {
        error = wxString::Format(ITOOL_TR("Unable to get file size (error %lu)"),
                                 ::GetLastError());
        ::CloseHandle(file);
        return false;
    }

    totalLength = static_cast<u64>(size.QuadPart);
    if (totalLength == 0)
    {
        ::CloseHandle(file);
        return true;
    }

    HANDLE mapping = ::CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (mapping == NULL)
    {
        error = wxString::Format(ITOOL_TR("Unable to create file mapping (error %lu)"),
                                 ::GetLastError());
        ::CloseHandle(file);
        return false;
    }

    bool ok = true;
    u64 offset = 0;
    while (offset < totalLength)
    {
        const size_t length = static_cast<size_t>(
            std::min<u64>(kViewSize, totalLength - offset));
        const DWORD offsetHigh = static_cast<DWORD>(offset >> 32);
        const DWORD offsetLow = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
        const u8* data = static_cast<const u8*>(
            ::MapViewOfFile(mapping, FILE_MAP_READ, offsetHigh, offsetLow, length));
        if (data == NULL)
        {
            error = wxString::Format(ITOOL_TR("Failed to map file (offset %llu, error %lu)"),
                                     static_cast<unsigned long long>(offset),
                                     ::GetLastError());
            ok = false;
            break;
        }

        const bool visited = visitor(data, length);
        ::UnmapViewOfFile(data);
        if (!visited)
        {
            error = ITOOL_TR("Incremental CRC calculation failed.");
            ok = false;
            break;
        }
        offset += length;
    }

    ::CloseHandle(mapping);
    ::CloseHandle(file);
    return ok;
}
#else
bool VisitMappedFile(const wxString& path,
                     const MappedChunkVisitor& visitor,
                     u64& totalLength,
                     wxString& error)
{
    totalLength = 0;
    const wxCharBuffer nativePath = path.fn_str();
    const int file = ::open(nativePath.data(), O_RDONLY);
    if (file < 0)
    {
        error = wxString::Format(ITOOL_TR("Unable to open file: %s (%s)"), path,
                                 wxString::FromUTF8(std::strerror(errno)));
        return false;
    }

    struct stat info;
    if (::fstat(file, &info) != 0 || info.st_size < 0)
    {
        error = wxString::Format(ITOOL_TR("Unable to get file size (%s)"),
                                 wxString::FromUTF8(std::strerror(errno)));
        ::close(file);
        return false;
    }

    totalLength = static_cast<u64>(info.st_size);
    bool ok = true;
    u64 offset = 0;
    while (offset < totalLength)
    {
        const size_t length = static_cast<size_t>(
            std::min<u64>(kViewSize, totalLength - offset));
        void* view = ::mmap(NULL, length, PROT_READ, MAP_PRIVATE, file,
                            static_cast<off_t>(offset));
        if (view == MAP_FAILED)
        {
            error = wxString::Format(ITOOL_TR("Failed to map file (offset %llu, %s)"),
                                     static_cast<unsigned long long>(offset),
                                     wxString::FromUTF8(std::strerror(errno)));
            ok = false;
            break;
        }

        const bool visited = visitor(static_cast<const u8*>(view), length);
        ::munmap(view, length);
        if (!visited)
        {
            error = ITOOL_TR("Incremental CRC calculation failed.");
            ok = false;
            break;
        }
        offset += length;
    }

    ::close(file);
    return ok;
}
#endif
