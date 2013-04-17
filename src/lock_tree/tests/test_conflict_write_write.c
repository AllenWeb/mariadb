// T(A) gets W(L)
// T(B) trys W(L) blocked
// T(B) gets conflicts { A }
// T(A) releases locks

#include "test.h"

int main(int argc, const char *argv[]) {
    int r;

    uint32_t max_locks = 2;
    uint64_t max_lock_memory = 4096;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose++;
            continue;
        }
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            if (verbose > 0) verbose--;
            continue;
        }
        if (strcmp(argv[i], "--max_locks") == 0 && i+1 < argc) {
            max_locks = atoi(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--max_lock_memory") == 0 && i+1 < argc) {
            max_lock_memory = atoi(argv[++i]);
            continue;
        }        
        assert(0);
    }

    // setup
    toku_ltm *ltm = NULL;
    r = toku_ltm_create(&ltm, max_locks, max_lock_memory, dbpanic);
    assert(r == 0 && ltm);

    DB *fake_db = (DB *) 1;

    toku_lock_tree *lt = NULL;
    r = toku_ltm_get_lt(ltm, &lt, (DICTIONARY_ID){1}, fake_db, dbcmp);
    assert(r == 0 && lt);

    DBT key_l; dbt_init(&key_l, "L", 1);

    const TXNID txn_a = 1;
    toku_lock_request a_w_l; toku_lock_request_init(&a_w_l, fake_db, txn_a, &key_l, &key_l, LOCK_REQUEST_WRITE);
    r = toku_lock_request_start(&a_w_l, lt, false); assert(r == 0); 
    assert(a_w_l.state == LOCK_REQUEST_COMPLETE && a_w_l.complete_r == 0);

    txnid_set conflicts; 

    txnid_set_init(&conflicts);
    r = toku_lt_get_lock_request_conflicts(lt, &a_w_l, &conflicts);
    assert(r == 0);
    assert(txnid_set_size(&conflicts) == 0);
    txnid_set_destroy(&conflicts);

    toku_lock_request_destroy(&a_w_l);

    const TXNID txn_b = 2;
    toku_lock_request b_w_l; toku_lock_request_init(&b_w_l, fake_db, txn_b, &key_l, &key_l, LOCK_REQUEST_WRITE);
    r = toku_lock_request_start(&b_w_l, lt, false); assert(r != 0); 
    assert(b_w_l.state == LOCK_REQUEST_PENDING);

    txnid_set_init(&conflicts);
    r = toku_lt_get_lock_request_conflicts(lt, &b_w_l, &conflicts);
    assert(r == 0);
    assert(txnid_set_size(&conflicts) == 1);
    assert(txnid_set_get(&conflicts, 0) == txn_a);
    txnid_set_destroy(&conflicts);

    toku_lock_request_destroy(&b_w_l);

    r = toku_lt_unlock_txn(lt, txn_a);  assert(r == 0);

    // shutdown 
    toku_lt_remove_db_ref(lt, fake_db);
    r = toku_ltm_close(ltm); assert(r == 0);

    return 0;
}
