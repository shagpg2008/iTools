#include <iostream>
#include <vector>

#include <wx/init.h>

#include "tools/protocol/ProtocolParser.h"

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition) std::cerr << message << std::endl;
    return condition;
}
}

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    bool ok = true;
    protocol::DefinitionMap definitions;
    wxString root, error;
    const wxString packet = wxS(
        "typedef struct {\n"
        " uint16_t length; uint8_t type; uint8_t flags;\n"
        " int32_t value; char name[8];\n"
        "} Packet;");
    ok &= Check(protocol::ParseDefinitions(packet, 4, definitions, root, error), "packet definition should parse");
    ok &= Check(root == wxS("Packet") && definitions[root].size == 16, "packet should be 16 bytes");
    std::vector<unsigned char> bytes;
    ok &= Check(protocol::ParseHexBytes(wxS("10 00 01 00 78 56 34 12 54 65 73 74 00 00 00 00"), bytes, error), "hex should parse");
    std::vector<protocol::DecodedField> rows; size_t records = 0, trailing = 0;
    ok &= Check(protocol::DecodeRecords(definitions, root, bytes, true, rows, records, trailing, error), "packet should decode");
    ok &= Check(records == 1 && trailing == 0 && rows.size() == 5, "packet row count mismatch");
    ok &= Check(rows[0].value.StartsWith(wxS("16 ")), "little-endian length mismatch");
    ok &= Check(rows[3].value.StartsWith(wxS("305419896 ")), "little-endian int32 mismatch");
    ok &= Check(rows[4].value == wxS("\"Test\""), "character array mismatch");

    definitions.clear(); root.clear(); error.clear(); rows.clear(); bytes.clear();
    const wxString nested = wxS(
        "#define SAMPLE_COUNT (2)\n"
        "#define MAGIC_SIZE 0x2U\n"
        "int unrelated_function(int value);\n"
        "typedef struct { uint16_t value; } Inner;\n"
        "typedef struct { uint8_t tag; Inner inner; uint8_t samples[SAMPLE_COUNT]; uint8_t magic[MAGIC_SIZE]; } Outer;");
    ok &= Check(protocol::ParseDefinitions(nested, 1, definitions, root, error), "nested definitions should parse");
    ok &= Check(definitions[root].size == 7, "packed nested struct with macro arrays should be 7 bytes");
    ok &= Check(protocol::ParseHexBytes(wxS("01 12 34 AA BB CC DD"), bytes, error), "big-endian bytes should parse");
    ok &= Check(protocol::DecodeRecords(definitions, root, bytes, false, rows, records, trailing, error), "nested struct should decode");
    ok &= Check(rows.size() == 4 && rows[1].path == wxS("inner.value") && rows[1].value.StartsWith(wxS("4660 ")), "nested big-endian value mismatch");
    ok &= Check(rows[2].size == 2 && rows[3].size == 2, "macro array sizes mismatch");

    definitions.clear(); root.clear(); error.clear();
    ok &= Check(!protocol::ParseDefinitions(wxS("typedef struct { enum X mode; } Bad;"), 1, definitions, root, error), "enum member must be rejected");

    definitions.clear(); root.clear(); error.clear(); rows.clear(); bytes.clear();
    const wxString dynamic = wxS("typedef struct { uint16_t type; uint16_t numOfArray; uint32_t array[0]; } Dynamic;");
    ok &= Check(protocol::ParseDefinitions(dynamic, 4, definitions, root, error, true), "zero-length tail array should parse when enabled");
    ok &= Check(protocol::ParseHexBytes(wxS("01 00 02 00 11 11 11 11 22 22 22 22"), bytes, error), "dynamic bytes should parse");
    ok &= Check(protocol::DecodeRecords(definitions, root, bytes, true, rows, records, trailing, error, true), "dynamic tail array should decode");
    ok &= Check(records == 1 && trailing == 0 && rows.size() == 3, "dynamic tail array row count mismatch");
    ok &= Check(rows[2].path == wxS("array") && rows[2].size == 8 && rows[2].typeName == wxS("uint32_t[2]"), "dynamic tail array size mismatch");

    definitions.clear(); root.clear(); error.clear();
    ok &= Check(!protocol::ParseDefinitions(dynamic, 4, definitions, root, error), "zero-length array should remain disabled by default");
    return ok ? 0 : 1;
}
