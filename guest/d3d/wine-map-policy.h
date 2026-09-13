/* SPDX-License-Identifier: GPL-2.0-or-later
 * Skip GL binding only when Wine's color location loader would immediately
 * return: the exact requested CPU location is already current. GPU locations,
 * stale copies and depth/stencil keep the original synchronization path.
 */
#ifndef DREAMGPU_WINE_MAP_POLICY_H
#define DREAMGPU_WINE_MAP_POLICY_H
static int dg_wine_cpu_map_is_current(unsigned int usage, unsigned int locations,
                                      unsigned int binding) {
    if (usage & WINED3DUSAGE_DEPTHSTENCIL)
        return 0;
    switch (binding) {
        case WINED3D_LOCATION_SYSMEM:
        case WINED3D_LOCATION_USER_MEMORY:
        case WINED3D_LOCATION_DIB:
            return (locations & binding) != 0;
        default:
            return 0;
    }
}
#endif
