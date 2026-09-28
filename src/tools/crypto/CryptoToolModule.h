#pragma once

#include "core/IToolModule.h"

class CryptoToolModule : public IToolModule
{
public:
    wxString GetId() const wxOVERRIDE;
    wxString GetName() const wxOVERRIDE;
    wxString GetDescription() const wxOVERRIDE;
    wxWindow* CreatePanel(wxWindow* parent) wxOVERRIDE;
};
