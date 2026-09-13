/* Source-built fixed app-local D3D provider setup after original UT installation. */
#include "common.h"
static UINT Run(void) {
    char root[4], module[MAX_PATH], config[MAX_PATH];
    OwnedHandle log{CreateFileA("C:\\DGUTDS.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, 0, NULL)};
    Log = log.get();
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (!PackageMedia(root, "UTD3D\\DGD3D.INI") || !Join(module, root, "UTD3D\\D3DDrv.dll") ||
        !Join(config, root, "UTD3D\\DGD3D.INI"))
        Die("FAIL PACKAGE_MEDIA");
    if (!Copy(module, "C:\\UT99\\System\\D3DDrv.dll") ||
        !Copy(config, "C:\\UT99\\System\\DGD3D.INI") ||
        !Copy("C:\\SIERRA\\Half-Life\\winedd.dll", "C:\\UT99\\System\\dgddr.dll") ||
        !Copy("C:\\SIERRA\\Half-Life\\wined3d.dll", "C:\\UT99\\System\\wined3d.dll") ||
        !Copy("C:\\SIERRA\\Half-Life\\dgpugl.dll", "C:\\UT99\\System\\dgpugl.dll"))
        Die("FAIL D3D_PROVIDER_COPY");
    Text("PASS automated utdsetup: fixed private D3D module and app-local Wine/DreamGPU "
         "providers\r\n");
    return 0;
}

extern "C" void WINAPI WinMainCRTStartup(void) {
    ExitProcess(Run());
}
