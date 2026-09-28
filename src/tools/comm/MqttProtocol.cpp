#include "tools/comm/MqttProtocol.h"

#include "core/Localization.h"

#include <algorithm>

#include <wx/socket.h>
#include <wx/stopwatch.h>

namespace mqtt
{
namespace
{
const size_t kMaximumPacket = 16 * 1024 * 1024;

bool WriteAll(wxSocketBase& socket, const unsigned char* data, size_t size, wxString& error)
{
    size_t offset = 0;
    while (offset < size)
    {
        socket.Write(data + offset, size - offset); const size_t count = socket.LastCount();
        if (!count || socket.Error()) { error = ITOOL_TR("MQTT send failed or the connection was disconnected."); return false; }
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
        const long elapsed = watch.Time(); if (elapsed < 0 || static_cast<unsigned long>(elapsed) >= timeoutMs) return false;
        const unsigned long wait = std::min<unsigned long>(50, timeoutMs - static_cast<unsigned long>(elapsed));
        if (!socket.WaitForRead(wait / 1000, wait % 1000)) continue;
        socket.Read(data + offset, size - offset); const size_t count = socket.LastCount();
        if (!count && socket.Error()) { error = ITOOL_TR("MQTT connection disconnected."); return false; }
        offset += count;
    }
    return offset == size;
}

bool ReadPacket(wxSocketBase& socket, unsigned long timeoutMs, const std::atomic<bool>& stopping,
                unsigned char& header, std::vector<unsigned char>& body, wxString& error)
{
    wxStopWatch watch;
    if (!ReadExact(socket, &header, 1, timeoutMs, stopping, watch, error)) return false;
    size_t length = 0, multiplier = 1;
    for (int i = 0; i < 4; ++i)
    {
        unsigned char byte = 0; if (!ReadExact(socket, &byte, 1, timeoutMs, stopping, watch, error)) return false;
        length += static_cast<size_t>(byte & 127) * multiplier;
        if (!(byte & 128)) break;
        if (i == 3) { error = ITOOL_TR("MQTT Remaining Length is invalid."); return false; }
        multiplier *= 128;
    }
    if (length > kMaximumPacket) { error = ITOOL_TR("MQTT message exceeds 16 MiB limit."); return false; }
    body.resize(length);
    return !length || ReadExact(socket, &body[0], length, timeoutMs, stopping, watch, error);
}

void PutString(std::vector<unsigned char>& target, const std::string& value)
{
    target.push_back(static_cast<unsigned char>((value.size() >> 8) & 0xff));
    target.push_back(static_cast<unsigned char>(value.size() & 0xff));
    target.insert(target.end(), value.begin(), value.end());
}

bool GetString(const std::vector<unsigned char>& source, size_t& offset, std::string& value)
{
    if (offset + 2 > source.size()) return false;
    const size_t length = (static_cast<size_t>(source[offset]) << 8) | source[offset + 1]; offset += 2;
    if (offset + length > source.size()) return false;
    value.assign(reinterpret_cast<const char*>(&source[offset]), length); offset += length; return true;
}

bool SendPacket(wxSocketBase& socket, unsigned char header, const std::vector<unsigned char>& body, wxString& error)
{
    std::vector<unsigned char> packet; packet.push_back(header);
    const std::vector<unsigned char> length = EncodeRemainingLength(body.size());
    packet.insert(packet.end(), length.begin(), length.end()); packet.insert(packet.end(), body.begin(), body.end());
    return WriteAll(socket, &packet[0], packet.size(), error);
}
}

std::vector<unsigned char> EncodeRemainingLength(size_t length)
{
    std::vector<unsigned char> result;
    do { unsigned char byte = static_cast<unsigned char>(length % 128); length /= 128; if (length) byte |= 128; result.push_back(byte); } while (length);
    return result;
}

bool ClientConnect(wxSocketBase& socket, const std::string& clientId,
                   unsigned long timeoutMs, const std::atomic<bool>& stopping, wxString& error)
{
    std::vector<unsigned char> body; PutString(body, "MQTT"); body.push_back(4); body.push_back(2);
    // Keep Alive 0 avoids requiring a background PINGREQ timer in the
    // synchronous request/response test transport.
    body.push_back(0); body.push_back(0); PutString(body, clientId);
    if (!SendPacket(socket, 0x10, body, error)) return false;
    unsigned char header = 0; body.clear();
    if (!ReadPacket(socket, timeoutMs, stopping, header, body, error)) return false;
    if (header != 0x20 || body.size() != 2 || body[1] != 0)
        { error = ITOOL_TR("MQTT Broker refused connection or invalid CONNACK."); return false; }
    return true;
}

bool ClientSubscribe(wxSocketBase& socket, const std::string& topic,
                     unsigned long timeoutMs, const std::atomic<bool>& stopping, wxString& error)
{
    if (topic.empty() || topic.size() > 0xffff) { error = ITOOL_TR("The MQTT subscription topic is invalid."); return false; }
    std::vector<unsigned char> body; body.push_back(0); body.push_back(1); PutString(body, topic); body.push_back(0);
    if (!SendPacket(socket, 0x82, body, error)) return false;
    unsigned char header = 0; body.clear();
    if (!ReadPacket(socket, timeoutMs, stopping, header, body, error)) return false;
    if (header != 0x90 || body.size() < 3 || body[0] != 0 || body[1] != 1 || body[2] == 0x80)
        { error = ITOOL_TR("The MQTT SUBACK is invalid or the subscription was refused."); return false; }
    return true;
}

bool Publish(wxSocketBase& socket, const std::string& topic,
             const std::vector<unsigned char>& payload, wxString& error)
{
    if (topic.empty() || topic.size() > 0xffff) { error = ITOOL_TR("The MQTT publish topic is invalid."); return false; }
    std::vector<unsigned char> body; PutString(body, topic); body.insert(body.end(), payload.begin(), payload.end());
    return SendPacket(socket, 0x30, body, error);
}

bool ReceivePublish(wxSocketBase& socket, unsigned long timeoutMs,
                    const std::atomic<bool>& stopping, std::string& topic,
                    std::vector<unsigned char>& payload, wxString& error)
{
    wxStopWatch total;
    while (!stopping && static_cast<unsigned long>(total.Time()) < timeoutMs)
    {
        unsigned char header = 0; std::vector<unsigned char> body;
        const unsigned long remaining = timeoutMs - static_cast<unsigned long>(total.Time());
        if (!ReadPacket(socket, remaining, stopping, header, body, error)) return false;
        const unsigned char type = header >> 4;
        if (type == 3)
        {
            size_t offset = 0; if (!GetString(body, offset, topic)) { error = ITOOL_TR("The MQTT PUBLISH topic is invalid."); return false; }
            const unsigned char qos = (header >> 1) & 3;
            if (qos && offset + 2 <= body.size()) offset += 2;
            payload.assign(body.begin() + offset, body.end()); return true;
        }
        if (type == 8)
        {
            if (body.size() < 5) { error = ITOOL_TR("The MQTT SUBSCRIBE message is invalid."); return false; }
            std::vector<unsigned char> ack; ack.push_back(body[0]); ack.push_back(body[1]); ack.push_back(0);
            if (!SendPacket(socket, 0x90, ack, error)) return false;
            continue;
        }
        if (type == 12) { if (!SendPacket(socket, 0xD0, std::vector<unsigned char>(), error)) return false; continue; }
        if (type == 14) { error = ITOOL_TR("MQTT client disconnected."); return false; }
    }
    return false;
}

bool ServerAcceptConnect(wxSocketBase& socket, unsigned long timeoutMs,
                         const std::atomic<bool>& stopping, wxString& error)
{
    unsigned char header = 0; std::vector<unsigned char> body;
    if (!ReadPacket(socket, timeoutMs, stopping, header, body, error)) return false;
    size_t offset = 0; std::string protocol;
    if (header != 0x10 || !GetString(body, offset, protocol) || protocol != "MQTT" ||
        offset + 4 > body.size() || body[offset] != 4)
        { error = ITOOL_TR("The request received was not a valid MQTT 3.1.1 CONNECT."); return false; }
    std::vector<unsigned char> ack; ack.push_back(0); ack.push_back(0);
    return SendPacket(socket, 0x20, ack, error);
}
}
