#include "tools/barcode/BarcodeToolModule.h"
#include "core/Localization.h"

#include <algorithm>
#include <numeric>
#include <vector>

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "tools/barcode/Code128Encoder.h"

namespace
{
class BarcodePreview : public wxPanel
{
public:
    explicit BarcodePreview(wxWindow* parent) : wxPanel(parent)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT); Bind(wxEVT_PAINT, &BarcodePreview::Paint, this);
    }
    void SetBitmap(const wxBitmap& bitmap) { m_bitmap = bitmap; Refresh(false); }
private:
    void Paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this); dc.SetBackground(*wxLIGHT_GREY_BRUSH); dc.Clear();
        if (!m_bitmap.IsOk()) return;
        const wxSize area = GetClientSize(); double scale = 1.0;
        if (m_bitmap.GetWidth() > area.x - 16) scale = static_cast<double>(area.x - 16) / m_bitmap.GetWidth();
        if (m_bitmap.GetHeight() * scale > area.y - 16) scale = static_cast<double>(area.y - 16) / m_bitmap.GetHeight();
        wxBitmap shown = m_bitmap;
        if (scale < 1.0)
            shown = wxBitmap(m_bitmap.ConvertToImage().Scale(std::max(1, static_cast<int>(m_bitmap.GetWidth() * scale)),
                                                              std::max(1, static_cast<int>(m_bitmap.GetHeight() * scale)), wxIMAGE_QUALITY_NORMAL));
        dc.DrawBitmap(shown, (area.x - shown.GetWidth()) / 2, (area.y - shown.GetHeight()) / 2, false);
    }
    wxBitmap m_bitmap;
};

class BarcodePanel : public wxPanel
{
public:
    explicit BarcodePanel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxStaticBoxSizer* inputBox = new wxStaticBoxSizer(wxVERTICAL, this, ITOOL_TR("Code 128-B content"));
        m_text = new wxTextCtrl(this, wxID_ANY, wxS("iTool-123456")); inputBox->Add(m_text, 0, wxEXPAND | wxALL, 8);
        wxBoxSizer* options = new wxBoxSizer(wxHORIZONTAL);
        options->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Module width:")), 0, wxALIGN_CENTER_VERTICAL);
        m_scale = new wxSpinCtrl(this, wxID_ANY, wxS("3"), wxDefaultPosition, wxSize(65, -1), wxSP_ARROW_KEYS, 1, 10, 3);
        options->Add(m_scale, 0, wxRIGHT, 14); options->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Barcode height:")), 0, wxALIGN_CENTER_VERTICAL);
        m_height = new wxSpinCtrl(this, wxID_ANY, wxS("140"), wxDefaultPosition, wxSize(75, -1), wxSP_ARROW_KEYS, 40, 1000, 140);
        options->Add(m_height, 0, wxRIGHT, 14); m_showText = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Show original text")); m_showText->SetValue(true);
        options->Add(m_showText, 0, wxALIGN_CENTER_VERTICAL); inputBox->Add(options, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        root->Add(inputBox, 0, wxEXPAND | wxALL, 8);
        m_preview = new BarcodePreview(this); root->Add(m_preview, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
        wxBoxSizer* actions = new wxBoxSizer(wxHORIZONTAL); m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Code 128-B generates the checksum automatically."));
        wxButton* generate = new wxButton(this, wxID_ANY, ITOOL_TR("Generate")); wxButton* copy = new wxButton(this, wxID_ANY, ITOOL_TR("Copy image")); wxButton* save = new wxButton(this, wxID_ANY, ITOOL_TR("Save PNG"));
        actions->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8); actions->Add(generate, 0, wxRIGHT, 6); actions->Add(copy, 0, wxRIGHT, 6); actions->Add(save);
        root->Add(actions, 0, wxEXPAND | wxALL, 8); SetSizer(root);
        generate->Bind(wxEVT_BUTTON, &BarcodePanel::Generate, this); copy->Bind(wxEVT_BUTTON, &BarcodePanel::Copy, this); save->Bind(wxEVT_BUTTON, &BarcodePanel::Save, this);
        m_scale->Bind(wxEVT_SPINCTRL, &BarcodePanel::Generate, this); m_height->Bind(wxEVT_SPINCTRL, &BarcodePanel::Generate, this); m_showText->Bind(wxEVT_CHECKBOX, &BarcodePanel::Generate, this);
        wxCommandEvent event; Generate(event);
    }
private:
    bool MakeBitmap(wxString& error)
    {
        std::vector<int> symbols, widths;
        if (!barcode::EncodeCode128B(m_text->GetValue(), symbols, widths, error)) return false;
        const int module = m_scale->GetValue(), quiet = 10 * module;
        const int bars = std::accumulate(widths.begin(), widths.end(), 0) * module;
        const int textHeight = m_showText->GetValue() ? 30 : 0;
        m_bitmap = wxBitmap(bars + quiet * 2, m_height->GetValue() + textHeight + 16, 24);
        wxMemoryDC dc(m_bitmap); dc.SetBackground(*wxWHITE_BRUSH); dc.Clear(); dc.SetPen(*wxTRANSPARENT_PEN); dc.SetBrush(*wxBLACK_BRUSH);
        int x = quiet; bool black = true;
        for (size_t i = 0; i < widths.size(); ++i)
        {
            const int width = widths[i] * module;
            if (black) dc.DrawRectangle(x, 8, width, m_height->GetValue());
            x += width; black = !black;
        }
        if (m_showText->GetValue())
        {
            dc.SetTextForeground(*wxBLACK); dc.SetFont(GetFont());
            wxCoord width = 0, height = 0; dc.GetTextExtent(m_text->GetValue(), &width, &height);
            dc.DrawText(m_text->GetValue(), (m_bitmap.GetWidth() - width) / 2, m_height->GetValue() + 11);
        }
        dc.SelectObject(wxNullBitmap); m_preview->SetBitmap(m_bitmap);
        m_status->SetLabel(wxString::Format(ITOOL_TR("%lu characters; image %d × %d."), static_cast<unsigned long>(m_text->GetValue().length()), m_bitmap.GetWidth(), m_bitmap.GetHeight()));
        return true;
    }
    void Generate(wxCommandEvent&) { wxString error; if (!MakeBitmap(error)) wxMessageBox(error, ITOOL_TR("Barcode encoding"), wxOK | wxICON_WARNING, this); }
    void Copy(wxCommandEvent&)
    {
        wxString error; if (!MakeBitmap(error)) { wxMessageBox(error); return; }
        if (wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxBitmapDataObject(m_bitmap)); wxTheClipboard->Close(); m_status->SetLabel(ITOOL_TR("Barcode image copied.")); }
    }
    void Save(wxCommandEvent&)
    {
        wxString error; if (!MakeBitmap(error)) { wxMessageBox(error); return; }
        wxFileDialog dialog(this, ITOOL_TR("Save barcode"), wxEmptyString, wxS("barcode.png"), ITOOL_TR("PNG image (*.png)|*.png"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() == wxID_OK)
        {
            if (!m_bitmap.ConvertToImage().SaveFile(dialog.GetPath(), wxBITMAP_TYPE_PNG)) wxMessageBox(ITOOL_TR("Failed to save PNG."), ITOOL_TR("Barcode encoding"), wxOK | wxICON_ERROR, this);
            else m_status->SetLabel(ITOOL_TR("PNG saved."));
        }
    }
    wxTextCtrl* m_text; wxSpinCtrl *m_scale, *m_height; wxCheckBox* m_showText; BarcodePreview* m_preview;
    wxStaticText* m_status; wxBitmap m_bitmap;
};
}

wxString BarcodeToolModule::GetId() const { return wxS("barcode-encoder"); }
wxString BarcodeToolModule::GetName() const { return ITOOL_TR("Barcode encoder"); }
wxString BarcodeToolModule::GetDescription() const { return ITOOL_TR("Encode ASCII text as a Code 128-B barcode"); }
wxWindow* BarcodeToolModule::CreatePanel(wxWindow* parent) { return new BarcodePanel(parent); }
