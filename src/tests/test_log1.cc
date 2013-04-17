/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

/* Simple test of logging.  Can I start a TokuDB with logging enabled? */

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <db.h>
#include <memory.h>
#include <stdio.h>
#include <errno.h>


// ENVDIR is defined in the Makefile

DB_ENV *env;
DB *db;
DB_TXN *tid;

static void make_db (bool close_env) {
    int r;
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);       assert(r==0);
    r=db_env_create(&env, 0); assert(r==0);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_PRIVATE|DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=db_create(&db, env, 0); CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0); assert(r==0);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    {
	DBT key,data;
        dbt_init(&key, "hello", sizeof "hello");
        dbt_init(&data, "there", sizeof "there");
	r=db->put(db, tid, &key, &data, 0);
	CKERR(r);
    }
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
        CAST_FROM_VOIDP(filename, iname.data);
        assert(filename);
    }
#else
    filename = toku_xstrdup("foo.db");
#endif


    r=tid->commit(tid, 0);    assert(r==0);
    r=db->close(db, 0);       assert(r==0);
    {
	toku_struct_stat statbuf;
        char fullfile[strlen(filename) + sizeof(ENVDIR "/")];
        snprintf(fullfile, sizeof(fullfile), ENVDIR "/%s", filename);
        toku_free(filename);
	r = toku_stat(fullfile, &statbuf);
	assert(r==0);
    }
    if (close_env) {
        r=env->close(env, 0);     assert(r==0);
    }
}

int
test_main (int argc, char *const argv[]) {
    bool close_env = true;
    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "--no-shutdown") == 0)
            close_env = false;
    }
    make_db(close_env);
    return 0;
}

