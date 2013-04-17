// truncate a database within a transaction
// commit the transaction and verify that the database is empty

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <toku_portability.h>
#include <db.h>
#include "test.h"

static int
test_truncate_txn_commit (int n) {
    int r;
    
    DB_ENV *env;
    DB *db;
    DBC *cursor;

    r = db_env_create(&env, 0); assert(r == 0);
    r = env->open(env, ENVDIR, DB_INIT_MPOOL + DB_INIT_LOCK + DB_INIT_TXN + DB_PRIVATE + DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);

    int i;

    // populate the tree
    r = db_create(&db, env, 0); assert(r == 0);
    r = db->open(db, 0, "test.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);

    for (i=0; i<n; i++) {
        int k = htonl(i); int v = i;
        DBT key, val;
        r = db->put(db, 0, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0); assert(r == 0);
    }

    r = db->close(db, 0); assert(r == 0);

    r = db_create(&db, env, 0); assert(r == 0);
    r = db->open(db, 0, "test.db", 0, DB_UNKNOWN, DB_AUTO_COMMIT, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);

    DB_TXN *txn;

    // walk - expect n rows
    r = env->txn_begin(env, NULL, &txn, 0); assert(r == 0);
    i = 0;
    r = db->cursor(db, txn, &cursor, 0); assert(r == 0);
    while (1) {
        DBT key, val;
        r = cursor->c_get(cursor, dbt_init(&key, 0, 0), dbt_init(&val, 0, 0), DB_NEXT);
        if (r == DB_NOTFOUND) break;
        i++;
    }
    r = cursor->c_close(cursor); assert(r == 0);
    assert(i == n);

    r = txn->commit(txn, 0); assert(r == 0);

    // truncate the db
    r = env->txn_begin(env, NULL, &txn, 0); assert(r == 0);

    u_int32_t row_count = 0;
    r = db->truncate(db, txn, &row_count, 0); assert(r == 0);
    
    r = txn->commit(txn, 0); assert(r == 0);

    // walk - expect 0 rows
    r = env->txn_begin(env, 0, &txn, 0); assert(r == 0);

    i = 0;
    r = db->cursor(db, txn, &cursor, 0); assert(r == 0);
    while (1) {
        DBT key, val;
        r = cursor->c_get(cursor, dbt_init(&key, 0, 0), dbt_init(&val, 0, 0), DB_NEXT);
        if (r == DB_NOTFOUND) break;
        i++;
    }
    r = cursor->c_close(cursor); assert(r == 0);
    assert(i == 0);

    r = txn->commit(txn, 0); assert(r == 0);

    r = db->close(db, 0); assert(r == 0);

    // walk the tree - expect 0 rows
    r = db_create(&db, env, 0); assert(r == 0);
    r = db->open(db, 0, "test.db", 0, DB_UNKNOWN, DB_AUTO_COMMIT, S_IRWXU+S_IRWXG+S_IRWXO); assert(r == 0);

    r = env->txn_begin(env, 0, &txn, 0); assert(r == 0);

    i = 0;
    r = db->cursor(db, txn, &cursor, 0); assert(r == 0);
    while (1) {
        DBT key, val;
        r = cursor->c_get(cursor, dbt_init(&key, 0, 0), dbt_init(&val, 0, 0), DB_NEXT);
        if (r == DB_NOTFOUND) break;
        i++;
    }
    r = cursor->c_close(cursor); assert(r == 0);
    assert(i == 0);

    r = txn->commit(txn, 0); assert(r == 0);

    r = db->close(db, 0); assert(r == 0);

    r = env->close(env, 0); assert(r == 0);
    return 0;
}

int main(int argc, const char *argv[]) {
    parse_args(argc, argv);
    int nodesize = 1024*1024;
    int leafentry = 25;
    int n = (nodesize/leafentry) * 2;
    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);
    int r = test_truncate_txn_commit(n);
    return r;
}
