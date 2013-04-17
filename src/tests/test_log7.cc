/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"


/* Test to see if we can do logging and recovery. */
/* This is very specific to TokuDB.  It won't work with Berkeley DB. */
/* This test_log7 is like test_log5 except maxcount is larger. */


#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>

// ENVDIR is defined in the Makefile

struct in_db;
struct in_db {
    long int r;
    int i;
    struct in_db *next;
} *items=0;

static void make_db (BOOL close_env) {
    DB_ENV *env;
    DB *db;
    DB_TXN *tid;
    int r;
    int i;

    int maxcount = 100000;

    r = system("rm -rf " ENVDIR);
    CKERR(r);
    r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
#ifdef TOKUDB
    r=env->set_lk_max_locks(env, 2*maxcount); CKERR(r);
#endif
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=tid->commit(tid, 0);    assert(r==0);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    
    for (i=0; i<maxcount; i++) {
	char hello[30], there[30];
	DBT key,data;
	struct in_db *XMALLOC(newitem);
	newitem->r = random();
	newitem->i = i;
	newitem->next = items;
	items = newitem;
	snprintf(hello, sizeof(hello), "hello%ld.%d", newitem->r, newitem->i);
	snprintf(there, sizeof(hello), "there%d", i);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data  = hello; key.size=strlen(hello)+1;
	data.data = there; data.size=strlen(there)+1;
	r=db->put(db, tid, &key, &data, 0);  assert(r==0);
#ifndef TOKUDB
	// BDB cannot handle this huge transaction even with a lot of locks.
	if (i%1000==599) {
	    r=tid->commit(tid, 0);    assert(r==0);
	    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	}
#endif
    }
    r=tid->commit(tid, 0);    assert(r==0);
    r=db->close(db, 0);       assert(r==0);
    if (close_env) {
        r=env->close(env, 0);     assert(r==0);
    }
    while (items) {
	struct in_db *next=items->next;
	toku_free(items);
	items=next;
    }
}

int
test_main (int argc, char *const argv[]) {
    BOOL close_env = TRUE;
    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "--no-shutdown") == 0)
            close_env = FALSE;
    }
    make_db(close_env);
    return 0;
}
