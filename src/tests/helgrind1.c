/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"
// The helgrind1.tdbrun test should fail.  This is merely a check to verify that helgrind actually notices a race.

#include <pthread.h>
int x;

static void *starta(void* ignore __attribute__((__unused__))) {
    if (verbose) printf("%s %d\n", __FUNCTION__, x);
    x++;
    return 0;
}
static void *startb(void* ignore __attribute__((__unused__))) {
    if (verbose) printf("%s %d\n", __FUNCTION__, x);
    x++;
    return 0;
}

int
test_main (int argc, char * const argv[]) {
    parse_args(argc, argv);
    pthread_t a,b;
    { int x_l = pthread_create(&a, NULL, starta, NULL); assert(x_l==0); }
    { int x_l = pthread_create(&b, NULL, startb, NULL); assert(x_l==0); }
    { int x_l = pthread_join(a, NULL);           assert(x_l==0); }
    { int x_l = pthread_join(b, NULL);           assert(x_l==0); }
    return 0;
}
