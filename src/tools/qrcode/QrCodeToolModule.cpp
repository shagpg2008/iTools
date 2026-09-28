#include "tools/qrcode/QrCodeToolModule.h"
#include "core/Localization.h"

#include <algorithm>
#include <exception>
#include <string>

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/choice.h>
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

#include "third_party/qrcodegen/qrcodegen.hpp"

namespace
{
class QrPreview : public wxPanel
{
public:
    explicit QrPreview(wxWindow* parent) : wxPanel(parent)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT); Bind(wxEVT_PAINT, &QrPreview::Paint, this);
    }
    void SetBitmap(const wxBitmap& bitmap) { m_bitmap = bitmap; Refresh(false); }
private:
    void Paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this); dc.SetBackground(*wxLIGHT_GREY_BRUSH); dc.Clear();
        if (!m_bitmap.IsOk()) return;
        const wxSize area = GetClientSize(); const int available = std::max(1, std::min(area.x, area.y) - 16);
        wxBitmap shown = m_bitmap;
        if (m_bitmap.GetWidth() > available)
            shown = wxBitmap(m_bitmap.ConvertToImage().Scale(available, available, wxIMAGE_QUALITY_NORMAL));
        dc.DrawBitmap(shown, (area.x - shown.GetWidth()) / 2, (area.y - shown.GetHeight()) / 2, false);
    }
    wxBitmap m_bitmap;
};

class QrCodePanel : public wxPanel
{
public:
    explicit QrCodePanel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxStaticBoxSizer* inputBox = new wxStaticBoxSizer(wxVERTICAL, this, ITOOL_TR("QR Code content (UTF-8)"));
        m_text = new wxTextCtrl(this, wxID_ANY, wxS("https://example.com/"), wxDefaultPosition, wxSize(-1, 90), wxTE_MULTILINE | wxTE_RICH2);
        inputBox->Add(m_text, 1, wxEXPAND | wxALL, 8);
        wxBoxSizer* options = new wxBoxSizer(wxHORIZONTAL); options->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Error correction:")), 0, wxALIGN_CENTER_VERTICAL);
        wxArrayString levels; levels.Add(ITOOL_TR("L (about 7%)")); levels.Add(ITOOL_TR("M (about 15%)")); levels.Add(ITOOL_TR("Q (about 25%)")); levels.Add(ITOOL_TR("H (about 30%)"));
        m_level = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, levels); m_level->SetSelection(1);
        options->Add(m_level, 0, wxRIGHT, 16); options->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Pixels per module:")), 0, wxALIGN_CENTER_VERTICAL);
        m_scale = new wxSpinCtrl(this, wxID_ANY, wxS("8"), wxDefaultPosition, wxSize(65, -1), wxSP_ARROW_KEYS, 1, 30, 8);
        options->Add(m_scale, 0); inputBox->Add(options, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        root->Add(inputBox, 0, wxEXPAND | wxALL, 8);
        m_preview = new QrPreview(this); root->Add(m_preview, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
        wxBoxSizer* actions = new wxBoxSizer(wxHORIZONTAL); m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Includes the standard four-module white quiet zone."));
        wxButton* generate = new wxButton(this, wxID_ANY, ITOOL_TR("Generate")); wxButton* copy = new wxButton(this, wxID_ANY, ITOOL_TR("Copy image")); wxButton* save = new wxButton(this, wxID_ANY, ITOOL_TR("Save PNG"));
        actions->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8); actions->Add(generate, 0, wxRIGHT, 6); actions->Add(copy, 0, wxRIGHT, 6); actions->Add(save);
        root->Add(actions, 0, wxEXPAND | wxALL, 8); SetSizer(root);
        generate->Bind(wxEVT_BUTTON, &QrCodePanel::Generate, this); copy->Bind(wxEVT_BUTTON, &QrCodePanel::Copy, this); save->Bind(wxEVT_BUTTON, &QrCodePanel::Save, this);
        m_level->Bind(wxEVT_CHOICE, &QrCodePanel::Generate, this); m_scale->Bind(wxEVT_SPINCTRL, &QrCodePanel::Generate, this);
        wxCommandEvent event; Generate(event);
    }
private:
    qrcodegen::QrCode::Ecc ErrorCorrection() const
    {
        switch (m_level->GetSelection())
        {
        case 0: return qrcodegen::QrCode::Ecc::LOW;
        case 2: return qrcodegen::QrCode::Ecc::QUARTILE;
        case 3: return qrcodegen::QrCode::Ecc::HIGH;
        default: return qrcodegen::QrCode::Ecc::MEDIUM;
        }
    }
    bool MakeBitmap(wxString& error)
    {
        if (m_text->GetValue().empty()) { error = ITOOL_TR("QR Code content cannot be empty."); return false; }
        try
        {
            const wxCharBuffer utf8 = m_text->GetValue().utf8_str();
            const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(utf8.data(), ErrorCorrection());
            const int scale = m_scale->GetValue(), border = 4, modules = qr.getSize();
            const int pixels = (modules + border * 2) * scale;
            m_bitmap = wxBitmap(pixels, pixels, 24); wxMemoryDC dc(m_bitmap); dc.SetBackground(*wxWHITE_BRUSH); dc.Clear();
            dc.SetPen(*wxTRANSPARENT_PEN); dc.SetBrush(*wxBLACK_BRUSH);
            for (int y = 0; y < modules; ++y)
                for (int x = 0; x < modules; ++x)
                    if (qr.getModule(x, y)) dc.DrawRectangle((x + border) * scale, (y + border) * scale, scale, scale);
            dc.SelectObject(wxNullBitmap); m_preview->SetBitmap(m_bitmap);
            const int version = (modules - 17) / 4;
            m_status->SetLabel(wxString::Format(ITOOL_TR("Version %d; %d × %d modules; image %d × %d."), version, modules, modules, pixels, pixels));
            return true;
        }
        catch (const std::exception& exception)
        {
            error = ITOOL_TR("QR Code content is too long: ") + wxString::FromUTF8(exception.what()); return false;
        }
    }
    void Generate(wxCommandEvent&) { wxString error; if (!MakeBitmap(error)) wxMessageBox(error, ITOOL_TR("QR Code encoding"), wxOK | wxICON_WARNING, this); }
    void Copy(wxCommandEvent&)
    {
        wxString error; if (!MakeBitmap(error)) { wxMessageBox(error); return; }
        if (wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxBitmapDataObject(m_bitmap)); wxTheClipboard->Close(); m_status->SetLabel(ITOOL_TR("QR Code image copied.")); }
    }
    void Save(wxCommandEvent&)
    {
        wxString error; if (!MakeBitmap(error)) { wxMessageBox(error); return; }
        wxFileDialog dialog(this, ITOOL_TR("Save QR Code"), wxEmptyString, wxS("qrcode.png"), ITOOL_TR("PNG image (*.png)|*.png"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() == wxID_OK)
        {
            if (!m_bitmap.ConvertToImage().SaveFile(dialog.GetPath(), wxBITMAP_TYPE_PNG)) wxMessageBox(ITOOL_TR("Failed to save PNG."), ITOOL_TR("QR Code encoding"), wxOK | wxICON_ERROR, this);
            else m_status->SetLabel(ITOOL_TR("PNG saved."));
        }
    }
    wxTextCtrl* m_text; wxChoice* m_level; wxSpinCtrl* m_scale; QrPreview* m_preview; wxStaticText* m_status; wxBitmap m_bitmap;
};
}

wxString QrCodeToolModule::GetId() const { return wxS("qrcode-encoder"); }
wxString QrCodeToolModule::GetName() const { return ITOOL_TR("QR Code encoder"); }
wxString QrCodeToolModule::GetDescription() const { return ITOOL_TR("Encode UTF-8 text as a QR Code image"); }
wxWindow* QrCodeToolModule::CreatePanel(wxWindow* parent) { return new QrCodePanel(parent); }
