/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "dreamgpu-timing.h"
#define VBE_DISPI_INDEX_ENABLE 0
#define VBE_DISPI_INDEX_YRES 1
#define VBE_DISPI_ENABLED 1
#define QEMU_CLOCK_VIRTUAL 0
static uint64_t clock_ns;
typedef struct {
    bool armed;
    uint64_t deadline;
} QEMUTimer;
typedef struct DreamGpu {
    struct {
        uint32_t vbe_regs[2];
        void *con;
    } vga;
    uint32_t timing_rate, timing_serial, timing_sequence, timing_completed, timing_status,
        irq_status;
    QEMUTimer *timing_timer;
    DgTimingSample timing_sample;
} DreamGpu;
static uint32_t qemu_console_get_height(void *con, int fallback) {
    (void)con;
    return fallback;
}
static uint64_t qemu_clock_get_ns(int clock) {
    (void)clock;
    return clock_ns;
}
static void timer_del(QEMUTimer *timer) {
    timer->armed = false;
}
static void timer_mod(QEMUTimer *timer, uint64_t deadline) {
    timer->armed = true;
    timer->deadline = deadline;
}
static void dg_update_irq(DreamGpu *s) {
    (void)s;
}
#include "display-timing-source.inc"
int main(void) {
    const uint32_t rates[] = {60, 75, 85, 100, 120};
    QEMUTimer timer = {0};
    DreamGpu s = {.vga = {.vbe_regs = {1, 768}}, .timing_timer = &timer, .timing_rate = 60};
    for (unsigned r = 0; r < 5; ++r) {
        dg_timing_write(&s, DG_TIMING_REG_RATE, rates[r]);
        assert(dg_timing_read(&s, DG_TIMING_REG_RATE) == rates[r]);
        for (uint64_t now = 0; now < 1000000000; now += 77777) {
            clock_ns = now;
            uint32_t serial = dg_timing_read(&s, DG_TIMING_REG_SNAPSHOT);
            DgTimingSample a = s.timing_sample;
            assert(a.height == 768 && a.line < 788 && (a.phase >> 16) == rates[r]);
            assert((a.phase & 1) == (a.line >= 768));
            assert(a.begin_ns && a.end_ns && a.begin_ns <= 16666667 && a.end_ns <= 16666667);
            clock_ns += 1234567;
            assert(dg_timing_read(&s, DG_TIMING_REG_SERIAL) == serial);
            assert(dg_timing_read(&s, DG_TIMING_REG_SCANLINE) == a.line);
            DgTimingSample b = dg_timing_sample(now + a.begin_ns, rates[r], 768);
            assert(b.phase & 1);
            b = dg_timing_sample(now + a.end_ns, rates[r], 768);
            assert(!(b.phase & 1) && b.line == 0);
        }
        clock_ns = 12345678;
        dg_timing_write(&s, DG_TIMING_REG_SEQUENCE, r + 1);
        dg_timing_write(&s, DG_TIMING_REG_COMMAND, DG_TIMING_WAIT_BEGIN);
        assert(timer.armed && timer.deadline > clock_ns && s.timing_status == DG_TIMING_PENDING);
        uint64_t deadline = timer.deadline;
        dg_timing_write(&s, DG_TIMING_REG_SEQUENCE, 999);
        dg_timing_write(&s, DG_TIMING_REG_COMMAND, DG_TIMING_WAIT_END);
        assert(s.timing_sequence == r + 1 && timer.deadline == deadline);
        clock_ns = deadline;
        dg_timing_expired(&s);
        assert(!timer.armed && s.timing_completed == r + 1 && s.timing_status == DG_TIMING_DONE);
        assert(s.irq_status & DG_IRQ_DISPLAY_TIMING);
        s.irq_status = DG_IRQ_GL_COMPLETION;
        dg_timing_write(&s, DG_TIMING_REG_SEQUENCE, r + 101);
        dg_timing_write(&s, DG_TIMING_REG_COMMAND, DG_TIMING_WAIT_END);
        dg_timing_write(&s, DG_TIMING_REG_RATE, rates[r]);
        assert(!timer.armed && s.timing_status == DG_TIMING_CANCELLED &&
               s.timing_completed == r + 101);
        assert(s.irq_status & DG_IRQ_GL_COMPLETION);
        dg_timing_expired(&s); /* Late completion cannot overwrite cancellation. */
        assert(s.timing_status == DG_TIMING_CANCELLED);
    }
    dg_timing_write(&s, DG_TIMING_REG_RATE, 144);
    assert(s.timing_rate == 120);
    puts("PASS rational60/75/85/100/120Hz scanout, coherent snapshots, one-shot wait/IRQ, "
         "cancellation and late callback");
}
