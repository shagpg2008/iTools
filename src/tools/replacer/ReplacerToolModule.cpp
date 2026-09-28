#include "tools/replacer/ReplacerToolModule.h"

#include <algorithm>
#include <set>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dir.h>
#include <wx/dnd.h>
#include <wx/dirdlg.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/regex.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/strconv.h>

#include "core/Localization.h"

namespace
{
enum TextEncoding { EncGb18030, EncUtf8, EncUtf8Bom, EncUtf16LE, EncUtf16BE, EncLocal };

struct Operation
{
    wxString oldPath, newPath;
    size_t matches;
    bool changeContent;
    bool directory;
    TextEncoding encoding;
};

class PathCollector : public wxDirTraverser
{
public:
    PathCollector(wxArrayString& files, wxArrayString& directories)
        : m_files(files), m_directories(directories) {}
    wxDirTraverseResult OnFile(const wxString& path) wxOVERRIDE
        { m_files.Add(path); return wxDIR_CONTINUE; }
    wxDirTraverseResult OnDir(const wxString& path) wxOVERRIDE
        { m_directories.Add(path); return wxDIR_CONTINUE; }
private:
    wxArrayString& m_files;
    wxArrayString& m_directories;
};

wxCSConv& Gb18030Converter()
{
    static wxCSConv converter(wxS("GB18030"));
    return converter;
}

const wxMBConv& Converter(TextEncoding encoding)
{
    static wxMBConvUTF16LE utf16le;
    static wxMBConvUTF16BE utf16be;
    if (encoding == EncUtf16LE) return utf16le;
    if (encoding == EncUtf16BE) return utf16be;
    if (encoding == EncLocal) return wxConvLocal;
    if (encoding == EncGb18030) return Gb18030Converter();
    return wxConvUTF8;
}

bool ReadText(const wxString& path, wxString& text, TextEncoding& encoding)
{
    wxFFile file(path, wxS("rb"));
    if (!file.IsOpened() || file.Length() < 0 || file.Length() > 256 * 1024 * 1024)
        return false;
    std::vector<char> bytes(static_cast<size_t>(file.Length()));
    if (!bytes.empty() && file.Read(&bytes[0], bytes.size()) != bytes.size())
        return false;
    size_t start = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        { encoding = EncUtf8Bom; start = 3; }
    else if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE)
        { encoding = EncUtf16LE; start = 2; }
    else if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFE && (unsigned char)bytes[1] == 0xFF)
        { encoding = EncUtf16BE; start = 2; }
    else
    {
        if (std::find(bytes.begin(), bytes.end(), '\0') != bytes.end()) return false;
        encoding = Gb18030Converter().IsOk() ? EncGb18030 : EncUtf8;
    }
    const char* data = bytes.empty() ? "" : &bytes[start];
    const size_t length = bytes.size() - start;
    size_t outLength = 0;
    wxWCharBuffer converted = Converter(encoding).cMB2WC(data, length, &outLength);
    if (!converted && length && encoding == EncGb18030)
        { encoding = EncUtf8; converted = Converter(encoding).cMB2WC(data, length, &outLength); }
    if (!converted && length && encoding == EncUtf8)
        { encoding = EncLocal; converted = Converter(encoding).cMB2WC(data, length, &outLength); }
    if (!converted && length) return false;
    text.assign(converted.data(), outLength);
    return true;
}

bool WriteText(const wxString& path, const wxString& text, TextEncoding encoding)
{
    const wxString temp = wxFileName::CreateTempFileName(path + wxS(".itool-"));
    wxFFile file(temp, wxS("wb"));
    if (!file.IsOpened()) return false;
    static const unsigned char utf8Bom[] = { 0xEF, 0xBB, 0xBF };
    static const unsigned char leBom[] = { 0xFF, 0xFE };
    static const unsigned char beBom[] = { 0xFE, 0xFF };
    bool ok = true;
    if (encoding == EncUtf8Bom) ok = file.Write(utf8Bom, 3) == 3;
    if (encoding == EncUtf16LE) ok = file.Write(leBom, 2) == 2;
    if (encoding == EncUtf16BE) ok = file.Write(beBom, 2) == 2;
    size_t byteLength = 0;
    wxCharBuffer encoded = Converter(encoding).cWC2MB(text.wc_str(), text.length(), &byteLength);
    if (ok && byteLength) ok = encoded && file.Write(encoded.data(), byteLength) == byteLength;
    ok = file.Close() && ok;
    if (!ok || !wxRenameFile(temp, path, true))
    {
        wxRemoveFile(temp);
        return false;
    }
    return true;
}

class ReplacerPanel : public wxPanel
{
public:
    explicit ReplacerPanel(wxWindow* parent) : wxPanel(parent), m_skipped(0)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxFlexGridSizer* form = new wxFlexGridSizer(3, 7, 7);
        form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Directory:")), 0, wxALIGN_CENTER_VERTICAL);
        m_dir = new wxTextCtrl(this, wxID_ANY); form->Add(m_dir, 1, wxEXPAND);
        wxButton* browse = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36,-1)); form->Add(browse);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Match expression:")), 0, wxALIGN_CENTER_VERTICAL);
        m_match = new wxTextCtrl(this, wxID_ANY); form->Add(m_match, 1, wxEXPAND); form->AddSpacer(1);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Replacement expression:")), 0, wxALIGN_CENTER_VERTICAL);
        m_replace = new wxTextCtrl(this, wxID_ANY); form->Add(m_replace, 1, wxEXPAND); form->AddSpacer(1);
        root->Add(form, 0, wxEXPAND | wxALL, 10);

        m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxBORDER_SUNKEN);
        m_list->InsertColumn(0, ITOOL_TR("Original filename"), wxLIST_FORMAT_LEFT, 300);
        m_list->InsertColumn(1, ITOOL_TR("New filename"), wxLIST_FORMAT_LEFT, 300);
        m_list->InsertColumn(2, ITOOL_TR("Content matches"), wxLIST_FORMAT_CENTER, 90);
        root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

        wxBoxSizer* bar = new wxBoxSizer(wxHORIZONTAL);
        m_recursive = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Include subdirectories"));
        m_content = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Replace file content"));
        m_filename = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Replace filenames"));
        m_foldername = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Replace folder names"));
        m_recursive->SetValue(true); m_content->SetValue(true); m_filename->SetValue(true); m_foldername->SetValue(true);
        bar->Add(m_recursive,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,12); bar->Add(m_content,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,12);
        bar->Add(m_filename,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,12); bar->Add(m_foldername,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,12); bar->AddStretchSpacer();
        wxButton* preview = new wxButton(this, wxID_ANY, ITOOL_TR("Preview"));
        wxButton* execute = new wxButton(this, wxID_ANY, ITOOL_TR("Replace"));
        bar->Add(preview,0,wxRIGHT,8); bar->Add(execute); root->Add(bar,0,wxEXPAND|wxALL,10);
        m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Encoding detection: GB18030-2022 -> UTF-8 -> local code page; maximum 256 MiB per file."));
        root->Add(m_status,0,wxLEFT|wxRIGHT|wxBOTTOM,10); SetSizer(root);
        SetDropTarget(new PathDropTarget(this));
        m_dir->SetDropTarget(new PathDropTarget(this));
        m_list->SetDropTarget(new PathDropTarget(this));
        browse->Bind(wxEVT_BUTTON, &ReplacerPanel::Browse, this);
        preview->Bind(wxEVT_BUTTON, &ReplacerPanel::Preview, this);
        execute->Bind(wxEVT_BUTTON, &ReplacerPanel::Execute, this);
        m_list->Bind(wxEVT_SIZE, &ReplacerPanel::ListResized, this);
    }

private:
    void ListResized(wxSizeEvent& event)
    {
        event.Skip(); const int total = m_list->GetClientSize().x - 2; if (total < 300) return;
        const int oldName = total * 34 / 100, newName = total * 34 / 100;
        m_list->SetColumnWidth(0, oldName); m_list->SetColumnWidth(1, newName);
        m_list->SetColumnWidth(2, total - oldName - newName);
    }
    class PathDropTarget : public wxFileDropTarget
    {
    public:
        explicit PathDropTarget(ReplacerPanel* owner) : m_owner(owner) {}
        bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& paths) wxOVERRIDE
        { return m_owner->AcceptPath(paths); }
    private:
        ReplacerPanel* m_owner;
    };

    bool AcceptPath(const wxArrayString& paths)
    {
        for (size_t i=0; i<paths.size(); ++i)
            if (wxFileName::DirExists(paths[i]) || wxFileName::FileExists(paths[i])) { m_dir->SetValue(paths[i]); m_status->SetLabel(wxFileName::DirExists(paths[i]) ? ITOOL_TR("Dropped directory accepted.") : ITOOL_TR("Dropped file accepted.")); return true; }
        m_status->SetLabel(ITOOL_TR("The dropped file or directory is invalid."));
        return false;
    }

    void Browse(wxCommandEvent&)
    {
        wxMenu menu;
        const int chooseFile = wxID_HIGHEST + 101;
        const int chooseDirectory = wxID_HIGHEST + 102;
        menu.Append(chooseFile, ITOOL_TR("Select file..."));
        menu.Append(chooseDirectory, ITOOL_TR("Select directory..."));
        const int choice = GetPopupMenuSelectionFromUser(menu);
        if (choice == chooseFile)
        {
            wxFileDialog d(this, ITOOL_TR("Select file"), wxEmptyString, wxEmptyString,
                           ITOOL_TR("All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (d.ShowModal() == wxID_OK) m_dir->SetValue(d.GetPath());
        }
        else if (choice == chooseDirectory)
        {
            wxDirDialog d(this, ITOOL_TR("Select directory"), wxEmptyString, wxDD_DIR_MUST_EXIST);
            if (d.ShowModal() == wxID_OK) m_dir->SetValue(d.GetPath());
        }
    }

    bool Plan(wxString& error)
    {
        m_ops.clear(); m_list->DeleteAllItems(); m_skipped = 0;
        if (!wxFileName::DirExists(m_dir->GetValue()) && !wxFileName::FileExists(m_dir->GetValue())) { error=ITOOL_TR("Select a valid file or directory."); return false; }
        if (m_match->GetValue().empty()) { error=ITOOL_TR("The match expression cannot be empty."); return false; }
        if (!m_content->GetValue() && !m_filename->GetValue() && !m_foldername->GetValue()) { error=ITOOL_TR("Select at least one replacement method."); return false; }
        wxRegEx regex;
        if (!regex.Compile(m_match->GetValue(), wxRE_ADVANCED)) { error=ITOOL_TR("The regular expression is invalid."); return false; }
        wxArrayString files, directories;
        if (wxFileName::FileExists(m_dir->GetValue())) files.Add(m_dir->GetValue());
        else if (m_recursive->GetValue())
        {
            wxDir dir(m_dir->GetValue());
            if (!dir.IsOpened()) { error=ITOOL_TR("Select a valid file or directory."); return false; }
            PathCollector collector(files, directories);
            dir.Traverse(collector, wxEmptyString, wxDIR_FILES | wxDIR_DIRS);
        }
        else
        {
            wxDir dir(m_dir->GetValue()); wxString n;
            for(bool more=dir.GetFirst(&n,wxEmptyString,wxDIR_FILES);more;more=dir.GetNext(&n)) files.Add(wxFileName(m_dir->GetValue(),n).GetFullPath());
            for(bool more=dir.GetFirst(&n,wxEmptyString,wxDIR_DIRS);more;more=dir.GetNext(&n)) directories.Add(wxFileName(m_dir->GetValue(),n).GetFullPath());
        }
        std::set<wxString> targets;
        for (size_t i=0;i<files.size();++i)
        {
            Operation op={files[i],files[i],0,false,false,EncUtf8};
            if (m_filename->GetValue())
            {
                wxFileName fn(files[i]); wxString name=fn.GetFullName();
                if (regex.Matches(name)) regex.ReplaceAll(&name,m_replace->GetValue());
                if (name.empty() || name.Find('/')!=wxNOT_FOUND || name.Find('\\')!=wxNOT_FOUND) { error=ITOOL_TR("The replacement produced an invalid filename."); return false; }
                op.newPath=wxFileName(fn.GetPath(),name).GetFullPath();
            }
            if (m_content->GetValue())
            {
                wxString text;
                if (ReadText(files[i],text,op.encoding)) { wxString replaced=text; op.matches=regex.ReplaceAll(&replaced,m_replace->GetValue()); op.changeContent=op.matches>0; }
                else ++m_skipped;
            }
            if (!op.changeContent && op.oldPath==op.newPath) continue;
            wxString key=op.newPath;
#ifdef __WXMSW__
            key.MakeLower();
#endif
            if (!targets.insert(key).second || (op.newPath!=op.oldPath && wxFileExists(op.newPath))) { error=ITOOL_TR("A duplicate target filename exists; operation stopped."); return false; }
            m_ops.push_back(op);
        }
        if (m_foldername->GetValue() && !wxFileName::FileExists(m_dir->GetValue()))
        {
            std::vector<Operation> directoryOps;
            for (size_t i=0; i<directories.size(); ++i)
            {
                wxFileName fn=wxFileName::DirName(directories[i]);
                const wxArrayString& parts=fn.GetDirs();
                if (parts.empty()) continue;
                wxString name=parts.Last();
                if (regex.Matches(name)) regex.ReplaceAll(&name,m_replace->GetValue());
                if (name.empty() || name.Find('/')!=wxNOT_FOUND || name.Find('\\')!=wxNOT_FOUND) { error=ITOOL_TR("The replacement produced an invalid filename."); return false; }
                wxFileName parent=fn; parent.RemoveLastDir();
                wxFileName renamed=parent; renamed.AppendDir(name);
                Operation op={fn.GetPath(),renamed.GetPath(),0,false,true,EncUtf8};
                if (op.oldPath==op.newPath) continue;
                wxString key=op.newPath;
#ifdef __WXMSW__
                key.MakeLower();
#endif
                if (!targets.insert(key).second || wxFileName::DirExists(op.newPath)) { error=ITOOL_TR("A duplicate target filename exists; operation stopped."); return false; }
                directoryOps.push_back(op);
            }
            std::sort(directoryOps.begin(),directoryOps.end(),[](const Operation& a,const Operation& b){return a.oldPath.length()>b.oldPath.length();});
            m_ops.insert(m_ops.end(),directoryOps.begin(),directoryOps.end());
        }
        for(size_t i=0;i<m_ops.size();++i) { long r=m_list->InsertItem(i,m_ops[i].oldPath); m_list->SetItem(r,1,m_ops[i].newPath); m_list->SetItem(r,2,wxString::Format(wxS("%lu"),(unsigned long)m_ops[i].matches)); }
        m_status->SetLabel(wxString::Format(ITOOL_TR("Preview: %lu files will change; %lu non-text or oversized files skipped."),(unsigned long)m_ops.size(),(unsigned long)m_skipped));
        return true;
    }

    void Preview(wxCommandEvent&) { wxBusyCursor b; wxString e; if(!Plan(e)) Error(e); }
    void Execute(wxCommandEvent&)
    {
        wxBusyCursor b; wxString e; if(!Plan(e)){Error(e);return;} if(m_ops.empty()){wxMessageBox(ITOOL_TR("No matches found."));return;}
        if(wxMessageBox(wxString::Format(ITOOL_TR("About to modify %lu files. This cannot be undone automatically. Continue?"),(unsigned long)m_ops.size()),ITOOL_TR("Confirm replacement"),wxYES_NO|wxNO_DEFAULT|wxICON_WARNING,this)!=wxYES)return;
        for(size_t i=0;i<m_ops.size();++i)
        {
            if(m_ops[i].changeContent)
            {
                wxString text; TextEncoding encoding=EncUtf8;
                wxRegEx regex(m_match->GetValue(), wxRE_ADVANCED);
                if(!ReadText(m_ops[i].oldPath,text,encoding) || !regex.IsValid()){Error(ITOOL_TR("Unable to reread file during replacement: ")+m_ops[i].oldPath);return;}
                regex.ReplaceAll(&text,m_replace->GetValue());
                if(!WriteText(m_ops[i].oldPath,text,encoding)){Error(ITOOL_TR("Failed to write file: ")+m_ops[i].oldPath);return;}
            }
            if(m_ops[i].newPath!=m_ops[i].oldPath)
            {
                const bool renamed=m_ops[i].directory
                    ? wxRename(m_ops[i].oldPath,m_ops[i].newPath)==0
                    : wxRenameFile(m_ops[i].oldPath,m_ops[i].newPath,false);
                if(!renamed){Error(ITOOL_TR("Failed to rename: ")+m_ops[i].oldPath);return;}
            }
        }
        m_status->SetLabel(wxString::Format(ITOOL_TR("Completed: %lu files modified."),(unsigned long)m_ops.size()));
    }
    void Error(const wxString& e){wxMessageBox(e,ITOOL_TR("Replacer error"),wxOK|wxICON_WARNING,this);}

    wxTextCtrl *m_dir,*m_match,*m_replace; wxCheckBox *m_recursive,*m_content,*m_filename,*m_foldername;
    wxListCtrl* m_list; wxStaticText* m_status; std::vector<Operation> m_ops; size_t m_skipped;
};
}

wxString ReplacerToolModule::GetId() const { return wxS("replacer"); }
wxString ReplacerToolModule::GetName() const { return ITOOL_TR("Find and replace"); }
wxString ReplacerToolModule::GetDescription() const { return ITOOL_TR("Use regular expressions to replace filenames and text in batches"); }
wxWindow* ReplacerToolModule::CreatePanel(wxWindow* parent) { return new ReplacerPanel(parent); }
