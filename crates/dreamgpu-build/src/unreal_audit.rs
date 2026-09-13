// SPDX-License-Identifier: GPL-2.0-or-later
//! The fixed UT tools share one scalar, no-CRT Win98/NT5 binary contract.
use anyhow::{bail, ensure, Context, Result};
use serde_json::{json, Value};

pub(super) fn applies(name: &str) -> bool {
    matches!(
        name,
        "dgut.exe" | "dgutd3.exe" | "dgutset.exe" | "dgutds.exe" | "dgutlog.exe"
    )
}

const WIN98_API: &str = "CloseHandle CopyFileA CreateDirectoryA CreateFileA CreateProcessA
CreateToolhelp32Snapshot DeleteFileA EnumChildWindows EnumWindows ExitProcess
FindClose FindFirstFileA FindNextFileA FlushFileBuffers GetClassNameA
GetExitCodeProcess GetDriveTypeA GetFileAttributesA GetFileAttributesExA GetFileSize
GetForegroundWindow GetLastError GetTickCount GetWindowTextA GetCurrentProcessId
GetCurrentThreadId GetModuleHandleA GetProcAddress Process32First Process32Next ResumeThread
GetWindowThreadProcessId IsWindowVisible Module32First Module32Next OpenEventA
PostMessageA ReadFile SendMessageTimeoutA SetErrorMode SetEvent SetFileAttributesA
SetFilePointer SetForegroundWindow SetLastError ShowWindowAsync Sleep
TerminateProcess Thread32First Thread32Next WaitForSingleObject WriteFile CreateWindowExA DefWindowProcA
DestroyWindow DispatchMessageA GetAsyncKeyState GetCursorPos GetMessageExtraInfo
GetSystemDirectoryA GetSystemMetrics MsgWaitForMultipleObjects PeekMessageA RegisterClassA SendInput
SetCursorPos ShowWindow TranslateMessage UnregisterClassA WindowFromPoint";

fn imports(details: &str) -> Result<Vec<String>> {
    let mut functions = Vec::new();
    let mut members = false;
    let mut library_count = 0;
    for line in details.lines() {
        let text = line.trim();
        if let Some(library) = text.strip_prefix("DLL Name:") {
            ensure!(
                matches!(
                    library.trim().to_ascii_lowercase().as_str(),
                    "kernel32.dll" | "user32.dll"
                ),
                "Unreal tool imported an unexpected DLL: {}",
                library.trim()
            );
            library_count += 1;
            members = false;
        } else if text.contains("Member-Name") {
            members = true;
        } else if text.is_empty() {
            members = false;
        } else if members {
            let columns: Vec<_> = text.split_whitespace().collect();
            ensure!(
                columns.len() == 4
                    && columns[1] == "<none>"
                    && columns[0].bytes().all(|c| c.is_ascii_hexdigit())
                    && columns[2].bytes().all(|c| c.is_ascii_hexdigit()),
                "Unreal tool has an unaudited ordinal/malformed import: {text}"
            );
            let function = columns[3];
            ensure!(
                WIN98_API
                    .split_whitespace()
                    .any(|allowed| allowed == function),
                "Unreal tool imported an unaudited Win98 API: {function}"
            );
            functions.push(function.to_owned());
        }
    }
    ensure!(
        library_count > 0 && !functions.is_empty(),
        "Unreal tool import table was empty or unrecognized"
    );
    functions.sort();
    functions.dedup();
    Ok(functions)
}

fn scalar_code(disassembly: &str) -> Result<usize> {
    let mut count = 0;
    for line in disassembly.lines() {
        let Some((address, instruction)) = line.split_once(':') else {
            continue;
        };
        if address.trim().is_empty() || !address.trim().bytes().all(|b| b.is_ascii_hexdigit()) {
            continue;
        }
        let mut words = instruction.split_whitespace().peekable();
        while words
            .peek()
            .is_some_and(|word| word.len() == 2 && word.bytes().all(|b| b.is_ascii_hexdigit()))
        {
            words.next();
        }
        let Some(mnemonic) = words.next() else {
            continue;
        };
        ensure!(
            mnemonic != "(bad)",
            "Unreal tool contains an undecodable instruction: {line}"
        );
        count += 1;
        // Integer-only and x87 instructions are permitted. These later-ISA
        // operations need no vector register, so the register gate alone would
        // miss them. Compiler flags remain -march=pentium3 -mno-sse -mno-sse2.
        ensure!(
            !matches!(
                mnemonic,
                "movnti"
                    | "lfence"
                    | "mfence"
                    | "sfence"
                    | "clflush"
                    | "ldmxcsr"
                    | "stmxcsr"
                    | "fisttp"
                    | "monitor"
                    | "mwait"
                    | "popcnt"
                    | "lzcnt"
                    | "tzcnt"
                    | "crc32"
                    | "rdrand"
                    | "rdseed"
                    | "xgetbv"
                    | "xsetbv"
            ) && !mnemonic.starts_with("xsave")
                && !mnemonic.starts_with("xrstor"),
            "Unreal tool contains a non-scalar/later-ISA instruction: {line}"
        );
        for token in
            words.flat_map(|word| word.split(|c: char| !c.is_ascii_alphanumeric() && c != '%'))
        {
            let Some(register) = token.strip_prefix('%') else {
                continue;
            };
            for prefix in ["xmm", "ymm", "zmm", "mm"] {
                if let Some(number) = register.strip_prefix(prefix) {
                    if !number.is_empty() && number.bytes().all(|b| b.is_ascii_digit()) {
                        bail!("Unreal tool contains a vector-register instruction: {line}");
                    }
                }
            }
        }
    }
    ensure!(
        count > 0,
        "Unreal tool disassembly was empty or unrecognized"
    );
    Ok(count)
}

pub(super) fn audit(data: &[u8], details: &str, disassembly: &str) -> Result<Value> {
    let header = super::read32(data, 0x3c)? as usize;
    let optional = header
        .checked_add(24)
        .context("PE header offset overflow")?;
    ensure!(
        data.get(header..header + 4) == Some(b"PE\0\0")
            && super::read16(data, header + 4)? == 0x14c
            && super::read16(data, optional)? == 0x10b,
        "Unreal tool must be i386 PE32"
    );
    ensure!(
        super::read16(data, optional + 40)? == 4
            && super::read16(data, optional + 42)? == 0
            && super::read16(data, optional + 48)? == 4
            && super::read16(data, optional + 50)? == 0,
        "Unreal tool must declare OS/subsystem version 4.0"
    );
    let imported_functions = imports(details)?;
    let instructions = scalar_code(disassembly)?;
    Ok(
        json!({"policy":"Win98/NT5 scalar Pentium III; no CRT; OS/subsystem 4.0",
              "imported_functions":imported_functions,"decoded_instructions":instructions}),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn accepts_reviewed_imports_and_rejects_unknown_or_ordinal_entries() {
        let valid = "DLL Name: KERNEL32.dll\n vma: Ordinal Hint Member-Name Bound-To\n 1000 <none> 0040 CloseHandle\n\nDLL Name: USER32.dll\n vma: Ordinal Hint Member-Name Bound-To\n 2000 <none> 0060 SendInput\n\n";
        assert_eq!(imports(valid).unwrap(), ["CloseHandle", "SendInput"]);
        for rejected in [
            valid.replace("CloseHandle", "CreateThread"),
            valid.replace("<none>", "0040"),
            valid.replace("USER32.dll", "msvcrt.dll"),
            "nothing".to_owned(),
        ] {
            assert!(imports(&rejected).is_err());
        }
    }
    #[test]
    fn accepts_legacy_system_path_and_thread_snapshot_imports() {
        // Normal-loader validation and owned-process teardown use these Win95+
        // Kernel32 APIs; no newer process-inspection import is admitted.
        let imports_text = "DLL Name: KERNEL32.dll\n vma: Ordinal Hint Member-Name Bound-To\n 1000 <none> 0040 GetSystemDirectoryA\n 1004 <none> 0041 Thread32First\n 1008 <none> 0042 Thread32Next\n\n";
        assert_eq!(
            imports(imports_text).unwrap(),
            ["GetSystemDirectoryA", "Thread32First", "Thread32Next"]
        );
        assert!(
            imports(&imports_text.replace("Thread32First", "QueryFullProcessImageNameA")).is_err()
        );
    }
    #[test]
    fn examines_decoded_operands_not_symbol_names_and_rejects_vector_code() {
        assert_eq!(
            scalar_code(
                "00400000 <xmm1_name>:\n 400000: 31 c0 xor %eax,%eax\n 400002: d9 e8 fld1\n"
            )
            .unwrap(),
            2
        );
        for rejected in [
            "400000: 0f 57 c0 xorps %xmm0,%xmm0",
            "400000: c5 fd ef c0 vpxor %ymm0,%ymm0,%ymm0",
            "400000: 0f ef c0 pxor %mm0,%mm0",
            "400000: f3 0f b8 c0 popcnt %eax,%eax",
            "400000: 0f ae f0 mfence",
            "400000: ff (bad)",
            "no instructions",
        ] {
            assert!(scalar_code(rejected).is_err(), "{rejected}");
        }
    }
    #[test]
    #[ignore = "requires existing real guest helper binaries and captured objdump output; no build or runtime"]
    fn built_unreal_helpers() {
        let root = std::path::PathBuf::from(
            std::env::var_os("DREAMGPU_UNREAL_AUDIT_DIRECTORY").expect("audit directory"),
        );
        let mut report = serde_json::Map::new();
        for name in [
            "DGUT.EXE",
            "DGUTD3.EXE",
            "DGUTSET.EXE",
            "DGUTDS.EXE",
            "DGUTLOG.EXE",
        ] {
            let binary = root.join(name);
            let data = std::fs::read(&binary).unwrap();
            let details = std::fs::read_to_string(root.join(format!("{name}.pe.txt"))).unwrap();
            let assembly = std::fs::read_to_string(root.join(format!("{name}.asm.txt"))).unwrap();
            let mut result = audit(&data, &details, &assembly).unwrap();
            result["binary_sha256"] = serde_json::json!(crate::digest(&binary).unwrap());
            report.insert(name.to_owned(), result);
        }
        std::fs::write(
            root.join("unreal-audit.json"),
            serde_json::to_vec_pretty(&report).unwrap(),
        )
        .unwrap();
    }

    #[test]
    fn rejects_later_or_truncated_pe_headers() {
        let mut bytes = vec![0u8; 256];
        bytes[0x3c..0x40].copy_from_slice(&64u32.to_le_bytes());
        bytes[64..68].copy_from_slice(b"PE\0\0");
        bytes[68..70].copy_from_slice(&0x14cu16.to_le_bytes());
        bytes[88..90].copy_from_slice(&0x10bu16.to_le_bytes());
        bytes[128..130].copy_from_slice(&4u16.to_le_bytes());
        bytes[136..138].copy_from_slice(&4u16.to_le_bytes());
        let imports = "DLL Name: KERNEL32.dll\n vma: Ordinal Hint Member-Name\n 1000 <none> 0040 CloseHandle\n";
        assert!(audit(&bytes, imports, "400000: c3 ret").is_ok());
        bytes[136] = 5;
        assert!(audit(&bytes, imports, "400000: c3 ret").is_err());
        assert!(audit(&bytes[..32], imports, "400000: c3 ret").is_err());
    }
}
