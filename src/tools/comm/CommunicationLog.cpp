#include "tools/comm/CommunicationLog.h"

#include "core/Localization.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/menu.h>
#include <wx/utils.h>

CommunicationLog::CommunicationLog(wxWindow* parent)
    : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_HRULES | wxLC_VRULES)
{
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.CmdDown() && event.GetKeyCode() == 'C')
            CopyRecords();
        else if (event.CmdDown() && event.GetKeyCode() == 'A')
            SetItemState(-1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        else
            event.Skip();
    });
    Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent&) {
        wxMenu menu;
        menu.Append(wxID_COPY, ITOOL_TR("Copy"));
        menu.Enable(wxID_COPY, GetItemCount() > 0);
        menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopyRecords(); }, wxID_COPY);
        PopupMenu(&menu);
    });
}

void CommunicationLog::SetRecordText(long row, int column, const wxString& preview,
                                     const wxString& fullText)
{
    if (SetItem(row, column, preview))
        m_fullText[std::make_pair(row, column)] = fullText;
}

void CommunicationLog::ClearRecords()
{
    if (DeleteAllItems()) m_fullText.clear();
}

void CommunicationLog::CopyRecords()
{
    if (GetItemCount() == 0) return;

    const bool selectedOnly = GetSelectedItemCount() > 0;
    wxString text;
    for (long row = 0; row < GetItemCount(); ++row)
    {
        if (selectedOnly && !GetItemState(row, wxLIST_STATE_SELECTED)) continue;
        if (!text.empty()) text += wxS("\n");
        for (int column = 0; column < GetColumnCount(); ++column)
        {
            if (column) text += wxS("\t");
            const auto full = m_fullText.find(std::make_pair(row, column));
            text += full != m_fullText.end() ? full->second : GetItemText(row, column);
        }
    }

    if (!wxTheClipboard->Open())
    {
        wxBell();
        return;
    }
    if (wxTheClipboard->SetData(new wxTextDataObject(text)))
        wxTheClipboard->Flush();
    else
        wxBell();
    wxTheClipboard->Close();
}
