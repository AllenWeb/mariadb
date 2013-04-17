/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"
/* A simple case to see if recovery works. */

#include <db.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory.h>


static void test (void) {
    int r;
    system("rm -rf " ENVDIR);
    r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);                                                     assert(r==0);

    DB_ENV *env;
    DB_TXN *tid;
    DB     *db;
    DBT key,data;

    r=db_env_create(&env, 0);                                                  assert(r==0);
    env->set_errfile(env, stderr);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE|DB_THREAD, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);

    r=db_create(&db, env, 0);                                                  CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0);                                         assert(r==0);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO);               CKERR(r);
    r=tid->commit(tid, 0);                                                     assert(r==0);

    r=env->txn_begin(env, 0, &tid, 0);                                         assert(r==0);
    int i;
    const int N=1000;
    char *keys[N];
    char *vals[N];
    for (i=0; i<N; i++) {
	char ks[100];  snprintf(ks, sizeof(ks), "k%09ld.%d", random(), i);
	char vs[1000]; snprintf(vs, sizeof(vs), "v%d.%0*d", i, (int)(sizeof(vs)-100), i);
	keys[i]=toku_strdup(ks);
	vals[i]=toku_strdup(vs);
	r=db->put(db, tid, dbt_init(&key, ks, strlen(ks)+1), dbt_init(&data, vs, strlen(vs)+1), 0);    assert(r==0);
    }
    r=tid->commit(tid, 0);                                                     assert(r==0);

    r=db->close(db, 0);                                                        assert(r==0);

    // dont close the env. we want recovery to run over the entire log and rebuild the database
    // r=env->close(env, 0);                                                      assert(r==0);

    unlink(ENVDIR "/foo.db");

    r=db_env_create(&env, 0);                                                  assert(r==0);
    env->set_errfile(env, stderr);
    r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE|DB_THREAD|DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r=env->txn_begin(env, 0, &tid, 0);                                         assert(r==0);
    r=db_create(&db, env, 0);                                                  CKERR(r);
    r=db->open(db, tid, "foo.db", 0, DB_BTREE, 0, S_IRWXU+S_IRWXG+S_IRWXO);                       CKERR(r);
    for (i=0; i<N; i++) {
	r=db->get(db, tid, dbt_init(&key, keys[i], 1+strlen(keys[i])), dbt_init_malloc(&data), 0);     assert(r==0);
	assert(strcmp(data.data, vals[i])==0);
	toku_free(data.data);
	data.data=0;
	toku_free(keys[i]);
	toku_free(vals[i]);
    }
    r=tid->commit(tid, 0);                                                     assert(r==0);
    toku_free(data.data);
    r=db->close(db, 0);                                                        CKERR(r);
    r=env->close(env, 0);                                                      CKERR(r);
}

int
test_main (int argc __attribute__((__unused__)), char *argv[] __attribute__((__unused__))) {
    test();
    return 0;
}
    
