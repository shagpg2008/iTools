#include "tools/zeropad/ZeroPadToolModule.h"

#include <map>
#include <set>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "core/Localization.h"

namespace
{
struct RenameOperation
{
    wxString source;
    wxString target;
    wxString oldName;
    wxString newName;
};

size_t LeadingDigitCount(const wxString& value)
{
    size_t count = 0;
    while (count < value.length() && value[count] >= '0' && value[count] <= '9') ++count;
    return count;
}

wxString PathKey(wxString path)
{
#ifdef __WXMSW__
    path.MakeLower();
#endif
    return path;
}

class ZeroPadPanel : public wxPanel
{
public:
    explicit ZeroPadPanel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxFlexGridSizer* form = new wxFlexGridSizer(3, 7, 7); form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Directory:")), 0, wxALIGN_CENTER_VERTICAL);
        m_directory = new wxTextCtrl(this, wxID_ANY); form->Add(m_directory, 1, wxEXPAND);
        wxButton* browse = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1)); form->Add(browse);
        root->Add(form, 0, wxEXPAND | wxALL, 10);

        wxBoxSizer* options = new wxBoxSizer(wxHORIZONTAL);
        m_autoWidth = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Determine width from the largest number")); m_autoWidth->SetValue(true);
        m_recursive = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Include subdirectories"));
        options->Add(m_autoWidth, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 18);
        options->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Manual width:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        m_width = new wxSpinCtrl(this, wxID_ANY, wxS("2"), wxDefaultPosition, wxSize(65, -1), wxSP_ARROW_KEYS, 1, 32, 2);
        options->Add(m_width, 0, wxRIGHT, 18); options->Add(m_recursive, 0, wxALIGN_CENTER_VERTICAL);
        root->Add(options, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxBORDER_SUNKEN);
        m_list->InsertColumn(0, ITOOL_TR("Original filename"), wxLIST_FORMAT_LEFT, 285);
        m_list->InsertColumn(1, ITOOL_TR("Direction"), wxLIST_FORMAT_CENTER, 65);
        m_list->InsertColumn(2, ITOOL_TR("Zero-padded filename"), wxLIST_FORMAT_LEFT, 285);
        root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

        wxBoxSizer* actions = new wxBoxSizer(wxHORIZONTAL);
        m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Only filenames beginning with digits are processed; remaining text and extensions are preserved."));
        wxButton* preview = new wxButton(this, wxID_ANY, ITOOL_TR("Preview"));
        wxButton* execute = new wxButton(this, wxID_ANY, ITOOL_TR("Rename"));
        actions->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        actions->Add(preview, 0, wxRIGHT, 8); actions->Add(execute);
        root->Add(actions, 0, wxEXPAND | wxALL, 10); SetSizer(root);

        browse->Bind(wxEVT_BUTTON, &ZeroPadPanel::Browse, this);
        preview->Bind(wxEVT_BUTTON, &ZeroPadPanel::Preview, this);
        execute->Bind(wxEVT_BUTTON, &ZeroPadPanel::Execute, this);
        m_autoWidth->Bind(wxEVT_CHECKBOX, &ZeroPadPanel::AutoWidthChanged, this);
        m_list->Bind(wxEVT_SIZE, &ZeroPadPanel::ListResized, this);
        m_width->Enable(false);
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
    void Browse(wxCommandEvent&)
    {
        wxDirDialog dialog(this, ITOOL_TR("Select directory for filename zero-padding"), m_directory->GetValue(), wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK) { m_directory->SetValue(dialog.GetPath()); wxCommandEvent event; Preview(event); }
    }

    void AutoWidthChanged(wxCommandEvent&) { m_width->Enable(!m_autoWidth->GetValue()); }

    void CollectFiles(wxArrayString& files) const
    {
        if (m_recursive->GetValue())
            wxDir::GetAllFiles(m_directory->GetValue(), &files, wxEmptyString,
                               wxDIR_FILES | wxDIR_DIRS);
        else
        {
            wxDir directory(m_directory->GetValue()); wxString name;
            for (bool more = directory.GetFirst(&name, wxEmptyString, wxDIR_FILES); more; more = directory.GetNext(&name))
                files.Add(wxFileName(m_directory->GetValue(), name).GetFullPath());
        }
    }

    bool BuildPlan(wxString& error)
    {
        m_operations.clear(); m_list->DeleteAllItems();
        if (!wxFileName::DirExists(m_directory->GetValue())) { error = ITOOL_TR("Select a valid directory."); return false; }
        wxArrayString files; CollectFiles(files);
        const size_t manualWidth = static_cast<size_t>(m_width->GetValue());
        std::map<wxString, size_t> directoryWidths;
        if (m_autoWidth->GetValue())
        {
            for (size_t i = 0; i < files.size(); ++i)
            {
                const wxFileName file(files[i]);
                const size_t digits = LeadingDigitCount(file.GetName());
                const wxString directory = PathKey(file.GetPath());
                if (digits > directoryWidths[directory]) directoryWidths[directory] = digits;
            }
        }

        std::set<wxString> targets;
        for (size_t i = 0; i < files.size(); ++i)
        {
            wxFileName source(files[i]); const wxString stem = source.GetName();
            const size_t digits = LeadingDigitCount(stem);
            const size_t width = m_autoWidth->GetValue()
                ? directoryWidths[PathKey(source.GetPath())] : manualWidth;
            if (!digits || digits >= width) continue;
            const wxString padded = wxString('0', width - digits) + stem;
            wxFileName target(source.GetPath(), padded, source.GetExt());
            RenameOperation operation;
            operation.source = source.GetFullPath(); operation.target = target.GetFullPath();
            operation.oldName = source.GetFullName(); operation.newName = target.GetFullName();
            const wxString key = PathKey(operation.target);
            if (!targets.insert(key).second || wxFileName::FileExists(operation.target))
            {
                error = ITOOL_TR("Zero-padding would create a duplicate filename: ") + operation.target;
                m_operations.clear(); return false;
            }
            m_operations.push_back(operation);
        }
        for (size_t i = 0; i < m_operations.size(); ++i)
        {
            wxString relativeDirectory = wxFileName(m_operations[i].source).GetPath();
            wxFileName relative(relativeDirectory, wxEmptyString); relative.MakeRelativeTo(m_directory->GetValue());
            const wxString prefix = relative.GetPath() == wxS(".") || relative.GetPath().empty() ? wxString() : relative.GetPathWithSep();
            const long row = m_list->InsertItem(static_cast<long>(i), prefix + m_operations[i].oldName);
            m_list->SetItem(row, 1, wxS("→"));
            m_list->SetItem(row, 2, prefix + m_operations[i].newName);
        }
        if (m_autoWidth->GetValue())
            m_status->SetLabel(wxString::Format(ITOOL_TR("Width is calculated separately for each folder; %lu files will be renamed."),
                static_cast<unsigned long>(m_operations.size())));
        else
            m_status->SetLabel(wxString::Format(ITOOL_TR("Target width: %lu digits; %lu files will be renamed."),
                static_cast<unsigned long>(manualWidth), static_cast<unsigned long>(m_operations.size())));
        return true;
    }

    void Preview(wxCommandEvent&)
    {
        wxBusyCursor busy; wxString error;
        if (!BuildPlan(error)) wxMessageBox(error, ITOOL_TR("Filename zero-padding"), wxOK | wxICON_WARNING, this);
    }

    void Execute(wxCommandEvent&)
    {
        wxBusyCursor busy; wxString error;
        if (!BuildPlan(error)) { wxMessageBox(error, ITOOL_TR("Filename zero-padding"), wxOK | wxICON_WARNING, this); return; }
        if (m_operations.empty()) { wxMessageBox(ITOOL_TR("No files need zero-padding."), ITOOL_TR("Filename zero-padding"), wxOK | wxICON_INFORMATION, this); return; }
        if (wxMessageBox(wxString::Format(ITOOL_TR("About to rename %lu files. Continue?"), static_cast<unsigned long>(m_operations.size())),
                         ITOOL_TR("Confirm rename"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) return;
        size_t completed = 0;
        for (size_t i = 0; i < m_operations.size(); ++i)
        {
            if (!wxRenameFile(m_operations[i].source, m_operations[i].target, false))
            {
                wxMessageBox(wxString::Format(ITOOL_TR("%lu files completed; failed to rename:\n%s"),
                    static_cast<unsigned long>(completed), m_operations[i].source), ITOOL_TR("Rename interrupted"), wxOK | wxICON_ERROR, this);
                BuildPlan(error); return;
            }
            ++completed;
        }
        BuildPlan(error);
        wxMessageBox(wxString::Format(ITOOL_TR("Completed: %lu files renamed."), static_cast<unsigned long>(completed)),
                     ITOOL_TR("Filename zero-padding"), wxOK | wxICON_INFORMATION, this);
    }

    wxTextCtrl* m_directory; wxCheckBox *m_autoWidth, *m_recursive; wxSpinCtrl* m_width;
    wxListCtrl* m_list; wxStaticText* m_status; std::vector<RenameOperation> m_operations;
};
}

wxString ZeroPadToolModule::GetId() const { return wxS("zero-pad-filenames"); }
wxString ZeroPadToolModule::GetName() const { return ITOOL_TR("Zero-pad filenames"); }
wxString ZeroPadToolModule::GetDescription() const { return ITOOL_TR("Add leading zeros to numbered filenames for natural sorting"); }
wxWindow* ZeroPadToolModule::CreatePanel(wxWindow* parent) { return new ZeroPadPanel(parent); }
