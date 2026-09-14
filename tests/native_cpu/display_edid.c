/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dreamgpu-edid.h"

int main(int argc, char **argv) {
    uint8_t edid[256];
    dg_edid_generate(edid);
    assert(edid[18] == 1 && edid[19] == 4 && edid[126] == 1);
    for (unsigned block = 0; block < 2; ++block) {
        unsigned sum = 0;
        for (unsigned i = 0; i < 128; ++i)
            sum += edid[block * 128 + i];
        assert(!(sum & 255));
    }
    const unsigned offsets[] = {54, 72, 108};
    const unsigned widths[] = {1024, 3840, 3200};
    const unsigned heights[] = {768, 2160, 2400};
    for (unsigned i = 0; i < 3; ++i) {
        const uint8_t *d = edid + offsets[i];
        unsigned w = d[2] | (d[4] >> 4) << 8;
        unsigned h = d[5] | (d[7] >> 4) << 8;
        unsigned ht = w + d[3] + ((d[4] & 15) << 8);
        unsigned vt = h + d[6] + ((d[7] & 15) << 8);
        uint64_t clock = (d[0] | d[1] << 8) * 10000ULL;
        assert(w == widths[i] && h == heights[i]);
        assert(clock * 1000 / (ht * vt) >= 59990);
        assert(clock * 1000 / (ht * vt) <= 60010);
    }
    assert(edid[93] == 0xfd && edid[94] == 8);
    assert(edid[95] == 60 && edid[96] == 120);
    assert(edid[97] == 30 && edid[98] + 255 == 510);
    assert(edid[99] * 10 == 2550);
    assert(edid[132] == 0x41 && edid[133] == 97);
    assert(!memcmp(edid + 139, "DreamGPU", 8));
    if (argc == 2) {
        FILE *out = fopen(argv[1], "wb");
        assert(out && fwrite(edid, 1, sizeof(edid), out) == sizeof(edid));
        assert(!fclose(out));
    }
    puts(
        "actual DreamGPU/QEMU EDID generation: checksums, default60, range120, NT dimensions PASS");
    return 0;
}
