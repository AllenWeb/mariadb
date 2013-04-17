/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#include "test.h"
#include <sys/stat.h>

/* basic checkpoint testing.  Do things end up in the log? */

DB_ENV *env;
DB *db;
DB_TXN *txn;

static void
insert(int i)
{
    char hello[30], there[30];
    snprintf(hello, sizeof(hello), "hello%d", i);
    snprintf(there, sizeof(there), "there%d", i);
    DBT key, val;
    int r=db->put(db, txn,
		  dbt_init(&key, hello, strlen(hello)+1),
		  dbt_init(&val, there, strlen(there)+1),
		  0);
    CKERR(r);
}

static void
checkpoint1 (void)
{
    int r;
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO);

    r = db_env_create(&env, 0);                                                     CKERR(r);
#ifdef TOKUDB
    r = env->set_redzone(env, 0);                                                   CKERR(r);
#endif

    r = env->open(env, TOKU_TEST_FILENAME, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r = db_create(&db, env, 0);                                                     CKERR(r);

    r=env->txn_begin(env, 0, &txn, 0);                                              CKERR(r);
    r=db->open(db, txn, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=txn->commit(txn, 0);                                                          CKERR(r);

    r=env->txn_begin(env, 0, &txn, 0);                                              CKERR(r);
    insert(0);
    r=env->txn_checkpoint(env, 0, 0, 0);                                            CKERR(r);
    r=txn->commit(txn, 0);                                                          CKERR(r);
    r=db->close(db, 0);                                                             CKERR(r);
    r=env->close(env, 0);                                                           CKERR(r);
}

int
test_main (int argc, char * const argv[])
{
    parse_args(argc, argv);
    checkpoint1();
    return 0;
}

