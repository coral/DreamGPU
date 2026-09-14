/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "qemu/queue.h"
typedef struct {
    int id;
} QemuConsole;
typedef struct {
    int id;
} DeviceState;
typedef int InputButton;
typedef int InputAxis;
enum { INPUT_EVENT_KIND_KEY, INPUT_EVENT_KIND_BTN, INPUT_EVENT_KIND_REL, INPUT_EVENT_KIND_ABS };
#define INPUT_EVENT_MASK_KEY (1 << INPUT_EVENT_KIND_KEY)
#define INPUT_EVENT_MASK_BTN (1 << INPUT_EVENT_KIND_BTN)
#define INPUT_EVENT_MASK_REL (1 << INPUT_EVENT_KIND_REL)
#define INPUT_EVENT_MASK_ABS (1 << INPUT_EVENT_KIND_ABS)
#define INPUT_EVENT_ABS_MIN 0
#define INPUT_EVENT_ABS_MAX 32767
#define INPUT_BUTTON__MAX 8
#define INPUT_AXIS_X 0
#define INPUT_AXIS_Y 1
typedef struct {
    int type;
    union {
        struct {
            int button;
            bool down;
        } btn;
        struct {
            int axis, value;
        } rel, abs;
    };
} QemuInputEvent;
typedef struct {
    uint32_t mask;
    void (*event)(DeviceState *, QemuConsole *, QemuInputEvent *);
} QemuInputHandler;
typedef struct QemuInputHandlerState {
    DeviceState *dev;
    const QemuInputHandler *handler;
    QemuConsole *con;
    unsigned events;
    QTAILQ_ENTRY(QemuInputHandlerState) node;
} QemuInputHandlerState;
static QTAILQ_HEAD(, QemuInputHandlerState) handlers = QTAILQ_HEAD_INITIALIZER(handlers);
static void notify_input_changed(uint32_t mask) {
    (void)mask;
}
static void qemu_input_event_trace(QemuConsole *con, QemuInputEvent *ev) {
    (void)con;
    (void)ev;
}
void qemu_input_event_send_impl(QemuConsole *, QemuInputEvent *);
static void qemu_input_event_send(QemuConsole *con, QemuInputEvent *ev) {
    qemu_input_event_send_impl(con, ev);
}
static void qemu_input_event_sync(void) {}
static void qemu_input_event_send_key_number(QemuConsole *con, int key, bool down) {
    (void)con;
    (void)key;
    (void)down;
}
static int qemu_input_key_number_to_linux(int key) {
    return key;
}
#define DREAMGPU_INPUT_MOUSE_REL 1
#define DREAMGPU_INPUT_MOUSE_ABS 2
#define DREAMGPU_INPUT_MOUSE_BTN 3
#define DREAMGPU_INPUT_KEY 4
#define DREAMGPU_INPUT_RESET 5
#define DREAMGPU_INPUT_REFRESH 6
#define DREAMGPU_HOST_REFRESH 7
#define DREAMGPU_INPUT_MOUSE_CAPTURE 8
#define DREAMGPU_INPUT_REFRESH_INTERVAL_MS 1
#define DREAMGPU_INPUT_REFRESH_WINDOW_US 12000
typedef struct {
    int width, height;
} Header;
typedef struct {
    QemuConsole *con;
} Listener;
typedef struct {
    Listener dcl;
    Header *shmem;
    int32_t mouse_x, mouse_y;
    bool mouse_initialized;
    bool held_keys[256], held_buttons[INPUT_BUTTON__MAX];
    uint64_t input_id, input_refresh_until_us;
    int refresh_clock;
    bool dirty;
} DreamGpuShmemState;
typedef struct {
    uint8_t type, button, pressed, reserved;
    int32_t x, y;
    uint32_t padding;
    uint64_t id;
} DreamGpuInputEvent;
static uint64_t dreamgpu_now_us(void) {
    return 100;
}
static void dreamgpu_refresh_set(int *c, int rate, uint64_t now) {
    (void)c;
    (void)rate;
    (void)now;
}
static int dreamgpu_refresh_delay(int *c, uint64_t now) {
    (void)c;
    (void)now;
    return 1;
}
static void qemu_console_listener_set_refresh(Listener *l, int delay) {
    (void)l;
    (void)delay;
}
static void dreamgpu_shmem_publish(DreamGpuShmemState *s) {
    (void)s;
}
static void dreamgpu_shmem_notify(DreamGpuShmemState *s, char c, uint64_t id, uint64_t t) {
    (void)s;
    (void)c;
    (void)id;
    (void)t;
}
