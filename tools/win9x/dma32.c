/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed Win98 ESDI_506 DMA request for present drives beneath the QEMU PIIX3.
 * This requests DMA; only a subsequent cold boot and actual IDE DMA commands
 * prove that the storage driver uses it. DMACurrentlyUsed is never written.
 */
#define WIN32_LEAN_AND_MEAN
#ifdef DG_DMA_TEST
#include "../../tests/guest/win9x/dma-test-shim.h"
#else
#include <windows.h>

#include <cfgmgr32.h>
#include <setupapi.h>
#endif
#ifndef DG_DMA_READONLY
#define DG_DMA_READONLY 0
#endif
#define LIMIT 512
#define PATH_BYTES 512
#define VALUE_BYTES 16
static HANDLE Log, Backup = INVALID_HANDLE_VALUE;
static BOOL IoFailed;
static HDEVINFO Devices;
typedef struct {
    char key[PATH_BYTES];
    char name[32];
    DWORD type, bytes;
    BYTE data[VALUE_BYTES];
    BOOL existed, changed;
} Change;
static Change Changes[4];
static DWORD ChangeCount, ChannelMask;
static void Zero(void *p, DWORD n) {
    BYTE *b = p;
    while (n--)
        *b++ = 0;
}
static void WriteLine(HANDLE file, const char *text) {
    DWORD n = 0, bytes = lstrlenA(text);
    if (!WriteFile(file, text, bytes, &n, NULL) || n != bytes ||
        !WriteFile(file, "\r\n", 2, &n, NULL) || n != 2 || !FlushFileBuffers(file))
        IoFailed = TRUE;
}
static void Text(const char *label, const char *value) {
    char line[1100];
    wsprintfA(line, "%s: %s", label, value);
    WriteLine(Log, line);
}
static BOOL Fail(const char *stage, DWORD value) {
    char line[256];
    wsprintfA(line, "FAIL %s 0x%08lx", stage, value);
    WriteLine(Log, line);
    return FALSE;
}
static BOOL EqualPrefix(const char *s, const char *prefix) {
    while (*prefix) {
        char a = *s++, b = *prefix++;
        if (a >= 'a' && a <= 'z')
            a -= 32;
        if (b >= 'a' && b <= 'z')
            b -= 32;
        if (a != b)
            return FALSE;
    }
    return TRUE;
}
static BOOL ExactMulti(const char *data, DWORD bytes, const char *id) {
    DWORD i = 0;
    while (i < bytes && data[i]) {
        DWORD start = i;
        while (i < bytes && data[i])
            i++;
        if (i == bytes)
            return FALSE;
        if (!lstrcmpiA(data + start, id))
            return TRUE;
        i++;
    }
    return FALSE;
}
static BOOL Id(DEVINST node, char *id) {
    Zero(id, MAX_DEVICE_ID_LEN);
    return CM_Get_Device_IDA(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS &&
           id[MAX_DEVICE_ID_LEN - 1] == 0;
}
/* Win98 SetupAPI reports legacy REG_SZ and does not reliably update the
 * RequiredSize argument for HardwareID. Read the typed value under this exact
 * present CM device ID instead of assuming NT REG_MULTI_SZ packing. */
static BOOL HardwareIds(DEVINST node, char *ids, DWORD capacity, DWORD *type, DWORD *bytes) {
    char id[MAX_DEVICE_ID_LEN], path[PATH_BYTES];
    HKEY key;
    LONG rc;
    DWORD i;
    *type = 0;
    *bytes = 0;
    if (!Id(node, id))
        return FALSE;
    wsprintfA(path, "Enum\\%s", id);
    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS)
        return FALSE;
    *bytes = capacity;
    rc = RegQueryValueExA(key, "HardwareID", NULL, type, (BYTE *)ids, bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || !*bytes || *bytes > capacity || ids[*bytes - 1])
        return FALSE;
    if (*type == REG_SZ) {
        for (i = 0; i + 1 < *bytes; i++)
            if (ids[i] == ',')
                ids[i] = 0;
        return TRUE;
    }
    return *type == REG_MULTI_SZ;
}
static BOOL PiiX(const char *id) {
    const char *prefix = "PCI\\VEN_8086&DEV_7010";
    DWORD n = lstrlenA(prefix);
    return EqualPrefix(id, prefix) && (id[n] == '&' || id[n] == '\\');
}
static BOOL ChannelKey(const char *key) {
    DWORD i;
    if (lstrlenA(key) != 8 || !EqualPrefix(key, "hdc\\"))
        return FALSE;
    for (i = 4; i < 8; i++)
        if (key[i] < '0' || key[i] > '9')
            return FALSE;
    return TRUE;
}
static BOOL StringValue(HKEY key, const char *name, char *data, DWORD capacity) {
    DWORD type, n = capacity;
    LONG rc;
    Zero(data, capacity);
    rc = RegQueryValueExA(key, name, NULL, &type, (BYTE *)data, &n);
    return rc == ERROR_SUCCESS && type == REG_SZ && n > 0 && n <= capacity && data[n - 1] == 0;
}
static void BytesLine(HANDLE file, const char *label, DWORD type, const BYTE *data, DWORD bytes,
                      BOOL exists) {
    char line[256];
    DWORD i, pos = wsprintfA(line, "%s present=%lu type=%lu bytes=%lu hex=", label, (DWORD)exists,
                             type, bytes);
    for (i = 0; i < bytes && i < VALUE_BYTES; i++)
        pos += wsprintfA(line + pos, "%02x", data[i]);
    WriteLine(file, line);
}
static BOOL Present(DEVINST node) {
    SP_DEVINFO_DATA info;
    DWORD i;
    Zero(&info, sizeof(info));
    info.cbSize = sizeof(info);
    for (i = 0; i < LIMIT && SetupDiEnumDeviceInfo(Devices, i, &info); i++)
        if (info.DevInst == node)
            return TRUE;
    return FALSE;
}
static BOOL Disk(DEVINST node, const char *channelKey, DWORD *units) {
    char id[MAX_DEVICE_ID_LEN], path[PATH_BYTES], kind[32], product[128];
    HKEY key;
    BYTE master = 0, dma[VALUE_BYTES];
    DWORD type, bytes, unit;
    LONG rc;
    Change *change;
    if (!Present(node))
        return Fail("channel child not present", node);
    if (!Id(node, id))
        return Fail("drive identity", node);
    Text("present drive", id);
    wsprintfA(path, "Enum\\%s", id);
    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS)
        return Fail("drive enum registry", rc);
    if (!StringValue(key, "Class", kind, sizeof(kind)) ||
        (!lstrcmpiA(kind, "DiskDrive") ? !EqualPrefix(id, "ESDI\\")
                                       : lstrcmpiA(kind, "CDROM") || !EqualPrefix(id, "SCSI\\"))) {
        RegCloseKey(key);
        return Fail("unexpected drive class or enumerator", node);
    }
    if (StringValue(key, "ProductId", product, sizeof(product)))
        Text("drive product", product);
    bytes = sizeof(master);
    rc = RegQueryValueExA(key, "IDEMaster", NULL, &type, &master, &bytes);
    if (rc != ERROR_SUCCESS || type != REG_BINARY || bytes != 1 || master > 1) {
        RegCloseKey(key);
        return Fail("typed IDEMaster missing or invalid", rc);
    }
    unit = master ? 0 : 1;
    bytes = sizeof(dma);
    Zero(dma, sizeof(dma));
    rc = RegQueryValueExA(key, "DMACurrentlyUsed", NULL, &type, dma, &bytes);
    if (rc == ERROR_SUCCESS && bytes <= sizeof(dma))
        BytesLine(Log, "observed DMACurrentlyUsed", type, dma, bytes, TRUE);
    else if (rc == ERROR_FILE_NOT_FOUND)
        BytesLine(Log, "observed DMACurrentlyUsed", 0, dma, 0, FALSE);
    else {
        RegCloseKey(key);
        return Fail("bounded DMA outcome query", rc);
    }
    RegCloseKey(key);
    if ((*units & (1UL << unit)) || ChangeCount == 4)
        return Fail("ambiguous drive slot", unit);
    *units |= 1UL << unit;
    change = &Changes[ChangeCount++];
    lstrcpyA(change->key, channelKey);
    wsprintfA(change->name, "IDEDMADrive%lu", unit);
    return TRUE;
}
static BOOL Channel(SP_DEVINFO_DATA *device, DWORD index) {
    char id[MAX_DEVICE_ID_LEN], parentId[MAX_DEVICE_ID_LEN], driver[64], path[PATH_BYTES], port[64];
    DEVINST parent, child;
    HKEY key;
    DWORD type, bytes, units = 0, count = 0;
    CONFIGRET cr;
    LONG rc;
    if (ChannelMask & (1UL << index))
        return Fail("duplicate present IDE channel", index);
    if (!Id(device->DevInst, id))
        return Fail("channel identity", device->DevInst);
    Text("present channel", id);
    cr = CM_Get_Parent(&parent, device->DevInst, 0);
    if (cr != CR_SUCCESS || !Present(parent) || !Id(parent, parentId) || !PiiX(parentId))
        return Fail("channel parent is not present exact PIIX3", cr);
    Text("controller", parentId);
    bytes = sizeof(driver);
    Zero(driver, sizeof(driver));
    if (!SetupDiGetDeviceRegistryPropertyA(Devices, device, SPDRP_DRIVER, &type, (BYTE *)driver,
                                           bytes, &bytes) ||
        type != REG_SZ || !bytes || bytes > sizeof(driver) || driver[bytes - 1] ||
        !ChannelKey(driver))
        return Fail("dynamic hdc key", GetLastError());
    wsprintfA(path, "System\\CurrentControlSet\\Services\\Class\\%s", driver);
    Text("channel registry", path);
    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS)
        return Fail("open channel registry", rc);
    if (!StringValue(key, "PortDriver", port, sizeof(port)) || lstrcmpiA(port, "ESDI_506.pdr")) {
        RegCloseKey(key);
        return Fail("channel PortDriver is not ESDI_506.pdr", index);
    }
    RegCloseKey(key);
    cr = CM_Get_Child(&child, device->DevInst, 0);
    while (cr == CR_SUCCESS) {
        DEVINST next;
        if (count++ == 8)
            return Fail("bounded channel children", count);
        if (!Disk(child, path, &units))
            return FALSE;
        cr = CM_Get_Sibling(&next, child, 0);
        if (cr == CR_SUCCESS)
            child = next;
    }
    if (cr != CR_NO_SUCH_DEVNODE)
        return Fail("channel child enumeration", cr);
    ChannelMask |= 1UL << index;
    return TRUE;
}
static BOOL ReadOriginal(Change *change) {
    HKEY key;
    LONG rc;
    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, change->key, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS)
        return Fail("original registry open", rc);
    change->bytes = sizeof(change->data);
    rc = RegQueryValueExA(key, change->name, NULL, &change->type, change->data, &change->bytes);
    RegCloseKey(key);
    if (rc == ERROR_FILE_NOT_FOUND) {
        change->existed = FALSE;
        change->bytes = 0;
        change->type = 0;
    } else if (rc == ERROR_SUCCESS && change->bytes <= VALUE_BYTES) {
        change->existed = TRUE;
    } else
        return Fail("bounded original registry value", rc);
    Text("request", change->name);
    BytesLine(Log, "before", change->type, change->data, change->bytes, change->existed);
    return TRUE;
}
static BOOL Rollback(void) {
    DWORD i;
    BOOL okay = TRUE;
    for (i = 0; i < ChangeCount; i++) {
        Change *c = &Changes[i];
        HKEY key;
        LONG rc;
        if (!c->changed)
            continue;
        rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, c->key, 0, KEY_SET_VALUE, &key);
        if (rc != ERROR_SUCCESS) {
            okay = Fail("rollback key open", rc);
            continue;
        }
        rc = c->existed ? RegSetValueExA(key, c->name, 0, c->type, c->data, c->bytes)
                        : RegDeleteValueA(key, c->name);
        if (rc != ERROR_SUCCESS)
            okay = Fail("rollback value", rc);
        if (RegFlushKey(key) != ERROR_SUCCESS)
            okay = Fail("rollback flush", GetLastError());
        RegCloseKey(key);
    }
    WriteLine(Log, okay ? "Rollback completed"
                        : "Rollback incomplete: original values preserved in C:\\DGDMA.BAK");
    return okay;
}
static BOOL Apply(void) {
    DWORD i, pending = 0;
    BYTE enabled = 1;
    for (i = 0; i < ChangeCount; i++) {
        Change *c = &Changes[i];
        if (!c->existed || c->type != REG_BINARY || c->bytes != 1 || c->data[0] != 1)
            pending++;
    }
    if (!pending) {
        WriteLine(Log, "All present drive DMA requests already enabled; no writes");
        return !IoFailed;
    }
    Backup = CreateFileA("C:\\DGDMA.BAK", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (Backup == INVALID_HANDLE_VALUE)
        return Fail("preserve originals without replacing prior backup", GetLastError());
    for (i = 0; i < ChangeCount; i++) {
        Change *c = &Changes[i];
        WriteLine(Backup, c->key);
        WriteLine(Backup, c->name);
        BytesLine(Backup, "original", c->type, c->data, c->bytes, c->existed);
    }
    if (IoFailed || !FlushFileBuffers(Backup)) {
        CloseHandle(Backup);
        return Fail("original backup flush", GetLastError());
    }
    CloseHandle(Backup);
    for (i = 0; i < ChangeCount; i++) {
        Change *c = &Changes[i];
        HKEY key;
        LONG rc;
        DWORD type, n = 1;
        BYTE actual = 0;
        rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, c->key, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
        if (rc != ERROR_SUCCESS) {
            Rollback();
            return Fail("DMA request key open", rc);
        }
        rc = RegSetValueExA(key, c->name, 0, REG_BINARY, &enabled, 1);
        if (rc == ERROR_SUCCESS)
            c->changed = TRUE;
        if (rc == ERROR_SUCCESS)
            rc = RegFlushKey(key);
        if (rc == ERROR_SUCCESS)
            rc = RegQueryValueExA(key, c->name, NULL, &type, &actual, &n);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS || type != REG_BINARY || n != 1 || actual != 1) {
            Rollback();
            return Fail("DMA request write/flush/readback", rc);
        }
        Text("updated channel", c->key);
        Text("updated value", c->name);
        BytesLine(Log, "after", REG_BINARY, &actual, 1, TRUE);
    }
    return TRUE;
}
static BOOL Run(void) {
    SP_DEVINFO_DATA device;
    DWORD i, type, bytes;
    char ids[4096];
    BOOL okay = FALSE;
    if (!(GetVersion() & 0x80000000UL))
        return Fail("requires Win9x", 0);
    Devices = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (Devices == INVALID_HANDLE_VALUE)
        return Fail("present device enumeration", GetLastError());
    Zero(&device, sizeof(device));
    device.cbSize = sizeof(device);
    for (i = 0; i < LIMIT && SetupDiEnumDeviceInfo(Devices, i, &device); i++) {
        Zero(ids, sizeof(ids));
        bytes = 0;
        {
            char nodeId[MAX_DEVICE_ID_LEN];
            BOOL property = HardwareIds(device.DevInst, ids, sizeof(ids), &type, &bytes);
            DWORD propertyError = property ? 0 : GetLastError();
            if (Id(device.DevInst, nodeId) && (EqualPrefix(nodeId, "MF\\") || PiiX(nodeId))) {
                char detail[128], shown[513];
                DWORD j;
                Text("candidate node", nodeId);
                wsprintfA(detail, "success=%lu type=%lu bytes=%lu error=%lu", (DWORD)property, type,
                          bytes, propertyError);
                Text("hardware property", detail);
                if (property && bytes <= 512) {
                    for (j = 0; j < bytes; j++)
                        shown[j] = ids[j] ? ids[j] : '|';
                    shown[bytes] = 0;
                    Text("hardware IDs (NUL=|)", shown);
                }
            }
            if (property && bytes <= sizeof(ids)) {
                if (ExactMulti(ids, bytes, "MF\\GOODPRIMARY")) {
                    if (!Channel(&device, 0))
                        goto done;
                } else if (ExactMulti(ids, bytes, "MF\\GOODSECONDARY")) {
                    if (!Channel(&device, 1))
                        goto done;
                }
            }
        }
    }
    if (i == LIMIT) {
        Fail("bounded device enumeration", i);
        goto done;
    }
    if (GetLastError() != ERROR_NO_MORE_ITEMS) {
        Fail("device enumeration failure", GetLastError());
        goto done;
    }
    if (ChannelMask != 3 || !ChangeCount) {
        Fail("expected both PIIX3 channels with present drives", ChannelMask);
        goto done;
    }
    for (i = 0; i < ChangeCount; i++)
        if (!ReadOriginal(&Changes[i]))
            goto done;
    okay = DG_DMA_READONLY ? TRUE : Apply();
done:
    SetupDiDestroyDeviceInfoList(Devices);
    return okay;
}
void WINAPI WinMainCRTStartup(void) {
    BOOL okay;
    Log = CreateFileA("C:\\DGDRV.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    WriteLine(Log, DG_DMA_READONLY ? "Readonly PIIX3 DMA topology and settings"
                                   : "Fixed PIIX3 ESDI_506 DMA request");
    okay = Run();
    WriteLine(Log, okay ? (DG_DMA_READONLY ? "PASS automated update98: readonly DMA preflight; no "
                                             "settings changed"
                                           : "PASS automated update98: DMA requests read back; "
                                             "cold restart and native DMA command proof required")
                        : "FAIL fixed DMA preflight/update");
    CloseHandle(Log);
    ExitProcess(okay ? 0 : 1);
}
