#include <wx/app.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/socket.h>

#include "app/MainFrame.h"
#include "core/Localization.h"

#ifdef __WXMSW__
#include <windows.h>

namespace
{
LONG WINAPI WriteCrashLog(EXCEPTION_POINTERS* exceptionInfo)
{
    wchar_t path[MAX_PATH] = { 0 };
    const DWORD pathLength = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (pathLength > 0 && pathLength < MAX_PATH)
    {
        wchar_t* separator = path + pathLength;
        while (separator > path && separator[-1] != L'\\' && separator[-1] != L'/')
            --separator;
        const wchar_t fileName[] = L"iTool-crash.log";
        if (static_cast<size_t>(separator - path) + sizeof(fileName) / sizeof(fileName[0]) <= MAX_PATH)
        {
            lstrcpyW(separator, fileName);
            HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE)
            {
                const ULONG_PTR address = reinterpret_cast<ULONG_PTR>(
                    exceptionInfo && exceptionInfo->ExceptionRecord
                        ? exceptionInfo->ExceptionRecord->ExceptionAddress : NULL);
                const ULONG_PTR imageBase = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(NULL));
                const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
                    ? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
                wchar_t message[512] = { 0 };
                wsprintfW(message,
                          L"ExceptionCode=0x%08lX\r\nExceptionAddress=0x%08lX\r\n"
                          L"ImageBase=0x%08lX\r\nImageOffset=0x%08lX\r\nThreadId=%lu\r\n",
                          code, static_cast<DWORD>(address), static_cast<DWORD>(imageBase),
                          static_cast<DWORD>(address - imageBase), GetCurrentThreadId());
                DWORD written = 0;
                WriteFile(file, message, static_cast<DWORD>(lstrlenW(message) * sizeof(wchar_t)),
                          &written, NULL);
                CloseHandle(file);
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}
#endif

class IToolApp : public wxApp
{
public:
    IToolApp() : m_socketsInitialized(false) {}
    bool OnInit();
    int OnExit();

private:
    bool m_socketsInitialized;
};

wxIMPLEMENT_APP(IToolApp);

bool IToolApp::OnInit()
{
#ifdef __WXMSW__
    // Keep this installed through process teardown as XP's error dialog does
    // not provide a useful stack trace for statically linked applications.
    SetUnhandledExceptionFilter(&WriteCrashLog);
#endif
    if (!wxApp::OnInit())
    {
        return false;
    }

    SetAppName("iTool");
    localization::Initialize(localization::LoadConfiguredLanguage());
    wxInitAllImageHandlers();
    if (!wxSocketBase::Initialize())
    {
        wxMessageBox(ITOOL_TR("Unable to initialize the network socket subsystem; network tools are unavailable."),
                     wxS("iTool"), wxOK | wxICON_ERROR);
        return false;
    }
    m_socketsInitialized = true;
    MainFrame* frame = new MainFrame;
    frame->Show(true);
    return true;
}

int IToolApp::OnExit()
{
    // wxSocketBase::Initialize() must be paired explicitly. In particular,
    // leave Winsock before static wxWidgets objects are torn down on XP.
    if (m_socketsInitialized)
    {
        wxSocketBase::Shutdown();
        m_socketsInitialized = false;
    }
    const int result = wxApp::OnExit();
    localization::Shutdown();
    return result;
}
