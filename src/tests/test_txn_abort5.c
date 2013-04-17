/* -*- mode: C; c-basic-offset: 4 -*- */
#include <toku_portability.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <toku_portability.h>
#include <db.h>
#include "test.h"

static void
test_txn_abort (int n) {
    if (verbose) printf("test_txn_abort:%d\n", n);

    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    int r;
    DB_ENV *env;
    r = db_env_create(&env, 0); assert(r == 0);
    r = env->set_data_dir(env, ENVDIR);
    r = env->set_lg_dir(env, ENVDIR);
    r = env->open(env, 0, DB_INIT_MPOOL + DB_INIT_LOG + DB_INIT_LOCK + DB_INIT_TXN + DB_PRIVATE + DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); 
    if (r != 0) printf("%s:%d:%d:%s\n", __FILE__, __LINE__, r, db_strerror(r));
    assert(r == 0);

    DB_TXN *txn = 0;
    r = env->txn_begin(env, 0, &txn, 0); assert(r == 0);

    DB *db;
    r = db_create(&db, env, 0); assert(r == 0);
    r = db->open(db, txn, "test.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);
    r = txn->commit(txn, 0); assert(r == 0);

    r = env->txn_begin(env, 0, &txn, 0); assert(r == 0);
    int i;
    for (i=0; i<n; i++) {
        DBT key, val;
        r = db->put(db, txn, dbt_init(&key, &i, sizeof i), dbt_init(&val, &i, sizeof i), 0); 
        if (r != 0) printf("%s:%d:%d:%s\n", __FILE__, __LINE__, r, db_strerror(r));
        assert(r == 0);
    }
    r = txn->abort(txn); 
#if 0
    assert(r == 0);
#else
    if (r != 0) printf("%s:%d:abort:%d\n", __FILE__, __LINE__, r);
#endif
    /* walk the db, should be empty */
    r = env->txn_begin(env, 0, &txn, 0); assert(r == 0);
    DBC *cursor;
    r = db->cursor(db, txn, &cursor, 0); assert(r == 0);
    DBT key; memset(&key, 0, sizeof key);
    DBT val; memset(&val, 0, sizeof val);
    r = cursor->c_get(cursor, &key, &val, DB_FIRST); 
    assert(r == DB_NOTFOUND);
    r = cursor->c_close(cursor); assert(r == 0);
    r = txn->commit(txn, 0);

    r = db->close(db, 0); assert(r == 0);
    r = env->close(env, 0); assert(r == 0);
}

int
test_main(int argc, const char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            verbose++;
            continue;
        }
    }
    for (i=1; i<100; i++) 
        test_txn_abort(i);
    return 0;
}
