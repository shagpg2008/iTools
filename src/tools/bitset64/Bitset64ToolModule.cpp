#include "tools/bitset64/Bitset64ToolModule.h"
#include "core/Localization.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdint.h>
#include <string>

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/control.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/settings.h>

namespace
{
enum DisplayBase { BaseHex = 0, BaseDecimal = 1, BaseBinary = 2 };
const int kActionHeight = 28;
const int kActionGap = 4;
const int kValueInputHeight = kActionHeight * 3 + kActionGap * 2;

std::string BinaryText(uint64_t value)
{
    std::string result("0b");
    for (int bit = 63; bit >= 0; --bit)
    {
        result += (value & (UINT64_C(1) << bit)) ? '1' : '0';
        if (bit && bit % 8 == 0) result += ' ';
    }
    return result;
}

wxString FormatValue(uint64_t value, DisplayBase base)
{
    if (base == BaseBinary) return wxString::FromUTF8(BinaryText(value).c_str());
    std::ostringstream stream;
    if (base == BaseHex)
        stream << "0x" << std::uppercase << std::hex << value;
    else
        stream << std::dec << value;
    return wxString::FromUTF8(stream.str().c_str());
}

bool ParseValue(const wxString& source, DisplayBase selectedBase, uint64_t& value)
{
    wxString clean(source); clean.Trim(true).Trim(false); clean.Replace(wxS(" "), wxEmptyString);
    clean.Replace(wxS("_"), wxEmptyString);
    int base = selectedBase == BaseHex ? 16 : (selectedBase == BaseBinary ? 2 : 10);
    if (clean.StartsWith(wxS("0x")) || clean.StartsWith(wxS("0X"))) { base = 16; clean = clean.Mid(2); }
    else if (clean.StartsWith(wxS("0b")) || clean.StartsWith(wxS("0B"))) { base = 2; clean = clean.Mid(2); }
    if (clean.empty() || clean.StartsWith(wxS("-"))) return false;
    wxCharBuffer ascii = clean.utf8_str();
    errno = 0; char* end = NULL;
    const unsigned long long parsed = std::strtoull(ascii.data(), &end, base);
    if (errno == ERANGE || end == ascii.data() || *end != '\0') return false;
    value = static_cast<uint64_t>(parsed);
    return true;
}

class BitButton : public wxControl
{
public:
    BitButton(wxWindow* parent, int bit)
        : wxControl(parent, wxID_ANY, wxDefaultPosition, wxSize(36, 36), wxBORDER_NONE),
          m_bit(bit), m_value(false)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(36, 36));
        SetMaxSize(wxSize(36, 36));
        Bind(wxEVT_PAINT, &BitButton::OnPaint, this);
        Bind(wxEVT_LEFT_UP, &BitButton::OnClick, this);
    }

    void SetBitValue(bool value)
    {
        if (m_value != value) { m_value = value; Refresh(false); }
    }

    void SetSquareSize(int size)
    {
        const wxSize square(size, size);
        SetMinSize(square); SetMaxSize(square); SetSize(square);
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxColour background = m_value ? wxColour(40, 190, 70) : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
        const wxColour foreground = m_value ? *wxWHITE : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour())); dc.Clear();
        dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
        dc.SetBrush(wxBrush(background));
        const wxSize size = GetClientSize();
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, 4);

        wxFont indexFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
        dc.SetFont(indexFont); dc.SetTextForeground(foreground);
        dc.DrawText(wxString::Format(wxS("%d"), m_bit), 4, 2);

        wxFont valueFont(13, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        dc.SetFont(valueFont);
        const wxString valueText = m_value ? wxS("1") : wxS("0");
        wxCoord width = 0, height = 0; dc.GetTextExtent(valueText, &width, &height);
        dc.DrawText(valueText, (size.x - width) / 2, (size.y - height) / 2 + 2);
    }

    void OnClick(wxMouseEvent&)
    {
        wxCommandEvent event(wxEVT_BUTTON, GetId());
        event.SetEventObject(this);
        ProcessWindowEvent(event);
    }

    int m_bit;
    bool m_value;
};

class Bitset64Panel : public wxPanel
{
public:
    explicit Bitset64Panel(wxWindow* parent) : wxPanel(parent), m_value(0), m_updating(false)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxStaticBoxSizer* valueBox = new wxStaticBoxSizer(wxHORIZONTAL, this, ITOOL_TR("64-bit unsigned integer"));
        wxPanel* valueHost = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                         wxSize(-1, kValueInputHeight), wxBORDER_SUNKEN);
        valueHost->SetMinSize(wxSize(-1, kValueInputHeight));
        valueHost->SetMaxSize(wxSize(-1, kValueInputHeight));
        valueHost->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        m_valueText = new wxTextCtrl(valueHost, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                     wxTE_PROCESS_ENTER | wxTE_CENTRE | wxBORDER_NONE);
        wxFont valueDisplayFont = m_valueText->GetFont();
        valueDisplayFont.SetPointSize(valueDisplayFont.GetPointSize() * 3);
        valueDisplayFont.SetWeight(wxFONTWEIGHT_BOLD);
        m_valueText->SetFont(valueDisplayFont);
        m_valueText->SetForegroundColour(wxColour(0, 150, 45));
        m_valueText->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        wxCoord sampleWidth = 0, sampleHeight = 0;
        m_valueText->GetTextExtent(wxS("0xFFFFFFFFFFFFFFFF"), &sampleWidth, &sampleHeight);
        const int valueEditorHeight = std::min(kValueInputHeight - 8,
                                               static_cast<int>(sampleHeight) + 14);
        m_valueText->SetMinSize(wxSize(-1, valueEditorHeight));
        m_valueText->SetMaxSize(wxSize(-1, valueEditorHeight));
        wxArrayString bases; bases.Add(ITOOL_TR("Hexadecimal")); bases.Add(ITOOL_TR("Decimal")); bases.Add(ITOOL_TR("Binary"));
        m_base = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(-1, kActionHeight), bases); m_base->SetSelection(BaseHex);
        m_base->SetMinSize(wxSize(-1, kActionHeight)); m_base->SetMaxSize(wxSize(-1, kActionHeight));
        wxButton* copy = new wxButton(this, wxID_ANY, ITOOL_TR("Copy"), wxDefaultPosition, wxSize(-1, kActionHeight), wxBU_EXACTFIT);
        wxButton* paste = new wxButton(this, wxID_ANY, ITOOL_TR("Paste"), wxDefaultPosition, wxSize(-1, kActionHeight), wxBU_EXACTFIT);
        const wxFont actionFont = GetFont(); m_base->SetFont(actionFont); copy->SetFont(actionFont); paste->SetFont(actionFont);
        copy->SetMinSize(wxSize(-1, kActionHeight)); copy->SetMaxSize(wxSize(-1, kActionHeight));
        paste->SetMinSize(wxSize(-1, kActionHeight)); paste->SetMaxSize(wxSize(-1, kActionHeight));
        wxBoxSizer* valueTools = new wxBoxSizer(wxVERTICAL);
        valueTools->Add(m_base, 0, wxEXPAND | wxBOTTOM, kActionGap);
        valueTools->Add(copy, 0, wxEXPAND | wxBOTTOM, kActionGap);
        valueTools->Add(paste, 0, wxEXPAND);
        wxBoxSizer* centeredValue = new wxBoxSizer(wxVERTICAL);
        centeredValue->AddStretchSpacer(1);
        centeredValue->Add(m_valueText, 0, wxEXPAND | wxLEFT | wxRIGHT, 3);
        centeredValue->AddStretchSpacer(1);
        valueHost->SetSizer(centeredValue);
        valueBox->Add(valueHost, 1, wxEXPAND | wxALL, 5);
        valueBox->Add(valueTools, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 5);
        root->Add(valueBox, 0, wxEXPAND | wxALL, 8);

        for (int group = 0; group < 2; ++group)
        {
            wxBoxSizer* groupSizer = new wxBoxSizer(wxVERTICAL);
            for (int row = 0; row < 2; ++row)
            {
                wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);
                for (int column = 0; column < 16; ++column)
                {
                    const int index = group * 32 + row * 16 + column;
                    const int bit = 63 - index;
                    m_bits[index] = new BitButton(this, bit);
                    m_bits[index]->Bind(wxEVT_BUTTON, &Bitset64Panel::BitChanged, this);
                    rowSizer->Add(m_bits[index], 0, wxALIGN_CENTER_VERTICAL);
                    if (column != 15)
                        rowSizer->AddSpacer((column == 3 || column == 7 || column == 11) ? 14 : 4);
                }
                groupSizer->Add(rowSizer, 0, wxALIGN_CENTER | (row == 0 ? wxBOTTOM : 0), 4);
            }
            root->Add(groupSizer, 0, wxALIGN_CENTER);
            if (group == 0) root->AddSpacer(10);
        }

        wxBoxSizer* operations = new wxBoxSizer(wxHORIZONTAL);
        wxButton* invert = new wxButton(this, wxID_ANY, ITOOL_TR("Invert all"), wxDefaultPosition, wxSize(-1, kActionHeight), wxBU_EXACTFIT);
        wxButton* clear = new wxButton(this, wxID_ANY, ITOOL_TR("Clear all"), wxDefaultPosition, wxSize(-1, kActionHeight), wxBU_EXACTFIT);
        wxButton* all = new wxButton(this, wxID_ANY, ITOOL_TR("Set all to 1"), wxDefaultPosition, wxSize(-1, kActionHeight), wxBU_EXACTFIT);
        wxButton* left = new wxButton(this, wxID_ANY, ITOOL_TR("Shift left <<"), wxDefaultPosition, wxSize(-1, kActionHeight), wxBU_EXACTFIT);
        wxButton* right = new wxButton(this, wxID_ANY, ITOOL_TR("Shift right >>"), wxDefaultPosition, wxSize(-1, kActionHeight), wxBU_EXACTFIT);
        invert->SetFont(actionFont); clear->SetFont(actionFont); all->SetFont(actionFont); left->SetFont(actionFont); right->SetFont(actionFont);
        m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Value range: 0 to 2^64-1"));
        operations->Add(invert, 0, wxRIGHT, 6); operations->Add(clear, 0, wxRIGHT, 6);
        operations->Add(all, 0, wxRIGHT, 6); operations->Add(left, 0, wxRIGHT, 6); operations->Add(right, 0, wxRIGHT, 10);
        operations->Add(m_status, 1, wxALIGN_CENTER_VERTICAL);
        root->Add(operations, 0, wxEXPAND | wxALL, 8); SetSizer(root);

        m_valueText->Bind(wxEVT_TEXT_ENTER, &Bitset64Panel::Apply, this);
        m_base->Bind(wxEVT_CHOICE, &Bitset64Panel::BaseChanged, this);
        copy->Bind(wxEVT_BUTTON, &Bitset64Panel::Copy, this); paste->Bind(wxEVT_BUTTON, &Bitset64Panel::Paste, this);
        invert->Bind(wxEVT_BUTTON, &Bitset64Panel::Invert, this); clear->Bind(wxEVT_BUTTON, &Bitset64Panel::Clear, this);
        all->Bind(wxEVT_BUTTON, &Bitset64Panel::All, this); left->Bind(wxEVT_BUTTON, &Bitset64Panel::ShiftLeft, this);
        right->Bind(wxEVT_BUTTON, &Bitset64Panel::ShiftRight, this);
        Bind(wxEVT_SIZE, &Bitset64Panel::PanelResized, this);
        RefreshDisplay();
    }

private:
    void PanelResized(wxSizeEvent& event)
    {
        event.Skip();
        const int available = GetClientSize().x - 90;
        if (available <= 0) return;
        const int square = std::max(28, available / 16);
        if (m_bits[0]->GetMinSize().x == square) return;
        for (int i = 0; i < 64; ++i) m_bits[i]->SetSquareSize(square);
        Layout();
    }
    DisplayBase CurrentBase() const { return static_cast<DisplayBase>(m_base->GetSelection()); }

    void RefreshDisplay()
    {
        m_updating = true;
        m_valueText->ChangeValue(FormatValue(m_value, CurrentBase()));
        for (int index = 0; index < 64; ++index)
        {
            const int bit = 63 - index; const bool set = (m_value & (UINT64_C(1) << bit)) != 0;
            m_bits[index]->SetBitValue(set);
        }
        m_status->SetLabel(wxString::Format(ITOOL_TR("Bits set: %d / 64"), CountSetBits()));
        m_updating = false;
    }

    int CountSetBits() const
    {
        uint64_t value = m_value; int count = 0;
        while (value) { value &= value - 1; ++count; }
        return count;
    }

    void BitChanged(wxCommandEvent& event)
    {
        if (m_updating) return;
        for (int index = 0; index < 64; ++index)
            if (event.GetEventObject() == m_bits[index]) { m_value ^= UINT64_C(1) << (63 - index); break; }
        RefreshDisplay();
    }

    void Apply(wxCommandEvent&)
    {
        uint64_t parsed = 0;
        if (!ParseValue(m_valueText->GetValue(), CurrentBase(), parsed))
        {
            wxMessageBox(ITOOL_TR("The value format is invalid or outside the range of a 64-bit unsigned integer."), wxS("Bitset64"), wxOK | wxICON_WARNING, this);
            return;
        }
        m_value = parsed; RefreshDisplay();
    }

    void BaseChanged(wxCommandEvent&) { RefreshDisplay(); }
    void Invert(wxCommandEvent&) { m_value = ~m_value; RefreshDisplay(); }
    void Clear(wxCommandEvent&) { m_value = 0; RefreshDisplay(); }
    void All(wxCommandEvent&) { m_value = std::numeric_limits<uint64_t>::max(); RefreshDisplay(); }
    void ShiftLeft(wxCommandEvent&) { m_value <<= 1; RefreshDisplay(); }
    void ShiftRight(wxCommandEvent&) { m_value >>= 1; RefreshDisplay(); }

    void Copy(wxCommandEvent&)
    {
        if (wxTheClipboard->Open())
        {
            wxTheClipboard->SetData(new wxTextDataObject(m_valueText->GetValue())); wxTheClipboard->Close();
            m_status->SetLabel(ITOOL_TR("Value copied to the clipboard."));
        }
    }

    void Paste(wxCommandEvent& event)
    {
        if (wxTheClipboard->Open())
        {
            if (wxTheClipboard->IsSupported(wxDF_TEXT))
            {
                wxTextDataObject data; wxTheClipboard->GetData(data); m_valueText->SetValue(data.GetText());
            }
            wxTheClipboard->Close();
        }
        Apply(event);
    }

    uint64_t m_value; bool m_updating; wxTextCtrl* m_valueText; wxChoice* m_base;
    BitButton* m_bits[64]; wxStaticText* m_status;
};
}

wxString Bitset64ToolModule::GetId() const { return wxS("bitset64"); }
wxString Bitset64ToolModule::GetName() const { return ITOOL_TR("Bitset64"); }
wxString Bitset64ToolModule::GetDescription() const { return ITOOL_TR("View and edit every bit of a 64-bit unsigned integer"); }
wxWindow* Bitset64ToolModule::CreatePanel(wxWindow* parent) { return new Bitset64Panel(parent); }
