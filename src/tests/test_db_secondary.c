/* -*- mode: C; c-basic-offset: 4 -*- */
#include <toku_portability.h>
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <memory.h>
#include <toku_portability.h>
#include <db.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

#include "test.h"

// ENVDIR is defined in the Makefile

static int
dbtcmp (DBT *dbt1, DBT *dbt2) {
    int r;
    
    r = dbt1->size - dbt2->size;  if (r) return r;
    return memcmp(dbt1->data, dbt2->data, dbt1->size);
}

struct student_record {
	char student_id[4];
	char last_name[15];
	char first_name[15];
};
#define SPACES "               "
DB *dbp;
DB *sdbp;
DB_TXN *const null_txn = 0;
DB_ENV *dbenv;
/*
 * getname -- extracts a secondary key (the last name) from a primary
 * 	key/data pair
 */
static int
getname(DB *UU(secondary), const DBT *UU(pkey), const DBT *pdata, DBT *skey)
{
	/*
	 * Since the secondary key is a simple structure member of the
	 * record, we don't have to do anything fancy to return it.  If
	 * we have composite keys that need to be constructed from the
	 * record, rather than simply pointing into it, then the user's
	 * function might need to allocate space and copy data.  In
	 * this case, the DB_DBT_APPMALLOC flag should be set in the
	 * secondary key DBT.
	 */
	memset(skey, 0, sizeof(DBT));
	skey->data = ((struct student_record *)pdata->data)->last_name;
	skey->size = sizeof ((struct student_record *)pdata->data)->last_name;
	return (0);
}

static void
second_setup (void) {
    int r;

    /* Open/create primary */
    r = db_create(&dbp, dbenv, 0);                                              CKERR(r);
    dbp->set_errfile(dbp,0); // Turn off those annoying errors
    r = dbp->open(dbp, NULL, ENVDIR "/students.db", NULL, DB_BTREE, DB_CREATE, 0600);   CKERR(r);

    /*
     * Open/create secondary. Note that it supports duplicate data
     * items, since last names might not be unique.
     */
    r = db_create(&sdbp, dbenv, 0);                                             CKERR(r);
    sdbp->set_errfile(sdbp,0); // Turn off those annoying errors
    r = sdbp->set_flags(sdbp, DB_DUP | DB_DUPSORT);                             CKERR(r);
    r = sdbp->open(sdbp, NULL, ENVDIR "/lastname.db", NULL, DB_BTREE, DB_CREATE, 0600); CKERR(r);
    

    /* Associate the secondary with the primary. */
    r = dbp->associate(dbp, NULL, sdbp, getname, 0);                            CKERR(r);
}

static void
setup_student (struct student_record *s) {
    memset(s, 0, sizeof(struct student_record));
    memcpy(&s->student_id, "WC42"      SPACES,  sizeof(s->student_id));
    //Padded with enough spaces to fill out last/first name.
    memcpy(&s->last_name,  "Churchill" SPACES,  sizeof(s->last_name));
    memcpy(&s->first_name, "Winston"   SPACES,  sizeof(s->first_name));
}

static void
insert_test (void) {
    struct student_record s;
    DBT data;
    DBT key;
    DBT skey;
    DBT testdata;
    DBT testkey;
    int r;
    
    setup_student(&s);
    memset(&testdata,   0, sizeof(DBT));
    memset(&testkey,    0, sizeof(DBT));
    memset(&skey,       0, sizeof(DBT));
    memset(&key,        0, sizeof(DBT));
    memset(&data,       0, sizeof(DBT));
    key.data = "WC42";
    key.size = strlen("WC42");
    data.data = &s;
    data.size = sizeof(s);
    //Set up secondary key
    r = getname(sdbp, &key, &data, &skey);              CKERR(r);

    //Insert into primary.        
    r = dbp->put(dbp, null_txn, &key, &data, 0);        CKERR(r);
    
    //Make certain we fail to insert into secondary.
    r = sdbp->put(sdbp, null_txn, &key, &data, 0);      assert(r == EINVAL);

    /* Try to get it from primary. */
    r = dbp->get(dbp, null_txn, &key, &testdata, 0);    CKERR(r);
    r = dbtcmp(&data, &testdata);                       assert(r == 0); // Not ckerr
    
    /* Try to get it from secondary. */
    r = sdbp->get(sdbp, null_txn, &skey, &testdata, 0); CKERR(r);
    r = dbtcmp(&data, &testdata);                       assert(r == 0); // Not ckerr

    /* Try to pget from secondary */ 
    r = sdbp->pget(sdbp, null_txn, &skey, &testkey, &testdata, 0);  CKERR(r);
    r = dbtcmp(&data, &testdata);                       assert(r == 0); // Not ckerr
    r = dbtcmp(&testkey, &key);                         assert(r == 0); // Not ckerr
    
    /* Make sure we fail 'pget' from primary */
    r = dbp->pget(dbp, null_txn, &key, &testkey, &data, 0);         assert(r == EINVAL);
}

static void
delete_from_primary (void) {
    int r;
    DBT key;

    memset(&key, 0, sizeof(DBT));
    key.data = "WC42";
    key.size = 4;
    r = dbp->del(dbp, null_txn, &key, 0);               CKERR(r);
}

static void
delete_from_secondary (void) {
    int r;
    DBT skey;
    DBT data;
    struct student_record s;

    setup_student(&s);
    memset(&skey, 0, sizeof(DBT));
    memset(&data, 0, sizeof(DBT));

    data.data = &s;
    data.size = sizeof(s);
    r = getname(sdbp, NULL, &data, &skey);              CKERR(r);
    r = sdbp->del(sdbp, null_txn, &skey, 0);            CKERR(r);
}

static void
verify_gone (void) {
    int r;
    DBT key;
    DBT skey;
    DBT data;
    struct student_record s;

    memset(&key, 0, sizeof(DBT));
    memset(&skey, 0, sizeof(DBT));
    memset(&data, 0, sizeof(DBT));
    key.data = "WC42";
    key.size = 4;

    /* Try (fail) to get it from primary. */
    r = dbp->get(dbp, null_txn, &key, &data, 0);        assert(r == DB_NOTFOUND);

    /* Try (fail) to get it from secondary. */
    setup_student(&s); memset(&data, 0, sizeof data); data.data = &s; data.size = sizeof s;
    r = getname(sdbp, NULL, &data, &skey);              CKERR(r);
    memset(&data, 0, sizeof data);
    r = sdbp->get(sdbp, null_txn, &skey, &data, 0);     assert(r == DB_NOTFOUND);

    /* Try (fail) to pget from secondary */ 
    setup_student(&s);  memset(&data, 0, sizeof data); data.data = &s; data.size = sizeof s;
    r = getname(sdbp, NULL, &data, &skey);              CKERR(r);
    memset(&data, 0, sizeof data);
    r = sdbp->pget(sdbp, null_txn, &skey, &key, &data, 0);assert(r == DB_NOTFOUND);
}

int
test_main(int argc, const char *argv[]) {
    parse_args(argc, argv);
    int r;

    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    second_setup();
    insert_test();
    delete_from_primary();
    verify_gone();
    insert_test();
    delete_from_secondary();
    verify_gone();
    
    r = dbp->close(dbp, 0);                             CKERR(r);
    r = sdbp->close(sdbp, 0);                           CKERR(r);
    return 0;
}
