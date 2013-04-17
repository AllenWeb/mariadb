/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2011 Tokutek Inc.  All rights reserved."
#include "includes.h"
#include "test.h"

CACHETABLE ct;

CACHEFILE f1;

static void
unlock_test_fun (void *v) {
    assert(v == NULL);
    // CT lock is held
    int r = toku_cachetable_unpin_ct_prelocked_no_flush(f1, make_blocknum(2), 2, CACHETABLE_CLEAN, make_pair_attr(8));
    assert(r==0);
}

static void
run_test (void) {
    const int test_limit = 20;
    int r;
    ct = NULL;
    r = toku_create_cachetable(&ct, test_limit, ZERO_LSN, NULL_LOGGER); assert(r == 0);
    char fname1[] = __SRCFILE__ "test1.dat";
    unlink(fname1);
    f1 = NULL;
    r = toku_cachetable_openf(&f1, ct, fname1, O_RDWR|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO); assert(r == 0);
    
    void* v1;
    void* v2;
    long s1;
    long s2;
    
    r = toku_cachetable_get_and_pin(f1, make_blocknum(1), 1, &v1, &s1, def_write_callback(NULL), def_fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
    r = toku_cachetable_unpin(f1, make_blocknum(1), 1, CACHETABLE_CLEAN, make_pair_attr(8)); assert(r==0);

    for (int i = 0; i < 20; i++) {
        r = toku_cachetable_get_and_pin(f1, make_blocknum(2), 2, &v2, &s2, def_write_callback(NULL), def_fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
        r = toku_cachetable_unpin(f1, make_blocknum(2), 2, CACHETABLE_CLEAN, make_pair_attr(8)); assert(r==0);
    }

    //
    // so at this point, we have 16 bytes in the cachetable that has a limit of 20 bytes
    // block 2 has been touched much more than block 1, so if one had to be evicted,
    // it would be block 2
    //


    // pin 1 and 2
    r = toku_cachetable_get_and_pin(f1, make_blocknum(1), 1, &v2, &s2, def_write_callback(NULL), def_fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
    r = toku_cachetable_begin_checkpoint(ct, NULL);
    // mark nodes as pending a checkpoint, so that get_and_pin_nonblocking on block 1 will return TOKUDB_TRY_AGAIN
    r = toku_cachetable_unpin(f1, make_blocknum(1), 1, CACHETABLE_DIRTY, make_pair_attr(8)); assert(r==0);

    r = toku_cachetable_get_and_pin(f1, make_blocknum(2), 2, &v2, &s2, def_write_callback(NULL), def_fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
    // now we try to pin 1, and it should get evicted out from under us
    struct unlockers foo;
    foo.extra = NULL;
    foo.locked = TRUE;
    foo.f = unlock_test_fun;
    foo.next = NULL;
    r = toku_cachetable_get_and_pin_nonblocking(
        f1,
        make_blocknum(1),
        1,
        &v1,
        &s1,
        def_write_callback(NULL),
        def_fetch,
        def_pf_req_callback,
        def_pf_callback,
        TRUE,
        NULL,
        &foo
        );
    assert(r==TOKUDB_TRY_AGAIN);
    
    r = toku_cachetable_end_checkpoint(
        ct, 
        NULL, 
        NULL,
        NULL
        );
    assert(r==0);
    
    toku_cachetable_verify(ct);
    r = toku_cachefile_close(&f1, 0, FALSE, ZERO_LSN); assert(r == 0);
    r = toku_cachetable_close(&ct); lazy_assert_zero(r);
    
    
}

int
test_main(int argc, const char *argv[]) {
  default_parse_args(argc, argv);
  run_test();
  return 0;
}
