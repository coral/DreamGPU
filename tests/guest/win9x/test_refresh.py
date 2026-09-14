"""Compile the actual prepared Win16 refresh admission/selection blocks."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
start = source.index('            if((DispInfo.diInfoFlags &')
end = source.index('            /* DPI might', start)
admission = source[start:end]
start = source.index('#ifdef QEMU\n    {')
end = source.index('    mode.xRes', start)
selection = source[start:end]
program = r'''
#include <assert.h>
#include <stdint.h>
#define QEMU 1
#define MONITOR_INFO_NOT_VALID 0x80
#define MONITOR_INFO_DISABLED_BY_USER 0x100
#define REFRESH_RATE_MAX_ONLY 0x200
typedef uint16_t WORD;
static WORD select_rate(WORD flags, WORD low, WORD high) {
    struct { WORD diInfoFlags, diRefreshRateMin, diRefreshRateMax; } DispInfo = {flags,low,high};
    WORD wFreqMin = 0, wFreqMax = 0;
''' + admission + selection + r'''
    return wFreqMax;
}
int main(void) {
    const WORD rates[] = {60,75,85,100,120};
    for (unsigned i=0;i<5;i++) {
        assert(select_rate(0x200,0,rates[i]) == rates[i]);
        assert(select_rate(0x280,0,rates[i]) == rates[i]);
        assert(select_rate(0x300,0,rates[i]) == rates[i]);
    }
    for (unsigned n=1;n<0x8000;n++) {
        int supported = n==60||n==75||n==85||n==100||n==120;
        assert(select_rate(0x280,0,n) == (supported ? n : 0xffff));
    }
    assert(select_rate(0x280,0,0)==60);
    assert(select_rate(0x280,0,0xffff)==60);
    assert(select_rate(0x80,60,120)==60);
    assert(select_rate(0x100,60,120)==60);
    assert(select_rate(0,60,120)==120);
    assert(select_rate(0,76,99)==85);
    assert(select_rate(0,121,130)==0xffff);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dg-refresh-') as temp:
    path=Path(temp);(path/'test.c').write_text(program)
    subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Actual Win16 refresh admission/selection: supported overrides, all unsupported positive rates, defaults and monitor-range guards PASS')
