/* -*- mode: C; c-basic-offset: 4 -*- */
#include <portability.h>
#include <db.h>
#include <sys/stat.h>
#include "test.h"

unsigned char N=8;

static int
fact (int n) {
    if (n<=2) return n;
    else return n*fact(n-1);
}

static void
swapc (unsigned char *a, unsigned char *b) {
    unsigned char tmp=*a;
    *a=*b;
    *b=tmp;
}

DB_ENV *env;
DB *db;

static void
run (int choice) {
    unsigned char v[N];
    int i;
    int r;
    for (i=0; i<N; i++) {
	v[i]=(char)i;
    }
    for (i=0; i<N; i++) {
	int nchoices=N-i;
	swapc(&v[i], &v[i+choice%nchoices]);
	choice=choice/nchoices;
    }
    if (0) {
	for (i=0; i<N; i++) {
	    printf("%d ", v[i]);
	}

	printf("\n");
    }
    DB_TXN *txn;
    {
	r=env->txn_begin(env, 0, &txn, 0);                            CKERR(r);
	for (i=0; i<N; i+=2) {
	    DBT kdbt,vdbt;
	    char key=v[i];
	    char val=v[i+1];
	    r=db->put(db, txn, dbt_init(&kdbt, &key, 1), dbt_init(&vdbt, &val, 1), DB_YESOVERWRITE); CKERR(r);
	}
	r=txn->commit(txn, DB_TXN_NOSYNC);                                        CKERR(r);
    }
    {
	r=env->txn_begin(env, 0, &txn, 0);                            CKERR(r);	
	DBC *c;
	r=db->cursor(db, txn, &c, 0);                                 CKERR(r);
	DBT kdbt,vdbt;
	dbt_init(&kdbt, 0, 0);
	dbt_init(&vdbt, 0, 0);
	i=0;
	while (0==(r=c->c_get(c, &kdbt, &vdbt, DB_NEXT))) {
	    i++;
	}
	CKERR2(r, DB_NOTFOUND);
	assert(i==N/2);
	r=c->c_close(c);                                                          CKERR(r);
	r=txn->commit(txn, DB_TXN_NOSYNC);                                        CKERR(r);
    }
    {
	r=env->txn_begin(env, 0, &txn, 0);                            CKERR(r);
	for (i=0; i<N; i+=2) {
	    DBT kdbt;
	    char key=v[i];
	    r=db->del(db, txn, dbt_init(&kdbt, &key, 1), 0);                      CKERR(r);
	}
	r=txn->commit(txn, DB_TXN_NOSYNC);                                        CKERR(r);
    }
    return;
#if 0
    char v101=101, v102=102, v1=1, v2=2;
    {
	r=env->txn_begin(env, 0, &txn, 0);                            CKERR(r);
	DBT k,v;
	r=db->put(db, txn, dbt_init(&k, &v1, 1), dbt_init(&v, &v101, 1), 0); CKERR(r);
	r=db->put(db, txn, dbt_init(&k, &v2, 1), dbt_init(&v, &v102, 1), 0); CKERR(r);
	r=txn->commit(txn, 0);                                        CKERR(r);
    }
    {
	r=env->txn_begin(env, 0, &txn, 0);                            CKERR(r);
	DBC *c;
	r=db->cursor(db, txn, &c, 0);                                 CKERR(r);
	DBT k,v;
	r=c->c_get(c, dbt_init_malloc(&k), dbt_init_malloc(&v), DB_FIRST); CKERR(r);
	assert(*(char*)k.data==v1); assert(*(char*)v.data==v101);
	r=c->c_get(c, dbt_init_malloc(&k), dbt_init_malloc(&v), DB_NEXT);  CKERR(r);
	assert(*(char*)k.data==v2); assert(*(char*)v.data==v102);
	r=c->c_get(c, dbt_init_malloc(&k), dbt_init_malloc(&v), DB_NEXT);  assert(r!=0);
	r=c->c_close(c);                                                   CKERR(r);
	r=txn->commit(txn, 0);                                             CKERR(r);
    }
#endif
}

int main(int argc, const char *argv[]) {
    parse_args(argc, argv);

    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    int r;

    DB_TXN *txn;
    {
        r = db_env_create(&env, 0);                                   CKERR(r);
	r=env->open(env, ENVDIR, DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_MPOOL|DB_INIT_TXN|DB_CREATE|DB_PRIVATE, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
	env->set_errfile(env, stderr);
	r=env->txn_begin(env, 0, &txn, 0);                            CKERR(r);
	r=db_create(&db, env, 0);                                     CKERR(r);
	r=db->open(db, txn, "foo.db", 0, DB_BTREE, DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO);  CKERR(r);
	r=txn->commit(txn, 0);                                        CKERR(r);
    }
    int i;
    for (i=0; i<fact(N); i++) {
	run(i);
    }
    {
	r=db->close(db, 0);                                           CKERR(r);
	r=env->close(env, 0);                                         CKERR(r);
    }

    return 0;
}
