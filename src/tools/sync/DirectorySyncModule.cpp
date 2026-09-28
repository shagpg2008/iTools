#include "tools/sync/DirectorySyncModule.h"

#include "core/Localization.h"

#include <algorithm>
#include <map>
#include <vector>

#include <wx/button.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace
{
struct FileInfo
{
    wxString path;
    wxString relative;
    wxULongLong size;
    wxDateTime modified;
};

struct SyncOperation
{
    wxString source;
    wxString target;
    wxString relative;
    bool leftToRight;
    bool overwrite;
};

typedef std::map<wxString, FileInfo> FileMap;

wxString KeyFor(const wxString& relative)
{
    wxString key(relative);
#ifdef __WXMSW__
    key.MakeLower();
#endif
    return key;
}

wxString PathUnderRoot(const wxString& root, const wxString& relative)
{
    // The two-argument wxFileName(path, name) constructor must not be used
    // here: "name" is required to contain no directory components and paths
    // such as "subdir/file.txt" consequently lose their subdirectory.
    wxFileName path(relative);
    if (!path.MakeAbsolute(root)) return wxString();
    path.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return path.GetFullPath();
}

bool IsSameOrInside(const wxString& possibleChild, const wxString& possibleParent)
{
    wxFileName child(possibleChild, wxEmptyString);
    wxFileName parent(possibleParent, wxEmptyString);
    child.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    parent.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    wxString childPath = child.GetPathWithSep();
    wxString parentPath = parent.GetPathWithSep();
#ifdef __WXMSW__
    childPath.MakeLower();
    parentPath.MakeLower();
#endif
    return childPath.StartsWith(parentPath);
}

bool ScanDirectory(const wxString& root, FileMap& result, wxString& error)
{
    wxArrayString paths;
    // wxDir::Traverse only descends when wxDIR_DIRS is present.  wxDIR_FILES
    // alone scans the selected folder itself but silently skips all children.
    if (wxDir::GetAllFiles(root, &paths, wxEmptyString,
                           wxDIR_FILES | wxDIR_DIRS) == (size_t)-1)
    {
        error = ITOOL_TR("Unable to read directory:") + root;
        return false;
    }

    for (size_t i = 0; i < paths.size(); ++i)
    {
        wxFileName name(paths[i]);
        wxFileName relative(paths[i]);
        if (!relative.MakeRelativeTo(root))
        {
            error = ITOOL_TR("Unable to calculate relative path:") + paths[i];
            return false;
        }
        FileInfo info;
        info.path = paths[i];
        info.relative = relative.GetFullPath();
        info.size = name.GetSize();
        info.modified = name.GetModificationTime();
        result[KeyFor(info.relative)] = info;
    }
    return true;
}

class DirectorySyncPanel : public wxPanel
{
public:
    explicit DirectorySyncPanel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxFlexGridSizer* paths = new wxFlexGridSizer(3, 6, 6);
        paths->AddGrowableCol(1, 1);
        paths->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Folder 1:")), 0, wxALIGN_CENTER_VERTICAL);
        m_left = new wxTextCtrl(this, wxID_ANY);
        paths->Add(m_left, 1, wxEXPAND);
        wxButton* browseLeft = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1));
        paths->Add(browseLeft);
        paths->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Folder 2:")), 0, wxALIGN_CENTER_VERTICAL);
        m_right = new wxTextCtrl(this, wxID_ANY);
        paths->Add(m_right, 1, wxEXPAND);
        wxButton* browseRight = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1));
        paths->Add(browseRight);
        root->Add(paths, 0, wxEXPAND | wxALL, 10);

        m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxBORDER_SUNKEN);
        m_list->InsertColumn(0, ITOOL_TR("folder 1"), wxLIST_FORMAT_LEFT, 290);
        m_list->InsertColumn(1, ITOOL_TR("direction"), wxLIST_FORMAT_CENTER, 70);
        m_list->InsertColumn(2, ITOOL_TR("Folder 2"), wxLIST_FORMAT_LEFT, 290);
        root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

        wxBoxSizer* actions = new wxBoxSizer(wxHORIZONTAL);
        m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("A sync preview will be automatically generated after selecting two folders."));
        actions->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        m_sync = new wxButton(this, wxID_ANY, ITOOL_TR("synchronous"), wxDefaultPosition, wxSize(110, -1));
        wxButton* cancel = new wxButton(this, wxID_ANY, ITOOL_TR("Cancel"), wxDefaultPosition, wxSize(110, -1));
        actions->Add(m_sync, 0, wxRIGHT, 8);
        actions->Add(cancel);
        root->Add(actions, 0, wxEXPAND | wxALL, 10);
        SetSizer(root);

        browseLeft->Bind(wxEVT_BUTTON, &DirectorySyncPanel::BrowseLeft, this);
        browseRight->Bind(wxEVT_BUTTON, &DirectorySyncPanel::BrowseRight, this);
        m_sync->Bind(wxEVT_BUTTON, &DirectorySyncPanel::Synchronize, this);
        cancel->Bind(wxEVT_BUTTON, &DirectorySyncPanel::Cancel, this);
        m_left->Bind(wxEVT_KILL_FOCUS, &DirectorySyncPanel::PathChanged, this);
        m_right->Bind(wxEVT_KILL_FOCUS, &DirectorySyncPanel::PathChanged, this);
        m_list->Bind(wxEVT_SIZE, &DirectorySyncPanel::ListResized, this);
    }

private:
    void ListResized(wxSizeEvent& event)
    {
        event.Skip(); const int total = m_list->GetClientSize().x - 2; if (total < 240) return;
        const int direction = std::max(64, total * 12 / 100);
        const int side = (total - direction) / 2;
        m_list->SetColumnWidth(0, side); m_list->SetColumnWidth(1, direction);
        m_list->SetColumnWidth(2, total - side - direction);
    }
    void Browse(wxTextCtrl* target)
    {
        wxDirDialog dialog(this, ITOOL_TR("Select folders to sync"), target->GetValue(), wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
        {
            target->SetValue(dialog.GetPath());
            Preview();
        }
    }

    void BrowseLeft(wxCommandEvent&) { Browse(m_left); }
    void BrowseRight(wxCommandEvent&) { Browse(m_right); }
    void PathChanged(wxFocusEvent& event) { Preview(); event.Skip(); }

    bool BuildPlan(wxString& error)
    {
        m_operations.clear();
        m_list->DeleteAllItems();
        const wxString leftRoot = m_left->GetValue();
        const wxString rightRoot = m_right->GetValue();
        if (!wxFileName::DirExists(leftRoot) || !wxFileName::DirExists(rightRoot))
        {
            error = ITOOL_TR("Please select two valid folders.");
            return false;
        }
        if (IsSameOrInside(leftRoot, rightRoot) || IsSameOrInside(rightRoot, leftRoot))
        {
            error = ITOOL_TR("The two folders cannot be the same, nor can they be subdirectories of each other.");
            return false;
        }

        FileMap leftFiles, rightFiles;
        if (!ScanDirectory(leftRoot, leftFiles, error) || !ScanDirectory(rightRoot, rightFiles, error))
            return false;

        FileMap all(leftFiles);
        all.insert(rightFiles.begin(), rightFiles.end());
        for (FileMap::const_iterator it = all.begin(); it != all.end(); ++it)
        {
            FileMap::const_iterator left = leftFiles.find(it->first);
            FileMap::const_iterator right = rightFiles.find(it->first);
            SyncOperation operation;
            if (left == leftFiles.end())
            {
                operation.source = right->second.path;
                operation.target = PathUnderRoot(leftRoot, right->second.relative);
                operation.relative = right->second.relative;
                operation.leftToRight = false;
                operation.overwrite = false;
            }
            else if (right == rightFiles.end())
            {
                operation.source = left->second.path;
                operation.target = PathUnderRoot(rightRoot, left->second.relative);
                operation.relative = left->second.relative;
                operation.leftToRight = true;
                operation.overwrite = false;
            }
            else
            {
                const bool sameSize = left->second.size == right->second.size;
                const wxTimeSpan difference = left->second.modified - right->second.modified;
                const wxLongLong seconds = difference.GetSeconds();
                if (sameSize && seconds >= -2 && seconds <= 2)
                    continue;
                if (seconds == 0)
                {
                    error = ITOOL_TR("Files with the same name have different sizes but the same modification time, so the direction cannot be safely determined:") + left->second.relative;
                    return false;
                }
                operation.leftToRight = seconds > 0;
                operation.source = operation.leftToRight ? left->second.path : right->second.path;
                operation.target = operation.leftToRight ? right->second.path : left->second.path;
                operation.relative = left->second.relative;
                operation.overwrite = true;
            }
            if (operation.target.empty())
            {
                error = ITOOL_TR("Unable to calculate relative path:") + operation.relative;
                return false;
            }
            m_operations.push_back(operation);
        }

        for (size_t i = 0; i < m_operations.size(); ++i)
        {
            const SyncOperation& operation = m_operations[i];
            const wxString leftText = operation.leftToRight ? operation.relative :
                                      (operation.overwrite ? operation.relative : wxString());
            const wxString rightText = operation.leftToRight ?
                                       (operation.overwrite ? operation.relative : wxString()) : operation.relative;
            long row = m_list->InsertItem(static_cast<long>(i), leftText);
            m_list->SetItem(row, 1, operation.leftToRight ? wxS("→") : wxS("←"));
            m_list->SetItem(row, 2, rightText);
        }
        m_status->SetLabel(wxString::Format(ITOOL_TR("To be synchronized: %lu files"),
                                            static_cast<unsigned long>(m_operations.size())));
        return true;
    }

    void Preview()
    {
        if (m_left->GetValue().empty() || m_right->GetValue().empty())
            return;
        wxBusyCursor busy;
        wxString error;
        if (!BuildPlan(error)) m_status->SetLabel(error);
    }

    void Synchronize(wxCommandEvent&)
    {
        wxBusyCursor busy;
        wxString error;
        if (!BuildPlan(error))
        {
            wxMessageBox(error, ITOOL_TR("Folder sync"), wxOK | wxICON_WARNING, this);
            return;
        }
        if (m_operations.empty())
        {
            wxMessageBox(ITOOL_TR("Both folders have been synchronized."), ITOOL_TR("Folder sync"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        if (wxMessageBox(wxString::Format(ITOOL_TR("%lu files will be copied; newer files of the same name will overwrite older files. Continue?"),
                                          static_cast<unsigned long>(m_operations.size())),
                         ITOOL_TR("Confirm synchronization"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
            return;

        size_t completed = 0;
        for (size_t i = 0; i < m_operations.size(); ++i)
        {
            wxFileName target(m_operations[i].target);
            if (!wxFileName::Mkdir(target.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) ||
                !wxCopyFile(m_operations[i].source, m_operations[i].target, true))
            {
                wxMessageBox(wxString::Format(ITOOL_TR("Completed %lu files, copy failed:\n%s"),
                                              static_cast<unsigned long>(completed), m_operations[i].target),
                             ITOOL_TR("Synchronization interruption"), wxOK | wxICON_ERROR, this);
                Preview();
                return;
            }
            ++completed;
        }
        Preview();
        wxMessageBox(wxString::Format(ITOOL_TR("Synchronization completed, %lu files copied in total."),
                                      static_cast<unsigned long>(completed)),
                     ITOOL_TR("Folder sync"), wxOK | wxICON_INFORMATION, this);
    }

    void Cancel(wxCommandEvent&)
    {
        m_left->Clear();
        m_right->Clear();
        m_list->DeleteAllItems();
        m_operations.clear();
        m_status->SetLabel(ITOOL_TR("Canceled."));
    }

    wxTextCtrl* m_left;
    wxTextCtrl* m_right;
    wxListCtrl* m_list;
    wxButton* m_sync;
    wxStaticText* m_status;
    std::vector<SyncOperation> m_operations;
};
}

wxString DirectorySyncModule::GetId() const { return wxS("directory-sync"); }
wxString DirectorySyncModule::GetName() const { return ITOOL_TR("Directory sync"); }
wxString DirectorySyncModule::GetDescription() const { return ITOOL_TR("Compare two directories and synchronize newer or one-sided files"); }
wxWindow* DirectorySyncModule::CreatePanel(wxWindow* parent) { return new DirectorySyncPanel(parent); }
