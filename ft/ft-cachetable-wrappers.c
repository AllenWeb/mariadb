/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "$Id$"
#ident "Copyright (c) 2007-2011 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include <ft-cachetable-wrappers.h>

#include <fttypes.h>
#include <ft-flusher.h>
#include <ft-internal.h>

static void
ftnode_get_key_and_fullhash(
    BLOCKNUM* cachekey,
    u_int32_t* fullhash,
    void* extra)
{
    FT h = extra;
    BLOCKNUM name;
    toku_allocate_blocknum(h->blocktable, &name, h);
    *cachekey = name;
    *fullhash = toku_cachetable_hash(h->cf, name);
}

void
cachetable_put_empty_node_with_dep_nodes(
    FT h,
    u_int32_t num_dependent_nodes,
    FTNODE* dependent_nodes,
    BLOCKNUM* name, //output
    u_int32_t* fullhash, //output
    FTNODE* result)
{
    FTNODE XMALLOC(new_node);
    CACHEFILE dependent_cf[num_dependent_nodes];
    BLOCKNUM dependent_keys[num_dependent_nodes];
    u_int32_t dependent_fullhash[num_dependent_nodes];
    enum cachetable_dirty dependent_dirty_bits[num_dependent_nodes];
    for (u_int32_t i = 0; i < num_dependent_nodes; i++) {
        dependent_cf[i] = h->cf;
        dependent_keys[i] = dependent_nodes[i]->thisnodename;
        dependent_fullhash[i] = toku_cachetable_hash(h->cf, dependent_nodes[i]->thisnodename);
        dependent_dirty_bits[i] = (enum cachetable_dirty) dependent_nodes[i]->dirty;
    }

    int r = toku_cachetable_put_with_dep_pairs(
        h->cf,
        ftnode_get_key_and_fullhash,
        new_node,
        make_pair_attr(sizeof(FTNODE)),
        get_write_callbacks_for_node(h),
        h,
        num_dependent_nodes,
        dependent_cf,
        dependent_keys,
        dependent_fullhash,
        dependent_dirty_bits,
        name,
        fullhash);
    assert_zero(r);

    *result = new_node;
}

void
create_new_ftnode_with_dep_nodes(
    FT h,
    FTNODE *result,
    int height,
    int n_children,
    u_int32_t num_dependent_nodes,
    FTNODE* dependent_nodes)
{
    u_int32_t fullhash = 0;
    BLOCKNUM name;

    cachetable_put_empty_node_with_dep_nodes(
        h,
        num_dependent_nodes,
        dependent_nodes,
        &name,
        &fullhash,
        result);

    assert(h->nodesize > 0);
    assert(h->basementnodesize > 0);
    if (height == 0) {
        assert(n_children > 0);
    }

    toku_initialize_empty_ftnode(
        *result,
        name,
        height,
        n_children,
        h->layout_version,
        h->nodesize,
        h->flags);

    assert((*result)->nodesize > 0);
    (*result)->fullhash = fullhash;
}

void
toku_create_new_ftnode (
    FT_HANDLE t,
    FTNODE *result,
    int height,
    int n_children)
{
    return create_new_ftnode_with_dep_nodes(
        t->h,
        result,
        height,
        n_children,
        0,
        NULL);
}

int
toku_pin_ftnode(
    FT_HANDLE brt,
    BLOCKNUM blocknum,
    u_int32_t fullhash,
    UNLOCKERS unlockers,
    ANCESTORS ancestors,
    const PIVOT_BOUNDS bounds,
    FTNODE_FETCH_EXTRA bfe,
    BOOL may_modify_node,
    BOOL apply_ancestor_messages, // this BOOL is probably temporary, for #3972, once we know how range query estimates work, will revisit this
    FTNODE *node_p,
    BOOL* msgs_applied)
{
    void *node_v;
    *msgs_applied = FALSE;
    int r = toku_cachetable_get_and_pin_nonblocking(
            brt->h->cf,
            blocknum,
            fullhash,
            &node_v,
            NULL,
            get_write_callbacks_for_node(brt->h),
            toku_ftnode_fetch_callback,
            toku_ftnode_pf_req_callback,
            toku_ftnode_pf_callback,
            may_modify_node,
            bfe, //read_extraargs
            unlockers);
    if (r==0) {
        FTNODE node = node_v;
        if (apply_ancestor_messages) {
            maybe_apply_ancestors_messages_to_node(brt, node, ancestors, bounds, msgs_applied);
        }
        *node_p = node;
        // printf("%*sPin %ld\n", 8-node->height, "", blocknum.b);
    } else {
        assert(r==TOKUDB_TRY_AGAIN); // Any other error and we should bomb out ASAP.
        // printf("%*sPin %ld try again\n", 8, "", blocknum.b);
    }
    return r;
}

void
toku_pin_ftnode_off_client_thread(
    FT h,
    BLOCKNUM blocknum,
    u_int32_t fullhash,
    FTNODE_FETCH_EXTRA bfe,
    BOOL may_modify_node,
    u_int32_t num_dependent_nodes,
    FTNODE* dependent_nodes,
    FTNODE *node_p)
{
    void *node_v;
    CACHEFILE dependent_cf[num_dependent_nodes];
    BLOCKNUM dependent_keys[num_dependent_nodes];
    u_int32_t dependent_fullhash[num_dependent_nodes];
    enum cachetable_dirty dependent_dirty_bits[num_dependent_nodes];
    for (u_int32_t i = 0; i < num_dependent_nodes; i++) {
        dependent_cf[i] = h->cf;
        dependent_keys[i] = dependent_nodes[i]->thisnodename;
        dependent_fullhash[i] = toku_cachetable_hash(h->cf, dependent_nodes[i]->thisnodename);
        dependent_dirty_bits[i] = (enum cachetable_dirty) dependent_nodes[i]->dirty;
    }

    int r = toku_cachetable_get_and_pin_with_dep_pairs(
        h->cf,
        blocknum,
        fullhash,
        &node_v,
        NULL,
        get_write_callbacks_for_node(h),
        toku_ftnode_fetch_callback,
        toku_ftnode_pf_req_callback,
        toku_ftnode_pf_callback,
        may_modify_node,
        bfe,
        num_dependent_nodes,
        dependent_cf,
        dependent_keys,
        dependent_fullhash,
        dependent_dirty_bits
        );
    assert(r==0);
    FTNODE node = node_v;
    *node_p = node;
}

void
toku_unpin_ftnode_off_client_thread(FT h, FTNODE node)
{
    int r = toku_cachetable_unpin(
        h->cf,
        node->thisnodename,
        node->fullhash,
        (enum cachetable_dirty) node->dirty,
        make_ftnode_pair_attr(node)
        );
    assert(r==0);
}

void
toku_unpin_ftnode(FT h, FTNODE node)
{
    // printf("%*sUnpin %ld\n", 8-node->height, "", node->thisnodename.b);
    //VERIFY_NODE(brt,node);
    toku_unpin_ftnode_off_client_thread(h, node);
}

void
toku_unpin_ftnode_read_only(FT_HANDLE brt, FTNODE node)
{
    int r = toku_cachetable_unpin(
        brt->h->cf,
        node->thisnodename,
        node->fullhash,
        (enum cachetable_dirty) node->dirty,
        make_invalid_pair_attr()
        );
    assert(r==0);
}

