/* SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic transport, not an OpenGL implementation/version claim.
 * No user pointers, physical addresses, or caller-selected process identity.
 */
#ifndef DG_ESCAPE_H
#define DG_ESCAPE_H
#define DG_ESCAPE 0x4a524701
#define DG_ESCAPE_VERSION 2
#define DG_ESCAPE_OPEN 1
#define DG_ESCAPE_CLOSE 2
#define DG_ESCAPE_QUERY 3
#define DG_ESCAPE_SUBMIT 4
/* Client-owned capability query. Function must be zero; FunctionWords in the
 * unchanged reply header contains DG_REG_CAPS, with no result payload. */
#define DG_ESCAPE_CAPABILITIES 5
#define DG_ESCAPE_MAX_BYTES 65536
#define DG_ESCAPE_MAX_CLIENTS 32
#define DG_ESCAPE_LEGACY_RESULT_BYTES 512
#define DG_ESCAPE_MAX_RESULT_BYTES 65536
#define DG_ESCAPE_OK 0
#define DG_ESCAPE_INVALID 1
#define DG_ESCAPE_UNSUPPORTED 2
#define DG_ESCAPE_OWNER 3
#define DG_ESCAPE_HOST 4
#define DG_ESCAPE_TIMEOUT 5
#define DG_ESCAPE_RESOURCES 6
#define DG_ESCAPE_STOPPED 7
typedef struct {
    ULONG Version, Operation, Client, Bytes, Function, Reserved;
    ULONG ResultCapacity, Reserved2;
    /* Exactly Bytes of immutable copied records follow SUBMIT requests.
     * ResultCapacity is 1..MaxResultBytes (negotiated at OPEN) only for a sole native DG_GL_QUERY
     * record; otherwise it must be zero. No addresses or output pointers cross ABI. */
} DG_ESCAPE_REQUEST;
typedef struct {
    ULONG Version, Status, Client, Generation, MaxBytes, MaxRecords;
    ULONG FunctionWords, DeviceError, CompletedSequence;
    ULONG MaxResultBytes, ResultType, ResultBytes;
    /* Exactly ResultBytes immutable little-endian result bytes follow.
     * Nonquery and failed requests return only this header, with both result
     * fields zero. FunctionWords includes the native DATA/QUERY kind bits for
     * QUERY, or the device capability bitmask for CAPABILITIES. */
} DG_ESCAPE_REPLY;
typedef char DgEscapeRequestSize[sizeof(DG_ESCAPE_REQUEST) == 32 ? 1 : -1];
typedef char DgEscapeReplySize[sizeof(DG_ESCAPE_REPLY) == 48 ? 1 : -1];
#endif
