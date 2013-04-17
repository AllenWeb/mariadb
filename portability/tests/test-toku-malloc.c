/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#include <stdio.h>
#include <toku_assert.h>
#include <memory.h>
#include <toku_pthread.h>

static void *f(void *arg) {
    void *vp = toku_malloc(32);
    assert(vp);
    toku_free(vp);
    return arg;
}

int main(void) {
    int r;
    int i;
    const int max_threads = 2;
    toku_pthread_t tids[max_threads];
    for (i=0; i<max_threads; i++) {
        r = toku_pthread_create(&tids[i], NULL, f, 0); assert(r == 0);
    }
    for (i=0; i<max_threads; i++) {
        void *ret;
        r = toku_pthread_join(tids[i], &ret); assert(r == 0);
    }
    return 0;
}
