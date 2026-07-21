#ifndef _STUB_LINUX_FS_H
#define _STUB_LINUX_FS_H
#include <sys/ioctl.h>
#define FS_SECRM_FL      0x00000001
#define FS_UNRM_FL       0x00000002
#define FS_COMPR_FL      0x00000004
#define FS_SYNC_FL       0x00000008
#define FS_IMMUTABLE_FL  0x00000010
#define FS_APPEND_FL     0x00000020
#define FS_NODUMP_FL     0x00000040
#define FS_NOATIME_FL    0x00000080
#define FS_DIRSYNC_FL    0x00010000
#define FS_IOC_GETFLAGS  _IOR('f', 1, long)
#define FS_IOC_SETFLAGS  _IOW('f', 2, long)
#endif
