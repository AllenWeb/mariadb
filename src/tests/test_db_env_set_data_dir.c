/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdio.h>
#include <assert.h>
#include <toku_portability.h>
#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "test.h"

// ENVDIR is defined in the Makefile

int main() {
    DB_ENV *dbenv;
    int r;

    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    r = db_env_create(&dbenv, 0); assert(r == 0);

    r = dbenv->set_data_dir(dbenv, ENVDIR); assert(r == 0);

    r = dbenv->set_data_dir(dbenv, ENVDIR); assert(r == 0);

    r = dbenv->open(dbenv, 0, DB_CREATE|DB_PRIVATE|DB_INIT_MPOOL, 0);
    CKERR(r);

#ifdef USE_TDB
    // According to the BDB man page, you may not call set_data_dir after doing the open.
    // Some versions of BDB don't actually check this or complain
    r = dbenv->set_data_dir(dbenv, "foo" ENVDIR);
    assert(r == EINVAL);
#endif

    DB *db;
    r = db_create(&db, dbenv, 0); assert(r == 0);

    r = db->open(db, 0, "test.db", "main", DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);
    
    r = db->close(db, 0); assert(r == 0);
    
    r = dbenv->close(dbenv, 0); assert(r == 0);

    return 0;
}
