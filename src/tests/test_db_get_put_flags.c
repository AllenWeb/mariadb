/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

#include <memory.h>
#include <db.h>

#include <errno.h>
#include <sys/stat.h>


// ENVDIR is defined in the Makefile

typedef struct {
    u_int32_t db_flags;
    u_int32_t flags;
    int       r_expect;
    int       key;
    int       data;
} PUT_TEST;

typedef struct {
    PUT_TEST  put;
    u_int32_t flags;
    int       r_expect;
    int       key;
    int       data;
} GET_TEST;

enum testtype {NONE=0, TGET=1, TPUT=2, SGET=3, SPUT=4, SPGET=5};

typedef struct {
    enum testtype kind;
    u_int32_t     flags;
    int           r_expect;
    int           key;
    int           data;
} TEST;

static DB *dbp;
static DB_TXN *const null_txn = 0;
static DB_ENV *dbenv;

static void
setup (u_int32_t flags) {
    int r;

    r = system("rm -rf " ENVDIR);
    CKERR(r);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);
    /* Open/create primary */
    r = db_env_create(&dbenv, 0); assert(r == 0);
#ifdef USE_TDB
    r = dbenv->set_redzone(dbenv, 0);                              CKERR(r);
#endif
    r = dbenv->open(dbenv, ENVDIR, DB_CREATE+DB_PRIVATE+DB_INIT_MPOOL, 0); assert(r == 0);
    r = db_create(&dbp, dbenv, 0);                                              CKERR(r);
    dbp->set_errfile(dbp,0); // Turn off those annoying errors
    if (flags) {
        r = dbp->set_flags(dbp, flags);                                       CKERR(r);
    }    
    r = dbp->open(dbp, NULL, "primary.db", NULL, DB_BTREE, DB_CREATE, 0600);   CKERR(r);
}

static void
close_dbs (void) {
    int r;
    r = dbp->close(dbp, 0);                             CKERR(r);
    r = dbenv->close(dbenv, 0);                         CKERR(r);
}

static void
insert_bad_flags (DB* db, u_int32_t flags, int r_expect, int keyint, int dataint) {
    DBT key;
    DBT data;
    int r;
    
    dbt_init(&key, &keyint, sizeof(keyint));
    dbt_init(&data,&dataint,sizeof(dataint));
    r = db->put(db, null_txn, &key, &data, flags);
    CKERR2(r, r_expect);
}

static void
get_bad_flags (DB* db, u_int32_t flags, int r_expect, int keyint, int dataint) {
    DBT key;
    DBT data;
    int r;
    
    dbt_init(&key, &keyint, sizeof(keyint));
    dbt_init(&data,&dataint,sizeof(dataint));
    r = db->get(db, null_txn, &key, &data, flags);
    CKERR2(r, r_expect);
    //Verify things don't change.
    assert(*(int*)key.data == keyint);
    assert(*(int*)data.data == dataint);
}

#ifdef USE_TDB
#define EINVAL_FOR_TDB_OK_FOR_BDB EINVAL
#else
#define EINVAL_FOR_TDB_OK_FOR_BDB 0
#endif


PUT_TEST put_tests[] = {
    {0,                 DB_NODUPDATA,    EINVAL, 0, 0},  //r_expect must change to 0, once implemented.
    {0,                 0, 0,      0, 0},
    {0,                 DB_NOOVERWRITE,  0,      0, 0},
    {0,                 0,               0,      0, 0},
};
const int num_put = sizeof(put_tests) / sizeof(put_tests[0]);

GET_TEST get_tests[] = {
    {{0,                 0,                         0, 0, 0}, 0          , 0,           0, 0},
    {{0,                 0,                         0, 0, 0}, 0          , 0,           0, 0},
    {{0,                 0,           0, 0, 0}, 0          , 0,           0, 0},
    {{0,                 0,           0, 0, 0}, 0          , 0,           0, 0},
    {{0,                 0,           0, 0, 0}, DB_RMW,      EINVAL,      0, 0},
    {{0,                 0,                         0, 0, 0}, DB_RMW,      EINVAL,      0, 0},
};
const int num_get = sizeof(get_tests) / sizeof(get_tests[0]);

int
test_main(int argc, char *const argv[]) {
    int i;
    
    parse_args(argc, argv);
    
    for (i = 0; i < num_put; i++) {
        if (verbose) printf("PutTest [%d]\n", i);
        setup(put_tests[i].db_flags);
        insert_bad_flags(dbp, put_tests[i].flags, put_tests[i].r_expect, put_tests[i].key, put_tests[i].data);
        close_dbs();
    }

    for (i = 0; i < num_get; i++) {
        if (verbose) printf("GetTest [%d]\n", i);
        setup(get_tests[i].put.db_flags);
        insert_bad_flags(dbp, get_tests[i].put.flags, get_tests[i].put.r_expect, get_tests[i].put.key, get_tests[i].put.data);
        get_bad_flags(dbp, get_tests[i].flags, get_tests[i].r_expect, get_tests[i].key, get_tests[i].data);
        close_dbs();
    }

    return 0;
}
