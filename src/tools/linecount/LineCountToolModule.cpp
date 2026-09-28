#include "tools/linecount/LineCountToolModule.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "core/Localization.h"

namespace
{
enum CommentStyle { StyleNone, StyleC, StyleHash, StyleSql, StyleMarkup, StyleBatch };

struct Counts
{
    Counts() : total(0), code(0), comments(0), blank(0) {}
    unsigned long total, code, comments, blank;
};

struct FileResult
{
    wxString path;
    Counts counts;
};

std::string LowerAscii(std::string value)
{
    for (size_t i = 0; i < value.size(); ++i)
        if (value[i] >= 'A' && value[i] <= 'Z') value[i] = static_cast<char>(value[i] - 'A' + 'a');
    return value;
}

std::string ExtensionOf(const wxString& path)
{
    wxCharBuffer extension = wxFileName(path).GetExt().Lower().utf8_str();
    return std::string(extension.data(), extension.length());
}

CommentStyle StyleFor(const wxString& path)
{
    const std::string ext = ExtensionOf(path);
    if (ext == "c" || ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "h" || ext == "hh" ||
        ext == "hpp" || ext == "hxx" || ext == "java" || ext == "js" || ext == "jsx" || ext == "ts" ||
        ext == "tsx" || ext == "cs" || ext == "go" || ext == "rs" || ext == "swift" || ext == "kt" ||
        ext == "kts" || ext == "php" || ext == "css" || ext == "scss" || ext == "less") return StyleC;
    if (ext == "py" || ext == "pyw" || ext == "sh" || ext == "bash" || ext == "zsh" || ext == "rb" ||
        ext == "pl" || ext == "pm" || ext == "r" || ext == "yaml" || ext == "yml" || ext == "toml" ||
        ext == "ini" || ext == "cmake") return StyleHash;
    if (ext == "sql" || ext == "lua" || ext == "hs") return StyleSql;
    if (ext == "html" || ext == "htm" || ext == "xml" || ext == "xaml" || ext == "svg" || ext == "vue") return StyleMarkup;
    if (ext == "bat" || ext == "cmd") return StyleBatch;
    return StyleNone;
}

bool IsSpace(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
}

bool StartsAt(const std::string& line, size_t offset, const char* marker)
{
    const size_t length = std::char_traits<char>::length(marker);
    return offset + length <= line.size() && line.compare(offset, length, marker) == 0;
}

void CountLine(const std::string& line, CommentStyle style, bool& inBlock, Counts& counts)
{
    bool hasCode = false, hasComment = false;
    bool inSingle = false, inDouble = false, escaped = false;
    size_t i = 0;

    if (style == StyleBatch)
    {
        while (i < line.size() && IsSpace(static_cast<unsigned char>(line[i]))) ++i;
        const std::string beginning = LowerAscii(line.substr(i, std::min<size_t>(4, line.size() - i)));
        if (StartsAt(line, i, "::") || beginning == "rem " || beginning == "rem\t" || beginning == "rem")
            hasComment = true;
        else if (i < line.size()) hasCode = true;
    }
    else
    {
        while (i < line.size())
        {
            if (inBlock)
            {
                hasComment = true;
                const char* end = style == StyleMarkup ? "-->" : "*/";
                const size_t found = line.find(end, i);
                if (found == std::string::npos) break;
                inBlock = false; i = found + std::char_traits<char>::length(end); continue;
            }
            const unsigned char ch = static_cast<unsigned char>(line[i]);
            if (inSingle || inDouble)
            {
                hasCode = true;
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if ((inSingle && ch == '\'') || (inDouble && ch == '"')) { inSingle = false; inDouble = false; }
                ++i; continue;
            }
            if (IsSpace(ch)) { ++i; continue; }
            if (style == StyleMarkup && StartsAt(line, i, "<!--"))
                { hasComment = true; inBlock = true; i += 4; continue; }
            if ((style == StyleC || style == StyleSql) && StartsAt(line, i, "/*"))
                { hasComment = true; inBlock = true; i += 2; continue; }
            if (style == StyleC && StartsAt(line, i, "//")) { hasComment = true; break; }
            if (style == StyleHash && line[i] == '#') { hasComment = true; break; }
            if (style == StyleSql && StartsAt(line, i, "--")) { hasComment = true; break; }
            hasCode = true;
            if (line[i] == '\'') inSingle = true;
            else if (line[i] == '"') inDouble = true;
            ++i;
        }
    }

    ++counts.total;
    if (hasCode) ++counts.code;
    if (hasComment) ++counts.comments;
    if (!hasCode && !hasComment) ++counts.blank;
}

bool CountFile(const wxString& path, Counts& counts)
{
    wxFFile file(path, wxS("rb"));
    if (!file.IsOpened() || file.Length() < 0 || file.Length() > 512LL * 1024LL * 1024LL) return false;
    std::string bytes(static_cast<size_t>(file.Length()), '\0');
    if (!bytes.empty() && file.Read(&bytes[0], bytes.size()) != bytes.size()) return false;

    // Source encodings such as UTF-8 and GB18030 preserve ASCII comment markers.
    // Convert BOM-marked UTF-16 so its embedded zero bytes do not distort parsing.
    if (bytes.size() >= 2 && (((unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) ||
                              ((unsigned char)bytes[0] == 0xFE && (unsigned char)bytes[1] == 0xFF)))
    {
        const bool littleEndian = (unsigned char)bytes[0] == 0xFF;
        wxMBConvUTF16LE le; wxMBConvUTF16BE be;
        size_t wideLength = 0;
        wxWCharBuffer wide = (littleEndian ? static_cast<wxMBConv&>(le) : static_cast<wxMBConv&>(be))
                             .cMB2WC(bytes.data() + 2, bytes.size() - 2, &wideLength);
        if (!wide) return false;
        wxCharBuffer utf8 = wxString(wide.data(), wideLength).utf8_str();
        bytes.assign(utf8.data(), utf8.length());
    }

    const CommentStyle style = StyleFor(path);
    bool inBlock = false;
    size_t start = 0;
    for (size_t i = 0; i <= bytes.size(); ++i)
    {
        if (i == bytes.size() || bytes[i] == '\n')
        {
            size_t end = i;
            if (end > start && bytes[end - 1] == '\r') --end;
            if (i > start || i < bytes.size()) CountLine(bytes.substr(start, end - start), style, inBlock, counts);
            start = i + 1;
        }
    }
    return true;
}

std::set<std::string> ParseExtensions(const wxString& value)
{
    wxString normalized(value); normalized.Replace(wxS(";"), wxS(",")); normalized.Replace(wxS(" "), wxS(","));
    std::set<std::string> result;
    wxArrayString parts = wxSplit(normalized, ',');
    for (size_t i = 0; i < parts.size(); ++i)
    {
        wxString part = parts[i].Lower(); part.Trim(true).Trim(false);
        while (part.StartsWith(wxS("*."))) part = part.Mid(2);
        while (part.StartsWith(wxS("."))) part = part.Mid(1);
        if (!part.empty()) { wxCharBuffer utf8 = part.utf8_str(); result.insert(std::string(utf8.data(), utf8.length())); }
    }
    return result;
}

class LineCountPanel : public wxPanel
{
public:
    explicit LineCountPanel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxFlexGridSizer* form = new wxFlexGridSizer(3, 7, 7); form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("File or directory:")), 0, wxALIGN_CENTER_VERTICAL);
        m_path = new wxTextCtrl(this, wxID_ANY); form->Add(m_path, 1, wxEXPAND);
        wxButton* browse = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1)); form->Add(browse);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Extensions:")), 0, wxALIGN_CENTER_VERTICAL);
        m_extensions = new wxTextCtrl(this, wxID_ANY,
            wxS("c,cc,cpp,cxx,h,hpp,java,cs,js,ts,py,go,rs,php,rb,sh,sql,html,xml,css,vue,cmake"));
        form->Add(m_extensions, 1, wxEXPAND); form->AddSpacer(1);
        root->Add(form, 0, wxEXPAND | wxALL, 10);

        m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxBORDER_SUNKEN);
        m_list->InsertColumn(0, ITOOL_TR("File"), wxLIST_FORMAT_LEFT, 390);
        m_list->InsertColumn(1, ITOOL_TR("Total lines"), wxLIST_FORMAT_RIGHT, 75);
        m_list->InsertColumn(2, ITOOL_TR("Code"), wxLIST_FORMAT_RIGHT, 75);
        m_list->InsertColumn(3, ITOOL_TR("Comments"), wxLIST_FORMAT_RIGHT, 75);
        m_list->InsertColumn(4, ITOOL_TR("Blank"), wxLIST_FORMAT_RIGHT, 75);
        root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

        wxBoxSizer* actions = new wxBoxSizer(wxHORIZONTAL);
        m_recursive = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Include subdirectories")); m_recursive->SetValue(true);
        m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Code and comments can be counted on the same line (for example, trailing comments)."));
        wxButton* count = new wxButton(this, wxID_ANY, ITOOL_TR("Start counting"));
        wxButton* clear = new wxButton(this, wxID_ANY, ITOOL_TR("Clear"));
        actions->Add(m_recursive, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
        actions->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        actions->Add(count, 0, wxRIGHT, 8); actions->Add(clear);
        root->Add(actions, 0, wxEXPAND | wxALL, 10); SetSizer(root);

        browse->Bind(wxEVT_BUTTON, &LineCountPanel::Browse, this);
        count->Bind(wxEVT_BUTTON, &LineCountPanel::Run, this);
        clear->Bind(wxEVT_BUTTON, &LineCountPanel::Clear, this);
    }

private:
    void Browse(wxCommandEvent&)
    {
        wxMenu menu; const int fileId = wxID_HIGHEST + 301, dirId = wxID_HIGHEST + 302;
        menu.Append(fileId, ITOOL_TR("Select file...")); menu.Append(dirId, ITOOL_TR("Select directory..."));
        const int choice = GetPopupMenuSelectionFromUser(menu);
        if (choice == fileId) { wxFileDialog d(this, ITOOL_TR("Select source code file"), wxEmptyString, wxEmptyString, ITOOL_TR("All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST); if (d.ShowModal() == wxID_OK) m_path->SetValue(d.GetPath()); }
        else if (choice == dirId) { wxDirDialog d(this, ITOOL_TR("Select source code directory"), wxEmptyString, wxDD_DIR_MUST_EXIST); if (d.ShowModal() == wxID_OK) m_path->SetValue(d.GetPath()); }
    }

    void Run(wxCommandEvent&)
    {
        const bool isFile = wxFileName::FileExists(m_path->GetValue());
        const bool isDirectory = wxFileName::DirExists(m_path->GetValue());
        if (!isFile && !isDirectory) { Error(ITOOL_TR("Select a valid file or directory.")); return; }
        wxBusyCursor busy; m_list->DeleteAllItems();
        wxArrayString files;
        if (isFile) files.Add(m_path->GetValue());
        else if (m_recursive->GetValue())
            wxDir::GetAllFiles(m_path->GetValue(), &files, wxEmptyString,
                               wxDIR_FILES | wxDIR_DIRS);
        else { wxDir dir(m_path->GetValue()); wxString name; for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_FILES); more; more = dir.GetNext(&name)) files.Add(wxFileName(m_path->GetValue(), name).GetFullPath()); }
        const std::set<std::string> extensions = ParseExtensions(m_extensions->GetValue());
        Counts totals; size_t skipped = 0, accepted = 0;
        for (size_t i = 0; i < files.size(); ++i)
        {
            if (!isFile && !extensions.empty() && extensions.find(ExtensionOf(files[i])) == extensions.end()) continue;
            Counts counts; if (!CountFile(files[i], counts)) { ++skipped; continue; }
            wxString shown = files[i]; if (isDirectory) { wxFileName relative(files[i]); relative.MakeRelativeTo(m_path->GetValue()); shown = relative.GetFullPath(); }
            const long row = m_list->InsertItem(static_cast<long>(accepted++), shown);
            m_list->SetItem(row, 1, wxString::Format(wxS("%lu"), counts.total));
            m_list->SetItem(row, 2, wxString::Format(wxS("%lu"), counts.code));
            m_list->SetItem(row, 3, wxString::Format(wxS("%lu"), counts.comments));
            m_list->SetItem(row, 4, wxString::Format(wxS("%lu"), counts.blank));
            totals.total += counts.total; totals.code += counts.code; totals.comments += counts.comments; totals.blank += counts.blank;
        }
        m_status->SetLabel(wxString::Format(ITOOL_TR("%lu files: total %lu, code %lu, comments %lu, blank %lu; skipped %lu"),
            static_cast<unsigned long>(accepted), totals.total, totals.code, totals.comments, totals.blank, static_cast<unsigned long>(skipped)));
    }

    void Clear(wxCommandEvent&) { m_list->DeleteAllItems(); m_status->SetLabel(ITOOL_TR("Count results cleared.")); }
    void Error(const wxString& message) { wxMessageBox(message, ITOOL_TR("Source line count"), wxOK | wxICON_WARNING, this); }

    wxTextCtrl *m_path, *m_extensions; wxCheckBox* m_recursive; wxListCtrl* m_list; wxStaticText* m_status;
};
}

wxString LineCountToolModule::GetId() const { return wxS("line-count"); }
wxString LineCountToolModule::GetName() const { return ITOOL_TR("Line counter"); }
wxString LineCountToolModule::GetDescription() const { return ITOOL_TR("Count total, code, comment, and blank lines by file"); }
wxWindow* LineCountToolModule::CreatePanel(wxWindow* parent) { return new LineCountPanel(parent); }
