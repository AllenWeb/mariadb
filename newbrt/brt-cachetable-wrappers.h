/* -*- mode: C; c-basic-offset: 4 -*- */
#ifndef BRT_CACHETABLE_WRAPPERS_H
#define BRT_CACHETABLE_WRAPPERS_H

#ident "$Id$"
#ident "Copyright (c) 2007-2011 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include <brttypes.h>
#include "cachetable.h"

/**
 * Put an empty node (that is, no fields filled) into the cachetable. 
 * In the process, write dependent nodes out for checkpoint if 
 * necessary.
 */
void
cachetable_put_empty_node_with_dep_nodes(
    struct brt_header* h,
    u_int32_t num_dependent_nodes,
    BRTNODE* dependent_nodes,
    BLOCKNUM* name, //output
    u_int32_t* fullhash, //output
    BRTNODE* result
    );

/**
 * Create a new brtnode with specified height and number of children.
 * In the process, write dependent nodes out for checkpoint if 
 * necessary.
 */
void
create_new_brtnode_with_dep_nodes(
    struct brt_header* h,
    BRTNODE *result,
    int height,
    int n_children,
    u_int32_t num_dependent_nodes,
    BRTNODE* dependent_nodes
    );

/**
 * Create a new brtnode with specified height
 * and children. 
 * Used for test functions only.
 */
void
toku_create_new_brtnode (
    BRT t,
    BRTNODE *result,
    int height,
    int n_children
    );

/**
 * The intent of toku_pin_brtnode(_holding_lock) is to abstract the
 * process of retrieving a node from the rest of brt.c, so that there is
 * only one place where we need to worry applying ancestor messages to a
 * leaf node. The idea is for all of brt.c (search, splits, merges,
 * flushes, etc) to access a node via toku_pin_brtnode(_holding_lock)
 */
int
toku_pin_brtnode(
    BRT brt,
    BLOCKNUM blocknum,
    u_int32_t fullhash,
    UNLOCKERS unlockers,
    ANCESTORS ancestors,
    const PIVOT_BOUNDS pbounds,
    BRTNODE_FETCH_EXTRA bfe,
    BOOL may_modify_node,
    BOOL apply_ancestor_messages, // this BOOL is probably temporary, for #3972, once we know how range query estimates work, will revisit this
    BRTNODE *node_p,
    BOOL* msgs_applied
    );

/**
 * Pin a brtnode off the client thread, which means
 * it is pinned without the ydb lock being held.
 * As a result, unlike toku_pin_brtnode, we cannot apply ancestor
 * messages.
 */
void
toku_pin_brtnode_off_client_thread(
    struct brt_header* h,
    BLOCKNUM blocknum,
    u_int32_t fullhash,
    BRTNODE_FETCH_EXTRA bfe,
    BOOL may_modify_node,
    u_int32_t num_dependent_nodes,
    BRTNODE* dependent_nodes,
    BRTNODE *node_p
    );

/**
 * Effect: Unpin a brt node. Used for
 * nodes that were pinned off client thread.
 */
void
toku_unpin_brtnode_off_client_thread(struct brt_header* h, BRTNODE node);

/**
 * Effect: Unpin a brt node.
 * Used for nodes pinned on a client thread
 */
void
toku_unpin_brtnode(BRT brt, BRTNODE node);

void
toku_unpin_brtnode_read_only(BRT brt, BRTNODE node);

#endif
