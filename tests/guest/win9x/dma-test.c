/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DG_DMA_TEST 1
#include "../../../tools/win9x/dma32.c"
static void Reset(void) {
    Zero(Changes, sizeof(Changes));
    ChangeCount = ChannelMask = LastError = Writes = Deletes = BackupWrites = SetFailure =
        FlushFailure = 0;
    IoFailed = WrongController = WrongPort = DuplicateUnit = WrongMaster = BackupFailure =
        BackupExists = FALSE;
    Values[0] = 0;
    Values[1] = 0;
    ValueExists[0] = TRUE;
    ValueExists[1] = FALSE;
    Log = 1;
}
int main(void) {
    assert(PiiX("PCI\\VEN_8086&DEV_7010&SUBSYS_0000\\0"));
    assert(!PiiX("PCI\\VEN_8086&DEV_70100\\0"));
    assert(!PiiX("PCI\\VEN_8086&DEV_7010"));
    assert(ChannelKey("hdc\\0007"));
    assert(!ChannelKey("hdc\\..\\x"));
    assert(!ChannelKey("hdc\\0007\\x"));
    assert(!ExactMulti("MF\\GOODPRIMARY", 14, "MF\\GOODPRIMARY"));
#if DG_DMA_READONLY
    Reset();
    assert(Run() && Writes == 0 && BackupWrites == 0 && ChangeCount == 2);
    puts("PASS actual DMA readonly helper: complete typed preflight with zero mutations");
    return 0;
#endif
    Reset();
    assert(Run());
    assert(Writes == 2 && ChangeCount == 2 && Values[0] == 1 && Values[1] == 1);
    assert(!strcmp(Changes[0].key, "System\\CurrentControlSet\\Services\\Class\\hdc\\0007"));
    assert(!strcmp(Changes[1].name, "IDEDMADrive1"));
    Reset();
    Values[0] = Values[1] = 1;
    ValueExists[1] = TRUE;
    BackupExists = TRUE;
    assert(Run() && Writes == 0 && BackupWrites == 0);
    Reset();
    WrongController = TRUE;
    assert(!Run() && Writes == 0);
    Reset();
    WrongPort = TRUE;
    assert(!Run() && Writes == 0);
    Reset();
    DuplicateUnit = TRUE;
    assert(!Run() && Writes == 0);
    Reset();
    WrongMaster = TRUE;
    assert(!Run() && Writes == 0);
    Reset();
    BackupFailure = TRUE;
    assert(!Run() && Writes == 0);
    Reset();
    BackupExists = TRUE;
    assert(!Run() && Writes == 0);
    Reset();
    SetFailure = 2;
    assert(!Run());
    assert(Writes == 3 && Values[0] == 0 && !ValueExists[1]);
    Reset();
    FlushFailure = 1;
    assert(!Run());
    assert(Writes == 2 && Values[0] == 0 && !ValueExists[1]);
    puts("PASS actual DMA helper: exact topology, typed units, durable backup, preflight "
         "rejection, write/flush rollback");
    return 0;
}
