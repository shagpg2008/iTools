#pragma once

#include <wx/listctrl.h>
#include <map>
#include <utility>

// Shared log selection and clipboard behavior for client and server panels.
class CommunicationLog : public wxListCtrl
{
public:
    explicit CommunicationLog(wxWindow* parent);
    void SetRecordText(long row, int column, const wxString& preview,
                       const wxString& fullText);
    void ClearRecords();
    void CopyRecords();

private:
    // Clipboard data must not be read back from truncated display strings.
    std::map<std::pair<long, int>, wxString> m_fullText;
};
