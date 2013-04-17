/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <db.h>

#include "test.h"



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
    if (kk != k || vv != v) printf("expect key %u got %u - %u %u\n", htonl(k), htonl(kk), htonl(v), htonl(vv));
    assert(kk == k);
    assert(vv == v);

    free(key.data);
    free(val.data);
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
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        db_put(db, k, v);

        expect_db_get(db, k, htonl(0));
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    db_del(db, htonl(n/2));

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(n+i);
        db_put(db, k, v);

        expect_db_get(db, k, htonl(n));
    } 

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n; i++) {
        expect_cursor_get(cursor, htonl(n/2), htonl(n+i));
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

/* insert, close, insert, search */
static void
test_ici_search (int n, int dup_mode) {
    if (verbose) printf("test_ici_search:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test_ici_search.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        db_put(db, k, v);

        expect_db_get(db, k, htonl(0));
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(n+i);
        db_put(db, k, v);

        expect_db_get(db, k, htonl(0));
    } 

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<2*n; i++) {
        expect_cursor_get(cursor, htonl(n/2), htonl(i));
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

/* insert 0, insert 1, close, insert 0, search 0 */
static void
test_i0i1ci0_search (int n, int dup_mode) {
    if (verbose) printf("test_i0i1ci0_search:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test_i0i1ci0.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);
    
    /* insert <0,0> */
    db_put(db, 0, 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(1);
        int v = htonl(i);
        db_put(db, k, v);
        expect_db_get(db, k, htonl(0));
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert <0,1> */
    db_put(db, 0, 1);

    /* verify dup search digs deep into the tree */
    expect_db_get(db, 0, 0);

    r = db->close(db, 0);
    assert(r == 0);
}

/* insert dup keys with data descending from n to 1 */
static void
test_reverse_search (int n, int dup_mode) {
    if (verbose) printf("test_reverse_search:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test_reverse_search.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* seq inserts to build the tree */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(i);
        int v = htonl(i);
        db_put(db, k, v);
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* dup key inserts <n,n>, <n,n-1>, .. <n,1> */
    for (i=0; i<n; i++) {
        int k = htonl(n);
        int v = htonl(n-i);
        db_put(db, k, v);
    } 

    if (dup_mode & DB_DUPSORT)
        expect_db_get(db, htonl(n), htonl(1));
    else if (dup_mode & DB_DUP)
        expect_db_get(db, htonl(n), htonl(n));
    else
        expect_db_get(db, htonl(n), htonl(1));

    r = db->close(db, 0);
    assert(r == 0);
}

int main(int argc, const char *argv[]) {
    int i;

    parse_args(argc, argv);
  
    system("rm -rf " ENVDIR);
    mkdir(ENVDIR, 0777);

    int limit = 1<<13;
    if (verbose > 1)
        limit = 1<<16;

    /* dup search */
    if (IS_TDB) {
	if (verbose) printf("%s:%d:WARNING:tokudb does not support DB_DUP\n", __FILE__, __LINE__);
    } else {
	for (i = 1; i <= limit; i *= 2) {
	    test_ici_search(i, DB_DUP);
	    test_icdi_search(i, DB_DUP);
	    test_i0i1ci0_search(i, DB_DUP);
	}
    }

    /* dupsort search */
    for (i = 1; i <= limit; i *= 2) {
         test_ici_search(i, DB_DUP + DB_DUPSORT);
         test_icdi_search(i, DB_DUP + DB_DUPSORT);
         test_i0i1ci0_search(i, DB_DUP + DB_DUPSORT);
    }

    /* insert data in descending order */
    for (i = 1; i <= limit; i *= 2) {
        test_reverse_search(i, 0);
#ifdef USE_BDB
        test_reverse_search(i, DB_DUP);
#endif
        test_reverse_search(i, DB_DUP + DB_DUPSORT);
    }

    return 0;
}
