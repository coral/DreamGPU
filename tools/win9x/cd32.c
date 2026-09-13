/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optical diagnostics using the actual legacy Windows MCI driver. Default
 * mode reads metadata only; /play D: explicitly exercises bounded CD playback.
 * No game files, registry values, media contents, or keys are read.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#ifdef __WATCOMC__
#pragma library("winmm")
#endif

static HANDLE output;
static char text[512];

static void line(const char *value) {
    DWORD written;
    WriteFile(output, value, lstrlenA(value), &written, NULL);
    WriteFile(output, "\r\n", 2, &written, NULL);
}

static void error_line(const char *operation, MCIERROR error) {
    char description[256];
    description[0] = 0;
    mciGetErrorStringA(error, description, sizeof(description));
    wsprintfA(text, "%s error=%lu description=%s", operation, error, description);
    line(text);
}

static MCIERROR status(MCIDEVICEID device, DWORD item, DWORD track, DWORD *value) {
    MCI_STATUS_PARMS request;
    MCIERROR error;
    ZeroMemory(&request, sizeof(request));
    request.dwItem = item;
    request.dwTrack = track;
    error =
        mciSendCommandA(device, MCI_STATUS, MCI_WAIT | MCI_STATUS_ITEM | (track ? MCI_TRACK : 0),
                        (DWORD_PTR)&request);
    *value = error ? 0 : request.dwReturn;
    return error;
}

static int log_status(MCIDEVICEID device, const char *name, DWORD item) {
    DWORD value;
    MCIERROR error = status(device, item, 0, &value);
    wsprintfA(text, "%s error=%lu value=%lu", name, error, value);
    line(text);
    return error != 0;
}

static int inspect(char letter) {
    char root[4] = "A:\\", element[3] = "A:", label[64], filesystem[32];
    DWORD serial, component, flags, tracks, track, type, position, length;
    MCI_OPEN_PARMSA open;
    MCI_SET_PARMS set;
    MCIERROR error, type_error, position_error, length_error;
    int failed = 0;

    root[0] = element[0] = letter;
    wsprintfA(text, "Drive %s", root);
    line(text);
    if (GetVolumeInformationA(root, label, sizeof(label), &serial, &component, &flags, filesystem,
                              sizeof(filesystem))) {
        wsprintfA(text, "label=%s filesystem=%s serial=%08lX", label, filesystem, serial);
        line(text);
    } else {
        wsprintfA(text, "GetVolumeInformation error=%lu", GetLastError());
        line(text);
        /* An audio-only disc has no filesystem and can still pass MCI. */
    }
    ZeroMemory(&open, sizeof(open));
    open.lpstrDeviceType = "cdaudio";
    open.lpstrElementName = element;
    error = mciSendCommandA(0, MCI_OPEN,
                            MCI_WAIT | MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_OPEN_SHAREABLE,
                            (DWORD_PTR)&open);
    error_line("MCI_OPEN", error);
    if (error)
        return 1;

    ZeroMemory(&set, sizeof(set));
    set.dwTimeFormat = MCI_FORMAT_MSF;
    error =
        mciSendCommandA(open.wDeviceID, MCI_SET, MCI_WAIT | MCI_SET_TIME_FORMAT, (DWORD_PTR)&set);
    error_line("MCI_SET_MSF", error);
    if (error) {
        failed = 1;
        goto close;
    }
    failed |= log_status(open.wDeviceID, "MEDIA_PRESENT", MCI_STATUS_MEDIA_PRESENT);
    failed |= log_status(open.wDeviceID, "MODE", MCI_STATUS_MODE);
    failed |= log_status(open.wDeviceID, "POSITION_MSF", MCI_STATUS_POSITION);
    failed |= log_status(open.wDeviceID, "LENGTH_MSF", MCI_STATUS_LENGTH);
    error = status(open.wDeviceID, MCI_STATUS_NUMBER_OF_TRACKS, 0, &tracks);
    wsprintfA(text, "TRACK_COUNT error=%lu value=%lu", error, tracks);
    line(text);
    if (error || !tracks || tracks > 99) {
        failed = 1;
        goto close;
    }

    for (track = 1; track <= tracks; ++track) {
        type_error = status(open.wDeviceID, MCI_CDA_STATUS_TYPE_TRACK, track, &type);
        position_error = status(open.wDeviceID, MCI_STATUS_POSITION, track, &position);
        length_error = status(open.wDeviceID, MCI_STATUS_LENGTH, track, &length);
        wsprintfA(text,
                  "TRACK %lu type=%s type_error=%lu position=%02u:%02u:%02u position_error=%lu "
                  "length=%02u:%02u:%02u length_error=%lu",
                  track,
                  type_error                    ? "unknown"
                  : type == MCI_CDA_TRACK_AUDIO ? "audio"
                  : type == MCI_CDA_TRACK_OTHER ? "data"
                                                : "unknown",
                  type_error, MCI_MSF_MINUTE(position), MCI_MSF_SECOND(position),
                  MCI_MSF_FRAME(position), position_error, MCI_MSF_MINUTE(length),
                  MCI_MSF_SECOND(length), MCI_MSF_FRAME(length), length_error);
        line(text);
        if (type_error || position_error || length_error ||
            (type != MCI_CDA_TRACK_AUDIO && type != MCI_CDA_TRACK_OTHER))
            failed = 1;
    }
close:
    error = mciSendCommandA(open.wDeviceID, MCI_CLOSE, MCI_WAIT, 0);
    error_line("MCI_CLOSE", error);
    return failed || error;
}

static DWORD frames(DWORD msf) {
    return ((DWORD)MCI_MSF_MINUTE(msf) * 60 + MCI_MSF_SECOND(msf)) * 75 + MCI_MSF_FRAME(msf);
}

static DWORD msf(DWORD frame) {
    return MCI_MAKE_MSF(frame / 4500, frame / 75 % 60, frame % 75);
}

static int sample(MCIDEVICEID device, const char *phase, DWORD expected_mode, DWORD *position) {
    DWORD mode, raw;
    MCIERROR mode_error = status(device, MCI_STATUS_MODE, 0, &mode);
    MCIERROR position_error = status(device, MCI_STATUS_POSITION, 0, &raw);
    *position = frames(raw);
    wsprintfA(text,
              "SAMPLE %s tick=%lu mode=%lu expected=%lu mode_error=%lu position=%02u:%02u:%02u "
              "frames=%lu position_error=%lu",
              phase, GetTickCount(), mode, expected_mode, mode_error, MCI_MSF_MINUTE(raw),
              MCI_MSF_SECOND(raw), MCI_MSF_FRAME(raw), *position, position_error);
    line(text);
    FlushFileBuffers(output);
    return mode_error || position_error || mode != expected_mode;
}

static int playback(char letter) {
    char element[3] = "D:";
    MCI_OPEN_PARMSA open;
    MCI_SET_PARMS set;
    MCI_PLAY_PARMS play;
    MCIERROR error;
    DWORD type, start, length, first, second, paused, still, resumed, stopped, end;
    int failed = 0;
    element[0] = letter;
    ZeroMemory(&open, sizeof(open));
    open.lpstrDeviceType = "cdaudio";
    open.lpstrElementName = element;
    error = mciSendCommandA(0, MCI_OPEN,
                            MCI_WAIT | MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_OPEN_SHAREABLE,
                            (DWORD_PTR)&open);
    error_line("MCI_OPEN", error);
    if (error)
        return 1;
    ZeroMemory(&set, sizeof(set));
    set.dwTimeFormat = MCI_FORMAT_MSF;
    error =
        mciSendCommandA(open.wDeviceID, MCI_SET, MCI_WAIT | MCI_SET_TIME_FORMAT, (DWORD_PTR)&set);
    error_line("MCI_SET_MSF", error);
    if (error) {
        failed = 1;
        goto close;
    }
    /* The supplied original Half-Life disc uses audio track 3 for its menu.
     * Refuse any disc where that bounded span is not actually audio. */
    if (status(open.wDeviceID, MCI_CDA_STATUS_TYPE_TRACK, 3, &type) ||
        type != MCI_CDA_TRACK_AUDIO || status(open.wDeviceID, MCI_STATUS_POSITION, 3, &start) ||
        status(open.wDeviceID, MCI_STATUS_LENGTH, 3, &length) || frames(length) < 15 * 75) {
        line("Track 3 must be audio with at least 15 seconds");
        failed = 1;
        goto close;
    }
    ZeroMemory(&play, sizeof(play));
    play.dwFrom = start;
    play.dwTo = msf(frames(start) + 15 * 75);
    wsprintfA(text, "PLAY_TRACK3 from_frames=%lu to_frames=%lu", frames(start), frames(play.dwTo));
    line(text);
    error = mciSendCommandA(open.wDeviceID, MCI_PLAY, MCI_FROM | MCI_TO, (DWORD_PTR)&play);
    error_line("MCI_PLAY", error);
    if (error) {
        failed = 1;
        goto stop;
    }
    Sleep(1500);
    failed |= sample(open.wDeviceID, "playing1", MCI_MODE_PLAY, &first);
    Sleep(1000);
    failed |= sample(open.wDeviceID, "playing2", MCI_MODE_PLAY, &second);
    if (first <= frames(start) || second <= first)
        failed = 1;
    error = mciSendCommandA(open.wDeviceID, MCI_PAUSE, MCI_WAIT, 0);
    error_line("MCI_PAUSE", error);
    failed |= error != 0;
    /* MCICDA implements MCI_PAUSE like MCI_STOP, retaining its current
     * position. See learn.microsoft.com/windows/win32/multimedia/pause.
     * Actual Win98 emits SEEK here and reports MCI_MODE_STOP. */
    failed |= sample(open.wDeviceID, "paused1", MCI_MODE_STOP, &paused);
    Sleep(1000);
    failed |= sample(open.wDeviceID, "paused2", MCI_MODE_STOP, &still);
    if (paused != still)
        failed = 1;
    /* PLAY without FROM resumes from the current position in the MCI CD
     * driver. Keep TO explicit so the resumed request remains bounded. */
    error = mciSendCommandA(open.wDeviceID, MCI_PLAY, MCI_TO, (DWORD_PTR)&play);
    error_line("MCI_PLAY_RESUME", error);
    failed |= error != 0;
    Sleep(1500);
    failed |= sample(open.wDeviceID, "resumed", MCI_MODE_PLAY, &resumed);
    if (resumed <= paused)
        failed = 1;
stop:
    error = mciSendCommandA(open.wDeviceID, MCI_STOP, MCI_WAIT, 0);
    error_line("MCI_STOP", error);
    failed |= error != 0;
    failed |= sample(open.wDeviceID, "stopped1", MCI_MODE_STOP, &stopped);
    Sleep(750);
    failed |= sample(open.wDeviceID, "stopped2", MCI_MODE_STOP, &end);
    if (stopped != end)
        failed = 1;
close:
    error = mciSendCommandA(open.wDeviceID, MCI_CLOSE, MCI_WAIT, 0);
    error_line("MCI_CLOSE", error);
    return failed || error;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    char root[4] = "A:\\";
    int found = 0, failed = 0, play = 0;
    (void)instance;
    (void)previous;
    (void)show;
    if (command && *command) {
        if (lstrlenA(command) != 8 || command[0] != '/' || command[1] != 'p' || command[2] != 'l' ||
            command[3] != 'a' || command[4] != 'y' || command[5] != ' ' || command[6] < 'A' ||
            command[6] > 'Z' || command[7] != ':')
            return 2;
        root[0] = command[6];
        play = 1;
    }
    output = CreateFileA(play ? "C:\\DGCDP.LOG" : "C:\\DGCD.LOG", GENERIC_WRITE, FILE_SHARE_READ,
                         NULL, CREATE_ALWAYS, 0, NULL);
    if (output == INVALID_HANDLE_VALUE)
        return 1;
    if (play) {
        line("DreamGPU legacy optical playback probe v3; bounded track3 play/pause/resume/stop; "
             "MCICDA pause=stop");
        failed = GetDriveTypeA(root) != DRIVE_CDROM || playback(root[0]);
        wsprintfA(text, "RESULT playback_failures=%d", failed);
        line(text);
        CloseHandle(output);
        return failed;
    }
    line("DreamGPU legacy optical metadata probe v1; read-only, no playback");
    for (root[0] = 'A'; root[0] <= 'Z'; ++root[0]) {
        if (GetDriveTypeA(root) != DRIVE_CDROM)
            continue;
        ++found;
        if (inspect(root[0]))
            failed = 1;
    }
    wsprintfA(text, "RESULT drives=%d failures=%d", found, failed);
    line(text);
    CloseHandle(output);
    return failed || !found;
}
