#include "tools/protocol/ProtocolParser.h"

#include "core/Localization.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdint.h>

#include <wx/wxcrt.h>

namespace protocol
{
namespace
{
struct Token
{
    wxString text;
    size_t line;
};

struct TypeInfo
{
    TypeInfo() : size(0), alignment(1), isSigned(false), isFloat(false), isCharacter(false), isStruct(false) {}
    size_t size, alignment;
    bool isSigned, isFloat, isCharacter, isStruct;
    wxString structName;
};

typedef std::map<wxString, size_t> ConstantMap;

bool NumericValue(wxString text, size_t& value)
{
    text.Trim(true).Trim(false);
    while (text.length() >= 2 && text.StartsWith(wxS("(")) && text.EndsWith(wxS(")")))
        { text = text.Mid(1, text.length() - 2); text.Trim(true).Trim(false); }
    while (!text.empty() && (text.Last() == 'u' || text.Last() == 'U' || text.Last() == 'l' || text.Last() == 'L'))
        text.RemoveLast();
    unsigned long parsed = 0;
    const int base = text.StartsWith(wxS("0x")) || text.StartsWith(wxS("0X")) ? 16 : 10;
    if (base == 16) text = text.Mid(2);
    if (text.empty() || !text.ToULong(&parsed, base)) return false;
    value = static_cast<size_t>(parsed); return true;
}

void ExtractConstants(const wxString& source, ConstantMap& constants)
{
    size_t start = 0;
    while (start <= source.length())
    {
        size_t end = source.find('\n', start); if (end == wxString::npos) end = source.length();
        wxString line = source.Mid(start, end - start); line.Trim(true).Trim(false);
        if (line.StartsWith(wxS("#")))
        {
            line = line.Mid(1); line.Trim(false);
            if (line.StartsWith(wxS("define")) && (line.length() == 6 || wxIsspace(line[6])))
            {
                line = line.Mid(6); line.Trim(false); size_t split = 0;
                while (split < line.length() && !wxIsspace(line[split])) ++split;
                const wxString name = line.Left(split);
                line = line.Mid(split); line.Trim(false);
                const size_t comment = line.find(wxS("//")); if (comment != wxString::npos) line = line.Left(comment);
                const size_t block = line.find(wxS("/*")); if (block != wxString::npos) line = line.Left(block);
                size_t value = 0;
                if (!name.empty() && name.Find('(') == wxNOT_FOUND && NumericValue(line, value)) constants[name] = value;
            }
        }
        if (end == source.length()) break;
        start = end + 1;
    }
}

size_t AlignUp(size_t value, size_t alignment)
{
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

wxString NormalizeType(const std::vector<Token>& tokens, size_t begin, size_t end)
{
    wxString result;
    for (size_t i = begin; i < end; ++i)
    {
        if (tokens[i].text == wxS("const") || tokens[i].text == wxS("volatile")) continue;
        if (!result.empty()) result += ' ';
        result += tokens[i].text;
    }
    return result;
}

bool Primitive(const wxString& source, TypeInfo& info)
{
    wxString type(source); type.MakeLower();
    if (type == wxS("signed")) type = wxS("int");
    if (type == wxS("unsigned")) type = wxS("unsigned int");
    if (type == wxS("signed int")) type = wxS("int");
    if (type == wxS("signed short") || type == wxS("short int") || type == wxS("signed short int")) type = wxS("short");
    if (type == wxS("unsigned short int")) type = wxS("unsigned short");
    if (type == wxS("signed long") || type == wxS("long int") || type == wxS("signed long int")) type = wxS("long");
    if (type == wxS("unsigned long int")) type = wxS("unsigned long");
    if (type == wxS("signed long long") || type == wxS("long long int") || type == wxS("signed long long int")) type = wxS("long long");
    if (type == wxS("unsigned long long int")) type = wxS("unsigned long long");

    if (type == wxS("char") || type == wxS("signed char") || type == wxS("int8_t") || type == wxS("int8"))
        { info.size = 1; info.isSigned = type != wxS("char"); info.isCharacter = type == wxS("char"); }
    else if (type == wxS("unsigned char") || type == wxS("uint8_t") || type == wxS("uint8") || type == wxS("byte")) info.size = 1;
    else if (type == wxS("short") || type == wxS("int16_t") || type == wxS("int16")) { info.size = 2; info.isSigned = true; }
    else if (type == wxS("unsigned short") || type == wxS("uint16_t") || type == wxS("uint16") || type == wxS("word")) info.size = 2;
    else if (type == wxS("int") || type == wxS("long") || type == wxS("int32_t") || type == wxS("int32")) { info.size = 4; info.isSigned = true; }
    else if (type == wxS("unsigned int") || type == wxS("unsigned long") || type == wxS("uint32_t") || type == wxS("uint32") || type == wxS("dword") || type == wxS("size_t")) info.size = 4;
    else if (type == wxS("long long") || type == wxS("int64_t") || type == wxS("int64")) { info.size = 8; info.isSigned = true; }
    else if (type == wxS("unsigned long long") || type == wxS("uint64_t") || type == wxS("uint64") || type == wxS("qword")) info.size = 8;
    else if (type == wxS("float")) { info.size = 4; info.isFloat = true; }
    else if (type == wxS("double")) { info.size = 8; info.isFloat = true; }
    else if (type == wxS("bool") || type == wxS("_bool")) info.size = 1;
    else if (type == wxS("wchar_t")) { info.size = 2; info.isCharacter = true; }
    else return false;
    info.alignment = info.size > 8 ? 8 : info.size;
    return true;
}

bool ResolveType(const wxString& name, const DefinitionMap& definitions, TypeInfo& info)
{
    if (Primitive(name, info)) return true;
    wxString key(name);
    if (key.StartsWith(wxS("struct "))) key = key.Mid(7);
    DefinitionMap::const_iterator found = definitions.find(key);
    if (found == definitions.end()) return false;
    info.size = found->second.size; info.alignment = found->second.alignment;
    info.isStruct = true; info.structName = key; return true;
}

bool Tokenize(const wxString& source, std::vector<Token>& tokens, wxString& error)
{
    size_t line = 1;
    for (size_t i = 0; i < source.length();)
    {
        const wxChar ch = source[i];
        if (ch == '\n') { ++line; ++i; continue; }
        if (wxIsspace(ch)) { ++i; continue; }
        if (ch == '#') { while (i < source.length() && source[i] != '\n') ++i; continue; }
        if (ch == '/' && i + 1 < source.length() && source[i + 1] == '/')
            { i += 2; while (i < source.length() && source[i] != '\n') ++i; continue; }
        if (ch == '/' && i + 1 < source.length() && source[i + 1] == '*')
        {
            i += 2; bool closed = false;
            while (i + 1 < source.length()) { if (source[i] == '\n') ++line; if (source[i] == '*' && source[i + 1] == '/') { i += 2; closed = true; break; } ++i; }
            if (!closed) { error = wxString::Format(ITOOL_TR("Line %lu: Block comment not ended."), static_cast<unsigned long>(line)); return false; }
            continue;
        }
        if (wxIsalpha(ch) || ch == '_')
        {
            const size_t start = i++; while (i < source.length() && (wxIsalnum(source[i]) || source[i] == '_')) ++i;
            Token token = { source.Mid(start, i - start), line }; tokens.push_back(token); continue;
        }
        if (wxIsdigit(ch))
        {
            const size_t start = i++;
            if (ch == '0' && i < source.length() && (source[i] == 'x' || source[i] == 'X'))
                { ++i; while (i < source.length() && wxIsxdigit(source[i])) ++i; }
            else while (i < source.length() && wxIsdigit(source[i])) ++i;
            while (i < source.length() && (source[i] == 'u' || source[i] == 'U' || source[i] == 'l' || source[i] == 'L')) ++i;
            Token token = { source.Mid(start, i - start), line }; tokens.push_back(token); continue;
        }
        if (ch == '"' || ch == '\'')
        {
            const wxChar quote = ch; const size_t tokenLine = line; ++i;
            while (i < source.length())
            {
                if (source[i] == '\\' && i + 1 < source.length()) { i += 2; continue; }
                if (source[i] == quote) { ++i; break; }
                if (source[i] == '\n') ++line;
                ++i;
            }
            Token token = { wxS("literal"), tokenLine }; tokens.push_back(token); continue;
        }
        if (wxString(wxS("{}[];,*:()=+-!&|<>.?~")).Find(ch) != wxNOT_FOUND)
        {
            Token token = { wxString(ch), line }; tokens.push_back(token); ++i; continue;
        }
        error = wxString::Format(ITOOL_TR("Line %lu: Unsupported character '%c'."), static_cast<unsigned long>(line), ch); return false;
    }
    return true;
}

class Parser
{
public:
    Parser(const std::vector<Token>& tokens, const ConstantMap& constants, size_t packing,
           DefinitionMap& definitions, bool allowZeroLengthTailArray)
        : m_tokens(tokens), m_constants(constants), m_position(0), m_packing(packing),
          m_definitions(definitions), m_allowZeroLengthTailArray(allowZeroLengthTailArray) {}

    bool Run(wxString& rootName, wxString& error)
    {
        while (m_position < m_tokens.size())
        {
            if (!IsDefinition()) { SkipTopLevel(); continue; }
            bool isTypedef = Accept(wxS("typedef"));
            if (!Accept(wxS("struct"))) return Fail(ITOOL_TR("Only struct definitions are supported (enum/union/ordinary declarations are not supported)."), error);
            wxString tag;
            if (m_position < m_tokens.size() && m_tokens[m_position].text != wxS("{")) tag = m_tokens[m_position++].text;
            if (!Expect(wxS("{"), error)) return false;
            StructDefinition definition; definition.size = 0; definition.alignment = 1;
            while (m_position < m_tokens.size() && m_tokens[m_position].text != wxS("}"))
                if (!ParseField(definition, error)) return false;
            if (!Expect(wxS("}"), error)) return false;
            for (size_t i = 0; i < definition.fields.size(); ++i)
                if (definition.fields[i].isArray && definition.fields[i].count == 0 && i + 1 != definition.fields.size())
                    return Fail(ITOOL_TR("A zero-length array is only supported as the last structure member."), error);
            wxString alias;
            if (m_position < m_tokens.size() && m_tokens[m_position].text != wxS(";")) alias = m_tokens[m_position++].text;
            if (!Expect(wxS(";"), error)) return false;
            definition.name = !alias.empty() ? alias : tag;
            if (definition.name.empty()) return Fail(ITOOL_TR("Anonymous structures must provide a typedef name."), error);
            if (!isTypedef && !alias.empty()) return Fail(ITOOL_TR("Aliases cannot be provided after the closing curly brace of a non-typedef structure."), error);
            definition.size = AlignUp(definition.size, definition.alignment);
            m_definitions[definition.name] = definition;
            if (!tag.empty()) m_definitions[tag] = definition;
            rootName = definition.name;
        }
        if (rootName.empty()) { error = ITOOL_TR("No structure definition found."); return false; }
        return true;
    }

private:
    bool IsDefinition() const
    {
        size_t position = m_position;
        if (position < m_tokens.size() && m_tokens[position].text == wxS("typedef")) ++position;
        if (position >= m_tokens.size() || m_tokens[position].text != wxS("struct")) return false;
        ++position;
        while (position < m_tokens.size() && m_tokens[position].text != wxS("{") && m_tokens[position].text != wxS(";")) ++position;
        return position < m_tokens.size() && m_tokens[position].text == wxS("{");
    }
    void SkipTopLevel()
    {
        int braces = 0;
        while (m_position < m_tokens.size())
        {
            const wxString token = m_tokens[m_position++].text;
            if (token == wxS("{")) ++braces;
            else if (token == wxS("}")) { if (braces > 0) --braces; else return; }
            else if (token == wxS(";") && braces == 0) return;
        }
    }
    bool ParseField(StructDefinition& definition, wxString& error)
    {
        const size_t typeStart = m_position; wxString typeName; TypeInfo type;
        size_t declarator = typeStart;
        if (m_position < m_tokens.size() && m_tokens[m_position].text == wxS("enum")) return Fail(ITOOL_TR("Member type enum is not supported."), error);
        if (m_position < m_tokens.size() && m_tokens[m_position].text == wxS("union")) return Fail(ITOOL_TR("union is not supported."), error);
        for (size_t candidate = typeStart + 1; candidate < m_tokens.size(); ++candidate)
        {
            if (m_tokens[candidate].text == wxS("*") || m_tokens[candidate].text == wxS(";") || m_tokens[candidate].text == wxS("{")) break;
            TypeInfo resolved; const wxString possible = NormalizeType(m_tokens, typeStart, candidate);
            if (ResolveType(possible, m_definitions, resolved) &&
                candidate < m_tokens.size() && wxIsalpha(m_tokens[candidate].text[0]))
                { typeName = possible; type = resolved; declarator = candidate; }
        }
        if (typeName.empty()) return Fail(ITOOL_TR("Unknown or undefined member type."), error);
        m_position = declarator;
        for (;;)
        {
            if (m_position >= m_tokens.size()) return Fail(ITOOL_TR("Member's statement is not concluded."), error);
            if (m_tokens[m_position].text == wxS("*")) return Fail(ITOOL_TR("Pointer members are not supported."), error);
            const wxString fieldName = m_tokens[m_position++].text;
            size_t count = 1; bool isArray = false;
            if (Accept(wxS("[")))
            {
                isArray = true;
                if (m_position >= m_tokens.size()) return Fail(ITOOL_TR("Array length missing."), error);
                size_t length = 0;
                if (!NumericValue(m_tokens[m_position].text, length))
                {
                    ConstantMap::const_iterator constant = m_constants.find(m_tokens[m_position].text);
                    if (constant == m_constants.end()) return Fail(ITOOL_TR("Array length must be a positive integer or a defined numeric macro."), error);
                    length = constant->second;
                }
                if (!length && !m_allowZeroLengthTailArray)
                    return Fail(ITOOL_TR("Array length must be greater than zero."), error);
                ++m_position; if (!Expect(wxS("]"), error)) return false; count = length;
            }
            if (Accept(wxS(":"))) return Fail(ITOOL_TR("Bit field members are not supported."), error);
            const size_t alignment = std::min(type.alignment, m_packing);
            definition.size = AlignUp(definition.size, alignment);
            Field field = { fieldName, typeName, definition.size, type.size, count, isArray };
            definition.fields.push_back(field); definition.size += type.size * count;
            definition.alignment = std::max(definition.alignment, alignment);
            if (Accept(wxS(","))) continue;
            if (!Expect(wxS(";"), error)) return false;
            return true;
        }
    }
    bool Accept(const wxString& value) { if (m_position < m_tokens.size() && m_tokens[m_position].text == value) { ++m_position; return true; } return false; }
    bool Expect(const wxString& value, wxString& error)
    {
        if (Accept(value)) return true;
        return Fail(ITOOL_TR("expect '") + value + wxS("'。"), error);
    }
    bool Fail(const wxString& message, wxString& error) const
    {
        const size_t line = m_position < m_tokens.size() ? m_tokens[m_position].line : (m_tokens.empty() ? 1 : m_tokens.back().line);
        error = wxString::Format(ITOOL_TR("Line %lu:"), static_cast<unsigned long>(line)) + message; return false;
    }
    const std::vector<Token>& m_tokens; const ConstantMap& m_constants;
    size_t m_position, m_packing; DefinitionMap& m_definitions;
    bool m_allowZeroLengthTailArray;
};

uint64_t UnsignedValue(const unsigned char* data, size_t size, bool littleEndian)
{
    uint64_t value = 0;
    if (littleEndian) for (size_t i = 0; i < size; ++i) value |= static_cast<uint64_t>(data[i]) << (i * 8);
    else for (size_t i = 0; i < size; ++i) value = (value << 8) | data[i];
    return value;
}

wxString RawHex(const unsigned char* data, size_t size)
{
    static const wxChar digits[] = wxS("0123456789ABCDEF"); wxString result;
    for (size_t i = 0; i < size; ++i) { if (i) result += ' '; result += digits[data[i] >> 4]; result += digits[data[i] & 15]; }
    return result;
}

wxString ScalarValue(const TypeInfo& type, const unsigned char* data, bool littleEndian)
{
    const uint64_t raw = UnsignedValue(data, type.size, littleEndian);
    if (type.isFloat)
    {
        std::ostringstream stream;
        if (type.size == 4) { const uint32_t bits = static_cast<uint32_t>(raw); float value; std::memcpy(&value, &bits, 4); stream << std::setprecision(9) << value; }
        else { double value; std::memcpy(&value, &raw, 8); stream << std::setprecision(17) << value; }
        return wxString::FromUTF8(stream.str().c_str());
    }
    if (type.isCharacter && type.size == 1 && raw >= 32 && raw <= 126)
        return wxString::Format(wxS("'%c' (%llu)"), static_cast<int>(raw), static_cast<unsigned long long>(raw));
    std::ostringstream stream;
    if (type.isSigned)
    {
        int64_t value;
        if (type.size == 8) value = static_cast<int64_t>(raw);
        else { const uint64_t sign = UINT64_C(1) << (type.size * 8 - 1); value = static_cast<int64_t>((raw ^ sign) - sign); }
        stream << value;
    }
    else stream << raw;
    stream << " (0x" << std::uppercase << std::hex << std::setw(static_cast<int>(type.size * 2)) << std::setfill('0') << raw << ')';
    return wxString::FromUTF8(stream.str().c_str());
}

bool DecodeStruct(const DefinitionMap& definitions, const StructDefinition& definition,
                  const unsigned char* data, size_t absoluteOffset, const wxString& prefix,
                  bool littleEndian, std::vector<DecodedField>& rows, wxString& error)
{
    for (size_t i = 0; i < definition.fields.size(); ++i)
    {
        const Field& field = definition.fields[i]; TypeInfo type;
        if (!ResolveType(field.typeName, definitions, type)) { error = ITOOL_TR("Unable to resolve type:") + field.typeName; return false; }
        const unsigned char* fieldData = data + field.offset;
        if (type.isStruct)
        {
            DefinitionMap::const_iterator nested = definitions.find(type.structName);
            for (size_t index = 0; index < field.count; ++index)
            {
                const wxString path = prefix + field.name + (field.count > 1 ? wxString::Format(wxS("[%lu]."), static_cast<unsigned long>(index)) : wxS("."));
                if (!DecodeStruct(definitions, nested->second, fieldData + index * type.size,
                                  absoluteOffset + field.offset + index * type.size, path, littleEndian, rows, error)) return false;
            }
        }
        else if (field.count == 1)
        {
            DecodedField row = { prefix + field.name, field.typeName, absoluteOffset + field.offset, type.size,
                                 RawHex(fieldData, type.size), ScalarValue(type, fieldData, littleEndian) };
            rows.push_back(row);
        }
        else
        {
            wxString value;
            if (type.isCharacter && type.size == 1)
            {
                wxString text;
                for (size_t index = 0; index < field.count && fieldData[index]; ++index)
                    text += fieldData[index] >= 32 && fieldData[index] <= 126 ? static_cast<wxChar>(fieldData[index]) : wxS('.');
                value = wxS("\"") + text + wxS("\"");
            }
            else
            {
                value = wxS("[");
                for (size_t index = 0; index < field.count; ++index) { if (index) value += wxS(", "); value += ScalarValue(type, fieldData + index * type.size, littleEndian); }
                value += wxS("]");
            }
            DecodedField row = { prefix + field.name, field.typeName + wxString::Format(wxS("[%lu]"), static_cast<unsigned long>(field.count)),
                                 absoluteOffset + field.offset, type.size * field.count, RawHex(fieldData, type.size * field.count), value };
            rows.push_back(row);
        }
    }
    return true;
}
}

bool ParseDefinitions(const wxString& source, size_t packing, DefinitionMap& definitions,
                      wxString& rootName, wxString& error, bool allowZeroLengthTailArray)
{
    definitions.clear(); rootName.clear(); std::vector<Token> tokens; ConstantMap constants;
    if (packing != 1 && packing != 2 && packing != 4 && packing != 8) { error = ITOOL_TR("Struct alignment must be 1, 2, 4, or 8."); return false; }
    ExtractConstants(source, constants);
    if (!Tokenize(source, tokens, error)) return false;
    Parser parser(tokens, constants, packing, definitions, allowZeroLengthTailArray); return parser.Run(rootName, error);
}

bool ParseHexBytes(const wxString& source, std::vector<unsigned char>& bytes, wxString& error)
{
    wxString clean;
    for (size_t i = 0; i < source.length(); ++i)
    {
        const wxChar ch = source[i];
        if (wxIsspace(ch) || ch == ':' || ch == '-' || ch == ',') continue;
        if (ch == '0' && i + 1 < source.length() && (source[i + 1] == 'x' || source[i + 1] == 'X')) { ++i; continue; }
        if (!wxIsxdigit(ch)) { error = ITOOL_TR("Hex input contains invalid characters."); return false; }
        clean += ch;
    }
    if (clean.empty()) { error = ITOOL_TR("Hex input cannot be empty."); return false; }
    if (clean.length() % 2) { error = ITOOL_TR("Hex input must contain an even number of numbers."); return false; }
    for (size_t i = 0; i < clean.length(); i += 2) { unsigned long value = 0; clean.Mid(i, 2).ToULong(&value, 16); bytes.push_back(static_cast<unsigned char>(value)); }
    return true;
}

bool DecodeRecords(const DefinitionMap& definitions, const wxString& rootName,
                   const std::vector<unsigned char>& bytes, bool littleEndian,
                   std::vector<DecodedField>& fields, size_t& recordCount,
                   size_t& trailingBytes, wxString& error, bool dynamicTailArray)
{
    DefinitionMap::const_iterator root = definitions.find(rootName);
    if (root == definitions.end() || !root->second.size) { error = ITOOL_TR("The root structure is invalid."); return false; }
    if (dynamicTailArray)
    {
        const StructDefinition& definition = root->second;
        if (definition.fields.size() < 2)
            { error = ITOOL_TR("Dynamic tail array requires at least two structure members."); return false; }
        const Field& countField = definition.fields[definition.fields.size() - 2];
        const Field& arrayField = definition.fields.back();
        TypeInfo countType, elementType;
        if (countField.count != 1 || !ResolveType(countField.typeName, definitions, countType) ||
            countType.isStruct || countType.isFloat || countType.isCharacter ||
            countType.size < 1 || countType.size > 4)
            { error = ITOOL_TR("The penultimate member must be an 8-32 bit integer scalar for a dynamic tail array."); return false; }
        if (!arrayField.isArray || !ResolveType(arrayField.typeName, definitions, elementType))
            { error = ITOOL_TR("The last member must be an array for a dynamic tail array."); return false; }

        fields.clear(); recordCount = 0; trailingBytes = 0; size_t offset = 0;
        while (offset < bytes.size())
        {
            if (bytes.size() - offset < arrayField.offset)
                { trailingBytes = bytes.size() - offset; break; }
            const uint64_t count64 = UnsignedValue(&bytes[offset + countField.offset], countType.size, littleEndian);
            if (count64 > (std::numeric_limits<size_t>::max() - arrayField.offset) / elementType.size)
                { error = ITOOL_TR("Dynamic tail array length is too large."); return false; }
            const size_t count = static_cast<size_t>(count64);
            const size_t recordSize = AlignUp(arrayField.offset + count * elementType.size, definition.alignment);
            if (recordSize > bytes.size() - offset)
                { trailingBytes = bytes.size() - offset; break; }

            StructDefinition dynamicDefinition = definition;
            dynamicDefinition.fields.back().count = count;
            dynamicDefinition.size = recordSize;
            const wxString prefix = wxString::Format(wxS("[%lu]."), static_cast<unsigned long>(recordCount));
            if (!DecodeStruct(definitions, dynamicDefinition, &bytes[offset], offset, prefix,
                              littleEndian, fields, error)) return false;
            offset += recordSize; ++recordCount;
        }
        if (!recordCount)
        {
            error = wxString::Format(ITOOL_TR("Insufficient data: Dynamic structure header requires %lu bytes."),
                                     static_cast<unsigned long>(arrayField.offset));
            return false;
        }
        if (recordCount == 1)
            for (size_t i = 0; i < fields.size(); ++i)
                if (fields[i].path.StartsWith(wxS("[0]."))) fields[i].path = fields[i].path.Mid(4);
        return true;
    }
    recordCount = bytes.size() / root->second.size; trailingBytes = bytes.size() % root->second.size;
    if (!recordCount) { error = wxString::Format(ITOOL_TR("Insufficient data: Structure requires %lu bytes."), static_cast<unsigned long>(root->second.size)); return false; }
    for (size_t record = 0; record < recordCount; ++record)
    {
        const wxString prefix = recordCount > 1 ? wxString::Format(wxS("[%lu]."), static_cast<unsigned long>(record)) : wxString();
        if (!DecodeStruct(definitions, root->second, &bytes[record * root->second.size], record * root->second.size,
                          prefix, littleEndian, fields, error)) return false;
    }
    return true;
}
}
