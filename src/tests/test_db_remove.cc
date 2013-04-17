/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"
#include <fcntl.h>

DB_TXN * const null_txn = 0;

const char * const fname = "test_db_remove.ft_handle";

static void test_db_remove (void) {
    int r;
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    r=toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO); assert(r==0);

    // create the DB
    DB_ENV *env;
    r = db_env_create(&env, 0); assert(r == 0);
    r = env->open(env, TOKU_TEST_FILENAME, DB_CREATE+DB_PRIVATE+DB_INIT_MPOOL, 0); assert(r == 0);

    DB *db1;
    r = db_create(&db1, env, 0);                                  assert(r == 0);
    r = db1->open(db1, null_txn, fname, 0, DB_BTREE, DB_CREATE, 0666); assert(r == 0);
    r = db1->close(db1, 0); assert(r == 0); //Header has been written to disk

    r = db_create(&db1, env, 0);                                  assert(r == 0);
    r = db1->open(db1, null_txn, fname, 0, DB_BTREE, DB_CREATE, 0666); assert(r == 0);

    // Now remove it, while it is open.
    r = env->dbremove(env, NULL, fname, 0, 0);
#ifdef USE_TDB
    assert(r!=0);
#else
    assert(r==0);
#endif

    r = db1->close(db1, 0);                                            assert(r==0);
    r = env->close(env, 0); assert(r == 0);
}

int
test_main(int argc, char *const argv[]) {
    parse_args(argc, argv);

    test_db_remove();

    return 0;
}
