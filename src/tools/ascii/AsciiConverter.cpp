#include "tools/ascii/AsciiConverter.h"
#include "core/Localization.h"
#include <wx/tokenzr.h>
#include <iomanip>
#include <sstream>

namespace asciiconv
{
bool ParseHexBytes(const wxString& input,
                   std::vector<unsigned char>& bytes,
                   wxString& error)
{
    wxString normalized = input;
    normalized.Replace(",", " ");
    normalized.Replace(";", " ");
    normalized.Replace("\t", " ");
    normalized.Replace("\r", " ");
    normalized.Replace("\n", " ");

    wxStringTokenizer tokenizer(normalized, " ", wxTOKEN_STRTOK);
    size_t index = 0;
    while (tokenizer.HasMoreTokens())
    {
        wxString token = tokenizer.GetNextToken();
        ++index;

        if (token.StartsWith("0x") || token.StartsWith("0X"))
        {
            token = token.Mid(2);
        }

        if (token.length() != 2)
        {
            error = wxString::Format(
                ITOOL_TR("Item %lu, “%s”, is not a two-digit hexadecimal byte."),
                static_cast<unsigned long>(index), token);
            return false;
        }

        unsigned long value = 0;
        if (token.find_first_not_of(wxS("0123456789abcdefABCDEF")) != wxString::npos ||
            !token.ToULong(&value, 16) || value > 0xFF)
        {
            error = wxString::Format(
                ITOOL_TR("Item %lu, “%s”, is not a valid Latin-1 byte (range 00–FF)."),
                static_cast<unsigned long>(index), token);
            return false;
        }

        bytes.push_back(static_cast<unsigned char>(value));
    }

    if (bytes.empty())
    {
        error = ITOOL_TR("Enter hexadecimal bytes, for example: 31 32 33");
        return false;
    }

    return true;
}

wxString BytesToText(const std::vector<unsigned char>& bytes)
{
    wxString text;
    for (unsigned char byte : bytes) text += wxUniChar(static_cast<unsigned int>(byte));
    return text;
}

bool TextToHex(const wxString& input, wxString& hex, wxString& error)
{
    std::ostringstream output;
    for (size_t index = 0; index < input.length(); ++index)
    {
        const wxUniChar character = input[index];
        if (character.GetValue() > 0xFF)
        {
            error = wxString::Format(
                    ITOOL_TR("Character %lu, “%s”, is outside the Latin-1 range (00–FF)."),
                    static_cast<unsigned long>(index + 1),
                    wxString(character));
            return false;
        }

        if (index != 0)
        {
            output << ' ';
        }

        output << std::uppercase << std::hex
               << std::setw(2) << std::setfill('0')
               << character.GetValue();
    }

    hex = wxString::FromUTF8(output.str().c_str());
    return true;
}
}
