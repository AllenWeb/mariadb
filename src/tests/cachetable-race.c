/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2010 Tokutek Inc.  All rights reserved."
#ident "$Id$"

/* This test is a little bit ugly, but I think it's mostly harmless.
 * It's ugly because it requires a hook into cachetable.c, and that
 * hook is a little ugly.
 *
 * The way it works:
 *
 *  In the cachetable we set a variable that indicates that we are
 *  saving user data.  (This is toku_checkpointing_user_data_status).
 *
 *  In this test (cachetable-race), we close 2 of 5 dbs, and then busy
 *  wait for that status variable to go to 1 and close the others.
 *
 *  In this way, the close happens (with good chances) right in the
 *  middle of the checkpoint loop for saving all the user data, which
 *  seems to trigger the race error.
 */

#include "test.h"
#include "toku_pthread.h"
#include <db.h>
#include <sys/stat.h>
#include "ydb.h"
#include "ydb-internal.h"

DB_ENV *env;
enum {NUM_DBS=5};

char *free_me = NULL;
const char *env_dir = ENVDIR; // the default env_dir.

static void run_cachetable_race_test(void) 
{
    int r;
    {
	int len = strlen(env_dir) + 20;
	char syscmd[len];
	r = snprintf(syscmd, len, "rm -rf %s", env_dir);
	assert(r<len);
	r = system(syscmd);                                                                                   CKERR(r);
    }
    r = toku_os_mkdir(env_dir, S_IRWXU+S_IRWXG+S_IRWXO);                                                      CKERR(r);

    r = db_env_create(&env, 0);                                                                               CKERR(r);
    env->set_errfile(env, stderr);
    int envflags = DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_INIT_TXN | DB_CREATE | DB_PRIVATE;
    r = env->open(env, env_dir, envflags, S_IRWXU+S_IRWXG+S_IRWXO);                                           CKERR(r);
    env->set_errfile(env, stderr);
    r = env->checkpointing_set_period(env, 5);                                                                CKERR(r);
    
    enum {MAX_NAME=128};
    char name[MAX_NAME];

    DB **dbs = (DB**)toku_malloc(sizeof(DB*) * NUM_DBS);
    assert(dbs != NULL);
    int idx[NUM_DBS];
    for(int i=0;i<NUM_DBS;i++) {
        idx[i] = i;
        r = db_create(&dbs[i], env, 0);                                                                       CKERR(r);
        dbs[i]->app_private = &idx[i];
        snprintf(name, sizeof(name), "db_%04x", i);
        r = dbs[i]->open(dbs[i], NULL, name, NULL, DB_BTREE, DB_CREATE, 0666);                                CKERR(r);

    }

    for(int i=0;i<NUM_DBS;i++) {
        dbs[i]->close(dbs[i], 0);                                                                             CKERR(r);
        dbs[i] = NULL;
	
	if (i==2) {
	    if (verbose) printf("%s:%d c=%d\n", __FILE__, __LINE__, toku_test_get_checkpointing_user_data_status());
	    while (toku_test_get_checkpointing_user_data_status()==0)
		sched_yield();
	    if (verbose) printf("%s:%d c=%d\n", __FILE__, __LINE__, toku_test_get_checkpointing_user_data_status());
	}
    }
    r = env->close(env, 0);                                                                                   CKERR(r);
    toku_free(dbs);
}

// ------------ infrastructure ----------
static void do_args(int argc, char * const argv[]);

int test_main(int argc, char * const *argv) {
    do_args(argc, argv);
    run_cachetable_race_test();
    if (free_me) toku_free(free_me);

    return 0;
}

static void do_args(int argc, char * const argv[]) {
    int resultcode;
    char *cmd = argv[0];
    argc--; argv++;
    
    while (argc>0) {
	if (strcmp(argv[0], "-v")==0) {
	    verbose++;
	} else if (strcmp(argv[0],"-q")==0) {
	    verbose--;
	    if (verbose<0) verbose=0;
        } else if (strcmp(argv[0], "-h")==0) {
	    resultcode=0;
	do_usage:
	    fprintf(stderr, "Usage: [-h] [-v] [-q] [-e <env>] -s\n%s\n", cmd);
	    fprintf(stderr, "  where -h               print this message\n");
	    fprintf(stderr, "        -v               verbose (multiple times for more verbosity)\n");
	    fprintf(stderr, "        -q               quiet (default is verbosity==1)\n");
	    fprintf(stderr, "        -e <env>         uses <env> to construct the directory (so that different tests can run concurrently)\n");
	    fprintf(stderr, "        -s               use size factor of 1 and count temporary files\n");
	    exit(resultcode);
	} else if (strcmp(argv[0], "-e")==0) {
            argc--; argv++;
	    if (free_me) toku_free(free_me);
	    int len = strlen(ENVDIR) + strlen(argv[0]) + 2;
	    char full_env_dir[len];
	    int r = snprintf(full_env_dir, len, "%s.%s", ENVDIR, argv[0]);
	    assert(r<len);
	    env_dir = toku_strdup(full_env_dir);
            free_me = (char *) env_dir;
        } else if (strcmp(argv[0], "-s")==0) {
	    printf("\nTesting loader with size_factor=1\n");
	    db_env_set_loader_size_factor(1);            
	} else {
	    fprintf(stderr, "Unknown arg: %s\n", argv[0]);
	    resultcode=1;
	    goto do_usage;
	}
	argc--;
	argv++;
    }
}
