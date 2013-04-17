/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#include "includes.h"
#include "test.h"

BOOL flush_may_occur;
long expected_bytes_to_free;

static void
flush (CACHEFILE f __attribute__((__unused__)),
       int UU(fd),
       CACHEKEY k  __attribute__((__unused__)),
       void* UU(v),
       void** UU(dd),
       void *e     __attribute__((__unused__)),
       PAIR_ATTR s      __attribute__((__unused__)),
       PAIR_ATTR* new_size      __attribute__((__unused__)),
       BOOL w      __attribute__((__unused__)),
       BOOL keep,
       BOOL c      __attribute__((__unused__)),
        BOOL UU(is_clone)
       ) {
    assert(flush_may_occur);
    if (!keep) {
        //int* foo = v;
        //assert(*foo == 3);
        toku_free(v);
    }
}

static int
fetch (CACHEFILE f        __attribute__((__unused__)),
       int UU(fd),
       CACHEKEY k         __attribute__((__unused__)),
       u_int32_t fullhash __attribute__((__unused__)),
       void **value       __attribute__((__unused__)),
       void** UU(dd),
       PAIR_ATTR *sizep        __attribute__((__unused__)),
       int  *dirtyp,
       void *extraargs    __attribute__((__unused__))
       ) {
    *dirtyp = 0;
    int* foo = toku_malloc(sizeof(int));
    *value = foo;
    *sizep = make_pair_attr(4);
    *foo = 4;
    return 0;
}

static void
other_flush (CACHEFILE f __attribute__((__unused__)),
       int UU(fd),
       CACHEKEY k  __attribute__((__unused__)),
       void *v     __attribute__((__unused__)),
	     void** UU(dd),
       void *e     __attribute__((__unused__)),
       PAIR_ATTR s      __attribute__((__unused__)),
       PAIR_ATTR* new_size      __attribute__((__unused__)),
       BOOL w      __attribute__((__unused__)),
       BOOL keep   __attribute__((__unused__)),
       BOOL c      __attribute__((__unused__)),
        BOOL UU(is_clone)
       ) {
}

static void 
pe_est_callback(
    void* UU(ftnode_pv), 
    void* UU(dd),
    long* bytes_freed_estimate, 
    enum partial_eviction_cost *cost, 
    void* UU(write_extraargs)
    )
{
    *bytes_freed_estimate = 1000;
    *cost = PE_EXPENSIVE;
}

static int 
pe_callback (
    void *ftnode_pv, 
    PAIR_ATTR UU(bytes_to_free), 
    PAIR_ATTR* bytes_freed, 
    void* extraargs __attribute__((__unused__))
    ) 
{
    *bytes_freed = make_pair_attr(bytes_to_free.size-1);
    if (verbose) printf("calling pe_callback\n");
    expected_bytes_to_free--;
    int* foo = ftnode_pv;
    int blah = *foo;
    *foo = blah-1;
    return 0;
}

static int 
other_pe_callback (
    void *ftnode_pv __attribute__((__unused__)), 
    PAIR_ATTR bytes_to_free __attribute__((__unused__)), 
    PAIR_ATTR* bytes_freed __attribute__((__unused__)), 
    void* extraargs __attribute__((__unused__))
    ) 
{
    *bytes_freed = bytes_to_free;
    return 0;
}

static void
cachetable_test (void) {
    const int test_limit = 20;
    int r;
    CACHETABLE ct;
    r = toku_create_cachetable(&ct, test_limit, ZERO_LSN, NULL_LOGGER); assert(r == 0);
    char fname1[] = __SRCFILE__ "test1.dat";
    unlink(fname1);
    CACHEFILE f1;
    r = toku_cachetable_openf(&f1, ct, fname1, O_RDWR|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO); assert(r == 0);

    void* v1;
    void* v2;
    long s1, s2;
    flush_may_occur = FALSE;
    for (int i = 0; i < 100000; i++) {
      CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
      wc.flush_callback = flush;
      wc.pe_est_callback = pe_est_callback;
      wc.pe_callback = pe_callback;
      r = toku_cachetable_get_and_pin(f1, make_blocknum(1), 1, &v1, &s1, wc, fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
      r = toku_cachetable_unpin(f1, make_blocknum(1), 1, CACHETABLE_CLEAN, make_pair_attr(4));
    }
    for (int i = 0; i < 8; i++) {
      CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
      wc.flush_callback = flush;
      wc.pe_est_callback = pe_est_callback;
      wc.pe_callback = pe_callback;
      r = toku_cachetable_get_and_pin(f1, make_blocknum(2), 2, &v2, &s2, wc, fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
      r = toku_cachetable_unpin(f1, make_blocknum(2), 2, CACHETABLE_CLEAN, make_pair_attr(4));
    }
    for (int i = 0; i < 4; i++) {
      CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
      wc.flush_callback = flush;
      wc.pe_est_callback = pe_est_callback;
      wc.pe_callback = pe_callback;
      r = toku_cachetable_get_and_pin(f1, make_blocknum(3), 3, &v2, &s2, wc, fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
      r = toku_cachetable_unpin(f1, make_blocknum(3), 3, CACHETABLE_CLEAN, make_pair_attr(4));
    }
    for (int i = 0; i < 2; i++) {
      CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
      wc.flush_callback = flush;
      wc.pe_est_callback = pe_est_callback;
      wc.pe_callback = pe_callback;
      r = toku_cachetable_get_and_pin(f1, make_blocknum(4), 4, &v2, &s2, wc, fetch, def_pf_req_callback, def_pf_callback, TRUE, NULL);
      r = toku_cachetable_unpin(f1, make_blocknum(4), 4, CACHETABLE_CLEAN, make_pair_attr(4));
    }
    flush_may_occur = FALSE;
    expected_bytes_to_free = 4;
    CACHETABLE_WRITE_CALLBACK wc = def_write_callback(NULL);
    wc.flush_callback = other_flush;
    wc.pe_est_callback = pe_est_callback;
    wc.pe_callback = other_pe_callback;
    r = toku_cachetable_put(f1, make_blocknum(5), 5, NULL, make_pair_attr(4), wc);
    flush_may_occur = TRUE;
    r = toku_cachetable_unpin(f1, make_blocknum(5), 5, CACHETABLE_CLEAN, make_pair_attr(8));

    // we are testing that having a wildly different estimate than
    // what actually gets freed is ok
    // in the callbacks, we estimate that 1000 bytes gets freed
    // whereas in reality, only 1 byte will be freed
    // we measure that only 1 byte gets freed (which leaves cachetable
    // oversubscrubed)
    usleep(2*1024*1024);
    assert(expected_bytes_to_free == 3);


    r = toku_cachefile_close(&f1, 0, FALSE, ZERO_LSN); assert(r == 0);
    r = toku_cachetable_close(&ct); assert(r == 0 && ct == 0);
}

int
test_main(int argc, const char *argv[]) {
    default_parse_args(argc, argv);
    cachetable_test();
    return 0;
}
