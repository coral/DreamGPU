/* Win16 loader/mode diagnostic. Reads driver state; does not change modes. */
#include <windows.h>

typedef struct {
    UINT size, bpp;
    int width, height;
} DG_MODE;
typedef UINT(FAR PASCAL *VALIDATE_MODE)(DG_MODE FAR *);

static HFILE output;
static void line(LPCSTR text) {
    _lwrite(output, text, lstrlen(text));
    _lwrite(output, "\r\n", 2);
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    HINSTANCE driver;
    VALIDATE_MODE validate;
    DG_MODE mode;
    char buffer[256];
    HDC screen;
    (void)instance;
    (void)previous;
    (void)show;
    output = _lcreat("C:\\DG9DIAG.LOG", 0);
    if (output == HFILE_ERROR)
        return 1;
    screen = GetDC(NULL);
    wsprintf(buffer, "Desktop %dx%d %d bpp", GetDeviceCaps(screen, HORZRES),
             GetDeviceCaps(screen, VERTRES),
             GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES));
    line(buffer);
    ReleaseDC(NULL, screen);
    if (!command || !*command)
        command = "dgpumini.drv";
    line(command);
    driver = LoadLibrary(command);
    wsprintf(buffer, "LoadLibrary result = %u", driver);
    line(buffer);
    if ((UINT)driver >= 32) {
        if (GetModuleFileName(driver, buffer, sizeof(buffer)))
            line(buffer);
        validate = (VALIDATE_MODE)GetProcAddress(driver, "ValidateMode");
        line(validate ? "ValidateMode export found" : "ValidateMode export missing");
        if (validate) {
            mode.size = sizeof(mode);
            mode.bpp = 32;
            mode.width = 1024;
            mode.height = 768;
            wsprintf(buffer, "ValidateMode 1024x768x32 = %u (0 means supported)", validate(&mode));
            line(buffer);
        }
        FreeLibrary(driver);
    }
    _lclose(output);
    MessageBox(NULL, "Results written to C:\\DG9DIAG.LOG", "DreamGPU Win9x driver diagnostic",
               MB_OK);
    return 0;
}
