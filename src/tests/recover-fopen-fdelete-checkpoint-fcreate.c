/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
// verify that we can fcreate after fdelete with different treeflags

#include <sys/stat.h>
#include "test.h"


const int envflags = DB_INIT_MPOOL|DB_CREATE|DB_THREAD |DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN|DB_PRIVATE;

const char *namea="a.db";
const char *nameb="b.db";

static void put_something(DB_ENV *env, DB *db, const char *k, const char *v) {
    int r;
    DBT key, val;
    dbt_init(&key, k, strlen(k));
    dbt_init(&val, v, strlen(v));
    DB_TXN *txn;
    r = env->txn_begin(env, 0, &txn, 0);                                              CKERR(r);
    r = db->put(db, txn, &key, &val, 0);                                CKERR(r);
    r = txn->commit(txn, 0);                                                          CKERR(r);
}

static void run_test (void) {
    int r;
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    DB_ENV *env;
    DB *db;
    DB_TXN *txn;

    r = db_env_create(&env, 0);                                                         CKERR(r);
    r = env->open(env, ENVDIR, envflags, S_IRWXU+S_IRWXG+S_IRWXO);                      CKERR(r);

    // fcreate
    r = db_create(&db, env, 0);                                                         CKERR(r);
    r = db->open(db, NULL, namea, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);      CKERR(r);
    r = db->close(db, 0);                                                               CKERR(r);

    // dummy transaction
    r = env->txn_begin(env, NULL, &txn, 0);                                             CKERR(r);

    // fopen
    r = db_create(&db, env, 0);                                                         CKERR(r);
    r = db->open(db, NULL, namea, NULL, DB_UNKNOWN, DB_AUTO_COMMIT, 0666);              CKERR(r);

    // insert something
    put_something(env, db, "a", "b");

    r = db->close(db, 0);                                                               CKERR(r);

    // fdelete
    r = env->dbremove(env, NULL, namea, NULL, 0);                                       CKERR(r);

    // checkpoint
    r = env->txn_checkpoint(env, 0, 0, 0);                                              CKERR(r);

    r = txn->commit(txn, 0);                                                            CKERR(r);

    // fcreate with different treeflags
    r = db_create(&db, env, 0);                                                         CKERR(r);
    r = db->open(db, NULL, namea, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);      CKERR(r);

    // insert something
    put_something(env, db, "c", "d");

    r = db->close(db, 0);                                                               CKERR(r);

    toku_hard_crash_on_purpose();
}

static void run_recover (void) {
    DB_ENV *env;
    int r;

    r = db_env_create(&env, 0);                                                             CKERR(r);
    r = env->open(env, ENVDIR, envflags + DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);             CKERR(r);
    
    u_int32_t dbflags;
    DB *db;
    r = db_create(&db, env, 0);                                                             CKERR(r);
    r = db->open(db, NULL, namea, NULL, DB_UNKNOWN, DB_AUTO_COMMIT, 0666);                  CKERR(r);
    r = db->get_flags(db, &dbflags);                                                        CKERR(r);
    r = db->close(db, 0);                                                                   CKERR(r);

    r = env->close(env, 0);                                                                 CKERR(r);
    exit(0);
}

static void run_no_recover (void) {
    DB_ENV *env;
    int r;

    r = db_env_create(&env, 0);                                                             CKERR(r);
    r = env->open(env, ENVDIR, envflags & ~DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);            CKERR(r);
    r = env->close(env, 0);                                                                 CKERR(r);
    exit(0);
}

const char *cmd;

BOOL do_test=FALSE, do_recover=FALSE, do_recover_only=FALSE, do_no_recover = FALSE;

static void test_parse_args (int argc, char * const argv[]) {
    int resultcode;
    cmd = argv[0];
    argc--; argv++;
    while (argc>0) {
	if (strcmp(argv[0], "-v") == 0) {
	    verbose++;
	} else if (strcmp(argv[0],"-q")==0) {
	    verbose--;
	    if (verbose<0) verbose=0;
	} else if (strcmp(argv[0], "--test")==0) {
	    do_test=TRUE;
        } else if (strcmp(argv[0], "--recover") == 0) {
            do_recover=TRUE;
        } else if (strcmp(argv[0], "--recover-only") == 0) {
            do_recover_only=TRUE;
        } else if (strcmp(argv[0], "--no-recover") == 0) {
            do_no_recover=TRUE;
	} else if (strcmp(argv[0], "-h")==0) {
	    resultcode=0;
	do_usage:
	    fprintf(stderr, "Usage:\n%s [-v|-q]* [-h] {--test | --recover } \n", cmd);
	    exit(resultcode);
	} else {
	    fprintf(stderr, "Unknown arg: %s\n", argv[0]);
	    resultcode=1;
	    goto do_usage;
	}
	argc--;
	argv++;
    }
}

int test_main (int argc, char * const argv[]) {
    test_parse_args(argc, argv);
    if (do_test) {
	run_test();
    } else if (do_recover) {
        run_recover();
    } else if (do_recover_only) {
        run_recover();
    } else if (do_no_recover) {
        run_no_recover();
    } 
    return 0;
}
