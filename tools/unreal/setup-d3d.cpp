/* Normal system D3D configuration after original UT installation. */
#include "common.h"
#include "system-provider.h"
static UINT Run(void) {
    char root[4], module[MAX_PATH], config[MAX_PATH];
    OwnedHandle log{CreateFileA("C:\\DGUTDS.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, 0, NULL)};
    Log = log.get();
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (!PackageMedia(root, "UTD3D\\DGD3D.INI") ||
        !Join(module, root, "UT99\\System\\D3DDrv.dll") || !Join(config, root, "UTD3D\\DGD3D.INI"))
        Die("FAIL PACKAGE_MEDIA");
    if (!UtCleanDirectory("C:\\UT99\\System"))
        Die("FAIL APP_LOCAL_PROVIDER");
    /* Restore the original game renderer from the original-media tree, never
     * the historical private-loader module in UTD3D. */
    if (!Copy(module, "C:\\UT99\\System\\D3DDrv.dll") ||
        !Copy(config, "C:\\UT99\\System\\DGD3D.INI"))
        Die("FAIL D3D_CONFIG_COPY");
    Text("PASS automated utdsetup: original D3D module and normal system providers\r\n");
    return 0;
}

extern "C" void WINAPI WinMainCRTStartup(void) {
    ExitProcess(Run());
}
