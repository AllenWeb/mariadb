/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <db.h>




static void
db_put (DB *db, int k, int v) {
    DB_TXN * const null_txn = 0;
    DBT key, val;
    int r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), DB_YESOVERWRITE);
    assert(r == 0);
}

static void
db_del (DB *db, int k) {
    DB_TXN * const null_txn = 0;
    DBT key;
    int r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);
}

static void
expect_db_get (DB *db, int k, int v) {
    DB_TXN * const null_txn = 0;
    DBT key, val;
    int r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
    assert(r == 0);
    int vv;
    assert(val.size == sizeof vv);
    memcpy(&vv, val.data, val.size);
    assert(vv == v);
    toku_free(val.data);
}

static void
expect_cursor_set (DBC *cursor, int k) {
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), DB_SET);
    assert(r == 0);
    toku_free(val.data);
}

static void
expect_cursor_get_current (DBC *cursor, int k, int v) {
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&val), DB_CURRENT);
    assert(r == 0);
    int kk, vv;
    assert(key.size == sizeof kk); memcpy(&kk, key.data, key.size); assert(kk == k);
    assert(val.size == sizeof vv); memcpy(&vv, val.data, val.size); assert(vv == v);
    toku_free(key.data); toku_free(val.data);
}


static void
test_dupsort_get (int n, int dup_mode) {
    if (verbose) printf("test_dupsort_get:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test.dupsort.get.brt";
    int r;

    r = system("rm -rf " ENVDIR); CKERR(r);
    r = toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);

    /* create the dup database file */
    r = db_create(&db, null_env, 0); assert(r == 0);
    r = db->set_flags(db, dup_mode); assert(r == 0);
    r = db->set_pagesize(db, 4096); assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666); assert(r == 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        db_put(db, k, v);

        expect_db_get(db, k, htonl(0));
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0); assert(r == 0);
    r = db_create(&db, null_env, 0); assert(r == 0);
    r = db->set_flags(db, dup_mode); assert(r == 0);
    r = db->set_pagesize(db, 4096); assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666); assert(r == 0);

    db_del(db, htonl(n/2));

    for (i=n-1; i>=0; i--)
        db_put(db, htonl(n/2), htonl(i));

    expect_db_get(db, htonl(n/2), htonl(0));

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0); assert(r == 0);
    expect_cursor_set(cursor, htonl(n/2));
    expect_cursor_get_current(cursor, htonl(n/2), 0);
    r = cursor->c_del(cursor, 0); assert(r == 0);
    if (n > 1) expect_db_get(db, htonl(n/2), htonl(1));

    r = cursor->c_close(cursor); assert(r == 0);

    r = db->close(db, 0); assert(r == 0);
}


int
test_main(int argc, char *argv[]) {
    int i;

    parse_args(argc, argv);
  
    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    int limit = 1<<13;
    if (verbose > 1)
        limit = 1<<16;

    for (i=1; i <= limit; i *= 2) {
        test_dupsort_get(i, DB_DUP + DB_DUPSORT);
    }
    return 0;
}
