#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <wx/string.h>

class wxSocketBase;

namespace mqtt
{
std::vector<unsigned char> EncodeRemainingLength(size_t length);

bool ClientConnect(wxSocketBase& socket, const std::string& clientId,
                   unsigned long timeoutMs, const std::atomic<bool>& stopping, wxString& error);
bool ClientSubscribe(wxSocketBase& socket, const std::string& topic,
                     unsigned long timeoutMs, const std::atomic<bool>& stopping, wxString& error);
bool Publish(wxSocketBase& socket, const std::string& topic,
             const std::vector<unsigned char>& payload, wxString& error);
bool ReceivePublish(wxSocketBase& socket, unsigned long timeoutMs,
                    const std::atomic<bool>& stopping, std::string& topic,
                    std::vector<unsigned char>& payload, wxString& error);

bool ServerAcceptConnect(wxSocketBase& socket, unsigned long timeoutMs,
                         const std::atomic<bool>& stopping, wxString& error);
}
