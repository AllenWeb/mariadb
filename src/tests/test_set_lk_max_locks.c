/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

/* Test to see if the set_lk_max_locks works. */
/* This is very specific to TokuDB.  It won't work with Berkeley DB. */


#include <db.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>

// ENVDIR is defined in the Makefile

#define EXTRA_LOCK_NEEDED 0 // pre #4472, TokuDB needed some extra locks due to grabbing of directory lock for some operations

static void make_db (int n_locks) {
    DB_ENV *env;
    DB *db;
    DB_TXN *tid, *tid2;
    int r;
    int i;
    u_int32_t actual_n_locks;

    r = system("rm -rf " ENVDIR);
    CKERR(r);
    r = toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);       assert(r == 0);
    r = db_env_create(&env, 0); assert(r == 0);
#if TOKUDB
    r = env->set_redzone(env, 0); assert(r == 0);
#endif
    env->set_errfile(env, 0);
    if (n_locks>0) {
#if !TOKUDB && DB_VERSION_MAJOR >= 5
        // BDB configures lock partitons with the number of processors.  We set the number of lock partitions to 1
        // to override the default and get consistent locking behaviour on many machines.
        r = env->set_lk_partitions(env, 1); CKERR(r);
#endif
	r = env->set_lk_max_locks(env, n_locks+EXTRA_LOCK_NEEDED); CKERR(r);
        /* test the get_lk_max_locks method */
#if TOKUDB
	// BDB cannot handle a NULL passed to get_lk_max_locks
        r = env->get_lk_max_locks(env, 0); 
        assert(r == EINVAL);
#endif
        r = env->get_lk_max_locks(env, &actual_n_locks);
        assert(r == 0 && actual_n_locks == (u_int32_t)n_locks+EXTRA_LOCK_NEEDED);
    }
    else {
	r = env->get_lk_max_locks(env, &actual_n_locks);	
	CKERR(r);
    }
    r = env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r = db_create(&db, env, 0); CKERR(r);
    r = env->txn_begin(env, 0, &tid, 0); assert(r == 0);
    r = db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r = tid->commit(tid, 0);    assert(r == 0);
    
#if !TOKUDB
    u_int32_t pagesize;
    r = db->get_pagesize(db, &pagesize); CKERR(r);
    u_int32_t datasize = pagesize/6;
#else
    u_int32_t datasize = 1;
#endif
    int effective_n_locks = (n_locks<0) ? (int)actual_n_locks-EXTRA_LOCK_NEEDED : n_locks;
    // create even numbered keys 0 2 4 ...  (effective_n_locks*32-2)
    
    r = env->txn_begin(env, 0, &tid, 0);    CKERR(r);
    for (i=0; i<effective_n_locks*16; i++) {
	char hello[30], there[datasize+30];
	DBT key,data;
	snprintf(hello, sizeof(hello), "hello%09d", 2*i);
	snprintf(there, sizeof(there), "there%d%0*d", 2*i, (int)datasize, 2*i); // For BDB this is chosen so that different locks are on different pages
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data  = hello; key.size=strlen(hello)+1;
	data.data = there; data.size=strlen(there)+1;
	if (i%50==49) {
	    r = tid->commit(tid, 0);                CKERR(r);
	    r = env->txn_begin(env, 0, &tid, 0);    CKERR(r);
	}
	r = db->put(db, tid, &key, &data, 0);   CKERR(r);
    }
    r = tid->commit(tid, 0);                CKERR(r);

    // Now using two different transactions have one transaction create keys
    //   1 17 33 ... (1 mod 16)
    // and another do
    //   9 25 41 ... (9 mod 16)

    r = env->txn_begin(env, 0, &tid, 0);    CKERR(r);
    r = env->txn_begin(env, 0, &tid2, 0);   CKERR(r);

    for (i=0; i<effective_n_locks*2; i++) {
	int j;
	for (j=0; j<2; j++) {
	    char hello[30], there[datasize+30];
	    DBT key,data;
	    int num = 16*i+8*j+1;
	    snprintf(hello, sizeof(hello), "hello%09d", num);
	    snprintf(there, sizeof(there), "there%d%*d", num, (int)datasize, num); // For BDB this is chosen so that different locks are on different pages
	    memset(&key, 0, sizeof(key));
	    memset(&data, 0, sizeof(data));
	    //printf("Writing %s in %d\n", hello, j);
	    key.data  = hello; key.size=strlen(hello)+1;
	    data.data = there; data.size=strlen(there)+1;
	    r = db->put(db, j==0 ? tid : tid2, &key, &data, 0);
#if TOKUDB
	    // Lock escalation cannot help here:  We require too many locks because we are alternating between tid and tid2
	    if (i*2+j<effective_n_locks) {
		CKERR(r);
	    } else CKERR2(r, TOKUDB_OUT_OF_LOCKS);
#else
            if (verbose) printf("%d %d %d\n", i, j, r);
#if DB_VERSION_MAJOR >= 5
	    if (i*2+j+1<effective_n_locks) {
#else
	    if (i*2+j+2<effective_n_locks) {
#endif
		if (r!=0) printf("r = %d on i=%d j=%d eff=%d\n", r, i, j, effective_n_locks);
		CKERR(r);
	    } else {
                CKERR2(r, ENOMEM);
            }
#endif
	}
    }
    r = tid->commit(tid2, 0);   assert(r == 0);
    r = tid->commit(tid, 0);    assert(r == 0);
    r = db->close(db, 0);       assert(r == 0);
    r = env->close(env, 0);     assert(r == 0);
}

int
test_main (int argc, char * const argv[]) {
    parse_args(argc, argv);
    make_db(100);
    make_db(1000);
    if (0) {
	make_db(-1);  // Could be used to test default, but default is now too large for this to be a useful test
    }
    return 0;
}
