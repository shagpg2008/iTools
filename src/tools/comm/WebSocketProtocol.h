#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <wx/string.h>

class wxSocketBase;

namespace websocket
{
std::string MakeAcceptKey(const std::string& clientKey);

bool ClientHandshake(wxSocketBase& socket, const wxString& host, unsigned short port,
                     const wxString& path, unsigned long timeoutMs,
                     const std::atomic<bool>& stopping, wxString& error);
bool ServerHandshake(wxSocketBase& socket, const wxString& requiredPath,
                     unsigned long timeoutMs, const std::atomic<bool>& stopping,
                     wxString& error);

bool SendBinary(wxSocketBase& socket, const std::vector<unsigned char>& data,
                bool clientMask, wxString& error);
bool SendText(wxSocketBase& socket, const std::vector<unsigned char>& data,
              bool clientMask, wxString& error);
bool ReceiveMessage(wxSocketBase& socket, unsigned long timeoutMs,
                    const std::atomic<bool>& stopping, bool expectClientMask,
                    std::vector<unsigned char>& data, wxString& error);
}
