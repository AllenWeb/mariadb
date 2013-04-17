/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ifndef _TOKU_WORKQUEUE_H
#define _TOKU_WORKQUEUE_H
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."


#include <errno.h>
#include "toku_assert.h"
#include <toku_pthread.h>

struct workitem;

// A work function is called by a worker thread when the workitem (see below) is being handled
// by a worker thread.
typedef void (*WORKFUNC)(struct workitem *wi);

// A workitem contains the function that is called by a worker thread in a threadpool.
// A workitem is queued in a workqueue.
typedef struct workitem *WORKITEM;
struct workitem {
    WORKFUNC f;
    void *arg;
    struct workitem *next;
};

// Initialize a workitem with a function and argument
static inline void workitem_init(WORKITEM wi, WORKFUNC f, void *arg) {
    wi->f = f;
    wi->arg = arg;
    wi->next = 0;
}

// Access the workitem function
static inline WORKFUNC workitem_func(WORKITEM wi) {
    return wi->f;
}

// Access the workitem argument
static inline void *workitem_arg(WORKITEM wi) {
    return wi->arg;
}

// A workqueue is currently a fifo of workitems that feeds a thread pool.  We may
// divide the workqueue into per worker thread queues.
typedef struct workqueue *WORKQUEUE;
struct workqueue {
    WORKITEM head, tail;             // list of workitems
    toku_mutex_t lock;
    toku_cond_t wait_read;   // wait for read
    int want_read;                   // number of threads waiting to read
    toku_cond_t wait_write;  // wait for write
    int want_write;                  // number of threads waiting to write
    char closed;                     // kicks waiting threads off of the write queue
    int n_in_queue;                  // count of how many workitems are in the queue.
};

// Get a pointer to the workqueue lock.  This is used by workqueue client software
// that wants to control the workqueue locking.
static inline toku_mutex_t *workqueue_lock_ref(WORKQUEUE wq) {
    return &wq->lock;
}

// Lock the workqueue
static inline void workqueue_lock(WORKQUEUE wq) {
    toku_mutex_lock(&wq->lock);
}

// Unlock the workqueue
static inline void workqueue_unlock(WORKQUEUE wq) {
    toku_mutex_unlock(&wq->lock);
}

// Initialize a workqueue
// Expects: the workqueue is not initialized
// Effects: the workqueue is set to empty and the condition variable is initialized
__attribute__((unused))
static void workqueue_init(WORKQUEUE wq) {
    toku_mutex_init(&wq->lock, 0);
    wq->head = wq->tail = 0;
    toku_cond_init(&wq->wait_read, 0);
    wq->want_read = 0;
    toku_cond_init(&wq->wait_write, 0);
    wq->want_write = 0;
    wq->closed = 0;
    wq->n_in_queue = 0;
}

// Destroy a work queue
// Expects: the work queue must be initialized and empty
__attribute__((unused))
static void workqueue_destroy(WORKQUEUE wq) {
    workqueue_lock(wq); // shutup helgrind
    assert(wq->head == 0 && wq->tail == 0);
    workqueue_unlock(wq);
    toku_cond_destroy(&wq->wait_read);
    toku_cond_destroy(&wq->wait_write);
    toku_mutex_destroy(&wq->lock);
}

// Close the work queue
// Effects: signal any threads blocked in the work queue
__attribute__((unused))
static void workqueue_set_closed(WORKQUEUE wq, int dolock) {
    if (dolock) workqueue_lock(wq);
    wq->closed = 1;
    toku_cond_broadcast(&wq->wait_read);
    toku_cond_broadcast(&wq->wait_write);
    if (dolock) workqueue_unlock(wq);
}

// Determine whether or not the work queue is empty
// Returns: 1 if the work queue is empty, otherwise 0
static inline int workqueue_empty(WORKQUEUE wq) {
    return wq->head == 0;
}

// Put a work item at the tail of the work queue
// Effects: append the work item to the end of the work queue and signal
// any work queue readers.
// Dolock controls whether or not the work queue lock should be taken.
__attribute__((unused))
static void workqueue_enq(WORKQUEUE wq, WORKITEM wi, int dolock) {
    if (dolock) workqueue_lock(wq);
    wq->n_in_queue++;
    wi->next = 0;
    if (wq->tail)
        wq->tail->next = wi;
    else
        wq->head = wi;
    wq->tail = wi;
    if (wq->want_read) {
        toku_cond_signal(&wq->wait_read);
    }
    if (dolock) workqueue_unlock(wq);
}

// Get a work item from the head of the work queue
// Effects: wait until the workqueue is not empty, remove the first workitem from the
// queue and return it.
// Dolock controls whether or not the work queue lock should be taken.
// Success: returns 0 and set the wiptr
// Failure: returns non-zero
__attribute__((unused))
static int workqueue_deq(WORKQUEUE wq, WORKITEM *wiptr, int dolock) {
    if (dolock) workqueue_lock(wq);
    assert(wq->n_in_queue >= 0);
    while (workqueue_empty(wq)) {
        if (wq->closed) {
            if (dolock) workqueue_unlock(wq);
            return EINVAL;
        }
        wq->want_read++;
        toku_cond_wait(&wq->wait_read, &wq->lock);
        wq->want_read--;
    }
    wq->n_in_queue--;
    WORKITEM wi = wq->head;
    wq->head = wi->next;
    if (wq->head == 0)
        wq->tail = 0;
    wi->next = 0;
    if (dolock) workqueue_unlock(wq);
    *wiptr = wi;
    return 0;
}

// Suspend the caller (thread that is currently attempting to put more work items into the work queue)
__attribute__((unused))
static void workqueue_wait_write(WORKQUEUE wq, int dolock) {
    if (dolock) workqueue_lock(wq);
    wq->want_write++;
    toku_cond_wait(&wq->wait_write, &wq->lock);
    wq->want_write--;
    if (dolock) workqueue_unlock(wq);
}

// Wakeup all threads that are currently attempting to put more work items into the work queue
__attribute__((unused))
static void workqueue_wakeup_write(WORKQUEUE wq, int dolock) {
    if (wq->want_write) {
        if (dolock) workqueue_lock(wq);
        if (wq->want_write) {
            toku_cond_broadcast(&wq->wait_write);
        }
        if (dolock) workqueue_unlock(wq);
    }
}

__attribute__((unused))
static int workqueue_n_in_queue (WORKQUEUE wq, int dolock) {
    if (dolock) workqueue_lock(wq);
    int r = wq->n_in_queue;
    if (dolock) workqueue_unlock(wq);
    return r;
}

#include "threadpool.h"

// initialize the work queue and worker 
void toku_init_workers(WORKQUEUE wq, THREADPOOL *tpptr, int fraction);

void toku_init_workers_with_num_threads(WORKQUEUE wq, THREADPOOL *tpptr, int num_threads);

// destroy the work queue and worker 
void toku_destroy_workers(WORKQUEUE wq, THREADPOOL *tpptr);

// this is the thread function for the worker threads in the worker thread
// pool. the arg is a pointer to the work queue that feeds work to the
// workers.
void *toku_worker(void *arg);

#endif

