/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "$Id$"
#ident "Copyright (c) 2011 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
/* Test an overflow condition on the leaf.  See #632. */


#include "test.h"
#include "includes.h"

int verbose;

static const char fname[]= __SRCFILE__ ".brt";

static TOKUTXN const null_txn = 0;
static DB * const null_db = 0;

static void
test_overflow (void) {
    BRT t;
    CACHETABLE ct;
    u_int32_t nodesize = 1<<20; 
    int r;
    unlink(fname);
    r = toku_brt_create_cachetable(&ct, 0, ZERO_LSN, NULL_LOGGER);         assert(r==0);
    r = toku_open_brt(fname, 1, &t, nodesize, nodesize / 8, TOKU_DEFAULT_COMPRESSION_METHOD, ct, null_txn, toku_builtin_compare_fun); assert(r==0);

    DBT k,v;
    u_int32_t vsize = nodesize/8;
    char buf[vsize];
    memset(buf, 'a', vsize);
    int i;
    for (i=0; i<8; i++) {
	char key[]={(char)('a'+i), 0};
	r = toku_brt_insert(t, toku_fill_dbt(&k, key, 2), toku_fill_dbt(&v,buf,sizeof(buf)), null_txn);
	assert(r==0);
    }
    r = toku_close_brt_nolsn(t, 0);        assert(r==0);
    r = toku_cachetable_close(&ct);     assert(r==0);
}

int
test_main (int argc, const char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (0 == strcmp(arg, "-v") || 0 == strcmp(arg, "--verbose"))
            verbose = 1;
        else if (0 == strcmp(arg, "-q") || 0 == strcmp(arg, "--quiet"))
            verbose = 0;
    }
    test_overflow();
    
    return 0;
}
