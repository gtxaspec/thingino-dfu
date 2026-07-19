#ifndef _STUB_LINUX_TYPES_H
#define _STUB_LINUX_TYPES_H
#include <stdint.h>
#include <sys/types.h>
typedef uint8_t  __u8;  typedef int8_t  __s8;
typedef uint16_t __u16; typedef int16_t __s16;
typedef uint32_t __u32; typedef int32_t __s32;
typedef uint64_t __u64; typedef int64_t __s64;
typedef __u16 __le16;   typedef __u16 __be16;
typedef __u32 __le32;   typedef __u32 __be32;
typedef __u64 __le64;   typedef __u64 __be64;
typedef __u16 __sum16;  typedef __u32 __wsum;
typedef long long __kernel_loff_t; typedef long __kernel_off_t;
typedef int __kernel_pid_t; typedef long __kernel_time_t;
typedef unsigned int __kernel_uid32_t; typedef unsigned int __kernel_gid32_t;
#endif
