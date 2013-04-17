/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"


#include <stdio.h>

#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>

// ENVDIR is defined in the Makefile

int
test_main(int argc, char*const* argv) {
    DB_ENV *dbenv;
    int r;
    if (argc == 2 && !strcmp(argv[1], "-v")) verbose = 1;
    
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO); assert(r==0);

    r = db_env_create(&dbenv, 0);
    assert(r == 0);

    r = dbenv->open(dbenv, ENVDIR, DB_CREATE|DB_INIT_MPOOL|DB_PRIVATE, 0666);
    assert(r == 0);

    r = dbenv->open(dbenv, ENVDIR, DB_CREATE|DB_INIT_MPOOL|DB_PRIVATE, 0666);
    if (verbose) printf("r=%d\n", r);
#ifdef USE_TDB
    assert(r == EINVAL);
#elif USE_BDB
#if DB_VERSION_MAJOR >= 5
    assert(r == EINVAL);
#else
    if (verbose) printf("test_db_env_open_open_close.bdb skipped.  (BDB apparently does not follow the spec).\n");
    assert(r == 0);
#endif
#else
#error
#endif    

    r = dbenv->close(dbenv, 0);
    assert(r == 0);

    return 0;
}
