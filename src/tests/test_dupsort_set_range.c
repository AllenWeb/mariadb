/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <toku_portability.h>
#include <db.h>

#include "test.h"

#ifndef DB_DELETE_ANY
#define DB_DELETE_ANY 0
#endif
#ifndef DB_YESOVERWRITE
#define DB_YESOVERWRITE 0
#endif

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
    int r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), DB_DELETE_ANY);
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
    free(val.data);
}

static void
expect_cursor_get (DBC *cursor, int k, int v) {
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&val), DB_NEXT);
    assert(r == 0);
    assert(key.size == sizeof k);
    int kk;
    memcpy(&kk, key.data, key.size);
    assert(val.size == sizeof v);
    int vv;
    memcpy(&vv, val.data, val.size);
    if (kk != k || vv != v) printf("expect key %u got %u - %u %u\n", (uint32_t)htonl(k), (uint32_t)htonl(kk), (uint32_t)htonl(v), (uint32_t)htonl(vv));
    assert(kk == k);
    assert(vv == v);

    free(key.data);
    free(val.data);
}

static void
expect_cursor_set_range (DBC *cursor, int k) {
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), DB_SET_RANGE);
    assert(r == 0);
    free(val.data);
}

static void
expect_cursor_get_current (DBC *cursor, int k, int v) {
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&val), DB_CURRENT);
    assert(r == 0);
    int kk, vv;
    assert(key.size == sizeof kk); memcpy(&kk, key.data, key.size); assert(kk == k);
    assert(val.size == sizeof vv); memcpy(&vv, val.data, val.size); assert(vv == v);
    free(key.data); free(val.data);
}


/* insert, close, delete, insert, search */
static void
test_icdi_search (int n, int dup_mode) {
    if (verbose) printf("test_icdi_search:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test_icdi_search.brt";
    int r;

    unlink(fname);

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

    /* insert n duplicates */
    for (i=n-1; i >= 0; i--) {
        int k = htonl(n/2);
        int v = htonl(n+i);
        db_put(db, k, v);

        DBC *cursor;
        r = db->cursor(db, 0, &cursor, 0); assert(r == 0);
        expect_cursor_set_range(cursor, k);
        expect_cursor_get_current(cursor, k, htonl(n+i));
        r = cursor->c_close(cursor); assert(r == 0);
    } 

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0); assert(r == 0);

    for (i=0; i<n; i++) {
        expect_cursor_get(cursor, htonl(n/2), htonl(n+i));
    }

    r = cursor->c_close(cursor); assert(r == 0);

    r = db->close(db, 0); assert(r == 0);
}


int
test_main(int argc, const char *argv[]) {
    int i;

    parse_args(argc, argv);
  
    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    int limit = 1<<13;
    if (verbose > 1)
        limit = 1<<16;

    for (i = 1; i <= limit; i *= 2) {
        test_icdi_search(i, DB_DUP + DB_DUPSORT);
    }
    return 0;
}
