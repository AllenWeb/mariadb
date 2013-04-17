/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-8 Tokutek Inc.  All rights reserved."

#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

/**
   \file  hash_rth.h
   \brief Hash rth
  
*/

#include <toku_portability.h>
#include "memory.h"
#include "rth.h"
#include <toku_assert.h>
#include <errno.h>
#include <string.h>

/* TODO: reallocate the hash rth if it grows too big. Perhaps, use toku_get_prime in ft/primes.c */
const uint32_t __toku_rth_init_size = 521;

static inline uint32_t toku__rth_hash(toku_rth* rth, TXNID key) {
    uint64_t tmp = (uint64_t)key;
    return (uint32_t)(tmp % rth->num_buckets);
}

static inline void toku__invalidate_scan(toku_rth* rth) {
    rth->iter_is_valid = false;
}

int toku_rth_create(toku_rth** prth) {
    int r = ENOSYS;
    assert(prth);
    toku_rth* tmp = NULL;
    tmp = (toku_rth*) toku_xmalloc(sizeof(*tmp));
    memset(tmp, 0, sizeof(*tmp));
    tmp->num_buckets = __toku_rth_init_size;
    tmp->buckets     = (toku_rth_elt*) toku_xmalloc(tmp->num_buckets * sizeof(*tmp->buckets));
    memset(tmp->buckets, 0, tmp->num_buckets * sizeof(*tmp->buckets));
    toku__invalidate_scan(tmp);
    tmp->iter_head.next_in_iteration = &tmp->iter_head;
    tmp->iter_head.prev_in_iteration = &tmp->iter_head;

    *prth = tmp;
    r = 0;
    return r;
}

rt_forest* toku_rth_find(toku_rth* rth, TXNID key) {
    assert(rth);

    uint32_t index          = toku__rth_hash(rth, key);
    toku_rth_elt* head    = &rth->buckets[index];
    toku_rth_elt* current = head->next_in_bucket;
    while (current) {
        if (current->value.hash_key == key) break;
        current = current->next_in_bucket;
    }
    return current ? &current->value : NULL;
}

void toku_rth_start_scan(toku_rth* rth) {
    assert(rth);
    rth->iter_curr = &rth->iter_head;
    rth->iter_is_valid = true;
}

static inline toku_rth_elt* toku__rth_next(toku_rth* rth) {
    assert(rth);
    assert(rth->iter_is_valid);

    rth->iter_curr     = rth->iter_curr->next_in_iteration;
    rth->iter_is_valid = (rth->iter_curr != &rth->iter_head);
    return rth->iter_curr;
}

rt_forest* toku_rth_next(toku_rth* rth) {
    assert(rth);
    toku_rth_elt* next = toku__rth_next(rth);
    return rth->iter_curr != &rth->iter_head ? &next->value : NULL;
}

/* Element MUST exist. */
void toku_rth_delete(toku_rth* rth, TXNID key) {
    assert(rth);
    toku__invalidate_scan(rth);

    /* Must have elements. */
    assert(rth->num_keys);

    uint32_t index = toku__rth_hash(rth, key);
    toku_rth_elt* head    = &rth->buckets[index]; 
    toku_rth_elt* prev    = head; 
    toku_rth_elt* current = prev->next_in_bucket;

    while (current != NULL) {
        if (current->value.hash_key == key) break;
        prev = current;
        current = current->next_in_bucket;
    }
    /* Must be found. */
    assert(current);
    current->prev_in_iteration->next_in_iteration = current->next_in_iteration;
    current->next_in_iteration->prev_in_iteration = current->prev_in_iteration;
    prev->next_in_bucket = current->next_in_bucket;
    toku_free(current);
    rth->num_keys--;
    return;
}
    
/* Will allow you to insert it over and over.  You need to keep track. */
int toku_rth_insert(toku_rth* rth, TXNID key) {
    int r = ENOSYS;
    assert(rth);
    toku__invalidate_scan(rth);

    uint32_t index = toku__rth_hash(rth, key);

    /* Allocate a new one. */
    toku_rth_elt* element = (toku_rth_elt*) toku_xmalloc(sizeof(*element));
    memset(element, 0, sizeof(*element));
    element->value.hash_key    = key;
    element->next_in_iteration = rth->iter_head.next_in_iteration;
    element->prev_in_iteration = &rth->iter_head;
    element->next_in_iteration->prev_in_iteration = element;
    element->prev_in_iteration->next_in_iteration = element;
    
    element->next_in_bucket            = rth->buckets[index].next_in_bucket;
    rth->buckets[index].next_in_bucket = element;
    rth->num_keys++;

    r = 0;
    return r;    
}

static inline void toku__rth_clear(toku_rth* rth, bool clean) {
    assert(rth);

    toku_rth_elt* element;
    toku_rth_elt* head = &rth->iter_head;
    toku_rth_elt* next = NULL;
    toku_rth_start_scan(rth);
    next = toku__rth_next(rth);
    while (next != head) {
        element = next;
        next    = toku__rth_next(rth);
        toku_free(element);
    }
    /* If clean is true, then we want to restore it to 'just created' status.
       If we are closing the tree, we don't need to do that restoration. */
    if (!clean) { return; }
    memset(rth->buckets, 0, rth->num_buckets * sizeof(*rth->buckets));
    toku__invalidate_scan(rth);
    rth->iter_head.next_in_iteration = &rth->iter_head;
    rth->iter_head.prev_in_iteration = &rth->iter_head;
    rth->num_keys = 0;
}

void toku_rth_clear(toku_rth* rth) {
    toku__rth_clear(rth, true);
}

void toku_rth_close(toku_rth* rth) {
    assert(rth);

    toku__rth_clear(rth, false);
    toku_free(rth->buckets);
    toku_free(rth);
}

bool toku_rth_is_empty(toku_rth* rth) {
    assert(rth);
    /* Verify consistency. */
    assert((rth->num_keys == 0) ==
           (rth->iter_head.next_in_iteration == &rth->iter_head));
    assert((rth->num_keys == 0) ==
           (rth->iter_head.prev_in_iteration == &rth->iter_head));
    return (rth->num_keys == 0);
}
