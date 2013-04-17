/* We are going to test whether create and close properly check their input. */

#include "test.h"

int r;
toku_lock_tree* lt  = NULL;
toku_ltm*       ltm = NULL;
DB*             db  = (DB*)1;
TXNID           txn = (TXNID)1;
u_int32_t max_locks = 1000;
BOOL duplicates = FALSE;
int  nums[100];

DBT _keys_left[2];
DBT _keys_right[2];
DBT _datas_left[2];
DBT _datas_right[2];
DBT* keys_left[2]   ;
DBT* keys_right[2]  ;
DBT* datas_left[2] ;
DBT* datas_right[2] ;

toku_point qleft, qright;
toku_interval query;
toku_range* buf;
unsigned buflen;
unsigned numfound;

static void init_query(BOOL dups) {  
    init_point(&qleft,  lt);
    init_point(&qright, lt);
    
    qleft.key_payload  = (void *) toku_lt_neg_infinity;
    qright.key_payload = (void *) toku_lt_infinity;

    if (dups) {
        qleft.data_payload  = qleft.key_payload;
        qright.data_payload = qright.key_payload;
    }

    memset(&query,0,sizeof(query));
    query.left  = &qleft;
    query.right = &qright;
}

static void setup_tree(BOOL dups) {
    assert(!lt && !ltm);
    r = toku_ltm_create(&ltm, max_locks, dbpanic,
                        get_compare_fun_from_db, get_dup_compare_from_db,
                        toku_malloc, toku_free, toku_realloc);
    CKERR(r);
    assert(ltm);
    r = toku_lt_create(&lt, dups, dbpanic, ltm,
                       get_compare_fun_from_db, get_dup_compare_from_db,
                       toku_malloc, toku_free, toku_realloc);
    CKERR(r);
    assert(lt);
    init_query(dups);
}

static void close_tree(void) {
    assert(lt && ltm);
    r = toku_lt_close(lt);
        CKERR(r);
    r = toku_ltm_close(ltm);
        CKERR(r);
    lt = NULL;
    ltm = NULL;
}

typedef enum { null = -1, infinite = -2, neg_infinite = -3 } lt_infty;

static DBT* set_to_infty(DBT *dbt, int value) {
    if (value == infinite) return (DBT*)toku_lt_infinity;
    if (value == neg_infinite) return (DBT*)toku_lt_neg_infinity;
    if (value == null) return dbt_init(dbt, NULL, 0);
    assert(value >= 0);
    return                    dbt_init(dbt, &nums[value], sizeof(nums[0]));
}


static void lt_insert(BOOL dups, int key_l, int data_l, int key_r, int data_r) {
    DBT _key_left;
    DBT _key_right;
    DBT _data_left;
    DBT _data_right;
    DBT* key_left   = &_key_left;
    DBT* key_right  = &_key_right;
    DBT* data_left  = dups ? &_data_left : NULL;
    DBT* data_right = dups ? &_data_right: NULL;

    key_left  = set_to_infty(key_left,  key_l);
    key_right = set_to_infty(key_right, key_r);
    if (dups) {
        if (key_left != &_key_left) data_left = key_left;
        else data_left = set_to_infty(data_left,  data_l);
        if (key_right != &_key_right) data_right = key_right;
        else data_right = set_to_infty(data_right,  data_r);
        assert(key_left  && data_left);
        assert(key_right && data_right);
    } else {
        data_left = data_right = NULL;
        assert(key_left  && !data_left);
        assert(key_right && !data_right);
    }

    r = toku_lt_acquire_range_read_lock(lt, db, txn, key_left,  data_left,
                                                     key_right, data_right);
    CKERR(r);
}

static void setup_payload_len(void** payload, u_int32_t* len, int val) {
    assert(payload && len);

    DBT temp;

    *payload = set_to_infty(&temp, val);
    
    if (val < 0) {
        *len = 0;
    }
    else {
        *len = sizeof(nums[0]);
        *payload = temp.data;
    }
}

static void temporarily_fake_comparison_functions(void) {
    assert(!lt->db && !lt->compare_fun && !lt->dup_compare);
    lt->db = db;
    lt->compare_fun = get_compare_fun_from_db(db);
    lt->dup_compare = get_dup_compare_from_db(db);
}

static void stop_fake_comparison_functions(void) {
    assert(lt->db && lt->compare_fun && lt->dup_compare);
    lt->db = NULL;
    lt->compare_fun = NULL;
    lt->dup_compare = NULL;
}

static void lt_find(BOOL dups, toku_range_tree* rt,
                        unsigned k, int key_l, int data_l,
                                    int key_r, int data_r,
                                    TXNID find_txn) {
temporarily_fake_comparison_functions();
    r = toku_rt_find(rt, &query, 0, &buf, &buflen, &numfound);
    CKERR(r);
    assert(numfound==k);

    toku_point left, right;
    init_point(&left, lt);
    setup_payload_len(&left.key_payload, &left.key_len, key_l);
    if (dups) {
        if (key_l < null) left.data_payload = left.key_payload;
        else setup_payload_len(&left.data_payload, &left.data_len, data_l);
    }
    init_point(&right, lt);
    setup_payload_len(&right.key_payload, &right.key_len, key_r);
    if (dups) {
        if (key_r < null) right.data_payload = right.key_payload;
        else setup_payload_len(&right.data_payload, &right.data_len, data_r);
    }
    unsigned i;
    for (i = 0; i < numfound; i++) {
        if (toku__lt_point_cmp(buf[i].ends.left,  &left ) == 0 &&
            toku__lt_point_cmp(buf[i].ends.right, &right) == 0 &&
            buf[i].data == find_txn) { goto cleanup; }
    }
    assert(FALSE);  //Crash since we didn't find it.
cleanup:
    stop_fake_comparison_functions();
}
              

static void insert_1(BOOL dups, int key_l, int key_r, int data_l, int data_r,
              const void* kl, const void* dl, const void* kr, const void* dr) {
    DBT _key_left;
    DBT _key_right;
    DBT _data_left;
    DBT _data_right;
    DBT* key_left   = &_key_left;
    DBT* key_right  = &_key_right;
    DBT* data_left  = dups ? &_data_left : NULL;
    DBT* data_right = dups ? &_data_right: NULL;

    dbt_init    (key_left,  &nums[key_l], sizeof(nums[key_l]));
    dbt_init    (key_right, &nums[key_r], sizeof(nums[key_r]));
    if (dups) {
        dbt_init(data_left,  &nums[data_l], sizeof(nums[data_l]));
        dbt_init(data_right, &nums[data_r], sizeof(nums[data_r]));
        if (dl) data_left  = (DBT*)dl;
        if (dr) data_right = (DBT*)dr;
    }
    if (kl) key_left   = (DBT*)kl;
    if (kr) key_right  = (DBT*)kr;
    

    setup_tree(dups);
    r = toku_lt_acquire_range_read_lock(lt, db, txn, key_left,  data_left,
                                                     key_right, data_right);
    CKERR(r);
    close_tree();

    setup_tree(dups);
    r = toku_lt_acquire_read_lock(lt, db, txn, key_left, data_left);
    CKERR(r);
    close_tree();
}

static void runtest(BOOL dups) {
    int i;
    const DBT* choices[3];

    choices[0] = toku_lt_neg_infinity;
    choices[1] = NULL;
    choices[2] = toku_lt_infinity;
    for (i = 0; i < 9; i++) {
        int a = i / 3;
        int b = i % 3;
        if (a > b) continue;

        insert_1(dups, 3, 3, 7, 7, choices[a], choices[a],
                                   choices[b], choices[b]);
    }
    
    toku_range_tree *rt;
    /* ************************************** */
    setup_tree(dups);

    /////BUG HERE MAYBE NOT CONSOLIDATING.
    /*nodups:
        [(3, 3),  (7,7)] and [(4,4), (5,5)]
      dups:
        [(3, 3),  (3,7)] and [(3,4), (3,5)]
    */
    
    lt_insert(dups,
              3,            3,
              dups ? 3 : 7, 7);
    lt_insert(dups,
              dups ? 3 : 4, 4,
              dups ? 3 : 5, 5);
    rt = toku__lt_ifexist_selfread(lt, txn);
    assert(rt);

    lt_find(dups, rt, 1,
            3,            3,
            dups ? 3 : 7, 7,
            txn);

#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;
    assert(rt);

    lt_find(dups, rt, 1,
            3,            3,
            dups ? 3 : 7, 7,
            txn);
#endif

    close_tree();
    /* ************************************** */
    setup_tree(dups);

    /*nodups:
        [(3, 3),  (7,7)] and [(4,4), (5,5)]
      dups:
        [(3, 3),  (3,7)] and [(3,4), (3,5)]
    */
    lt_insert(dups,
              dups ? 3 : 4, 4,
              dups ? 3 : 5, 5);
    lt_insert(dups,
              3,            3,
              dups ? 3 : 7, 7);
    
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);

    lt_find(dups, rt, 1,
            3,            3,
            dups ? 3 : 7, 7,
            txn);

#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);

    lt_find(dups, rt, 1,
            3,            3,
            dups ? 3 : 7, 7,
            txn);
#endif
    rt = NULL;
    close_tree();
    /* ************************************** */
    setup_tree(dups);
    lt_insert(dups, 3, 3, 3, 3);
    lt_insert(dups, 4, 4, 4, 4);
    lt_insert(dups, 3, 3, 3, 3);
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 2, 3, 3, 3, 3, txn);
    lt_find(dups, rt, 2, 4, 4, 4, 4, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 2, 3, 3, 3, 3, txn);
    lt_find(dups, rt, 2, 4, 4, 4, 4, txn);
#endif
    rt = NULL;
    close_tree();
    /* ************************************** */
    setup_tree(dups);
    for (i = 0; i < 20; i += 2) {
        lt_insert(dups,       i, 5, i + 1, 10);
    }
    rt = toku__lt_ifexist_selfread(lt, txn);
    assert(rt);
    for (i = 0; i < 20; i += 2) {
        lt_find(dups, rt, 10, i, 5, i + 1, 10, txn);
    }
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread; assert(rt);
    for (i = 0; i < 20; i += 2) {
        lt_find(dups, rt, 10, i, 5, i + 1, 10, txn);
    }
#endif
    lt_insert(dups,        0, neg_infinite, 20, infinite);
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(  dups, rt, 1, 0, neg_infinite, 20, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(  dups, rt, 1, 0, neg_infinite, 20, infinite, txn);
#endif
    rt = NULL;
    close_tree();
    /* ************************************** */
    setup_tree(dups);
    lt_insert(dups,        0, neg_infinite, 1, infinite);
    lt_insert(dups,        1, neg_infinite, 2, infinite);

    lt_insert(dups,        4, neg_infinite, 5, infinite);
    lt_insert(dups,        3, neg_infinite, 4, infinite);
    
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 2,   0, neg_infinite, 2, infinite, txn);
    lt_find(dups, rt, 2,   3, neg_infinite, 5, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 2,   0, neg_infinite, 2, infinite, txn);
    lt_find(dups, rt, 2,   3, neg_infinite, 5, infinite, txn);
#endif

    lt_insert(dups,        2, neg_infinite, 3, infinite);

    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 1,   0, neg_infinite, 5, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 1,   0, neg_infinite, 5, infinite, txn);
#endif
    rt = NULL;
    close_tree();
    /* ************************************** */
    setup_tree(dups);
    lt_insert(dups,        1, neg_infinite, 3, infinite);
    lt_insert(dups,        4, neg_infinite, 6, infinite);
    lt_insert(dups,        2, neg_infinite, 5, infinite);
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, 6, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, 6, infinite, txn);
#endif
    close_tree();

    setup_tree(dups);
    lt_insert(dups, neg_infinite, neg_infinite, 3, infinite);
    lt_insert(dups,        4, neg_infinite, 5, infinite);
    lt_insert(dups,        6, neg_infinite, 8, infinite);
    lt_insert(dups,        2, neg_infinite, 7, infinite);
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 1,   neg_infinite, neg_infinite, 8, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 1,   neg_infinite, neg_infinite, 8, infinite, txn);
#endif
    close_tree();

    setup_tree(dups);
    lt_insert(dups,        1, neg_infinite, 2, infinite);
    lt_insert(dups,        3, neg_infinite, infinite, infinite);
    lt_insert(dups,        2, neg_infinite, 3, infinite);
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, infinite, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, infinite, infinite, txn);
#endif
    close_tree();

    setup_tree(dups);
    lt_insert(dups,        1, neg_infinite, 2, infinite);
    lt_insert(dups,        3, neg_infinite, 4, infinite);
    lt_insert(dups,        5, neg_infinite, 6, infinite);
    lt_insert(dups,        2, neg_infinite, 5, infinite);
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, 6, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, 6, infinite, txn);
#endif
    close_tree();

    setup_tree(dups);
    lt_insert(dups,        1, neg_infinite, 2, infinite);
    lt_insert(dups,        3, neg_infinite, 5, infinite);
    lt_insert(dups,        2, neg_infinite, 4, infinite);
    rt = toku__lt_ifexist_selfread(lt, txn);   assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, 5, infinite, txn);
#ifndef TOKU_RT_NOOVERLAPS
    rt = lt->mainread;                          assert(rt);
    lt_find(dups, rt, 1,   1, neg_infinite, 5, infinite, txn);
#endif
    close_tree();

    /* ************************************** */
}


static void init_test(void) {
    unsigned i;
    for (i = 0; i < sizeof(nums)/sizeof(nums[0]); i++) nums[i] = i;

    buflen = 64;
    buf = (toku_range*) toku_malloc(buflen*sizeof(toku_range));
    assert(buf);
}

static void close_test(void) {
    toku_free(buf);
}




int main(int argc, const char *argv[]) {
    parse_args(argc, argv);

    init_test();

    runtest(FALSE);
    runtest(TRUE);

    close_test();
    return 0;
}
