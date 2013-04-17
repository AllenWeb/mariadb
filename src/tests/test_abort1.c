/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

/* Simple test of logging.  Can I start a TokuDB with logging enabled? */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <db.h>
#include <string.h>
#include <stdio.h>

#include "test.h"

// ENVDIR is defined in the Makefile

static void
test_db_open_aborts (void) {
    DB_ENV *env;
    DB *db;

    int r;
    system("rm -rf " ENVDIR);
    r=mkdir(ENVDIR, 0777);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_PRIVATE|DB_CREATE, 0777); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);

    {
	DB_TXN *tid;
	r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, 0777); CKERR(r);
	{
	    DBT key,data;
	    memset(&key, 0, sizeof(key));
	    memset(&data, 0, sizeof(data));
	    key.data="hello";
	    key.size=6;
	    data.data="there";
	    data.size=6;
	    r=db->put(db, tid, &key, &data, 0);
	    CKERR(r);
	}
	r=tid->abort(tid);        assert(r==0);
    }
    {
	struct stat buf;
	r=stat(ENVDIR "/foo.db", &buf);
	assert(r!=0);
	assert(errno==ENOENT);
    }

    r=db->close(db, 0);       assert(r==0);
    r=env->close(env, 0);     assert(r==0);
}

// Do two transactions, one commits, and one aborts.  Do them concurrently.
static void
test_db_put_aborts (void) {
    DB_ENV *env;
    DB *db;

    int r;
    system("rm -rf " ENVDIR);
    r=mkdir(ENVDIR, 0777);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_PRIVATE|DB_CREATE, 0777); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);

    {
	DB_TXN *tid;
	r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, 0777); CKERR(r);
	r=tid->commit(tid,0);        assert(r==0);
    }
    {
	DB_TXN *tid;
	DB_TXN *tid2;
	r=env->txn_begin(env, 0, &tid, 0);  assert(r==0);
	r=env->txn_begin(env, 0, &tid2, 0); assert(r==0);
	{
	    DBT key,data;
	    memset(&key, 0, sizeof(key));
	    memset(&data, 0, sizeof(data));
	    key.data="hello";
	    key.size=6;
	    data.data="there";
	    data.size=6;
	    r=db->put(db, tid, &key, &data, 0);
	    CKERR(r);
	}
	{
	    DBT key,data;
	    memset(&key, 0, sizeof(key));
	    memset(&data, 0, sizeof(data));
	    key.data="bye";
	    key.size=4;
	    data.data="now";
	    data.size=4;
	    r=db->put(db, tid2, &key, &data, 0);
	    CKERR(r);
	}
	//printf("%s:%d aborting\n", __FILE__, __LINE__);
	r=tid->abort(tid);        assert(r==0);
	//printf("%s:%d committing\n", __FILE__, __LINE__);
	r=tid2->commit(tid2,0);   assert(r==0);
    }
    // The database should exist
    {
	struct stat buf;
	r=stat(ENVDIR "/foo.db", &buf);
	assert(r==0);
    }
    // But the item should not be in it.
    if (1)
    {
	DB_TXN *tid;
	r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	{
	    DBT key,data;
	    memset(&key, 0, sizeof(key));
	    memset(&data, 0, sizeof(data));
	    key.data="hello";
	    key.size=6;
	    r=db->get(db, tid, &key, &data, 0);
	    assert(r!=0);
	    assert(r==DB_NOTFOUND);
	}	    
	{
	    DBT key,data;
	    memset(&key, 0, sizeof(key));
	    memset(&data, 0, sizeof(data));
	    key.data="bye";
	    key.size=4;
	    r=db->get(db, tid, &key, &data, 0);
	    CKERR(r);
	}	    
	r=tid->commit(tid,0);        assert(r==0);
    }

    r=db->close(db, 0);       assert(r==0);
    r=env->close(env, 0);     assert(r==0);
}

int main (int UU(argc), char UU(*argv[])) {
    test_db_open_aborts();
    test_db_put_aborts();
    return 0;
}
