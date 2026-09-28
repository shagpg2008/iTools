#pragma once

#include <wx/string.h>

class wxConfigBase;

class IStatefulPanel
{
public:
    virtual ~IStatefulPanel() {}
    virtual void LoadCustomState(wxConfigBase& config, const wxString& prefix) = 0;
    virtual void SaveCustomState(wxConfigBase& config, const wxString& prefix) const = 0;
};
