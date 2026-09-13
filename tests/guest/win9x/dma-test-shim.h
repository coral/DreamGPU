/* SPDX-License-Identifier: GPL-2.0-or-later */
/* host-only API seam for actual dma32.c tests. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef unsigned long DWORD, DEVINST, CONFIGRET;
typedef long LONG, HANDLE, HKEY, HDEVINFO;
typedef unsigned char BYTE;
typedef int BOOL;
typedef struct {
    DWORD cbSize, DevInst;
} SP_DEVINFO_DATA;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE (-1)
#define HKEY_LOCAL_MACHINE 0
#define MAX_DEVICE_ID_LEN 200
#define KEY_READ 1
#define KEY_SET_VALUE 2
#define KEY_QUERY_VALUE 4
#define GENERIC_WRITE 1
#define FILE_SHARE_READ 1
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0
#define REG_SZ 1
#define REG_BINARY 3
#define REG_MULTI_SZ 7
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_MORE_DATA 234
#define CR_SUCCESS 0
#define CR_NO_SUCH_DEVNODE 13
#define SPDRP_DRIVER 9
#define SPDRP_HARDWAREID 1
#define DIGCF_PRESENT 2
#define DIGCF_ALLCLASSES 4
#define lstrlenA(s) ((DWORD)strlen(s))
#define lstrcmpiA strcasecmp
#define lstrcpyA strcpy
#define wsprintfA(dest, ...) snprintf((dest), __builtin_object_size((dest), 0), __VA_ARGS__)
static DWORD LastError, Writes, Deletes, BackupWrites, SetFailure, FlushFailure;
static BOOL WrongController, WrongPort, DuplicateUnit, WrongMaster, BackupFailure, BackupExists;
static BYTE Values[2];
static BOOL ValueExists[2];
static DWORD GetLastError(void) {
    return LastError;
}
static DWORD GetVersion(void) {
    return 0x80000000UL;
}
static BOOL WriteFile(HANDLE file, const void *data, DWORD bytes, DWORD *done, void *overlap) {
    (void)data;
    (void)overlap;
    *done = bytes;
    if (file == 2) {
        BackupWrites++;
        if (BackupFailure) {
            *done = 0;
            return FALSE;
        }
    }
    return TRUE;
}
static BOOL FlushFileBuffers(HANDLE h) {
    (void)h;
    return TRUE;
}
static HANDLE CreateFileA(const char *path, DWORD a, DWORD b, void *c, DWORD d, DWORD e, void *f) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    assert(!strcmp(path, "C:\\DGDMA.BAK") || !strcmp(path, "C:\\DGDRV.LOG"));
    return BackupExists ? -1 : 2;
}
static BOOL CloseHandle(HANDLE h) {
    (void)h;
    return TRUE;
}
static void ExitProcess(DWORD code) {
    exit((int)code);
}
static HDEVINFO SetupDiGetClassDevsA(void *a, void *b, void *c, DWORD flags) {
    (void)a;
    (void)b;
    (void)c;
    assert(flags == (DIGCF_PRESENT | DIGCF_ALLCLASSES));
    return 1;
}
static BOOL SetupDiDestroyDeviceInfoList(HDEVINFO set) {
    (void)set;
    return TRUE;
}
static BOOL SetupDiEnumDeviceInfo(HDEVINFO set, DWORD index, SP_DEVINFO_DATA *info) {
    (void)set;
    if (index >= 5) {
        LastError = ERROR_NO_MORE_ITEMS;
        return FALSE;
    }
    info->DevInst = index + 1;
    return TRUE;
}
static const char *Ids[] = {"",
                            "PCI\\VEN_8086&DEV_7010&SUBSYS_0000\\0",
                            "MF\\CHILD0000\\PIIX",
                            "MF\\CHILD0001\\PIIX",
                            "ESDI\\GENERIC_IDE_DISK_TYPE47\\MASTER",
                            "SCSI\\CDROMQEMU_DVD-ROM\\SLAVE"};
static CONFIGRET CM_Get_Device_IDA(DEVINST node, char *id, DWORD bytes, DWORD flags) {
    (void)bytes;
    (void)flags;
    assert(node <= 5);
    strcpy(id, node == 1 && WrongController ? "PCI\\VEN_8086&DEV_9999\\0" : Ids[node]);
    return CR_SUCCESS;
}
static CONFIGRET CM_Get_Parent(DEVINST *parent, DEVINST node, DWORD flags) {
    (void)flags;
    assert(node == 2 || node == 3);
    *parent = 1;
    return CR_SUCCESS;
}
static CONFIGRET CM_Get_Child(DEVINST *child, DEVINST node, DWORD flags) {
    (void)flags;
    assert(node == 2 || node == 3);
    if (node == 2) {
        *child = 4;
        return CR_SUCCESS;
    }
    return CR_NO_SUCH_DEVNODE;
}
static CONFIGRET CM_Get_Sibling(DEVINST *next, DEVINST node, DWORD flags) {
    (void)flags;
    if (node == 4) {
        *next = 5;
        return CR_SUCCESS;
    }
    return CR_NO_SUCH_DEVNODE;
}
static BOOL SetupDiGetDeviceRegistryPropertyA(HDEVINFO set, SP_DEVINFO_DATA *device, DWORD prop,
                                              DWORD *type, BYTE *out, DWORD cap, DWORD *bytes) {
    const char *s = "\0";
    DWORD size;
    (void)set;
    if (prop == SPDRP_HARDWAREID) {
        *type = REG_MULTI_SZ;
        if (device->DevInst == 2)
            s = "MF\\GOODPRIMARY\0";
        if (device->DevInst == 3)
            s = "MF\\GOODSECONDARY\0";
        size = strlen(s) + 2;
    } else {
        assert(prop == SPDRP_DRIVER);
        *type = REG_SZ;
        s = device->DevInst == 2 ? "hdc\\0007" : "hdc\\0012";
        size = strlen(s) + 1;
    }
    assert(size <= cap);
    memcpy(out, s, size);
    *bytes = size;
    return TRUE;
}
static LONG RegOpenKeyExA(HKEY root, const char *path, DWORD zero, DWORD rights, HKEY *out) {
    (void)zero;
    (void)rights;
    assert(root == HKEY_LOCAL_MACHINE);
    if (strstr(path, "Class\\hdc\\0007"))
        *out = 10;
    else if (strstr(path, "Class\\hdc\\0012"))
        *out = 11;
    else if (strstr(path, "Enum\\PCI\\"))
        *out = 31;
    else if (strstr(path, "Enum\\MF\\CHILD0000"))
        *out = 32;
    else if (strstr(path, "Enum\\MF\\CHILD0001"))
        *out = 33;
    else if (strstr(path, "Enum\\ESDI\\"))
        *out = 20;
    else if (strstr(path, "Enum\\SCSI\\"))
        *out = 21;
    else
        assert(0);
    return ERROR_SUCCESS;
}
static LONG RegCloseKey(HKEY key) {
    (void)key;
    return ERROR_SUCCESS;
}
static LONG Result(DWORD *type, BYTE *data, DWORD *bytes, DWORD kind, const void *source, DWORD n) {
    if (*bytes < n) {
        *bytes = n;
        return ERROR_MORE_DATA;
    }
    *type = kind;
    memcpy(data, source, n);
    *bytes = n;
    return ERROR_SUCCESS;
}
static LONG RegQueryValueExA(HKEY key, const char *name, void *reserved, DWORD *type, BYTE *data,
                             DWORD *bytes) {
    const char *s = NULL;
    BYTE v;
    DWORD unit;
    (void)reserved;
    if (!strcmp(name, "HardwareID")) {
        s = key == 32   ? "MF\\GOODPRIMARY,MF\\CHILD0000"
            : key == 33 ? "MF\\GOODSECONDARY,MF\\CHILD0001"
                        : "GENERIC";
        return Result(type, data, bytes, REG_SZ, s, strlen(s) + 1);
    }
    if (!strcmp(name, "PortDriver"))
        s = WrongPort ? "OTHER.pdr" : "ESDI_506.pdr";
    else if (!strcmp(name, "Class"))
        s = key == 20 ? "DiskDrive" : "CDROM";
    else if (!strcmp(name, "ProductId"))
        s = key == 20 ? "IDE DISK TYPE47" : "QEMU DVD-ROM";
    if (s)
        return Result(type, data, bytes, REG_SZ, s, strlen(s) + 1);
    if (!strcmp(name, "IDEMaster")) {
        v = (key == 20 || DuplicateUnit) ? 1 : 0;
        return Result(type, data, bytes, WrongMaster ? REG_SZ : REG_BINARY, &v, 1);
    }
    if (!strcmp(name, "DMACurrentlyUsed")) {
        v = 0;
        return Result(type, data, bytes, REG_BINARY, &v, 1);
    }
    assert(key == 10);
    assert(!strcmp(name, "IDEDMADrive0") || !strcmp(name, "IDEDMADrive1"));
    unit = name[11] - '0';
    assert(unit < 2);
    if (!ValueExists[unit])
        return ERROR_FILE_NOT_FOUND;
    return Result(type, data, bytes, REG_BINARY, &Values[unit], 1);
}
static LONG RegSetValueExA(HKEY key, const char *name, DWORD zero, DWORD type, const BYTE *value,
                           DWORD bytes) {
    DWORD unit;
    (void)zero;
    assert(key == 10);
    assert(BackupWrites > 0);
    assert(!strncmp(name, "IDEDMADrive", 11));
    unit = name[11] - '0';
    assert(unit < 2 && type == REG_BINARY && bytes == 1);
    Writes++;
    if (SetFailure && Writes == SetFailure)
        return 5;
    Values[unit] = *value;
    ValueExists[unit] = TRUE;
    return ERROR_SUCCESS;
}
static LONG RegDeleteValueA(HKEY key, const char *name) {
    DWORD unit = name[11] - '0';
    assert(key == 10 && unit < 2);
    Deletes++;
    ValueExists[unit] = FALSE;
    return ERROR_SUCCESS;
}
static LONG RegFlushKey(HKEY key) {
    (void)key;
    if (FlushFailure) {
        FlushFailure--;
        return 5;
    }
    return ERROR_SUCCESS;
}
