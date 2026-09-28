#pragma once

#include <wx/string.h>
#include <vector>

namespace asciiconv
{
bool ParseHexBytes(const wxString& input, std::vector<unsigned char>& bytes, wxString& error);
// Map each byte to the same Unicode code point (Latin-1), without locale conversion.
wxString BytesToText(const std::vector<unsigned char>& bytes);
bool TextToHex(const wxString& input, wxString& hex, wxString& error);
}
