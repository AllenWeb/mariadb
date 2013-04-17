/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#include <toku_portability.h>
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <memory.h>
#include <toku_portability.h>
#include <db.h>

#include <errno.h>
#include <sys/stat.h>

#include "test.h"

// ENVDIR is defined in the Makefile
#define FNAME       "foo.tokudb"
char *name = NULL;

#define NUM         3
#define MAX_LENGTH  (1<<16)

int order[NUM+1];
u_int32_t length[NUM];
u_int8_t data[NUM][MAX_LENGTH];
DBT descriptors[NUM];
DB_ENV *env;

enum {NUM_DBS=2};
DB *dbs[NUM_DBS];
DB_TXN *txn = NULL;
DB_TXN *null_txn;
int last_open_descriptor = -1;

int abort_type;
int get_table_lock;
u_int64_t num_called = 0;


static void
verify_db_matches(void) {
    DB *db;
    int which;
    for (which = 0; which < NUM_DBS; which++) {
        db = dbs[which];
        if (db) {
            const DBT * dbt = &db->descriptor->dbt;

            if (last_open_descriptor<0) {
                assert(dbt->size == 0 && dbt->data == NULL);
            }
            else {
                assert(last_open_descriptor < NUM);
                assert(dbt->size == descriptors[last_open_descriptor].size);
                assert(!memcmp(dbt->data, descriptors[last_open_descriptor].data, dbt->size));
                assert(dbt->data != descriptors[last_open_descriptor].data);
            }
        }
    }
    
}

static int
verify_int_cmp (DB *dbp, const DBT *a, const DBT *b) {
    num_called++;
    verify_db_matches();
    int r = int_dbt_cmp(dbp, a, b);
    return r;
}

static void
open_db(int descriptor, int which) {
    /* create the dup database file */
    assert(dbs[which]==NULL);
    DB *db;
    int r = db_create(&db, env, 0);
    CKERR(r);
    dbs[which] = db;

    assert(abort_type >=0 && abort_type <= 2);
    if (abort_type==2 && !txn) {
        r = env->txn_begin(env, null_txn, &txn, 0);
            CKERR(r);
        last_open_descriptor = -1; //DB was destroyed at end of last close, did not hang around.
    }
    r = db->open(db, txn, FNAME, name, DB_BTREE, DB_CREATE, 0666);
    CKERR(r);
    if (descriptor >= 0) {
        assert(descriptor < NUM);
        if (txn) {
            CHK(db->change_descriptor(db, txn, &descriptors[descriptor], 0));
        }
        else {
            IN_TXN_COMMIT(env, NULL, txn_desc, 0, {
                CHK(db->change_descriptor(db, txn_desc, &descriptors[descriptor], 0));
            });
        }
        last_open_descriptor = descriptor;
    }
    verify_db_matches();
    if (abort_type!=2 && !txn) {
        r = env->txn_begin(env, null_txn, &txn, 0);
            CKERR(r);
    }
    assert(txn);
    if (get_table_lock) {
        r = db->pre_acquire_table_lock(db, txn);
        CKERR(r);
    }
}

static void
delete_db(void) {
    int which;
    for (which = 0; which < NUM_DBS; which++) {
        assert(dbs[which] == NULL);
    }
    int r = env->dbremove(env, NULL, FNAME, name, 0);
    if (abort_type==2) {
        CKERR2(r, ENOENT); //Abort deleted it
    }
    else CKERR(r);
    last_open_descriptor = -1;
}

static void
close_db(int which) {
    assert(dbs[which]!=NULL);
    DB *db = dbs[which];
    dbs[which] = NULL;

    int r;
    if (which==1) {
        r = db->close(db, 0);
        CKERR(r);
        return;
    }
    if (abort_type>0) {
        if (abort_type==2 && dbs[1]) {
            close_db(1);
        }
        r = db->close(db, 0);
        CKERR(r);
        r = txn->abort(txn);
        CKERR(r);
    }
    else {
        r = txn->commit(txn, 0);
        CKERR(r);
        r = db->close(db, 0);
        CKERR(r);
    }
    txn = NULL;
}

static void
setup_data(void) {
    int r = db_env_create(&env, 0);                                           CKERR(r);
    r = env->set_default_bt_compare(env, verify_int_cmp);                     CKERR(r);
    const int envflags = DB_CREATE|DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOCK |DB_THREAD |DB_PRIVATE;
    r = env->open(env, ENVDIR, envflags, S_IRWXU+S_IRWXG+S_IRWXO);        CKERR(r);
    int i;
    for (i=0; i < NUM; i++) {
        length[i] = i * MAX_LENGTH / (NUM-1);
        u_int32_t j;
        for (j = 0; j < length[i]; j++) {
            data[i][j] = (u_int8_t)(random() & 0xFF);
        }
        memset(&descriptors[i], 0, sizeof(descriptors[i]));
        descriptors[i].size = length[i];
        descriptors[i].data = &data[i][0];
    }
    last_open_descriptor = -1;
    txn = NULL;
}

static void
permute_order(void) {
    int i;
    for (i=0; i < NUM; i++) {
        order[i] = i;
    }
    for (i=0; i < NUM; i++) {
        int which = (random() % (NUM-i)) + i;
        int temp = order[i];
        order[i] = order[which];
        order[which] = temp;
    }
}

static void
test_insert (int n, int which) {
    if (which == -1) {
        for (which = 0; which < NUM_DBS; which++) {
            if (dbs[which]) {
                test_insert(n, which);
            }
        }
        return;
    }
    assert(dbs[which]!=NULL);
    DB *db = dbs[which];
    int i;
    static int last = 0;
    for (i=0; i<n; i++) {
        int k = last++;
        DBT key, val;
        u_int64_t called = num_called;
        int r = db->put(db, txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &i, sizeof i), 0);
        if (i>0) assert(num_called > called);
        CKERR(r);
    }
}

   
static void
runtest(void) {
    int r;
    r = system("rm -rf " ENVDIR);
    CKERR(r);
    r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO); assert(r==0);
    setup_data();
    permute_order();

    int i;
    /* Subsumed by rest of test.
    for (i=0; i < NUM; i++) {
        open_db(-1, 0);
        test_insert(i, 0);
        close_db(0);
        open_db(-1, 0);
        test_insert(i, 0);
        close_db(0);
        delete_db();
    }

    for (i=0; i < NUM; i++) {
        open_db(order[i], 0);
        test_insert(i, 0);
        close_db(0);
        open_db(-1, 0);
        test_insert(i, 0);
        close_db(0);
        open_db(order[i], 0);
        test_insert(i, 0);
        close_db(0);
        delete_db();
    }
    */

    //Upgrade descriptors along the way.  Need version to increase, so do not use 'order[i]'
    for (i=0; i < NUM; i++) {
        open_db(i, 0);
        test_insert(i, 0);
        close_db(0);
        open_db(-1, 0);
        test_insert(i, 0);
        close_db(0);
        open_db(i, 0);
        test_insert(i, 0);
        close_db(0);
    }
    delete_db();

    //Upgrade descriptors along the way. With two handles
    open_db(-1, 1);
    for (i=0; i < NUM; i++) {
        open_db(i, 0);
        test_insert(i, -1);
        close_db(0);
        open_db(-1, 0);
        test_insert(i, -1);
        close_db(0);
        open_db(i, 0);
        test_insert(i, -1);
        close_db(0);
    }
    if (dbs[1]) { 
        close_db(1);
    }
    delete_db();
    
    env->close(env, 0);
}


int
test_main(int argc, char *const argv[]) {
    parse_args(argc, argv);

    for (abort_type = 0; abort_type < 3; abort_type++) {
        for (get_table_lock = 0; get_table_lock < 2; get_table_lock++) {
            name = NULL;
            runtest();

            name = "bar";
            runtest();
            
        }
    }

    return 0;
}                                        

