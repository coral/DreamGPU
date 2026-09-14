/* SPDX-License-Identifier: GPL-2.0-or-later */
static struct {
    int dev;
    QemuInputEvent event;
} log_events[256];
static unsigned count;
static void received(DeviceState *d, QemuConsole *c, QemuInputEvent *e) {
    (void)c;
    assert(count < 256);
    log_events[count].dev = d->id;
    log_events[count++].event = *e;
}
static void send_event(DreamGpuShmemState *s, unsigned type, int x, int y, bool down) {
    DreamGpuInputEvent e = {.type = type, .x = x, .y = y, .pressed = down};
    dreamgpu_shmem_process_event(s, &e);
}
int main(void) {
    DeviceState ps2dev = {1}, tabletdev = {2};
    QemuInputHandler ps2h = {INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_BTN, received},
                     tableth = {INPUT_EVENT_MASK_ABS | INPUT_EVENT_MASK_BTN, received};
    QemuInputHandlerState ps2 = {.dev = &ps2dev, .handler = &ps2h},
                          tablet = {.dev = &tabletdev, .handler = &tableth};
    QemuConsole con = {0};
    Header header = {1024, 768};
    DreamGpuShmemState s = {.dcl = {&con}, .shmem = &header};
    QTAILQ_INSERT_TAIL(&handlers, &tablet, node);
    QTAILQ_INSERT_TAIL(&handlers, &ps2, node);
    send_event(&s, 2, 300, 200, false);
    assert(count == 2 && log_events[0].dev == 2);
    send_event(&s, 3, 0, 0, true);
    assert(log_events[2].dev == 2 && log_events[2].event.btn.down);
    send_event(&s, 8, 0, 0, true);
    assert(count == 4 && log_events[3].dev == 2 && !log_events[3].event.btn.down);
    send_event(&s, 3, 0, 0, true);
    assert(log_events[4].dev == 1 && log_events[4].event.btn.down);
    count = 0;
    /* Game recenter cannot influence true deltas, even at framebuffer edges. */
    s.mouse_x = 1023;
    s.mouse_y = 0;
    send_event(&s, 1, 17, -23, false);
    send_event(&s, 1, 17, -23, false);
    assert(count == 4);
    for (unsigned i = 0; i < 4; i++) {
        assert(log_events[i].dev == 1);
        assert(log_events[i].event.type == INPUT_EVENT_KIND_REL);
        assert(log_events[i].event.rel.value == (i % 2 ? -23 : 17));
    }
    count = 0;
    send_event(&s, 8, 0, 0, false);
    assert(count == 1 && log_events[0].dev == 1 && !log_events[0].event.btn.down);
    send_event(&s, 3, 0, 0, true);
    assert(log_events[1].dev == 2);
    send_event(&s, 2, 500, 400, false);
    assert(log_events[2].dev == 2);
    count = 0;
    send_event(&s, 1, -4, 7, false); /* legacy consumer infers relative */
    assert(count == 3 && log_events[0].dev == 2 && !log_events[0].event.btn.down);
    assert(log_events[1].dev == 1 && log_events[1].event.rel.value == -4);
    count = 0;
    send_event(&s, 5, 0, 0, false); /* snapshot/focus release both devices */
    unsigned ps2up = 0, tabletup = 0;
    for (unsigned i = 0; i < count; i++) {
        assert(!log_events[i].event.btn.down);
        ps2up += log_events[i].dev == 1;
        tabletup += log_events[i].dev == 2;
    }
    assert(ps2up == INPUT_BUTTON__MAX && tabletup == INPUT_BUTTON__MAX &&
           !qemu_input_is_absolute(&con));
    count = 0;
    send_event(&s, 2, -8, 9999, false);
    assert(count == 2 && log_events[0].dev == 2 && log_events[0].event.abs.value == 0);
    QTAILQ_REMOVE(&handlers, &tablet, node);
    s.mouse_initialized = false;
    count = 0;
    send_event(&s, 2, 10, 20, false);
    assert(count == 0);
    send_event(&s, 2, 15, 27, false);
    assert(count == 2 && log_events[0].event.rel.value == 5 && log_events[1].event.rel.value == 7);
    count = 0;
    send_event(&s, 1, 100, -100, false);
    send_event(&s, 2, 300, 200, false);
    assert(count == 2); /* re-anchor absolute fallback */
    puts("Actual QEMU pointer routing: capture, repeated relative deltas, button ownership, reset, "
         "legacy and relative-only desktop PASS");
}
