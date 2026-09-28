#include "tools/ascii/AsciiConverterModule.h"
#include "core/Localization.h"
#include "tools/ascii/AsciiConverter.h"

#include <algorithm>
#include <vector>

#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace
{
const int kMargin = 16;

class AsciiConverterPanel : public wxPanel
{
public:
    explicit AsciiConverterPanel(wxWindow* parent)
        : wxPanel(parent), m_hexInput(NULL), m_asciiInput(NULL), m_hint(NULL), m_byteCount(NULL),
          m_lastHintWidth(-1)
    {
        BuildInterface();
    }

private:
    void BuildInterface()
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

        wxStaticText* title = new wxStaticText(
            this, wxID_ANY, ITOOL_TR("Convert between hexadecimal strings and ASCII"));
        wxFont titleFont = title->GetFont();
        titleFont.SetPointSize(titleFont.GetPointSize() + 3);
        titleFont.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(titleFont);
        root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, kMargin);
        root->AddSpacer(6);

        m_hintText = ITOOL_TR("Hex bytes may be separated by spaces, commas, semicolons, or line breaks, for example 31 32 33; ")
            + ITOOL_TR("the 0x prefix is allowed and the Latin-1 range is 00–FF.");
        m_hint = new wxStaticText(this, wxID_ANY, m_hintText);
        m_hint->SetMinSize(wxSize(0, -1));
        root->Add(m_hint, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, kMargin);

        root->Add(new wxStaticLine(this), 0,
                  wxEXPAND | wxLEFT | wxRIGHT, kMargin);
        root->AddSpacer(12);

        root->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Hex string")),
                  0, wxLEFT | wxRIGHT, kMargin);
        m_hexInput = new wxTextCtrl(
            this, wxID_ANY, "31 32 33", wxDefaultPosition,
            wxSize(-1, 100), wxTE_MULTILINE);
        m_hexInput->SetHint(ITOOL_TR("For example: 31 32 33 or 0x31, 0x32, 0x33"));
        root->Add(m_hexInput, 1,
                  wxEXPAND | wxLEFT | wxRIGHT | wxTOP, kMargin);

        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        wxButton* toAscii = new wxButton(this, wxID_ANY, wxS("Hex → ASCII"));
        wxButton* toHex = new wxButton(this, wxID_ANY, wxS("ASCII → Hex"));
        wxButton* clear = new wxButton(this, wxID_CLEAR, ITOOL_TR("Clear"));
        buttons->Add(toAscii, 0, wxRIGHT, 8);
        buttons->Add(toHex, 0, wxRIGHT, 8);
        buttons->AddStretchSpacer();
        buttons->Add(clear);
        root->Add(buttons, 0, wxEXPAND | wxALL, kMargin);

        m_byteCount = new wxStaticText(this, wxID_ANY, wxEmptyString);
        UpdateByteCount(0);
        wxBoxSizer* asciiHeader = new wxBoxSizer(wxHORIZONTAL);
        asciiHeader->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("ASCII string")),
                         0, wxALIGN_CENTER_VERTICAL);
        asciiHeader->AddStretchSpacer();
        asciiHeader->Add(m_byteCount, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        root->Add(asciiHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, kMargin);
        m_asciiInput = new wxTextCtrl(
            this, wxID_ANY, wxEmptyString, wxDefaultPosition,
            wxSize(-1, 100), wxTE_MULTILINE);
        m_asciiInput->SetHint(ITOOL_TR("For example: 123"));
        root->Add(m_asciiInput, 1,
                  wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, kMargin);

        SetSizer(root);

        toAscii->Bind(wxEVT_BUTTON,
                      &AsciiConverterPanel::OnConvertToAscii, this);
        toHex->Bind(wxEVT_BUTTON,
                    &AsciiConverterPanel::OnConvertToHex, this);
        clear->Bind(wxEVT_BUTTON,
                    &AsciiConverterPanel::OnClear, this);
        Bind(wxEVT_SIZE, &AsciiConverterPanel::OnSize, this);
    }

    void OnSize(wxSizeEvent& event)
    {
        event.Skip();
        const int width = std::max(1, GetClientSize().x - 2 * kMargin);
        if (!m_hint || width == m_lastHintWidth) return;
        m_lastHintWidth = width;
        m_hint->SetLabel(m_hintText);
        m_hint->Wrap(width);
        Layout();
    }

    void OnConvertToAscii(wxCommandEvent&)
    {
        std::vector<unsigned char> bytes;
        wxString error;
        if (!asciiconv::ParseHexBytes(m_hexInput->GetValue(), bytes, error))
        {
            ShowInputError(error, m_hexInput);
            return;
        }

        m_asciiInput->SetValue(asciiconv::BytesToText(bytes));
        UpdateByteCount(bytes.size());
    }

    void OnConvertToHex(wxCommandEvent&)
    {
        const wxString input = m_asciiInput->GetValue();
        if (input.empty())
        {
            ShowInputError(ITOOL_TR("Enter an ASCII string, for example: 123"), m_asciiInput);
            return;
        }

        wxString hex, error;
        if (!asciiconv::TextToHex(input, hex, error))
        {
            ShowInputError(error, m_asciiInput);
            return;
        }

        m_hexInput->SetValue(hex);
        UpdateByteCount(input.length());
    }

    void OnClear(wxCommandEvent&)
    {
        m_hexInput->Clear();
        m_asciiInput->Clear();
        UpdateByteCount(0);
        m_hexInput->SetFocus();
    }

    void UpdateByteCount(size_t count)
    {
        m_byteCount->SetLabel(wxString::Format(
            ITOOL_TR("Converted bytes: %llu"), static_cast<unsigned long long>(count)));
        Layout();
    }

    void ShowInputError(const wxString& message, wxTextCtrl* control)
    {
        UpdateByteCount(0);
        wxMessageBox(message, ITOOL_TR("Invalid input"),
                     wxOK | wxICON_WARNING, this);
        control->SetFocus();
        control->SelectAll();
    }

    wxTextCtrl* m_hexInput;
    wxTextCtrl* m_asciiInput;
    wxStaticText* m_hint;
    wxStaticText* m_byteCount;
    wxString m_hintText;
    int m_lastHintWidth;
};
} // namespace

wxString AsciiConverterModule::GetId() const
{
    return wxS("ascii-converter");
}

wxString AsciiConverterModule::GetName() const
{
    return ITOOL_TR("ASCII converter");
}

wxString AsciiConverterModule::GetDescription() const
{
    return ITOOL_TR("Convert between hexadecimal strings and ASCII") + wxS(" (Latin-1, 00–FF)");
}

wxWindow* AsciiConverterModule::CreatePanel(wxWindow* parent)
{
    return new AsciiConverterPanel(parent);
}
