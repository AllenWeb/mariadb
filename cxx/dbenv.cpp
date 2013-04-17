/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#include <stdarg.h>
#include <errno.h>
#include <toku_assert.h>
#include "../ft/fttypes.h"
#include <db_cxx.h>

DbEnv::DbEnv (u_int32_t flags)
    : do_no_exceptions((flags&DB_CXX_NO_EXCEPTIONS)!=0),
      errcall(NULL)
{
    int ret = db_env_create(&the_env, flags & ~DB_CXX_NO_EXCEPTIONS);
    assert(ret==0); // should do an error.
    the_env->api1_internal = this;
}

DbEnv::DbEnv(DB_ENV *env, u_int32_t flags)
    : do_no_exceptions((flags&DB_CXX_NO_EXCEPTIONS)!=0), _error_stream(0)
{
    the_env = env;
    if (env == 0) {
	DB_ENV *new_env;
	int ret = db_env_create(&new_env, flags & ~DB_CXX_NO_EXCEPTIONS);
	assert(ret==0); // should do an error.
	the_env = new_env;
    }
    the_env->api1_internal = this;
}

// If still open, close it.  In most cases, the caller should call close explicitly so that they can catch the exceptions.
DbEnv::~DbEnv(void)
{
    if (the_env!=NULL) {
	(void)the_env->close(the_env, 0);
	the_env = 0;
    }
}

int DbEnv::close(u_int32_t flags) {
    int ret = EINVAL;
    if (the_env)
        ret = the_env->close(the_env, flags);
    the_env = 0; /* get rid of the env ref, so we don't touch it (even if we failed, or when the destructor is called) */
    return maybe_throw_error(ret);
}

int DbEnv::open(const char *home, u_int32_t flags, int mode) {
    int ret = the_env->open(the_env, home, flags, mode);
    return maybe_throw_error(ret);
}

int DbEnv::set_cachesize(u_int32_t gbytes, u_int32_t bytes, int ncache) {
    int ret = the_env->set_cachesize(the_env, gbytes, bytes, ncache);
    return maybe_throw_error(ret);
}

int DbEnv::set_redzone(u_int32_t percent) {
    int ret = the_env->set_redzone(the_env, percent);
    return maybe_throw_error(ret);
}

int DbEnv::set_flags(u_int32_t flags, int onoff) {
    int ret = the_env->set_flags(the_env, flags, onoff);
    return maybe_throw_error(ret);
}

#if DB_VERSION_MAJOR<4 || (DB_VERSION_MAJOR==4 && DB_VERSION_MINOR<=4)
int DbEnv::set_lk_max(u_int32_t flags) {
    int ret = the_env->set_lk_max(the_env, flags);
    return maybe_throw_error(ret);
}
#endif

int DbEnv::txn_begin(DbTxn *parenttxn, DbTxn **txnp, u_int32_t flags) {
    DB_TXN *txn;
    int ret = the_env->txn_begin(the_env, parenttxn->get_DB_TXN(), &txn, flags);
    if (ret==0) {
	*txnp = new DbTxn(txn);
    }
    return maybe_throw_error(ret);
}

int DbEnv::set_default_bt_compare(bt_compare_fcn_type bt_compare_fcn) {
    int ret = the_env->set_default_bt_compare(the_env, bt_compare_fcn);
    return maybe_throw_error(ret);
}

int DbEnv::set_data_dir(const char *dir) {
    int ret = the_env->set_data_dir(the_env, dir);
    return maybe_throw_error(ret);
}

void DbEnv::set_errpfx(const char *errpfx) {
    the_env->set_errpfx(the_env, errpfx);
}

int DbEnv::maybe_throw_error(int err, DbEnv *env, int no_exceptions) throw (DbException) {
    if (err==0 || err==DB_NOTFOUND || err==DB_KEYEXIST) return err;
    if (no_exceptions) return err;
    if (err==DB_LOCK_DEADLOCK) {
	DbDeadlockException e(env);
	throw e;
    } else {
	DbException e(err);
	e.set_env(env);
	throw e;
    }
}

int DbEnv::maybe_throw_error(int err) throw (DbException) {
    return maybe_throw_error(err, this, do_no_exceptions);
}

extern "C" {
void toku_ydb_error_all_cases(const DB_ENV * env, 
                              int error, 
                              BOOL include_stderrstring, 
                              BOOL use_stderr_if_nothing_else, 
                              const char *fmt, va_list ap);
}

void DbEnv::err(int error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    toku_ydb_error_all_cases(the_env, error, TRUE, TRUE, fmt, ap);
    va_end(ap);
}

void DbEnv::set_errfile(FILE *errfile) {
    the_env->set_errfile(the_env, errfile);
}

int DbEnv::get_flags(u_int32_t *flagsp) {
    int ret = the_env->get_flags(the_env, flagsp);
    return maybe_throw_error(ret);
}

extern "C" void toku_db_env_errcall_c(const DB_ENV *dbenv_c, const char *errpfx, const char *msg) {
    DbEnv *dbenv = (DbEnv *) dbenv_c->api1_internal;
    dbenv->errcall(dbenv, errpfx, msg);
}

void DbEnv::set_errcall(void (*db_errcall_fcn)(const DbEnv *, const char *, const char *)) {
    errcall = db_errcall_fcn;
    the_env->set_errcall(the_env, toku_db_env_errcall_c);
}

extern "C" void toku_db_env_error_stream_c(const DB_ENV *dbenv_c, const char *errpfx, const char *msg) {
    DbEnv *dbenv = (DbEnv *) dbenv_c->api1_internal;
    if (dbenv->_error_stream) {
        if (errpfx) *(dbenv->_error_stream) << errpfx;
        if (msg) *(dbenv->_error_stream) << ":" << msg << "\n";
    }
}

void DbEnv::set_error_stream(std::ostream *new_error_stream) {
    _error_stream = new_error_stream;
    the_env->set_errcall(the_env, toku_db_env_error_stream_c);
}

int DbEnv::set_lk_max_locks(u_int32_t max_locks) {
    int ret = the_env->set_lk_max_locks(the_env, max_locks);
    return maybe_throw_error(ret);
}

int DbEnv::get_lk_max_locks(u_int32_t *max_locks) {
    int ret = the_env->get_lk_max_locks(the_env, max_locks);
    return maybe_throw_error(ret);
}

