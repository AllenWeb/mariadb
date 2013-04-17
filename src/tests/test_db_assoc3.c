/* -*- mode: C; c-basic-offset: 4 -*- */
#include <toku_portability.h>
/* Primary with two associated things. */

#include <assert.h>
#include <db.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory.h>
#include <toku_portability.h>

#include "test.h"

enum mode {
    MODE_DEFAULT, MODE_DB_CREATE, MODE_MORE
} mode;



/* Primary is a map from a UID which consists of a random number followed by the current time. */

struct timestamp {
    unsigned int tv_sec; /* in newtork order */
    unsigned int tv_usec; /* in network order */
};


struct primary_key {
    int rand; /* in network order */
    struct timestamp ts;
};

struct name_key {
    unsigned char* name;
};

struct primary_data {
    struct timestamp expiretime; /* not valid if doesexpire==0 */
    unsigned char doesexpire;
    struct name_key name;
};

static void
free_pd (struct primary_data *pd) {
    toku_free(pd->name.name);
    toku_free(pd);
}

static void
write_uchar_to_dbt (DBT *dbt, const unsigned char c) {
    assert(dbt->size+1 <= dbt->ulen);
    ((char*)dbt->data)[dbt->size++]=c;
}

static void
write_uint_to_dbt (DBT *dbt, const unsigned int v) {
    write_uchar_to_dbt(dbt, (unsigned char)((v>>24)&0xff));
    write_uchar_to_dbt(dbt, (unsigned char)((v>>16)&0xff));
    write_uchar_to_dbt(dbt, (unsigned char)((v>> 8)&0xff));
    write_uchar_to_dbt(dbt, (unsigned char)((v>> 0)&0xff));
}

static void
write_timestamp_to_dbt (DBT *dbt, const struct timestamp *ts) {
    write_uint_to_dbt(dbt, ts->tv_sec);
    write_uint_to_dbt(dbt, ts->tv_usec);
}

static void
write_pk_to_dbt (DBT *dbt, const struct primary_key *pk) {
    write_uint_to_dbt(dbt, pk->rand);
    write_timestamp_to_dbt(dbt, &pk->ts);
}

static void
write_name_to_dbt (DBT *dbt, const struct name_key *nk) {
    int i;
    for (i=0; 1; i++) {
	write_uchar_to_dbt(dbt, nk->name[i]);
	if (nk->name[i]==0) break;
    }
}

static void
write_pd_to_dbt (DBT *dbt, const struct primary_data *pd) {
    write_timestamp_to_dbt(dbt, &pd->expiretime);
    write_uchar_to_dbt(dbt, pd->doesexpire);
    write_name_to_dbt(dbt, &pd->name);
}

static void
read_uchar_from_dbt (const DBT *dbt, unsigned int *off, unsigned char *uchar) {
    assert(*off < dbt->size);
    *uchar = ((unsigned char *)dbt->data)[(*off)++];
}

static void
read_uint_from_dbt (const DBT *dbt, unsigned int *off, unsigned int *uintptr) {
    unsigned char a,b,c,d;
    read_uchar_from_dbt(dbt, off, &a);
    read_uchar_from_dbt(dbt, off, &b);
    read_uchar_from_dbt(dbt, off, &c);
    read_uchar_from_dbt(dbt, off, &d);
    *uintptr = (a<<24)+(b<<16)+(c<<8)+d;
}

static void
read_timestamp_from_dbt (const DBT *dbt, unsigned int *off, struct timestamp *ts) {
    read_uint_from_dbt(dbt, off, &ts->tv_sec);
    read_uint_from_dbt(dbt, off, &ts->tv_usec);
}

static void
read_name_from_dbt (const DBT *dbt, unsigned int *off, struct name_key *nk) {
    unsigned char buf[1000];
    int i;
    for (i=0; 1; i++) {
	read_uchar_from_dbt(dbt, off, &buf[i]);
	if (buf[i]==0) break;
    }
    nk->name=(unsigned char*)(toku_strdup((char*)buf));
}

static void
read_pd_from_dbt (const DBT *dbt, unsigned int *off, struct primary_data *pd) {
    read_timestamp_from_dbt(dbt, off, &pd->expiretime);
    read_uchar_from_dbt(dbt, off, &pd->doesexpire);
    read_name_from_dbt(dbt, off, &pd->name);
}

static int
name_offset_in_pd_dbt (void) {
    return 9;
}

static int
name_callback (DB *UU(secondary), const DBT * UU(key), const DBT *data, DBT *result) {
    struct primary_data *pd = toku_malloc(sizeof(*pd));
    unsigned int off=0;
    read_pd_from_dbt(data, &off, pd);
    static int buf[1000];

    result->ulen=1000;
    result->data=buf;
    result->size=0;
    write_name_to_dbt(result,  &pd->name);
    free_pd(pd);
    return 0;
}

static int
expire_callback (DB *UU(secondary), const DBT *UU(key), const DBT *data, DBT *result) {
    struct primary_data *d = data->data;
    if (d->doesexpire) {
	result->flags=0;
	result->size=sizeof(struct timestamp);
	result->data=&d->expiretime;
	return 0;
    } else {
	return DB_DONOTINDEX;
    }
}


#if 0
// Calculate things with an in-memory database.
static int n_names=0;
static unsigned char **names=0; // An unsorted array of all the names.

static void dbg_name_insert (unsigned char *name) {
    name = (unsigned char*)strdup((char*)name);
    n_names++;
    if (names==0) {
	names=malloc(sizeof(*names));
    } else {
	names = toku_realloc(names, n_names*sizeof(*names));
    }
    names[n_names-1]=name;
}
static void dbg_name_delete (char *name) {
    int i;
    for (i=0; i<n_names; i++) {
	if (strcmp((char*)name, (char*)names[i])==0) {
	    names[i] = names[n_names-1];
	    n_names--;
	    return;
	}
    }
    assert(0);
}
#else
static inline void dbg_name_insert(unsigned char*name __attribute__((__unused__))) {}
static inline void dbg_name_delete(char*name __attribute__((__unused__))) {}
#endif



// The expire_key is simply a timestamp.

DB_ENV *dbenv;
DB *dbp,*namedb,*expiredb;

DB_TXN * const null_txn=0;

DBC *delete_cursor=0, *name_cursor=0;

// We use a cursor to count the names.
int cursor_count_n_items=0; // The number of items the cursor saw as it scanned over.
int calc_n_items=0;        // The number of items we expect the cursor to acount
int count_all_items=0;      // The total number of items
DBT nc_key,nc_data;


static void
create_databases (void) {
    int r;

    r = db_env_create(&dbenv, 0);                                                            CKERR(r);
    r = dbenv->open(dbenv, ENVDIR, DB_PRIVATE|DB_INIT_MPOOL|DB_CREATE, 0);                      CKERR(r);

    r = db_create(&dbp, dbenv, 0);                                                           CKERR(r);
    r = dbp->open(dbp, null_txn, "primary.db", NULL, DB_BTREE, DB_CREATE, 0600);             CKERR(r);

    r = db_create(&namedb, dbenv, 0);                                                        CKERR(r);
    r = namedb->set_flags(namedb, DB_DUP|DB_DUPSORT);
    r = namedb->open(namedb, null_txn, "name.db", NULL, DB_BTREE, DB_CREATE, 0600);          CKERR(r);

    r = db_create(&expiredb, dbenv, 0);                                                      CKERR(r);
    r = expiredb->set_flags(expiredb, DB_DUP|DB_DUPSORT);
    r = expiredb->open(expiredb, null_txn, "expire.db", NULL, DB_BTREE, DB_CREATE, 0600);    CKERR(r);
    
    r = dbp->associate(dbp, NULL, namedb, name_callback, 0);                                 CKERR(r);
    r = dbp->associate(dbp, NULL, expiredb, expire_callback, 0);                             CKERR(r);
}

static void
close_databases (void) {
    int r;
    if (delete_cursor) {
	r = delete_cursor->c_close(delete_cursor); CKERR(r);
    }
    if (name_cursor) {
	r = name_cursor->c_close(name_cursor);     CKERR(r);
    }
    if (nc_key.data) toku_free(nc_key.data);
    if (nc_data.data) toku_free(nc_data.data);
    r = namedb->close(namedb, 0);     CKERR(r);
    r = dbp->close(dbp, 0);           CKERR(r);
    r = expiredb->close(expiredb, 0); CKERR(r);
    r = dbenv->close(dbenv, 0);       CKERR(r);
}
    

static int tod_counter=0;


static void
gettod (struct timestamp *ts) {
#if 0
    struct timeval tv;
    int r = gettimeofday(&tv, 0);
    assert(r==0);
    ts->tv_sec  = htonl(tv.tv_sec);
    ts->tv_usec = htonl(tv.tv_usec);
#else
    ts->tv_sec  = 0;
    ts->tv_usec = tod_counter++;
#endif
}

static void
setup_for_db_create (void) {

    // Remove name.db and then rebuild it with associate(... DB_CREATE)

    int r=unlink(ENVDIR "/name.db");
    assert(r==0);

    r = db_env_create(&dbenv, 0);                                                    CKERR(r);
    r = dbenv->open(dbenv, ENVDIR, DB_PRIVATE|DB_INIT_MPOOL, 0);                        CKERR(r);

    r = db_create(&dbp, dbenv, 0);                                                   CKERR(r);
    r = dbp->open(dbp, null_txn, "primary.db", NULL, DB_BTREE, 0, 0600);             CKERR(r);

    r = db_create(&namedb, dbenv, 0);                                                CKERR(r);
    r = namedb->open(namedb, null_txn, "name.db", NULL, DB_BTREE, DB_CREATE, 0600);  CKERR(r);

    r = db_create(&expiredb, dbenv, 0);                                              CKERR(r);
    r = expiredb->open(expiredb, null_txn, "expire.db", NULL, DB_BTREE, 0, 0600);    CKERR(r);
    
    r = dbp->associate(dbp, NULL, expiredb, expire_callback, 0);                     CKERR(r);
    r = dbp->associate(dbp, NULL, namedb, name_callback, DB_CREATE);                 CKERR(r);

}

static int
count_entries (DB *db) {
    DBC *dbc;
    int r = db->cursor(db, null_txn, &dbc, 0);                                       CKERR(r);
    DBT key,data;
    memset(&key,  0, sizeof(key));    
    memset(&data, 0, sizeof(data));
    int n_found=0;
    for (r = dbc->c_get(dbc, &key, &data, DB_FIRST);
	 r==0;
	 r = dbc->c_get(dbc, &key, &data, DB_NEXT)) {
	n_found++;
    }
    assert(r==DB_NOTFOUND);
    r=dbc->c_close(dbc);                                                             CKERR(r);
    return n_found;
}

static int
count_entries_and_max_tod (DB *db, int *tod) {
    DBC *dbc;
    int r = db->cursor(db, null_txn, &dbc, 0);                                       CKERR(r);
    DBT key,data;
    memset(&key,  0, sizeof(key));    
    memset(&data, 0, sizeof(data));
    int n_found=0;
    *tod=0;
    for (r = dbc->c_get(dbc, &key, &data, DB_FIRST);
	 r==0;
	 r = dbc->c_get(dbc, &key, &data, DB_NEXT)) {
	int thistod = ntohl(*(2+(unsigned int*)key.data));
	if (thistod>*tod) *tod=thistod;
	n_found++;
	dbg_name_insert(name_offset_in_pd_dbt()+(unsigned char*)data.data);
    }
    (*tod)++;
    assert(r==DB_NOTFOUND);
    r=dbc->c_close(dbc);                                                             CKERR(r);
    return n_found;
}

static void
do_create (void) {
    setup_for_db_create();
    // Now check to see if the number of names matches the number of associated things.
    int n_named = count_entries(namedb);
    int n_prim  = count_entries(dbp);
    assert(n_named==n_prim);
}

static void
insert_person (void) {
    const int extrafortod = 20;
    int namelen = 5+extrafortod+myrandom()%245;
    struct primary_key  pk;
    struct primary_data pd;
    char keyarray[1000], dataarray[1000]; 
    unsigned char namearray[1000];
    pk.rand = myrandom();
    gettod(&pk.ts);
    pd.expiretime   = pk.ts;
    pd.expiretime.tv_sec += 24*60*60*366;
    pd.doesexpire =(char)(myrandom()%10==0);
    int i;
    pd.name.name = namearray;
    pd.name.name[0] = (char)('A'+myrandom()%26);
    for (i=1; i<namelen; i++) {
	pd.name.name[i] = (char)('a'+myrandom()%26);
    }
    int count=snprintf((char*)&pd.name.name[i], extrafortod, "%u.%u", pk.ts.tv_sec, pk.ts.tv_usec);
    assert(count<extrafortod);
    DBT key,data;
    memset(&key,0,sizeof(DBT));
    memset(&data,0,sizeof(DBT));
    key.data = keyarray;
    key.ulen = 1000;
    key.size = 0;
    data.data = dataarray;
    data.ulen = 1000;
    data.size = 0;
    write_pk_to_dbt(&key, &pk);
    write_pd_to_dbt(&data, &pd);
    {
	char *dt = data.data;
	if (0) fprintf(stderr, "put %2d%c %s\n", dt[7], dt[8]?'e':' ', dt+9);
    }
    int r=dbp->put(dbp, null_txn, &key, &data,0);   CKERR(r);
    dbg_name_insert(namearray);
    // If the cursor is to the left of the current item, then increment count_items
    {
	int compare=strcmp((char*)namearray, nc_key.data);
	//fprintf(stderr, "%s:%d compare=%d insert %s, cursor at %s\n", __FILE__, __LINE__, compare, namearray, (char*)nc_key.data);
	if (compare>0) calc_n_items++;
	count_all_items++;
    }
}

static void
delete_oldest_expired (void) {
    int r;
    int r3=myrandom()%3;
    if (delete_cursor==0) {
	r = expiredb->cursor(expiredb, null_txn, &delete_cursor, 0); CKERR(r);
	
    }
    DBT key,pkey,data, savepkey;
    memset(&key, 0, sizeof(key));
    memset(&pkey, 0, sizeof(pkey));
    memset(&data, 0, sizeof(data));
    r = delete_cursor->c_pget(delete_cursor, &key, &pkey, &data, DB_FIRST);
    if (r==DB_NOTFOUND) return;
    CKERR(r);
    {
	char *dt=data.data;
	char *deleted_key = dt+name_offset_in_pd_dbt();
	int compare=strcmp(deleted_key, nc_key.data);
	dbg_name_delete(deleted_key);
	if (0) fprintf(stderr, "del %2d%c %s\n", dt[7], dt[8]?'e':' ', dt+9);
	if (compare>0) {
	    //fprintf(stderr, "%s:%d r3=%d compare=%d count=%d cacount=%d cucount=%d deleting %s cursor=%s\n", __FILE__, __LINE__, r3, compare, count_all_items, calc_n_items, cursor_count_n_items, deleted_key, (char*)nc_key.data);
	    calc_n_items--;
	}
	count_all_items--;
    }
    savepkey = pkey;
    savepkey.data = toku_malloc(pkey.size);
    memcpy(savepkey.data, pkey.data, pkey.size);
    switch (r3) {
    case 0:
	r = delete_cursor->c_del(delete_cursor, 0);  CKERR(r);
	break;
    case 1:
	r = expiredb->del(expiredb, null_txn, &key, 0); CKERR(r);
	break;
    case 2:
	r = dbp->del(dbp, null_txn, &pkey, 0);   CKERR(r);
	break;
    default:
	printf("r3=%d\n", r3);
	assert(0);
    }
    // Make sure it's really gone.
    r = delete_cursor->c_get(delete_cursor, &key, &data, DB_CURRENT);
    assert(r==DB_KEYEMPTY);
    r = dbp->get(dbp, null_txn, &savepkey, &data, 0);
    assert(r==DB_NOTFOUND);
    toku_free(savepkey.data);
}

// Use a cursor to step through the names.
static void
step_name (void) {
    int r;
    if (name_cursor==0) {
	r = namedb->cursor(namedb, null_txn, &name_cursor, 0); CKERR(r);
    }
    r = name_cursor->c_get(name_cursor, &nc_key, &nc_data, DB_NEXT); // an uninitialized cursor should do a DB_FIRST.
    if (r==0) {
	char *dt = nc_data.data;
	cursor_count_n_items++;
	if (0) fprintf(stderr, "%4d %4d crs %2d%c %s\n", cursor_count_n_items, calc_n_items, dt[7], dt[8] ? 'e' : ' ', dt+name_offset_in_pd_dbt());
    } else if (r==DB_NOTFOUND) {
	// Got to the end.
	// fprintf(stderr, "%s:%d Got to end count=%d curscount=%d all=%d\n", __FILE__, __LINE__, calc_n_items, cursor_count_n_items, count_all_items);
	//printf("n_names=%d cursor_count_n_items=%d calc_n_items=%d\n", n_names, cursor_count_n_items, calc_n_items);
	assert(cursor_count_n_items==calc_n_items);
	r = name_cursor->c_get(name_cursor, &nc_key, &nc_data, DB_FIRST);
	if (r==DB_NOTFOUND) {
	    nc_key.data = toku_realloc(nc_key.data, 1);
	    ((char*)nc_key.data)[0]=0;
	    cursor_count_n_items=0;
	} else {
	    cursor_count_n_items=1;
	    char *dt = nc_data.data;
	    if (0) fprintf(stderr, "crs %2d%c %s\n", dt[7], dt[8] ? 'e' : ' ', dt+9);
	}
	calc_n_items = count_all_items;
    }
}

int cursor_load=2; /* Set this to a higher number to do more cursor work for every insertion.   Needed to get to the end. */

static void
activity (void) {
    if (myrandom()%20==0) {
	// Delete the oldest expired one.  Keep the cursor open
	delete_oldest_expired();
    } else if (myrandom()%cursor_load==0) {
	insert_person();
    } else {
	step_name();
    }
    //assert(count_all_items==count_entries(dbp));
}
		       

static __attribute__((__noreturn__))
void usage (const char *argv1) {
    fprintf(stderr, "Usage:\n %s [ --DB-CREATE | --more ] [ --tod=N ] [ --seed=SEED ] [ --count=count ] \n", argv1);
    exit(1);
}

static int
maybe_parse_intarg (const char *progname, const char *arg, const char *cmdname, int *result) {
    int len = strlen(cmdname);
    if (strncmp(arg, cmdname, len)==0) {
	errno=0;
	char *endptr;
	*result = strtoul(arg+len, &endptr, 10);
	if (errno!=0 || *endptr!=0 || endptr==arg+len) {
	    usage(progname);
	}
	return 1;
    } else {
	return 0;
    }
}

int
test_main (int argc, const char *argv[]) {
    const char *progname=argv[0];
    int useseed;
    int activity_count = 100000;

    {
	struct timeval tv;
	gettimeofday(&tv, 0);
	useseed = tv.tv_sec+tv.tv_usec*997;  // magic:  997 is a prime, and a million (microseconds/second) times 997 is still 32 bits.
    }

    memset(&nc_key, 0, sizeof(nc_key));
    memset(&nc_data, 0, sizeof(nc_data));
    nc_key.flags = DB_DBT_REALLOC;
    nc_key.data = toku_malloc(1); // Iniitalize it.
    ((char*)nc_key.data)[0]=0;
    nc_data.flags = DB_DBT_REALLOC;
    nc_data.data = toku_malloc(1); // Iniitalize it.


    mode = MODE_DEFAULT;
    argv++; argc--;
    while (argc>0) {
	if (strcmp(argv[0], "--DB_CREATE")==0) {
	    mode = MODE_DB_CREATE;
	} else if (strcmp(argv[0], "--more")==0) {
	    mode = MODE_MORE;
	} else if (strcmp(argv[0], "-v")==0) {
	    verbose++;
	} else if (strcmp(argv[0], "-q")==0) {
	    verbose--;
	    if (verbose<0) verbose=0;
	} else if (maybe_parse_intarg(progname, argv[0], "--seed=", &useseed)
		   || maybe_parse_intarg(progname, argv[0], "--count=", &activity_count)) {
	    /* nothing */
	} else {
	    usage(progname);
	}
	argc--; argv++;
    }

    if (verbose) printf("seed=%d\n", useseed);
    mysrandom(useseed);

    switch (mode) {
    case MODE_DEFAULT:
	system("rm -rf " ENVDIR);
	toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO); 
	create_databases();
	{
	    int i;
	    for (i=0; i<activity_count; i++)
		activity();
	}
	break;
    case MODE_MORE:
	create_databases();
	calc_n_items = count_all_items = count_entries_and_max_tod(dbp, &tod_counter);
	//printf("tod=%d\n", tod_counter);
	{
	    int i;
	    cursor_load = 8*(1+2*count_all_items/activity_count);
	    if (verbose) printf("%s:%d count=%d cursor_load=%d\n", __FILE__, __LINE__, count_all_items, cursor_load);
	    for (i=0; i<activity_count; i++)
		activity();
	}
	break;
    case MODE_DB_CREATE:
	do_create();
	break;
    }

    close_databases();

    //fprintf(stderr, "now: --tod=%d\n", tod_counter);

    return 0;
}

