/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <sys/stat.h>
#include <db.h>


static void
test_db_set_flags (int flags, int expectr, int flags2, int expectr2) {
    if (verbose) printf("test_db_set_flags:%d %d %d %d\n", flags, expectr, flags2, expectr2);

    DB_TXN * const null_txn = 0;
    const char * const fname = "test.db.set.flags.ft_handle";
    int r;

    r = system("rm -rf " ENVDIR); CKERR(r);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    DB_ENV *env;
    r = db_env_create(&env, 0); assert(r == 0);
    r = env->open(env, ENVDIR, DB_CREATE+DB_PRIVATE+DB_INIT_MPOOL, 0); assert(r == 0);

    DB *db;
    r = db_create(&db, env, 0); assert(r == 0);
    db->set_errfile(db,0); // Turn off those annoying errors
    r = db->set_flags(db, flags); assert(r == expectr);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666); assert(r == 0);
    r = db->set_flags(db, flags2); assert(r == expectr2);
    r = db->close(db, 0); assert(r == 0);
    r = env->close(env, 0); assert(r == 0);
}

int
test_main(int argc, char *const argv[]) {

    parse_args(argc, argv);
  
    test_db_set_flags(0, 0, 0, 0);

    return 0;
}
