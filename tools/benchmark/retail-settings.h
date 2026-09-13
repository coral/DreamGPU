/* SPDX-License-Identifier: GPL-2.0-or-later
 * Retail build742 chooses its engine through MFC profile settings before
 * forwarding +commands. Its original launcher maps EngineType1=software,
 * 2=OpenGL,3=Direct3D; -gl does not replace this persisted selector.
 * Select only the existing authorized Half-Life installation and verify the
 * ordinary system OpenGL setting before a benchmark can launch.
 */
#ifndef DG_RETAIL_SETTINGS_H
#define DG_RETAIL_SETTINGS_H
static const char *ConfigureRetailGl(void) {
    static const char path[] = "Software\\Valve\\Half-Life\\Settings";
    static const char driver[] = "default";
    HKEY key;
    DWORD value = 0, type = 0, bytes = sizeof(value), i;
    char actual[sizeof(driver)];
    const char *error = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS)
        return "retail-settings-missing";
    if (RegQueryValueExA(key, "EngineType", NULL, &type, (BYTE *)&value, &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(value) || value < 1 || value > 3) {
        error = "retail-engine-setting-invalid";
        goto done;
    }
    /* Retail742 hl.exe selector at 0x443680 maps the literal "default"
     * to a null driver name. hw.dll at 0x10072880 then loads opengl32.dll
     * through the normal Windows loader. Other names receive gldrv\\.
     * Evidence: target/follow-through/half-life-system-loader-v1. */
    /* Keep resolution/window preferences; the fixed command supplies
     * -windowed. No renderer binary, game data or unrelated settings change. */
    value = 2;
    if (RegSetValueExA(key, "EngineGLDriver", 0, REG_SZ, (const BYTE *)driver, sizeof(driver)) !=
            ERROR_SUCCESS ||
        RegSetValueExA(key, "EngineType", 0, REG_DWORD, (const BYTE *)&value, sizeof(value)) !=
            ERROR_SUCCESS) {
        error = "retail-opengl-select-failed";
        goto done;
    }
    value = 0;
    type = 0;
    bytes = sizeof(value);
    if (RegQueryValueExA(key, "EngineType", NULL, &type, (BYTE *)&value, &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(value) || value != 2) {
        error = "retail-engine-readback-failed";
        goto done;
    }
    type = 0;
    bytes = sizeof(actual);
    if (RegQueryValueExA(key, "EngineGLDriver", NULL, &type, (BYTE *)actual, &bytes) !=
            ERROR_SUCCESS ||
        type != REG_SZ || bytes != sizeof(driver)) {
        error = "retail-driver-readback-failed";
        goto done;
    }
    for (i = 0; i < sizeof(driver); ++i)
        if (actual[i] != driver[i]) {
            error = "retail-driver-readback-failed";
            break;
        }
done:
    RegCloseKey(key);
    return error;
}
#endif
