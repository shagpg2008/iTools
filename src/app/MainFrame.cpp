#include "app/MainFrame.h"

#include "core/Localization.h"

#include <wx/app.h>
#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/imaglist.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/srchctrl.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statusbr.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/spinctrl.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include "core/AppPaths.h"
#include "core/IStatefulPanel.h"
#include "core/UiState.h"
#include "version.h"
#include "tools/ascii/AsciiConverterModule.h"
#include "tools/base64/Base64ToolModule.h"
#include "tools/barcode/BarcodeToolModule.h"
#include "tools/bitset64/Bitset64ToolModule.h"
#include "tools/crc/CrcToolModule.h"
#include "tools/commtest/CommTestToolModule.h"
#include "tools/commserver/CommServerToolModule.h"
#include "tools/crypto/CryptoToolModule.h"
#include "tools/hash/HashToolModule.h"
#include "tools/linecount/LineCountToolModule.h"
#include "tools/protocol/ProtocolToolModule.h"
#include "tools/qrcode/QrCodeToolModule.h"
#include "tools/replacer/ReplacerToolModule.h"
#include "tools/sync/DirectorySyncModule.h"
#include "tools/time/TimeToolModule.h"
#include "tools/zeropad/ZeroPadToolModule.h"

namespace
{
const int kNavigationWidth = 180;
const int kToolIconIndex = 0;
const int kLanguageBase = wxID_HIGHEST + 310;

#ifndef __WXMSW__
wxString FindPortableIconPath()
{
    const wxString resourcesIcon = wxFileName(
        wxStandardPaths::Get().GetResourcesDir(), wxS("itool.png")).GetFullPath();
    if (wxFileName::FileExists(resourcesIcon)) return resourcesIcon;

    const wxString executableIcon = wxFileName(
        wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(),
        wxS("itool.png")).GetFullPath();
    if (wxFileName::FileExists(executableIcon)) return executableIcon;

#ifdef ITOOL_INSTALLED_ICON_PATH
    const wxString installedIcon = wxString::FromUTF8(ITOOL_INSTALLED_ICON_PATH);
    if (wxFileName::FileExists(installedIcon)) return installedIcon;
#endif
    return wxEmptyString;
}

wxBitmap LoadPortableIconBitmap(const wxSize& size)
{
    const wxString path = FindPortableIconPath();
    wxImage image;
    if (path.empty() || !image.LoadFile(path, wxBITMAP_TYPE_PNG)) return wxNullBitmap;
    if (image.GetSize() != size)
        image.Rescale(size.x, size.y, wxIMAGE_QUALITY_HIGH);
    return wxBitmap(image);
}
#endif

wxString DetailedFeatureDescription(const IToolModule& module)
{
    if (localization::EffectiveLanguage(localization::LoadConfiguredLanguage()) == wxS("en"))
        return module.GetDescription();
    const wxString id = module.GetId();
    if (id == wxS("crc-calculator"))
        return ITOOL_TR("Input: Hex bytes, UTF-8 text, files (large files use chunked memory mapping).") + wxS(" ")
               + ITOOL_TR("Preset algorithms (18 types): CRC8/SMBUS; CRC16/MODBUS, CCITT_FALSE, XMODEM, KERMIT, USB; CRC24/LTEA, LTEB, BLE; CRC32, CRC32C, CRC32/MPEG2, POSIX, AUTOSAR, BZIP2; CRC64/ECMA, XZ, WE.") + wxS("\n")
               + ITOOL_TR("Custom algorithm: Width, Init, Poly, XorOut, RefIn, RefOut can be set, and multiple algorithm calculations can be selected at the same time.");
    if (id == wxS("hash"))
        return ITOOL_TR("Input: Hex bytes, UTF-8 text, files (supports drag-and-drop and chunked memory mapping).") + wxS(" ")
               + ITOOL_TR("Algorithms (22 types): MD5; SHA1; SHA2-224/256/384/512; SHA3-224/256/384/512; Keccak-224/256/384/512; RIPEMD-128/160/256/320; BLAKE2s-256; BLAKE2b-256/512; SM3. Multiple algorithms can be calculated at once.");
    if (id == wxS("crypto"))
        return ITOOL_TR("Support AES, SM4, DES/3DES, Blowfish, Twofish, CAST5, IDEA, Serpent, TEA/XTEA/XXTEA,") + wxS(" ")
               + ITOOL_TR("RC4/5/6, ChaCha20, Salsa20, Camellia, SEED and RSA; supports GCM, CBC, CTR, ECB,") + wxS(" ")
               + ITOOL_TR("OAEP and PKCS#1 v1.5; input and output can be independently selected as UTF-8, Hex, Base64 or file;") + wxS(" ")
               + ITOOL_TR("CBC/ECB optional PKCS, Zero, ISO/IEC 7816-4 or no padding; supports secure random key, IV/Nonce and RSA key pair generation.");
    if (id == wxS("base64"))
        return ITOOL_TR("Base64 encoding and decoding; input and output can independently select UTF-8 text or files;") + wxS(" ") + ITOOL_TR("Supports binary files, progress display, and cancellation operations; non-UTF-8 decoding results can be saved as files.");
    if (id == wxS("bitset64"))
        return ITOOL_TR("Enter a 64-bit unsigned integer in hexadecimal, decimal or binary; display and click to edit bits 0~63;") + wxS(" ") + ITOOL_TR("Each hexadecimal value and bit status are updated together, supporting copying the results.");
    if (id == wxS("ascii-converter"))
        return module.GetDescription();
    if (id == wxS("replacer"))
        return ITOOL_TR("Select a single file or directory and batch replace file names and text content with advanced regular expressions; supports drag and drop;") + wxS(" ") + ITOOL_TR("Text encoding is detected according to GB18030-2022, UTF-8, and local code page. The maximum single file size is 256 MiB.");
    if (id == wxS("directory-sync"))
        return ITOOL_TR("Compare two folders and their subdirectories; generate synchronized previews based on modification time; copy newer files and single-sided files in both directions;") + wxS(" ") + ITOOL_TR("Requires confirmation before execution and displays the number of files to be synchronized and copied.");
    if (id == wxS("line-count"))
        return ITOOL_TR("Count a single file or directory, you can choose whether to recurse and customize the extension; count total lines, code, comments and blank lines respectively;") + wxS(" ") + ITOOL_TR("Recognizes C/C++, Java, C#, JavaScript/TypeScript, Python, Go, Rust, PHP, Ruby, Shell, SQL,") + wxS(" ") + ITOOL_TR("Commonly used files such as HTML/XML, CSS, Vue, CMake and their comment styles; the maximum size of a single file is 512 MiB.");
    if (id == wxS("zero-pad-filenames"))
        return ITOOL_TR("Scan the numeric numbers at the beginning of the file names in the directory and automatically add leading zeros according to the maximum number width; support for including subdirectories;") + wxS(" ") + ITOOL_TR("Used to make dictionary order consistent with natural numbering order and to check for conflicts before renaming.");
    if (id == wxS("communication-test"))
        return ITOOL_TR("Communication client: Windows, Linux, and macOS all support serial ports, and support TCP, UDP, WebSocket, and MQTT 3.1.1; request/response data can be optionally Hex or ASCII;") + wxS(" ") + ITOOL_TR("Execute multiple sets of tests in configured order, supporting timeouts, loops, result statistics and millisecond timestamp logs.") + wxS(" ") + ITOOL_TR("Serial port: preset baud rate 1200~921600, and can manually input 1~4000000; 5/6/7/8 data bits;") + wxS(" ") + ITOOL_TR("Windows supports 1/1.5/2 stop bits, all checksums, and flow control combinations; the specific combinations of Linux/macOS are limited by the system driver.");
    if (id == wxS("communication-server"))
        return ITOOL_TR("Communication server: Windows, Linux, and macOS can all monitor the serial port and support TCP, UDP, WebSocket, and MQTT 3.1.1; receiving frames can be Hex or ASCII;") + wxS(" ") + ITOOL_TR("Match complete frames out of order according to multiple configurations and send corresponding responses, checking for duplicate frames and prefix ambiguities;") + wxS(" ") + ITOOL_TR("Interface and log recording millisecond timestamp. The serial port preset baud rate is up to 921600, and a specific baud rate can also be entered manually.");
    if (id == wxS("protocol-parser"))
        return ITOOL_TR("Read C struct/typedef struct definition and #define array length, and parse Hex data stream or BIN file;") + wxS(" ") + ITOOL_TR("Supports nested structures, basic integers, float, double, bool, characters and fixed-length arrays;") + wxS(" ") + ITOOL_TR("Supports 1/2/4/8 byte packing, little endian/big endian, multiple consecutive records and tail remaining byte prompts.");
    if (id == wxS("unix-utc-time"))
        return ITOOL_TR("Bidirectional conversion between 64-bit Unix timestamp and UTC date and time; timestamp unit supports seconds and milliseconds;") + wxS(" ") + ITOOL_TR("Supports getting current time, copy timestamp and UTC text, and checking value range and date format.");
    if (id == wxS("barcode-encoder"))
        return ITOOL_TR("Encode text in the range of ASCII 32 to 126 and up to 200 characters into Code 128-B;") + wxS(" ") + ITOOL_TR("Automatically calculate checksum, adjust zoom and copy image or save as PNG.");
    if (id == wxS("qrcode-encoder"))
        return ITOOL_TR("Encode UTF-8 text into QR Code; automatically select the QR code version; support L (about 7%), M (about 15%),") + wxS(" ") + ITOOL_TR("Four error correction levels: Q (approx. 25%), H (approx. 30%); adjust zoom, copy image, or save as PNG.");
    return module.GetDescription();
}

void PolishPage(wxWindow* window)
{
    if (dynamic_cast<wxPanel*>(window))
        window->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    const wxWindowList& children = window->GetChildren();
    for (wxWindowList::const_iterator it = children.begin(); it != children.end(); ++it)
    {
        wxWindow* child = *it;
        wxSize minimum = child->GetMinSize();
        if (wxButton* button = dynamic_cast<wxButton*>(child))
        {
            if (!(button->GetWindowStyleFlag() & wxBU_EXACTFIT) && minimum.y < 30)
                button->SetMinSize(wxSize(minimum.x, 30));
        }
        else if (dynamic_cast<wxChoice*>(child) || dynamic_cast<wxComboBox*>(child) || dynamic_cast<wxSpinCtrl*>(child))
        {
            if (minimum.y < 28) child->SetMinSize(wxSize(minimum.x, 28));
        }
        else if (wxTextCtrl* text = dynamic_cast<wxTextCtrl*>(child))
        {
            if (!(text->GetWindowStyleFlag() & wxTE_MULTILINE) && minimum.y < 28)
                text->SetMinSize(wxSize(minimum.x, 28));
        }
        if (dynamic_cast<wxPanel*>(child)) PolishPage(child);
    }
}

}

MainFrame::MainFrame()
    : wxFrame(NULL, wxID_ANY, "iTool", wxDefaultPosition, wxSize(1000, 648)),
      m_toolTree(NULL),
      m_toolFilter(NULL),
      m_rebuildingToolTree(false),
      m_stateSaved(false),
      m_pages(NULL),
      m_language(localization::LoadConfiguredLanguage())
{
#ifdef __WXMSW__
    // Set explicitly so the title bar, taskbar and Alt-Tab UI all use the
    // embedded application icon instead of relying on shell heuristics.
    SetIcon(wxICON(IDI_ITOOL_APP_ICON));
#else
    const wxBitmap appIconBitmap = LoadPortableIconBitmap(wxSize(64, 64));
    if (appIconBitmap.IsOk())
    {
        // wxGTK doesn't provide wxIcon(const wxBitmap&). CopyFromBitmap() is
        // supported by the generic Unix implementation as well as wxOSX.
        wxIcon appIcon;
        appIcon.CopyFromBitmap(appIconBitmap);
        SetIcon(appIcon);
    }
#endif

    RegisterTools();
    BuildInterface();
    Centre();
}

MainFrame::~MainFrame()
{
    if (!m_stateSaved) SaveUiState();
}

void MainFrame::RegisterTools()
{
    m_modules.push_back(std::unique_ptr<IToolModule>(
        new Base64ToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new Bitset64ToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new AsciiConverterModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new CrcToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new HashToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new CryptoToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new ReplacerToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new DirectorySyncModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new LineCountToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new ZeroPadToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new CommTestToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new CommServerToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new ProtocolToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new TimeToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new BarcodeToolModule));

    m_modules.push_back(std::unique_ptr<IToolModule>(
        new QrCodeToolModule));
}

void MainFrame::BuildInterface()
{
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(wxID_EXIT, ITOOL_TR("Exit (&X)\tAlt-F4"));

    wxMenu* languageMenu = new wxMenu;
    languageMenu->AppendRadioItem(kLanguageBase, ITOOL_TR("Follow system"));
    const std::vector<localization::LanguageInfo>& languages = localization::SupportedLanguages();
    int checkedLanguage = kLanguageBase;
    for (size_t i = 0; i < languages.size(); ++i)
    {
        const int id = kLanguageBase + static_cast<int>(i) + 1;
        languageMenu->AppendRadioItem(id, languages[i].displayName);
        if (m_language == languages[i].code) checkedLanguage = id;
    }
    languageMenu->Check(checkedLanguage, true);

    wxMenu* settingsMenu = new wxMenu;
    settingsMenu->AppendSubMenu(languageMenu, ITOOL_TR("Language"));

    wxMenu* helpMenu = new wxMenu;
    helpMenu->Append(wxID_ABOUT, ITOOL_TR("About (&A)"));

    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append(fileMenu, ITOOL_TR("File (&F)"));
    menuBar->Append(settingsMenu, ITOOL_TR("Settings (&S)"));
    menuBar->Append(helpMenu, ITOOL_TR("Help (&H)"));
    SetMenuBar(menuBar);

    wxSplitterWindow* splitter = new wxSplitterWindow(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxSP_LIVE_UPDATE | wxSP_3D);
    splitter->SetMinimumPaneSize(90);

    wxPanel* navigation = new wxPanel(splitter);
    wxBoxSizer* navigationSizer = new wxBoxSizer(wxVERTICAL);
    m_toolFilter = new wxSearchCtrl(navigation, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                    wxTE_PROCESS_ENTER);
    m_toolFilter->SetDescriptiveText(ITOOL_TR("Search tools..."));
    m_toolFilter->ShowSearchButton(true); m_toolFilter->ShowCancelButton(true);
    m_toolTree = new wxTreeCtrl(navigation, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT);
    wxImageList* toolImages = new wxImageList(16, 16, true);
    bool toolImageAdded = false;
#ifdef __WXMSW__
    // Add the native icon handle directly. Converting it to an alpha bitmap
    // makes ImageList_Add() fail on the Windows XP common-controls version.
    const wxIcon toolIcon(wxS("IDI_ITOOL_APP_ICON"), wxBITMAP_TYPE_ICO_RESOURCE, 16, 16);
    if (toolIcon.IsOk())
        toolImageAdded = toolImages->Add(toolIcon) != wxNOT_FOUND;
#else
    const wxBitmap toolBitmap = LoadPortableIconBitmap(wxSize(16, 16));
    toolImageAdded = toolBitmap.IsOk() && toolImages->Add(toolBitmap) != wxNOT_FOUND;
#endif
    if (toolImageAdded)
        m_toolTree->AssignImageList(toolImages);
    else
        delete toolImages;
    navigationSizer->Add(m_toolFilter, 0, wxEXPAND | wxALL, 4);
    navigationSizer->Add(m_toolTree, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
    navigation->SetSizer(navigationSizer);
    wxPanel* content = new wxPanel(splitter);
    wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
    m_pages = new wxSimplebook(content, wxID_ANY);
    contentSizer->Add(m_pages, 1, wxEXPAND | wxALL, 6);
    content->SetSizer(contentSizer);
    m_toolRoot = m_toolTree->AddRoot(ITOOL_TR("Tools"));

    for (std::vector<std::unique_ptr<IToolModule> >::iterator it = m_modules.begin();
         it != m_modules.end(); ++it)
    {
        wxWindow* page = (*it)->CreatePanel(m_pages);
        PolishPage(page);
        m_pages->AddPage(page, (*it)->GetName());
    }
    RebuildToolTree();

    splitter->SplitVertically(navigation, content, kNavigationWidth);

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
    root->Add(splitter, 1, wxEXPAND);
    SetSizer(root);

    CreateStatusBar();
    SetStatusText(ITOOL_TR("Ready"));

    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MainFrame::OnLanguage, this, kLanguageBase,
         kLanguageBase + static_cast<int>(languages.size()));
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    m_toolTree->Bind(wxEVT_TREE_SEL_CHANGED, &MainFrame::OnToolSelected, this);
    m_toolFilter->Bind(wxEVT_TEXT, &MainFrame::OnToolFilter, this);
    m_toolFilter->Bind(wxEVT_SEARCH_CANCEL, &MainFrame::OnToolFilterClear, this);

    if (!m_modules.empty())
    {
        m_toolTree->SelectItem(m_toolItems[0]);
        m_pages->SetSelection(0);
    }
    LoadUiState();
}

void MainFrame::OnLanguage(wxCommandEvent& event)
{
    wxString selected = wxS("system");
    const int index = event.GetId() - kLanguageBase - 1;
    const std::vector<localization::LanguageInfo>& languages = localization::SupportedLanguages();
    if (index >= 0 && index < static_cast<int>(languages.size()))
        selected = languages[static_cast<size_t>(index)].code;
    if (selected == m_language) return;
    if (!localization::SaveConfiguredLanguage(selected))
    {
        wxMessageBox(ITOOL_TR("The language setting could not be saved."),
                     ITOOL_TR("Language"), wxOK | wxICON_ERROR, this);
        return;
    }
    m_language = selected;

    // Tool panels create most labels in their constructors. Persist their
    // current values, replace the active catalog, and rebuild the frame so
    // every visible string changes in the same event-loop iteration.
    SaveUiState();
    m_stateSaved = true;
    const wxRect previousRect = GetRect();
    const bool wasMaximized = IsMaximized();
    localization::Initialize(selected);

    MainFrame* replacement = new MainFrame;
    if (!wasMaximized)
        replacement->SetSize(previousRect);
    replacement->Show(true);
    if (wasMaximized)
        replacement->Maximize(true);
    wxTheApp->SetTopWindow(replacement);
    Close(true);
}

void MainFrame::LoadUiState()
{
    const wxString path = apppaths::ConfigurationFile();
    if (!wxFileName::FileExists(path)) return;
    wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    for (size_t i = 0; i < m_modules.size(); ++i)
    {
        wxWindow* page = m_pages->GetPage(i); const wxString prefix = wxS("/tools/") + m_modules[i]->GetId();
        uistate::Load(page, config, prefix);
        if (IStatefulPanel* custom = dynamic_cast<IStatefulPanel*>(page)) custom->LoadCustomState(config, prefix);
    }
    wxString filter;
    if (config.Read(wxS("/window/toolFilter"), &filter)) { m_toolFilter->ChangeValue(filter); RebuildToolTree(); }
    long selected = 0; wxString selectedId;
    if (config.Read(wxS("/window/selectedToolId"), &selectedId))
    {
        for (size_t i = 0; i < m_modules.size(); ++i)
            if (m_modules[i]->GetId() == selectedId) { selected = static_cast<long>(i); break; }
    }
    else
    {
        long legacy = 0; config.Read(wxS("/window/selectedTool"), &legacy, 0L);
        static const int legacyToCurrent[] = { 2, 3, 4, 5, 6, 0, 7, 1, 8, 9, 10, 11, 12, 13, 14 };
        if (legacy >= 0 && legacy < static_cast<long>(sizeof(legacyToCurrent) / sizeof(legacyToCurrent[0])))
            selected = legacyToCurrent[legacy];
    }
    if (selected >= 0 && selected < static_cast<long>(m_modules.size()))
    {
        m_pages->SetSelection(static_cast<size_t>(selected));
        if (m_toolItems[static_cast<size_t>(selected)].IsOk())
            m_toolTree->SelectItem(m_toolItems[static_cast<size_t>(selected)]);
        SetStatusText(m_modules[static_cast<size_t>(selected)]->GetDescription());
    }
}

void MainFrame::SaveUiState()
{
    if (!m_pages || !m_toolTree) return;
    if (!apppaths::EnsureUserDataDirectory()) return;
    const wxString path = apppaths::ConfigurationFile();
    wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    const int selectedPage = m_pages->GetSelection();
    // Update the existing configuration in place. wxFileConfig::DeleteAll()
    // physically removes iTool.ini first, which can block for several seconds
    // behind filesystem/antivirus filters on Windows XP.
    config.Write(wxS("/window/selectedTool"), static_cast<long>(selectedPage));
    if (selectedPage >= 0 && static_cast<size_t>(selectedPage) < m_modules.size())
        config.Write(wxS("/window/selectedToolId"), m_modules[static_cast<size_t>(selectedPage)]->GetId());
    config.Write(wxS("/window/toolFilter"), m_toolFilter->GetValue());
    for (size_t i = 0; i < m_modules.size(); ++i)
    {
        wxWindow* page = m_pages->GetPage(i); const wxString prefix = wxS("/tools/") + m_modules[i]->GetId();
        uistate::Save(page, config, prefix);
        if (const IStatefulPanel* custom = dynamic_cast<const IStatefulPanel*>(page)) custom->SaveCustomState(config, prefix);
    }
    config.Flush();
}

void MainFrame::RebuildToolTree()
{
    if (!m_toolTree || !m_toolRoot.IsOk()) return;
    const size_t current = m_pages ? m_pages->GetSelection() : 0;
    wxString keyword = m_toolFilter ? m_toolFilter->GetValue() : wxString();
    keyword.Trim(true).Trim(false); keyword.MakeLower();
    const bool showAllTools = keyword.empty();
    m_rebuildingToolTree = true;
    m_toolTree->Freeze();
    m_toolTree->UnselectAll();
    m_toolTree->DeleteChildren(m_toolRoot);
    m_toolItems.assign(m_modules.size(), wxTreeItemId());
    for (size_t i = 0; i < m_modules.size(); ++i)
    {
        wxString searchable = m_modules[i]->GetName() + wxS(" ") + m_modules[i]->GetDescription();
        searchable.MakeLower();
        if (!showAllTools && searchable.Find(keyword) == wxNOT_FOUND) continue;
        m_toolItems[i] = m_toolTree->AppendItem(m_toolRoot, m_modules[i]->GetName(),
                                                kToolIconIndex, kToolIconIndex);
    }
    m_toolTree->Expand(m_toolRoot);
    if (current < m_toolItems.size() && m_toolItems[current].IsOk())
        m_toolTree->SelectItem(m_toolItems[current]);
    m_toolTree->Thaw();
    m_rebuildingToolTree = false;
}

void MainFrame::OnToolFilter(wxCommandEvent&)
{
    RebuildToolTree();
}

void MainFrame::OnToolFilterClear(wxCommandEvent&)
{
    m_toolFilter->ChangeValue(wxEmptyString);
    RebuildToolTree();
}

void MainFrame::OnToolSelected(wxTreeEvent& event)
{
    if (m_rebuildingToolTree) return;
    const wxTreeItemId selected = event.GetItem();
    for (size_t selection = 0; selection < m_toolItems.size(); ++selection)
    {
        if (m_toolItems[selection] != selected) continue;
        m_pages->SetSelection(selection);
        SetStatusText(m_modules[selection]->GetDescription());
        break;
    }
}

void MainFrame::OnExit(wxCommandEvent&)
{
    Close(true);
}

void MainFrame::OnClose(wxCloseEvent&)
{
    if (!m_stateSaved)
    {
        SaveUiState();
        m_stateSaved = true;
    }
    // Destroying a native XP TreeCtrl changes/removes its selection and can
    // synchronously emit wxEVT_TREE_SEL_CHANGED while sibling pages are
    // already being destroyed. Disconnect navigation handlers first so they
    // can never dereference m_pages during teardown.
    m_rebuildingToolTree = true;
    if (m_toolTree)
        m_toolTree->Unbind(wxEVT_TREE_SEL_CHANGED, &MainFrame::OnToolSelected, this);
    if (m_toolFilter)
    {
        m_toolFilter->Unbind(wxEVT_TEXT, &MainFrame::OnToolFilter, this);
        m_toolFilter->Unbind(wxEVT_SEARCH_CANCEL, &MainFrame::OnToolFilterClear, this);
    }
    if (m_toolTree)
    {
        // wxTreeCtrl::~wxTreeCtrl() calls TreeView_DeleteAllItems() itself.
        // Do it while the control and frame are still fully alive, with all
        // application callbacks disconnected, to avoid XP's slow teardown
        // notification path. Detach/free the image list before HWND cleanup.
        m_toolTree->Freeze();
        m_toolTree->Show(false);
        m_toolTree->DeleteAllItems();
        m_toolTree->AssignImageList(NULL);
        m_toolRoot = wxTreeItemId();
        m_toolItems.assign(m_modules.size(), wxTreeItemId());
    }
    // Destroy explicitly instead of hiding and relying on the platform's
    // default close processing. This also guarantees child panels and their
    // worker objects are gone before IToolApp::OnExit() shuts Winsock down.
    Destroy();
}

void MainFrame::OnAbout(wxCommandEvent&)
{
    wxDialog dialog(this, wxID_ANY, ITOOL_TR("About iTool"),
                    wxDefaultPosition, wxSize(700, 560),
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

    const wxString versionText = wxString::Format(
        ITOOL_TR("iTool %s\nBuilt: %s %s    wxWidgets %s    %s"),
        wxString::FromUTF8(ITOOL_VERSION_STRING).c_str(),
        wxString::FromUTF8(ITOOL_BUILD_DATE).c_str(),
        wxString::FromUTF8(ITOOL_BUILD_TIME).c_str(),
        wxString::FromUTF8(wxVERSION_NUM_DOT_STRING).c_str(),
        wxString::FromUTF8(ITOOL_PLATFORM_DESCRIPTION).c_str());
    rootSizer->Add(new wxStaticText(&dialog, wxID_ANY, versionText),
                   0, wxEXPAND | wxALL, 12);

    wxString featureText = ITOOL_TR("Features\n\n");
    for (size_t i = 0; i < m_modules.size(); ++i)
    {
        featureText += wxString::Format(wxS("%u. %s\n   %s"),
            static_cast<unsigned int>(i + 1),
            m_modules[i]->GetName().c_str(),
            DetailedFeatureDescription(*m_modules[i]).c_str());
        featureText += wxS("\n\n");
    }

    wxTextCtrl* features = new wxTextCtrl(
        &dialog, wxID_ANY, featureText, wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    features->SetInsertionPoint(0);
    rootSizer->Add(features, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxSizer* buttons = dialog.CreateSeparatedButtonSizer(wxOK);
    if (buttons) rootSizer->Add(buttons, 0, wxEXPAND | wxALL, 12);
    dialog.SetSizer(rootSizer);
    dialog.SetMinSize(wxSize(560, 420));
    dialog.ShowModal();
}
