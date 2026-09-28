#pragma once

#include <stdint.h>
#include <wx/string.h>

namespace timeconv
{
bool ParseUtc(const wxString& text, int64_t& unixMilliseconds, wxString& error);
wxString FormatUtc(int64_t unixMilliseconds, bool includeMilliseconds);
bool ParseTimestamp(const wxString& text, bool milliseconds, int64_t& unixMilliseconds, wxString& error);
wxString FormatTimestamp(int64_t unixMilliseconds, bool milliseconds);
}
