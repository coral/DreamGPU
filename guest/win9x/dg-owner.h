/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded ownership/rundown state, independent of VMM callback conventions.
 * DWORD must be 32 bits. The adapter supplies validated OS-derived owner and
 * handle keys; user request fields must never populate these keys.
 *
 * Callers serialize table operations. No function allocates, sleeps, invokes
 * the host GPU or assumes a callback can wait. TakeRetired hands cleanup to a
 * legal worker context; FinishRetired runs only after confirmed host cleanup.
 * A live native adapter must not reinitialize this table to recover a timeout.
 */
#ifndef DG9_OWNER_H
#define DG9_OWNER_H
#define DG9_OWNER_SLOTS 32
#define DG9_OWNER_FREE 0
#define DG9_OWNER_ACTIVE 1
#define DG9_OWNER_RETIRING 2
#define DG9_OWNER_DRAINING 3
typedef char Dg9OwnerDwordSize[sizeof(DWORD) == 4 ? 1 : -1];

typedef struct {
    DWORD Process, Context;
} DG9_OWNER_KEY;
typedef struct {
    DWORD Tag, Handle;
} DG9_HANDLE_KEY;
typedef struct {
    DG9_OWNER_KEY Owner;
    DG9_HANDLE_KEY Handle;
    DWORD Token, References, State;
} DG9_OWNER_SLOT;
typedef struct {
    DWORD NextToken;
    DG9_OWNER_SLOT Slots[DG9_OWNER_SLOTS];
} DG9_OWNER_TABLE;

#if defined(__WATCOMC__)
#define DREAMGPU_CDECL __cdecl
#elif defined(__i386__)
#define DREAMGPU_CDECL __attribute__((cdecl))
#else
#define DREAMGPU_CDECL
#endif
#ifdef __cplusplus
extern "C" {
#endif
void DREAMGPU_CDECL Dg9OwnerInitialize(DG9_OWNER_TABLE *);
DG9_OWNER_SLOT *DREAMGPU_CDECL Dg9OwnerFind(DG9_OWNER_TABLE *, DWORD);
DWORD DREAMGPU_CDECL DreamGpuOwnerClaim(DG9_OWNER_TABLE *, const DG9_OWNER_KEY *,
                                        const DG9_HANDLE_KEY *);
int DREAMGPU_CDECL DreamGpuOwnerAcquire(DG9_OWNER_TABLE *, DWORD, const DG9_OWNER_KEY *,
                                        const DG9_HANDLE_KEY *);
int DREAMGPU_CDECL Dg9OwnerRelease(DG9_OWNER_TABLE *, DWORD);
int DREAMGPU_CDECL Dg9OwnerRetireToken(DG9_OWNER_TABLE *, DWORD);
void DREAMGPU_CDECL DreamGpuOwnerRetireHandle(DG9_OWNER_TABLE *, const DG9_HANDLE_KEY *);
void DREAMGPU_CDECL Dg9OwnerRetireProcess(DG9_OWNER_TABLE *, DWORD);
DWORD DREAMGPU_CDECL Dg9OwnerTakeRetired(DG9_OWNER_TABLE *);
int DREAMGPU_CDECL Dg9OwnerFinishRetired(DG9_OWNER_TABLE *, DWORD);
#ifdef __cplusplus
}
#endif
/* Keep by-value aggregates inside one compiler ABI; bridge only POD pointers. */
static inline DWORD Dg9OwnerClaim(DG9_OWNER_TABLE *table, DG9_OWNER_KEY owner,
                                  DG9_HANDLE_KEY handle) {
    return DreamGpuOwnerClaim(table, &owner, &handle);
}
static inline int Dg9OwnerAcquire(DG9_OWNER_TABLE *table, DWORD token, DG9_OWNER_KEY owner,
                                  DG9_HANDLE_KEY handle) {
    return DreamGpuOwnerAcquire(table, token, &owner, &handle);
}
static inline void Dg9OwnerRetireHandle(DG9_OWNER_TABLE *table, DG9_HANDLE_KEY handle) {
    DreamGpuOwnerRetireHandle(table, &handle);
}
#endif
