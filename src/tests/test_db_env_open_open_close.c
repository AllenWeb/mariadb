/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdio.h>
#include <assert.h>
#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

// ENVDIR is defined in the Makefile

#define CKERR(r) if (r!=0) fprintf(stderr, "%s:%d error %d %s\n", __FILE__, __LINE__, r, db_strerror(r)); assert(r==0);

int main(int argc, char** argv) {
    DB_ENV *dbenv;
    int r;
    int __attribute__((__unused__)) verbose = 0;
    if (argc == 2 && !strcmp(argv[1], "-v")) verbose = 1;
    
    system("rm -rf " ENVDIR);
    r=mkdir(ENVDIR, 0777); assert(r==0);

    r = db_env_create(&dbenv, 0);
    assert(r == 0);

    r = dbenv->open(dbenv, ENVDIR, DB_CREATE|DB_INIT_MPOOL|DB_PRIVATE, 0666);
    assert(r == 0);

    r = dbenv->open(dbenv, ENVDIR, DB_CREATE|DB_INIT_MPOOL|DB_PRIVATE, 0666);
#ifdef USE_TDB
    if (verbose) printf("r=%d\n", r);
    assert(r != 0);
#else
    if (verbose) printf("test_db_env_open_open_close.bdb skipped.  (BDB apparently does not follow the spec).\n");
    assert(r==0);
#endif    

    r = dbenv->close(dbenv, 0);
    assert(r == 0);

    return 0;
}
