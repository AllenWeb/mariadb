/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdio.h>
#include <assert.h>
#include <toku_portability.h>
#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

// ENVDIR is defined in the Makefile

int main() {
    DB_ENV *dbenv;
    int r;

    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    r = db_env_create(&dbenv, 0);
    assert(r == 0);

    dbenv->set_errpfx(dbenv, "houdy partners");

    r = dbenv->open(dbenv, ENVDIR, DB_CREATE|DB_PRIVATE|DB_INIT_MPOOL, 0);
    assert(r == 0);

    dbenv->set_errpfx(dbenv, "houdy partners");

    r = dbenv->close(dbenv, 0);
    assert(r == 0);

    return 0;
}
