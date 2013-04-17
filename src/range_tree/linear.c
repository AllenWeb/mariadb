/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007-8 Tokutek Inc.  All rights reserved."

#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

/**
   \file linear.c
   \brief Range tree implementation
  
   See rangetree.h for documentation on the following. */

//Currently this is a stub implementation just so we can write and compile tests
//before actually implementing the range tree.

#include <rangetree.h>
#include <errno.h>
#include <toku_assert.h>
#include <stdlib.h>
#include <string.h>
struct __toku_range_tree_local {
    //Linear version only fields:
    toku_range* ranges;
    u_int32_t   ranges_len;
};
#include <rangetree-internal.h>


static const u_int32_t minlen = 64;

static inline int toku__rt_decrease_capacity(toku_range_tree* tree,
                                             u_int32_t _num) {
    //TODO: SOME ATTRIBUTE TO REMOVE NEVER EXECUTABLE ERROR: assert(tree);
    u_int32_t num = _num < minlen ? minlen : _num;
    
    if (tree->i.ranges_len >= num * 2) {
        u_int32_t temp_len = tree->i.ranges_len;
        while (temp_len >= num * 2) temp_len /= 2;
        assert(temp_len >= _num);   //Sanity check.
        toku_range* temp_ranges =
                     tree->realloc(tree->i.ranges, temp_len * sizeof(toku_range));
        if (!temp_ranges) return errno;
        tree->i.ranges     = temp_ranges;
        tree->i.ranges_len = temp_len;
    }
    return 0;
}

static inline int toku__rt_increase_capacity(toku_range_tree* tree,
                                             u_int32_t num) {
    //TODO: SOME ATTRIBUTE TO REMOVE NEVER EXECUTABLE ERROR: assert(tree);
    if (tree->i.ranges_len < num) {
        u_int32_t temp_len = tree->i.ranges_len;
        while (temp_len < num) temp_len *= 2;
        toku_range* temp_ranges =
                     tree->realloc(tree->i.ranges, temp_len * sizeof(toku_range));
        if (!temp_ranges) return errno;
        tree->i.ranges     = temp_ranges;
        tree->i.ranges_len = temp_len;
    }
    return 0;
}

static inline BOOL toku__rt_overlap(toku_range_tree* tree,
                                    toku_interval* a, toku_interval* b) {
    assert(tree);
    assert(a);
    assert(b);
    //a->left <= b->right && b->left <= a->right
    return (BOOL)((tree->end_cmp(a->left, b->right) <= 0) &&
		  (tree->end_cmp(b->left, a->right) <= 0));
}

static inline BOOL toku__rt_exact(toku_range_tree* tree,
                                  toku_range* a, toku_range* b) {
    assert(tree);
    assert(a);
    assert(b);

    return (BOOL)((tree->end_cmp (a->ends.left,  b->ends.left)  == 0) &&
		  (tree->end_cmp (a->ends.right, b->ends.right) == 0) &&
		  (tree->data_cmp(a->data,  b->data)  == 0));
}

static inline int toku__rt_cmp(toku_range_tree* tree,
                               toku_range* a, toku_range* b) {
    int cmp = 0;
    assert(tree);
    assert(a);
    assert(b);

    cmp = tree->end_cmp(a->ends.left,  b->ends.left);
    if (cmp!=0) { goto cleanup; }
    cmp = tree->end_cmp(a->ends.right, b->ends.right);
    if (cmp!=0) { goto cleanup; }
    cmp = tree->data_cmp(a->data,  b->data);
    if (cmp!=0) { goto cleanup; }

    cmp = 0;
cleanup:
    return cmp;
}

int toku_rt_create(toku_range_tree** ptree,
                   int (*end_cmp)(const toku_point*,const toku_point*),
                   int (*data_cmp)(const TXNID,const TXNID),
		           BOOL allow_overlaps,
                   void* (*user_malloc) (size_t),
                   void  (*user_free)   (void*),
                   void* (*user_realloc)(void*, size_t)) {
    int r;
    toku_range_tree* tmptree;

    if (!ptree) return EINVAL;
    r = toku_rt_super_create(ptree, &tmptree, end_cmp, data_cmp, allow_overlaps,
                             user_malloc, user_free, user_realloc);
    if (0) {
        died1:
        user_free(tmptree);
        return r;
    }
    if (r!=0) return r;
    
    //Any local initializers go here.
    tmptree->i.ranges_len = minlen;
    tmptree->i.ranges     = (toku_range*)
                       user_malloc(tmptree->i.ranges_len * sizeof(toku_range));
    if (!tmptree->i.ranges) { r = errno; goto died1; }

    *ptree = tmptree;

    return 0;
}

void toku_rt_clear(toku_range_tree* tree) {
    assert(tree);
    toku__rt_decrease_capacity(tree, 0);
    tree->numelements = 0;
}

int toku_rt_close(toku_range_tree* tree) {
    if (!tree)                                           return EINVAL;
    tree->free(tree->i.ranges);
    tree->free(tree);
    return 0;
}

int toku_rt_find(toku_range_tree* tree, toku_interval* query, u_int32_t k,
                 toku_range** buf, u_int32_t* buflen, u_int32_t* numfound) {
    if (!tree || !query || !buf || !buflen || !numfound) return EINVAL;
    if (*buflen == 0)                                    return EINVAL;
    
    u_int32_t temp_numfound = 0;
    int r;
    u_int32_t i;
    
    for (i = 0; i < tree->numelements; i++) {
        if (toku__rt_overlap(tree, query, &tree->i.ranges[i].ends)) {
            r = toku__rt_increase_buffer(tree, buf, buflen, temp_numfound + 1);
            if (r != 0) return r;
            (*buf)[temp_numfound++] = tree->i.ranges[i];
            //k == 0 means limit of infinity, this is not a bug.
            if (temp_numfound == k) break;
        }
    }
    *numfound = temp_numfound;
    return 0;
}

int toku_rt_insert(toku_range_tree* tree, toku_range* range) {
    if (!tree || !range)                                 return EINVAL;

    u_int32_t i;
    u_int32_t move;
    int r;

    //EDOM cases
    if (tree->allow_overlaps) {
        for (i = 0; i < tree->numelements; i++) {
            if (toku__rt_exact  (tree, range, &tree->i.ranges[i])) return EDOM;
        }
    }
    else {
        for (i = 0; i < tree->numelements; i++) {
            if (toku__rt_overlap(tree, &range->ends, &tree->i.ranges[i].ends)) return EDOM;
        }
    }
    for (i = 0; i < tree->numelements; i++) {
        if (toku__rt_cmp(tree, range, &tree->i.ranges[i]) > 0) { break; }
    }
    /* Goes in slot 'i' */
    r = toku__rt_increase_capacity(tree, tree->numelements + 1);
    if (r != 0) return r;
    tree->numelements++;
    /* Shift to make room. */
    for (move = tree->numelements - 1; move > i; move--) {
        tree->i.ranges[move] = tree->i.ranges[move - 1];
    }
    tree->i.ranges[i] = *range;
    return 0;
}

int toku_rt_delete(toku_range_tree* tree, toku_range* range) {
    if (!tree || !range)                                 return EINVAL;
    u_int32_t i;
    u_int32_t move;
    
    for (i = 0;
         i < tree->numelements &&
         !toku__rt_exact(tree, range, &(tree->i.ranges[i]));
         i++) {}
    //EDOM case: Not Found
    if (i == tree->numelements) return EDOM;
    /* Shift left. */
    for (move = i; move < tree->numelements - 1; move++) {
        tree->i.ranges[move] = tree->i.ranges[move + 1];        
    }
    toku__rt_decrease_capacity(tree, --tree->numelements);
    return 0;
}

int toku_rt_predecessor (toku_range_tree* tree, toku_point* point,
                         toku_range* pred, BOOL* wasfound) {
    if (!tree || !point || !pred || !wasfound)           return EINVAL;
    if (tree->allow_overlaps)                            return EINVAL;
    toku_range* best = NULL;
    u_int32_t i;

    for (i = 0; i < tree->numelements; i++) {
        if (toku__rt_p_cmp(tree, point, &tree->i.ranges[i].ends) > 0 &&
            (!best || tree->end_cmp(best->ends.left, tree->i.ranges[i].ends.left) < 0)) {
            best = &tree->i.ranges[i];
        }
    }
    *wasfound = (BOOL)(best != NULL);
    if (best) *pred = *best;
    return 0;
}

int toku_rt_successor (toku_range_tree* tree, toku_point* point,
                       toku_range* succ, BOOL* wasfound) {
    if (!tree || !point || !succ || !wasfound)           return EINVAL;
    if (tree->allow_overlaps)                            return EINVAL;
    toku_range* best = NULL;
    u_int32_t i;

    for (i = 0; i < tree->numelements; i++) {
        if (toku__rt_p_cmp(tree, point, &tree->i.ranges[i].ends) < 0 &&
            (!best || tree->end_cmp(best->ends.left, tree->i.ranges[i].ends.left) > 0)) {
            best = &tree->i.ranges[i];
        }
    }
    *wasfound = (BOOL)(best != NULL);
    if (best) *succ = *best;
    return 0;
}

int toku_rt_get_allow_overlaps(toku_range_tree* tree, BOOL* allowed) {
    if (!tree || !allowed)                               return EINVAL;
    *allowed = tree->allow_overlaps;
    return 0;
}

int toku_rt_get_size(toku_range_tree* tree, u_int32_t* size) {
    if (!tree || !size)                                  return EINVAL;
    *size = tree->numelements;
    return 0;
}

int toku_rt_iterate(toku_range_tree* tree, int (*f)(toku_range*,void*), void* extra) {
    u_int32_t index;

    int r = ENOSYS;
    for (index = 0; index < tree->numelements; index++) {
        if ((r = f(&tree->i.ranges[index], extra))) goto cleanup;
    }
    r = 0;
cleanup:
    return r;
}

