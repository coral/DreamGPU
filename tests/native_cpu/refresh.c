/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../../vendor/qemu/ui/dreamgpu-refresh.h"
int main(void) {
    const uint32_t rates[] = {59940, 60000, 75000, 85000, 100000, 120000, 144000, 240000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        DreamGpuRefresh c;
        dreamgpu_refresh_set(&c, rates[r], 0);
        uint64_t now = 0;
        for (unsigned i = 0; i < 10000; i++) {
            int ms = dreamgpu_refresh_delay(&c, now);
            assert(ms > 0 && ms <= 101);
            now += (uint64_t)ms * 1000;
            uint64_t ideal_ceil = ((uint64_t)(i + 1) * 1000000000 + rates[r] - 1) / rates[r];
            assert(now >= ideal_ceil && now - ideal_ceil < 1000);
        }
        now += 10000000; /* long pause skips deadlines with no catch-up loop */
        int ms = dreamgpu_refresh_delay(&c, now);
        assert(ms > 0 && ms <= 101);
    }
    DreamGpuRefresh c;
    dreamgpu_refresh_set(&c, 120000, 0);
    assert(dreamgpu_refresh_delay(&c, 8333) == 1);
    return 0;
}
