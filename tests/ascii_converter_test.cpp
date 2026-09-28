#include "tools/ascii/AsciiConverter.h"
#include <iostream>
#include <wx/debug.h>
#include <cstdlib>

void FailOnAssert(const wxString&, int line, const wxString&, const wxString& condition,
                  const wxString& message)
{
    std::cerr << "wxWidgets assertion at line " << line << ": "
              << condition.ToStdString() << " " << message.ToStdString() << '\n';
    std::exit(1);
}

int main()
{
    wxSetAssertHandler(FailOnAssert);
    std::vector<unsigned char> bytes;
    for (unsigned int value = 0; value <= 255; ++value)
        bytes.push_back(static_cast<unsigned char>(value));
    wxString hex, error;
    const wxString text = asciiconv::BytesToText(bytes);
    std::vector<unsigned char> decoded;
    if (text.length() != 256 || !asciiconv::TextToHex(text, hex, error) ||
        !asciiconv::ParseHexBytes(hex, decoded, error) || decoded != bytes)
    {
        std::cerr << "Full byte-range round trip failed\n";
        return 1;
    }
    decoded.clear();
    if (!asciiconv::ParseHexBytes(wxS("0x7F, 80; FE\n0XFF"), decoded, error) ||
        decoded != std::vector<unsigned char>({0x7F, 0x80, 0xFE, 0xFF})) return 1;
    const char* invalid[] = {"GG", "100", "+1", "-1", "0x", ""};
    for (const char* value : invalid)
    {
        decoded.clear();
        if (asciiconv::ParseHexBytes(wxString::FromUTF8(value), decoded, error)) return 1;
    }
    if (asciiconv::TextToHex(wxString(wxUniChar(0x100)), hex, error)) return 1;
    return 0;
}
