#pragma once

#include <vector>
#include <wx/string.h>

namespace barcode
{
bool EncodeCode128B(const wxString& text, std::vector<int>& symbolValues,
                    std::vector<int>& barWidths, wxString& error);
}
