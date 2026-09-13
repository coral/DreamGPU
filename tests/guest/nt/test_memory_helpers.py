#!/usr/bin/env python3
"""Actual freestanding copy/fill: unaligned lengths, guard pages, exact bounds."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CODE = r'''
#include <cassert>
#include <cstddef>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
extern "C" void *dg_memcpy(void *, const void *, size_t);
extern "C" void *dg_memset(void *, int, size_t);
int main() {
    unsigned char source[1024], output[1024], expected[1024];
    for (size_t i=0;i<sizeof(source);++i) source[i]=(unsigned char)(i*37+11);
    for (size_t length=0;length<=257;++length)
        for (size_t in=0;in<16;++in)
            for (size_t out=0;out<16;++out) {
                std::memset(output,0xa7,sizeof(output));
                std::memset(expected,0xa7,sizeof(expected));
                std::memcpy(expected+out,source+in,length);
                assert(dg_memcpy(output+out,source+in,length)==output+out);
                assert(!std::memcmp(output,expected,sizeof(output)));
            }
    for (int value: {0,1,0x80,0xff,0x1234,-1})
        for (size_t length=0;length<=257;++length)
            for (size_t out=0;out<16;++out) {
                std::memset(output,0xa7,sizeof(output));
                std::memset(expected,0xa7,sizeof(expected));
                std::memset(expected+out,value,length);
                assert(dg_memset(output+out,value,length)==output+out);
                assert(!std::memcmp(output,expected,sizeof(output)));
            }
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    auto *a=(unsigned char*)mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    auto *b=(unsigned char*)mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(a!=MAP_FAILED && b!=MAP_FAILED);
    assert(!mprotect(a+page,page,PROT_READ|PROT_WRITE));
    assert(!mprotect(b+page,page,PROT_READ|PROT_WRITE));
    for (size_t i=0;i<page;++i) a[page+i]=(unsigned char)(i*17+5);
    for (size_t n=0;n<=page;++n) {
        for (bool end: {false,true}) {
            auto *src=a+(end?2*page-n:page);
            auto *dst=b+(end?2*page-n:page);
            assert(dg_memcpy(dst,src,n)==dst);
            assert(!std::memcmp(dst,src,n));
            assert(dg_memset(dst,0x187,n)==dst);
            for(size_t i=0;i<n;++i) assert(dst[i]==0x87);
        }
    }
    assert(dg_memcpy(nullptr,nullptr,0)==nullptr);
    assert(dg_memset(nullptr,0,0)==nullptr);
    assert(!munmap(a,page*3));assert(!munmap(b,page*3));
}
'''

def main():
    compiler = os.environ.get('CXX', 'c++')
    with tempfile.TemporaryDirectory(prefix='dg-memory-') as directory:
        base = Path(directory)
        test = base / 'test.cpp'
        test.write_text('#include <initializer_list>\n' + CODE)
        flags = ['-std=c++23', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-fno-builtin', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run([compiler, *flags, '-Dmemcpy=dg_memcpy', '-Dmemset=dg_memset',
                        '-c', str(ROOT / 'guest/nt/memory.cpp'), '-o', str(base / 'memory.o')], check=True)
        subprocess.run([compiler, *flags, str(test), str(base / 'memory.o'),
                        '-o', str(base / 'test')], check=True)
        subprocess.run([str(base / 'test')], check=True)
    print('PASS: unaligned byte/word/chunk lengths, fill truncation, page boundaries and zero length')

if __name__ == '__main__':
    main()
