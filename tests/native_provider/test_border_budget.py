#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual pinned-provider allocation ledger and final release hook."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1])
src = (root / 'src/gallium/auxiliary/util/u_helpers.c').read_text()
body = src[src.index('/* Border CPU images and GPU atlases'):]
src = (root / 'src/gallium/auxiliary/util/u_inlines.h').read_text()
a = src.index('static inline void\npipe_resource_destroy(')
b = src.index('\nstatic inline void\npipe_resource_reference(', a)
destroy = src[a:b]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
typedef pthread_mutex_t simple_mtx_t;
#define SIMPLE_MTX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define simple_mtx_lock pthread_mutex_lock
#define simple_mtx_unlock pthread_mutex_unlock
#define p_atomic_read(p) __atomic_load_n(p,__ATOMIC_SEQ_CST)
#define p_atomic_inc(p) ((void)__atomic_fetch_add(p,1,__ATOMIC_SEQ_CST))
#define p_atomic_dec(p) ((void)__atomic_fetch_sub(p,1,__ATOMIC_SEQ_CST))
struct pipe_resource;
struct pipe_screen {void (*resource_destroy)(struct pipe_screen*,struct pipe_resource*);};
struct pipe_reference {unsigned count;};
struct pipe_resource {struct pipe_reference reference;struct pipe_resource *next;struct pipe_screen *screen;};
typedef void (*debug_reference_descriptor)(void);
static void debug_describe_resource(void) {}
static bool pipe_reference_described(struct pipe_reference*r,void*n,debug_reference_descriptor d) {
 (void)n;(void)d;return r && --r->count==0;
}
static bool fail_entry;
static void *entry_malloc(size_t n){return fail_entry?NULL:malloc(n);}
#define malloc entry_malloc
'''
tests = r'''
#undef malloc
static atomic_uint destructions;
static bool reuse_during_destroy;
static uintptr_t reused_identity;
static void native_destroy(struct pipe_screen*s,struct pipe_resource*r) {
 assert(s==r->screen);
 // Credit must still be charged until this real destructor finishes.
 if (r->reference.count==0) assert(border_storage_bytes);
 uintptr_t key=(uintptr_t)r;
 atomic_fetch_add(&destructions,1);free(r);
 if(reuse_during_destroy) {
   reuse_during_destroy=false;
   // Deterministic address reuse inside the original native destructor.
   // An ordinary resource at this key has no old entry to consume.
   assert(!util_border_resource_detach(key));
   assert(util_border_storage_reserve(512));
   assert(util_border_resource_register((struct pipe_resource*)key,512));
   reused_identity=key;
 }
}
static struct pipe_screen screen={native_destroy};
static struct pipe_resource *make(unsigned refs) {
 struct pipe_resource*r=calloc(1,sizeof*r);assert(r);r->reference.count=refs;r->screen=&screen;return r;
}
static void drop(struct pipe_resource*r) {assert(r->reference.count);if(!--r->reference.count)pipe_resource_destroy(r);}
static void *worker(void*p) {
 (void)p;
 for(unsigned i=0;i<1000;i++) {
   assert(util_border_storage_reserve(64));
   struct pipe_resource*r=make(2);assert(util_border_resource_register(r,64));
   drop(r);drop(r);
 }
 return NULL;
}
int main(void) {
 assert(!util_border_storage_reserve(SIZE_MAX));assert(!border_storage_bytes);
 assert(util_border_storage_reserve(BORDER_STORAGE_LIMIT));
 assert(!util_border_storage_reserve(1));util_border_storage_release(BORDER_STORAGE_LIMIT);
 struct pipe_resource*r=make(3);
 assert(util_border_storage_reserve(4096));fail_entry=true;
 assert(!util_border_resource_register(r,4096));assert(border_storage_bytes==4096&&!border_resource_count);
 fail_entry=false;assert(util_border_resource_register(r,4096));
 size_t charge=4096+sizeof(struct border_resource_entry);assert(border_storage_bytes==charge);
 assert(!util_border_resource_register(r,4096));assert(border_storage_bytes==charge&&border_resource_count==1);
 // Shared sampler-view references survive object deletion without releasing credit.
 drop(r);drop(r);assert(border_storage_bytes==charge&&atomic_load(&destructions)==0);
 // A foreign/cloned resource identity cannot release registered ownership.
 assert(!util_border_resource_detach((uintptr_t)r+1));assert(border_storage_bytes==charge);
 assert(util_border_storage_reserve(BORDER_STORAGE_LIMIT-charge));
 assert(!util_border_storage_reserve(1));drop(r);
 assert(border_storage_bytes==BORDER_STORAGE_LIMIT-charge&&!border_resource_count);
 util_border_storage_release(BORDER_STORAGE_LIMIT-charge);assert(!border_storage_bytes);
 // Real helper destroys linked planes only at each plane's final reference.
 struct pipe_resource*a=make(1),*b=make(2);a->next=b;
 assert(util_border_storage_reserve(96));
 assert(util_border_resource_register(a,32));assert(util_border_resource_register(b,64));
 drop(a);assert(b->reference.count==1&&border_resource_count==1);
 drop(b);assert(!border_storage_bytes&&!border_resource_count);
 // Detached credits remain charged even when no entries remain registered.
 r=make(1);assert(util_border_storage_reserve(1024));assert(util_border_resource_register(r,1024));
 reuse_during_destroy=true;drop(r);
 assert(border_resource_count==1&&border_storage_bytes==512+sizeof(struct border_resource_entry));
 void *token=util_border_resource_detach(reused_identity);
 assert(token&&!border_resource_count&&border_storage_bytes==512+sizeof(struct border_resource_entry));
 assert(util_border_storage_reserve(BORDER_STORAGE_LIMIT-border_storage_bytes));
 assert(!util_border_storage_reserve(1));util_border_resource_retire(token);
 util_border_storage_release(BORDER_STORAGE_LIMIT-512-sizeof(struct border_resource_entry));
 assert(!border_storage_bytes);
 pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 assert(!border_storage_bytes&&!border_resource_count);
 puts("PASS actual64MiB ledger/final destructor: metadata/failure rollback, shared views, foreign identity, chains, concurrent teardown");
}
'''
# The native destructor's diagnostic read runs concurrently in the threaded case;
# read the protected counter under its own mutex rather than introducing a test race.
tests = tests.replace('if (r->reference.count==0) assert(border_storage_bytes);',
                      'simple_mtx_lock(&border_storage_mutex); assert(border_storage_bytes); simple_mtx_unlock(&border_storage_mutex);')
with tempfile.TemporaryDirectory(prefix='dg-border-budget-') as temporary:
    p = Path(temporary)
    (p / 'test.c').write_text(prefix + body + destroy + tests)
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-pthread', str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
