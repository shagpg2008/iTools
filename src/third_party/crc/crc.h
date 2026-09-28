#ifndef CRC_CORE__H
#define CRC_CORE__H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 如果项目已经定义这些类型，请删除重复定义。
 */
typedef unsigned char   u8;
typedef unsigned short  u16;
typedef unsigned int    u32;
#ifdef _MSC_VER
typedef unsigned __int64 u64;
#else
typedef unsigned long long u64;
#endif


#ifdef _MSC_VER
#define CRC_U64_C(value) value##UI64
#else
#define CRC_U64_C(value) value##ULL
#endif

typedef u64 (*CrcUpdateFun)(
    u64 value,
    const u8 *data,
    u32 length,
    const void *table);

typedef struct {
	u32 width;
    u64 polynomial;
    u64 initial;
    u64 xorOut;
    int reflectIn;
    int reflectOut;
    CrcUpdateFun tableUpdate;
    const void *table;
} CrcConfig;

typedef struct {
	u64 value;
	int index;
	CrcConfig *config;
    u32 bitwiseLength;
} CrcContext;

int crcInitialize(CrcContext *context, CrcConfig *config);
int crcUpdate(CrcContext *context, const u8 *data, u32 length);
u64 crcFinalize(CrcContext *context);
void ClearCrcTableCache(void);

u64 crcCalculate(CrcContext *context, const u8 *data, u32 length);

u32 crcCalculateCrc32POSIX(const u8 *data, u32 length);

extern CrcConfig crc8SMBUS;
extern CrcConfig crc16MODBUS;
extern CrcConfig crc16CCITT_FALSE;
extern CrcConfig crc16XMODEM; // XMODEM、ZMODEM、CRC-16/ACORN 和 CRC-16/LTE
extern CrcConfig crc16USB;
extern CrcConfig crc16KERMIT;
extern CrcConfig crc24LTEA;
extern CrcConfig crc24LTEB;
extern CrcConfig crc24BLE;
extern CrcConfig crc32IsoHDLC; // CRC-32,CRC-32/ADCCP、CRC-32/V-42、PKZIP CRC-32
extern CrcConfig crc32C; //CRC-32/ISCSI、CRC-32/CASTAGNOLI、CRC-32/INTERLAKEN 和 CRC-32/NVME。
extern CrcConfig crc32MPEG2;
extern CrcConfig crc32POSIX; //CRC-32/CKSUM
extern CrcConfig crc32AUTOSAR;
extern CrcConfig crc32BZIP2;
extern CrcConfig crc64Ecma;
extern CrcConfig crc64Xz;
extern CrcConfig crc64We;


#ifdef __cplusplus
}
#endif


#endif

