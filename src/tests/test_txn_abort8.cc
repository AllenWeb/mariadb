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

// 
static void
test_abort_close (void) {

#ifndef USE_TDB
#if DB_VERSION_MAJOR==4 && DB_VERSION_MINOR==3
    if (verbose) fprintf(stderr, "%s does not work for BDB %d.%d.   Not running\n", __FILE__, DB_VERSION_MAJOR, DB_VERSION_MINOR);
    return;
#else
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO);

    int r;
    DB_ENV *env;
    r = db_env_create(&env, 0); assert(r == 0);
    r = env->set_data_dir(env, TOKU_TEST_FILENAME);
    r = env->set_lg_dir(env, TOKU_TEST_FILENAME);
    env->set_errfile(env, stdout);
    r = env->open(env, 0, DB_INIT_MPOOL + DB_INIT_LOG + DB_INIT_LOCK + DB_INIT_TXN + DB_PRIVATE + DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); 
    if (r != 0) printf("%s:%d:%d:%s\n", __FILE__, __LINE__, r, db_strerror(r));
    assert(r == 0);

    DB_TXN *txn = 0;
    r = env->txn_begin(env, 0, &txn, 0); assert(r == 0);

    DB *db;
    r = db_create(&db, env, 0); assert(r == 0);
    r = db->open(db, txn, "test.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);

    {
	toku_struct_stat statbuf;
        char fullfile[TOKU_PATH_MAX+1];
	r = toku_stat(toku_path_join(fullfile, 2, TOKU_TEST_FILENAME, "test.db"), &statbuf);
	assert(r==0);
    }

    // Close before abort.
    r = db->close(db, 0);

    r = txn->abort(txn); assert(r == 0);

    r = env->close(env, 0); assert(r == 0);

    {
	toku_struct_stat statbuf;
        char fullfile[TOKU_PATH_MAX+1];
	r = toku_stat(toku_path_join(fullfile, 2, TOKU_TEST_FILENAME, "test.db"), &statbuf);
	assert(r!=0);
    }
#endif
#endif
}

int
test_main(int UU(argc), char UU(*const argv[])) {
    test_abort_close();
    return 0;
}
