#include "tools/comm/CommunicationLog.h"
#include "tools/commtest/CommTestToolModule.h"

#include "core/Localization.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
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
#include <wx/stopwatch.h>
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
#else
#include <sys/socket.h>
#endif

namespace
{
enum Protocol { ProtocolSerial, ProtocolTcp, ProtocolUdp, ProtocolWebSocket, ProtocolMqtt };

wxArrayString AvailableSerialPorts()
{
    wxArrayString ports;
#ifdef __WXMSW__
    wchar_t target[512];
    for (int number = 1; number <= 256; ++number)
    {
        const wxString name = wxString::Format(wxS("COM%d"), number);
        if (QueryDosDeviceW(name.wc_str(), target, sizeof(target) / sizeof(target[0])) != 0)
            ports.Add(name);
    }
#else
    ports = posixserial::AvailablePorts();
#endif
    return ports;
}

struct Exchange
{
    wxString name;
    unsigned long delayMs;
    std::vector<unsigned char> request;
    std::vector<unsigned char> expected;
    bool requestAscii;
    bool expectedAscii;
    bool verify;
};

struct TestSettings
{
    Protocol protocol;
    wxString endpoint;
    unsigned short port;
    unsigned short localPort;
    wxString websocketPath;
    wxString mqttPublishTopic;
    wxString mqttSubscribeTopic;
    unsigned long baud;
    unsigned char dataBits;
    int stopBits;
    int parity;
    int flowControl;
    unsigned long cycles;
    unsigned long intervalMs;
    unsigned long timeoutMs;
    unsigned long retries;
    bool stopOnFailure;
    std::vector<Exchange> exchanges;
};

struct AttemptResult
{
    wxString name;
    wxString detail;
    bool success;
    bool mismatch;
    bool timeout;
    size_t sent;
    size_t received;
    std::vector<unsigned char> txData;
    std::vector<unsigned char> rxData;
};

wxDECLARE_EVENT(EVT_COMM_ATTEMPT, wxThreadEvent);
wxDECLARE_EVENT(EVT_COMM_CYCLE, wxThreadEvent);
wxDECLARE_EVENT(EVT_COMM_FINISHED, wxThreadEvent);
wxDEFINE_EVENT(EVT_COMM_ATTEMPT, wxThreadEvent);
wxDEFINE_EVENT(EVT_COMM_CYCLE, wxThreadEvent);
wxDEFINE_EVENT(EVT_COMM_FINISHED, wxThreadEvent);

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
    if (clean.empty()) { if (allowEmpty) return true; error = ITOOL_TR("Request data cannot be empty."); return false; }
    if (clean.length() % 2) { error = ITOOL_TR("Hex data must contain an even number of numbers."); return false; }
    for (size_t i = 0; i < clean.length(); i += 2)
    {
        unsigned long value = 0;
        if (!clean.Mid(i, 2).ToULong(&value, 16)) return false;
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

std::unique_ptr<wxIPaddress> ResolveAddress(wxString host, unsigned short port, bool anyAddress, wxString& error)
{
    host.Trim(true).Trim(false);
    if (host.StartsWith(wxS("[")) && host.EndsWith(wxS("]"))) host = host.Mid(1, host.length() - 2);
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
        const bool hostOk = anyAddress ? address->AnyAddress() : address->Hostname(host);
        if (hostOk && address->Service(port)) return address;
    }
    error = ITOOL_TR("Unable to resolve IPv4/IPv6 address:") + host;
    return std::unique_ptr<wxIPaddress>();
}

wxString HexText(const std::vector<unsigned char>& bytes, size_t limit = 64)
{
    static const wxChar digits[] = wxS("0123456789ABCDEF");
    wxString result; const size_t count = std::min(bytes.size(), limit);
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

class ExchangeDialog : public wxDialog
{
public:
    ExchangeDialog(wxWindow* parent, const Exchange* initial)
        : wxDialog(parent, wxID_ANY, initial ? ITOOL_TR("Edit send/receive pairs") : ITOOL_TR("Add new send/receive pair"),
                   wxDefaultPosition, wxSize(620, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        SetMinSize(wxSize(560, 500));
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxFlexGridSizer* form = new wxFlexGridSizer(2, 7, 7); form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("name:")), 0, wxALIGN_CENTER_VERTICAL);
        m_name = new wxTextCtrl(this, wxID_ANY, initial ? initial->name : ITOOL_TR("test frame")); form->Add(m_name, 1, wxEXPAND);
        form->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Delay before sending (ms):")), 0, wxALIGN_CENTER_VERTICAL);
        m_delay = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 600000,
                                 initial ? static_cast<int>(initial->delayMs) : 0); form->Add(m_delay, 1, wxEXPAND);
        root->Add(form, 0, wxEXPAND | wxALL, 10);
        wxArrayString formats; formats.Add(wxS("Hex")); formats.Add(wxS("ASCII"));
        wxBoxSizer* requestHeader = new wxBoxSizer(wxHORIZONTAL);
        requestHeader->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("ask:")), 1, wxALIGN_CENTER_VERTICAL);
        m_requestFormat = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(90, -1), formats);
        m_requestFormat->SetSelection(initial && initial->requestAscii ? 1 : 0); requestHeader->Add(m_requestFormat);
        root->Add(requestHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        m_request = new wxTextCtrl(this, wxID_ANY, initial ? (initial->requestAscii ? AsciiText(initial->request) : HexText(initial->request, initial->request.size())) : wxString(),
                                   wxDefaultPosition, wxSize(-1, 130), wxTE_MULTILINE); m_request->SetMinSize(wxSize(-1, 110)); root->Add(m_request, 1, wxEXPAND | wxALL, 10);
        wxBoxSizer* responseHeader = new wxBoxSizer(wxHORIZONTAL);
        responseHeader->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Expected response (leave blank to indicate completion upon receipt of any non-empty data):")), 1, wxALIGN_CENTER_VERTICAL);
        m_responseFormat = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(90, -1), formats);
        m_responseFormat->SetSelection(initial && initial->expectedAscii ? 1 : 0); responseHeader->Add(m_responseFormat);
        root->Add(responseHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        m_response = new wxTextCtrl(this, wxID_ANY, initial ? (initial->expectedAscii ? AsciiText(initial->expected) : HexText(initial->expected, initial->expected.size())) : wxString(),
                                    wxDefaultPosition, wxSize(-1, 130), wxTE_MULTILINE); m_response->SetMinSize(wxSize(-1, 110)); root->Add(m_response, 1, wxEXPAND | wxALL, 10);
        m_verify = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Verify response content (only wait for the configured length when unchecked)"));
        m_verify->SetValue(initial ? initial->verify : true); root->Add(m_verify, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        root->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10); SetSizer(root);
    }

    bool GetExchange(Exchange& exchange, wxString& error) const
    {
        exchange.name = m_name->GetValue(); exchange.name.Trim(true).Trim(false);
        if (exchange.name.empty()) { error = ITOOL_TR("Name cannot be empty."); return false; }
        exchange.delayMs = static_cast<unsigned long>(m_delay->GetValue());
        exchange.verify = m_verify->GetValue();
        exchange.requestAscii = m_requestFormat->GetSelection() == 1;
        exchange.expectedAscii = m_responseFormat->GetSelection() == 1;
        if (!(exchange.requestAscii ? ParseAscii(m_request->GetValue(), exchange.request, error, false) : ParseHex(m_request->GetValue(), exchange.request, error, false))) return false;
        if (!(exchange.expectedAscii ? ParseAscii(m_response->GetValue(), exchange.expected, error, true) : ParseHex(m_response->GetValue(), exchange.expected, error, true))) return false;
        if (exchange.verify && exchange.expected.empty())
            { error = ITOOL_TR("When response validation is enabled, the expected response cannot be empty."); return false; }
        return true;
    }

private:
    wxTextCtrl *m_name, *m_request, *m_response; wxChoice *m_requestFormat, *m_responseFormat; wxSpinCtrl* m_delay; wxCheckBox* m_verify;
};

class Transport
{
public:
    virtual ~Transport() {}
    virtual void Cancel() = 0;
    virtual bool Open(const TestSettings& settings, std::atomic<bool>& stopping,
                      wxString& error) = 0;
    virtual bool Send(const std::vector<unsigned char>& data, bool text,
                      const std::atomic<bool>& stopping, wxString& error) = 0;
    virtual bool Receive(size_t expectedLength, unsigned long timeoutMs, std::atomic<bool>& stopping,
                         std::vector<unsigned char>& data, wxString& error) = 0;
};

bool WaitForConnect(wxSocketClient& socket, unsigned long timeoutMs,
                    const std::atomic<bool>& stopping)
{
    wxStopWatch watch;
    while (!stopping)
    {
        if (socket.IsConnected()) return true;
        const long elapsed = watch.Time();
        if (elapsed < 0 || static_cast<unsigned long>(elapsed) >= timeoutMs)
            break;
        const unsigned long remaining = timeoutMs - static_cast<unsigned long>(elapsed);
        const unsigned long slice = std::min<unsigned long>(50, remaining);
        socket.WaitOnConnect(slice / 1000, slice % 1000);
    }
    return !stopping && socket.IsConnected();
}

void InterruptSocket(wxSocketBase* socket)
{
    if (!socket) return;
#ifdef __WXMSW__
    ::shutdown(static_cast<SOCKET>(socket->GetSocket()), SD_BOTH);
#else
    ::shutdown(socket->GetSocket(), SHUT_RDWR);
#endif
}

class TcpTransport : public Transport
{
public:
    void Cancel() { InterruptSocket(m_socket.get()); }
    bool Open(const TestSettings& settings, std::atomic<bool>& stopping,
              wxString& error)
    {
        std::unique_ptr<wxIPaddress> address = ResolveAddress(settings.endpoint, settings.port, false, error);
        if (!address.get()) return false;
        // These sockets live entirely on the worker thread. On wxGTK,
        // wxSOCKET_NONE registers asynchronous notifications with the GUI
        // event loop and can race with socket destruction when the test ends.
        m_socket.reset(new wxSocketClient(wxSOCKET_BLOCK | wxSOCKET_NOWAIT));
        m_socket->Connect(*address, false);
        if (!WaitForConnect(*m_socket, settings.timeoutMs, stopping))
            { error = ITOOL_TR("TCP connection failed or timed out."); return false; }
        return true;
    }

    bool Send(const std::vector<unsigned char>& data, bool,
              const std::atomic<bool>& stopping, wxString& error)
    {
        size_t offset = 0;
        while (offset < data.size() && !stopping)
        {
            if (!m_socket->WaitForWrite(0, 50)) continue;
            m_socket->Write(&data[offset], data.size() - offset);
            const size_t written = m_socket->LastCount();
            if (!written || m_socket->Error()) { error = ITOOL_TR("TCP send failed."); return false; }
            offset += written;
        }
        return !stopping;
    }

    bool Receive(size_t expectedLength, unsigned long timeoutMs, std::atomic<bool>& stopping,
                 std::vector<unsigned char>& data, wxString& error)
    {
        wxStopWatch watch; unsigned char buffer[4096];
        while (!stopping && static_cast<unsigned long>(watch.Time()) < timeoutMs)
        {
            if (!m_socket->WaitForRead(0, 50)) continue;
            const size_t capacity = expectedLength ? std::min(sizeof(buffer), expectedLength - data.size()) : sizeof(buffer);
            m_socket->Read(buffer, capacity); const size_t count = m_socket->LastCount();
            if (m_socket->Error() && !count) { error = ITOOL_TR("TCP reception failed or the connection was dropped."); return false; }
            data.insert(data.end(), buffer, buffer + count);
            if ((!expectedLength && !data.empty()) || (expectedLength && data.size() >= expectedLength)) return true;
        }
        return false;
    }
private:
    std::unique_ptr<wxSocketClient> m_socket;
};

class UdpTransport : public Transport
{
public:
    UdpTransport() : m_ipv6(false) {}
    void Cancel() { InterruptSocket(m_socket.get()); }
    bool Open(const TestSettings& settings, std::atomic<bool>&,
              wxString& error)
    {
        m_remote = ResolveAddress(settings.endpoint, settings.port, false, error);
        if (!m_remote.get()) return false;
        m_ipv6 = m_remote->Type() == wxSockAddress::IPV6;
        wxString any = m_ipv6 ? wxS("::") : wxS("0.0.0.0");
        std::unique_ptr<wxIPaddress> local = ResolveAddress(any, settings.localPort, true, error);
        if (!local.get()) return false;
        m_socket.reset(new wxDatagramSocket(*local, wxSOCKET_BLOCK));
        if (!m_socket->IsOk()) { error = ITOOL_TR("Unable to open UDP local port (address family or port not available)."); return false; }
        return true;
    }

    bool Send(const std::vector<unsigned char>& data, bool,
              const std::atomic<bool>&, wxString& error)
    {
        m_socket->SendTo(*m_remote, &data[0], data.size());
        if (m_socket->Error() || m_socket->LastCount() != data.size()) { error = ITOOL_TR("UDP send failed."); return false; }
        return true;
    }

    bool Receive(size_t expectedLength, unsigned long timeoutMs, std::atomic<bool>& stopping,
                 std::vector<unsigned char>& data, wxString& error)
    {
        wxStopWatch watch; unsigned char buffer[65535];
        while (!stopping && static_cast<unsigned long>(watch.Time()) < timeoutMs)
        {
            if (!m_socket->WaitForRead(0, 50)) continue;
            std::unique_ptr<wxIPaddress> sender;
#if wxUSE_IPV6
            if (m_ipv6) sender.reset(new wxIPV6address);
            else
#endif
                sender.reset(new wxIPV4address);
            m_socket->RecvFrom(*sender, buffer, sizeof(buffer)); const size_t count = m_socket->LastCount();
            if (m_socket->Error()) { error = ITOOL_TR("UDP reception failed."); return false; }
            data.insert(data.end(), buffer, buffer + count);
            if ((!expectedLength && !data.empty()) || (expectedLength && data.size() >= expectedLength)) return true;
        }
        return false;
    }
private:
    std::unique_ptr<wxDatagramSocket> m_socket;
    std::unique_ptr<wxIPaddress> m_remote;
    bool m_ipv6;
};

class WebSocketTransport : public Transport
{
public:
    void Cancel() { InterruptSocket(m_socket.get()); }
    bool Open(const TestSettings& settings, std::atomic<bool>& stopping,
              wxString& error)
    {
        std::unique_ptr<wxIPaddress> address = ResolveAddress(settings.endpoint, settings.port, false, error);
        if (!address.get()) return false;
        m_socket.reset(new wxSocketClient(wxSOCKET_BLOCK));
        m_socket->Connect(*address, false);
        if (!WaitForConnect(*m_socket, settings.timeoutMs, stopping))
            { error = ITOOL_TR("WebSocket TCP connection failed or timed out."); return false; }
        return websocket::ClientHandshake(*m_socket, settings.endpoint, settings.port,
                                          settings.websocketPath, settings.timeoutMs,
                                          stopping, error);
    }
    bool Send(const std::vector<unsigned char>& data, bool text,
              const std::atomic<bool>&, wxString& error)
    {
        return text ? websocket::SendText(*m_socket, data, true, error)
                    : websocket::SendBinary(*m_socket, data, true, error);
    }
    bool Receive(size_t, unsigned long timeoutMs, std::atomic<bool>& stopping,
                 std::vector<unsigned char>& data, wxString& error)
    {
        return websocket::ReceiveMessage(*m_socket, timeoutMs, stopping, false, data, error);
    }
private:
    std::unique_ptr<wxSocketClient> m_socket;
};

class MqttTransport : public Transport
{
public:
    void Cancel() { InterruptSocket(m_socket.get()); }
    bool Open(const TestSettings& settings, std::atomic<bool>& stopping,
              wxString& error)
    {
        std::unique_ptr<wxIPaddress> address = ResolveAddress(settings.endpoint, settings.port, false, error);
        if (!address.get()) return false;
        m_socket.reset(new wxSocketClient(wxSOCKET_BLOCK)); m_socket->Connect(*address, false);
        if (!WaitForConnect(*m_socket, settings.timeoutMs, stopping))
            { error = ITOOL_TR("MQTT Broker connection failed or timed out."); return false; }
        m_publishTopic = std::string(settings.mqttPublishTopic.utf8_str());
        const std::string subscribeTopic(settings.mqttSubscribeTopic.utf8_str());
        const std::string clientId = "iTool-" + std::to_string(static_cast<unsigned long>(wxGetUTCTime()));
        return mqtt::ClientConnect(*m_socket, clientId, settings.timeoutMs, stopping, error) &&
               mqtt::ClientSubscribe(*m_socket, subscribeTopic, settings.timeoutMs, stopping, error);
    }
    bool Send(const std::vector<unsigned char>& data, bool,
              const std::atomic<bool>&, wxString& error)
    {
        return mqtt::Publish(*m_socket, m_publishTopic, data, error);
    }
    bool Receive(size_t, unsigned long timeoutMs, std::atomic<bool>& stopping,
                 std::vector<unsigned char>& data, wxString& error)
    {
        std::string topic; return mqtt::ReceivePublish(*m_socket, timeoutMs, stopping, topic, data, error);
    }
private:
    std::unique_ptr<wxSocketClient> m_socket;
    std::string m_publishTopic;
};

#ifdef __WXMSW__
class SerialTransport : public Transport
{
public:
    SerialTransport() : m_handle(INVALID_HANDLE_VALUE) {}
    ~SerialTransport() { if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle); }
    void Cancel() {}
    bool Open(const TestSettings& settings, std::atomic<bool>&,
              wxString& error)
    {
        wxString port = settings.endpoint;
        if (!port.StartsWith(wxS("\\\\.\\"))) port = wxS("\\\\.\\") + port;
        m_handle = CreateFileW(port.wc_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (m_handle == INVALID_HANDLE_VALUE) { error = ITOOL_TR("Unable to open serial port:") + settings.endpoint; return false; }
        DCB dcb; ZeroMemory(&dcb, sizeof(dcb)); dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(m_handle, &dcb)) { error = ITOOL_TR("Unable to read serial port configuration."); return false; }
        static const BYTE parityValues[] = { NOPARITY, ODDPARITY, EVENPARITY, MARKPARITY, SPACEPARITY };
        static const BYTE stopValues[] = { ONESTOPBIT, ONE5STOPBITS, TWOSTOPBITS };
        dcb.BaudRate = settings.baud; dcb.ByteSize = settings.dataBits;
        dcb.Parity = parityValues[settings.parity]; dcb.fParity = settings.parity != 0;
        dcb.StopBits = stopValues[settings.stopBits]; dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE; dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE; dcb.fInX = FALSE; dcb.fTXContinueOnXoff = TRUE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE; dcb.fRtsControl = RTS_CONTROL_ENABLE;
        if (settings.flowControl == 1) { dcb.fOutxCtsFlow = TRUE; dcb.fRtsControl = RTS_CONTROL_HANDSHAKE; }
        else if (settings.flowControl == 2) { dcb.fOutxDsrFlow = TRUE; dcb.fDtrControl = DTR_CONTROL_HANDSHAKE; }
        else if (settings.flowControl == 3) { dcb.fOutX = TRUE; dcb.fInX = TRUE; }
        if (!SetCommState(m_handle, &dcb)) { error = ITOOL_TR("Unable to set serial port parameters."); return false; }
        COMMTIMEOUTS timeouts; ZeroMemory(&timeouts, sizeof(timeouts));
        // Bound every synchronous driver call.  Stop is polled between calls,
        // so neither a silent device nor a full transmit queue can trap the
        // worker indefinitely inside a serial driver.
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 100;
        SetCommTimeouts(m_handle, &timeouts); PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }
    bool Send(const std::vector<unsigned char>& data, bool,
              const std::atomic<bool>& stopping, wxString& error)
    {
        DWORD written = 0;
        if (!WriteFile(m_handle, &data[0], static_cast<DWORD>(data.size()), &written, NULL) ||
            written != data.size())
            { error = ITOOL_TR("Serial port transmission failed."); return false; }
        return !stopping;
    }
    bool Receive(size_t expectedLength, unsigned long timeoutMs, std::atomic<bool>& stopping,
                 std::vector<unsigned char>& data, wxString& error)
    {
        wxStopWatch watch; unsigned char buffer[4096];
        while (!stopping && static_cast<unsigned long>(watch.Time()) < timeoutMs)
        {
            const DWORD capacity = static_cast<DWORD>(expectedLength ? std::min(sizeof(buffer), expectedLength - data.size()) : sizeof(buffer));
            DWORD received = 0;
            if (!ReadFile(m_handle, buffer, capacity, &received, NULL))
                { error = ITOOL_TR("Serial port reception failed."); return false; }
            if (received) data.insert(data.end(), buffer, buffer + received);
            if ((!expectedLength && !data.empty()) || (expectedLength && data.size() >= expectedLength)) return true;
            if (!received) wxMilliSleep(10);
        }
        return false;
    }
private:
    HANDLE m_handle;
};
#else
class SerialTransport : public Transport
{
public:
    // POSIX reads are already polled in 50 ms slices.  The descriptor remains
    // owned by the worker thread to avoid racing close() with poll()/write().
    void Cancel() {}
    bool Open(const TestSettings& settings, std::atomic<bool>&,
              wxString& error)
    {
        return m_port.Open(settings.endpoint, settings.baud, settings.dataBits,
                           settings.stopBits, settings.parity, settings.flowControl, error);
    }
    bool Send(const std::vector<unsigned char>& data, bool,
              const std::atomic<bool>&, wxString& error)
    {
        return m_port.Write(data, error);
    }
    bool Receive(size_t expectedLength, unsigned long timeoutMs, std::atomic<bool>& stopping,
                 std::vector<unsigned char>& data, wxString& error)
    {
        wxStopWatch watch; unsigned char buffer[4096];
        while (!stopping && static_cast<unsigned long>(watch.Time()) < timeoutMs)
        {
            const size_t capacity = expectedLength ? std::min(sizeof(buffer), expectedLength - data.size()) : sizeof(buffer);
            size_t received = 0;
            if (!m_port.Read(buffer, capacity, 50, received, error)) return false;
            if (received) data.insert(data.end(), buffer, buffer + received);
            if ((!expectedLength && !data.empty()) || (expectedLength && data.size() >= expectedLength)) return true;
        }
        return false;
    }
private:
    posixserial::Port m_port;
};
#endif

class TestThread : public wxThread
{
public:
    TestThread(wxEvtHandler* owner, const TestSettings& settings)
        : wxThread(wxTHREAD_JOINABLE), m_owner(owner), m_settings(settings),
          m_stopping(false), m_pendingProgressEvents(0), m_transport(NULL) {}
    void Stop()
    {
        m_stopping = true;
        std::lock_guard<std::mutex> lock(m_transportMutex);
        if (m_transport) m_transport->Cancel();
    }
    void ProgressEventHandled()
    {
        if (m_pendingProgressEvents.load() != 0) --m_pendingProgressEvents;
    }

protected:
    ExitCode Entry()
    {
        std::unique_ptr<Transport> transport;
        if (m_settings.protocol == ProtocolTcp) transport.reset(new TcpTransport);
        else if (m_settings.protocol == ProtocolUdp) transport.reset(new UdpTransport);
        else if (m_settings.protocol == ProtocolWebSocket) transport.reset(new WebSocketTransport);
        else if (m_settings.protocol == ProtocolMqtt) transport.reset(new MqttTransport);
        else transport.reset(new SerialTransport);
        {
            std::lock_guard<std::mutex> lock(m_transportMutex);
            m_transport = transport.get();
        }
        wxString openError;
        if (!transport.get() || !transport->Open(m_settings, m_stopping, openError))
        {
            UnregisterTransport();
            Finish(openError.empty() ? ITOOL_TR("Unable to create communication channel.") : openError); return static_cast<ExitCode>(0);
        }

        for (unsigned long cycle = 0; cycle < m_settings.cycles && !m_stopping; ++cycle)
        {
            bool cycleSucceeded = true;
            for (size_t pairIndex = 0; pairIndex < m_settings.exchanges.size() && !m_stopping; ++pairIndex)
            {
                const Exchange& exchange = m_settings.exchanges[pairIndex];
                if (exchange.delayMs && !WaitInterruptible(exchange.delayMs)) break;
                bool pairSucceeded = false;
                for (unsigned long retry = 0; retry <= m_settings.retries && !m_stopping; ++retry)
                {
                    AttemptResult result; result.name = exchange.name; result.success = false; result.mismatch = false;
                    result.timeout = false; result.sent = 0; result.received = 0;
                    result.txData = exchange.request;
                    wxString error;
                    if (!transport->Send(exchange.request, exchange.requestAscii,
                                         m_stopping, error))
                    {
                        result.detail = m_stopping ? ITOOL_TR("test stopped") : error;
                        Post(result); break;
                    }
                    result.sent = exchange.request.size();
                    std::vector<unsigned char> response;
                    const bool received = transport->Receive(exchange.expected.size(), m_settings.timeoutMs, m_stopping, response, error);
                    result.received = response.size(); result.rxData = response;
                    if (m_stopping)
                    {
                        result.detail = ITOOL_TR("test stopped"); Post(result); break;
                    }
                    if (!received)
                    {
                        result.timeout = error.empty(); result.detail = error.empty() ? ITOOL_TR("receive timeout") : error;
                    }
                    else if (exchange.verify && response != exchange.expected)
                    {
                        result.mismatch = true; result.detail = ITOOL_TR("Response does not match:") + HexText(response);
                    }
                    else
                    {
                        result.success = true; result.detail = ITOOL_TR("Success, RX:") + HexText(response); pairSucceeded = true;
                    }
                    Post(result);
                    if (pairSucceeded) break;
                }
                if (!pairSucceeded) cycleSucceeded = false;
                if (!pairSucceeded && m_settings.stopOnFailure)
                {
                    PostCycle(false); UnregisterTransport();
                    Finish(ITOOL_TR("Encountered failure and stopped as configured."));
                    return static_cast<ExitCode>(0);
                }
                if (m_settings.intervalMs)
                {
                    if (!WaitInterruptible(m_settings.intervalMs)) break;
                }
                else
                {
                    // Zero means no artificial delay, but it must not become a
                    // tight loop that consumes an entire scheduler quantum and
                    // starves the GUI thread sharing this process.
                    wxThread::Yield();
                }
            }
            if (!m_stopping) PostCycle(cycleSucceeded);
        }
        UnregisterTransport();
        Finish(m_stopping ? ITOOL_TR("Testing has been stopped.") : ITOOL_TR("Testing is complete."));
        return static_cast<ExitCode>(0);
    }

private:
    void UnregisterTransport()
    {
        std::lock_guard<std::mutex> lock(m_transportMutex);
        m_transport = NULL;
    }
    bool WaitInterruptible(unsigned long milliseconds)
    {
        unsigned long elapsed = 0;
        while (elapsed < milliseconds && !m_stopping)
        {
            const unsigned long slice = std::min<unsigned long>(50, milliseconds - elapsed);
            wxMilliSleep(slice); elapsed += slice;
        }
        return !m_stopping;
    }
    void Post(const AttemptResult& result)
    {
        if (!ReserveProgressEvent()) return;
        wxThreadEvent* event = new wxThreadEvent(EVT_COMM_ATTEMPT); event->SetPayload(result); wxQueueEvent(m_owner, event);
    }
    void Finish(const wxString& message)
    {
        wxThreadEvent* event = new wxThreadEvent(EVT_COMM_FINISHED); event->SetString(message); wxQueueEvent(m_owner, event);
    }
    void PostCycle(bool success)
    {
        if (!ReserveProgressEvent()) return;
        wxThreadEvent* event = new wxThreadEvent(EVT_COMM_CYCLE); event->SetInt(success ? 1 : 0); wxQueueEvent(m_owner, event);
    }
    bool ReserveProgressEvent()
    {
        // Never let a high-speed transport monopolize the GUI event queue.
        // Keeping this queue short ensures mouse/Stop events are dispatched
        // promptly even when the test itself can run thousands of cycles/s.
        while (!m_stopping && m_pendingProgressEvents.load() >= 16)
            wxMilliSleep(2);
        if (m_stopping) return false;
        ++m_pendingProgressEvents;
        return true;
    }
    wxEvtHandler* m_owner; TestSettings m_settings; std::atomic<bool> m_stopping;
    std::atomic<unsigned int> m_pendingProgressEvents;
    std::mutex m_transportMutex;
    Transport* m_transport;
};

class CommTestPanel : public wxPanel, public IStatefulPanel
{
public:
    explicit CommTestPanel(wxWindow* parent)
        : wxPanel(parent), m_thread(NULL), m_sent(0), m_valid(0), m_invalid(0), m_timeouts(0),
          m_completedCycles(0), m_successCycles(0)
    {
        Build(); Bind(EVT_COMM_ATTEMPT, &CommTestPanel::OnAttempt, this); Bind(EVT_COMM_CYCLE, &CommTestPanel::OnCycle, this);
        Bind(EVT_COMM_FINISHED, &CommTestPanel::OnFinished, this);
    }
    ~CommTestPanel() { StopThread(); }

private:
    void Build()
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* connection = new wxBoxSizer(wxHORIZONTAL);
        wxArrayString protocols; protocols.Add(wxS("TCP")); protocols.Add(wxS("UDP")); protocols.Add(wxS("WebSocket")); protocols.Add(wxS("MQTT"));
        const wxArrayString serialPorts = AvailableSerialPorts();
        for (size_t i = 0; i < serialPorts.size(); ++i) protocols.Add(serialPorts[i]);
        m_protocol = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), protocols, wxCB_READONLY);
        m_protocol->SetSelection(serialPorts.empty() ? 0 : 4);
        m_endpointLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Host:")); m_endpoint = new wxTextCtrl(this, wxID_ANY, wxS("127.0.0.1"), wxDefaultPosition, wxSize(150, -1));
        m_endpoint->SetToolTip(ITOOL_TR("Supports IPv4, IPv6 (e.g. ::1) and hostname"));
        m_portLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("port:")); m_port = new wxSpinCtrl(this, wxID_ANY, wxS("9000"), wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, 65535, 9000);
        m_localLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Local port:")); m_localPort = new wxSpinCtrl(this, wxID_ANY, wxS("0"), wxDefaultPosition, wxSize(75, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
        m_wsPathLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("path:"));
        m_wsPath = new wxTextCtrl(this, wxID_ANY, wxS("/"), wxDefaultPosition, wxSize(110, -1));
        m_wsPath->SetToolTip(ITOOL_TR("WebSocket request path, such as /ws; currently supports ws://, but does not support TLS/wss://"));
        m_mqttPubLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("release:")); m_mqttPub = new wxTextCtrl(this, wxID_ANY, wxS("itool/request"), wxDefaultPosition, wxSize(120, -1));
        m_mqttSubLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("subscription:")); m_mqttSub = new wxTextCtrl(this, wxID_ANY, wxS("itool/response"), wxDefaultPosition, wxSize(120, -1));
        wxArrayString bauds; bauds.Add(wxS("1200")); bauds.Add(wxS("2400")); bauds.Add(wxS("4800")); bauds.Add(wxS("9600")); bauds.Add(wxS("19200")); bauds.Add(wxS("38400")); bauds.Add(wxS("57600")); bauds.Add(wxS("115200")); bauds.Add(wxS("230400")); bauds.Add(wxS("460800")); bauds.Add(wxS("921600"));
        m_baudLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Baud rate:")); m_baud = new wxComboBox(this, wxID_ANY, wxS("9600"), wxDefaultPosition, wxSize(105, -1), bauds, wxCB_DROPDOWN);
        m_baud->SetToolTip(ITOOL_TR("Common baud rates can be selected or manually entered from 1 to 4000000."));
        m_stopOnFailure = new wxCheckBox(this, wxID_ANY, ITOOL_TR("Stop on failure"));
        connection->Add(m_protocol, 0, wxRIGHT, 7); connection->Add(m_endpointLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_endpoint, 0, wxRIGHT, 7);
        connection->Add(m_portLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_port, 0, wxRIGHT, 7);
        connection->Add(m_localLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_localPort, 0, wxRIGHT, 7);
        connection->Add(m_wsPathLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_wsPath, 0, wxRIGHT, 7);
        connection->Add(m_mqttPubLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_mqttPub, 0, wxRIGHT, 7);
        connection->Add(m_mqttSubLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_mqttSub, 0, wxRIGHT, 7);
        connection->Add(m_baudLabel, 0, wxALIGN_CENTER_VERTICAL); connection->Add(m_baud, 0);
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
        connection->Add(m_stopOnFailure, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

        wxBoxSizer* settings = new wxBoxSizer(wxHORIZONTAL);
        m_cycles = AddSpin(settings, ITOOL_TR("Number of tasks sent:"), 1, 100000000, 1, 90);
        m_interval = AddSpin(settings, ITOOL_TR("Sending interval (ms):"), 0, 600000, 0, 85);
        m_timeout = AddSpin(settings, ITOOL_TR("Timeout (ms):"), 1, 600000, 3000, 85);
        m_retries = AddSpin(settings, ITOOL_TR("Try again:"), 0, 1000, 0, 65);
        root->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        wxBoxSizer* body = new wxBoxSizer(wxHORIZONTAL);
        m_log = new CommunicationLog(this);
        m_log->InsertColumn(0, ITOOL_TR("serial number"), wxLIST_FORMAT_RIGHT, 55);
        m_log->InsertColumn(1, ITOOL_TR("Timestamp"), wxLIST_FORMAT_LEFT, 175);
        m_log->InsertColumn(2, ITOOL_TR("name"), wxLIST_FORMAT_LEFT, 100);
        m_log->InsertColumn(3, ITOOL_TR("result"), wxLIST_FORMAT_LEFT, 360);
        wxBoxSizer* logSide = new wxBoxSizer(wxVERTICAL);
        wxButton* copyLog = new wxButton(this, wxID_ANY, ITOOL_TR("Copy"));
        copyLog->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_log->CopyRecords(); });
        copyLog->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
            event.Enable(m_log->GetItemCount() > 0);
        });
        logSide->Add(copyLog, 0, wxALIGN_RIGHT | wxBOTTOM, 5);
        logSide->Add(m_log, 1, wxEXPAND);
        body->Add(logSide, 1, wxEXPAND | wxRIGHT, 8);
        wxBoxSizer* pairSide = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* pairButtons = new wxBoxSizer(wxHORIZONTAL);
        wxButton* add = new wxButton(this, wxID_ADD, wxS("+"), wxDefaultPosition, wxSize(28, -1), wxBU_EXACTFIT);
        wxButton* edit = new wxButton(this, wxID_EDIT, wxS("E"), wxDefaultPosition, wxSize(28, -1), wxBU_EXACTFIT);
        wxButton* remove = new wxButton(this, wxID_REMOVE, wxS("-"), wxDefaultPosition, wxSize(28, -1), wxBU_EXACTFIT);
        add->SetToolTip(ITOOL_TR("Add new send/receive pair")); edit->SetToolTip(ITOOL_TR("Edit selected send/receive pair")); remove->SetToolTip(ITOOL_TR("Delete selected send/receive pair"));
        wxButton* moveUp = new wxButton(this, wxID_UP, wxS("↑"), wxDefaultPosition, wxSize(26, -1), wxBU_EXACTFIT);
        wxButton* moveDown = new wxButton(this, wxID_DOWN, wxS("↓"), wxDefaultPosition, wxSize(26, -1), wxBU_EXACTFIT);
        moveUp->SetToolTip(ITOOL_TR("Move up: Adjust the execution order of send/receive pairs"));
        moveDown->SetToolTip(ITOOL_TR("Move down: Adjust the execution order of send/receive pairs"));
        pairButtons->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("send/receive pair")), 1, wxALIGN_CENTER_VERTICAL); pairButtons->Add(add, 0, wxRIGHT, 2); pairButtons->Add(edit, 0, wxRIGHT, 2); pairButtons->Add(remove, 0, wxRIGHT, 2); pairButtons->Add(moveUp, 0, wxRIGHT, 2); pairButtons->Add(moveDown);
        pairSide->Add(pairButtons, 0, wxEXPAND | wxBOTTOM, 5);
        m_pairs = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
        m_pairs->InsertColumn(0, ITOOL_TR("name"), wxLIST_FORMAT_LEFT, 100); m_pairs->InsertColumn(1, ITOOL_TR("delay"), wxLIST_FORMAT_RIGHT, 60);
        m_pairs->InsertColumn(2, ITOOL_TR("ask"), wxLIST_FORMAT_LEFT, 145); m_pairs->InsertColumn(3, ITOOL_TR("Response/verification"), wxLIST_FORMAT_LEFT, 150);
        pairSide->Add(m_pairs, 1, wxEXPAND); body->Add(pairSide, 1, wxEXPAND);
        root->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

        wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL);
        m_stats = new wxStaticText(this, wxID_ANY, ITOOL_TR("Sending frame: 0 Legal receiving: 0 Illegal receiving: 0 Timeout: 0 Successful task: 0/0 Success rate: 0.00%"));
        m_start = new wxButton(this, wxID_ANY, ITOOL_TR("start")); m_stop = new wxButton(this, wxID_ANY, ITOOL_TR("stop")); wxButton* reset = new wxButton(this, wxID_ANY, ITOOL_TR("reset")); m_stop->Disable();
        footer->Add(m_stats, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8); footer->Add(m_start, 0, wxRIGHT, 6); footer->Add(m_stop, 0, wxRIGHT, 6); footer->Add(reset);
        root->Add(footer, 0, wxEXPAND | wxALL, 8); SetSizer(root);

        m_protocol->Bind(wxEVT_COMBOBOX, &CommTestPanel::ProtocolChanged, this);
        m_protocol->Bind(wxEVT_COMBOBOX_DROPDOWN, &CommTestPanel::ProtocolDropdown, this); add->Bind(wxEVT_BUTTON, &CommTestPanel::AddPair, this);
        edit->Bind(wxEVT_BUTTON, &CommTestPanel::EditPair, this); remove->Bind(wxEVT_BUTTON, &CommTestPanel::RemovePair, this);
        moveUp->Bind(wxEVT_BUTTON, &CommTestPanel::MoveUp, this); moveDown->Bind(wxEVT_BUTTON, &CommTestPanel::MoveDown, this);
        m_pairs->Bind(wxEVT_LIST_ITEM_ACTIVATED, &CommTestPanel::EditPairList, this); m_start->Bind(wxEVT_BUTTON, &CommTestPanel::Start, this);
        m_stop->Bind(wxEVT_BUTTON, &CommTestPanel::Stop, this); reset->Bind(wxEVT_BUTTON, &CommTestPanel::Reset, this);
        ProtocolChangedDummy();
    }

    wxSpinCtrl* AddSpin(wxBoxSizer* sizer, const wxString& label, int minimum, int maximum, int value, int width)
    {
        sizer->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        wxSpinCtrl* control = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(width, -1), wxSP_ARROW_KEYS, minimum, maximum, value);
        sizer->Add(control, 0, wxRIGHT, 8); return control;
    }
    void ProtocolChanged(wxCommandEvent&) { ProtocolChangedDummy(); }
    void ProtocolDropdown(wxCommandEvent& event)
    {
        const wxString selected = m_protocol->GetStringSelection();
        m_protocol->Freeze(); m_protocol->Clear(); m_protocol->Append(wxS("TCP")); m_protocol->Append(wxS("UDP")); m_protocol->Append(wxS("WebSocket")); m_protocol->Append(wxS("MQTT"));
        const wxArrayString ports = AvailableSerialPorts();
        for (size_t i = 0; i < ports.size(); ++i) m_protocol->Append(ports[i]);
        const int restored = m_protocol->FindString(selected);
        m_protocol->SetSelection(restored == wxNOT_FOUND ? 0 : restored);
        m_protocol->Thaw();
        if (restored == wxNOT_FOUND) ProtocolChangedDummy();
        event.Skip();
    }
    Protocol SelectedProtocol() const
    {
        if (m_protocol->GetSelection() == 0) return ProtocolTcp;
        if (m_protocol->GetSelection() == 1) return ProtocolUdp;
        if (m_protocol->GetSelection() == 2) return ProtocolWebSocket;
        if (m_protocol->GetSelection() == 3) return ProtocolMqtt;
        return ProtocolSerial;
    }
    void ProtocolChangedDummy()
    {
        const Protocol protocol = SelectedProtocol(); const bool serial = protocol == ProtocolSerial, udp = protocol == ProtocolUdp, ws = protocol == ProtocolWebSocket, mqttProtocol = protocol == ProtocolMqtt;
        m_endpointLabel->SetLabel(mqttProtocol ? wxS("Broker：") : ITOOL_TR("Host:"));
        if (mqttProtocol && m_port->GetValue() == 9000) m_port->SetValue(1883);
        m_endpointLabel->Show(!serial); m_endpoint->Show(!serial);
        m_portLabel->Show(!serial); m_port->Show(!serial); m_localLabel->Show(udp); m_localPort->Show(udp); m_baudLabel->Show(serial); m_baud->Show(serial);
        m_wsPathLabel->Show(ws); m_wsPath->Show(ws);
        m_mqttPubLabel->Show(mqttProtocol); m_mqttPub->Show(mqttProtocol); m_mqttSubLabel->Show(mqttProtocol); m_mqttSub->Show(mqttProtocol);
        m_serialOptions->ShowItems(serial); Layout();
    }
    void RefreshPairs()
    {
        m_pairs->DeleteAllItems();
        for (size_t i = 0; i < m_exchanges.size(); ++i)
        {
            const long row = m_pairs->InsertItem(static_cast<long>(i), m_exchanges[i].name);
            m_pairs->SetItem(row, 1, wxString::Format(wxS("%lu"), m_exchanges[i].delayMs));
            m_pairs->SetItem(row, 2, (m_exchanges[i].requestAscii ? wxS("ASCII: ") + AsciiText(m_exchanges[i].request) : HexText(m_exchanges[i].request, 16)));
            wxString response = m_exchanges[i].expected.empty() ? ITOOL_TR("Any non-empty") :
                (m_exchanges[i].expectedAscii ? wxS("ASCII: ") + AsciiText(m_exchanges[i].expected) : HexText(m_exchanges[i].expected, 16));
            response += m_exchanges[i].verify ? ITOOL_TR("/check") : ITOOL_TR("/ No verification"); m_pairs->SetItem(row, 3, response);
        }
    }
    void AddPair(wxCommandEvent&)
    {
        ExchangeDialog dialog(this, NULL);
        if (dialog.ShowModal() == wxID_OK) { Exchange value; wxString error; if (!dialog.GetExchange(value, error)) { wxMessageBox(error); return; } m_exchanges.push_back(value); RefreshPairs(); }
    }
    void EditSelected()
    {
        const long selected = m_pairs->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); if (selected < 0) return;
        ExchangeDialog dialog(this, &m_exchanges[static_cast<size_t>(selected)]);
        if (dialog.ShowModal() == wxID_OK) { Exchange value; wxString error; if (!dialog.GetExchange(value, error)) { wxMessageBox(error); return; } m_exchanges[static_cast<size_t>(selected)] = value; RefreshPairs(); }
    }
    void EditPair(wxCommandEvent&) { EditSelected(); }
    void EditPairList(wxListEvent&) { EditSelected(); }
    void RemovePair(wxCommandEvent&)
    {
        const long selected = m_pairs->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); if (selected < 0) return;
        m_exchanges.erase(m_exchanges.begin() + selected); RefreshPairs();
    }
    void MoveSelected(int direction)
    {
        const long selected = m_pairs->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (selected < 0) return;
        const long target = selected + direction;
        if (target < 0 || target >= static_cast<long>(m_exchanges.size())) return;
        std::swap(m_exchanges[static_cast<size_t>(selected)], m_exchanges[static_cast<size_t>(target)]);
        RefreshPairs(); m_pairs->SetItemState(target, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                               wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
    void MoveUp(wxCommandEvent&) { MoveSelected(-1); }
    void MoveDown(wxCommandEvent&) { MoveSelected(1); }
    bool Settings(TestSettings& settings, wxString& error)
    {
        if (m_exchanges.empty()) { error = ITOOL_TR("Please add at least one send/receive pair."); return false; }
        settings.protocol = SelectedProtocol();
        settings.endpoint = settings.protocol == ProtocolSerial ? m_protocol->GetStringSelection() : m_endpoint->GetValue();
        settings.endpoint.Trim(true).Trim(false);
        if (settings.endpoint.empty()) { error = ITOOL_TR("The serial port or host cannot be empty."); return false; }
        settings.port = static_cast<unsigned short>(m_port->GetValue()); settings.localPort = static_cast<unsigned short>(m_localPort->GetValue());
        settings.websocketPath = m_wsPath->GetValue(); settings.websocketPath.Trim(true).Trim(false);
        if (settings.protocol == ProtocolWebSocket && settings.websocketPath.empty()) settings.websocketPath = wxS("/");
        settings.mqttPublishTopic = m_mqttPub->GetValue(); settings.mqttPublishTopic.Trim(true).Trim(false);
        settings.mqttSubscribeTopic = m_mqttSub->GetValue(); settings.mqttSubscribeTopic.Trim(true).Trim(false);
        if (settings.protocol == ProtocolMqtt && (settings.mqttPublishTopic.empty() || settings.mqttSubscribeTopic.empty()))
            { error = ITOOL_TR("MQTT publishing topics and subscription topics cannot be empty."); return false; }
        unsigned long baud = 0;
        if (!m_baud->GetValue().ToULong(&baud) || baud < 1 || baud > 4000000)
            { error = ITOOL_TR("The baud rate must be an integer between 1 and 4000000."); return false; }
        settings.baud = baud;
        unsigned long dataBits = 8; m_dataBits->GetStringSelection().ToULong(&dataBits); settings.dataBits = static_cast<unsigned char>(dataBits);
        settings.stopBits = m_stopBits->GetSelection(); settings.parity = m_parity->GetSelection(); settings.flowControl = m_flowControl->GetSelection();
        if (settings.protocol == ProtocolSerial && settings.stopBits == 1 && settings.dataBits != 5)
            { error = ITOOL_TR("1.5 stop bits can only be used in combination with 5 data bits."); return false; }
        if (settings.protocol == ProtocolSerial && settings.stopBits == 2 && settings.dataBits == 5)
            { error = ITOOL_TR("5 data bits cannot be combined with 2 stop bits."); return false; }
        settings.cycles = static_cast<unsigned long>(m_cycles->GetValue()); settings.intervalMs = static_cast<unsigned long>(m_interval->GetValue());
        settings.timeoutMs = static_cast<unsigned long>(m_timeout->GetValue()); settings.retries = static_cast<unsigned long>(m_retries->GetValue());
        settings.stopOnFailure = m_stopOnFailure->GetValue(); settings.exchanges = m_exchanges; return true;
    }
    void Start(wxCommandEvent&)
    {
        TestSettings settings; wxString error; if (!Settings(settings, error)) { wxMessageBox(error, ITOOL_TR("communication client"), wxOK | wxICON_WARNING, this); return; }
        m_logFile.reset(); m_logPath.clear();
        m_logStem = settings.protocol == ProtocolSerial
            ? settings.endpoint
            : settings.endpoint + wxString::Format(wxS("_%u"), static_cast<unsigned int>(settings.port));
        const wxString invalid = wxS("<>:\"/\\|?*");
        for (size_t i = 0; i < m_logStem.length(); ++i)
            if (invalid.Find(m_logStem[i]) != wxNOT_FOUND) m_logStem[i] = '_';
        m_thread = new TestThread(this, settings);
        if (m_thread->Run() != wxTHREAD_NO_ERROR) { delete m_thread; m_thread = NULL; wxMessageBox(ITOOL_TR("Unable to start test thread.")); return; }
        m_start->Disable(); m_stop->Enable();
    }
    void Stop(wxCommandEvent&)
    {
        if (!m_thread) return;
        m_stop->Disable();
        m_thread->Stop();
    }
    void StopThread()
    {
        if (m_thread) { m_thread->Stop(); m_thread->Wait(); delete m_thread; m_thread = NULL; }
    }
    void Reset(wxCommandEvent&)
    {
        if (m_thread) return;
        m_log->ClearRecords(); m_sent = m_valid = m_invalid = m_timeouts = 0;
        m_completedCycles = m_successCycles = 0; UpdateStats();
    }
    void OnAttempt(wxThreadEvent& event)
    {
        const AttemptResult result = event.GetPayload<AttemptResult>(); if (result.sent) ++m_sent;
        if (result.success) ++m_valid; else if (result.timeout) ++m_timeouts; else ++m_invalid;
        const wxDateTime now = wxDateTime::UNow();
        const long row = m_log->InsertItem(m_log->GetItemCount(), wxString::Format(wxS("%ld"), m_log->GetItemCount() + 1));
        m_log->SetItem(row, 1, FormatTimestamp(now)); m_log->SetItem(row, 2, result.name);
        wxString fullDetail = result.detail;
        if (result.success || result.mismatch)
            fullDetail = (result.success ? ITOOL_TR("Success, RX:") : ITOOL_TR("Response does not match:"))
                         + HexText(result.rxData, result.rxData.size());
        m_log->SetRecordText(row, 3, result.detail, fullDetail);
        WriteDataLog(result, now);
        m_log->EnsureVisible(row); UpdateStats();
        if (m_thread) m_thread->ProgressEventHandled();
    }
    void OnFinished(wxThreadEvent& event)
    {
        m_stop->Disable();
        CallAfter(&CommTestPanel::CompleteThread, event.GetString());
    }
    void CompleteThread(const wxString& message)
    {
        if (m_thread) { m_thread->Wait(); delete m_thread; m_thread = NULL; }
        m_logFile.reset(); m_logPath.clear();
        m_start->Enable(); m_stop->Disable(); SetToolTip(message);
    }
    void OnCycle(wxThreadEvent& event)
    {
        ++m_completedCycles; if (event.GetInt() != 0) ++m_successCycles; UpdateStats();
        if (m_thread) m_thread->ProgressEventHandled();
    }
    void UpdateStats()
    {
        const double rate = m_completedCycles ? 100.0 * static_cast<double>(m_successCycles) / static_cast<double>(m_completedCycles) : 0.0;
        m_stats->SetLabel(wxString::Format(ITOOL_TR("Sending frame: %lu Legal receiving: %lu Illegal receiving: %lu Timeout: %lu Successful task: %lu/%lu Success rate: %.2f%%"),
            m_sent, m_valid, m_invalid, m_timeouts, m_successCycles, m_completedCycles, rate));
    }
    void LoadCustomState(wxConfigBase& config, const wxString& prefix)
    {
        long count = 0; config.Read(prefix + wxS("/pairs/count"), &count, 0L); m_exchanges.clear();
        for (long i = 0; i < count; ++i)
        {
            const wxString base = prefix + wxString::Format(wxS("/pairs/%ld"), i); Exchange value; wxString request, expected, error;
            config.Read(base + wxS("/name"), &value.name, ITOOL_TR("test frame"));
            long delay = 0; config.Read(base + wxS("/delayMs"), &delay, 0L); value.delayMs = static_cast<unsigned long>(std::max(0L, delay));
            config.Read(base + wxS("/request"), &request); config.Read(base + wxS("/expected"), &expected);
            config.Read(base + wxS("/verify"), &value.verify, true);
            config.Read(base + wxS("/requestAscii"), &value.requestAscii, false);
            config.Read(base + wxS("/expectedAscii"), &value.expectedAscii, false);
            const bool requestOk = value.requestAscii ? ParseAscii(request, value.request, error, false) : ParseHex(request, value.request, error, false);
            const bool expectedOk = value.expectedAscii ? ParseAscii(expected, value.expected, error, true) : ParseHex(expected, value.expected, error, true);
            if (requestOk && expectedOk) m_exchanges.push_back(value);
        }
        RefreshPairs();
    }
    void SaveCustomState(wxConfigBase& config, const wxString& prefix) const
    {
        config.Write(prefix + wxS("/pairs/count"), static_cast<long>(m_exchanges.size()));
        for (size_t i = 0; i < m_exchanges.size(); ++i)
        {
            const wxString base = prefix + wxString::Format(wxS("/pairs/%lu"), static_cast<unsigned long>(i));
            config.Write(base + wxS("/name"), m_exchanges[i].name); config.Write(base + wxS("/delayMs"), static_cast<long>(m_exchanges[i].delayMs));
            config.Write(base + wxS("/request"), m_exchanges[i].requestAscii ? AsciiText(m_exchanges[i].request) : HexText(m_exchanges[i].request, m_exchanges[i].request.size()));
            config.Write(base + wxS("/expected"), m_exchanges[i].expectedAscii ? AsciiText(m_exchanges[i].expected) : HexText(m_exchanges[i].expected, m_exchanges[i].expected.size()));
            config.Write(base + wxS("/requestAscii"), m_exchanges[i].requestAscii);
            config.Write(base + wxS("/expectedAscii"), m_exchanges[i].expectedAscii);
            config.Write(base + wxS("/verify"), m_exchanges[i].verify);
        }
    }
    void WriteDataLog(const AttemptResult& result, const wxDateTime& now)
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
        wxString entry;
        if (result.sent)
            entry += timestamp + wxS(" [") + result.name + wxS("] TX: ") + HexText(result.txData, result.txData.size()) + wxS("\r\n");
        else
            entry += timestamp + wxS(" [") + result.name + wxS("] TX-ERROR: ") + result.detail + wxS("\r\n");
        if (!result.rxData.empty())
            entry += timestamp + wxS(" [") + result.name + wxS("] RX: ") + HexText(result.rxData, result.rxData.size()) + wxS("\r\n");
        entry += timestamp + wxS(" [") + result.name + wxS("] RESULT: ") + result.detail + wxS("\r\n");
        const wxCharBuffer utf8 = entry.utf8_str();
        if (utf8.length()) m_logFile->Write(utf8.data(), utf8.length());
    }

    wxComboBox *m_protocol, *m_baud; wxChoice *m_dataBits, *m_stopBits, *m_parity, *m_flowControl; wxSizer* m_serialOptions;
    wxStaticText *m_endpointLabel, *m_portLabel, *m_localLabel, *m_wsPathLabel, *m_mqttPubLabel, *m_mqttSubLabel, *m_baudLabel, *m_stats;
    wxTextCtrl *m_endpoint, *m_wsPath, *m_mqttPub, *m_mqttSub; wxSpinCtrl *m_port, *m_localPort, *m_cycles, *m_interval, *m_timeout, *m_retries;
    wxCheckBox* m_stopOnFailure; CommunicationLog* m_log; wxListCtrl* m_pairs; wxButton *m_start, *m_stop;
    std::vector<Exchange> m_exchanges; TestThread* m_thread;
    unsigned long m_sent, m_valid, m_invalid, m_timeouts, m_completedCycles, m_successCycles;
    wxString m_logStem;
    std::unique_ptr<wxFFile> m_logFile;
    wxString m_logPath;
};
}

wxString CommTestToolModule::GetId() const { return wxS("communication-test"); }
wxString CommTestToolModule::GetName() const { return ITOOL_TR("Communication client"); }
wxString CommTestToolModule::GetDescription() const { return ITOOL_TR("Run serial, TCP, UDP, WebSocket, or MQTT request and response tests"); }
wxWindow* CommTestToolModule::CreatePanel(wxWindow* parent) { return new CommTestPanel(parent); }
