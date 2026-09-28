#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/init.h>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;

    const wxString root = wxFileName::CreateTempFileName(wxS("itool-directory-sync-"));
    if (root.empty() || !wxRemoveFile(root)) return 2;

    wxFileName nested(root, wxEmptyString);
    nested.AppendDir(wxS("level-1"));
    nested.AppendDir(wxS("level-2"));
    if (!wxFileName::Mkdir(nested.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) return 3;

    const wxString source = wxFileName(nested.GetPath(), wxS("payload.txt")).GetFullPath();
    wxFFile file(source, wxS("wb"));
    if (!file.IsOpened() || !file.Write("nested", 6)) return 4;
    file.Close();

    wxArrayString files;
    const size_t count = wxDir::GetAllFiles(root, &files, wxEmptyString,
                                            wxDIR_FILES | wxDIR_DIRS);
    const bool found = count == 1 && files.size() == 1 && files[0] == source;
    wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
    return found ? 0 : 5;
}
