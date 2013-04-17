/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2011-2013 Tokutek Inc.  All rights reserved."

// generate a tree with a single leaf node containing unsorted keys
// check that brt verify finds them


#include <ft-cachetable-wrappers.h>
#include "test.h"

static FTNODE
make_node(FT_HANDLE brt, int height) {
    FTNODE node = NULL;
    int n_children = (height == 0) ? 1 : 0;
    toku_create_new_ftnode(brt, &node, height, n_children);
    if (n_children) BP_STATE(node,0) = PT_AVAIL;
    return node;
}

static void
append_leaf(FTNODE leafnode, void *key, size_t keylen, void *val, size_t vallen) {
    assert(leafnode->height == 0);

    DBT thekey;
    toku_fill_dbt(&thekey, key, keylen);
    DBT theval;
    toku_fill_dbt(&theval, val, vallen);

    // get an index that we can use to create a new leaf entry
    uint32_t idx = toku_omt_size(BLB_BUFFER(leafnode, 0));

    // apply an insert to the leaf node
    MSN msn = next_dummymsn();
    FT_MSG_S cmd = { FT_INSERT, msn, xids_get_root_xids(), .u={.id = { &thekey, &theval }} };
    toku_ft_bn_apply_cmd_once(BLB(leafnode, 0), &cmd, idx, NULL, TXNID_NONE, make_gc_info(false), NULL, NULL);

    // dont forget to dirty the node
    leafnode->dirty = 1;
}

static void 
populate_leaf(FTNODE leafnode, int k, int v) {
    append_leaf(leafnode, &k, sizeof k, &v, sizeof v);
}

static void 
test_dup_in_leaf(int do_verify) {
    int r;

    // cleanup
    const char *fname = TOKU_TEST_FILENAME;
    r = unlink(fname);
    assert(r == 0 || (r == -1 && errno == ENOENT));

    // create a cachetable
    CACHETABLE ct = NULL;
    toku_cachetable_create(&ct, 0, ZERO_LSN, NULL_LOGGER);

    // create the brt
    TOKUTXN null_txn = NULL;
    FT_HANDLE brt = NULL;
    r = toku_open_ft_handle(fname, 1, &brt, 1024, 256, TOKU_DEFAULT_COMPRESSION_METHOD, ct, null_txn, toku_builtin_compare_fun);
    assert(r == 0);

    // discard the old root block
    FTNODE newroot = make_node(brt, 0);
    populate_leaf(newroot, htonl(2), 1);
    populate_leaf(newroot, htonl(1), 2);

    // set the new root to point to the new tree
    toku_ft_set_new_root_blocknum(brt->ft, newroot->thisnodename);

    // unpin the new root
    toku_unpin_ftnode(brt->ft, newroot);

    if (do_verify) {
        r = toku_verify_ft(brt);
        assert(r != 0);
    }

    // flush to the file system
    r = toku_close_ft_handle_nolsn(brt, 0);     
    assert(r == 0);

    // shutdown the cachetable
    toku_cachetable_close(&ct);
}

static int
usage(void) {
    return 1;
}

int
test_main (int argc , const char *argv[]) {
    int do_verify = 1;
    initialize_dummymsn();
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-v") == 0) {
            verbose++;
            continue;
        }
        if (strcmp(arg, "-q") == 0) {
            verbose = 0;
            continue;
        }
        if (strcmp(arg, "--verify") == 0 && i+1 < argc) {
            do_verify = atoi(argv[++i]);
            continue;
        }
        return usage();
    }
    test_dup_in_leaf(do_verify);
    return 0;
}
