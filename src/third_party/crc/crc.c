#include <stdio.h>
#include <string.h>
#include "crc.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#define CRC_TABLE_SIZE         256

/*
 * 阈值按“尚未使用查表法累计处理的字节数”判断。
 * 阈值按不同位宽的逐位计算成本分别设置。
 */
#define CRC8_TABLE_THRESHOLD    300UL
#define CRC16_TABLE_THRESHOLD   350UL
#define CRC24_TABLE_THRESHOLD   400UL
#define CRC32_TABLE_THRESHOLD   400UL
#define CRC64_TABLE_THRESHOLD   250UL

struct CrcTableCacheNode
{
    u32 width;
    u64 polynomial;
    int reflectIn;
    void *table;
    struct CrcTableCacheNode *next;
};

static struct CrcTableCacheNode *crcTableCacheHead = NULL;

static int crcIsValidWidth(u32 width)
{
    return width == 8 || width == 16 || width == 24 ||
           width == 32 || width == 64;
}

static u32 crcGetTableThreshold(u32 width)
{
    if (width == 8)
        return CRC8_TABLE_THRESHOLD;
    if (width == 16)
        return CRC16_TABLE_THRESHOLD;
    if (width == 24)
        return CRC24_TABLE_THRESHOLD;
    if (width == 32)
        return CRC32_TABLE_THRESHOLD;

    return CRC64_TABLE_THRESHOLD;
}

static u32 crcGetElementSize(u32 width)
{
    if (width == 8)
        return sizeof(u8);
    if (width == 16)
        return sizeof(u16);
    if (width == 24 || width == 32)
        return sizeof(u32);

    return sizeof(u64);
}

static void crcSetTableValue(
    void *table,
    u32 width,
    u32 index,
    u64 value)
{
    if (width == 8)
        ((u8 *)table)[index] = (u8)value;
    else if (width == 16)
        ((u16 *)table)[index] = (u16)value;
    else if (width == 24 || width == 32)
        ((u32 *)table)[index] = (u32)value;
    else
        ((u64 *)table)[index] = value;
}

static u64 crcGetMask(u32 width)
{
    if (width == 64)
        return CRC_U64_C(0xFFFFFFFFFFFFFFFF);

    return (((u64)1 << width) - 1);
}

static u64 crcReflect(
    u64 value,
    u32 bitCount)
{
    u64 result;
    u32 i;

    result = 0;

    for (i = 0; i < bitCount; ++i)
    {
        result <<= 1;

        if (value & 1)
            result |= 1;

        value >>= 1;
    }

    return result;
}

static void crcGenerateTable(
    void *table,
    u32 width,
    u64 polynomial,
    int reflectIn)
{
    u64 crc;
    u64 mask;
    u64 topBit;
    u64 actualPolynomial;
    u32 index;
    u32 bit;

    mask = crcGetMask(width);
    polynomial &= mask;

    if (reflectIn)
    {
        actualPolynomial = crcReflect(polynomial, width) & mask;

        for (index = 0; index < CRC_TABLE_SIZE; ++index)
        {
            crc = index;

            for (bit = 0; bit < 8; ++bit)
            {
                if (crc & 1)
                    crc = (crc >> 1) ^ actualPolynomial;
                else
                    crc >>= 1;
            }

            crcSetTableValue(table, width, index, crc & mask);
        }
    }
    else
    {
        if (width == 64)
            topBit = CRC_U64_C(0x8000000000000000);
        else
            topBit = (u64)1 << (width - 1);

        for (index = 0; index < CRC_TABLE_SIZE; ++index)
        {
            crc = (u64)index << (width - 8);

            for (bit = 0; bit < 8; ++bit)
            {
                if (crc & topBit)
                    crc = (crc << 1) ^ polynomial;
                else
                    crc <<= 1;

                crc &= mask;
            }

            crcSetTableValue(table, width, index, crc);
        }
    }
}

static struct CrcTableCacheNode *crcFindCachedTable(
    u32 width,
    u64 polynomial,
    int reflectIn)
{
    struct CrcTableCacheNode *node;

    polynomial &= crcGetMask(width);
    reflectIn = reflectIn ? 1 : 0;
    node = crcTableCacheHead;

    while (node != NULL)
    {
        if (node->width == width &&
            node->polynomial == polynomial &&
            node->reflectIn == reflectIn)
        {
            return node;
        }

        node = node->next;
    }

    return NULL;
}

static struct CrcTableCacheNode *crcPrepareTable(
    u32 width,
    u64 polynomial,
    int reflectIn)
{
    struct CrcTableCacheNode *node;
    u32 elementSize;
    u32 allocationSize;

    node = crcFindCachedTable(width, polynomial, reflectIn);
    if (node != NULL)
        return node;

    elementSize = crcGetElementSize(width);
    allocationSize = CRC_TABLE_SIZE * elementSize;

    node = (struct CrcTableCacheNode *)malloc(
        sizeof(struct CrcTableCacheNode));
    if (node == NULL)
        return NULL;

    node->table = malloc(allocationSize);
    if (node->table == NULL)
    {
        free(node);
        return NULL;
    }

    polynomial &= crcGetMask(width);
    reflectIn = reflectIn ? 1 : 0;

    node->width = width;
    node->polynomial = polynomial;
    node->reflectIn = reflectIn;
    node->next = NULL;

    crcGenerateTable(node->table, width, polynomial, reflectIn);

    node->next = crcTableCacheHead;
    crcTableCacheHead = node;

    return node;
}

static u64 crc8TableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u8 *lookup;
    u8 crc;

    lookup = (const u8 *)table;
    crc = (u8)value;

    while (length >= 4)
    {
        crc = lookup[(u8)(crc ^ data[0])];
        crc = lookup[(u8)(crc ^ data[1])];
        crc = lookup[(u8)(crc ^ data[2])];
        crc = lookup[(u8)(crc ^ data[3])];
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        crc = lookup[(u8)(crc ^ *data)];
        ++data;
        --length;
    }

    return crc;
}

static u64 crc16ReflectedTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u16 *lookup;
    u16 crc;

    lookup = (const u16 *)table;
    crc = (u16)value;

#define CRC16_REFLECTED_STEP(byteValue) \
    crc = (u16)((crc >> 8) ^ lookup[(u8)(crc ^ (byteValue))])

    while (length >= 4)
    {
        CRC16_REFLECTED_STEP(data[0]);
        CRC16_REFLECTED_STEP(data[1]);
        CRC16_REFLECTED_STEP(data[2]);
        CRC16_REFLECTED_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC16_REFLECTED_STEP(*data);
        ++data;
        --length;
    }

#undef CRC16_REFLECTED_STEP

    return crc;
}

static u64 crc16NormalTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u16 *lookup;
    u16 crc;

    lookup = (const u16 *)table;
    crc = (u16)value;

#define CRC16_NORMAL_STEP(byteValue) \
    crc = (u16)((crc << 8) ^ \
        lookup[(u8)((crc >> 8) ^ (byteValue))])

    while (length >= 4)
    {
        CRC16_NORMAL_STEP(data[0]);
        CRC16_NORMAL_STEP(data[1]);
        CRC16_NORMAL_STEP(data[2]);
        CRC16_NORMAL_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC16_NORMAL_STEP(*data);
        ++data;
        --length;
    }

#undef CRC16_NORMAL_STEP

    return crc;
}

static u64 crc24ReflectedTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u32 *lookup;
    u32 crc;

    lookup = (const u32 *)table;
    crc = (u32)value & 0x00FFFFFFUL;

#define CRC24_REFLECTED_STEP(byteValue) \
    crc = (crc >> 8) ^ lookup[(u8)(crc ^ (byteValue))]

    while (length >= 4)
    {
        CRC24_REFLECTED_STEP(data[0]);
        CRC24_REFLECTED_STEP(data[1]);
        CRC24_REFLECTED_STEP(data[2]);
        CRC24_REFLECTED_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC24_REFLECTED_STEP(*data);
        ++data;
        --length;
    }

#undef CRC24_REFLECTED_STEP

    return crc & 0x00FFFFFFUL;
}

static u64 crc24NormalTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u32 *lookup;
    u32 crc;

    lookup = (const u32 *)table;
    crc = (u32)value & 0x00FFFFFFUL;

#define CRC24_NORMAL_STEP(byteValue) \
    crc = ((crc << 8) & 0x00FFFFFFUL) ^ \
        lookup[(u8)((crc >> 16) ^ (byteValue))]

    while (length >= 4)
    {
        CRC24_NORMAL_STEP(data[0]);
        CRC24_NORMAL_STEP(data[1]);
        CRC24_NORMAL_STEP(data[2]);
        CRC24_NORMAL_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC24_NORMAL_STEP(*data);
        ++data;
        --length;
    }

#undef CRC24_NORMAL_STEP

    return crc & 0x00FFFFFFUL;
}

/* ---------- CRC-32/64 单表、强类型、四次循环展开 ---------- */

static u64 crc32ReflectedTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u32 *lookup;
    u32 crc;

    lookup = (const u32 *)table;
    crc = (u32)value;

#define CRC32_REFLECTED_STEP(byteValue) \
    crc = (crc >> 8) ^ lookup[(u8)(crc ^ (byteValue))]

    while (length >= 4)
    {
        CRC32_REFLECTED_STEP(data[0]);
        CRC32_REFLECTED_STEP(data[1]);
        CRC32_REFLECTED_STEP(data[2]);
        CRC32_REFLECTED_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC32_REFLECTED_STEP(*data);
        ++data;
        --length;
    }

#undef CRC32_REFLECTED_STEP

    return crc;
}

static u64 crc32NormalTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u32 *lookup;
    u32 crc;

    lookup = (const u32 *)table;
    crc = (u32)value;

#define CRC32_NORMAL_STEP(byteValue) \
    crc = (crc << 8) ^ lookup[(u8)((crc >> 24) ^ (byteValue))]

    while (length >= 4)
    {
        CRC32_NORMAL_STEP(data[0]);
        CRC32_NORMAL_STEP(data[1]);
        CRC32_NORMAL_STEP(data[2]);
        CRC32_NORMAL_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC32_NORMAL_STEP(*data);
        ++data;
        --length;
    }

#undef CRC32_NORMAL_STEP

    return crc;
}

static u64 crc64ReflectedTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u64 *lookup;
    u64 crc;

    lookup = (const u64 *)table;
    crc = value;

#define CRC64_REFLECTED_STEP(byteValue) \
    crc = (crc >> 8) ^ lookup[(u8)(crc ^ (byteValue))]

    while (length >= 4)
    {
        CRC64_REFLECTED_STEP(data[0]);
        CRC64_REFLECTED_STEP(data[1]);
        CRC64_REFLECTED_STEP(data[2]);
        CRC64_REFLECTED_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC64_REFLECTED_STEP(*data);
        ++data;
        --length;
    }

#undef CRC64_REFLECTED_STEP

    return crc;
}

static u64 crc64NormalTableUpdate(
    u64 value, const u8 *data, u32 length, const void *table)
{
    const u64 *lookup;
    u64 crc;

    lookup = (const u64 *)table;
    crc = value;

#define CRC64_NORMAL_STEP(byteValue) \
    crc = (crc << 8) ^ lookup[(u8)((crc >> 56) ^ (byteValue))]

    while (length >= 4)
    {
        CRC64_NORMAL_STEP(data[0]);
        CRC64_NORMAL_STEP(data[1]);
        CRC64_NORMAL_STEP(data[2]);
        CRC64_NORMAL_STEP(data[3]);
        data += 4;
        length -= 4;
    }

    while (length != 0)
    {
        CRC64_NORMAL_STEP(*data);
        ++data;
        --length;
    }

#undef CRC64_NORMAL_STEP

    return crc;
}

static CrcUpdateFun crcSelectTableUpdate(
    u32 width, int reflectIn)
{
    if (width == 8)
        return (CrcUpdateFun)crc8TableUpdate;
    if (width == 16)
        return (CrcUpdateFun)(reflectIn ? crc16ReflectedTableUpdate : crc16NormalTableUpdate);
    if (width == 24)
        return (CrcUpdateFun)(reflectIn ? crc24ReflectedTableUpdate : crc24NormalTableUpdate);
    if (width == 32)
        return (CrcUpdateFun)(reflectIn ? crc32ReflectedTableUpdate : crc32NormalTableUpdate);

    return (CrcUpdateFun)(reflectIn ? crc64ReflectedTableUpdate : crc64NormalTableUpdate);
}

/* ---------- 强类型逐位回退算法 ---------- */

static u64 crcUpdateBitwise(
    u64 value,
    const u8 *data,
    u32 length,
    const CrcConfig *config)
{
    u64 mask;
    u64 polynomial;
    u64 crc64;
    u64 topBit;
    u32 crc32;
    u32 polynomial32;
    u32 topBit32;
    u32 index;
    u32 bit;
    u16 crc16;
    u16 polynomial16;
    u8 crc8;
    u8 polynomial8;

    mask = crcGetMask(config->width);
    polynomial = config->polynomial & mask;

    if (config->reflectIn)
        polynomial = crcReflect(polynomial, config->width) & mask;

    if (config->width == 8)
    {
        crc8 = (u8)value;
        polynomial8 = (u8)polynomial;

        for (index = 0; index < length; ++index)
        {
            crc8 ^= data[index];
            for (bit = 0; bit < 8; ++bit)
            {
                if (config->reflectIn)
                    crc8 = (u8)((crc8 & 1) ? ((crc8 >> 1) ^ polynomial8) : (crc8 >> 1));
                else
                    crc8 = (u8)((crc8 & 0x80) ? ((crc8 << 1) ^ polynomial8) : (crc8 << 1));
            }
        }
        return crc8;
    }

    if (config->width == 16)
    {
        crc16 = (u16)value;
        polynomial16 = (u16)polynomial;

        for (index = 0; index < length; ++index)
        {
            if (config->reflectIn)
                crc16 ^= data[index];
            else
                crc16 ^= (u16)data[index] << 8;

            for (bit = 0; bit < 8; ++bit)
            {
                if (config->reflectIn)
                    crc16 = (u16)((crc16 & 1) ? ((crc16 >> 1) ^ polynomial16) : (crc16 >> 1));
                else
                    crc16 = (u16)((crc16 & 0x8000) ? ((crc16 << 1) ^ polynomial16) : (crc16 << 1));
            }
        }
        return crc16;
    }

    if (config->width == 24 || config->width == 32)
    {
        crc32 = (u32)value;
        polynomial32 = (u32)polynomial;
        topBit32 = config->width == 24 ? 0x00800000UL : 0x80000000UL;

        for (index = 0; index < length; ++index)
        {
            if (config->reflectIn)
                crc32 ^= data[index];
            else
                crc32 ^= (u32)data[index] << (config->width - 8);

            for (bit = 0; bit < 8; ++bit)
            {
                if (config->reflectIn)
                    crc32 = (crc32 & 1) ? ((crc32 >> 1) ^ polynomial32) : (crc32 >> 1);
                else
                    crc32 = (crc32 & topBit32) ? ((crc32 << 1) ^ polynomial32) : (crc32 << 1);

                if (config->width == 24)
                    crc32 &= 0x00FFFFFFUL;
            }
        }
        return (u64)(crc32 & (u32)mask);
    }

    crc64 = value;
    topBit = CRC_U64_C(0x8000000000000000);

    for (index = 0; index < length; ++index)
    {
        if (config->reflectIn)
            crc64 ^= data[index];
        else
            crc64 ^= (u64)data[index] << 56;

        for (bit = 0; bit < 8; ++bit)
        {
            if (config->reflectIn)
                crc64 = (crc64 & 1) ? ((crc64 >> 1) ^ polynomial) : (crc64 >> 1);
            else
                crc64 = (crc64 & topBit) ? ((crc64 << 1) ^ polynomial) : (crc64 << 1);
        }
    }

    return crc64;
}

static void crcBindTable(
    CrcConfig *config,
    const struct CrcTableCacheNode *node)
{
    config->table = node != NULL ? node->table : NULL;
}

int crcInitialize(CrcContext *context, CrcConfig *config)
{
    u64 mask;
    u64 initial;

    if (context == NULL || config == NULL)
        return 0;
    if (!crcIsValidWidth(config->width))
        return 0;

    mask = crcGetMask(config->width);
    initial = config->initial & mask;

    if (config->reflectIn)
        initial = crcReflect(initial, config->width);

    context->config = config;
    context->value = initial & mask;
    context->bitwiseLength = 0;
    config->tableUpdate = crcSelectTableUpdate(
        config->width, config->reflectIn ? 1 : 0);

    /* 表的查找和生成只允许在 crcUpdate() 中发生。 */
    config->table = NULL;

    return 1;
}

/*
 * 第一阶段：初始化 CRC 寄存器。
 *
 * width   ：CRC 位宽，支持 8、16、32、64
 * initial ：CRC 初始值
 */

int crcUpdate(CrcContext *context, const u8 *data, u32 length)
{
    struct CrcTableCacheNode *node;
    u32 threshold;
    u32 remainingToThreshold;
	CrcConfig *config = NULL;

    if (context == NULL || context->config == NULL)
        return 0;
    if (data == NULL && length != 0)
        return 0;
    if (length == 0)
        return 1;

    config = context->config;

    if (config->table == NULL)
    {
        /* 其他上下文可能已经为相同参数创建了缓存。 */
        node = crcFindCachedTable(
            config->width,
            config->polynomial,
            config->reflectIn);
        crcBindTable(config, node);
    }

    if (config->table == NULL)
    {
        threshold = crcGetTableThreshold(config->width);

        if (context->bitwiseLength >= threshold)
            remainingToThreshold = 0;
        else
            remainingToThreshold = threshold - context->bitwiseLength;

        if (length > remainingToThreshold)
        {
            node = crcPrepareTable(
                config->width,
                config->polynomial,
                config->reflectIn);
            crcBindTable(config, node);
        }
    }

    if (config->table != NULL)
    {
        context->value = config->tableUpdate(
            context->value,
            data,
            length,
            config->table);
        return 1;
    }

    context->value = crcUpdateBitwise(
        context->value,
        data,
        length,
        config);

    if (0xFFFFFFFFUL - context->bitwiseLength < length)
        context->bitwiseLength = 0xFFFFFFFFUL;
    else
        context->bitwiseLength += length;

    return 1;
}

u64 crcFinalize(CrcContext *context)
{
    u64 crc;
    u64 mask;
	CrcConfig *config = NULL;

    if (context == NULL || context->config == NULL)
        return 0;

    config = context->config;

    mask = crcGetMask(config->width);
    crc = context->value & mask;

    if ((config->reflectIn ? 1 : 0) !=
        (config->reflectOut ? 1 : 0))
    {
        crc = crcReflect(crc, config->width);
    }

    return ((crc ^ config->xorOut) & mask);
}

void ClearCrcTableCache(void)
{
    struct CrcTableCacheNode *node;
    struct CrcTableCacheNode *next;

    node = crcTableCacheHead;
    crcTableCacheHead = NULL;

    while (node != NULL)
    {
        next = node->next;
        free(node->table);
        free(node);
        node = next;
    }
}

u64 crcCalculate(CrcContext *context, const u8 *data, u32 length)
{
    crcInitialize(context, context->config);
    crcUpdate(context, data, length);
    return crcFinalize(context);
}

u32 crcCalculateCrc32POSIX(const u8 *data, u32 length)
{
    extern CrcConfig crc32POSIX;
    CrcContext context;

    u32 remainingLength;
    u8 lengthByte;

    crcInitialize(&context, &crc32POSIX);

    /*
     * 处理原始数据。
     */
    crcUpdate(&context, data, length);

    /*
     * POSIX cksum 还需要继续处理数据长度。
     * 长度按低字节在前的顺序加入 CRC。
     */
    remainingLength = length;

    while (remainingLength != 0)
    {
        lengthByte = (u8)(remainingLength & 0xFF);
        crcUpdate(&context, &lengthByte,1);
        remainingLength >>= 8;
    }

    return (u32)crcFinalize(&context);
}


u8 checksum(const u8 *data, u16 len)
{
	u8 sum = 0;
	while (len-- > 0) {
		sum += *data++;
	}

	return sum;
}

void scrambe(u8 *data, u16 len, u8 sc)
{
	while (len-- > 0) {
		*data++ += sc;
	}
}

CrcConfig crc8SMBUS = {
	8, 0x07, 0x00, 0x00, 0, 0
};

CrcConfig crc16MODBUS = {
	16, 0x8005, 0xFFFF, 0x0000, 1, 1
};

CrcConfig crc16CCITT_FALSE = {
	16, 0x1021, 0xFFFF, 0x0000, 0, 0
};

CrcConfig crc16XMODEM = {
	16, 0x1021, 0x0000, 0x0000, 0, 0
};

CrcConfig crc16KERMIT = {
	16, 0x1021, 0x0000, 0x0000, 1, 1
};

CrcConfig crc16USB = {
	16, 0x8005, 0xFFFF, 0xFFFF, 1, 1
};

CrcConfig crc24LTEA = {
	24, 0x864cfb, 0, 0, 0, 0
};

CrcConfig crc24LTEB = {
	24, 0x800063, 0, 0, 0, 0
};

CrcConfig crc24BLE = {
	24, 0x65b, 0x555555, 0, 1, 1
};

CrcConfig crc32IsoHDLC = {
	32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, 1, 1
};

CrcConfig crc32C = {
	32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, 1, 1
};

CrcConfig crc32MPEG2 = {
	32, 0x04C11DB7, 0xFFFFFFFF, 0x00000000, 0, 0
};

CrcConfig crc32POSIX = {
	32, 0x04C11DB7, 0x00000000, 0xFFFFFFFF, 0, 0
};

CrcConfig crc32AUTOSAR = {
	32, 0xF4ACFB13, 0xFFFFFFFF, 0xFFFFFFFF, 1, 1
};

CrcConfig crc32BZIP2 = {
	32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0
};

CrcConfig crc64Ecma = {
    64, CRC_U64_C(0x42F0E1EBA9EA3693), 0x0000, 0x0000, 0, 0
};

CrcConfig crc64Xz = {
    64, CRC_U64_C(0x42F0E1EBA9EA3693), CRC_U64_C(0xFFFFFFFFFFFFFFFF), CRC_U64_C(0xFFFFFFFFFFFFFFFF), 1, 1
};

CrcConfig crc64We = {
    64, CRC_U64_C(0x42F0E1EBA9EA3693), CRC_U64_C(0xFFFFFFFFFFFFFFFF), CRC_U64_C(0xFFFFFFFFFFFFFFFF), 0, 0
};
