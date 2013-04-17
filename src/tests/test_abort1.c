/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

/* Simple test of logging.  Can I start a TokuDB with logging enabled? */

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <db.h>
#include <memory.h>
#include <stdio.h>


// ENVDIR is defined in the Makefile

static void
test_db_open_aborts (void) {
    DB_ENV *env;
    DB *db;

    int r;
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_PRIVATE|DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);
#if 0
    {
	DB_TXN *tid;
	r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
	r=tid->abort(tid);        assert(r==0);
    }
    {
	toku_struct_stat buf;
	r=toku_stat(ENVDIR "/foo.db", &buf);
	assert(r!=0);
	assert(errno==ENOENT);
    }
#endif
    {
	DB_TXN *tid;
	r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU|S_IRWXG|S_IRWXO); CKERR(r);
	{
	    DBT key,data;
            dbt_init(&key, "hello", 6);
            dbt_init(&data, "there", 6);
	    r=db->put(db, tid, &key, &data, 0);
	    CKERR(r);
	}
        r=db->close(db, 0);       assert(r==0);
	r=tid->abort(tid);        assert(r==0);
    }
    {
#if USE_TDB
        {
            DBT dname;
            DBT iname;
            dbt_init(&dname, "foo.db", sizeof("foo.db"));
            dbt_init(&iname, NULL, 0);
            iname.flags |= DB_DBT_MALLOC;
            r = env->get_iname(env, &dname, &iname);
            CKERR2(r, DB_NOTFOUND);
        }
#endif
        toku_struct_stat statbuf;
        r = toku_stat(ENVDIR "/foo.db", &statbuf);
        assert(r!=0);
        assert(errno==ENOENT);
    }

    r=env->close(env, 0);     assert(r==0);
}

// Do two transactions, one commits, and one aborts.  Do them concurrently.
static void
test_db_put_aborts (void) {
    DB_ENV *env;
    DB *db;

    int r;
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_PRIVATE|DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);

    {
	DB_TXN *tid;
	r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
	r=tid->commit(tid,0);        assert(r==0);
    }
    {
	DB_TXN *tid;
	DB_TXN *tid2;
	r=env->txn_begin(env, 0, &tid, 0);  assert(r==0);
	r=env->txn_begin(env, 0, &tid2, 0); assert(r==0);
	{
	    DBT key,data;
            dbt_init(&key, "hello", 6);
            dbt_init(&data, "there", 6);
	    r=db->put(db, tid, &key, &data, 0);
	    CKERR(r);
	}
	{
	    DBT key,data;
            dbt_init(&key, "bye", 4);
            dbt_init(&data, "now", 4);
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
        char *filename;
#if USE_TDB
        {
            DBT dname;
            DBT iname;
            dbt_init(&dname, "foo.db", sizeof("foo.db"));
            dbt_init(&iname, NULL, 0);
            iname.flags |= DB_DBT_MALLOC;
            r = env->get_iname(env, &dname, &iname);
            CKERR(r);
            filename = cast_to_typeof(filename) iname.data;
            assert(filename);
        }
#else
        filename = toku_xstrdup("foo.db");
#endif
	toku_struct_stat statbuf;
        char fullfile[strlen(filename) + sizeof(ENVDIR "/")];
        snprintf(fullfile, sizeof(fullfile), ENVDIR "/%s", filename);
        toku_free(filename);
	r = toku_stat(fullfile, &statbuf);
	assert(r==0);
    }
    // But the item should not be in it.
    if (1)
    {
	DB_TXN *tid;
	r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
	{
	    DBT key,data;
            dbt_init(&key, "hello", 6);
            dbt_init(&data, NULL, 0);
	    r=db->get(db, tid, &key, &data, 0);
	    assert(r!=0);
	    assert(r==DB_NOTFOUND);
	}	    
	{
	    DBT key,data;
            dbt_init(&key, "bye", 4);
            dbt_init(&data, NULL, 0);
	    r=db->get(db, tid, &key, &data, 0);
	    CKERR(r);
	}	    
	r=tid->commit(tid,0);        assert(r==0);
    }

    r=db->close(db, 0);       assert(r==0);
    r=env->close(env, 0);     assert(r==0);
}

int
test_main (int UU(argc), char UU(*const argv[])) {
    test_db_open_aborts();
    test_db_put_aborts();
    return 0;
}
