#pragma once

#include "core/IToolModule.h"

class Base64ToolModule : public IToolModule
{
public:
    wxString GetId() const;
    wxString GetName() const;
    wxString GetDescription() const;
    wxWindow* CreatePanel(wxWindow* parent);
};
