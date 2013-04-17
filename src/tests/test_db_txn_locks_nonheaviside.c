/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#include <toku_portability.h>
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <memory.h>
#include <toku_portability.h>
#include <db.h>

#include <errno.h>
#include <sys/stat.h>

#include "test.h"

// ENVDIR is defined in the Makefile

static DB *db;
static DB_TXN* txns[(int)256];
static DB_ENV* dbenv;
static DBC*    cursors[(int)256];

static void
put(BOOL success, char txn, int _key, int _data) {
    assert(txns[(int)txn]);

    int r;
    DBT key;
    DBT data;
    
    r = db->put(db, txns[(int)txn],
                    dbt_init(&key, &_key, sizeof(int)),
                    dbt_init(&data, &_data, sizeof(int)),
                    0);

    if (success)    CKERR(r);
    else            CKERR2s(r, DB_LOCK_DEADLOCK, DB_LOCK_NOTGRANTED);
}

static void
cget(BOOL success, BOOL find, char txn, int _key, int _data, 
     int _key_expect, int _data_expect, u_int32_t flags) {
    assert(txns[(int)txn] && cursors[(int)txn]);

    int r;
    DBT key;
    DBT data;
    
    r = cursors[(int)txn]->c_get(cursors[(int)txn],
                                 dbt_init(&key,  &_key,  sizeof(int)),
                                 dbt_init(&data, &_data, sizeof(int)),
                                 flags);
    if (success) {
        if (find) {
            CKERR(r);
            assert(*(int *)key.data  == _key_expect);
            assert(*(int *)data.data == _data_expect);
        }
        else        CKERR2(r,  DB_NOTFOUND);
    }
    else            CKERR2s(r, DB_LOCK_DEADLOCK, DB_LOCK_NOTGRANTED);
}

static void
dbdel (BOOL success, BOOL find, char txn, int _key) {
    int r;
    DBT key;

    /* If DB_DELETE_ANY changes to 0, then find is meaningful and 
       has to be fixed in test_dbdel*/
    r = db->del(db, txns[(int)txn], dbt_init(&key,&_key, sizeof(int)), 
                DB_DELETE_ANY);
    if (success) {
        if (find) CKERR(r);
        else      CKERR2( r, DB_NOTFOUND);
    }
    else          CKERR2s(r, DB_LOCK_DEADLOCK, DB_LOCK_NOTGRANTED);
}

static void
init_txn (char name) {
    int r;
    assert(!txns[(int)name]);
    r = dbenv->txn_begin(dbenv, NULL, &txns[(int)name], DB_TXN_NOWAIT);
        CKERR(r);
    assert(txns[(int)name]);
}

static void
init_dbc (char name) {
    int r;

    assert(!cursors[(int)name] && txns[(int)name]);
    r = db->cursor(db, txns[(int)name], &cursors[(int)name], 0);
        CKERR(r);
    assert(cursors[(int)name]);
}

static void
commit_txn (char name) {
    int r;
    assert(txns[(int)name] && !cursors[(int)name]);

    r = txns[(int)name]->commit(txns[(int)name], 0);
        CKERR(r);
    txns[(int)name] = NULL;
}

static void
abort_txn (char name) {
    int r;
    assert(txns[(int)name] && !cursors[(int)name]);

    r = txns[(int)name]->abort(txns[(int)name]);
        CKERR(r);
    txns[(int)name] = NULL;
}

static void
close_dbc (char name) {
    int r;

    assert(cursors[(int)name]);
    r = cursors[(int)name]->c_close(cursors[(int)name]);
        CKERR(r);
    cursors[(int)name] = NULL;
}

static void
early_commit (char name) {
    assert(cursors[(int)name] && txns[(int)name]);
    close_dbc(name);
    commit_txn(name);
}

static void
early_abort (char name) {
    assert(cursors[(int)name] && txns[(int)name]);
    close_dbc(name);
    abort_txn(name);
}

static void
setup_dbs (void) {
    int r;

    r = system("rm -rf " ENVDIR);
    CKERR(r);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);
    dbenv   = NULL;
    db      = NULL;
    /* Open/create primary */
    r = db_env_create(&dbenv, 0);
        CKERR(r);
#ifdef TOKUDB
    r = dbenv->set_default_bt_compare(dbenv, int_dbt_cmp);
        CKERR(r);
#endif
    u_int32_t env_txn_flags  = DB_INIT_TXN | DB_INIT_LOCK;
    u_int32_t env_open_flags = DB_CREATE | DB_PRIVATE | DB_INIT_MPOOL;
	r = dbenv->open(dbenv, ENVDIR, env_open_flags | env_txn_flags, 0600);
        CKERR(r);
    
    r = db_create(&db, dbenv, 0);
        CKERR(r);
#ifndef TOKUDB
    r = db->set_bt_compare( db, int_dbt_cmp);
    CKERR(r);
#endif

    char a;
    for (a = 'a'; a <= 'z'; a++) init_txn(a);
    init_txn('\0');
    r = db->open(db, txns[(int)'\0'], "foobar.db", NULL, DB_BTREE, DB_CREATE, 0600);
        CKERR(r);
    commit_txn('\0');
    for (a = 'a'; a <= 'z'; a++) init_dbc(a);
}

static void
close_dbs(void) {
    char a;
    for (a = 'a'; a <= 'z'; a++) {
        if (cursors[(int)a]) close_dbc(a);
        if (txns[(int)a])    commit_txn(a);
    }

    int r;
    r = db->close(db, 0);
        CKERR(r);
    db      = NULL;
    r = dbenv->close(dbenv, 0);
        CKERR(r);
    dbenv   = NULL;
}


static __attribute__((__unused__))
void
test_abort (void) {
    /* ********************************************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    early_abort('a');
    cget(TRUE, FALSE, 'b', 1, 1, 0, 0, DB_SET);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, DB_SET);
    cget(TRUE, FALSE, 'b', 1, 1, 0, 0, DB_SET);
    put(FALSE, 'a', 1, 1);
    early_commit('b');
    put(TRUE, 'a', 1, 1);
    cget(TRUE, TRUE, 'a', 1, 1, 1, 1, DB_SET);
    cget(TRUE, FALSE, 'a', 2, 1, 1, 1, DB_SET);
    cget(FALSE, TRUE, 'c', 1, 1, 0, 0, DB_SET);
    early_abort('a');
    cget(TRUE, FALSE, 'c', 1, 1, 0, 0, DB_SET);
    close_dbs();
    /* ********************************************************************** */
}

static void
test_both (u_int32_t db_flags) {
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, db_flags);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, db_flags);
    cget(TRUE, FALSE, 'a', 2, 1, 0, 0, db_flags);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, db_flags);
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, db_flags);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, db_flags);
    cget(TRUE, FALSE, 'b', 2, 1, 0, 0, db_flags);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, db_flags);
    cget(TRUE, FALSE, 'b', 1, 1, 0, 0, db_flags);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 1, 1, 0, 0, db_flags);
    cget(TRUE, FALSE, 'b', 1, 1, 0, 0, db_flags);
    put(FALSE, 'a', 1, 1);
    early_commit('b');
    put(TRUE, 'a', 1, 1);
    cget(TRUE, TRUE, 'a', 1, 1, 1, 1, db_flags);
    cget(TRUE, FALSE, 'a', 2, 1, 0, 0, db_flags);
    cget(FALSE, TRUE, 'c', 1, 1, 0, 0, db_flags);
    early_commit('a');
    cget(TRUE, TRUE, 'c', 1, 1, 1, 1, db_flags);
    close_dbs();
}


static void
test_last (void) {
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 0, 0, 0, 0, DB_LAST);
    put(FALSE, 'b', 2, 1);
    put(TRUE, 'a', 2, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 2, 1, DB_LAST);
    early_commit('a');
    put(TRUE, 'b', 2, 1);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, DB_LAST);
    put(FALSE, 'b', 2, 1);
    put(TRUE, 'b', -1, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, DB_LAST);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    put(TRUE, 'a', 3, 1);
    put(TRUE, 'a', 6, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 6, 1, DB_LAST);
    put(TRUE, 'b', 2, 1);
    put(TRUE, 'b', 4, 1);
    put(FALSE, 'b', 7, 1);
    put(TRUE, 'b', -1, 1);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, DB_LAST);
    put(FALSE, 'b', 1, 0);
    close_dbs();
}

static void
test_first (void) {
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', 0, 0, 0, 0, DB_FIRST);
    put(FALSE, 'b', 2, 1);
    put(TRUE, 'a', 2, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 2, 1, DB_FIRST);
    early_commit('a');
    put(TRUE, 'b', 2, 1);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, DB_FIRST);
    put(TRUE, 'b', 2, 1);
    put(FALSE, 'b', -1, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, DB_FIRST);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    put(TRUE, 'a', 3, 1);
    put(TRUE, 'a', 6, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, DB_FIRST);
    put(TRUE, 'b', 2, 1);
    put(TRUE, 'b', 4, 1);
    put(TRUE, 'b', 7, 1);
    put(FALSE, 'b', -1, 1);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, DB_FIRST);
    put(FALSE, 'b', 1, 2);
    close_dbs();
}

static void
test_set_range (u_int32_t flag, int i) {
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', i*1, i*1, 0, 0, flag);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', i*1, i*1, 0, 0, flag);
    cget(TRUE, FALSE, 'a', i*2, i*1, 0, 0, flag);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', i*1, i*1, 0, 0, flag);
    cget(TRUE, FALSE, 'a', i*1, i*1, 0, 0, flag);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', i*1, i*1, 0, 0, flag);
    cget(TRUE, FALSE, 'b', i*2, i*1, 0, 0, flag);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', i*1, i*1, 0, 0, flag);
    cget(TRUE, FALSE, 'b', i*1, i*1, 0, 0, flag);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    cget(TRUE, FALSE, 'a', i*1, i*1, 0, 0, flag);
    cget(TRUE, FALSE, 'b', i*5, i*5, 0, 0, flag);
    put(FALSE, 'a', i*7, i*6);
    put(FALSE, 'a', i*5, i*5);
    put(TRUE,  'a', i*4, i*4);
    put(TRUE,  'b', -i*1, i*4);
    put(FALSE,  'b', i*2, i*4);
    put(FALSE, 'a', i*5, i*4);
    early_commit('b');
    put(TRUE, 'a', i*7, i*6);
    put(TRUE, 'a', i*5, i*5);
    put(TRUE,  'a', i*4, i*4);
    put(TRUE, 'a', i*5, i*4);
    cget(TRUE, TRUE, 'a', i*1, i*1, i*4, i*4, flag);
    cget(TRUE, TRUE, 'a', i*2, i*1, i*4, i*4, flag);
    cget(FALSE, TRUE, 'c', i*6, i*6, i*7, i*6, flag);
    early_commit('a');
    cget(TRUE, TRUE, 'c', i*6, i*6, i*7, i*6, flag);
    close_dbs();
}

static void
test_next (u_int32_t next_type) {
    /* ********************************************************************** */
    setup_dbs();
    put(TRUE,  'a', 2, 1);
    put(TRUE,  'a', 5, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 2, 1, next_type);
    put(FALSE, 'b', 2, 1);
    put(TRUE,  'b', 4, 1);
    put(FALSE, 'b', -1, 1);
    cget(FALSE, TRUE, 'a', 0, 0, 4, 1, next_type);
    early_commit('b');
    cget(TRUE,  TRUE, 'a', 2, 1, 2, 1, DB_SET);
    cget(TRUE,  TRUE, 'a', 0, 0, 4, 1, next_type);
    cget(TRUE,  TRUE, 'a', 0, 0, 5, 1, next_type);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    put(TRUE, 'a', 3, 1);
    put(TRUE, 'a', 6, 1);
    cget(TRUE, TRUE, 'a', 0, 0, 1, 1, next_type);
    cget(TRUE, TRUE, 'a', 0, 0, 3, 1, next_type);
    put(FALSE, 'b', 2, 1);
    put(TRUE,  'b', 4, 1);
    put(TRUE,  'b', 7, 1);
    put(FALSE, 'b', -1, 1);
    close_dbs();
}

static void
test_prev (u_int32_t next_type) {
    /* ********************************************************************** */
    setup_dbs();
    put(TRUE,  'a', -2, -1);
    put(TRUE,  'a', -5, -1);
    cget(TRUE, TRUE, 'a', 0, 0, -2, -1, next_type);
    put(FALSE, 'b', -2, -1);
    put(TRUE,  'b', -4, -1);
    put(FALSE, 'b', 1, -1);
    cget(FALSE, TRUE, 'a', 0, 0, -4, -1, next_type);
    early_commit('b');
    cget(TRUE,  TRUE, 'a', -2, -1, -2, -1, DB_SET);
    cget(TRUE,  TRUE, 'a', 0, 0, -4, -1, next_type);
    cget(TRUE,  TRUE, 'a', 0, 0, -5, -1, next_type);
    close_dbs();
    /* ****************************************** */
    setup_dbs();
    put(TRUE, 'a', -1, -1);
    put(TRUE, 'a', -3, -1);
    put(TRUE, 'a', -6, -1);
    cget(TRUE, TRUE, 'a', 0, 0, -1, -1, next_type);
    cget(TRUE, TRUE, 'a', 0, 0, -3, -1, next_type);
    put(FALSE, 'b', -2, -1);
    put(TRUE,  'b', -4, -1);
    put(TRUE,  'b', -7, -1);
    put(FALSE, 'b', 1, -1);
    close_dbs();
}

static void
test_dbdel (void) {
    /* If DB_DELETE_ANY changes to 0, then find is meaningful and 
       has to be fixed in test_dbdel*/
    /* ********************************************************************** */
    setup_dbs();
    put(TRUE, 'c', 1, 1);
    early_commit('c');
    dbdel(TRUE, TRUE, 'a', 1);
    cget(FALSE, TRUE, 'b', 1, 1, 1, 1, DB_SET);
    cget(FALSE, TRUE, 'b', 1, 4, 1, 4, DB_SET);
    cget(FALSE, TRUE, 'b', 1, 0, 1, 4, DB_SET);
    cget(TRUE, FALSE, 'b', 0, 0, 0, 0, DB_SET);
    cget(TRUE, FALSE, 'b', 2, 10, 2, 10, DB_SET);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    dbdel(TRUE, TRUE, 'a', 1);
    cget(FALSE, TRUE, 'b', 1, 1, 1, 1, DB_SET);
    cget(FALSE, TRUE, 'b', 1, 4, 1, 4, DB_SET);
    cget(FALSE, TRUE, 'b', 1, 0, 1, 4, DB_SET);
    cget(TRUE, FALSE, 'b', 0, 0, 0, 0, DB_SET);
    cget(TRUE, FALSE, 'b', 2, 10, 2, 10, DB_SET);
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    put(TRUE, 'c', 1, 1);
    early_commit('c');
    cget(TRUE,  TRUE, 'b', 1, 1, 1, 1, DB_SET);
    dbdel(FALSE, TRUE, 'a', 1);
    dbdel(TRUE, TRUE, 'a', 2);
    dbdel(TRUE, TRUE, 'a', 0);
    close_dbs();
}

static void
test_current (void) {
    /* ********************************************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    early_commit('a');
    cget(TRUE,  TRUE, 'b', 1, 1, 1, 1, DB_SET);
    cget(TRUE,  TRUE, 'b', 1, 1, 1, 1, DB_CURRENT);
    close_dbs();
}

struct dbt_pair {
    DBT key;
    DBT val;
};

struct int_pair {
    int key;
    int val;
};

int got_r_h;

static __attribute__((__unused__))
void
ignore (void *ignore __attribute__((__unused__))) {
}
#define TOKU_IGNORE(x) ignore((void*)x)

static void
test (void) {
    /* ********************************************************************** */
    setup_dbs();
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    early_abort('a');
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    early_commit('a');
    close_dbs();
    /* ********************************************************************** */
    setup_dbs();
    put(TRUE, 'a', 1, 1);
    close_dbs();
    /* ********************************************************************** */
    test_both( DB_SET);
    /* ********************************************************************** */
    test_first();
    /* ********************************************************************** */
    test_last();
    /* ********************************************************************** */
    test_set_range( DB_SET_RANGE, 1);
#ifdef DB_SET_RANGE_REVERSE
    test_set_range( DB_SET_RANGE_REVERSE, -1);
#endif
    /* ********************************************************************** */
    test_next( DB_NEXT);
    test_next( DB_NEXT_NODUP);
    /* ********************************************************************** */
    test_prev( DB_PREV);
    test_prev( DB_PREV_NODUP);
    /* ********************************************************************** */
    test_dbdel();
    /* ********************************************************************** */
    test_current();
    /* ********************************************************************** */
}


int
test_main(int argc, char *const argv[]) {
    parse_args(argc, argv);
    if (!IS_TDB) {
	if (verbose) {
	    printf("Warning: " __FILE__" does not work in BDB.\n");
	}
    } else {
	test();
	/*
	  test_abort(0);
	  test_abort(DB_DUP | DB_DUPSORT);
	*/
    }
    return 0;
}
