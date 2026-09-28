#pragma once

#include "core/IToolModule.h"

class TimeToolModule : public IToolModule
{
public:
    wxString GetId() const;
    wxString GetName() const;
    wxString GetDescription() const;
    wxWindow* CreatePanel(wxWindow* parent);
};
