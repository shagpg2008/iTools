#include "core/UiState.h"

#include <vector>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/config.h>
#include <wx/grid.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/window.h>

namespace uistate
{
namespace
{
enum Kind { TextKind, ChoiceKind, ComboKind, CheckKind, RadioKind, SpinKind, GridKind };
struct Item { wxWindow* window; Kind kind; size_t index; };
struct Counters { Counters() : text(0), choice(0), combo(0), check(0), radio(0), spin(0), grid(0) {} size_t text, choice, combo, check, radio, spin, grid; };

void Collect(wxWindow* root, Counters& counters, std::vector<Item>& items)
{
    const wxWindowList& children = root->GetChildren();
    for (wxWindowList::const_iterator iterator = children.begin(); iterator != children.end(); ++iterator)
    {
        wxWindow* child = *iterator;
        if (dynamic_cast<wxGrid*>(child)) items.push_back(Item{ child, GridKind, counters.grid++ });
        else if (dynamic_cast<wxSpinCtrl*>(child)) items.push_back(Item{ child, SpinKind, counters.spin++ });
        else if (dynamic_cast<wxRadioButton*>(child)) items.push_back(Item{ child, RadioKind, counters.radio++ });
        else if (dynamic_cast<wxCheckBox*>(child)) items.push_back(Item{ child, CheckKind, counters.check++ });
        else if (dynamic_cast<wxComboBox*>(child)) items.push_back(Item{ child, ComboKind, counters.combo++ });
        else if (dynamic_cast<wxChoice*>(child)) items.push_back(Item{ child, ChoiceKind, counters.choice++ });
        else if (wxTextCtrl* text = dynamic_cast<wxTextCtrl*>(child))
        {
            if (!(text->GetWindowStyleFlag() & wxTE_READONLY)) items.push_back(Item{ child, TextKind, counters.text++ });
        }
        else Collect(child, counters, items);
    }
}

wxString Key(const wxString& prefix, Kind kind, size_t index)
{
    static const wxChar* names[] = { wxS("text"), wxS("choice"), wxS("combo"), wxS("check"), wxS("radio"), wxS("spin"), wxS("grid") };
    return prefix + wxS("/") + names[kind] + wxString::Format(wxS("/%lu"), static_cast<unsigned long>(index));
}

void Dispatch(wxWindow* window, wxEventType type)
{
    wxCommandEvent event(type, window->GetId()); event.SetEventObject(window); window->ProcessWindowEvent(event);
}
}

void Save(wxWindow* root, wxConfigBase& config, const wxString& prefix)
{
    Counters counters; std::vector<Item> items; Collect(root, counters, items);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item& item = items[i]; const wxString key = Key(prefix, item.kind, item.index);
        if (item.kind == TextKind) config.Write(key, static_cast<wxTextCtrl*>(item.window)->GetValue());
        // Persist language-neutral selections. Older releases stored the
        // translated label; Load() below retains a migration path for it.
        else if (item.kind == ChoiceKind)
            config.Write(key, static_cast<long>(static_cast<wxChoice*>(item.window)->GetSelection()));
        else if (item.kind == ComboKind)
        {
            wxComboBox* combo = static_cast<wxComboBox*>(item.window);
            const int selection = combo->GetSelection();
            if (selection != wxNOT_FOUND)
                config.Write(key, wxString::Format(wxS("@index:%d"), selection));
            else
                config.Write(key, wxS("@value:") + combo->GetValue());
        }
        else if (item.kind == CheckKind) config.Write(key, static_cast<wxCheckBox*>(item.window)->GetValue());
        else if (item.kind == RadioKind) config.Write(key, static_cast<wxRadioButton*>(item.window)->GetValue());
        else if (item.kind == SpinKind) config.Write(key, static_cast<long>(static_cast<wxSpinCtrl*>(item.window)->GetValue()));
        else
        {
            wxGrid* grid = static_cast<wxGrid*>(item.window);
            for (int row = 0; row < grid->GetNumberRows(); ++row)
                for (int column = 0; column < grid->GetNumberCols(); ++column)
                    if (!grid->IsReadOnly(row, column))
                        config.Write(key + wxString::Format(wxS("/%d/%d"), row, column), grid->GetCellValue(row, column));
        }
    }
}

void Load(wxWindow* root, wxConfigBase& config, const wxString& prefix)
{
    Counters counters; std::vector<Item> items; Collect(root, counters, items);
    // Restore modes first so their handlers can enable the appropriate input controls.
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item& item = items[i]; const wxString key = Key(prefix, item.kind, item.index);
        if (item.kind == ChoiceKind)
        {
            long selection = wxNOT_FOUND;
            if (config.Read(key, &selection) && selection >= 0 &&
                static_cast<unsigned long>(selection) < static_cast<wxChoice*>(item.window)->GetCount())
                static_cast<wxChoice*>(item.window)->SetSelection(static_cast<int>(selection));
            else
            {
                wxString legacyValue;
                if (config.Read(key, &legacyValue))
                {
                    const int legacySelection = static_cast<wxChoice*>(item.window)->FindString(legacyValue);
                    if (legacySelection != wxNOT_FOUND)
                        static_cast<wxChoice*>(item.window)->SetSelection(legacySelection);
                }
            }
        }
        else if (item.kind == ComboKind)
        {
            wxString value;
            // Read the former wxChoice key as a one-time migration for controls
            // converted to wxComboBox (notably the communication protocol list).
            if (config.Read(key, &value) || config.Read(Key(prefix, ChoiceKind, item.index), &value))
            {
                wxComboBox* combo = static_cast<wxComboBox*>(item.window);
                if (value.StartsWith(wxS("@index:")))
                {
                    long selection = wxNOT_FOUND;
                    if (value.Mid(7).ToLong(&selection) && selection >= 0 &&
                        static_cast<unsigned long>(selection) < combo->GetCount())
                        combo->SetSelection(static_cast<int>(selection));
                }
                else if (value.StartsWith(wxS("@value:")))
                    combo->SetValue(value.Mid(7));
                else
                {
                    const int selection = combo->FindString(value);
                    if (selection != wxNOT_FOUND) combo->SetSelection(selection);
                    else combo->SetValue(value);
                }
            }
        }
        else if (item.kind == CheckKind)
        {
            bool value = false; if (config.Read(key, &value)) static_cast<wxCheckBox*>(item.window)->SetValue(value);
        }
        else if (item.kind == RadioKind)
        {
            bool value = false; if (config.Read(key, &value)) static_cast<wxRadioButton*>(item.window)->SetValue(value);
        }
        else if (item.kind == SpinKind)
        {
            long value = 0; if (config.Read(key, &value)) static_cast<wxSpinCtrl*>(item.window)->SetValue(static_cast<int>(value));
        }
        else if (item.kind == GridKind)
        {
            wxGrid* grid = static_cast<wxGrid*>(item.window);
            for (int row = 0; row < grid->GetNumberRows(); ++row)
                for (int column = 0; column < grid->GetNumberCols(); ++column)
                    if (!grid->IsReadOnly(row, column))
                    {
                        wxString value;
                        if (config.Read(key + wxString::Format(wxS("/%d/%d"), row, column), &value)) grid->SetCellValue(row, column, value);
                    }
            grid->ForceRefresh();
        }
    }
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item& item = items[i];
        if (item.kind == ChoiceKind) Dispatch(item.window, wxEVT_CHOICE);
        else if (item.kind == ComboKind) Dispatch(item.window, wxEVT_COMBOBOX);
        else if (item.kind == RadioKind && static_cast<wxRadioButton*>(item.window)->GetValue()) Dispatch(item.window, wxEVT_RADIOBUTTON);
        else if (item.kind == CheckKind) Dispatch(item.window, wxEVT_CHECKBOX);
    }
    // Restore text after mode handlers (some handlers reformat text fields).
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item& item = items[i]; if (item.kind != TextKind) continue;
        wxString value; if (config.Read(Key(prefix, item.kind, item.index), &value)) static_cast<wxTextCtrl*>(item.window)->ChangeValue(value);
    }
    // Recompute previews and derived controls from the restored inputs.
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item& item = items[i];
        if (item.kind == SpinKind) Dispatch(item.window, wxEVT_SPINCTRL);
        else if (item.kind == TextKind && (item.window->GetWindowStyleFlag() & wxTE_PROCESS_ENTER)) Dispatch(item.window, wxEVT_TEXT_ENTER);
    }
}
}
