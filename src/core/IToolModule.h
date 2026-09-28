#pragma once

#include <wx/string.h>

class wxWindow;

class IToolModule
{
public:
    virtual ~IToolModule() {}

    virtual wxString GetId() const = 0;
    virtual wxString GetName() const = 0;
    virtual wxString GetDescription() const = 0;
    virtual wxWindow* CreatePanel(wxWindow* parent) = 0;
};

