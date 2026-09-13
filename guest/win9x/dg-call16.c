/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included after pm16_calls.c: use its already-discovered VxD call gate.
 * Packed register arguments avoid sharing a pageable 16-bit command pointer.
 */
#pragma code_seg(_TEXT)
WORD DgNativeBlt(DWORD operation, DWORD source, DWORD destination, DWORD extent) {
    static DWORD service, src, dst, size;
    WORD result = 0;
    if (!VXD_VM)
        return 0;
    service = (operation << 16) | 0x4a00UL;
    src = source;
    dst = destination;
    size = extent;
    _asm {
        .386
        push eax
        push ebx
        push ecx
        push edx
        push esi
        mov ebx, [src]
        mov ecx, [dst]
        mov esi, [size]
        mov edx, [service]
        call dword ptr [VXD_VM]
        mov [result], ax
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
    }
    return result;
}

WORD DgWindowBlt16(DWORD binding, DWORD source, DWORD destination, DWORD extent) {
    static DWORD token, src, dst, size;
    WORD result = 0;
    if (!VXD_VM)
        return 0;
    token = binding;
    src = source;
    dst = destination;
    size = extent;
    _asm {
        .386
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov ebx, [src]
        mov ecx, [dst]
        mov esi, [size]
        mov edi, [token]
        mov edx, 04a01h
        call dword ptr [VXD_VM]
        mov [result], ax
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
    }
    return result;
}

WORD DgNativeCursor(DWORD operation, DWORD source, DWORD destination, DWORD extent) {
    static DWORD service, src, dst, size;
    WORD result = 0;
    if (!VXD_VM)
        return 0;
    service = (operation << 16) | 0x4a02UL;
    src = source;
    dst = destination;
    size = extent;
    _asm {
        .386
        push eax
        push ebx
        push ecx
        push edx
        push esi
        mov ebx, [src]
        mov ecx, [dst]
        mov esi, [size]
        mov edx, [service]
        call dword ptr [VXD_VM]
        mov [result], ax
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
    }
    return result;
}
