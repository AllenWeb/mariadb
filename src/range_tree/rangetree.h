/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-8 Tokutek Inc.  All rights reserved."

#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#if !defined(TOKU_RANGE_TREE_H)
#define TOKU_RANGE_TREE_H
/**
   \file  rangetree.h
   \brief Range trees: header and comments
  
   Range trees are an ordered data structure to hold intervals.
   You can learn about range trees at 
   http://en.wikipedia.org/wiki/Interval_tree
   or by consulting the textbooks, e.g., 
   Thomas H. Cormen, Charles E. Leiserson, Ronald L. Rivest, and 
   Clifford Stein. Introduction to Algorithms, Second Edition. 
   MIT Press and McGraw-Hill, 2001. ISBN 0-262-03293-7
*/

#include <toku_portability.h>
#include <ft/fttypes.h>
#include <db.h>


struct __toku_point;
#if !defined(__TOKU_POINT)
#define __TOKU_POINT
typedef struct __toku_point toku_point;
#endif

/** \brief Range with value
    Represents a range of data with an associated value.
 */
typedef struct {
    toku_point* left;  /**< Left end-point */
    toku_point* right; /**< Right end-point */
} toku_interval;

typedef struct {
    toku_interval ends;
    TXNID     data;  /**< Data associated with the range */
} toku_range;

/** Export the internal representation to a sensible name */
/*  These lines will remain. */
typedef struct __toku_range_tree       toku_range_tree;

/** \brief Internal range representation 
    Internal representation of a range tree. Some fields depend on the
    implementation of range trees, and some others are shared.
    Parameters are never modified on failure with the exception of
    buf and buflen.
 */
struct __toku_range_tree;

/**
    Gets whether the range tree allows overlapping ranges.
    
    \param tree     The range tree.
    \param allowed  Returns whether overlaps are allowed.

    \return
    - 0:            Success.
    - EINVAL:       If any pointer argument is NULL. */
int toku_rt_get_allow_overlaps(toku_range_tree* tree, bool* allowed);

/**
    Creates a range tree.

    \param ptree            Points to the new range tree if create is successful 
    \param end_cmp          User provided function to compare end points.
                            Return value conforms to cmp in qsort(3). 
                            It is assumed to define a total order.
    \param data_cmp         User provided function to compare data values.
                            Return value conforms to cmp in qsort(3). 
    \param allow_overlaps   Whether ranges in this range tree are permitted 
                            to overlap. 
    \param user_malloc      A user provided malloc(3) function.
    \param user_free        A user provided free(3) function.
    \param user_realloc     A user provided realloc(3) function.

    \return
    - 0:                    Success.
    - EINVAL:               If any pointer argument is NULL.
    - EINVAL:               If allow_overlaps = true and the implementation does
                            not support overlaps.
    - Other exit codes may be forwarded from underlying system calls. */
int toku_rt_create(toku_range_tree** ptree,
                   int (*end_cmp)(const toku_point*,const toku_point*), 
                   int (*data_cmp)(const TXNID,const TXNID),
                   bool allow_overlaps,
                   void (*incr_memory_size)(void *extra_memory_size, size_t s),
                   void (*decr_memory_size)(void *extra_memory_size, size_t s),
                   void *extra_memory_size);

/**
    Destroys and frees a range tree.

    \param tree             The range tree to close.
    
    \return
    - 0:                    Success.
    - EINVAL:               If tree is NULL.
 */
int toku_rt_close(toku_range_tree* tree);

/**
    Deletes all elements of a range tree.

    \param tree             The range tree to clear.
 */
void toku_rt_clear(toku_range_tree* tree);

/**
    Finds ranges in the range tree that overlap a query range.

    \param tree     The range tree to search in. 
    \param query    The range to query. range.data must be NULL. 
    \param k        The maximum number of ranges to return. 
                    The special value '0' is used to request ALL overlapping 
                    ranges. 
    \param buf      A pointer to the buffer used to return ranges.
                    The buffer will be increased using realloc(3) if necessary.
                    NOTE: buf must have been dynamically allocated i.e. via 
                    malloc(3), calloc(3), etc.
                    The buffer will not be modified unless it is too small.
    \param buflen   A pointer to the current length of the buffer.  
                    If the buffer is increased, then buflen will be updated. 
    \param numfound The number of ranges found. This will necessarily be <= k
                    unless k == 0.
                    If k != 0 && numfound == k, there may be additional
                    ranges that overlap the queried range but were skipped
                    to accomodate the request of k. 

    \return
    - 0:             Success.
    - EINVAL:        If any pointer argument is NULL. If range.data != NULL.
                     If buflen == 0.
    - Other exit codes may be forwarded from underlying system calls if buf is
     not large enough.

    Growth direction: It may be useful in the future to add an extra out 
    parameter to specify whether more elements exist in the tree that overlap 
    (in excess of the requested limit of k).
 */
int toku_rt_find(toku_range_tree* tree,toku_interval* query, uint32_t k,
                 toku_range** buf, uint32_t* buflen, uint32_t* numfound);
 

/**
    Inserts a range into the range tree.

    \param tree     The range tree to insert into.
    \param range    The range to insert.

    \return
    - 0:            Success.
    - EINVAL:       If any pointer argument is NULL.
    - EDOM:         If an equivalent range (left, right, and data according to
                    end_cmp and data_cmp) already exists in the tree.
                    If an overlapping range exists in the tree and
                    allow_overlaps == false.
    - Other exit codes may be forwarded from underlying system calls.
 */
int toku_rt_insert(toku_range_tree* tree, toku_range* range);

/**
    Deletes a range from the range tree.

    \param tree     The range tree to delete from.
    \param range    The range to delete.

    \return
    - 0:            Success.
    - EINVAL:       If any pointer argument is NULL.
    - EDOM:         If the exact range (left, right, and data) did
                    not exist in the tree.
 */
int toku_rt_delete(toku_range_tree* tree, toku_range* range);

/**
    Finds the strict predecessor range of a point i.e. the rightmost range
    completely to the left of the query point according to end_cmp.
    This operation is only defined if allow_overlaps == false.

    \param tree         The range tree to search in.
    \param point        The point to query.  Must be a valid argument to
                        end_cmp.
    \param pred         A buffer to return the predecessor.
    \param wasfound     Whether a predecessor was found.
                        If no range is strictly to the left of the query 
                        point this will be false.

    \return
    - 0:                Success.
    - EINVAL:           If any pointer argument is NULL.
                        If tree allows overlaps.
    - Other exit codes may be forwarded from underlying system calls.
 */
int toku_rt_predecessor(toku_range_tree* tree, toku_point* point,
                        toku_range* pred, bool* wasfound);

/**
    Finds the strict successor range of a point i.e. the leftmost range
    completely to the right of the query point according to end_cmp.
    This operation is only defined if allow_overlaps == false.

    \param tree         The range tree to search in.
    \param point        The point to query.  Must be a valid argument to
                        end_cmp.
    \param succ         A buffer to return the successor range.
    \param wasfound     Whether a predecessor was found.
                        If no range is strictly to the left of the query point
                        this will be false.

    \return
    - 0:                Success.
    - EINVAL:           If any pointer argument is NULL.
                        If tree allows overlaps.
    - Other exit codes may be forwarded from underlying system calls. 
 */
int toku_rt_successor(toku_range_tree* tree, toku_point* point,
                      toku_range* succ, bool* wasfound);

/**
   Finds the number of elements in the range tree.
   \param tree          The range tree.
   \return              The number of ranges in the range tree
 */
size_t toku_rt_get_size(toku_range_tree* tree);

int toku_rt_iterate(toku_range_tree* tree, int (*f)(toku_range*,void*), void* extra);

void toku_rt_verify(toku_range_tree *tree);

size_t toku_rt_memory_size(toku_range_tree *tree);



#endif  /* #if !defined(TOKU_RANGE_TREE_H) */
