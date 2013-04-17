/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <memory.h>
#include <db.h>


static void
db_put (DB *db, int k, int v) {
    DB_TXN * const null_txn = 0;
    DBT key, val;
    int r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
    assert(r == 0);
}

static void
test_cursor_current (void) {
    if (verbose) printf("test_cursor_current\n");

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test.cursor.current.brt";
    int r;

    unlink(fname);

    r = db_create(&db, null_env, 0); assert(r == 0);
    db->set_errfile(db,0); // Turn off those annoying errors
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666); assert(r == 0);

    int k = 42, v = 42000;
    db_put(db, k, v);
    db_put(db, 43, 2000);
 
    DBC *cursor;

    r = db->cursor(db, null_txn, &cursor, 0); assert(r == 0);

    DBT key, data; int kk, vv;

    r = cursor->c_del(cursor, 0);
    assert(r == EINVAL);

    r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&data), DB_CURRENT);
    assert(r == EINVAL);

    r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&data), DB_FIRST);
    assert(r == 0);
    assert(key.size == sizeof kk);
    memcpy(&kk, key.data, sizeof kk);
    assert(kk == k);
    assert(data.size == sizeof vv);
    memcpy(&vv, data.data, data.size);
    assert(vv == v);
    toku_free(key.data); toku_free(data.data);

    r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&data), DB_CURRENT);
    assert(r == 0);
    assert(key.size == sizeof kk);
    memcpy(&kk, key.data, sizeof kk);
    assert(kk == k);
    assert(data.size == sizeof vv);
    memcpy(&vv, data.data, data.size);
    assert(vv == v);
    toku_free(key.data); toku_free(data.data);

    r = cursor->c_del(cursor, 0); 
    assert(r == 0);

    r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&data), DB_CURRENT);
    assert(r == DB_KEYEMPTY);

    r = cursor->c_del(cursor, 0); 
    assert(r == DB_KEYEMPTY);

    r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&data), DB_CURRENT);
    assert(r == DB_KEYEMPTY);

    r = cursor->c_close(cursor); assert(r == 0);

    r = db->close(db, 0); assert(r == 0);
}

static void
db_get (DB *db, int k, int UU(v), int expectr) {
    DBT key, val;
    int r = db->get(db, 0, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
    assert(r == expectr);
}

static void
test_reopen (void) {
    if (verbose) printf("test_reopen\n");

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test.cursor.current.brt";
    int r;

    r = db_create(&db, null_env, 0); assert(r == 0);
    db->set_errfile(db,0); // Turn off those annoying errors
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666); assert(r == 0);

    db_get(db, 1, 1, DB_NOTFOUND);

    r = db->close(db, 0); assert(r == 0);
}

int
test_main(int argc, const char *argv[]) {
    parse_args(argc, argv);
  
    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    test_cursor_current();
    test_reopen();

    return 0;
}
