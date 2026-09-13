/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DREAMGPU_LAUNCH_PHASE_H
#define DREAMGPU_LAUNCH_PHASE_H
/* The game is created suspended. Host acknowledgement follows sampler readiness;
 * neither this process boundary nor console observation is an engine frame marker. */
static const char *LaunchObserved(const char *id, PROCESS_INFORMATION *process, BOOL *resumed) {
    *resumed = FALSE;
    if (!Reply("STARTED", id, NULL, FALSE) || !Reply("PROCESS_READY", id, NULL, FALSE))
        return "serial-write";
    if (!FixedAcknowledged("CONTINUE", id))
        return "launch-not-armed";
    if (ResumeThread(process->hThread) == (DWORD)-1)
        return "resume-failed";
    *resumed = TRUE;
    if (!Reply("PROCESS_RESUMED", id, NULL, FALSE))
        return "serial-write";
    return NULL;
}
static const char *ResultObserved(const char *id) {
    if (!Reply("TIMEDEMO_RESULT", id, NULL, FALSE))
        return "serial-write";
    return FixedAcknowledged("OBSERVED", id) ? NULL : "result-not-observed";
}
#endif
