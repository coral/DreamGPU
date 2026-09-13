#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual QEMU cancellation bridge without a VM or native GL driver."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'vendor/qemu/hw/display/dreamgpu-gl.c'


def function(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    source = SOURCE.read_text()
    bodies = '\n'.join(function(source, name) for name in (
        'static uint32_t submission_cancelled(',
        'static bool submission_reset_complete(',
        'void dg_gl_engine_reset('))
    free = function(source, 'void dg_gl_engine_free(')
    # Stop publishes cancellation under the same lock, before resource joins.
    assert free.index('qemu_mutex_lock') < free.index('e->stopping = true;')
    assert free.index('qatomic_set(&e->cancel_requested, 1)') < free.index('qemu_thread_join')
    harness = r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#define qatomic_read(p) atomic_load_explicit(p, memory_order_relaxed)
#define qatomic_set(p,v) atomic_store_explicit(p,v,memory_order_relaxed)
#define qemu_mutex_lock pthread_mutex_lock
#define qemu_mutex_unlock pthread_mutex_unlock
#define qemu_cond_broadcast pthread_cond_broadcast
struct Work { uint32_t generation; bool reset; };
typedef struct { uint32_t generation; } DreamGpuResetTicket;
typedef struct { pthread_mutex_t lock; pthread_cond_t cond; struct Work work;
    bool stopping; _Atomic uint32_t cancel_requested; int desktop,last_present; } DgGLEngine;
typedef struct { DgGLEngine *engine; } DgDispatch;
static int submission_memory;
static void dreamgpu_submission_reset(struct Work *w, int *m, uint32_t g, uint64_t e, uint64_t f) {
    (void)m;(void)e;(void)f;w->generation=g;w->reset=true;
}
static bool dreamgpu_submission_reset_done(struct Work *w, int *m, DreamGpuResetTicket *t, int *d, int *p) {
    (void)m;(void)d;(void)p;
    if(t->generation!=w->generation) return false;
    w->reset=false;return true;
}
''' + bodies + r'''
static void *writer(void *p) { dg_gl_engine_reset(p,3,0,0);return NULL; }
static uint64_t clock_ns(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
int main(void) {
    alarm(10);
    DgGLEngine e={.lock=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};
    DgDispatch d={&e};
    assert(!submission_cancelled(&d));
    pthread_mutex_lock(&e.lock);
    assert(!submission_cancelled(&d)); /* callback must not acquire held engine lock */
    pthread_mutex_unlock(&e.lock);
    dg_gl_engine_reset(&e,1,0,0);assert(submission_cancelled(&d));
    DreamGpuResetTicket old={1},newer={2};
    dg_gl_engine_reset(&e,2,0,0);
    pthread_mutex_lock(&e.lock);
    assert(!submission_reset_complete(&e,&old));assert(submission_cancelled(&d));
    assert(submission_reset_complete(&e,&newer));assert(!submission_cancelled(&d));
    pthread_mutex_unlock(&e.lock);
    pthread_t t;assert(!pthread_create(&t,NULL,writer,&e));assert(!pthread_join(t,NULL));
    assert(submission_cancelled(&d));
    pthread_mutex_lock(&e.lock);e.stopping=true;
    DreamGpuResetTicket last={3};assert(submission_reset_complete(&e,&last));
    assert(submission_cancelled(&d));pthread_mutex_unlock(&e.lock);
    uint64_t start=clock_ns();unsigned sum=0;
    for(unsigned i=0;i<2000000;i++) sum+=submission_cancelled(&d);
    uint64_t fast=clock_ns()-start;assert(sum==2000000);
    start=clock_ns();sum=0;
    for(unsigned i=0;i<2000000;i++) {pthread_mutex_lock(&e.lock);sum+=e.stopping||e.work.reset;pthread_mutex_unlock(&e.lock);}
    assert(sum==2000000);
    printf("records=2000000 atomic_ns=%llu locked_ns=%llu\n",(unsigned long long)fast,(unsigned long long)(clock_ns()-start));
    pthread_cond_destroy(&e.cond);pthread_mutex_destroy(&e.lock);
    puts("PASS actual cancellation/reset bridge: stale ticket, stop, concurrent reset, no held-lock deadlock");
}
'''
    with tempfile.TemporaryDirectory(prefix='dg-cancel-') as folder:
        c = Path(folder) / 'gate.c'
        binary = Path(folder) / 'gate'
        c.write_text(harness)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-pthread', '-fsanitize=address,undefined', str(c), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == '__main__':
    main()
