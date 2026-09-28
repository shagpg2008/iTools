#pragma once

#include "core/IToolModule.h"

class DirectorySyncModule : public IToolModule
{
public:
    wxString GetId() const;
    wxString GetName() const;
    wxString GetDescription() const;
    wxWindow* CreatePanel(wxWindow* parent);
};
