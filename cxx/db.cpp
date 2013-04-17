#include <assert.h>
#include <db.h>
#include <errno.h>
#include <db_cxx.h>

#define do_maybe_error(errno) 

Db::Db(DbEnv *env, u_int32_t flags)
    :      the_Env(env)
{
    assert(env); // modern versions of TokuDB require an env.
    the_db   = 0;

    is_private_env = (the_Env == 0);

    DB *tmp_db;
    int ret = db_create(&tmp_db, the_Env->get_DB_ENV(), flags & ~(DB_CXX_NO_EXCEPTIONS));
    if (ret!=0) {
	the_Env->maybe_throw_error(ret);
	// Otherwise cannot do much
	return;
    }
    the_db = tmp_db; 
    tmp_db->api_internal = this;
    if (is_private_env) {
	the_Env = new DbEnv(tmp_db->dbenv, flags & DB_CXX_NO_EXCEPTIONS);
    }
}

Db::~Db() {
    if (the_db) {
	close(0); // the user should have called close, but we do it here if not done.
	assert(the_db==0);
    }
    if (is_private_env && the_Env) {
	the_Env->close(0);
	delete the_Env;
    }
}

int Db::set_flags(u_int32_t flags) {
    int ret = the_db->set_flags(the_db, flags);
    return the_Env->maybe_throw_error(ret);
}

int Db::get_flags(u_int32_t *flags) {
    int ret = the_db->get_flags(the_db, flags);
    return the_Env->maybe_throw_error(ret);
}

int Db::close (u_int32_t flags) {
    if (!the_db) {
	return the_Env->maybe_throw_error(EINVAL);
    }
    the_db->api_internal = 0;


    int ret = the_db->close(the_db, flags);

    the_db = 0;

    int no_exceptions = the_Env->do_no_exceptions; // Get this out before possibly deleting the env
    
    if (is_private_env) {
	// The env was closed by the_db->close, we have to tell the DbEnv that the DB_ENV is gone, and delete it.
	the_Env->the_env = 0;
	delete the_Env;
	the_Env=0;
    }

    // Do we need to clean up "private environments"?
    // What about cursors?  They should be cleaned up already, but who did it?

    // This maybe_throw must be the static one because the env is gone.
    return DbEnv::maybe_throw_error(ret, NULL, no_exceptions);
}

int Db::open(DbTxn *txn, const char *filename, const char *subname, DBTYPE typ, u_int32_t flags, int mode) {
    int ret = the_db->open(the_db, txn->get_DB_TXN(), filename, subname, typ, flags, mode);
    return the_Env->maybe_throw_error(ret);
}

int Db::del(DbTxn *txn, Dbt *key, u_int32_t flags) {
    int ret = the_db->del(the_db, txn->get_DB_TXN(), key->get_DBT(), flags);
    return the_Env->maybe_throw_error(ret);
}

int Db::get(DbTxn *txn, Dbt *key, Dbt *data, u_int32_t flags) {
    int ret = the_db->get(the_db, txn->get_DB_TXN(), key->get_DBT(), data->get_DBT(), flags);
    return the_Env->maybe_throw_error(ret);
}

int Db::put(DbTxn *txn, Dbt *key, Dbt *data, u_int32_t flags) {
    int ret = the_db->put(the_db, txn->get_DB_TXN(), key->get_DBT(), data->get_DBT(), flags);
    return the_Env->maybe_throw_error(ret);
}

int Db::cursor(DbTxn *txn, Dbc **cursorp, u_int32_t flags) {
    int ret = the_db->cursor(the_db, txn->get_DB_TXN(), (DBC**)cursorp, flags);
    return the_Env->maybe_throw_error(ret);
}

int Db::set_pagesize(u_int32_t size) {
    int ret = the_db->set_pagesize(the_db, size);
    return the_Env->maybe_throw_error(ret);
}

int Db::remove(const char *file, const char *database, u_int32_t flags) {
    int ret = the_db->remove(the_db, file, database, flags);
    the_db = 0;
    return the_Env->maybe_throw_error(ret);
}

extern "C" int toku_bt_compare_callback_c(DB *db_c, const DBT *a, const DBT *b) {
    Db *db_cxx=Db::get_Db(db_c);
    return db_cxx->bt_compare_callback_cxx(db_cxx, Dbt::get_const_Dbt(a), Dbt::get_const_Dbt(b));
}

int Db::set_bt_compare(int (*bt_compare_callback)(Db *, const Dbt *, const Dbt *)) {
    bt_compare_callback_cxx = bt_compare_callback;
    int ret = the_db->set_bt_compare(the_db, toku_bt_compare_callback_c);
    return the_Env->maybe_throw_error(ret);
}

int Db::set_bt_compare(bt_compare_fcn_type bt_compare_fcn) {
    int ret = the_db->set_bt_compare(the_db, bt_compare_fcn);
    return the_Env->maybe_throw_error(ret);
}

int Db::fd(int *fdp) {
    int ret = the_db->fd(the_db, fdp);
    return the_Env->maybe_throw_error(ret);
}

extern "C" int toku_dup_compare_callback_c(DB *db_c, const DBT *a, const DBT *b) {
    Db *db_cxx=Db::get_Db(db_c);
    return db_cxx->dup_compare_callback_cxx(db_cxx, Dbt::get_const_Dbt(a), Dbt::get_const_Dbt(b));
}

void Db::set_errpfx(const char *errpfx) {
    the_Env->set_errpfx(errpfx);
}

void Db::set_error_stream(std::ostream *new_error_stream) {
    the_Env->set_error_stream(new_error_stream);
}
