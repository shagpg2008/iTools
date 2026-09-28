#pragma once

#include <memory>
#include <vector>

#include <wx/frame.h>
#include <wx/treectrl.h>

#include "core/IToolModule.h"

class wxSimplebook;
class wxSearchCtrl;

class MainFrame : public wxFrame
{
public:
    MainFrame();
    ~MainFrame();

private:
    void RegisterTools();
    void BuildInterface();
    void LoadUiState();
    void SaveUiState();
    void RebuildToolTree();
    void OnToolFilter(wxCommandEvent& event);
    void OnToolFilterClear(wxCommandEvent& event);
    void OnToolSelected(wxTreeEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnLanguage(wxCommandEvent& event);

    std::vector<std::unique_ptr<IToolModule> > m_modules;
    wxTreeCtrl* m_toolTree;
    wxSearchCtrl* m_toolFilter;
    wxTreeItemId m_toolRoot;
    std::vector<wxTreeItemId> m_toolItems;
    bool m_rebuildingToolTree;
    bool m_stateSaved;
    wxSimplebook* m_pages;
    wxString m_language;
};
