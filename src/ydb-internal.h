#ifndef YDB_INTERNAL_H
#define YDB_INTERNAL_H

#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include "../include/db.h"
#include "../newbrt/brttypes.h"
#include "../newbrt/brt.h"
#include "../newbrt/list.h"
#include "./lock_tree/locktree.h"
#include "./lock_tree/db_id.h"
#include "./lock_tree/idlth.h"
#include <limits.h>

struct db_header {
    int n_databases; // Or there can be >=1 named databases.  This is the count.
    char *database_names; // These are the names
    BRT  *database_brts;  // These 
};

struct __toku_lock_tree;

struct __toku_db_internal {
    DB *db; // A pointer back to the DB.
    int freed;
    struct db_header *header;
    int database_number; // -1 if it is the single unnamed database.  Nonnengative number otherwise.
    char *fname;
    char *full_fname;
    //int fd;
    u_int32_t open_flags;
    int open_mode;
    BRT brt;
    FILENUM fileid;
    struct __toku_lock_tree* lt;
    toku_db_id* db_id;
    struct simple_dbt skey, sval; // static key and value
};

#if DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR == 1
typedef void (*toku_env_errcall_t)(const char *, char *);
#elif DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 3
typedef void (*toku_env_errcall_t)(const DB_ENV *, const char *, const char *);
#else
#error
#endif

struct __toku_db_env_internal {
    int is_panicked; // if nonzero, then its an error number
    char *panic_string;
    int ref_count;
    u_int32_t open_flags;
    int open_mode;
    toku_env_errcall_t errcall;
    void *errfile;
    const char *errpfx;
    char *dir;                  /* A malloc'd copy of the directory. */
    char *tmp_dir;
    char *lg_dir;
    char **data_dirs;
    u_int32_t n_data_dirs;
    //void (*noticecall)(DB_ENV *, db_notices);
    unsigned long cachetable_size;
    CACHETABLE cachetable;
    TOKULOGGER logger;
    toku_ltm* ltm;
};

struct __toku_db_txn_internal {
    //TXNID txnid64; /* A sixty-four bit txn id. */
    TOKUTXN tokutxn;
    toku_lth* lth;
    u_int32_t flags;
    DB_TXN *child, *next, *prev;
};

struct __toku_dbc_internal {
    BRT_CURSOR c;
    DB_TXN *txn;
    struct simple_dbt skey_s,sval_s;
    struct simple_dbt *skey,*sval;
};


/* *********************************************************

   Ephemeral locking

   ********************************************************* */
void toku_ydb_lock_init(void);
void toku_ydb_lock_destroy(void);
void toku_ydb_lock(void);
void toku_ydb_unlock(void);

/* *********************************************************

   Error handling

   ********************************************************* */

/* Exception handling */
/** Raise a C-like exception: currently returns an status code */
#define RAISE_EXCEPTION(status) {return status;}
/** Raise a C-like conditional exception: currently returns an status code 
    if condition is true */
#define RAISE_COND_EXCEPTION(cond, status) {if (cond) return status;}
/** Propagate the exception to the caller: if the status is non-zero,
    returns it to the caller */
#define PROPAGATE_EXCEPTION(status) ({if (status != 0) return status;})

/** Handle a panicked environment: return EINVAL if the env is panicked */
#define HANDLE_PANICKED_ENV(env) \
        RAISE_COND_EXCEPTION(toku_env_is_panicked(env), EINVAL)
/** Handle a panicked database: return EINVAL if the database env is panicked */
#define HANDLE_PANICKED_DB(db) HANDLE_PANICKED_ENV(db->dbenv)

/* */
void toku_ydb_error_all_cases(const DB_ENV * env, 
                              int error, 
                              BOOL include_stderrstring, 
                              BOOL use_stderr_if_nothing_else, 
                              const char *fmt, va_list ap)
    __attribute__((format (printf, 5, 0)))
    __attribute__((__visibility__("default"))); // this is needed by the C++ interface. 

int toku_ydb_do_error (const DB_ENV *dbenv, int error, const char *string, ...)
                       __attribute__((__format__(__printf__, 3, 4)));

/* Location specific debug print-outs */
void toku_ydb_barf(void);
void toku_ydb_notef(const char *, ...);

/* Environment related errors */
int toku_env_is_panicked(DB_ENV *dbenv);
void toku_locked_env_err(const DB_ENV * env, int error, const char *fmt, ...) 
                         __attribute__((__format__(__printf__, 3, 4)));

#endif
