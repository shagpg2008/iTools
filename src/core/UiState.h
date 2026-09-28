#pragma once

#include <wx/string.h>

class wxConfigBase;
class wxWindow;

namespace uistate
{
void Save(wxWindow* root, wxConfigBase& config, const wxString& prefix);
void Load(wxWindow* root, wxConfigBase& config, const wxString& prefix);
}
