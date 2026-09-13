#!/usr/bin/env python3
"""Execute exact patched donor functions with bounded Win32 seams under sanitizers."""
from pathlib import Path
import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('checked',ROOT/'tests/guest/support/patches.py')
checked=importlib.util.module_from_spec(spec);spec.loader.exec_module(checked)
def function(source,name):
    start=re.search(r'^[A-Za-z][^\n]*\b'+name+r'\([^\n]*\)\n\{',source,re.M).start()
    brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
with tempfile.TemporaryDirectory(prefix='dreamgpu-switcher-') as tmp:
    root=Path(tmp);donor=root/'wine'
    shutil.copytree(ROOT/'vendor/wine9x',donor,ignore=shutil.ignore_patterns('.git'))
    checked.apply(donor,ROOT/'support/guest/d3d/patches/base/manifest.json')
    original=(donor/'switcher/switcher.c').read_text()
    names=('getSysPath','getExeName','readSetting','getSettings','tryLoad','fncheck','getVMDISP9xFlags','switcher_reinit')
    code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <strings.h>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstdio>
using BYTE=uint8_t;using DWORD=uint32_t;using LONG=int32_t;using BOOL=int;
using HANDLE=void*;using HMODULE=void*;using HWND=void*;using HDC=void*;using HKEY=void*;using LPVOID=void*;
#define MAX_PATH 260
#define TRUE 1
#define FALSE 0
#define REG_SZ 1
#define REG_DWORD 4
#define ERROR_SUCCESS 0
#define KEY_READ 1
#define HKEY_LOCAL_MACHINE ((void*)1)
#define INVALID_HANDLE_VALUE ((void*)~uintptr_t(0))
#define TH32CS_SNAPMODULE 8
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define stricmp strcasecmp
static const char reg_base[]="Software\\DDSwitcher";
static const char*blacklist_exe[]={"iexplore.exe","explorer.exe",nullptr};
static const char*blacklist_dll[]={"opengl32.dll","dgpuicd.dll","dgpugl.dll","ddsys.dll",nullptr};
static HMODULE thisDLL=(void*)9;
static std::string systemPath="C:\\WINDOWS\\SYSTEM", exePath="C:\\GAME\\GAME.EXE", loadedPath;
static DWORD systemReturn,exeReturn,settingType=REG_SZ;
static std::string setting="wine",requested;
static bool missingSetting,exportFound=true,truncateLoaded;
static unsigned loads,frees,closes,regCloses,releases;
static DWORD GetSystemDirectoryA(char*out,DWORD cap){if(systemReturn)return systemReturn;assert(systemPath.size()<cap);strcpy(out,systemPath.c_str());return systemPath.size();}
static DWORD GetModuleFileNameA(HMODULE module,char*out,DWORD cap){
 if(!module&&exeReturn)return exeReturn;
 if(module&&truncateLoaded)return cap;
 auto&s=module?loadedPath:exePath;assert(s.size()<cap);strcpy(out,s.c_str());return s.size();}
static LONG RegOpenKeyEx(HKEY,const char*,DWORD,DWORD,HKEY*out){*out=(void*)1;return 0;}
static LONG RegQueryValueExA(HKEY,const char*,void*,DWORD*type,BYTE*out,DWORD*bytes){
 if(missingSetting)return 2;assert(*bytes==MAX_PATH);*type=settingType;
 if(setting.size()>*bytes){*bytes=setting.size();return 234;}
 memcpy(out,setting.data(),setting.size());*bytes=setting.size();return 0;}
static LONG RegCloseKey(HKEY){++regCloses;return 0;}
static HMODULE LoadLibraryA(const char*path){++loads;requested=path;if(loadedPath.empty())loadedPath=path;return (void*)2;}
static void*GetProcAddress(HMODULE,const char*){return exportFound?(void*)1:nullptr;}
static BOOL FreeLibrary(HMODULE){++frees;return TRUE;}
struct MODULEENTRY32{DWORD dwSize;BYTE*modBaseAddr;DWORD modBaseSize;char szModule[MAX_PATH];};
static MODULEENTRY32 moduleEntry{};static bool snapshotFail,firstFail;
static HANDLE CreateToolhelp32Snapshot(DWORD,DWORD){return snapshotFail?INVALID_HANDLE_VALUE:(void*)3;}
static BOOL Module32First(HANDLE,MODULEENTRY32*out){if(firstFail)return FALSE;*out=moduleEntry;return TRUE;}
static BOOL Module32Next(HANDLE,MODULEENTRY32*){return FALSE;}
static BOOL CloseHandle(HANDLE){++closes;return TRUE;}
struct FBHDA_t{DWORD cb,version,flags;};
#define OP_FBHDA_SETUP 1
#define API_3DACCEL_VER 2
static FBHDA_t hda={sizeof(FBHDA_t),2,17};static bool dcFail,escapeFail;
static HWND GetDesktopWindow(){return(void*)1;}
static HDC GetDC(HWND){return dcFail?nullptr:(void*)2;}
static int ExtEscape(HDC,int,int,void*,size_t,LPVOID out){*(FBHDA_t**)out=&hda;return !escapeFail;}
static int ReleaseDC(HWND,HDC){++releases;return 1;}
static volatile LONG init_state=0,init_owner=0;
static HANDLE init_event=(void*)1;
static std::atomic<unsigned> nextThread{1};
static DWORD GetCurrentThreadId(){static thread_local DWORD id=nextThread++;return id;}
static LONG InterlockedExchange(volatile LONG*p,LONG value){return __atomic_exchange_n(p,value,__ATOMIC_SEQ_CST);}
static LONG InterlockedCompareExchange(volatile LONG*p,LONG value,LONG expected){__atomic_compare_exchange_n(p,&expected,value,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return expected;}
static std::mutex mutex;
static std::condition_variable condition;
static bool signaled,entered,waited,releaseInit,holdInit,failInit,forceTimeout;
static std::atomic<unsigned> initializations{0};static std::atomic<bool> published{false};
static BOOL switcher_reinit(void);
static BOOL switcher_init(){
 ++initializations;assert(!switcher_reinit()); // same-thread recursion uses immutable failure stubs
 {std::unique_lock lock(mutex);entered=true;condition.notify_all();if(holdInit)condition.wait(lock,[]{return releaseInit;});}
 if(failInit)return FALSE;published=true;return TRUE;
}
static BOOL SetEvent(HANDLE){std::lock_guard lock(mutex);signaled=true;condition.notify_all();return TRUE;}
static DWORD WaitForSingleObject(HANDLE,DWORD timeout){assert(timeout==5000);std::unique_lock lock(mutex);waited=true;condition.notify_all();if(forceTimeout)return WAIT_TIMEOUT;condition.wait(lock,[]{return signaled;});return WAIT_OBJECT_0;}
'''
    code+='\n'.join(function(original,name) for name in names)
    code+=r'''
int main(){
 struct{char text[MAX_PATH];uint32_t canary;} b{{},0xdeadbeef};
 getSysPath(b.text,(char*)"ddsys.dll");assert(!strcmp(b.text,"C:\\WINDOWS\\SYSTEM\\ddsys.dll")&&b.canary==0xdeadbeef);
 systemReturn=MAX_PATH+7;getSysPath(b.text,(char*)"ddsys.dll");assert(!b.text[0]&&b.canary==0xdeadbeef);systemReturn=0;
 getSysPath(b.text,(char*)"..\\evil.dll");assert(!b.text[0]);getSysPath(b.text,(char*)"C:evil.dll");assert(!b.text[0]);
 systemPath=std::string(252,'A');getSysPath(b.text,(char*)"ddsys.dll");assert(!b.text[0]);systemPath="C:\\WINDOWS\\SYSTEM";
 assert(getExeName(b.text)&&!strcmp(b.text,"GAME.EXE"));exeReturn=MAX_PATH;assert(!getExeName(b.text)&&!b.text[0]);exeReturn=0;
 setting=std::string("wine\0",5);assert(readSetting((void*)1,"global",b.text)&&!strcmp(b.text,"wine"));
 setting="wine";assert(!readSetting((void*)1,"global",b.text)&&!b.text[0]);
 setting=std::string("wine\0evil\0",10);assert(!readSetting((void*)1,"global",b.text));
 setting=std::string("C:\\EVIL.DLL\0",12);assert(!readSetting((void*)1,"global",b.text));
 setting=std::string("wine\0",5);settingType=REG_DWORD;assert(!readSetting((void*)1,"global",b.text));settingType=REG_SZ;
 assert(getSettings(b.text,"EXPLORER.EXE")&&!strcmp(b.text,"system")&&!regCloses);
 assert(getSettings(b.text,"game.exe")&&!strcmp(b.text,"wine")&&regCloses==1);
 assert(tryLoad("ddsys.dll","DirectDrawCreate")&&requested=="C:\\WINDOWS\\SYSTEM\\ddsys.dll");
 auto count=loads;assert(!tryLoad("C:\\EVIL\\ddsys.dll","DirectDrawCreate")&&loads==count);
 loadedPath="C:\\GAME\\ddsys.dll";assert(!tryLoad("ddsys.dll","DirectDrawCreate")&&frees==1);
 loadedPath.clear();truncateLoaded=true;assert(!tryLoad("ddsys.dll","DirectDrawCreate")&&frees==2);truncateLoaded=false;
 snapshotFail=true;assert(!fncheck((BYTE*)0x1000,0)&&!closes);snapshotFail=false;firstFail=true;assert(!fncheck((BYTE*)0x1000,0)&&closes==1);firstFail=false;
 moduleEntry.modBaseAddr=(BYTE*)0x1000;moduleEntry.modBaseSize=0x100;strcpy(moduleEntry.szModule,"game.exe");
 assert(fncheck((BYTE*)0x1000,0)&&!fncheck((BYTE*)0x1100,0));strcpy(moduleEntry.szModule,"DGPUGL.DLL");assert(!fncheck((BYTE*)0x1001,0));
 DWORD flags=0;assert(getVMDISP9xFlags(&flags)&&flags==17&&releases==1);escapeFail=true;assert(!getVMDISP9xFlags(&flags)&&releases==2);
 assert(!getVMDISP9xFlags(nullptr)&&releases==3);dcFail=true;assert(!getVMDISP9xFlags(&flags)&&releases==3);
 holdInit=true;bool a=false,c=false;
 std::thread winner([&]{a=switcher_reinit();});
 {std::unique_lock lock(mutex);condition.wait(lock,[]{return entered;});}
 std::thread observer([&]{c=switcher_reinit();assert(published);});
 {std::unique_lock lock(mutex);condition.wait(lock,[]{return waited;});assert(!published&&initializations==1);releaseInit=true;condition.notify_all();}
 winner.join();observer.join();assert(a&&c&&initializations==1&&switcher_reinit());
 InterlockedExchange(&init_state,1);InterlockedExchange(&init_owner,999);forceTimeout=true;assert(!switcher_reinit());
 InterlockedExchange(&init_state,0);failInit=true;holdInit=false;assert(!switcher_reinit()&&init_state==3);assert(!switcher_reinit()&&initializations==2);
 puts("PASS actual switcher: bounded registry/paths, system-only identity, native unknown routing, DC lifetime, wait/reentry/publication");
}
'''
    (root/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-O1','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-pthread','-fsanitize=address,undefined',str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True,timeout=15)
    # The actual i386 thunk must check readiness before touching either mutable
    # table and keep a distinct immutable fallback target for reentrant loads.
    asm=(donor/'switcher/switcher.inc').read_text()
    assert asm.index('jz %1 %+ _not_ready') < asm.index('mov eax,_fnlist')
    for name in ('ddraw','d3d8','d3d9'):
        assert 'const fnlistStruct fnlist_stubs' in (donor/f'switcher/{name}_stubs.c').read_text()
        assert 'extern _fnlist_stubs' in (donor/f'switcher/{name}_sw.asm').read_text()
