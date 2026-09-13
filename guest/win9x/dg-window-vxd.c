/* SPDX-License-Identifier: GPL-2.0-or-later
 * Watcom DIOC/register ABI adapters. All binding/retention/coherence policy
 * lives with the shared ordered channel in channel32.cpp.
 */
static BOOL Dg9WindowControl(struct DIOCParams *params, DWORD *result) {
    DG9_CHANNEL_CONTROL input;
    if (!DgVxdRegisters())
        return FALSE;
    Dg9ChannelParams(params, &input);
    return DreamGpuWindowControl(&input, result);
}
WORD Dg9WindowBlt(PCRS_32 state) {
    DG9_CHANNEL_BLT input;
    input.Client_EDI = state->Client_EDI;
    input.Client_EBX = state->Client_EBX;
    input.Client_ECX = state->Client_ECX;
    input.Client_ESI = state->Client_ESI;
    return DreamGpuWindowBlt(&input);
}
