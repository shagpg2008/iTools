#include "tools/comm/WebSocketProtocol.h"

#include "core/Localization.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

#include <wx/socket.h>
#include <wx/stopwatch.h>

#include <base64.h>
#include <filters.h>
#include <osrng.h>
#include <sha.h>

namespace websocket
{
namespace
{
const size_t kMaximumHeader = 16 * 1024;
const size_t kMaximumMessage = 16 * 1024 * 1024;

std::string Lower(std::string value)
{
    for (size_t i = 0; i < value.size(); ++i)
        value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    return value;
}

std::string Trim(const std::string& value)
{
    size_t first = 0, last = value.size();
    while (first < last && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

bool WriteAll(wxSocketBase& socket, const unsigned char* data, size_t size, wxString& error)
{
    size_t offset = 0;
    while (offset < size)
    {
        socket.Write(data + offset, size - offset);
        const size_t count = socket.LastCount();
        if (!count || socket.Error()) { error = ITOOL_TR("WebSocket send failed or the connection was disconnected."); return false; }
        offset += count;
    }
    return true;
}

bool ReadExact(wxSocketBase& socket, unsigned char* data, size_t size, unsigned long timeoutMs,
               const std::atomic<bool>& stopping, wxStopWatch& watch, wxString& error)
{
    size_t offset = 0;
    while (offset < size && !stopping)
    {
        const long elapsed = watch.Time();
        if (elapsed < 0 || static_cast<unsigned long>(elapsed) >= timeoutMs) return false;
        const unsigned long left = timeoutMs - static_cast<unsigned long>(elapsed);
        const unsigned long wait = std::min<unsigned long>(50, left);
        if (!socket.WaitForRead(wait / 1000, wait % 1000)) continue;
        socket.Read(data + offset, size - offset);
        const size_t count = socket.LastCount();
        if (!count && socket.Error()) { error = ITOOL_TR("WebSocket connection disconnected."); return false; }
        offset += count;
    }
    return offset == size;
}

bool ReadHttpHeader(wxSocketBase& socket, unsigned long timeoutMs,
                    const std::atomic<bool>& stopping, std::string& header,
                    wxString& error)
{
    wxStopWatch watch;
    while (!stopping && header.find("\r\n\r\n") == std::string::npos &&
           header.size() < kMaximumHeader)
    {
        if (static_cast<unsigned long>(watch.Time()) >= timeoutMs) { error = ITOOL_TR("WebSocket handshake timed out."); return false; }
        if (!socket.WaitForRead(0, 50)) continue;
        char buffer[1024]; socket.Read(buffer, sizeof(buffer)); const size_t count = socket.LastCount();
        if (!count && socket.Error()) { error = ITOOL_TR("WebSocket handshake connection broken."); return false; }
        header.append(buffer, count);
    }
    if (header.find("\r\n\r\n") == std::string::npos) { error = ITOOL_TR("WebSocket HTTP header is too large or incomplete."); return false; }
    return true;
}

bool ParseHeader(const std::string& source, std::string& firstLine,
                 std::map<std::string, std::string>& fields)
{
    std::istringstream stream(source); std::string line;
    if (!std::getline(stream, firstLine)) return false;
    if (!firstLine.empty() && firstLine[firstLine.size() - 1] == '\r') firstLine.resize(firstLine.size() - 1);
    while (std::getline(stream, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
        if (line.empty()) break;
        const size_t colon = line.find(':'); if (colon == std::string::npos) continue;
        fields[Lower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
    }
    return true;
}

bool HasToken(const std::string& value, const std::string& token)
{
    std::istringstream stream(Lower(value)); std::string item;
    while (std::getline(stream, item, ',')) if (Trim(item) == Lower(token)) return true;
    return false;
}

bool SendFrame(wxSocketBase& socket, unsigned char opcode, const std::vector<unsigned char>& payload,
               bool mask, wxString& error)
{
    std::vector<unsigned char> frame; frame.reserve(payload.size() + 14);
    frame.push_back(static_cast<unsigned char>(0x80 | opcode));
    const unsigned char maskBit = mask ? 0x80 : 0;
    if (payload.size() <= 125) frame.push_back(static_cast<unsigned char>(maskBit | payload.size()));
    else if (payload.size() <= 0xffff)
    {
        frame.push_back(static_cast<unsigned char>(maskBit | 126));
        frame.push_back(static_cast<unsigned char>((payload.size() >> 8) & 0xff));
        frame.push_back(static_cast<unsigned char>(payload.size() & 0xff));
    }
    else
    {
        frame.push_back(static_cast<unsigned char>(maskBit | 127));
        const unsigned long long size = static_cast<unsigned long long>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(static_cast<unsigned char>((size >> shift) & 0xff));
    }
    unsigned char key[4] = {0, 0, 0, 0};
    if (mask)
    {
        CryptoPP::AutoSeededRandomPool random; random.GenerateBlock(key, sizeof(key));
        frame.insert(frame.end(), key, key + 4);
    }
    const size_t payloadOffset = frame.size(); frame.insert(frame.end(), payload.begin(), payload.end());
    if (mask) for (size_t i = 0; i < payload.size(); ++i) frame[payloadOffset + i] ^= key[i % 4];
    return WriteAll(socket, &frame[0], frame.size(), error);
}
}

std::string MakeAcceptKey(const std::string& clientKey)
{
    const std::string input = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string digest, encoded;
    CryptoPP::SHA1 hash;
    CryptoPP::StringSource(input, true,
        new CryptoPP::HashFilter(hash, new CryptoPP::StringSink(digest)));
    CryptoPP::StringSource(digest, true,
        new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), false));
    return encoded;
}

bool ClientHandshake(wxSocketBase& socket, const wxString& host, unsigned short port,
                     const wxString& requestedPath, unsigned long timeoutMs,
                     const std::atomic<bool>& stopping, wxString& error)
{
    unsigned char randomBytes[16]; CryptoPP::AutoSeededRandomPool random; random.GenerateBlock(randomBytes, sizeof(randomBytes));
    std::string key;
    CryptoPP::StringSource(randomBytes, sizeof(randomBytes), true,
        new CryptoPP::Base64Encoder(new CryptoPP::StringSink(key), false));
    wxString path = requestedPath; path.Trim(true).Trim(false); if (path.empty()) path = wxS("/"); if (!path.StartsWith(wxS("/"))) path = wxS("/") + path;
    const std::string request = "GET " + std::string(path.utf8_str()) + " HTTP/1.1\r\nHost: " +
        std::string(host.utf8_str()) + ":" + std::to_string(port) +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
        "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!WriteAll(socket, reinterpret_cast<const unsigned char*>(request.data()), request.size(), error)) return false;
    std::string response; if (!ReadHttpHeader(socket, timeoutMs, stopping, response, error)) return false;
    std::string first; std::map<std::string, std::string> fields;
    if (!ParseHeader(response, first, fields) || first.find(" 101 ") == std::string::npos ||
        Lower(fields["upgrade"]) != "websocket" || !HasToken(fields["connection"], "upgrade") ||
        fields["sec-websocket-accept"] != MakeAcceptKey(key))
        { error = ITOOL_TR("The server rejected the WebSocket upgrade or the handshake response was invalid."); return false; }
    return true;
}

bool ServerHandshake(wxSocketBase& socket, const wxString& requiredPath,
                     unsigned long timeoutMs, const std::atomic<bool>& stopping,
                     wxString& error)
{
    std::string request; if (!ReadHttpHeader(socket, timeoutMs, stopping, request, error)) return false;
    std::string first; std::map<std::string, std::string> fields;
    if (!ParseHeader(request, first, fields) || first.compare(0, 4, "GET ") != 0 ||
        Lower(fields["upgrade"]) != "websocket" || !HasToken(fields["connection"], "upgrade") ||
        fields["sec-websocket-version"] != "13" || fields["sec-websocket-key"].empty())
        { error = ITOOL_TR("The request received was not a valid WebSocket RFC 6455 handshake."); return false; }
    const size_t pathEnd = first.find(' ', 4); const std::string actualPath = pathEnd == std::string::npos ? "" : first.substr(4, pathEnd - 4);
    wxString wanted = requiredPath; wanted.Trim(true).Trim(false); if (wanted.empty()) wanted = wxS("/"); if (!wanted.StartsWith(wxS("/"))) wanted = wxS("/") + wanted;
    if (actualPath != std::string(wanted.utf8_str()))
    {
        const std::string response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        WriteAll(socket, reinterpret_cast<const unsigned char*>(response.data()), response.size(), error);
        error = ITOOL_TR("WebSocket path mismatch."); return false;
    }
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
        MakeAcceptKey(fields["sec-websocket-key"]) + "\r\n\r\n";
    return WriteAll(socket, reinterpret_cast<const unsigned char*>(response.data()), response.size(), error);
}

bool SendBinary(wxSocketBase& socket, const std::vector<unsigned char>& data,
                bool clientMask, wxString& error)
{
    return SendFrame(socket, 0x2, data, clientMask, error);
}

bool SendText(wxSocketBase& socket, const std::vector<unsigned char>& data,
              bool clientMask, wxString& error)
{
    return SendFrame(socket, 0x1, data, clientMask, error);
}

bool ReceiveMessage(wxSocketBase& socket, unsigned long timeoutMs,
                    const std::atomic<bool>& stopping, bool expectClientMask,
                    std::vector<unsigned char>& data, wxString& error)
{
    wxStopWatch watch; bool started = false;
    while (!stopping)
    {
        unsigned char header[2];
        if (!ReadExact(socket, header, 2, timeoutMs, stopping, watch, error)) return false;
        const bool fin = (header[0] & 0x80) != 0, masked = (header[1] & 0x80) != 0;
        const unsigned char opcode = header[0] & 0x0f;
        if ((header[0] & 0x70) != 0 || masked != expectClientMask)
            { error = ITOOL_TR("Invalid WebSocket frame format or mask direction."); return false; }
        unsigned long long length = header[1] & 0x7f;
        if (length == 126)
        {
            unsigned char extended[2]; if (!ReadExact(socket, extended, 2, timeoutMs, stopping, watch, error)) return false;
            length = (static_cast<unsigned long long>(extended[0]) << 8) | extended[1];
        }
        else if (length == 127)
        {
            unsigned char extended[8]; if (!ReadExact(socket, extended, 8, timeoutMs, stopping, watch, error)) return false;
            length = 0; for (int i = 0; i < 8; ++i) length = (length << 8) | extended[i];
        }
        if (length > kMaximumMessage || data.size() + static_cast<size_t>(length) > kMaximumMessage)
            { error = ITOOL_TR("WebSocket message exceeds 16 MiB limit."); return false; }
        unsigned char mask[4] = {0, 0, 0, 0};
        if (masked && !ReadExact(socket, mask, 4, timeoutMs, stopping, watch, error)) return false;
        std::vector<unsigned char> payload(static_cast<size_t>(length));
        if (length && !ReadExact(socket, &payload[0], payload.size(), timeoutMs, stopping, watch, error)) return false;
        if (masked) for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
        if (opcode == 0x8) { SendFrame(socket, 0x8, payload, !expectClientMask, error); error = ITOOL_TR("The WebSocket peer has closed the connection."); return false; }
        if (opcode == 0x9) { if (!SendFrame(socket, 0xA, payload, !expectClientMask, error)) return false; continue; }
        if (opcode == 0xA) continue;
        if (opcode == 0x1 || opcode == 0x2) { if (started) { error = ITOOL_TR("Invalid WebSocket fragment sequence."); return false; } started = true; }
        else if (opcode != 0x0 || !started) { error = ITOOL_TR("Invalid WebSocket opcode."); return false; }
        data.insert(data.end(), payload.begin(), payload.end());
        if (fin) return true;
    }
    return false;
}
}
