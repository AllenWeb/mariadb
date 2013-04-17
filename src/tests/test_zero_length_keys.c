/* -*- mode: C; c-basic-offset: 4 -*- */
#include <toku_portability.h>
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <toku_portability.h>
#include <db.h>

#include "test.h"
#ifndef ENVDIR
#define ENVDIR "./test.dir"
#endif

static void
walk (DB *db) {
    int r;
    DB_TXN * const null_txn = 0;

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0); assert(r == 0);

    DBT key; memset(&key, 0, sizeof key); key.flags = DB_DBT_REALLOC;
    DBT val; memset(&val, 0, sizeof val); val.flags = DB_DBT_REALLOC;
    int i;
    for (i=0; ; i++) {
        r = cursor->c_get(cursor, &key, &val, DB_NEXT);
        if (r != 0) 
            break;
        if (verbose) printf("%d %u %u\n", i, key.size, val.size);
        if (i == 0) assert(key.size == 0);
    }
    assert(i != 0);
    r = cursor->c_close(cursor); assert(r == 0);

    if (key.data) toku_free(key.data);
    if (val.data) toku_free(val.data);
}

static void
test_insert_zero_length (int n, int dup_mode, const char *dbname) {
    if (verbose) printf("test_insert_zero_length:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    int r;

    char fname[strlen(ENVDIR) + strlen("/") + strlen(dbname) + 1];
    sprintf(fname, "%s/%s", ENVDIR, dbname);

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0); assert(r == 0);
    r = db->set_flags(db, dup_mode); assert(r == 0);
    r = db->set_pagesize(db, 4096); assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666); assert(r == 0);

    int i;
    for (i=0; i<n; i++) {
        char k[n]; memset(k, i, n);
        char v[n]; memset(v, i, n);
        DBT key;
        DBT val;
        r = db->put(db, null_txn, dbt_init(&key, &k, i), dbt_init(&val, &v, i), 0);
        if (r != 0) {
            if (verbose) printf("db->put %d %d = %d\n", n, n, r);
            assert(r == 0);
        }
        if (i == 0) {
            dbt_init(&key, &k, i);
            memset(&val, 0, sizeof val);
            r = db->get(db, null_txn, &key, &val, 0);
            assert(r == 0 && val.data == 0 && val.size == 0);

            r = db->get(db, null_txn, &key, dbt_init_malloc(&val), 0);
            assert(r == 0 && val.data != 0 && val.size == 0);
            toku_free(val.data);

            memset(&key, 0, sizeof key);
            memset(&val, 0, sizeof val);
            r = db->get(db, null_txn, &key, &val, 0);
            assert(r == 0 && val.data == 0 && val.size == 0);

            r = db->get(db, null_txn, &key, dbt_init_malloc(&val), 0);
            assert(r == 0 && val.data != 0 && val.size == 0);
            toku_free(val.data);
        }
    }

    walk(db);

    r = db->close(db, 0); assert(r == 0);
}

static void
test_insert_zero_length_keys (int n, int dup_mode, const char *dbname) {
    if (verbose) printf("test_insert_zero_length_keys:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    int r;

    char fname[strlen(ENVDIR) + strlen("/") + strlen(dbname) + 1];
    sprintf(fname, "%s/%s", ENVDIR, dbname);

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0); assert(r == 0);
    r = db->set_flags(db, dup_mode); assert(r == 0);
    r = db->set_pagesize(db, 4096); assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666); assert(r == 0);

    int i;
    for (i=0; i<n; i++) {
        char k[n]; memset(k, i, n);
        char v[n]; memset(v, i, n);
        DBT key;
        DBT val;
        r = db->put(db, null_txn, dbt_init(&key, &k, 0), dbt_init(&val, &v, i), DB_YESOVERWRITE);
        if (r != 0) {
            if (verbose) printf("db->put %d %d = %d\n", n, n, r);
            assert(r == 0);
        }
    }

    walk(db);

    r = db->close(db, 0); assert(r == 0);
}

int
test_main(int argc, const char *argv[]) {

    parse_args(argc, argv);
  
#define TFILE __FILE__ ".tktrace"
    unlink(TFILE);
    SET_TRACE_FILE(TFILE);

    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    test_insert_zero_length(32, 0, "test0");
    test_insert_zero_length_keys(32, 0, "test0keys");
    test_insert_zero_length_keys(32, DB_DUP+DB_DUPSORT, "test0keys_dupsort");

    CLOSE_TRACE_FILE();

    return 0;
}
