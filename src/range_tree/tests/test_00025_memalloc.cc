/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
/* We are going to test whether we can manage memory once we do lots of 
   insert and delete. */

#include "test.h"

static void
RunTest (bool f_overlaps_allowed) {
    int i, j;
    int r;
    toku_range_tree *tree;
    toku_range range;
    int nums[1024];
    char letters[2] = {'A','B'};

    for (i = 0; i < 1024; i++)
      nums[i] = i;


    /* Insert and delete lots of ranges to force memory increase and decrease */

    r = toku_rt_create(&tree, int_cmp, char_cmp, f_overlaps_allowed, test_incr_memory_size, test_decr_memory_size, NULL);
    CKERR(r);

    /* Insert lots of ranges */
    for (i = 0; i < 512; i++) {
      j = i + i;
      range.ends.left  = (toku_point*)&nums[j];
      range.ends.right = (toku_point*)&nums[j+1];
      range.data  = (TXNID)letters[0];
      r = toku_rt_insert(tree, &range);   CKERR(r);
    }

    /* Decrease lots of ranges */
    for (i = 0; i < 512; i++) {
      j = i + i;
      range.ends.left  = (toku_point*)&nums[j];
      range.ends.right = (toku_point*)&nums[j+1];
      range.data  = (TXNID)letters[0];
      r = toku_rt_delete(tree, &range);   CKERR(r);
    }

    r = toku_rt_close(tree);            CKERR(r);
    tree = NULL;
}

int main(int argc, const char *argv[]) {
    parse_args(argc, argv);

#ifndef TOKU_RT_NOOVERLAPS
    RunTest(true);
#endif
    RunTest(false);
    
    return 0;
}
