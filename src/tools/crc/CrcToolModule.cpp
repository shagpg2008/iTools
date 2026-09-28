#include "tools/crc/CrcToolModule.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <wx/button.h>
#include <wx/dnd.h>
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/renderer.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/thread.h>

#include "core/Localization.h"
#include "third_party/crc/crc.h"
#include "tools/crc/MappedFile.h"

namespace
{
enum InputMode
{
    InputHex,
    InputText,
    InputFile
};

enum GridColumn
{
    ColumnSelected,
    ColumnName,
    ColumnResult,
    ColumnWidth,
    ColumnInitial,
    ColumnPolynomial,
    ColumnXorOut,
    ColumnReflectIn,
    ColumnReflectOut,
    ColumnCount
};

struct AlgorithmDefinition
{
    const wxChar* name;
    CrcConfig* config;
    bool posixLength;
};

AlgorithmDefinition kAlgorithms[] = {
    { wxS("CRC8/SMBUS"),        &crc8SMBUS,          false },
    { wxS("CRC16/MODBUS"),      &crc16MODBUS,        false },
    { wxS("CRC16/CCITT_FALSE"), &crc16CCITT_FALSE,   false },
    { wxS("CRC16/XMODEM"),      &crc16XMODEM,        false },
    { wxS("CRC16/KERMIT"),      &crc16KERMIT,        false },
    { wxS("CRC16/USB"),         &crc16USB,           false },
    { wxS("CRC24/LTEA"),        &crc24LTEA,          false },
    { wxS("CRC24/LTEB"),        &crc24LTEB,          false },
    { wxS("CRC24/BLE"),         &crc24BLE,           false },
    { wxS("CRC32"),             &crc32IsoHDLC,       false },
    { wxS("CRC32C"),            &crc32C,             false },
    { wxS("CRC32/MPEG2"),       &crc32MPEG2,         false },
    { wxS("CRC32/POSIX"),       &crc32POSIX,         true  },
    { wxS("CRC32/AUTOSAR"),     &crc32AUTOSAR,       false },
    { wxS("CRC32/BZIP2"),       &crc32BZIP2,         false },
    { wxS("CRC64/ECMA"),        &crc64Ecma,          false },
    { wxS("CRC64/XZ"),          &crc64Xz,            false },
    { wxS("CRC64/WE"),          &crc64We,            false }
};

const int kAlgorithmCount = sizeof(kAlgorithms) / sizeof(kAlgorithms[0]);
const int kCustomRow = kAlgorithmCount;

wxString FormatHex(u64 value, u32 width)
{
    return wxString::Format(wxS("%0*llX"), static_cast<int>(width / 4),
                            static_cast<unsigned long long>(value));
}

bool ParseUnsignedHex(const wxString& source, u64& value)
{
    wxString text = source;
    text.Trim(true).Trim(false);
    if (text.StartsWith(wxS("0x")) || text.StartsWith(wxS("0X")))
        text = text.Mid(2);
    if (text.empty())
        return false;

    unsigned long long parsed = 0;
    if (!text.ToULongLong(&parsed, 16))
        return false;
    value = static_cast<u64>(parsed);
    return true;
}

bool ParseHexInput(const wxString& source,
                   std::vector<u8>& bytes,
                   wxString& error)
{
    wxString digits;
    for (size_t i = 0; i < source.length(); ++i)
    {
        const wxUniChar ch = source[i];
        if (ch == wxS('0') && i + 1 < source.length() &&
            (source[i + 1] == wxS('x') || source[i + 1] == wxS('X')))
        {
            ++i;
            continue;
        }
        if (wxIsxdigit(ch))
        {
            digits.Append(ch);
        }
        else if (!wxIsspace(ch) && ch != wxS(',') && ch != wxS(';') &&
                 ch != wxS(':') && ch != wxS('-'))
        {
            error = wxString::Format(ITOOL_TR("Invalid Hex character: '%c'."),
                                     static_cast<wxChar>(ch));
            return false;
        }
    }

    if (digits.empty())
    {
        error = ITOOL_TR("Enter Hex data, for example: 31 32 33 34 35 36 37 38 39");
        return false;
    }
    if ((digits.length() & 1) != 0)
    {
        error = ITOOL_TR("Hex data must contain an even number of hexadecimal digits.");
        return false;
    }

    bytes.reserve(digits.length() / 2);
    for (size_t i = 0; i < digits.length(); i += 2)
    {
        unsigned long value = 0;
        if (!digits.Mid(i, 2).ToULong(&value, 16))
        {
            error = ITOOL_TR("Failed to parse Hex data.");
            return false;
        }
        bytes.push_back(static_cast<u8>(value));
    }
    return true;
}

struct ActiveCalculation
{
    int row;
    bool posixLength;
    CrcConfig config;
    CrcContext context;
};

wxDEFINE_EVENT(EVT_CRC_FILE_COMPLETE, wxThreadEvent);

class FileCrcThread : public wxThread
{
public:
    FileCrcThread(wxEvtHandler* owner,
                  const wxString& path,
                  std::vector<ActiveCalculation>& active)
        : wxThread(wxTHREAD_JOINABLE), m_owner(owner), m_path(path),
          m_ok(false), m_totalLength(0)
    {
        m_active.swap(active);
    }

    bool WasSuccessful() const { return m_ok; }
    const wxString& GetError() const { return m_error; }
    u64 GetTotalLength() const { return m_totalLength; }
    std::vector<ActiveCalculation>& GetCalculations()
    {
        return m_active;
    }

protected:
    ExitCode Entry() wxOVERRIDE
    {
        m_ok = VisitMappedFile(
            m_path,
            [this](const u8* data, size_t length) {
                const u32 count = static_cast<u32>(length);
                for (size_t i = 0; i < m_active.size(); ++i)
                {
                    if (!crcUpdate(&m_active[i].context, data, count))
                        return false;
                }
                return true;
            },
            m_totalLength, m_error);

        if (m_ok)
        {
            for (size_t i = 0; i < m_active.size(); ++i)
            {
                if (!m_active[i].posixLength)
                    continue;

                // if (m_totalLength > 0xFFFFFFFFULL)
                // {
                //     CRC32/POSIX length fields support inputs up to 4 GiB.
                //     m_ok = false;
                //     break;
                // }

                u32 remaining = static_cast<u32>(m_totalLength);
                while (remaining != 0)
                {
                    const u8 byte = static_cast<u8>(remaining & 0xFF);
                    if (!crcUpdate(&m_active[i].context, &byte, 1))
                    {
                        m_error = ITOOL_TR("Failed to update the CRC32/POSIX length.");
                        m_ok = false;
                        break;
                    }
                    remaining >>= 8;
                }
                if (!m_ok)
                    break;
            }
        }

        wxQueueEvent(m_owner, new wxThreadEvent(EVT_CRC_FILE_COMPLETE));
        return static_cast<ExitCode>(0);
    }

private:
    wxEvtHandler* m_owner;
    wxString m_path;
    std::vector<ActiveCalculation> m_active;
    bool m_ok;
    u64 m_totalLength;
    wxString m_error;
};

class CrcGrid : public wxGrid
{
public:
    explicit CrcGrid(wxWindow* parent)
        : wxGrid(parent, wxID_ANY)
    {
    }

protected:
    void DrawColLabel(wxDC& dc, int column) wxOVERRIDE
    {
        wxGrid::DrawColLabel(dc, column);
        if (column != ColumnSelected || GetNumberRows() == 0)
            return;

        bool anySelected = false;
        bool allSelected = true;
        for (int row = 0; row < GetNumberRows(); ++row)
        {
            const bool selected = GetCellValue(row, ColumnSelected) == wxS("1");
            anySelected = anySelected || selected;
            allSelected = allSelected && selected;
        }

        int flags = 0;
        if (allSelected)
            flags |= wxCONTROL_CHECKED;
        else if (anySelected)
            flags |= wxCONTROL_UNDETERMINED;

        wxRendererNative& renderer = wxRendererNative::Get();
        const wxSize checkSize = renderer.GetCheckBoxSize(
            GetGridColLabelWindow(), flags);
        const wxRect labelRect(GetColLeft(column), 0,
                               GetColSize(column), GetColLabelSize());
        const wxRect checkRect(
            labelRect.x + (labelRect.width - checkSize.x) / 2,
            labelRect.y + (labelRect.height - checkSize.y) / 2,
            checkSize.x, checkSize.y);
        renderer.DrawCheckBox(GetGridColLabelWindow(), dc, checkRect, flags);
    }
};

class CrcPanel : public wxPanel
{
public:
    explicit CrcPanel(wxWindow* parent)
        : wxPanel(parent), m_mode(InputHex), m_input(NULL), m_filePath(NULL),
          m_browse(NULL), m_grid(NULL), m_calculate(NULL), m_status(NULL),
          m_fileThread(NULL), m_waitDialog(NULL)
    {
        BuildInterface();
        PopulateAlgorithms();
        UpdateInputMode();
    }

    ~CrcPanel()
    {
        if (m_fileThread != NULL)
        {
            m_fileThread->Wait();
            delete m_fileThread;
            m_fileThread = NULL;
        }
        ClearCrcTableCache();
    }

private:
    class FileDropTarget : public wxFileDropTarget
    {
    public:
        explicit FileDropTarget(CrcPanel* owner) : m_owner(owner) {}

        bool OnDropFiles(wxCoord, wxCoord,
                         const wxArrayString& filenames) wxOVERRIDE
        {
            return m_owner->AcceptDroppedFiles(filenames);
        }

    private:
        CrcPanel* m_owner;
    };

    void BuildInterface()
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* inputHeader = new wxBoxSizer(wxHORIZONTAL);

        wxRadioButton* hex = new wxRadioButton(
            this, wxID_ANY, wxS("Hex"), wxDefaultPosition, wxDefaultSize,
            wxRB_GROUP);
        wxRadioButton* text = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Text"));
        wxRadioButton* file = new wxRadioButton(this, wxID_ANY, ITOOL_TR("File"));
        inputHeader->Add(hex, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        inputHeader->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        inputHeader->Add(file, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

        m_filePath = new wxTextCtrl(this, wxID_ANY);
        m_browse = new wxButton(this, wxID_ANY, wxS("..."),
                                wxDefaultPosition, wxSize(36, -1));
        inputHeader->Add(m_filePath, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        inputHeader->Add(m_browse, 0, wxALIGN_CENTER_VERTICAL);
        root->Add(inputHeader, 0, wxEXPAND | wxALL, 8);

        m_input = new wxTextCtrl(
            this, wxID_ANY, wxS("31 32 33 34 35 36 37 38 39"),
            wxDefaultPosition, wxSize(-1, 130), wxTE_MULTILINE);
        root->Add(m_input, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        m_grid = new CrcGrid(this);
        m_grid->CreateGrid(kAlgorithmCount + 1, ColumnCount);
        m_grid->SetRowLabelSize(0);
        m_grid->SetColLabelValue(ColumnSelected, wxEmptyString);
        m_grid->SetColLabelValue(ColumnName, ITOOL_TR("Name"));
        m_grid->SetColLabelValue(ColumnResult, wxS("CRC (HEX)"));
        m_grid->SetColLabelValue(ColumnWidth, ITOOL_TR("Width"));
        m_grid->SetColLabelValue(ColumnInitial, wxS("Init (HEX)"));
        m_grid->SetColLabelValue(ColumnPolynomial, wxS("Poly (HEX)"));
        m_grid->SetColLabelValue(ColumnXorOut, wxS("XorOut (HEX)"));
        m_grid->SetColLabelValue(ColumnReflectIn, wxS("RefIn"));
        m_grid->SetColLabelValue(ColumnReflectOut, wxS("RefOut"));
        m_grid->SetColFormatBool(ColumnSelected);
        m_grid->SetColFormatBool(ColumnReflectIn);
        m_grid->SetColFormatBool(ColumnReflectOut);
        m_grid->SetColLabelAlignment(wxALIGN_CENTER, wxALIGN_CENTER);
        m_grid->SetDefaultCellAlignment(wxALIGN_CENTER, wxALIGN_CENTER);
        m_grid->SetColSize(ColumnSelected, 30);
        m_grid->SetColSize(ColumnName, 100);
        m_grid->SetColSize(ColumnResult, 125);
        m_grid->SetColSize(ColumnWidth, 55);
        m_grid->SetColSize(ColumnInitial, 115);
        m_grid->SetColSize(ColumnPolynomial, 125);
        m_grid->SetColSize(ColumnXorOut, 115);
        m_grid->SetColSize(ColumnReflectIn, 58);
        m_grid->SetColSize(ColumnReflectOut, 62);
        for (int row = 0; row <= kCustomRow; ++row)
        {
            m_grid->SetCellAlignment(row, ColumnSelected, wxALIGN_CENTER, wxALIGN_CENTER);
            m_grid->SetCellAlignment(row, ColumnName,
                                     wxALIGN_LEFT, wxALIGN_CENTER);
            m_grid->SetCellAlignment(row, ColumnReflectIn, wxALIGN_CENTER, wxALIGN_CENTER);
            m_grid->SetCellAlignment(row, ColumnReflectOut, wxALIGN_CENTER, wxALIGN_CENTER);
        }
        root->Add(m_grid, 2, wxEXPAND | wxLEFT | wxRIGHT, 8);

        wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL);
        m_calculate = new wxButton(this, wxID_ANY, ITOOL_TR("Calculate"));
        wxButton* clear = new wxButton(this, wxID_CLEAR, ITOOL_TR("Clear results"));
        m_status = new wxStaticText(this, wxID_ANY,
                                    ITOOL_TR("Select algorithms, then calculate."));
        footer->Add(m_calculate, 0, wxRIGHT, 8);
        footer->Add(clear, 0, wxRIGHT, 12);
        footer->Add(m_status, 1, wxALIGN_CENTER_VERTICAL);
        root->Add(footer, 0, wxEXPAND | wxALL, 8);
        SetSizer(root);

        hex->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) {
            m_mode = InputHex; UpdateInputMode();
        });
        text->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) {
            m_mode = InputText; UpdateInputMode();
        });
        file->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) {
            m_mode = InputFile; UpdateInputMode();
        });
        m_browse->Bind(wxEVT_BUTTON, &CrcPanel::OnBrowse, this);
        m_calculate->Bind(wxEVT_BUTTON, &CrcPanel::OnCalculate, this);
        clear->Bind(wxEVT_BUTTON, &CrcPanel::OnClearResults, this);
        m_grid->Bind(wxEVT_GRID_LABEL_LEFT_CLICK,
                     &CrcPanel::OnGridLabelClick, this);
        m_grid->Bind(wxEVT_GRID_CELL_LEFT_CLICK,
                     &CrcPanel::OnGridCellClick, this);
        m_grid->Bind(wxEVT_GRID_CELL_CHANGED,
                     &CrcPanel::OnGridCellChanged, this);
        m_grid->Bind(wxEVT_SIZE, &CrcPanel::OnGridSize, this);

        SetDropTarget(new FileDropTarget(this));
        m_input->SetDropTarget(new FileDropTarget(this));
        m_filePath->SetDropTarget(new FileDropTarget(this));
        m_grid->GetGridWindow()->SetDropTarget(new FileDropTarget(this));
        Bind(EVT_CRC_FILE_COMPLETE, &CrcPanel::OnFileCalculationComplete, this);
    }

    void OnGridSize(wxSizeEvent& event)
    {
        event.Skip();
        const int total = m_grid->GetClientSize().x - m_grid->GetRowLabelSize()
                        - wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, m_grid) - 4;
        if (total < 600) return;
        const int selected = 32, width = 56, reflectIn = 60, reflectOut = 64;
        const int flexible = total - selected - width - reflectIn - reflectOut;
        // Name previously used 18/78 of the flexible width. Keep it at 80%
        // of that share and distribute the released space across data columns.
        const int name = flexible * 72 / 390;
        const int result = flexible * 86 / 390;
        const int initial = flexible * 75 / 390;
        const int polynomial = flexible * 86 / 390;
        const int xorOut = flexible - name - result - initial - polynomial;
        m_grid->SetColSize(ColumnSelected, selected); m_grid->SetColSize(ColumnName, name);
        m_grid->SetColSize(ColumnResult, result); m_grid->SetColSize(ColumnWidth, width);
        m_grid->SetColSize(ColumnInitial, initial); m_grid->SetColSize(ColumnPolynomial, polynomial);
        m_grid->SetColSize(ColumnXorOut, xorOut); m_grid->SetColSize(ColumnReflectIn, reflectIn);
        m_grid->SetColSize(ColumnReflectOut, reflectOut);
    }

    void PopulateAlgorithms()
    {
        for (int row = 0; row < kAlgorithmCount; ++row)
        {
            const CrcConfig& config = *kAlgorithms[row].config;
            m_grid->SetCellValue(row, ColumnSelected,
                                 row == 9 ? wxS("1") : wxS("0"));
            m_grid->SetCellValue(row, ColumnName, kAlgorithms[row].name);
            m_grid->SetCellValue(row, ColumnWidth,
                                 wxString::Format(wxS("%u"), config.width));
            m_grid->SetCellValue(row, ColumnInitial,
                                 FormatHex(config.initial, config.width));
            m_grid->SetCellValue(row, ColumnPolynomial,
                                 FormatHex(config.polynomial, config.width));
            m_grid->SetCellValue(row, ColumnXorOut,
                                 FormatHex(config.xorOut, config.width));
            m_grid->SetCellValue(row, ColumnReflectIn,
                                 config.reflectIn ? wxS("1") : wxS("0"));
            m_grid->SetCellValue(row, ColumnReflectOut,
                                 config.reflectOut ? wxS("1") : wxS("0"));
            for (int column = ColumnName; column < ColumnCount; ++column)
                m_grid->SetReadOnly(row, column);
        }

        m_grid->SetCellValue(kCustomRow, ColumnSelected, wxS("0"));
        m_grid->SetCellValue(kCustomRow, ColumnName, ITOOL_TR("Custom"));
        m_grid->SetCellValue(kCustomRow, ColumnWidth, wxS("32"));
        m_grid->SetCellValue(kCustomRow, ColumnInitial, wxS("FFFFFFFF"));
        m_grid->SetCellValue(kCustomRow, ColumnPolynomial, wxS("04C11DB7"));
        m_grid->SetCellValue(kCustomRow, ColumnXorOut, wxS("FFFFFFFF"));
        m_grid->SetCellValue(kCustomRow, ColumnReflectIn, wxS("1"));
        m_grid->SetCellValue(kCustomRow, ColumnReflectOut, wxS("1"));
        m_grid->SetReadOnly(kCustomRow, ColumnName);
        m_grid->SetReadOnly(kCustomRow, ColumnResult);
    }

    void UpdateInputMode()
    {
        const bool fileMode = m_mode == InputFile;
        m_filePath->Enable(fileMode);
        m_browse->Enable(fileMode);
        m_input->Enable(!fileMode);
        if (m_mode == InputHex)
            m_input->SetHint(ITOOL_TR("Hex: 31 32 33 or 313233"));
        else if (m_mode == InputText)
            m_input->SetHint(ITOOL_TR("Text is processed using UTF-8 encoding"));

        if (fileMode)
            m_status->SetLabel(ITOOL_TR("You can drop a file onto this page."));
    }

    bool AcceptDroppedFiles(const wxArrayString& filenames)
    {
        if (m_mode != InputFile || filenames.empty())
            return false;

        for (size_t index = 0; index < filenames.size(); ++index)
        {
            if (!wxFileName::FileExists(filenames[index]))
                continue;

            m_filePath->SetValue(filenames[index]);
            m_status->SetLabel(filenames.size() > 1
                ? ITOOL_TR("Selected the first valid file from the dropped files.")
                : ITOOL_TR("Dropped file accepted."));
            return true;
        }

        m_status->SetLabel(ITOOL_TR("No valid file was found in the dropped content."));
        return false;
    }

    bool ReadCustomConfig(CrcConfig& config, wxString& error)
    {
        unsigned long width = 0;
        if (!m_grid->GetCellValue(kCustomRow, ColumnWidth).ToULong(&width) ||
            (width != 8 && width != 16 && width != 24 &&
             width != 32 && width != 64))
        {
            error = ITOOL_TR("Custom Width supports only 8, 16, 24, 32, or 64.");
            return false;
        }

        u64 initial = 0, polynomial = 0, xorOut = 0;
        if (!ParseUnsignedHex(m_grid->GetCellValue(kCustomRow, ColumnInitial), initial) ||
            !ParseUnsignedHex(m_grid->GetCellValue(kCustomRow, ColumnPolynomial), polynomial) ||
            !ParseUnsignedHex(m_grid->GetCellValue(kCustomRow, ColumnXorOut), xorOut))
        {
            error = ITOOL_TR("Custom Init, Poly, and XorOut must be hexadecimal numbers.");
            return false;
        }

        const u64 mask = width == 64 ? CRC_U64_C(0xFFFFFFFFFFFFFFFF)
                                     : ((CRC_U64_C(1) << width) - 1);
        if ((initial & ~mask) || (polynomial & ~mask) || (xorOut & ~mask))
        {
            error = ITOOL_TR("A custom parameter exceeds the bit width specified by Width.");
            return false;
        }

        std::memset(&config, 0, sizeof(config));
        config.width = static_cast<u32>(width);
        config.initial = initial;
        config.polynomial = polynomial;
        config.xorOut = xorOut;
        config.reflectIn = m_grid->GetCellValue(
            kCustomRow, ColumnReflectIn) == wxS("1");
        config.reflectOut = m_grid->GetCellValue(
            kCustomRow, ColumnReflectOut) == wxS("1");
        return true;
    }

    bool PrepareCalculations(std::vector<ActiveCalculation>& active,
                             wxString& error)
    {
        active.reserve(kAlgorithmCount + 1);
        for (int row = 0; row <= kCustomRow; ++row)
        {
            if (m_grid->GetCellValue(row, ColumnSelected) != wxS("1"))
                continue;

            ActiveCalculation item;
            std::memset(&item, 0, sizeof(item));
            item.row = row;
            item.posixLength = row < kAlgorithmCount
                ? kAlgorithms[row].posixLength : false;
            if (row == kCustomRow)
            {
                if (!ReadCustomConfig(item.config, error))
                    return false;
            }
            else
            {
                item.config = *kAlgorithms[row].config;
                item.config.table = NULL;
                item.config.tableUpdate = NULL;
            }
            active.push_back(item);
        }

        if (active.empty())
        {
            error = ITOOL_TR("Select at least one CRC algorithm.");
            return false;
        }

        for (size_t i = 0; i < active.size(); ++i)
        {
            if (!crcInitialize(&active[i].context, &active[i].config))
            {
                error = ITOOL_TR("Failed to initialize CRC.");
                return false;
            }
        }
        return true;
    }

    bool UpdateAll(std::vector<ActiveCalculation>& active,
                   const u8* data, size_t length)
    {
        const u32 count = static_cast<u32>(length);
        for (size_t i = 0; i < active.size(); ++i)
        {
            if (!crcUpdate(&active[i].context, data, count))
                return false;
        }
        return true;
    }

    bool FinalizeAll(std::vector<ActiveCalculation>& active,
                     u64 totalLength, wxString& error)
    {
        for (size_t i = 0; i < active.size(); ++i)
        {
            if (active[i].posixLength)
            {
                // if (totalLength > 0xFFFFFFFFULL)
                // {
                //     CRC32/POSIX length fields support inputs up to 4 GiB.
                //     return false;
                // }
                u32 remaining = static_cast<u32>(totalLength);
                while (remaining != 0)
                {
                    const u8 byte = static_cast<u8>(remaining & 0xFF);
                    if (!crcUpdate(&active[i].context, &byte, 1))
                    {
                        error = ITOOL_TR("Failed to update the CRC32/POSIX length.");
                        return false;
                    }
                    remaining >>= 8;
                }
            }

            const u64 result = crcFinalize(&active[i].context);
            m_grid->SetCellValue(active[i].row, ColumnResult,
                                 FormatHex(result, active[i].config.width));
        }
        return true;
    }

    void OnBrowse(wxCommandEvent&)
    {
        wxFileDialog dialog(this, ITOOL_TR("Select a file to calculate CRC"),
                            wxEmptyString, wxEmptyString,
                            ITOOL_TR("All files (*.*)|*.*"),
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
            m_filePath->SetValue(dialog.GetPath());
    }

    void OnGridLabelClick(wxGridEvent& event)
    {
        if (event.GetRow() == -1 && event.GetCol() == ColumnSelected)
        {
            bool allSelected = true;
            for (int row = 0; row <= kCustomRow; ++row)
            {
                if (m_grid->GetCellValue(row, ColumnSelected) != wxS("1"))
                {
                    allSelected = false;
                    break;
                }
            }

            const wxString value = allSelected ? wxS("0") : wxS("1");
            for (int row = 0; row <= kCustomRow; ++row)
                m_grid->SetCellValue(row, ColumnSelected, value);

            m_grid->ForceRefresh();
            m_grid->GetGridColLabelWindow()->Refresh(false);
            return;
        }

        event.Skip();
    }

    void OnGridCellChanged(wxGridEvent& event)
    {
        if (event.GetCol() == ColumnSelected)
            m_grid->GetGridColLabelWindow()->Refresh(false);
        event.Skip();
    }

    void OnGridCellClick(wxGridEvent& event)
    {
        const int row = event.GetRow();
        const int column = event.GetCol();
        const bool checkboxColumn = column == ColumnSelected ||
                                    column == ColumnReflectIn ||
                                    column == ColumnReflectOut;
        if (!checkboxColumn || row < 0 || m_grid->IsReadOnly(row, column))
        {
            event.Skip();
            return;
        }

        m_grid->SetGridCursor(row, column);
        m_grid->SetCellValue(row, column,
            m_grid->GetCellValue(row, column) == wxS("1") ? wxS("0") : wxS("1"));
        m_grid->ForceRefresh();
        if (column == ColumnSelected)
            m_grid->GetGridColLabelWindow()->Refresh(false);
    }

    void OnCalculate(wxCommandEvent&)
    {
        std::vector<ActiveCalculation> active;
        wxString error;
        if (!PrepareCalculations(active, error))
        {
            ShowError(error);
            return;
        }

        u64 totalLength = 0;
        bool ok = true;
        wxBusyCursor busy;
        if (m_mode == InputFile)
        {
            const wxString path = m_filePath->GetValue();
            if (path.empty() || !wxFileName::FileExists(path))
            {
                ShowError(ITOOL_TR("Select an existing file."));
                return;
            }

            m_fileThread = new FileCrcThread(this, path, active);
            if (m_fileThread->Create() != wxTHREAD_NO_ERROR)
            {
                delete m_fileThread;
                m_fileThread = NULL;
                ShowError(ITOOL_TR("Failed to create the file CRC worker thread."));
                return;
            }

            const wxULongLong fileSize = wxFileName(path).GetSize();
            const bool showWaitDialog = fileSize != wxInvalidSize &&
                fileSize.GetValue() > CRC_U64_C(50) * 1024 * 1024;
            if (showWaitDialog)
            {
                m_waitDialog = new wxDialog(
                    this, wxID_ANY, wxS("CRC"), wxDefaultPosition,
                    wxDefaultSize, wxDEFAULT_DIALOG_STYLE & ~wxCLOSE_BOX);
                wxBoxSizer* waitLayout = new wxBoxSizer(wxVERTICAL);
                waitLayout->Add(new wxStaticText(
                    m_waitDialog, wxID_ANY, ITOOL_TR("Please wait ...")),
                    0, wxALL | wxALIGN_CENTER, 28);
                m_waitDialog->SetSizerAndFit(waitLayout);
                m_waitDialog->CentreOnParent();
                m_waitDialog->Bind(wxEVT_CLOSE_WINDOW,
                    [](wxCloseEvent& event) { event.Veto(); });
            }

            m_calculate->Disable();
            m_status->SetLabel(ITOOL_TR("Calculating file CRC in the background ..."));
            if (m_fileThread->Run() != wxTHREAD_NO_ERROR)
            {
                m_calculate->Enable();
                delete m_fileThread;
                m_fileThread = NULL;
                if (m_waitDialog != NULL)
                {
                    m_waitDialog->Destroy();
                    m_waitDialog = NULL;
                }
                ShowError(ITOOL_TR("Failed to start the file CRC worker thread."));
                return;
            }

            if (m_waitDialog != NULL)
            {
                m_waitDialog->ShowModal();
                m_waitDialog->Destroy();
                m_waitDialog = NULL;
            }
            return;
        }
        else
        {
            std::vector<u8> bytes;
            if (m_mode == InputHex)
            {
                ok = ParseHexInput(m_input->GetValue(), bytes, error);
            }
            else
            {
                const wxScopedCharBuffer utf8 = m_input->GetValue().utf8_str();
                const size_t length = utf8.length();
                bytes.assign(reinterpret_cast<const u8*>(utf8.data()),
                             reinterpret_cast<const u8*>(utf8.data()) + length);
            }

            if (ok)
            {
                totalLength = bytes.size();
                ok = UpdateAll(active, bytes.empty() ? NULL : &bytes[0],
                               bytes.size());
                if (!ok)
                    error = ITOOL_TR("Incremental CRC calculation failed.");
            }
        }

        if (ok)
            ok = FinalizeAll(active, totalLength, error);
        if (!ok)
        {
            ShowError(error);
            return;
        }

        m_status->SetLabel(wxString::Format(
            ITOOL_TR("Completed: %llu bytes, %lu algorithms."),
            static_cast<unsigned long long>(totalLength),
            static_cast<unsigned long>(active.size())));
    }

    void OnFileCalculationComplete(wxThreadEvent&)
    {
        if (m_fileThread == NULL)
            return;

        m_fileThread->Wait();
        const bool ok = m_fileThread->WasSuccessful();
        const wxString error = m_fileThread->GetError();
        const u64 totalLength = m_fileThread->GetTotalLength();
        std::vector<ActiveCalculation>& active =
            m_fileThread->GetCalculations();

        if (ok)
        {
            for (size_t i = 0; i < active.size(); ++i)
            {
                const u64 result = crcFinalize(&active[i].context);
                m_grid->SetCellValue(active[i].row, ColumnResult,
                                     FormatHex(result, active[i].config.width));
            }
            m_status->SetLabel(wxString::Format(
                ITOOL_TR("Completed: %llu bytes, %lu algorithms."),
                static_cast<unsigned long long>(totalLength),
                static_cast<unsigned long>(active.size())));
        }

        delete m_fileThread;
        m_fileThread = NULL;
        m_calculate->Enable();

        if (m_waitDialog != NULL && m_waitDialog->IsModal())
            m_waitDialog->EndModal(ok ? wxID_OK : wxID_CANCEL);

        if (!ok)
            ShowError(error);
    }

    void OnClearResults(wxCommandEvent&)
    {
        for (int row = 0; row <= kCustomRow; ++row)
            m_grid->SetCellValue(row, ColumnResult, wxEmptyString);
        m_status->SetLabel(ITOOL_TR("Results cleared."));
    }

    void ShowError(const wxString& error)
    {
        wxMessageBox(error, ITOOL_TR("CRC calculation error"),
                     wxOK | wxICON_WARNING, this);
    }

    InputMode m_mode;
    wxTextCtrl* m_input;
    wxTextCtrl* m_filePath;
    wxButton* m_browse;
    wxGrid* m_grid;
    wxButton* m_calculate;
    wxStaticText* m_status;
    FileCrcThread* m_fileThread;
    wxDialog* m_waitDialog;
};
} // namespace

wxString CrcToolModule::GetId() const
{
    return wxS("crc-calculator");
}

wxString CrcToolModule::GetName() const
{
    return ITOOL_TR("CRC calculator");
}

wxString CrcToolModule::GetDescription() const
{
    return ITOOL_TR("Calculate standard and custom CRC values for text, bytes, or files");
}

wxWindow* CrcToolModule::CreatePanel(wxWindow* parent)
{
    return new CrcPanel(parent);
}
