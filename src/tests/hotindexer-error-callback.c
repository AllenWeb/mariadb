/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2010 Tokutek Inc.  All rights reserved."

#include "test.h"
#include "toku_pthread.h"
#include <db.h>
#include <sys/stat.h>
#include "key-val.h"
#include "ydb.h"
#include "indexer.h"

enum {NUM_DBS=1};
static const int NUM_ROWS = 10;
typedef enum {FORWARD = 0, BACKWARD} Direction;
typedef enum {TXN_NONE = 0, TXN_CREATE = 1, TXN_END = 2} TxnWork;

DB_ENV *env;

int error_cb_count = 0;
static void error_callback(DB *db, int which_db, int err, DBT *key, DBT *val, void *extra) 
{
    error_cb_count++;
    if ( verbose ) {
        printf("error_callback (%d) : db_p = %p, which_db = %d, error = %d, key_p = %p, val_p = %p, extra_p = %p\n", 
               error_cb_count, 
               db, which_db, 
               err, 
               key, val, extra);
    }
}

static void test_indexer(DB *src, DB **dbs)
{
    int r;
    DB_TXN    *txn;
    DB_INDEXER *indexer;
    uint32_t db_flags[NUM_DBS];

    if ( verbose ) printf("test_indexer\n");
    for(int i=0;i<NUM_DBS;i++) { 
        db_flags[i] = DB_NOOVERWRITE; 
    }

    // create and initialize loader
    r = env->txn_begin(env, NULL, &txn, 0);                                                               
    CKERR(r);

    if ( verbose ) printf("test_indexer create_indexer\n");
    r = env->create_indexer(env, txn, &indexer, src, NUM_DBS, dbs, db_flags, 0);
    CKERR(r);
    r = indexer->set_error_callback(indexer, error_callback, NULL);
    CKERR(r);
    toku_indexer_set_test_only_flags(indexer, INDEXER_TEST_ONLY_ERROR_CALLBACK);

    r = indexer->set_poll_function(indexer, poll_print, NULL);
    CKERR(r);

    r = indexer->build(indexer);
    assert(r != 0 ); // build should return an error
    assert(error_cb_count == 1);  // error callback count should be 1

    if ( verbose ) printf("test_indexer close\n");
    r = indexer->close(indexer);
    CKERR(r);
    r = txn->commit(txn, DB_TXN_SYNC);
    CKERR(r);

    if ( verbose ) printf("PASS\n");
    if ( verbose ) printf("test_indexer done\n");
}


static void run_test(void) 
{
    int r;
    r = system("rm -rf " ENVDIR);                                                CKERR(r);
    r = toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);                          CKERR(r);
    r = toku_os_mkdir(ENVDIR "/log", S_IRWXU+S_IRWXG+S_IRWXO);                   CKERR(r);

    r = db_env_create(&env, 0);                                                  CKERR(r);
    r = env->set_lg_dir(env, "log");                                             CKERR(r);
//    r = env->set_default_bt_compare(env, int64_dbt_cmp);                         CKERR(r);
    r = env->set_default_bt_compare(env, int_dbt_cmp);                           CKERR(r);
    generate_permute_tables();
    r = env->set_generate_row_callback_for_put(env, put_multiple_generate);      CKERR(r);
    int envflags = DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_INIT_TXN | DB_CREATE | DB_PRIVATE | DB_INIT_LOG;
    r = env->open(env, ENVDIR, envflags, S_IRWXU+S_IRWXG+S_IRWXO);               CKERR(r);
    env->set_errfile(env, stderr);
    //Disable auto-checkpointing
    r = env->checkpointing_set_period(env, 0);                                   CKERR(r);

    DB    *src_db = NULL;
    char *src_name="src.db";
    r = db_create(&src_db, env, 0);                                                             CKERR(r);
    r = src_db->open(src_db, NULL, src_name, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);   CKERR(r);
    DB_TXN *txn;
    r = env->txn_begin(env, NULL, &txn, 0);                                      CKERR(r);
    r = generate_initial_table(src_db, txn, NUM_ROWS);                           CKERR(r);
    r = txn->commit(txn, DB_TXN_SYNC);                                           CKERR(r);

    DBT desc;
    dbt_init(&desc, "foo", sizeof("foo"));

    DB *dbs[NUM_DBS];
    int idx[MAX_DBS];
    for (int i = 0; i < NUM_DBS; i++) {
        idx[i] = i+1;
        r = db_create(&dbs[i], env, 0); CKERR(r);
        dbs[i]->app_private = &idx[i];
        char key_name[32]; 
        sprintf(key_name, "key%d", i);
        r = dbs[i]->open(dbs[i], NULL, key_name, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);   CKERR(r);
        IN_TXN_COMMIT(env, NULL, txn_desc, 0, {
            CHK(dbs[i]->change_descriptor(dbs[i], txn_desc, &desc, 0));
        });
    }

    // -------------------------- //
    if (1) test_indexer(src_db, dbs);
    // -------------------------- //

    for(int i=0;i<NUM_DBS;i++) {
        r = dbs[i]->close(dbs[i], 0);                                            CKERR(r);
    }

    r = src_db->close(src_db, 0);                                                CKERR(r);
    r = env->close(env, 0);                                                      CKERR(r);
}

// ------------ infrastructure ----------

int test_main(int argc, char * const argv[]) {
    default_parse_args(argc, argv);
    run_test();
    return 0;
}
