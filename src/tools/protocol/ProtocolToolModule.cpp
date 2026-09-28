#include "tools/protocol/ProtocolToolModule.h"

#include "core/Localization.h"

#include <algorithm>
#include <set>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "tools/protocol/ProtocolParser.h"

namespace
{
bool ReadHeader(const wxString& path, wxString& text, wxString& error)
{
    wxFFile file(path, wxS("rb"));
    if (!file.IsOpened() || file.Length() < 0 || file.Length() > 16LL * 1024LL * 1024LL)
        { error = ITOOL_TR("Unable to read header file, or file exceeds 16 MiB:") + path; return false; }
    std::vector<char> bytes(static_cast<size_t>(file.Length()));
    if (!bytes.empty() && file.Read(&bytes[0], bytes.size()) != bytes.size())
        { error = ITOOL_TR("Failed to read header file:") + path; return false; }
    if (bytes.empty()) { text.clear(); return true; }
    const char* data = &bytes[0]; size_t length = bytes.size();
    if (length >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        { data += 3; length -= 3; }
    size_t convertedLength = 0; wxWCharBuffer converted = wxConvUTF8.cMB2WC(data, length, &convertedLength);
    if (!converted && length) converted = wxConvLocal.cMB2WC(data, length, &convertedLength);
    if (!converted && length) { error = ITOOL_TR("Header file is not a valid UTF-8 or local encoding:") + path; return false; }
    text.assign(converted.data(), convertedLength); return true;
}

wxString IncludeKey(wxString path)
{
    wxFileName normalized(path); normalized.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE); path = normalized.GetFullPath();
#ifdef __WXMSW__
    path.MakeLower();
#endif
    return path;
}

bool ExpandIncludes(const wxString& source, const wxString& currentDirectory, const wxString& includeRoot,
                    std::set<wxString>& visited, wxString& output, wxString& error)
{
    size_t start = 0;
    while (start <= source.length())
    {
        size_t end = source.find('\n', start); if (end == wxString::npos) end = source.length();
        const wxString original = source.Mid(start, end - start); wxString line(original); line.Trim(true).Trim(false);
        bool handled = false;
        if (line.StartsWith(wxS("#include")))
        {
            const int firstQuote = line.Find('"'); const int firstAngle = line.Find('<');
            if (firstQuote != wxNOT_FOUND)
            {
                const int secondQuote = line.find('"', static_cast<size_t>(firstQuote + 1));
                if (secondQuote == wxNOT_FOUND) { error = ITOOL_TR("include is missing the closing quote:") + original; return false; }
                const wxString relative = line.Mid(firstQuote + 1, secondQuote - firstQuote - 1);
                wxString path = wxFileName(currentDirectory, relative).GetFullPath();
                if (!wxFileName::FileExists(path)) path = wxFileName(includeRoot, relative).GetFullPath();
                if (!wxFileName::FileExists(path)) { error = ITOOL_TR("Include header file not found:") + relative; return false; }
                const wxString key = IncludeKey(path);
                if (visited.insert(key).second)
                {
                    wxString included;
                    if (!ReadHeader(path, included, error) ||
                        !ExpandIncludes(included, wxFileName(path).GetPath(), includeRoot, visited, output, error)) return false;
                }
                handled = true;
            }
            else if (firstAngle != wxNOT_FOUND)
                handled = true; // System headers provide primitive types and are intentionally not loaded.
        }
        if (!handled) output += original + wxS("\n");
        if (end == source.length()) break;
        start = end + 1;
    }
    return true;
}

class ProtocolPanel : public wxPanel
{
public:
    explicit ProtocolPanel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL); wxBoxSizer* editors = new wxBoxSizer(wxHORIZONTAL);
        wxStaticBoxSizer* definitionBox = new wxStaticBoxSizer(wxVERTICAL, this, ITOOL_TR("C structure definition"));
        wxBoxSizer* includeRow = new wxBoxSizer(wxHORIZONTAL);
        includeRow->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Header file directory:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        m_includeDirectory = new wxTextCtrl(this, wxID_ANY); wxButton* chooseDirectory = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1));
        wxButton* loadDirectory = new wxButton(this, wxID_ANY, ITOOL_TR("Load directory"));
        includeRow->Add(m_includeDirectory, 1, wxRIGHT, 5); includeRow->Add(chooseDirectory, 0, wxRIGHT, 5); includeRow->Add(loadDirectory);
        definitionBox->Add(includeRow, 0, wxEXPAND | wxALL, 5);
        m_definition = new wxTextCtrl(this, wxID_ANY,
            wxS("typedef struct {\n    uint16_t length;\n    uint8_t type;\n    uint8_t flags;\n    int32_t value;\n    char name[8];\n} Packet;"),
            wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_RICH2);
        definitionBox->Add(m_definition, 1, wxEXPAND | wxALL, 5);
        wxBoxSizer* layout = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* formatRow = new wxBoxSizer(wxHORIZONTAL); formatRow->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Alignment:")), 0, wxALIGN_CENTER_VERTICAL);
        wxArrayString packs; packs.Add(ITOOL_TR("1 byte (compact)")); packs.Add(ITOOL_TR("2 bytes")); packs.Add(ITOOL_TR("4 bytes")); packs.Add(ITOOL_TR("8 bytes"));
        m_pack = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, packs); m_pack->SetSelection(2);
        wxArrayString endian; endian.Add(ITOOL_TR("little endian")); endian.Add(ITOOL_TR("big endian")); m_endian = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, endian); m_endian->SetSelection(0);
        m_dynamicTailArray = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Penultimate field specifies tail array length"));
        formatRow->Add(m_pack, 0, wxRIGHT, 12); formatRow->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Endianness:")), 0, wxALIGN_CENTER_VERTICAL);
        formatRow->Add(m_endian);
        layout->Add(formatRow, 0, wxBOTTOM, 5);
        layout->Add(m_dynamicTailArray, 0, wxALIGN_LEFT);
        definitionBox->Add(layout, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5); editors->Add(definitionBox, 3, wxEXPAND | wxRIGHT, 8);

        wxStaticBoxSizer* inputBox = new wxStaticBoxSizer(wxVERTICAL, this, ITOOL_TR("Enter data"));
        wxBoxSizer* modes = new wxBoxSizer(wxHORIZONTAL); m_hexMode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Hex string"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_fileMode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("BIN file")); modes->Add(m_hexMode, 0, wxRIGHT, 12); modes->Add(m_fileMode); inputBox->Add(modes, 0, wxALL, 5);
        wxBoxSizer* fileRow = new wxBoxSizer(wxHORIZONTAL); m_file = new wxTextCtrl(this, wxID_ANY); m_browse = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1));
        fileRow->Add(m_file, 1, wxRIGHT, 5); fileRow->Add(m_browse); inputBox->Add(fileRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
        m_hex = new wxTextCtrl(this, wxID_ANY, wxS("10 00 01 00 78 56 34 12 54 65 73 74 00 00 00 00"), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_RICH2);
        inputBox->Add(m_hex, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5); editors->Add(inputBox, 2, wxEXPAND);
        root->Add(editors, 1, wxEXPAND | wxALL, 8);

        m_grid = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);
        m_grid->InsertColumn(0, ITOOL_TR("member"), wxLIST_FORMAT_LEFT, 185); m_grid->InsertColumn(1, ITOOL_TR("type"), wxLIST_FORMAT_LEFT, 120);
        m_grid->InsertColumn(2, ITOOL_TR("offset"), wxLIST_FORMAT_RIGHT, 70); m_grid->InsertColumn(3, ITOOL_TR("size"), wxLIST_FORMAT_RIGHT, 60);
        m_grid->InsertColumn(4, ITOOL_TR("Original Hex"), wxLIST_FORMAT_LEFT, 230); m_grid->InsertColumn(5, ITOOL_TR("parsed value"), wxLIST_FORMAT_LEFT, 250);
        root->Add(m_grid, 2, wxEXPAND | wxLEFT | wxRIGHT, 8);
        wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL); m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Supports basic types, fixed-length arrays, and defined nested structures."));
        wxButton* parse = new wxButton(this, wxID_ANY, ITOOL_TR("parse")); wxButton* clear = new wxButton(this, wxID_CLEAR, ITOOL_TR("Clear results"));
        footer->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8); footer->Add(parse, 0, wxRIGHT, 6); footer->Add(clear); root->Add(footer, 0, wxEXPAND | wxALL, 8); SetSizer(root);
        m_hexMode->SetValue(true); m_browse->Disable(); m_file->Disable();
        m_hexMode->Bind(wxEVT_RADIOBUTTON, &ProtocolPanel::ModeChanged, this); m_fileMode->Bind(wxEVT_RADIOBUTTON, &ProtocolPanel::ModeChanged, this);
        m_browse->Bind(wxEVT_BUTTON, &ProtocolPanel::Browse, this); parse->Bind(wxEVT_BUTTON, &ProtocolPanel::Parse, this);
        clear->Bind(wxEVT_BUTTON, &ProtocolPanel::Clear, this);
        chooseDirectory->Bind(wxEVT_BUTTON, &ProtocolPanel::ChooseIncludeDirectory, this);
        loadDirectory->Bind(wxEVT_BUTTON, &ProtocolPanel::LoadIncludeDirectory, this);
    }
private:
    size_t Packing() const { static const size_t values[] = { 1, 2, 4, 8 }; return values[m_pack->GetSelection()]; }
    void ModeChanged(wxCommandEvent&) { const bool file = m_fileMode->GetValue(); m_file->Enable(file); m_browse->Enable(file); m_hex->Enable(!file); }
    void Browse(wxCommandEvent&) { wxFileDialog dialog(this, ITOOL_TR("Select BIN file"), wxEmptyString, wxEmptyString, ITOOL_TR("Binary files (*.bin)|*.bin|All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST); if (dialog.ShowModal() == wxID_OK) m_file->SetValue(dialog.GetPath()); }
    void ChooseIncludeDirectory(wxCommandEvent&)
    {
        wxDirDialog dialog(this, ITOOL_TR("Select header file directory"), m_includeDirectory->GetValue(), wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK) m_includeDirectory->SetValue(dialog.GetPath());
    }
    void LoadIncludeDirectory(wxCommandEvent&)
    {
        if (!wxFileName::DirExists(m_includeDirectory->GetValue())) { Error(ITOOL_TR("Please select a valid header directory.")); return; }
        wxArrayString all; wxDir::GetAllFiles(m_includeDirectory->GetValue(), &all, wxEmptyString, wxDIR_FILES);
        std::vector<wxString> headers;
        for (size_t i = 0; i < all.size(); ++i)
        {
            const wxString extension = wxFileName(all[i]).GetExt().Lower();
            if (extension == wxS("h") || extension == wxS("hpp") || extension == wxS("hh") || extension == wxS("hxx")) headers.push_back(all[i]);
        }
        std::sort(headers.begin(), headers.end()); wxString includes;
        for (size_t i = 0; i < headers.size(); ++i)
        {
            wxFileName relative(headers[i]); relative.MakeRelativeTo(m_includeDirectory->GetValue());
            wxString path = relative.GetFullPath(); path.Replace(wxS("\\"), wxS("/"));
            includes += wxS("#include \"") + path + wxS("\"\n");
        }
        m_definition->SetValue(includes);
        m_status->SetLabel(wxString::Format(ITOOL_TR("%lu header file entries have been loaded; include will be expanded recursively during parsing."), static_cast<unsigned long>(headers.size())));
    }
    bool Input(std::vector<unsigned char>& bytes, wxString& error)
    {
        if (!m_fileMode->GetValue()) return protocol::ParseHexBytes(m_hex->GetValue(), bytes, error);
        if (!wxFileName::FileExists(m_file->GetValue())) { error = ITOOL_TR("Please select a valid BIN file."); return false; }
        wxFFile file(m_file->GetValue(), wxS("rb"));
        if (!file.IsOpened() || file.Length() < 0 || file.Length() > 256LL * 1024LL * 1024LL) { error = ITOOL_TR("The BIN file cannot be read, or the file exceeds 256 MiB."); return false; }
        bytes.resize(static_cast<size_t>(file.Length()));
        if (!bytes.empty() && file.Read(&bytes[0], bytes.size()) != bytes.size()) { error = ITOOL_TR("Failed to read BIN file."); return false; }
        return true;
    }
    void Parse(wxCommandEvent&)
    {
        wxBusyCursor busy; wxString error, root; protocol::DefinitionMap definitions;
        wxString expanded = m_definition->GetValue();
        if (!m_includeDirectory->GetValue().empty())
        {
            if (!wxFileName::DirExists(m_includeDirectory->GetValue())) { Error(ITOOL_TR("The header file directory is invalid.")); return; }
            expanded.clear(); std::set<wxString> visited;
            if (!ExpandIncludes(m_definition->GetValue(), m_includeDirectory->GetValue(), m_includeDirectory->GetValue(), visited, expanded, error)) { Error(error); return; }
        }
        const bool dynamicTailArray = m_dynamicTailArray->GetValue();
        if (!protocol::ParseDefinitions(expanded, Packing(), definitions, root, error, dynamicTailArray)) { Error(error); return; }
        std::vector<unsigned char> bytes; if (!Input(bytes, error)) { Error(error); return; }
        std::vector<protocol::DecodedField> rows; size_t records = 0, trailing = 0;
        if (!protocol::DecodeRecords(definitions, root, bytes, m_endian->GetSelection() == 0, rows, records, trailing, error, dynamicTailArray)) { Error(error); return; }
        m_grid->DeleteAllItems();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const long row = m_grid->InsertItem(static_cast<long>(i), rows[i].path); m_grid->SetItem(row, 1, rows[i].typeName);
            m_grid->SetItem(row, 2, wxString::Format(wxS("%lu"), static_cast<unsigned long>(rows[i].offset))); m_grid->SetItem(row, 3, wxString::Format(wxS("%lu"), static_cast<unsigned long>(rows[i].size)));
            m_grid->SetItem(row, 4, rows[i].rawHex); m_grid->SetItem(row, 5, rows[i].value);
        }
        const protocol::StructDefinition& definition = definitions.find(root)->second;
        m_status->SetLabel(wxString::Format(ITOOL_TR("Root structure %s: %lu bytes; parse %lu records; remaining %lu bytes at the end."), root.c_str(),
            static_cast<unsigned long>(definition.size), static_cast<unsigned long>(records), static_cast<unsigned long>(trailing)));
    }
    void Clear(wxCommandEvent&) { m_grid->DeleteAllItems(); m_status->SetLabel(ITOOL_TR("The parsing results have been cleared.")); }
    void Error(const wxString& error) { wxMessageBox(error, ITOOL_TR("Protocol analysis"), wxOK | wxICON_WARNING, this); }
    wxTextCtrl *m_definition, *m_includeDirectory, *m_file, *m_hex; wxChoice *m_pack, *m_endian; wxCheckBox* m_dynamicTailArray; wxRadioButton *m_hexMode, *m_fileMode;
    wxButton* m_browse; wxListCtrl* m_grid; wxStaticText* m_status;
};
}

wxString ProtocolToolModule::GetId() const { return wxS("protocol-parser"); }
wxString ProtocolToolModule::GetName() const { return ITOOL_TR("Protocol parser"); }
wxString ProtocolToolModule::GetDescription() const { return ITOOL_TR("Parse Hex streams or BIN files using C structure definitions"); }
wxWindow* ProtocolToolModule::CreatePanel(wxWindow* parent) { return new ProtocolPanel(parent); }
