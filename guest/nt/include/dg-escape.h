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
/* Larger batches negotiated in OPEN's MaxBytes by a driver using the queued
 * host transport (gl.h). Older drivers never advertise more than MAX_BYTES. */
#define DG_ESCAPE_QUEUE_MAX_BYTES 131072
/* OPEN replies carry driver feature bits in DeviceError; older drivers leave
 * it zero. QUEUE: SUBMIT accepts Flags and Stream and may report DEFERRED. */
#define DG_ESCAPE_FEATURE_QUEUE 1
/* SUBMIT Flags. ASYNC (never with ResultCapacity) returns OK once the batch
 * is captured in driver memory and published, before the host executes it;
 * its outcome is not reported to that request. */
#define DG_ESCAPE_SUBMIT_ASYNC 1
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
/* The request was not executed: an earlier batch of the client's stream
 * FunctionWords failed after its request returned (DeviceError and
 * CompletedSequence identify that batch). Each failure is reported once; the
 * frontend marks that stream's context failed, then may repeat a request of
 * another stream. */
#define DG_ESCAPE_DEFERRED 8
typedef struct {
    ULONG Version, Operation, Client, Bytes, Function, Flags;
    ULONG ResultCapacity, Stream;
    /* Exactly Bytes of immutable copied records follow SUBMIT requests.
     * ResultCapacity is 1..MaxResultBytes (negotiated at OPEN) only for a sole native DG_GL_QUERY
     * record; otherwise it must be zero. No addresses or output pointers cross ABI.
     * Flags and Stream are zero unless OPEN reported DG_ESCAPE_FEATURE_QUEUE.
     * Stream is then the submitting frontend context (nonzero, never reused by
     * the client), or zero for requests outside any stream. */
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
