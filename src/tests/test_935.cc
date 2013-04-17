/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"
#include <stdio.h>

#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <db.h>
#include <memory.h>

static void
testit (const int klen, const int vlen, const int n, const int lastvlen) {
    if (verbose) printf("testit %d %d %d %d\n", klen, vlen, n, lastvlen);

    int r;

    // setup test directory
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    // setup environment
    DB_ENV *env;
    {
        r = db_env_create(&env, 0); assert(r == 0);
        env->set_errfile(env, stdout);
        r = env->open(env, ENVDIR, DB_INIT_MPOOL + DB_PRIVATE + DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); 
        assert(r == 0);
    }

    // setup database
    DB *db;
    {
        DB_TXN *txn = 0;
        r = db_create(&db, env, 0); assert(r == 0);
        r = db->open(db, txn, "test.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);
    }

    // insert to fill up a node
    {    
        void *v = toku_malloc(vlen); assert(v); memset(v, 0, vlen);
        DB_TXN *txn = 0;
        int i;
        for (i=0; i<n; i++) {
            int k = htonl(i);
            assert(sizeof k == klen);
            DBT key, val;
            r = db->put(db, txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, v, vlen), 0);
            assert(r == 0);
        }
        if (lastvlen > 0) {
            int k = htonl(n);
            DBT key, val;
            r = db->put(db, txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, v, lastvlen), 0);
            assert(r == 0);
        }
        toku_free(v);
    }

    // add another one to force a node split
    {    
        void *v = toku_malloc(vlen); assert(v); memset(v, 0, vlen);
        DB_TXN *txn = 0;
        int k = htonl(n+1);
        DBT key, val;
        r = db->put(db, txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, v, vlen), 0);
        assert(r == 0);
        toku_free(v);
    }

    // close db
    r = db->close(db, 0); assert(r == 0);

    // close env
    r = env->close(env, 0); assert(r == 0);
}

int
test_main(int argc, char *const argv[]) {
    parse_args(argc, argv);
    const int meg = 1024*1024;
    const int headeroverhead = 12*4;
    const int numentries = 4;
    const int klen = 4;
    const int vlen = 4096;
    const int leafoverhead = 1+8+4+4;
    const int leafentrysize = leafoverhead+klen+vlen;
    int n = (meg - headeroverhead - numentries) / leafentrysize;
    int left = meg - headeroverhead - numentries - n*leafentrysize;
    int lastvlen = left - leafoverhead - klen;
    testit(klen, vlen, n, lastvlen-1);
    testit(klen, vlen, n, lastvlen-0);
    testit(klen, vlen, n, lastvlen+1);
    return 0;
}
