#include "core/AppPaths.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace apppaths
{
wxString UserDataDirectory()
{
#ifdef __WXMSW__
    return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
#else
    return wxFileName(wxGetHomeDir(), wxS(".iTool")).GetFullPath();
#endif
}

wxString ConfigurationFile()
{
    return wxFileName(UserDataDirectory(), wxS("iTool.ini")).GetFullPath();
}

wxString LogDirectory()
{
    return wxFileName(UserDataDirectory(), wxS("logs")).GetFullPath();
}

bool EnsureUserDataDirectory()
{
    const wxString path = UserDataDirectory();
    return wxFileName::DirExists(path) ||
           wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

bool EnsureLogDirectory()
{
    const wxString path = LogDirectory();
    return wxFileName::DirExists(path) ||
           wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}
}
