// SPDX-License-Identifier: GPL-2.0-or-later
#include "native-alias.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char **argv) {
    using namespace setup;
    using namespace setup::native_alias;
    std::vector<uint8_t> pe(128);
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[60] = 64;
    pe[64] = 'P';
    pe[65] = 'E';
    pe[68] = 0x4c;
    pe[69] = 1;
    pe[88] = 0x0b;
    pe[89] = 1;
    const auto original = pe;
    assert(derive(Os::nt5, "ddraw.dll", pe) && pe == original);
    assert(derive(Os::win98, "d3d8.dll", pe) && pe == original);
    assert(!derive(Os::win98, "ddraw.dll", pe) && pe == original);
    assert(!derive(Os::nt5, "foreign.dll", pe) && pe == original);
    pe[63] = 255;
    assert(!derive(Os::nt5, "ddraw.dll", pe));
    pe = original;
    pe[69] = 2;
    assert(!derive(Os::nt5, "ddraw.dll", pe));
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<uint8_t> source((std::istreambuf_iterator<char>(input)), {});
        assert(input.is_open() && !source.empty());
        char before[65];
        hash(source, before);
        assert(equal(before, Win98Source));
        assert(derive(Os::win98, "ddraw.dll", source));
        char after[65];
        hash(source, after);
        assert(equal(after, Win98Alias));
    }
    puts("PASS actual native alias: exact PE32/copy policy, bounded known Win98 thunk "
         "transformation");
}
