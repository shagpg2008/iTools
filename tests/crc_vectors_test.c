#include <stdio.h>
#include <string.h>

#include "third_party/crc/crc.h"

struct Vector
{
    const char* name;
    CrcConfig* config;
    u64 expected;
};

int main(void)
{
    static const u8 input[] = "123456789";
    struct Vector vectors[] = {
        { "CRC8/SMBUS",        &crc8SMBUS,        CRC_U64_C(0xF4) },
        { "CRC16/MODBUS",      &crc16MODBUS,      CRC_U64_C(0x4B37) },
        { "CRC16/CCITT_FALSE", &crc16CCITT_FALSE, CRC_U64_C(0x29B1) },
        { "CRC16/XMODEM",      &crc16XMODEM,      CRC_U64_C(0x31C3) },
        { "CRC16/KERMIT",      &crc16KERMIT,      CRC_U64_C(0x2189) },
        { "CRC16/USB",         &crc16USB,         CRC_U64_C(0xB4C8) },
        { "CRC24/LTEA",        &crc24LTEA,        CRC_U64_C(0xCDE703) },
        { "CRC24/LTEB",        &crc24LTEB,        CRC_U64_C(0x23EF52) },
        { "CRC24/BLE",         &crc24BLE,         CRC_U64_C(0xC25A56) },
        { "CRC32",             &crc32IsoHDLC,     CRC_U64_C(0xCBF43926) },
        { "CRC32C",            &crc32C,           CRC_U64_C(0xE3069283) },
        { "CRC32/MPEG2",       &crc32MPEG2,       CRC_U64_C(0x0376E6E7) },
        { "CRC32/AUTOSAR",     &crc32AUTOSAR,     CRC_U64_C(0x1697D06A) },
        { "CRC32/BZIP2",       &crc32BZIP2,       CRC_U64_C(0xFC891918) },
        { "CRC64/ECMA",        &crc64Ecma,        CRC_U64_C(0x6C40DF5F0B497347) },
        { "CRC64/XZ",          &crc64Xz,          CRC_U64_C(0x995DC9BBDF1939FA) },
        { "CRC64/WE",          &crc64We,          CRC_U64_C(0x62EC59E3F1A4F00A) }
    };
    const size_t count = sizeof(vectors) / sizeof(vectors[0]);
    size_t index;
    int failures = 0;

    for (index = 0; index < count; ++index)
    {
        CrcContext context;
        context.config = vectors[index].config;
        if (crcCalculate(&context, input, 9) != vectors[index].expected)
        {
            fprintf(stderr, "%s failed\n", vectors[index].name);
            ++failures;
        }
    }

    if (crcCalculateCrc32POSIX(input, 9) != 0x377A6011UL)
    {
        fprintf(stderr, "CRC32/POSIX failed\n");
        ++failures;
    }

    ClearCrcTableCache();
    if (failures == 0)
        puts("All 18 CRC vectors passed.");
    return failures == 0 ? 0 : 1;
}
