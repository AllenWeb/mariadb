#ifdef USE_PRAGMA_INTERFACE
#pragma interface               /* gcc class implementation */
#endif

#define TOKU_INCLUDE_CHECKPOINT_LOCK 1


#if !defined(HA_CLUSTERING)
#define HA_CLUSTERING 0
#define HA_CLUSTERED_INDEX 0
#endif

#include <db.h>
#include "hatoku_cmp.h"

class ha_tokudb;

typedef struct loader_context {
    THD* thd;
    char write_status_msg[200];
    ha_tokudb* ha;
} *LOADER_CONTEXT;

typedef struct hot_optimize_context {
    THD *thd;
    char* write_status_msg;
    ha_tokudb *ha;
    uint current_table;
    uint num_tables;
} *HOT_OPTIMIZE_CONTEXT;

//
// This object stores table information that is to be shared
// among all ha_tokudb objects.
// There is one instance per table, shared among threads.
// Some of the variables here are the DB* pointers to indexes,
// and auto increment information.
//
typedef struct st_tokudb_share {
    char *table_name;
    uint table_name_length, use_count;
    pthread_mutex_t mutex;
    THR_LOCK lock;

    ulonglong auto_ident;
    ulonglong last_auto_increment, auto_inc_create_value;
    //
    // estimate on number of rows in table
    //
    ha_rows rows;
    //
    // estimate on number of rows added in the process of a locked tables
    // this is so we can better estimate row count during a lock table
    //
    ha_rows rows_from_locked_table;
    DB *status_block;
    //
    // DB that is indexed on the primary key
    //
    DB *file;
    //
    // array of all DB's that make up table, includes DB that
    // is indexed on the primary key, add 1 in case primary
    // key is hidden
    //
    DB *key_file[MAX_KEY +1];
    rw_lock_t key_file_lock;
    uint status, version, capabilities;
    uint ref_length;
    //
    // whether table has an auto increment column
    //
    bool has_auto_inc;
    //
    // index of auto increment column in table->field, if auto_inc exists
    //
    uint ai_field_index;

    KEY_AND_COL_INFO kc_info;
    
    // 
    // we want the following optimization for bulk loads, if the table is empty, 
    // attempt to grab a table lock. emptiness check can be expensive, 
    // so we try it once for a table. After that, we keep this variable around 
    // to tell us to not try it again. 
    // 
    bool try_table_lock; 

    bool has_unique_keys;
    bool replace_into_fast;
    rw_lock_t num_DBs_lock;
    u_int32_t num_DBs;
} TOKUDB_SHARE;

#define HA_TOKU_ORIG_VERSION 4
#define HA_TOKU_VERSION 4
//
// no capabilities yet
//
#define HA_TOKU_CAP 0

//
// These are keys that will be used for retrieving metadata in status.tokudb
// To get the version, one looks up the value associated with key hatoku_version
// in status.tokudb
//

typedef ulonglong HA_METADATA_KEY;
#define hatoku_old_version 0
#define hatoku_capabilities 1
#define hatoku_max_ai 2 //maximum auto increment value found so far
#define hatoku_ai_create_value 3
#define hatoku_key_name 4
#define hatoku_frm_data 5
#define hatoku_new_version 6

typedef struct st_filter_key_part_info {
    uint offset;
    uint part_index;
} FILTER_KEY_PART_INFO;

typedef enum {
    lock_read = 0,
    lock_write
} TABLE_LOCK_TYPE;

int create_tokudb_trx_data_instance(tokudb_trx_data** out_trx);
int generate_row_for_del(
    DB *dest_db, 
    DB *src_db,
    DBT *dest_key,
    const DBT *src_key, 
    const DBT *src_val
    );
int generate_row_for_put(
    DB *dest_db, 
    DB *src_db,
    DBT *dest_key, 
    DBT *dest_val,
    const DBT *src_key, 
    const DBT *src_val
    ); 
int tokudb_update_fun(
    DB* db,
    const DBT *key,
    const DBT *old_val, 
    const DBT *extra,
    void (*set_val)(const DBT *new_val, void *set_extra),
    void *set_extra
    );

// the number of rows bulk fetched in one callback grows exponentially
// with the bulk fetch iteration, so the max iteration is the max number
// of shifts we can perform on a 64 bit integer.
#define HA_TOKU_BULK_FETCH_ITERATION_MAX 63

class ha_tokudb : public handler {
private:
    THR_LOCK_DATA lock;         ///< MySQL lock
    TOKUDB_SHARE *share;        ///< Shared lock info

    //
    // last key returned by ha_tokudb's cursor
    //
    DBT last_key;
    //
    // pointer used for multi_alloc of key_buff, key_buff2, primary_key_buff
    //
    void *alloc_ptr;
    //
    // buffer used to temporarily store a "packed row" 
    // data pointer of a DBT will end up pointing to this
    // see pack_row for usage
    //
    uchar *rec_buff;
    //
    // number of bytes allocated in rec_buff
    //
    ulong alloced_rec_buff_length;
    //
    // same as above two, but for updates
    //
    uchar *rec_update_buff;
    ulong alloced_update_rec_buff_length;
    u_int32_t max_key_length;

    uchar* range_query_buff; // range query buffer
    u_int32_t size_range_query_buff; // size of the allocated range query buffer
    u_int32_t bytes_used_in_range_query_buff; // number of bytes used in the range query buffer
    u_int32_t curr_range_query_buff_offset; // current offset into the range query buffer for queries to read
    uint64_t bulk_fetch_iteration;
    uint64_t rows_fetched_using_bulk_fetch;
    bool doing_bulk_fetch;

    //
    // buffer used to temporarily store a "packed key" 
    // data pointer of a DBT will end up pointing to this
    //
    uchar *key_buff; 
    //
    // buffer used to temporarily store a "packed key" 
    // data pointer of a DBT will end up pointing to this
    // This is used in functions that require the packing
    // of more than one key
    //
    uchar *key_buff2; 
    uchar *key_buff3; 
    //
    // buffer used to temporarily store a "packed key" 
    // data pointer of a DBT will end up pointing to this
    // currently this is only used for a primary key in
    // the function update_row, hence the name. It 
    // does not carry any state throughout the class.
    //
    uchar *primary_key_buff;

    //
    // ranges of prelocked area, used to know how much to bulk fetch
    //
    uchar *prelocked_left_range; 
    u_int32_t prelocked_left_range_size;
    uchar *prelocked_right_range; 
    u_int32_t prelocked_right_range_size;


    //
    // individual DBTs for each index
    //
    DBT mult_key_dbt[2*(MAX_KEY + 1)];
    DBT mult_rec_dbt[MAX_KEY + 1];
    u_int32_t mult_put_flags[MAX_KEY + 1];
    u_int32_t mult_del_flags[MAX_KEY + 1];
    u_int32_t mult_dbt_flags[MAX_KEY + 1];
    

    //
    // when unpacking blobs, we need to store it in a temporary
    // buffer that will persist because MySQL just gets a pointer to the 
    // blob data, a pointer we need to ensure is valid until the next
    // query
    //
    uchar* blob_buff;
    u_int32_t num_blob_bytes;

    bool unpack_entire_row;

    //
    // buffers (and their sizes) that will hold the indexes
    // of fields that need to be read for a query
    //
    u_int32_t* fixed_cols_for_query;
    u_int32_t num_fixed_cols_for_query;
    u_int32_t* var_cols_for_query;
    u_int32_t num_var_cols_for_query;
    bool read_blobs;
    bool read_key;

    //
    // transaction used by ha_tokudb's cursor
    //
    DB_TXN *transaction;
    bool is_fast_alter_running;

    // external_lock will set this true for read operations that will be closely followed by write operations.
    bool use_write_locks; // use write locks for reads

    //
    // instance of cursor being used for init_xxx and rnd_xxx functions
    //
    DBC *cursor;
    u_int32_t cursor_flags; // flags for cursor
    //
    // flags that are returned in table_flags()
    //
    ulonglong int_table_flags;
    // 
    // count on the number of rows that gets changed, such as when write_row occurs
    // this is meant to help keep estimate on number of elements in DB
    // 
    ulonglong added_rows;
    ulonglong deleted_rows;


    uint last_dup_key;
    //
    // if set to 0, then the primary key is not hidden
    // if non-zero (not necessarily 1), primary key is hidden
    //
    uint hidden_primary_key;
    bool key_read, using_ignore;

    //
    // After a cursor encounters an error, the cursor will be unusable
    // In case MySQL attempts to do a cursor operation (such as rnd_next
    // or index_prev), we will gracefully return this error instead of crashing
    //
    int last_cursor_error;

    //
    // For instances where we successfully prelock a range or a table,
    // we set this to TRUE so that successive cursor calls can know
    // know to limit the locking overhead in a call to the fractal tree
    //
    bool range_lock_grabbed;

    //
    // For bulk inserts, we want option of not updating auto inc
    // until all inserts are done. By default, is false
    //
    bool delay_updating_ai_metadata; // if true, don't update auto-increment metadata until bulk load completes
    bool ai_metadata_update_required; // if true, autoincrement metadata must be updated 

    //
    // buffer for updating the status of long insert, delete, and update
    // statements. Right now, the the messages are 
    // "[inserted|updated|deleted] about %llu rows",
    // so a buffer of 200 is good enough.
    //
    char write_status_msg[200]; //buffer of 200 should be a good upper bound.
    struct loader_context lc;

    DB_LOADER* loader;
    bool abort_loader;
    int loader_error;

    bool num_DBs_locked_in_bulk;
    u_int32_t lock_count;
    
    bool fix_rec_buff_for_blob(ulong length);
    bool fix_rec_update_buff_for_blob(ulong length);
    uchar current_ident[TOKUDB_HIDDEN_PRIMARY_KEY_LENGTH];

    ulong max_row_length(const uchar * buf);
    int pack_row_in_buff(
        DBT * row, 
        const uchar* record,
        uint index,
        uchar* row_buff
        );
    int pack_row(
        DBT * row, 
        const uchar* record,
        uint index
        );
    int pack_old_row_for_update(
        DBT * row, 
        const uchar* record,
        uint index
        );
    u_int32_t place_key_into_mysql_buff(KEY* key_info, uchar * record, uchar* data);
    void unpack_key(uchar * record, DBT const *key, uint index);
    u_int32_t place_key_into_dbt_buff(KEY* key_info, uchar * buff, const uchar * record, bool* has_null, int key_length);
    DBT* create_dbt_key_from_key(DBT * key, KEY* key_info, uchar * buff, const uchar * record, bool* has_null, bool dont_pack_pk, int key_length = MAX_KEY_LENGTH);
    DBT *create_dbt_key_from_table(DBT * key, uint keynr, uchar * buff, const uchar * record, bool* has_null, int key_length = MAX_KEY_LENGTH);
    DBT* create_dbt_key_for_lookup(DBT * key, KEY* key_info, uchar * buff, const uchar * record, bool* has_null, int key_length = MAX_KEY_LENGTH);
    DBT *pack_key(DBT * key, uint keynr, uchar * buff, const uchar * key_ptr, uint key_length, int8_t inf_byte);
    int key_cmp(uint keynr, const uchar * old_row, const uchar * new_row);
    int handle_cursor_error(int error, int err_to_return, uint keynr);
    DBT *get_pos(DBT * to, uchar * pos);
 
    int open_main_dictionary(const char* name, bool is_read_only, DB_TXN* txn);
    int open_secondary_dictionary(DB** ptr, KEY* key_info, const char* name, bool is_read_only, DB_TXN* txn);
    int acquire_table_lock (DB_TXN* trans, TABLE_LOCK_TYPE lt);
    int estimate_num_rows(DB* db, u_int64_t* num_rows, DB_TXN* txn);
    bool has_auto_increment_flag(uint* index);
    int write_frm_data(DB* db, DB_TXN* txn, const char* frm_name);
    int verify_frm_data(const char* frm_name, DB_TXN* trans);
    int write_to_status(DB* db, HA_METADATA_KEY curr_key_data, void* data, uint size, DB_TXN* txn );
    int write_metadata(DB* db, void* key, uint key_size, void* data, uint data_size, DB_TXN* txn );
    int remove_metadata(DB* db, void* key_data, uint key_size, DB_TXN* transaction);
    int update_max_auto_inc(DB* db, ulonglong val);
    int remove_key_name_from_status(DB* status_block, char* key_name, DB_TXN* txn);
    int write_key_name_to_status(DB* status_block, char* key_name, DB_TXN* txn);
    int write_auto_inc_create(DB* db, ulonglong val, DB_TXN* txn);
    void init_auto_increment();
    bool can_replace_into_be_fast(TABLE_SHARE* table_share, KEY_AND_COL_INFO* kc_info, uint pk);
    int initialize_share(
        const char* name,
        int mode
        );

    void set_query_columns(uint keynr);
    int prelock_range (const key_range *start_key, const key_range *end_key);
    int create_txn(THD* thd, tokudb_trx_data* trx);
    bool may_table_be_empty(DB_TXN *txn);
    int delete_or_rename_table (const char* from_name, const char* to_name, bool is_delete);
    int delete_or_rename_dictionary( const char* from_name, const char* to_name, const char* index_name, bool is_key, DB_TXN* txn, bool is_delete);
    int truncate_dictionary( uint keynr, DB_TXN* txn );
    int create_secondary_dictionary(
        const char* name, 
        TABLE* form, 
        KEY* key_info, 
        DB_TXN* txn, 
        KEY_AND_COL_INFO* kc_info, 
        u_int32_t keynr, 
        bool is_hot_index
        );
    int create_main_dictionary(const char* name, TABLE* form, DB_TXN* txn, KEY_AND_COL_INFO* kc_info);
    void trace_create_table_info(const char *name, TABLE * form);
    int is_index_unique(bool* is_unique, DB_TXN* txn, DB* db, KEY* key_info);
    int is_val_unique(bool* is_unique, uchar* record, KEY* key_info, uint dict_index, DB_TXN* txn);
    int do_uniqueness_checks(uchar* record, DB_TXN* txn, THD* thd);
    // XXX 3414 remove last parameter
    void set_main_dict_put_flags(THD* thd, bool opt_eligible, u_int32_t* put_flags);
    int insert_row_to_main_dictionary(uchar* record, DBT* pk_key, DBT* pk_val, DB_TXN* txn);
    int insert_rows_to_dictionaries_mult(DBT* pk_key, DBT* pk_val, DB_TXN* txn, THD* thd);
    void test_row_packing(uchar* record, DBT* pk_key, DBT* pk_val);
    u_int32_t fill_row_mutator(
        uchar* buf, 
        u_int32_t* dropped_columns, 
        u_int32_t num_dropped_columns,
        TABLE* altered_table,
        KEY_AND_COL_INFO* altered_kc_info,
        u_int32_t keynr,
        bool is_add
        );

 
public:
    ha_tokudb(handlerton * hton, TABLE_SHARE * table_arg);
    ~ha_tokudb();

    const char *table_type() const;
    const char *index_type(uint inx);
    const char **bas_ext() const;

    //
    // Returns a bit mask of capabilities of storage engine. Capabilities 
    // defined in sql/handler.h
    //
    ulonglong table_flags(void) const;
    
    ulong index_flags(uint inx, uint part, bool all_parts) const;

    //
    // Returns limit on the number of keys imposed by tokudb.
    //
    uint max_supported_keys() const {
        return MAX_KEY;
    } 

    uint extra_rec_buf_length() const {
        return TOKUDB_HIDDEN_PRIMARY_KEY_LENGTH;
    } 
    ha_rows estimate_rows_upper_bound();

    //
    // Returns the limit on the key length imposed by tokudb.
    //
    uint max_supported_key_length() const {
        return UINT_MAX32;
    } 

    //
    // Returns limit on key part length imposed by tokudb.
    //
    uint max_supported_key_part_length() const {
        return UINT_MAX32;
    } 
    const key_map *keys_to_use_for_scanning() {
        return &key_map_full;
    }

    double scan_time();
    double keyread_time(uint index, uint ranges, ha_rows rows);
    double read_time(uint index, uint ranges, ha_rows rows);

    int open(const char *name, int mode, uint test_if_locked);
    int close(void);
    void update_create_info(HA_CREATE_INFO* create_info);
    int create(const char *name, TABLE * form, HA_CREATE_INFO * create_info);
    int delete_table(const char *name);
    int rename_table(const char *from, const char *to);
    int optimize(THD * thd, HA_CHECK_OPT * check_opt);
#if 0
    int analyze(THD * thd, HA_CHECK_OPT * check_opt);
#endif
    int write_row(uchar * buf);
    int update_row(const uchar * old_data, uchar * new_data);
    int delete_row(const uchar * buf);

    void start_bulk_insert(ha_rows rows);
    int end_bulk_insert();
    int end_bulk_insert(bool abort);

    int prepare_index_scan();
    int prepare_index_key_scan( const uchar * key, uint key_len );
    int prepare_range_scan( const key_range *start_key, const key_range *end_key);
    void column_bitmaps_signal();
    int index_init(uint index, bool sorted);
    int index_end();
    int index_next_same(uchar * buf, const uchar * key, uint keylen); 
    int index_read(uchar * buf, const uchar * key, uint key_len, enum ha_rkey_function find_flag);
    int index_read_last(uchar * buf, const uchar * key, uint key_len);
    int index_next(uchar * buf);
    int index_prev(uchar * buf);
    int index_first(uchar * buf);
    int index_last(uchar * buf);

    int rnd_init(bool scan);
    int rnd_end();
    int rnd_next(uchar * buf);
    int rnd_pos(uchar * buf, uchar * pos);

    int read_range_first(const key_range *start_key,
                                 const key_range *end_key,
                                 bool eq_range, bool sorted);
    int read_range_next();


    void position(const uchar * record);
    int info(uint);
    int extra(enum ha_extra_function operation);
    int reset(void);
    int external_lock(THD * thd, int lock_type);
    int start_stmt(THD * thd, thr_lock_type lock_type);

    ha_rows records_in_range(uint inx, key_range * min_key, key_range * max_key);

    u_int32_t get_cursor_isolation_flags(enum thr_lock_type lock_type, THD* thd);
    THR_LOCK_DATA **store_lock(THD * thd, THR_LOCK_DATA ** to, enum thr_lock_type lock_type);

    int get_status(DB_TXN* trans);
    void init_hidden_prim_key_info();
    inline void get_auto_primary_key(uchar * to) {
        pthread_mutex_lock(&share->mutex);
        share->auto_ident++;
        hpk_num_to_char(to, share->auto_ident);
        pthread_mutex_unlock(&share->mutex);
    }
    virtual void get_auto_increment(ulonglong offset, ulonglong increment, ulonglong nb_desired_values, ulonglong * first_value, ulonglong * nb_reserved_values);
    bool is_optimize_blocking();
    bool is_auto_inc_singleton();
    void print_error(int error, myf errflag);
    uint8 table_cache_type() {
        return HA_CACHE_TBL_TRANSACT;
    }
    bool primary_key_is_clustered() {
        return true;
    }
    bool supports_clustered_keys() {
        return true;
    }
    int cmp_ref(const uchar * ref1, const uchar * ref2);
    bool check_if_incompatible_data(HA_CREATE_INFO * info, uint table_changes);

    int add_index(TABLE *table_arg, KEY *key_info, uint num_of_keys);
    int tokudb_add_index(
        TABLE *table_arg, 
        KEY *key_info, 
        uint num_of_keys, 
        DB_TXN* txn, 
        bool* inc_num_DBs,
        bool* modified_DB
        ); 
    void restore_add_index(TABLE* table_arg, uint num_of_keys, bool incremented_numDBs, bool modified_DBs);
    int drop_indexes(TABLE *table_arg, uint *key_num, uint num_of_keys, DB_TXN* txn);
    int prepare_drop_index(TABLE *table_arg, uint *key_num, uint num_of_keys);
    void restore_drop_indexes(TABLE *table_arg, uint *key_num, uint num_of_keys);
    int final_drop_index(TABLE *table_arg);

#if defined(HA_GENERAL_ONLINE)
    void print_alter_info(
        TABLE *altered_table,
        HA_CREATE_INFO *create_info,
        HA_ALTER_FLAGS *alter_flags,
        uint table_changes
        );
    int check_if_supported_alter(TABLE *altered_table,
         HA_CREATE_INFO *create_info,
         HA_ALTER_FLAGS *alter_flags,
         HA_ALTER_INFO  *alter_info,
         uint table_changes
         );
    int alter_table_phase1(THD *thd,
                                   TABLE *altered_table,
                                   HA_CREATE_INFO *create_info,
                                   HA_ALTER_INFO *alter_info,
                                   HA_ALTER_FLAGS *alter_flags)
    {
      return 0;
    }
    int alter_table_phase2(THD *thd,
                                   TABLE *altered_table,
                                   HA_CREATE_INFO *create_info,
                                   HA_ALTER_INFO *alter_info,
			           HA_ALTER_FLAGS *alter_flags);
    int alter_table_phase3(THD *thd, TABLE *table)
    {
      return 0;
    }
#endif

    // delete all rows from the table
    // effect: all dictionaries, including the main and indexes, should be empty
    int discard_or_import_tablespace(my_bool discard);
    int truncate();
    int delete_all_rows();
    void extract_hidden_primary_key(uint keynr, DBT const *found_key);
    void read_key_only(uchar * buf, uint keynr, DBT const *found_key);
    int read_row_callback (uchar * buf, uint keynr, DBT const *row, DBT const *found_key);
    int read_primary_key(uchar * buf, uint keynr, DBT const *row, DBT const *found_key);
    int unpack_blobs(
        uchar* record,
        const uchar* from_tokudb_blob,
        u_int32_t num_blob_bytes,
        bool check_bitmap
        );
    int unpack_row(
        uchar* record, 
        DBT const *row, 
        DBT const *key,
        uint index
        );

    int prefix_cmp_dbts( uint keynr, const DBT* first_key, const DBT* second_key) {
        return tokudb_prefix_cmp_dbt_key(share->key_file[keynr], first_key, second_key);
    }

    void track_progress(THD* thd);
    void set_loader_error(int err);
    void set_dup_value_for_pk(DBT* key);


    //
    // index into key_file that holds DB* that is indexed on
    // the primary_key. this->key_file[primary_index] == this->file
    //
    uint primary_key;

    int check(THD *thd, HA_CHECK_OPT *check_opt);

    int fill_range_query_buf(
        bool need_val, 
        DBT const *key, 
        DBT  const *row, 
        int direction,
        THD* thd
        );
   
private:
    int read_full_row(uchar * buf);
    int __close(int mutex_is_locked);
    int get_next(uchar* buf, int direction);
    int read_data_from_range_query_buff(uchar* buf, bool need_val);
    void invalidate_bulk_fetch();
};

int open_status_dictionary(DB** ptr, const char* name, DB_TXN* txn);


#if MYSQL_VERSION_ID >= 50506

static inline void my_free(void *p, int arg) {
    my_free(p);
}

static inline void *memcpy_fixed(void *a, const void *b, size_t n) {
    return memcpy(a, b, n);
}

#endif

