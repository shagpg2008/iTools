#include "tools/comm/CommunicationLog.h"
#include "tools/commserver/CommServerToolModule.h"

#include "core/Localization.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/config.h>
#include <wx/dialog.h>
#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/socket.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/thread.h>

#include "core/AppPaths.h"
#include "core/IStatefulPanel.h"
#include "tools/comm/MqttProtocol.h"
#include "tools/comm/WebSocketProtocol.h"
#ifndef __WXMSW__
#include "tools/comm/PosixSerialPort.h"
#endif

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

namespace
{
enum ServerProtocol { ServerTcp, ServerUdp, ServerWebSocket, ServerMqtt, ServerSerial };

struct ResponseMap
{
    wxString name;
    std::vector<unsigned char> receive;
    std::vector<unsigned char> send;
    bool receiveAscii;
    bool sendAscii;
    unsigned long delayMs;
    bool enabled;
};

struct ServerSettings
{
    ServerProtocol protocol;
    wxString endpoint;
    unsigned short port;
    wxString websocketPath;
    wxString mqttRequestTopic;
    wxString mqttResponseTopic;
    unsigned long baud;
    unsigned char dataBits;
    int stopBits;
    int parity;
    int flowControl;
    std::vector<ResponseMap> maps;
};

struct ServerResult
{
    wxString peer;
    wxString name;
    wxString status;
    std::vector<unsigned char> received;
    std::vector<unsigned char> sent;
    bool matched;
    bool sendOk;
};

wxDECLARE_EVENT(EVT_SERVER_RESULT, wxThreadEvent);
wxDECLARE_EVENT(EVT_SERVER_FINISHED, wxThreadEvent);
wxDEFINE_EVENT(EVT_SERVER_RESULT, wxThreadEvent);
wxDEFINE_EVENT(EVT_SERVER_FINISHED, wxThreadEvent);

wxArrayString SerialPorts()
{
    wxArrayString ports;
#ifdef __WXMSW__
    wchar_t target[512];
    for (int number = 1; number <= 256; ++number)
    {
        const wxString name = wxString::Format(wxS("COM%d"), number);
        if (QueryDosDeviceW(name.wc_str(), target, sizeof(target) / sizeof(target[0])) != 0) ports.Add(name);
    }
#else
    ports = posixserial::AvailablePorts();
#endif
    return ports;
}

bool ParseHex(const wxString& source, std::vector<unsigned char>& bytes, wxString& error, bool allowEmpty)
{
    wxString clean;
    for (size_t i = 0; i < source.length(); ++i)
    {
        const wxChar ch = source[i];
        if (wxIsspace(ch) || ch == ':' || ch == '-' || ch == ',') continue;
        if (!wxIsxdigit(ch)) { error = ITOOL_TR("Hex data contains invalid characters."); return false; }
        clean += ch;
    }
    if (clean.empty()) { if (allowEmpty) return true; error = ITOOL_TR("The received data cannot be empty."); return false; }
    if (clean.length() % 2) { error = ITOOL_TR("Hex data must contain an even number of numbers."); return false; }
    for (size_t i = 0; i < clean.length(); i += 2)
    {
        unsigned long value = 0; if (!clean.Mid(i, 2).ToULong(&value, 16)) return false;
        bytes.push_back(static_cast<unsigned char>(value));
    }
    return true;
}

bool ParseAscii(const wxString& source, std::vector<unsigned char>& bytes, wxString& error, bool allowEmpty)
{
    if (source.empty() && !allowEmpty) { error = ITOOL_TR("ASCII text cannot be empty."); return false; }
    for (size_t i = 0; i < source.length(); ++i)
    {
        const unsigned long value = static_cast<unsigned long>(source[i]);
        if (value > 0x7f) { error = ITOOL_TR("ASCII text can only contain characters from 0x00 to 0x7F."); return false; }
        bytes.push_back(static_cast<unsigned char>(value));
    }
    return true;
}

wxString AsciiText(const std::vector<unsigned char>& bytes)
{
    wxString result;
    for (size_t i = 0; i < bytes.size(); ++i) result += static_cast<wxChar>(bytes[i]);
    return result;
}

std::unique_ptr<wxIPaddress> ResolveListenAddress(wxString host, unsigned short port, wxString& error)
{
    host.Trim(true).Trim(false);
    if (host.StartsWith(wxS("[")) && host.EndsWith(wxS("]"))) host = host.Mid(1, host.length() - 2);
    if (host.empty()) host = wxS("0.0.0.0");
    const bool preferIpv6 = host.Find(':') != wxNOT_FOUND;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const bool ipv6 = attempt == 0 ? preferIpv6 : !preferIpv6;
#if wxUSE_IPV6
        std::unique_ptr<wxIPaddress> address(ipv6 ? static_cast<wxIPaddress*>(new wxIPV6address)
                                                  : static_cast<wxIPaddress*>(new wxIPV4address));
#else
        if (ipv6) continue;
        std::unique_ptr<wxIPaddress> address(new wxIPV4address);
#endif
        const bool any = host == wxS("0.0.0.0") || host == wxS("::");
        if ((any ? address->AnyAddress() : address->Hostname(host)) && address->Service(port)) return address;
    }
    error = ITOOL_TR("Unable to resolve IPv4/IPv6 listening address:") + host;
    return std::unique_ptr<wxIPaddress>();
}

wxString HexText(const std::vector<unsigned char>& bytes, size_t limit = 48)
{
    static const wxChar digits[] = wxS("0123456789ABCDEF"); wxString result;
    const size_t count = std::min(bytes.size(), limit);
    for (size_t i = 0; i < count; ++i)
    {
        if (i) result += ' ';
        result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15];
    }
    if (bytes.size() > limit) result += wxS(" ...");
    return result;
}

wxString FormatTimestamp(const wxDateTime& value)
{
    return value.Format(wxS("%Y-%m-%d %H:%M:%S.%l"));
}

bool IsPrefix(const std::vector<unsigned char>& prefix, const std::vector<unsigned char>& value)
{
    return prefix.size() <= value.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

class MapDialog : public wxDialog
{
public:
    MapDialog(wxWindow* parent, const ResponseMap* initial)
        : wxDialog(parent, wxID_ANY, initial ? ITOOL_TR("Edit receive/send pairs") : ITOOL_TR("Add new receive/send pair"),
                   wxDefaultPosition, wxSize(620, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        SetMinSize(wxSize(560, 500));
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxFlexGridSizer* form = new wxFlexGridSizer(2, 7, 7); form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("name:")), 0, wxALIGN_CENTER_VERTICAL);
        m_name = new wxTextCtrl(this, wxID_ANY, initial ? initial->name : ITOOL_TR("Reply frame")); form->Add(m_name, 1, wxEXPAND);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Response delay (ms):")), 0, wxALIGN_CENTER_VERTICAL);
        m_delay = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 600000,
                                 initial ? static_cast<int>(initial->delayMs) : 0); form->Add(m_delay, 1, wxEXPAND);
        root->Add(form, 0, wxEXPAND | wxALL, 10);
        wxArrayString formats; formats.Add(wxS("Hex")); formats.Add(wxS("ASCII"));
        wxBoxSizer* receiveHeader = new wxBoxSizer(wxHORIZONTAL);
        receiveHeader->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Receive a match (must be an exact match):")), 1, wxALIGN_CENTER_VERTICAL);
        m_receiveFormat = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(90, -1), formats);
        m_receiveFormat->SetSelection(initial && initial->receiveAscii ? 1 : 0); receiveHeader->Add(m_receiveFormat);
        root->Add(receiveHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        m_receive = new wxTextCtrl(this, wxID_ANY, initial ? (initial->receiveAscii ? AsciiText(initial->receive) : HexText(initial->receive, initial->receive.size())) : wxString(),
                                   wxDefaultPosition, wxSize(-1, 130), wxTE_MULTILINE); m_receive->SetMinSize(wxSize(-1, 110)); root->Add(m_receive, 1, wxEXPAND | wxALL, 10);
        wxBoxSizer* sendHeader = new wxBoxSizer(wxHORIZONTAL);
        sendHeader->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Send after matching (can be left blank, only receive statistics):")), 1, wxALIGN_CENTER_VERTICAL);
        m_sendFormat = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(90, -1), formats);
        m_sendFormat->SetSelection(initial && initial->sendAscii ? 1 : 0); sendHeader->Add(m_sendFormat);
        root->Add(sendHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        m_send = new wxTextCtrl(this, wxID_ANY, initial ? (initial->sendAscii ? AsciiText(initial->send) : HexText(initial->send, initial->send.size())) : wxString(),
                                wxDefaultPosition, wxSize(-1, 130), wxTE_MULTILINE); m_send->SetMinSize(wxSize(-1, 110)); root->Add(m_send, 1, wxEXPAND | wxALL, 10);
        m_enabled = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Enable this mapping")); m_enabled->SetValue(initial ? initial->enabled : true);
        root->Add(m_enabled, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        root->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10); SetSizer(root);
    }
    bool Value(ResponseMap& map, wxString& error) const
    {
        map.name = m_name->GetValue(); map.name.Trim(true).Trim(false);
        if (map.name.empty()) { error = ITOOL_TR("Name cannot be empty."); return false; }
        map.delayMs = static_cast<unsigned long>(m_delay->GetValue()); map.enabled = m_enabled->GetValue();
        map.receiveAscii = m_receiveFormat->GetSelection() == 1; map.sendAscii = m_sendFormat->GetSelection() == 1;
        if (!(map.receiveAscii ? ParseAscii(m_receive->GetValue(), map.receive, error, false) : ParseHex(m_receive->GetValue(), map.receive, error, false))) return false;
        return map.sendAscii ? ParseAscii(m_send->GetValue(), map.send, error, true) : ParseHex(m_send->GetValue(), map.send, error, true);
    }
private:
    wxTextCtrl *m_name, *m_receive, *m_send; wxChoice *m_receiveFormat, *m_sendFormat; wxSpinCtrl* m_delay; wxCheckBox* m_enabled;
};

class ServerThread : public wxThread
{
public:
    ServerThread(wxEvtHandler* owner, const ServerSettings& settings)
        : wxThread(wxTHREAD_JOINABLE), m_owner(owner), m_settings(settings),
          m_stopping(false), m_pendingResultEvents(0) {}
    void Stop() { m_stopping = true; }
    void ResultEventHandled()
    {
        if (m_pendingResultEvents.load() != 0) --m_pendingResultEvents;
    }

protected:
    ExitCode Entry()
    {
        wxString error;
        if (m_settings.protocol == ServerTcp) RunTcp(error);
        else if (m_settings.protocol == ServerUdp) RunUdp(error);
        else if (m_settings.protocol == ServerWebSocket) RunWebSocket(error);
        else if (m_settings.protocol == ServerMqtt) RunMqtt(error);
        else RunSerial(error);
        wxThreadEvent* event = new wxThreadEvent(EVT_SERVER_FINISHED);
        event->SetString(error.empty() ? (m_stopping ? ITOOL_TR("The server has stopped.") : ITOOL_TR("The server has ended.")) : error);
        wxQueueEvent(m_owner, event); return static_cast<ExitCode>(0);
    }

private:
    const ResponseMap* ExactMatch(const std::vector<unsigned char>& data) const
    {
        for (size_t i = 0; i < m_settings.maps.size(); ++i)
            if (m_settings.maps[i].enabled && m_settings.maps[i].receive == data) return &m_settings.maps[i];
        return NULL;
    }
    const ResponseMap* StreamMatch(const std::vector<unsigned char>& buffer) const
    {
        for (size_t i = 0; i < m_settings.maps.size(); ++i)
            if (m_settings.maps[i].enabled && IsPrefix(m_settings.maps[i].receive, buffer)) return &m_settings.maps[i];
        return NULL;
    }
    bool CouldBePrefix(const std::vector<unsigned char>& buffer) const
    {
        for (size_t i = 0; i < m_settings.maps.size(); ++i)
            if (m_settings.maps[i].enabled && IsPrefix(buffer, m_settings.maps[i].receive)) return true;
        return false;
    }
    bool Delay(unsigned long milliseconds)
    {
        unsigned long elapsed = 0;
        while (elapsed < milliseconds && !m_stopping)
        {
            const unsigned long slice = std::min<unsigned long>(50, milliseconds - elapsed);
            wxMilliSleep(slice); elapsed += slice;
        }
        return !m_stopping;
    }
    void Post(const wxString& peer, const std::vector<unsigned char>& received,
              const ResponseMap* map, const std::vector<unsigned char>& sent, bool sendOk, const wxString& status)
    {
        // The server may run in the same process as a zero-interval client.
        // Apply backpressure here too, otherwise server result events alone
        // can starve mouse and Stop events in the shared GUI queue.
        while (!m_stopping && m_pendingResultEvents.load() >= 16)
            wxMilliSleep(2);
        if (m_stopping) return;
        ++m_pendingResultEvents;
        ServerResult result; result.peer = peer; result.received = received; result.sent = sent;
        result.name = map ? map->name : ITOOL_TR("Not matched"); result.matched = map != NULL; result.sendOk = sendOk; result.status = status;
        wxThreadEvent* event = new wxThreadEvent(EVT_SERVER_RESULT); event->SetPayload(result); wxQueueEvent(m_owner, event);
    }
    template <typename Sender>
    void ProcessStream(std::vector<unsigned char>& buffer, const wxString& peer, Sender& sender)
    {
        while (!buffer.empty() && !m_stopping)
        {
            const ResponseMap* map = StreamMatch(buffer);
            if (map)
            {
                std::vector<unsigned char> frame(buffer.begin(), buffer.begin() + map->receive.size());
                buffer.erase(buffer.begin(), buffer.begin() + map->receive.size());
                if (!Delay(map->delayMs)) return;
                const bool ok = map->send.empty() || sender.Send(map->send);
                Post(peer, frame, map, ok ? map->send : std::vector<unsigned char>(), ok,
                     ok ? ITOOL_TR("Matched and responded") : ITOOL_TR("Response sending failed"));
            }
            else if (CouldBePrefix(buffer)) return;
            else
            {
                std::vector<unsigned char> unmatched(1, buffer[0]); buffer.erase(buffer.begin());
                Post(peer, unmatched, NULL, std::vector<unsigned char>(), true, ITOOL_TR("Receive data does not match"));
            }
        }
    }

    struct SocketSender
    {
        wxSocketBase* socket;
        bool Send(const std::vector<unsigned char>& data)
        {
            size_t offset = 0;
            while (offset < data.size())
            {
                socket->Write(&data[offset], data.size() - offset);
                const size_t written = socket->LastCount();
                if (!written || socket->Error()) return false;
                offset += written;
            }
            return true;
        }
    };
    void RunTcp(wxString& error)
    {
        std::unique_ptr<wxIPaddress> address = ResolveListenAddress(m_settings.endpoint, m_settings.port, error);
        if (!address.get()) return;
        const bool ipv6 = address->Type() == wxSockAddress::IPV6;
        // The server is driven synchronously by this worker thread. Avoid
        // wxGTK asynchronous socket notifications crossing into the GUI loop
        // while the worker is finishing and destroying its socket objects.
        wxSocketServer server(*address, wxSOCKET_BLOCK);
        if (!server.IsOk()) { error = ITOOL_TR("Unable to listen on TCP port."); return; }
        std::unique_ptr<wxSocketBase> client; std::vector<unsigned char> buffer; wxString peer;
        while (!m_stopping)
        {
            if (!client.get())
            {
                if (!server.WaitForAccept(0, 50)) continue;
                client.reset(server.Accept(false)); buffer.clear(); if (!client.get()) continue;
                std::unique_ptr<wxIPaddress> peerAddress;
#if wxUSE_IPV6
                if (ipv6) peerAddress.reset(new wxIPV6address); else
#endif
                    peerAddress.reset(new wxIPV4address);
                peer = client->GetPeer(*peerAddress) ? peerAddress->IPAddress() : ITOOL_TR("TCP client");
            }
            if (!client->WaitForRead(0, 50)) continue;
            unsigned char chunk[4096]; client->Read(chunk, sizeof(chunk)); const size_t count = client->LastCount();
            if (!count && client->Error()) { client.reset(); buffer.clear(); continue; }
            buffer.insert(buffer.end(), chunk, chunk + count); SocketSender sender = { client.get() };
            ProcessStream(buffer, peer, sender);
            if (buffer.size() > 1024 * 1024) { buffer.clear(); Post(peer, std::vector<unsigned char>(), NULL, std::vector<unsigned char>(), true, ITOOL_TR("The receive cache has exceeded the limit and has been cleared.")); }
        }
    }

    void RunUdp(wxString& error)
    {
        std::unique_ptr<wxIPaddress> local = ResolveListenAddress(m_settings.endpoint, m_settings.port, error);
        if (!local.get()) return;
        const bool ipv6 = local->Type() == wxSockAddress::IPV6;
        wxDatagramSocket socket(*local, wxSOCKET_BLOCK);
        if (!socket.IsOk()) { error = ITOOL_TR("Unable to bind UDP port."); return; }
        unsigned char chunk[65535];
        while (!m_stopping)
        {
            if (!socket.WaitForRead(0, 50)) continue;
            std::unique_ptr<wxIPaddress> senderAddress;
#if wxUSE_IPV6
            if (ipv6) senderAddress.reset(new wxIPV6address); else
#endif
                senderAddress.reset(new wxIPV4address);
            socket.RecvFrom(*senderAddress, chunk, sizeof(chunk)); const size_t count = socket.LastCount();
            if (socket.Error()) continue;
            std::vector<unsigned char> frame(chunk, chunk + count); const ResponseMap* map = ExactMatch(frame);
            bool ok = true; std::vector<unsigned char> sent;
            if (map && Delay(map->delayMs) && !map->send.empty())
            {
                socket.SendTo(*senderAddress, &map->send[0], map->send.size()); ok = !socket.Error() && socket.LastCount() == map->send.size(); if (ok) sent = map->send;
            }
            Post(senderAddress->IPAddress(), frame, map, sent, ok, map ? (ok ? ITOOL_TR("Matched and responded") : ITOOL_TR("Response sending failed")) : ITOOL_TR("Receive data does not match"));
        }
    }

    void RunWebSocket(wxString& error)
    {
        std::unique_ptr<wxIPaddress> address = ResolveListenAddress(m_settings.endpoint, m_settings.port, error);
        if (!address.get()) return;
        const bool ipv6 = address->Type() == wxSockAddress::IPV6;
        wxSocketServer server(*address, wxSOCKET_BLOCK);
        if (!server.IsOk()) { error = ITOOL_TR("Unable to listen on WebSocket port."); return; }
        std::unique_ptr<wxSocketBase> client; wxString peer;
        while (!m_stopping)
        {
            if (!client.get())
            {
                if (!server.WaitForAccept(0, 50)) continue;
                client.reset(server.Accept(false)); if (!client.get()) continue;
                std::unique_ptr<wxIPaddress> peerAddress;
#if wxUSE_IPV6
                if (ipv6) peerAddress.reset(new wxIPV6address); else
#endif
                    peerAddress.reset(new wxIPV4address);
                peer = client->GetPeer(*peerAddress) ? peerAddress->IPAddress() : ITOOL_TR("WebSocket client");
                wxString handshakeError;
                if (!websocket::ServerHandshake(*client, m_settings.websocketPath,
                                                5000, m_stopping, handshakeError))
                {
                    Post(peer, std::vector<unsigned char>(), NULL, std::vector<unsigned char>(), false, handshakeError);
                    client.reset(); continue;
                }
            }
            std::vector<unsigned char> frame; wxString receiveError;
            if (!websocket::ReceiveMessage(*client, 600000, m_stopping, true, frame, receiveError))
            {
                if (!m_stopping && !receiveError.empty())
                    Post(peer, std::vector<unsigned char>(), NULL, std::vector<unsigned char>(), false, receiveError);
                client.reset(); continue;
            }
            const ResponseMap* map = ExactMatch(frame); bool ok = true; std::vector<unsigned char> sent;
            if (map && Delay(map->delayMs) && !map->send.empty())
            {
                ok = map->sendAscii ? websocket::SendText(*client, map->send, false, receiveError)
                                    : websocket::SendBinary(*client, map->send, false, receiveError);
                if (ok) sent = map->send;
            }
            Post(peer, frame, map, sent, ok, map ? (ok ? ITOOL_TR("Matched and responded") : ITOOL_TR("Response sending failed")) : ITOOL_TR("Received message not matched"));
            if (!ok) client.reset();
        }
    }

    void RunMqtt(wxString& error)
    {
        std::unique_ptr<wxIPaddress> address = ResolveListenAddress(m_settings.endpoint, m_settings.port, error);
        if (!address.get()) return;
        const bool ipv6 = address->Type() == wxSockAddress::IPV6;
        wxSocketServer server(*address, wxSOCKET_BLOCK);
        if (!server.IsOk()) { error = ITOOL_TR("Unable to listen on MQTT port."); return; }
        std::unique_ptr<wxSocketBase> client; wxString peer;
        while (!m_stopping)
        {
            if (!client.get())
            {
                if (!server.WaitForAccept(0, 50)) continue;
                client.reset(server.Accept(false)); if (!client.get()) continue;
                std::unique_ptr<wxIPaddress> peerAddress;
#if wxUSE_IPV6
                if (ipv6) peerAddress.reset(new wxIPV6address); else
#endif
                    peerAddress.reset(new wxIPV4address);
                peer = client->GetPeer(*peerAddress) ? peerAddress->IPAddress() : ITOOL_TR("MQTT client");
                wxString connectError;
                if (!mqtt::ServerAcceptConnect(*client, 5000, m_stopping, connectError))
                {
                    Post(peer, std::vector<unsigned char>(), NULL, std::vector<unsigned char>(), false, connectError);
                    client.reset(); continue;
                }
            }
            std::string topic; std::vector<unsigned char> payload; wxString receiveError;
            if (!mqtt::ReceivePublish(*client, 600000, m_stopping, topic, payload, receiveError))
            {
                if (!m_stopping && !receiveError.empty())
                    Post(peer, std::vector<unsigned char>(), NULL, std::vector<unsigned char>(), false, receiveError);
                client.reset(); continue;
            }
            if (topic != std::string(m_settings.mqttRequestTopic.utf8_str()))
            {
                Post(peer, payload, NULL, std::vector<unsigned char>(), true, ITOOL_TR("MQTT publish topic mismatch:") + wxString::FromUTF8(topic.c_str()));
                continue;
            }
            const ResponseMap* map = ExactMatch(payload); bool ok = true; std::vector<unsigned char> sent;
            if (map && Delay(map->delayMs) && !map->send.empty())
            {
                ok = mqtt::Publish(*client, std::string(m_settings.mqttResponseTopic.utf8_str()), map->send, receiveError);
                if (ok) sent = map->send;
            }
            Post(peer, payload, map, sent, ok, map ? (ok ? ITOOL_TR("Matched and posted response") : ITOOL_TR("MQTT response publishing failed")) : ITOOL_TR("MQTT Payload not matched"));
            if (!ok) client.reset();
        }
    }

#ifdef __WXMSW__
    struct SerialSender
    {
        HANDLE handle;
        bool Send(const std::vector<unsigned char>& data)
        {
            if (data.empty()) return true;
            DWORD written = 0;
            return WriteFile(handle, &data[0], static_cast<DWORD>(data.size()), &written, NULL) && written == data.size();
        }
    };
    void RunSerial(wxString& error)
    {
        wxString name = m_settings.endpoint; if (!name.StartsWith(wxS("\\\\.\\"))) name = wxS("\\\\.\\") + name;
        HANDLE handle = CreateFileW(name.wc_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) { error = ITOOL_TR("Unable to open serial port:") + m_settings.endpoint; return; }
        DCB dcb; ZeroMemory(&dcb, sizeof(dcb)); dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle, &dcb)) { CloseHandle(handle); error = ITOOL_TR("Unable to read serial port configuration."); return; }
        static const BYTE parityValues[] = { NOPARITY, ODDPARITY, EVENPARITY, MARKPARITY, SPACEPARITY };
        static const BYTE stopValues[] = { ONESTOPBIT, ONE5STOPBITS, TWOSTOPBITS };
        dcb.BaudRate = m_settings.baud; dcb.ByteSize = m_settings.dataBits;
        dcb.Parity = parityValues[m_settings.parity]; dcb.fParity = m_settings.parity != 0;
        dcb.StopBits = stopValues[m_settings.stopBits]; dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE; dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE; dcb.fInX = FALSE; dcb.fTXContinueOnXoff = TRUE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE; dcb.fRtsControl = RTS_CONTROL_ENABLE;
        if (m_settings.flowControl == 1) { dcb.fOutxCtsFlow = TRUE; dcb.fRtsControl = RTS_CONTROL_HANDSHAKE; }
        else if (m_settings.flowControl == 2) { dcb.fOutxDsrFlow = TRUE; dcb.fDtrControl = DTR_CONTROL_HANDSHAKE; }
        else if (m_settings.flowControl == 3) { dcb.fOutX = TRUE; dcb.fInX = TRUE; }
        if (!SetCommState(handle, &dcb)) { CloseHandle(handle); error = ITOOL_TR("Unable to set serial port parameters."); return; }
        COMMTIMEOUTS timeouts; ZeroMemory(&timeouts, sizeof(timeouts)); timeouts.ReadIntervalTimeout = 20; timeouts.ReadTotalTimeoutConstant = 20; SetCommTimeouts(handle, &timeouts);
        std::vector<unsigned char> buffer; SerialSender sender = { handle };
        while (!m_stopping)
        {
            unsigned char chunk[4096]; DWORD count = 0;
            if (!ReadFile(handle, chunk, sizeof(chunk), &count, NULL)) { error = ITOOL_TR("Serial port reception failed."); break; }
            if (count) { buffer.insert(buffer.end(), chunk, chunk + count); ProcessStream(buffer, m_settings.endpoint, sender); }
            else wxMilliSleep(10);
            if (buffer.size() > 1024 * 1024) buffer.clear();
        }
        CloseHandle(handle);
    }
#else
    struct SerialSender
    {
        posixserial::Port* port;
        bool Send(const std::vector<unsigned char>& data)
        {
            wxString ignored;
            return data.empty() || port->Write(data, ignored);
        }
    };
    void RunSerial(wxString& error)
    {
        posixserial::Port port;
        if (!port.Open(m_settings.endpoint, m_settings.baud, m_settings.dataBits,
                       m_settings.stopBits, m_settings.parity, m_settings.flowControl, error)) return;
        std::vector<unsigned char> buffer; SerialSender sender = { &port };
        while (!m_stopping)
        {
            unsigned char chunk[4096]; size_t count = 0;
            if (!port.Read(chunk, sizeof(chunk), 50, count, error)) break;
            if (count) { buffer.insert(buffer.end(), chunk, chunk + count); ProcessStream(buffer, m_settings.endpoint, sender); }
            if (buffer.size() > 1024 * 1024) buffer.clear();
        }
    }
#endif
    wxEvtHandler* m_owner; ServerSettings m_settings; std::atomic<bool> m_stopping;
    std::atomic<unsigned int> m_pendingResultEvents;
};

class CommServerPanel : public wxPanel, public IStatefulPanel
{
public:
    explicit CommServerPanel(wxWindow* parent)
        : wxPanel(parent), m_thread(NULL), m_received(0), m_matched(0), m_sent(0), m_unmatched(0)
    {
        Build(); Bind(EVT_SERVER_RESULT, &CommServerPanel::OnResult, this); Bind(EVT_SERVER_FINISHED, &CommServerPanel::OnFinished, this);
    }
    ~CommServerPanel() { StopThread(); }
private:
    void Build()
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL); wxBoxSizer* connection = new wxBoxSizer(wxHORIZONTAL);
        wxArrayString endpoints; endpoints.Add(wxS("TCP")); endpoints.Add(wxS("UDP")); endpoints.Add(wxS("WebSocket")); endpoints.Add(wxS("MQTT")); const wxArrayString ports = SerialPorts();
        for (size_t i = 0; i < ports.size(); ++i) endpoints.Add(ports[i]);
        m_protocol = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), endpoints, wxCB_READONLY); m_protocol->SetSelection(0);
        m_addressLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Listening address:")); m_address = new wxTextCtrl(this, wxID_ANY, wxS("0.0.0.0"), wxDefaultPosition, wxSize(170, -1));
        m_address->SetToolTip(ITOOL_TR("IPv4 full interface: 0.0.0.0; IPv6 full interface:::"));
        m_portLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("port:")); m_port = new wxSpinCtrl(this, wxID_ANY, wxS("9000"), wxDefaultPosition, wxSize(85, -1), wxSP_ARROW_KEYS, 1, 65535, 9000);
        m_wsPathLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("path:")); m_wsPath = new wxTextCtrl(this, wxID_ANY, wxS("/"), wxDefaultPosition, wxSize(110, -1));
        m_wsPath->SetToolTip(ITOOL_TR("Only accept this WebSocket path, such as /ws; currently supports ws://, not TLS/wss://"));
        m_mqttReqLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Receive topic:")); m_mqttReq = new wxTextCtrl(this, wxID_ANY, wxS("itool/request"), wxDefaultPosition, wxSize(120, -1));
        m_mqttRespLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Respond to the topic:")); m_mqttResp = new wxTextCtrl(this, wxID_ANY, wxS("itool/response"), wxDefaultPosition, wxSize(120, -1));
        wxArrayString bauds; bauds.Add(wxS("1200")); bauds.Add(wxS("2400")); bauds.Add(wxS("4800")); bauds.Add(wxS("9600")); bauds.Add(wxS("19200")); bauds.Add(wxS("38400")); bauds.Add(wxS("57600")); bauds.Add(wxS("115200")); bauds.Add(wxS("230400")); bauds.Add(wxS("460800")); bauds.Add(wxS("921600"));
        m_baudLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Baud rate:")); m_baud = new wxComboBox(this, wxID_ANY, wxS("9600"), wxDefaultPosition, wxSize(105, -1), bauds, wxCB_DROPDOWN);
        m_baud->SetToolTip(ITOOL_TR("Common baud rates can be selected or manually entered from 1 to 4000000."));
        connection->Add(m_protocol, 0, wxRIGHT, 8); connection->Add(m_addressLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_address, 0, wxRIGHT, 8);
        connection->Add(m_portLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_port, 0, wxRIGHT, 8); connection->Add(m_baudLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_baud, 0);
        connection->Add(m_wsPathLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_wsPath, 0, wxRIGHT, 8);
        connection->Add(m_mqttReqLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_mqttReq, 0, wxRIGHT, 8);
        connection->Add(m_mqttRespLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_mqttResp, 0, wxRIGHT, 8);
        root->Add(connection, 0, wxEXPAND | wxALL, 8);

        m_serialOptions = new wxBoxSizer(wxHORIZONTAL);
        wxArrayString dataBits; dataBits.Add(wxS("5")); dataBits.Add(wxS("6")); dataBits.Add(wxS("7")); dataBits.Add(wxS("8"));
        m_dataBits = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(65, -1), dataBits); m_dataBits->SetSelection(3);
        wxArrayString stopBits; stopBits.Add(wxS("1")); stopBits.Add(wxS("1.5")); stopBits.Add(wxS("2"));
        m_stopBits = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(70, -1), stopBits); m_stopBits->SetSelection(0);
        wxArrayString parity; parity.Add(ITOOL_TR("none")); parity.Add(ITOOL_TR("odd parity")); parity.Add(ITOOL_TR("even parity")); parity.Add(ITOOL_TR("mark")); parity.Add(ITOOL_TR("space"));
        m_parity = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(90, -1), parity); m_parity->SetSelection(0);
        wxArrayString flow; flow.Add(ITOOL_TR("none")); flow.Add(wxS("RTS/CTS")); flow.Add(wxS("DTR/DSR")); flow.Add(wxS("XON/XOFF"));
        m_flowControl = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(105, -1), flow); m_flowControl->SetSelection(0);
        m_serialOptions->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Data bits:")), 0, wxALIGN_CENTER_VERTICAL); m_serialOptions->Add(m_dataBits, 0, wxRIGHT, 10);
        m_serialOptions->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Stop bit:")), 0, wxALIGN_CENTER_VERTICAL); m_serialOptions->Add(m_stopBits, 0, wxRIGHT, 10);
        m_serialOptions->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Parity check:")), 0, wxALIGN_CENTER_VERTICAL); m_serialOptions->Add(m_parity, 0, wxRIGHT, 10);
        m_serialOptions->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Flow control:")), 0, wxALIGN_CENTER_VERTICAL); m_serialOptions->Add(m_flowControl);
        connection->Add(m_serialOptions, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

        wxBoxSizer* body = new wxBoxSizer(wxHORIZONTAL);
        m_log = new CommunicationLog(this);
        m_log->InsertColumn(0, ITOOL_TR("serial number"), wxLIST_FORMAT_RIGHT, 55);
        m_log->InsertColumn(1, ITOOL_TR("Timestamp"), wxLIST_FORMAT_LEFT, 175);
        m_log->InsertColumn(2, ITOOL_TR("source"), wxLIST_FORMAT_LEFT, 100);
        m_log->InsertColumn(3, ITOOL_TR("mapping"), wxLIST_FORMAT_LEFT, 100);
        m_log->InsertColumn(4, ITOOL_TR("take over"), wxLIST_FORMAT_LEFT, 170);
        m_log->InsertColumn(5, ITOOL_TR("result"), wxLIST_FORMAT_LEFT, 130);
        wxBoxSizer* logSide = new wxBoxSizer(wxVERTICAL);
        wxButton* copyLog = new wxButton(this, wxID_ANY, ITOOL_TR("Copy"));
        copyLog->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_log->CopyRecords(); });
        copyLog->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
            event.Enable(m_log->GetItemCount() > 0);
        });
        logSide->Add(copyLog, 0, wxALIGN_RIGHT | wxBOTTOM, 5);
        logSide->Add(m_log, 1, wxEXPAND);
        body->Add(logSide, 1, wxEXPAND | wxRIGHT, 8);
        wxBoxSizer* side = new wxBoxSizer(wxVERTICAL); wxBoxSizer* mapButtons = new wxBoxSizer(wxHORIZONTAL);
        wxButton* add = new wxButton(this, wxID_ADD, wxS("+"), wxDefaultPosition, wxSize(28, -1), wxBU_EXACTFIT);
        wxButton* edit = new wxButton(this, wxID_EDIT, wxS("E"), wxDefaultPosition, wxSize(28, -1), wxBU_EXACTFIT);
        wxButton* remove = new wxButton(this, wxID_REMOVE, wxS("-"), wxDefaultPosition, wxSize(28, -1), wxBU_EXACTFIT);
        add->SetToolTip(ITOOL_TR("Added receive/send mapping")); edit->SetToolTip(ITOOL_TR("Edit selected receive/send mapping")); remove->SetToolTip(ITOOL_TR("Delete selected receive/send mapping"));
        mapButtons->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Out-of-order receive/transmit mapping")), 1, wxALIGN_CENTER_VERTICAL); mapButtons->Add(add, 0, wxRIGHT, 2); mapButtons->Add(edit, 0, wxRIGHT, 2); mapButtons->Add(remove);
        side->Add(mapButtons, 0, wxEXPAND | wxBOTTOM, 5);
        m_maps = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
        m_maps->InsertColumn(0, ITOOL_TR("name"), wxLIST_FORMAT_LEFT, 90); m_maps->InsertColumn(1, ITOOL_TR("take over"), wxLIST_FORMAT_LEFT, 130);
        m_maps->InsertColumn(2, ITOOL_TR("send"), wxLIST_FORMAT_LEFT, 130); m_maps->InsertColumn(3, ITOOL_TR("delay"), wxLIST_FORMAT_RIGHT, 55);
        side->Add(m_maps, 1, wxEXPAND); body->Add(side, 1, wxEXPAND); root->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

        wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL); m_stats = new wxStaticText(this, wxID_ANY, ITOOL_TR("Received: 0 Matched: 0 Unmatched: 0 Responded: 0"));
        m_start = new wxButton(this, wxID_ANY, ITOOL_TR("Start the server")); m_stop = new wxButton(this, wxID_ANY, ITOOL_TR("stop")); wxButton* clear = new wxButton(this, wxID_CLEAR, ITOOL_TR("Clear log")); m_stop->Disable();
        footer->Add(m_stats, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8); footer->Add(m_start, 0, wxRIGHT, 6); footer->Add(m_stop, 0, wxRIGHT, 6); footer->Add(clear);
        root->Add(footer, 0, wxEXPAND | wxALL, 8); SetSizer(root);
        m_protocol->Bind(wxEVT_COMBOBOX, &CommServerPanel::ProtocolChanged, this);
        m_protocol->Bind(wxEVT_COMBOBOX_DROPDOWN, &CommServerPanel::ProtocolDropdown, this); add->Bind(wxEVT_BUTTON, &CommServerPanel::Add, this);
        edit->Bind(wxEVT_BUTTON, &CommServerPanel::Edit, this); remove->Bind(wxEVT_BUTTON, &CommServerPanel::Remove, this); m_maps->Bind(wxEVT_LIST_ITEM_ACTIVATED, &CommServerPanel::EditList, this);
        m_start->Bind(wxEVT_BUTTON, &CommServerPanel::Start, this); m_stop->Bind(wxEVT_BUTTON, &CommServerPanel::Stop, this); clear->Bind(wxEVT_BUTTON, &CommServerPanel::Clear, this); UpdateProtocol();
    }
    ServerProtocol Protocol() const { return m_protocol->GetSelection() == 0 ? ServerTcp : (m_protocol->GetSelection() == 1 ? ServerUdp : (m_protocol->GetSelection() == 2 ? ServerWebSocket : (m_protocol->GetSelection() == 3 ? ServerMqtt : ServerSerial))); }
    void ProtocolChanged(wxCommandEvent&) { UpdateProtocol(); }
    void ProtocolDropdown(wxCommandEvent& event)
    {
        const wxString selected = m_protocol->GetStringSelection();
        m_protocol->Freeze(); m_protocol->Clear(); m_protocol->Append(wxS("TCP")); m_protocol->Append(wxS("UDP")); m_protocol->Append(wxS("WebSocket")); m_protocol->Append(wxS("MQTT"));
        const wxArrayString ports = SerialPorts();
        for (size_t i = 0; i < ports.size(); ++i) m_protocol->Append(ports[i]);
        const int restored = m_protocol->FindString(selected);
        m_protocol->SetSelection(restored == wxNOT_FOUND ? 0 : restored);
        m_protocol->Thaw();
        if (restored == wxNOT_FOUND) UpdateProtocol();
        event.Skip();
    }
    void UpdateProtocol()
    {
        const bool serial = Protocol() == ServerSerial, ws = Protocol() == ServerWebSocket, mqttProtocol = Protocol() == ServerMqtt;
        if (mqttProtocol && m_port->GetValue() == 9000) m_port->SetValue(1883);
        m_addressLabel->Show(!serial); m_address->Show(!serial); m_portLabel->Show(!serial); m_port->Show(!serial);
        m_wsPathLabel->Show(ws); m_wsPath->Show(ws); m_baudLabel->Show(serial); m_baud->Show(serial); m_serialOptions->ShowItems(serial); Layout();
        m_mqttReqLabel->Show(mqttProtocol); m_mqttReq->Show(mqttProtocol); m_mqttRespLabel->Show(mqttProtocol); m_mqttResp->Show(mqttProtocol); Layout();
    }
    void RefreshMaps()
    {
        m_maps->DeleteAllItems();
        for (size_t i = 0; i < m_values.size(); ++i)
        {
            const long row = m_maps->InsertItem(static_cast<long>(i), (m_values[i].enabled ? wxString() : ITOOL_TR("[stop]")) + m_values[i].name);
            m_maps->SetItem(row, 1, m_values[i].receiveAscii ? wxS("ASCII: ") + AsciiText(m_values[i].receive) : HexText(m_values[i].receive, 12));
            m_maps->SetItem(row, 2, m_values[i].sendAscii ? wxS("ASCII: ") + AsciiText(m_values[i].send) : HexText(m_values[i].send, 12));
            m_maps->SetItem(row, 3, wxString::Format(wxS("%lu"), m_values[i].delayMs));
        }
    }
    void Add(wxCommandEvent&)
    {
        MapDialog dialog(this, NULL); if (dialog.ShowModal() != wxID_OK) return; ResponseMap value; wxString error;
        if (!dialog.Value(value, error) || !Validate(value, -1, error)) { wxMessageBox(error, ITOOL_TR("communication server"), wxOK | wxICON_WARNING, this); return; }
        m_values.push_back(value); RefreshMaps();
    }
    bool Validate(const ResponseMap& value, long ignore, wxString& error) const
    {
        for (size_t i = 0; i < m_values.size(); ++i)
        {
            if (static_cast<long>(i) == ignore || !m_values[i].enabled || !value.enabled) continue;
            if ((Protocol() == ServerTcp || Protocol() == ServerSerial) &&
                (IsPrefix(value.receive, m_values[i].receive) || IsPrefix(m_values[i].receive, value.receive)))
                { error = ITOOL_TR("The received frames cannot be the same or prefix each other, otherwise the TCP/serial port cannot determine the frame boundary."); return false; }
        }
        return true;
    }
    void EditSelected()
    {
        const long selected = m_maps->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); if (selected < 0) return;
        MapDialog dialog(this, &m_values[static_cast<size_t>(selected)]); if (dialog.ShowModal() != wxID_OK) return; ResponseMap value; wxString error;
        if (!dialog.Value(value, error) || !Validate(value, selected, error)) { wxMessageBox(error, ITOOL_TR("communication server"), wxOK | wxICON_WARNING, this); return; }
        m_values[static_cast<size_t>(selected)] = value; RefreshMaps();
    }
    void Edit(wxCommandEvent&) { EditSelected(); } void EditList(wxListEvent&) { EditSelected(); }
    void Remove(wxCommandEvent&) { const long selected = m_maps->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); if (selected >= 0) { m_values.erase(m_values.begin() + selected); RefreshMaps(); } }
    bool Settings(ServerSettings& settings, wxString& error)
    {
        bool enabled = false; for (size_t i = 0; i < m_values.size(); ++i) enabled = enabled || m_values[i].enabled;
        if (!enabled) { error = ITOOL_TR("Please configure at least one enabled receive/send mapping."); return false; }
        settings.protocol = Protocol(); settings.endpoint = settings.protocol == ServerSerial ? m_protocol->GetStringSelection() : m_address->GetValue();
        settings.port = static_cast<unsigned short>(m_port->GetValue());
        settings.websocketPath = m_wsPath->GetValue(); settings.websocketPath.Trim(true).Trim(false);
        if (settings.protocol == ServerWebSocket && settings.websocketPath.empty()) settings.websocketPath = wxS("/");
        settings.mqttRequestTopic = m_mqttReq->GetValue(); settings.mqttRequestTopic.Trim(true).Trim(false);
        settings.mqttResponseTopic = m_mqttResp->GetValue(); settings.mqttResponseTopic.Trim(true).Trim(false);
        if (settings.protocol == ServerMqtt && (settings.mqttRequestTopic.empty() || settings.mqttResponseTopic.empty()))
            { error = ITOOL_TR("MQTT receive topic and response topic cannot be empty."); return false; }
        unsigned long baud = 0;
        if (!m_baud->GetValue().ToULong(&baud) || baud < 1 || baud > 4000000)
            { error = ITOOL_TR("The baud rate must be an integer between 1 and 4000000."); return false; }
        settings.baud = baud;
        unsigned long dataBits = 8; m_dataBits->GetStringSelection().ToULong(&dataBits); settings.dataBits = static_cast<unsigned char>(dataBits);
        settings.stopBits = m_stopBits->GetSelection(); settings.parity = m_parity->GetSelection(); settings.flowControl = m_flowControl->GetSelection();
        if (settings.protocol == ServerSerial && settings.stopBits == 1 && settings.dataBits != 5)
            { error = ITOOL_TR("1.5 stop bits can only be used in combination with 5 data bits."); return false; }
        if (settings.protocol == ServerSerial && settings.stopBits == 2 && settings.dataBits == 5)
            { error = ITOOL_TR("5 data bits cannot be combined with 2 stop bits."); return false; }
        settings.maps = m_values; return true;
    }
    void Start(wxCommandEvent&)
    {
        ServerSettings settings; wxString error; if (!Settings(settings, error)) { wxMessageBox(error, ITOOL_TR("communication server"), wxOK | wxICON_WARNING, this); return; }
        m_logFile.reset(); m_logPath.clear();
        m_logStem = settings.protocol == ServerSerial
            ? settings.endpoint
            : settings.endpoint + wxString::Format(wxS("_%u"), static_cast<unsigned int>(settings.port));
        const wxString invalid = wxS("<>:\"/\\|?*");
        for (size_t i = 0; i < m_logStem.length(); ++i)
            if (invalid.Find(m_logStem[i]) != wxNOT_FOUND) m_logStem[i] = '_';
        m_thread = new ServerThread(this, settings); if (m_thread->Run() != wxTHREAD_NO_ERROR) { delete m_thread; m_thread = NULL; wxMessageBox(ITOOL_TR("Unable to start server thread.")); return; }
        m_start->Disable(); m_stop->Enable();
    }
    void Stop(wxCommandEvent&)
    {
        if (!m_thread) return;
        m_stop->Disable();
        m_thread->Stop();
    }
    void StopThread() { if (m_thread) { m_thread->Stop(); m_thread->Wait(); delete m_thread; m_thread = NULL; } }
    void Clear(wxCommandEvent&) { if (!m_thread) { m_log->ClearRecords(); m_received = m_matched = m_sent = m_unmatched = 0; UpdateStats(); } }
    void OnResult(wxThreadEvent& event)
    {
        const ServerResult result = event.GetPayload<ServerResult>(); ++m_received; if (result.matched) ++m_matched; else ++m_unmatched; if (!result.sent.empty() && result.sendOk) ++m_sent;
        const wxDateTime now = wxDateTime::UNow();
        const long row = m_log->InsertItem(m_log->GetItemCount(), wxString::Format(wxS("%ld"), m_log->GetItemCount() + 1));
        m_log->SetItem(row, 1, FormatTimestamp(now)); m_log->SetItem(row, 2, result.peer); m_log->SetItem(row, 3, result.name);
        m_log->SetRecordText(row, 4, HexText(result.received), HexText(result.received, result.received.size()));
        m_log->SetItem(row, 5, result.status); WriteDataLog(result, now); m_log->EnsureVisible(row); UpdateStats();
        if (m_thread) m_thread->ResultEventHandled();
    }

    void WriteDataLog(const ServerResult& result, const wxDateTime& now)
    {
        if (!apppaths::EnsureLogDirectory()) return;
        const wxString logDirectory = apppaths::LogDirectory();
        const wxString fileName = m_logStem + wxS("_") + now.Format(wxS("%Y-%m-%dT%H")) + wxS(".log");
        const wxString path = wxFileName(logDirectory, fileName).GetFullPath();
        if (!m_logFile.get() || path != m_logPath)
        {
            m_logFile.reset(new wxFFile(path, wxS("ab")));
            m_logPath = path;
        }
        if (!m_logFile->IsOpened()) return;
        const wxString timestamp = FormatTimestamp(now);
        const wxString label = wxS(" [") + result.peer + wxS("/") + result.name + wxS("] ");
        wxString entry = timestamp + label + wxS("RX: ") + HexText(result.received, result.received.size()) + wxS("\r\n");
        if (!result.sent.empty()) entry += timestamp + label + wxS("TX: ") + HexText(result.sent, result.sent.size()) + wxS("\r\n");
        entry += timestamp + label + wxS("RESULT: ") + result.status + wxS("\r\n");
        const wxCharBuffer utf8 = entry.utf8_str();
        if (utf8.length()) m_logFile->Write(utf8.data(), utf8.length());
    }
    void OnFinished(wxThreadEvent& event)
    {
        // The finish event is posted just before Entry() returns. Defer the
        // join/delete operation until the GTK event queue has drained all
        // socket result events, and don't open a modal dialog while the
        // worker thread is still unwinding.
        m_stop->Disable();
        CallAfter(&CommServerPanel::CompleteThread, event.GetString());
    }
    void CompleteThread(const wxString& message)
    {
        if (m_thread) { m_thread->Wait(); delete m_thread; m_thread = NULL; }
        m_logFile.reset(); m_logPath.clear();
        m_start->Enable(); m_stop->Disable(); SetToolTip(message);
    }
    void UpdateStats() { m_stats->SetLabel(wxString::Format(ITOOL_TR("Received: %lu Matched: %lu Unmatched: %lu Responded: %lu"), m_received, m_matched, m_unmatched, m_sent)); }
    void LoadCustomState(wxConfigBase& config, const wxString& prefix)
    {
        long count = 0; config.Read(prefix + wxS("/maps/count"), &count, 0L); m_values.clear();
        for (long i = 0; i < count; ++i)
        {
            const wxString base = prefix + wxString::Format(wxS("/maps/%ld"), i); ResponseMap value; wxString receive, send, error;
            config.Read(base + wxS("/name"), &value.name, ITOOL_TR("Reply frame"));
            long delay = 0; config.Read(base + wxS("/delayMs"), &delay, 0L); value.delayMs = static_cast<unsigned long>(std::max(0L, delay));
            config.Read(base + wxS("/receive"), &receive); config.Read(base + wxS("/send"), &send); config.Read(base + wxS("/enabled"), &value.enabled, true);
            config.Read(base + wxS("/receiveAscii"), &value.receiveAscii, false); config.Read(base + wxS("/sendAscii"), &value.sendAscii, false);
            const bool receiveOk = value.receiveAscii ? ParseAscii(receive, value.receive, error, false) : ParseHex(receive, value.receive, error, false);
            const bool sendOk = value.sendAscii ? ParseAscii(send, value.send, error, true) : ParseHex(send, value.send, error, true);
            if (receiveOk && sendOk) m_values.push_back(value);
        }
        RefreshMaps();
    }
    void SaveCustomState(wxConfigBase& config, const wxString& prefix) const
    {
        config.Write(prefix + wxS("/maps/count"), static_cast<long>(m_values.size()));
        for (size_t i = 0; i < m_values.size(); ++i)
        {
            const wxString base = prefix + wxString::Format(wxS("/maps/%lu"), static_cast<unsigned long>(i));
            config.Write(base + wxS("/name"), m_values[i].name); config.Write(base + wxS("/delayMs"), static_cast<long>(m_values[i].delayMs));
            config.Write(base + wxS("/receive"), m_values[i].receiveAscii ? AsciiText(m_values[i].receive) : HexText(m_values[i].receive, m_values[i].receive.size()));
            config.Write(base + wxS("/send"), m_values[i].sendAscii ? AsciiText(m_values[i].send) : HexText(m_values[i].send, m_values[i].send.size()));
            config.Write(base + wxS("/receiveAscii"), m_values[i].receiveAscii); config.Write(base + wxS("/sendAscii"), m_values[i].sendAscii); config.Write(base + wxS("/enabled"), m_values[i].enabled);
        }
    }
    wxComboBox *m_protocol, *m_baud; wxChoice *m_dataBits, *m_stopBits, *m_parity, *m_flowControl; wxSizer* m_serialOptions;
    wxStaticText *m_addressLabel, *m_portLabel, *m_wsPathLabel, *m_mqttReqLabel, *m_mqttRespLabel, *m_baudLabel, *m_stats;
    wxTextCtrl *m_address, *m_wsPath, *m_mqttReq, *m_mqttResp; wxSpinCtrl* m_port;
    CommunicationLog* m_log; wxListCtrl* m_maps; wxButton *m_start, *m_stop; std::vector<ResponseMap> m_values; ServerThread* m_thread;
    unsigned long m_received, m_matched, m_sent, m_unmatched;
    wxString m_logStem;
    std::unique_ptr<wxFFile> m_logFile;
    wxString m_logPath;
};
}

wxString CommServerToolModule::GetId() const { return wxS("communication-server"); }
wxString CommServerToolModule::GetName() const { return ITOOL_TR("Communication server"); }
wxString CommServerToolModule::GetDescription() const { return ITOOL_TR("Match messages and respond over serial, TCP, UDP, WebSocket, or MQTT"); }
wxWindow* CommServerToolModule::CreatePanel(wxWindow* parent) { return new CommServerPanel(parent); }
