#pragma once

#include <wx/string.h>

namespace apppaths
{
wxString UserDataDirectory();
wxString ConfigurationFile();
wxString LogDirectory();
bool EnsureUserDataDirectory();
bool EnsureLogDirectory();
}
