#!/usr/bin/env python3
"""Exercise production probe finalization with destructive log reads and cleanup."""
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
source = (HERE / 'runner.cpp').read_text()
implementation = source[source.index('static const char *FinishProbe('):
                        source.index('static const char *Probe(')]
shim = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
using DWORD = uint32_t;
using BYTE = unsigned char;
using BOOL = bool;
constexpr BOOL TRUE = true;
constexpr DWORD OUTPUT_MAX = 65536;
struct PROCESS_INFORMATION { int hProcess; DWORD dwProcessId; };
struct LOG_STATE {};
static BYTE Output[OUTPUT_MAX + 1];
static DWORD OutputBytes;
static bool stopped, closed, cleanup_ok;
static unsigned captures;
static std::string log_text, json_text;
static const char *json_error;
static void output(const std::string &s) {
    assert(s.size() <= OUTPUT_MAX);
    std::memcpy(Output, s.data(), s.size());
    OutputBytes = DWORD(s.size());
}
static void CaptureFailureProcess(PROCESS_INFORMATION *p) {
    assert(!closed && p->hProcess == 42);
    ++captures;
    output("\r\nOWNED_PROCESS exit=0xc0000005\r\n");
}
static void CaptureOwnedModal(DWORD pid) {
    assert(!closed && pid == 7);
    const char *text = "OWNED_MODAL static: fault\r\n";
    std::memcpy(Output + OutputBytes, text, std::strlen(text));
    OutputBytes += DWORD(std::strlen(text));
}
static BOOL StopOwned(PROCESS_INFORMATION *p) {
    assert(!closed && p->hProcess == 42);
    stopped = true;
    output("cleanup clobbered shared output");
    return cleanup_ok;
}
static void CloseHandle(int h) { assert(stopped && h == 42); closed = true; }
static void PollLog(const char *, LOG_STATE *, BOOL exited) {
    assert(stopped && closed && exited);
    output(log_text);
}
static const char *ReadCapabilityResult(const char *) {
    assert(stopped && closed);
    output(json_text);
    return json_error;
}
'''
checks = r'''
int main() {
    PROCESS_INFORMATION process{42, 7}; LOG_STATE state;
    for (unsigned mode = 0; mode < 9; ++mode) {
        stopped = closed = false; captures = 0; cleanup_ok = mode != 5;
        log_text = "STAGE actual installed API\r\n";
        json_text = "{\"complete\":false}"; json_error = nullptr;
        if (mode == 1 || mode == 4) log_text.clear();
        if (mode == 2) log_text = std::string(OUTPUT_MAX - 4, 'x') + "TAIL";
        if (mode == 8) { json_text.clear(); json_error = "missing-capability-output"; }
        bool capability = mode >= 6;
        const char *error = mode == 3 || mode == 4 || mode == 5 || mode == 7 || mode == 8
                                ? nullptr : "probe-failed";
        OutputBytes = 0; Output[OUTPUT_MAX] = 0xa5;
        const char *result = FinishProbe(&process, "fixed.log", &state, capability, error);
        assert(stopped && closed && Output[OUTPUT_MAX] == 0xa5);
        const std::string text(reinterpret_cast<char *>(Output), OutputBytes);
        if (capability) {
            assert(text == json_text && captures == 0);
            if (mode == 6) assert(std::strcmp(result, "probe-failed") == 0);
            if (mode == 7) assert(!result);
            if (mode == 8) assert(std::strcmp(result, "missing-capability-output") == 0);
        } else if (mode == 3 || mode == 4) {
            assert(text == log_text && captures == 0);
            assert(mode == 3 ? !result : std::strcmp(result, "missing-probe-log") == 0);
        } else {
            assert(captures == 1 && text.find("OWNED_PROCESS exit=0xc0000005") != std::string::npos);
            assert(text.ends_with("OWNED_MODAL static: fault\r\n"));
            assert(std::strcmp(result, mode == 5 ? "cleanup-failed" : "probe-failed") == 0);
            if (mode == 0 || mode == 5) assert(text.starts_with(log_text));
            if (mode == 2) assert(OutputBytes == OUTPUT_MAX && text.find("TAIL\r\nOWNED_PROCESS") != std::string::npos);
        }
    }
}
'''
with tempfile.TemporaryDirectory(prefix='dg-probe-failure-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(shim + implementation + checks)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS actual probe finalizer: exit/modal evidence survives logs, bounded tail, cleanup errors, exact JSON')
