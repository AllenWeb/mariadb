/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <portability.h>
#include <db.h>

#include "test.h"

static void
expect_cursor_get (DBC *cursor, int k, int v, int op) {
    int kk, vv;
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&val), op);
    assert(r == 0); 
    assert(key.size == sizeof kk); memcpy(&kk, key.data, key.size); assert(kk == k); free(key.data); 
    assert(val.size == sizeof vv); memcpy(&vv, val.data, val.size); assert(vv == v); free(val.data);
}

static DBC *
new_cursor (DB *db, int k, int v, int op) {
    DBC *cursor;
    int r;
    r = db->cursor(db, 0, &cursor, 0); assert(r == 0);
    expect_cursor_get(cursor, k, v, op);
    return cursor;
}

static int
db_put (DB *db, int k, int v) {
    DBT key, val;
    int r = db->put(db, 0, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
    return r;
}

/* use inserts and cursors to test the brt_nonleaf_expand function
   insert keys 0 and n and set cursors to them
   then insert keys 1 .. n-1.  this should cause leaf splits, new root nodes, nonleaf expands
   and nonleaf splits as the tree grows. 

   the reverse parameter controls where in insertions are made to test the <, =, >
   cases in the brt_nonleaf_expand function */

static void
test_cursor_nonleaf_expand (int n, int reverse) {
    if (verbose) printf("test_cursor_nonleaf_expand:%d %d\n", n, reverse);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test.insert.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0); assert(r == 0);
    r = db->set_pagesize(db, 4096); assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666); assert(r == 0);
    
    r = db_put(db, htonl(0), 0); assert(r == 0);
    DBC *cursor0 = new_cursor(db, htonl(0), 0, DB_FIRST); assert(cursor0);
    r = db_put(db, htonl(n), n); assert(r == 0);
    DBC *cursorn = new_cursor(db, htonl(n), n, DB_LAST); assert(cursorn);

    int i;
    if (reverse) {
        for (i=n-1; i > 0; i--) {
            r = db_put(db, htonl(i), i); assert(r == 0);
        }
    } else {
        for (i=1; i < n; i++) {
            r = db_put(db, htonl(i), i); assert(r == 0);
        } 
    }

    /* make sure the cursors did not move */
    expect_cursor_get(cursor0, htonl(0), 0, DB_CURRENT);
    expect_cursor_get(cursorn, htonl(n), n, DB_CURRENT);

    r = cursor0->c_close(cursor0); assert(r == 0);
    r = cursorn->c_close(cursorn); assert(r == 0);
    r = db->close(db, 0); assert(r == 0);
}

int main(int argc, const char *argv[]) {
    parse_args(argc, argv);

    system("rm -rf " ENVDIR);
    int r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO); assert(r==0);

    int i;
    for (i=1; i<=65536; i *= 2) {
        test_cursor_nonleaf_expand(i, 0);
        test_cursor_nonleaf_expand(i, 1);
    }

    return 0;
}
