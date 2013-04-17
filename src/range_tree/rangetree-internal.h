/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."

#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#if !defined(TOKU_RANGE_TREE_INTERNAL_H)
#define TOKU_RANGE_TREE_INTERNAL_H
/** Export the internal representation to a sensible name */
/*  These lines will remain. */
typedef struct __toku_range_tree_local toku_range_tree_local;
struct __toku_range_tree_local;

/** \brief Internal range representation 
    Internal representation of a range tree. Some fields depend on the
    implementation of range trees, and some others are shared.
    Parameters are never modified on failure with the exception of
    buf and buflen.
 */
struct __toku_range_tree {
    //Shared fields:
    /** A comparison function, as in bsearch(3), to compare the end-points of 
        a range. It is assumed to be commutative. */
    int       (*end_cmp)(const toku_point*,const toku_point*);  
    /** A comparison function, as in bsearch(3), to compare the data associated
        with a range */
    int       (*data_cmp)(const TXNID,const TXNID);
    /** Whether this tree allows ranges to overlap */
    bool        allow_overlaps;
    toku_range_tree_local i;
    
    void (*incr_memory_size)(void *extra_memory_size, size_t s);
    void (*decr_memory_size)(void *extra_memory_size, size_t s);
    void *extra_memory_size;
};

/*
 *  Returns:
 *      0:      Point \in range
 *      < 0:    Point strictly less than the range.
 *      > 0:    Point strictly greater than the range.
 */
static inline int toku__rt_p_cmp(toku_range_tree* tree,
                           toku_point* point, toku_interval* interval) {
    if (tree->end_cmp(point, interval->left) < 0)  
        return -1;
    if (tree->end_cmp(point, interval->right) > 0) 
        return 1;
    return 0;
}
    
static inline int toku__rt_increase_buffer(toku_range_tree* tree UU(), toku_range** buf,
                                     uint32_t* buflen, uint32_t num) {
    assert(buf);
    //TODO: SOME ATTRIBUTE TO REMOVE NEVER EXECUTABLE ERROR: assert(buflen);
    if (*buflen < num) {
        uint32_t temp_len = *buflen; 
        if (temp_len == 0)
            temp_len = 1;
        while (temp_len < num) 
            temp_len *= 2;
        XREALLOC_N(temp_len, *buf);
        *buflen = temp_len;
    }
    return 0;
}

static inline int 
toku_rt_super_create(toku_range_tree** upperptree,
                     toku_range_tree** ptree,
                     int (*end_cmp)(const toku_point*,const toku_point*),
                     int (*data_cmp)(const TXNID,const TXNID),
                     bool allow_overlaps,
                     void (*incr_memory_size)(void *extra_memory_size, size_t s),
                     void (*decr_memory_size)(void *extra_memory_size, size_t s),
                     void *extra_memory_size) {

    if (!upperptree || !ptree || !end_cmp || !data_cmp) 
        return EINVAL;
    
    toku_range_tree* temptree = (toku_range_tree*) toku_xmalloc(sizeof(toku_range_tree));
    
    //Any initializers go here.
    temptree->end_cmp        = end_cmp;
    temptree->data_cmp       = data_cmp;
    temptree->allow_overlaps = allow_overlaps;
    temptree->incr_memory_size = incr_memory_size;
    temptree->decr_memory_size = decr_memory_size;
    temptree->extra_memory_size = extra_memory_size;
    *ptree = temptree;

    return 0;
}

#endif  /* #if !defined(TOKU_RANGE_TREE_INTERNAL_H) */
