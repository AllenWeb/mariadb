/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include "includes.h"
#include "test.h"

static const char fname[]= __FILE__ ".brt";

static TOKUTXN const null_txn = 0;
static DB * const null_db = 0;

static void test3 (int nodesize, int count, int memcheck) {
    BRT t;
    int r;
    struct timeval t0,t1;
    int i;
    CACHETABLE ct;
    toku_memory_check=memcheck;
    toku_memory_check_all_free();
    r = toku_brt_create_cachetable(&ct, 0, ZERO_LSN, NULL_LOGGER); assert(r==0);
    gettimeofday(&t0, 0);
    unlink(fname);
    r = toku_open_brt(fname, 1, &t, nodesize, ct, null_txn, toku_default_compare_fun, null_db);
    assert(r==0);
    for (i=0; i<count; i++) {
	char key[100],val[100];
	DBT k,v;
	snprintf(key,100,"hello%d",i);
	snprintf(val,100,"there%d",i);
	toku_brt_insert(t, toku_fill_dbt(&k, key, 1+strlen(key)), toku_fill_dbt(&v, val, 1+strlen(val)), null_txn);
    }
    r = toku_verify_brt(t); assert(r==0);
    r = toku_close_brt(t, 0, 0);        assert(r==0);
    r = toku_cachetable_close(&ct);     assert(r==0);
    toku_memory_check_all_free();
    gettimeofday(&t1, 0);
    {
	double tdiff = (t1.tv_sec-t0.tv_sec)+1e-6*(t1.tv_usec-t0.tv_usec);
	if (verbose) printf("serial insertions: blocksize=%d %d insertions in %.3f seconds, %.2f insertions/second\n", nodesize, count, tdiff, count/tdiff);
    }
}

static void brt_blackbox_test (void) {
    if (verbose) printf("test3 slow\n");
    toku_memory_check=0;
    test3(2048, 1<<15, 1);
    if (verbose) printf("test3 fast\n");

    //if (verbose) toku_pma_show_stats();

    test3(1<<15, 1024, 1);
    if (verbose) printf("test3 fast\n");

    test3(1<<18, 1<<20, 0);

    toku_memory_check = 1;

//    test3(1<<19, 1<<20, 0);

//    test3(1<<20, 1<<20, 0);

//    test3(1<<20, 1<<21, 0);

//    test3(1<<20, 1<<22, 0);

}

int
test_main (int argc , const char *argv[]) {
    default_parse_args(argc, argv);

    brt_blackbox_test();
    toku_malloc_cleanup();
    if (verbose) printf("test ok\n");
    return 0;
}
