#ident "$Id$"
// Test the toku_dump_brt() call that is available in the debugger.
#include <stdio.h>
#include "includes.h"
#include "test.h"

static TOKUTXN const null_txn = 0;
static DB * const null_db = 0;

int
test_main(int argc, const char *argv[]) {
    default_parse_args (argc, argv);
    const char *n = __FILE__ "dump.brt";
    int r;
    BRT t;
    CACHETABLE ct;
    FILE *f = fopen("test-dump-brt.out", "w");
    unlink(n);
    assert(f);
    r = toku_brt_create_cachetable(&ct, 0, ZERO_LSN, NULL_LOGGER);   assert(r==0);
    r = toku_open_brt(n, 1, &t, 1<<12, 1<<9, TOKU_DEFAULT_COMPRESSION_METHOD, ct, null_txn, toku_builtin_compare_fun); assert(r==0);
    int i;
    for (i=0; i<10000; i++) {
	char key[100],val[100];
	DBT k,v;
	snprintf(key, 100, "key%d", i);
	snprintf(val, 100, "val%d", i);
	r = toku_brt_insert(t, toku_fill_dbt(&k, key, 1+strlen(key)), toku_fill_dbt(&v, val, 1+strlen(val)), null_txn);
	assert(r==0);
    }
    r = toku_dump_brt(f, t); assert(r==0);
    r = toku_close_brt_nolsn(t, 0); assert(r==0);
    r = toku_cachetable_close(&ct); assert(r==0);
    fclose(f);
    return 0;
}
