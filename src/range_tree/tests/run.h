/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
static toku_interval *
init_query(toku_interval* range, int left, int right) {
    assert(0 <= left && left < (int) (sizeof nums / sizeof nums[0]));
    range->left = (toku_point*)&nums[left];
    assert(0 <= right && right < (int) (sizeof nums / sizeof nums[0]));
    range->right = (toku_point*)&nums[right];
    return range;
}

static toku_range *
init_range (toku_range* range, int left, int right, int data) {
    init_query(&range->ends, left, right);
    if (data < 0) {
        range->data = 0;
    } else {
        assert(0 <= data && data < (int) (sizeof letters / sizeof letters[0]));
        range->data = (TXNID)letters[data];
    }
    return range;
}

static void
setup_tree (bool allow_overlaps, BOOL insert, int left, int right, int data) {
    int r;
    toku_range range;
    r = toku_rt_create(&tree, int_cmp, char_cmp, allow_overlaps, test_incr_memory_size, test_decr_memory_size, NULL);
    CKERR(r);

    if (insert) {
        r = toku_rt_insert(tree, init_range(&range, left, right, data));
        CKERR(r);
    }
}

static void
close_tree (void) {
    int r;
    r = toku_rt_close(tree);    CKERR(r);
}

static void
runinsert (int rexpect, toku_range* toinsert) {
    int r;
    r = toku_rt_insert(tree, toinsert);
    CKERR2(r, rexpect);
    toku_rt_verify(tree);
}

static __attribute__((__unused__)) void 
runsearch (int rexpect, toku_interval* query, toku_range* expect);

static void
runsearch (int rexpect, toku_interval* query, toku_range* expect) {
    int r;
    unsigned found;
    r = toku_rt_find(tree, query, 0, &buf, &buflen, &found);
    CKERR2(r, rexpect);
    
    if (rexpect != 0) return;
    assert(found == 1);
    assert(int_cmp(buf[0].ends.left, expect->ends.left) == 0 &&
           int_cmp(buf[0].ends.right, expect->ends.right) == 0 &&
           char_cmp(buf[0].data, expect->data) == 0);
}

static __attribute__((__unused__)) void 
runsearch2 (int rexpect, toku_interval* query, toku_range* expect1, toku_range *expect2);

static void
runsearch2 (int rexpect, toku_interval* query, toku_range* expect1, toku_range *expect2) {
    int r;
    unsigned found;
    r = toku_rt_find(tree, query, 0, &buf, &buflen, &found);
    CKERR2(r, rexpect);
    
    if (rexpect != 0) return;
    assert(found == 2);
    assert(int_cmp(buf[0].ends.left, expect1->ends.left) == 0 &&
           int_cmp(buf[0].ends.right, expect1->ends.right) == 0 &&
           char_cmp(buf[0].data, expect1->data) == 0);
    assert(int_cmp(buf[1].ends.left, expect2->ends.left) == 0 &&
           int_cmp(buf[1].ends.right, expect2->ends.right) == 0 &&
           char_cmp(buf[1].data, expect2->data) == 0);
}

static __attribute__((__unused__)) void
runlimitsearch (toku_interval* query, unsigned limit, unsigned findexpect);

static void
runlimitsearch (toku_interval* query, unsigned limit, unsigned findexpect) {
    int r;
    unsigned found;
    r=toku_rt_find(tree, query, limit, &buf, &buflen, &found);  CKERR(r);
    verify_all_overlap(query, buf, found);
    
    assert(found == findexpect);
}
