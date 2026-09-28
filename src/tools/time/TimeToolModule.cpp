#include "tools/time/TimeToolModule.h"
#include "core/Localization.h"

#include <stdint.h>

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/datetime.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "tools/time/TimeConverter.h"

namespace
{
class TimePanel : public wxPanel
{
public:
    explicit TimePanel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxStaticBoxSizer* unixBox = new wxStaticBoxSizer(wxHORIZONTAL, this, ITOOL_TR("Unix timestamp"));
        m_timestamp = new wxTextCtrl(this, wxID_ANY); wxArrayString units; units.Add(ITOOL_TR("Seconds")); units.Add(ITOOL_TR("Milliseconds"));
        m_unit = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(80, -1), units); m_unit->SetSelection(0);
        wxButton* toUtc = new wxButton(this, wxID_ANY, ITOOL_TR("Convert to UTC ↓")); wxButton* copyTimestamp = new wxButton(this, wxID_ANY, ITOOL_TR("Copy"));
        unixBox->Add(m_timestamp, 1, wxEXPAND | wxALL, 8); unixBox->Add(m_unit, 0, wxEXPAND | wxTOP | wxBOTTOM, 8);
        unixBox->Add(toUtc, 0, wxEXPAND | wxALL, 8); unixBox->Add(copyTimestamp, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 8);
        root->Add(unixBox, 0, wxEXPAND | wxALL, 12);

        wxStaticBoxSizer* utcBox = new wxStaticBoxSizer(wxHORIZONTAL, this, ITOOL_TR("UTC time (YYYY-MM-DD HH:MM:SS[.mmm])"));
        m_utc = new wxTextCtrl(this, wxID_ANY); wxButton* toUnix = new wxButton(this, wxID_ANY, ITOOL_TR("Convert to Unix ↑")); wxButton* copyUtc = new wxButton(this, wxID_ANY, ITOOL_TR("Copy"));
        utcBox->Add(m_utc, 1, wxEXPAND | wxALL, 8); utcBox->Add(toUnix, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 8); utcBox->Add(copyUtc, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 8);
        root->Add(utcBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        wxBoxSizer* actions = new wxBoxSizer(wxHORIZONTAL); m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("UTC input may use a space or T separator and an optional trailing Z."));
        wxButton* now = new wxButton(this, wxID_ANY, ITOOL_TR("Current time")); wxButton* clear = new wxButton(this, wxID_CLEAR, ITOOL_TR("Clear"));
        actions->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8); actions->Add(now, 0, wxRIGHT, 8); actions->Add(clear);
        root->Add(actions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12); root->AddStretchSpacer(); SetSizer(root);
        toUtc->Bind(wxEVT_BUTTON, &TimePanel::ToUtc, this); toUnix->Bind(wxEVT_BUTTON, &TimePanel::ToUnix, this);
        now->Bind(wxEVT_BUTTON, &TimePanel::Now, this); clear->Bind(wxEVT_BUTTON, &TimePanel::Clear, this);
        copyTimestamp->Bind(wxEVT_BUTTON, &TimePanel::CopyTimestamp, this); copyUtc->Bind(wxEVT_BUTTON, &TimePanel::CopyUtc, this);
        m_unit->Bind(wxEVT_CHOICE, &TimePanel::UnitChanged, this); NowDummy();
    }
private:
    bool Milliseconds() const { return m_unit->GetSelection() == 1; }
    void ToUtc(wxCommandEvent&)
    {
        int64_t value = 0; wxString error;
        if (!timeconv::ParseTimestamp(m_timestamp->GetValue(), Milliseconds(), value, error)) { Error(error); return; }
        m_utc->SetValue(timeconv::FormatUtc(value, Milliseconds())); m_status->SetLabel(ITOOL_TR("Unix timestamp converted to UTC."));
    }
    void ToUnix(wxCommandEvent&)
    {
        int64_t value = 0; wxString error;
        if (!timeconv::ParseUtc(m_utc->GetValue(), value, error)) { Error(error); return; }
        m_timestamp->SetValue(timeconv::FormatTimestamp(value, Milliseconds())); m_utc->SetValue(timeconv::FormatUtc(value, Milliseconds()));
        m_status->SetLabel(ITOOL_TR("UTC time converted to a Unix timestamp."));
    }
    void Now(wxCommandEvent&) { NowDummy(); }
    void NowDummy()
    {
        const wxDateTime now = wxDateTime::UNow();
        const int64_t value = static_cast<int64_t>(now.GetTicks()) * 1000 + now.GetMillisecond();
        m_timestamp->SetValue(timeconv::FormatTimestamp(value, Milliseconds())); m_utc->SetValue(timeconv::FormatUtc(value, Milliseconds())); m_status->SetLabel(ITOOL_TR("Current UTC time loaded."));
    }
    void UnitChanged(wxCommandEvent&)
    {
        int64_t value = 0; wxString error;
        if (timeconv::ParseUtc(m_utc->GetValue(), value, error)) m_timestamp->SetValue(timeconv::FormatTimestamp(value, Milliseconds()));
    }
    void Clear(wxCommandEvent&) { m_timestamp->Clear(); m_utc->Clear(); m_status->SetLabel(ITOOL_TR("Cleared.")); }
    void Copy(const wxString& text)
    {
        if (wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxTextDataObject(text)); wxTheClipboard->Close(); m_status->SetLabel(ITOOL_TR("Copied to the clipboard.")); }
    }
    void CopyTimestamp(wxCommandEvent&) { Copy(m_timestamp->GetValue()); }
    void CopyUtc(wxCommandEvent&) { Copy(m_utc->GetValue()); }
    void Error(const wxString& error) { wxMessageBox(error, ITOOL_TR("Unix / UTC conversion"), wxOK | wxICON_WARNING, this); }
    wxTextCtrl *m_timestamp, *m_utc; wxChoice* m_unit; wxStaticText* m_status;
};
}

wxString TimeToolModule::GetId() const { return wxS("unix-utc-time"); }
wxString TimeToolModule::GetName() const { return ITOOL_TR("Timestamp converter"); }
wxString TimeToolModule::GetDescription() const { return ITOOL_TR("Convert between 64-bit Unix timestamps and UTC date/time"); }
wxWindow* TimeToolModule::CreatePanel(wxWindow* parent) { return new TimePanel(parent); }
