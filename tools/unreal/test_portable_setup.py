#!/usr/bin/env python3
"""Execute the actual fixed-media and installer-name selectors under sanitizers."""
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def function(source, name):
    start = source.index('static BOOL ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

common = (HERE / 'common.h').read_text()
installer = (HERE.parent / 'benchmark/install.cpp').read_text()
code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef unsigned long DWORD;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define DRIVE_CDROM 5
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 16
#define ERROR_DUP_NAME 52
#define ERROR_FILE_NOT_FOUND 2
static unsigned optical, markers, directories, calls;
static DWORD error;
static void SetLastError(DWORD value) { error=value; }
static DWORD GetDriveTypeA(const char *root) {
    assert(strlen(root)==3 && root[1]==':' && root[2]=='\\'); calls++;
    return optical & (1u<<(root[0]-'D')) ? DRIVE_CDROM : 3;
}
static DWORD GetFileAttributesA(const char *path) {
    unsigned bit=1u<<(path[0]-'D');
    assert(!strcmp(path+1,":\\UT99\\DGUT.INI"));
    if(!(markers&bit))return INVALID_FILE_ATTRIBUTES;
    return directories&bit?FILE_ATTRIBUTE_DIRECTORY:0;
}
static BOOL Join(char *out,const char *dir,const char *name) {
    return snprintf(out,MAX_PATH,"%s\\%s",dir,name)<MAX_PATH;
}
'''
code += function(common, 'PackageMedia') + '\n'
code += function(installer, 'Equal') + '\n' + function(installer, 'RunnerName')
code += r'''
int main(void) {
    char root[4];
    optical=markers=1; assert(PackageMedia(root,"UT99\\DGUT.INI")); assert(!strcmp(root,"D:")); assert(calls==23);
    optical=markers=2; assert(PackageMedia(root,"UT99\\DGUT.INI")); assert(!strcmp(root,"E:"));
    optical=markers=3; assert(!PackageMedia(root,"UT99\\DGUT.INI") && error==ERROR_DUP_NAME);
    optical=1; markers=2; assert(!PackageMedia(root,"UT99\\DGUT.INI") && error==ERROR_FILE_NOT_FOUND);
    optical=markers=1; directories=1; assert(!PackageMedia(root,"UT99\\DGUT.INI"));
    assert(RunnerName("DGPUBEN.EXE")); assert(RunnerName("C:\\DGPUBEN.EXE"));
    assert(RunnerName("e:/dgpuben.exe")); assert(!RunnerName("C:\\NOTDGPUBEN.EXE"));
    assert(!RunnerName("C:\\DGPUBEN.EXE.bak")); assert(!RunnerName(""));
    /* Selection does not replace the subsequent exact module ownership gate. */
    puts("PASS portable fixed optical media and runner candidate selection");
}
'''
assert 'Equal(module.szExePath,"C:\\\\DGPUBEN.EXE")' in ''.join(installer.split())
with tempfile.TemporaryDirectory(prefix='dg-portable-') as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(code)
    subprocess.run([shutil.which('clang') or 'cc', '-O1', '-g', '-fsanitize=address,undefined',
                    '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
