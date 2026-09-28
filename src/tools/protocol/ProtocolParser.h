#pragma once

#include <map>
#include <vector>

#include <wx/string.h>

namespace protocol
{
struct Field
{
    wxString name;
    wxString typeName;
    size_t offset;
    size_t elementSize;
    size_t count;
    bool isArray;
};

struct StructDefinition
{
    wxString name;
    std::vector<Field> fields;
    size_t size;
    size_t alignment;
};

struct DecodedField
{
    wxString path;
    wxString typeName;
    size_t offset;
    size_t size;
    wxString rawHex;
    wxString value;
};

typedef std::map<wxString, StructDefinition> DefinitionMap;

bool ParseDefinitions(const wxString& source, size_t packing, DefinitionMap& definitions,
                      wxString& rootName, wxString& error,
                      bool allowZeroLengthTailArray = false);
bool ParseHexBytes(const wxString& source, std::vector<unsigned char>& bytes, wxString& error);
bool DecodeRecords(const DefinitionMap& definitions, const wxString& rootName,
                   const std::vector<unsigned char>& bytes, bool littleEndian,
                   std::vector<DecodedField>& fields, size_t& recordCount,
                   size_t& trailingBytes, wxString& error,
                   bool dynamicTailArray = false);
}
