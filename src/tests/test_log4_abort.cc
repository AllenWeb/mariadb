/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#include "test.h"


/* Like test_log4, except abort */


#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>

// TOKU_TEST_FILENAME is defined in the Makefile

#define N 20000
long random_nums[N];

static void make_db (void) {
    DB_ENV *env;
    DB *db;
    DB_TXN *tid;
    int r;
    int i;

    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    r=toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    
    r=env->open(env, TOKU_TEST_FILENAME, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=tid->commit(tid, 0);    assert(r==0);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    
    for (i=0; i<N; i++) {
	char hello[30], there[30];
	DBT key,data;
	snprintf(hello, sizeof(hello), "hello%ld.%d", (random_nums[i]=random()), i);
	snprintf(there, sizeof(hello), "there%d", i);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data  = hello; key.size=strlen(hello)+1;
	data.data = there; data.size=strlen(there)+1;
	r=db->put(db, tid, &key, &data, 0);  CKERR(r);
    }
    r=tid->abort(tid);    assert(r==0);
    for (i=0; i<N; i++) {
	char hello[30];
	DBT key,data;
	snprintf(hello, sizeof(hello), "hello%ld.%d", (random_nums[i]=random()), i);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data  = hello; key.size=strlen(hello)+1;
	r=db->get(db, 0, &key, &data, 0);
	assert(r==DB_NOTFOUND);
    }
    r=db->close(db, 0);       assert(r==0);
    r=env->close(env, 0);     assert(r==0);
}

int
test_main (int argc __attribute__((__unused__)), char *const argv[] __attribute__((__unused__))) {
    make_db();
    return 0;
}
