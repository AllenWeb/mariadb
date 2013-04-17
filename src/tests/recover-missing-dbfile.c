/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
// verify that DB_RUNRECOVERY is returned when there is a missing db file

#include <sys/stat.h>
#include "test.h"


const int envflags = DB_INIT_MPOOL|DB_CREATE|DB_THREAD |DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN|DB_PRIVATE;

#define NAMEA "a.db"
const char *namea=NAMEA;
#define NAMEB "b.db"
const char *nameb=NAMEB;

static void run_test (void) {
    int r;
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);
    DB_ENV *env;
    DB *dba;

    r = db_env_create(&env, 0);                                                         CKERR(r);
#if IS_TDB
    db_env_enable_engine_status(0);  // disable engine status on crash because test is expected to fail
#endif
    r = env->open(env, ENVDIR, envflags, S_IRWXU+S_IRWXG+S_IRWXO);                      CKERR(r);

    r = db_create(&dba, env, 0);                                                        CKERR(r);
    r = dba->open(dba, NULL, namea, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);    CKERR(r);

    r = env->txn_checkpoint(env, 0, 0, 0);                                              CKERR(r);

    DB_TXN *txn;
    r = env->txn_begin(env, NULL, &txn, 0);                                             CKERR(r);
    {
        DBT a,b;
        dbt_init(&a, "a", 2);
        dbt_init(&b, "b", 2);
	r = dba->put(dba, txn, &a, &b, 0);                                CKERR(r);
    }

    r = txn->commit(txn, 0);                                                            CKERR(r);

    toku_hard_crash_on_purpose();
}

static void run_recover (void) {
    DB_ENV *env;
    int r;

    r = system("rm -rf " ENVDIR "/saveddbs");
    CKERR(r);
    r = toku_os_mkdir(ENVDIR "/saveddbs", S_IRWXU+S_IRWXG+S_IRWXO);
    CKERR(r);

    r = system("mv " ENVDIR "/*.tokudb " ENVDIR "/saveddbs/");
    CKERR(r);

    r = db_env_create(&env, 0);                                                             CKERR(r);
#if IS_TDB
    db_env_enable_engine_status(0);  // disable engine status on crash because test is expected to fail
#endif
    r = env->open(env, ENVDIR, envflags + DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);
    CKERR2(r, DB_RUNRECOVERY);

    r = system("rm -rf " ENVDIR "/*.tokudb");
    CKERR(r);

    r = system("mv "  ENVDIR "/saveddbs/*.tokudb " ENVDIR "/");
    CKERR(r);

    r = env->open(env, ENVDIR, envflags + DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);             CKERR(r);
    r = env->close(env, 0);                                                                 CKERR(r);
    exit(0);
}

static void run_no_recover (void) {
    DB_ENV *env;
    int r;

    r = db_env_create(&env, 0);                                                             CKERR(r);
#if IS_TDB
    db_env_enable_engine_status(0);  // disable engine status on crash because test is expected to fail
#endif
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
