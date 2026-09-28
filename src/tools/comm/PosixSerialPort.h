#pragma once

#include "core/Localization.h"

#ifndef __WXMSW__

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#ifdef __linux__
#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <sys/ioctl.h>
#elif defined(__APPLE__)
#include <IOKit/serial/ioss.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include <wx/arrstr.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/string.h>

namespace posixserial
{
inline wxString SystemError(const wxString& action)
{
    return action + wxS(": ") + wxString::FromUTF8(std::strerror(errno));
}

inline void AddMatchingPorts(const wxString& pattern, wxArrayString& ports)
{
    wxDir directory(wxS("/dev"));
    if (!directory.IsOpened()) return;
    wxString name;
    bool found = directory.GetFirst(&name, pattern, wxDIR_FILES);
    while (found)
    {
        const wxString path = wxFileName(wxS("/dev"), name).GetFullPath();
        if (ports.Index(path) == wxNOT_FOUND) ports.Add(path);
        found = directory.GetNext(&name);
    }
}

inline wxArrayString AvailablePorts()
{
    wxArrayString ports;
#ifdef __linux__
    AddMatchingPorts(wxS("ttyS*"), ports);
    AddMatchingPorts(wxS("ttyUSB*"), ports);
    AddMatchingPorts(wxS("ttyACM*"), ports);
    AddMatchingPorts(wxS("ttyAMA*"), ports);
    AddMatchingPorts(wxS("rfcomm*"), ports);
#elif defined(__APPLE__)
    AddMatchingPorts(wxS("cu.*"), ports);
    AddMatchingPorts(wxS("tty.*"), ports);
#endif
    ports.Sort();
    return ports;
}

class Port
{
public:
    Port() : m_fd(-1) {}
    ~Port() { Close(); }

    bool Open(const wxString& path, unsigned long baud, unsigned char dataBits,
              int stopBits, int parity, int flowControl, wxString& error)
    {
        Close();
        if (stopBits == 1)
        {
            error = ITOOL_TR("Linux/macOS serial ports do not support 1.5 stop bits.");
            return false;
        }
        if (flowControl == 2)
        {
            error = ITOOL_TR("Linux/macOS serial port does not support DTR/DSR flow control, please select RTS/CTS, XON/XOFF or None.");
            return false;
        }
#ifdef __APPLE__
        if (parity == 3 || parity == 4)
        {
            error = ITOOL_TR("The macOS serial port does not support mark/space parity.");
            return false;
        }
#endif
        const wxCharBuffer encoded = path.fn_str();
        m_fd = ::open(encoded.data(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (m_fd < 0) { error = SystemError(ITOOL_TR("Unable to open serial port") + path); return false; }

#ifdef __linux__
        struct termios2 options;
        if (::ioctl(m_fd, TCGETS2, &options) != 0)
            return Fail(SystemError(ITOOL_TR("Unable to read serial port configuration")), error);
        options.c_iflag = flowControl == 3 ? static_cast<tcflag_t>(IXON | IXOFF) : 0;
        options.c_oflag = 0;
        options.c_lflag = 0;
        options.c_cflag = CLOCAL | CREAD | BOTHER;
        switch (dataBits) {
        case 5: options.c_cflag |= CS5; break; case 6: options.c_cflag |= CS6; break;
        case 7: options.c_cflag |= CS7; break; default: options.c_cflag |= CS8; break;
        }
        if (stopBits == 2) options.c_cflag |= CSTOPB;
        if (parity == 1) options.c_cflag |= PARENB | PARODD;
        else if (parity == 2) options.c_cflag |= PARENB;
        else if (parity == 3) options.c_cflag |= PARENB | PARODD | CMSPAR;
        else if (parity == 4) options.c_cflag |= PARENB | CMSPAR;
        if (flowControl == 1) options.c_cflag |= CRTSCTS;
        options.c_ispeed = baud;
        options.c_ospeed = baud;
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;
        if (::ioctl(m_fd, TCSETS2, &options) != 0)
            return Fail(SystemError(ITOOL_TR("Unable to set serial port parameters or baud rate")), error);
        ::ioctl(m_fd, TCFLSH, TCIOFLUSH);
#elif defined(__APPLE__)
        struct termios options;
        if (::tcgetattr(m_fd, &options) != 0)
            return Fail(SystemError(ITOOL_TR("Unable to read serial port configuration")), error);
        ::cfmakeraw(&options);
        options.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
        options.c_cflag |= CLOCAL | CREAD;
        switch (dataBits) {
        case 5: options.c_cflag |= CS5; break; case 6: options.c_cflag |= CS6; break;
        case 7: options.c_cflag |= CS7; break; default: options.c_cflag |= CS8; break;
        }
        if (stopBits == 2) options.c_cflag |= CSTOPB;
        if (parity == 1) options.c_cflag |= PARENB | PARODD;
        else if (parity == 2) options.c_cflag |= PARENB;
        if (flowControl == 1) options.c_cflag |= CRTSCTS;
        if (flowControl == 3) options.c_iflag |= IXON | IXOFF;
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;
        if (::tcsetattr(m_fd, TCSANOW, &options) != 0)
            return Fail(SystemError(ITOOL_TR("Unable to set serial port parameters")), error);
        speed_t speed = static_cast<speed_t>(baud);
        if (::ioctl(m_fd, IOSSIOSPEED, &speed) != 0)
            return Fail(SystemError(ITOOL_TR("Unable to set custom baud rate")), error);
        ::tcflush(m_fd, TCIOFLUSH);
#endif
        return true;
    }

    bool Write(const std::vector<unsigned char>& data, wxString& error)
    {
        size_t offset = 0;
        while (offset < data.size())
        {
            struct pollfd descriptor = { m_fd, POLLOUT, 0 };
            const int ready = ::poll(&descriptor, 1, 1000);
            if (ready <= 0) { error = ready == 0 ? ITOOL_TR("Serial port transmission timeout.") : SystemError(ITOOL_TR("Serial port sending and waiting failed")); return false; }
            const ssize_t count = ::write(m_fd, &data[offset], data.size() - offset);
            if (count < 0) { if (errno == EINTR || errno == EAGAIN) continue; error = SystemError(ITOOL_TR("Serial port transmission failed")); return false; }
            offset += static_cast<size_t>(count);
        }
        return true;
    }

    bool Read(unsigned char* buffer, size_t capacity, int timeoutMs, size_t& count, wxString& error)
    {
        count = 0;
        struct pollfd descriptor = { m_fd, POLLIN, 0 };
        int ready;
        do { ready = ::poll(&descriptor, 1, timeoutMs); } while (ready < 0 && errno == EINTR);
        if (ready == 0) return true;
        if (ready < 0) { error = SystemError(ITOOL_TR("Serial port reception and waiting failed")); return false; }
        const ssize_t received = ::read(m_fd, buffer, capacity);
        if (received < 0)
        {
            if (errno == EINTR || errno == EAGAIN) return true;
            error = SystemError(ITOOL_TR("Serial port reception failed")); return false;
        }
        count = static_cast<size_t>(received);
        return true;
    }

private:
    bool Fail(const wxString& message, wxString& error) { error = message; Close(); return false; }
    void Close() { if (m_fd >= 0) { ::close(m_fd); m_fd = -1; } }
    int m_fd;
};
}

#endif
