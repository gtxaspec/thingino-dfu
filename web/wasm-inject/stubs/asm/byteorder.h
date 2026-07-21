#ifndef _STUB_ASM_BYTEORDER_H
#define _STUB_ASM_BYTEORDER_H
#include <endian.h>
#define __cpu_to_le16(x) htole16(x)
#define __cpu_to_le32(x) htole32(x)
#define __cpu_to_le64(x) htole64(x)
#define __le16_to_cpu(x) le16toh(x)
#define __le32_to_cpu(x) le32toh(x)
#define __le64_to_cpu(x) le64toh(x)
#define __cpu_to_be16(x) htobe16(x)
#define __cpu_to_be32(x) htobe32(x)
#define __cpu_to_be64(x) htobe64(x)
#define __be16_to_cpu(x) be16toh(x)
#define __be32_to_cpu(x) be32toh(x)
#define __be64_to_cpu(x) be64toh(x)
#endif
