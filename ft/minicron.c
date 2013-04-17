/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007-2010 Tokutek Inc.  All rights reserved."
#ident "$Id$"

#include <toku_portability.h>
#include <errno.h>
#include <string.h>

#include "toku_assert.h"
#include "fttypes.h"
#include "minicron.h"

static void
toku_gettime (toku_timespec_t *a) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    a->tv_sec  = tv.tv_sec;
    a->tv_nsec = tv.tv_usec * 1000LL;
}
    

static int
timespec_compare (toku_timespec_t *a, toku_timespec_t *b) {
    if (a->tv_sec > b->tv_sec) return 1;
    if (a->tv_sec < b->tv_sec) return -1;
    if (a->tv_nsec > b->tv_nsec) return 1;
    if (a->tv_nsec < b->tv_nsec) return -1;
    return 0;
}

// Implementation notes:
//  When calling do_shutdown or change_period, the mutex is obtained, the variables in the minicron struct are modified, and
//  the condition variable is signalled.  Possibly the minicron thread will miss the signal.  To avoid this problem, whenever
//  the minicron thread acquires the mutex, it must check to see what the variables say to do (e.g., should it shut down?).

static void*
minicron_do (void *pv)
{
    struct minicron *p = cast_to_typeof(p) pv;
    toku_mutex_lock(&p->mutex);
    while (1) {
	if (p->do_shutdown) {
	    toku_mutex_unlock(&p->mutex);
	    return 0;
	}
	if (p->period_in_seconds==0) {
	    // if we aren't supposed to do it then just do an untimed wait.
	    toku_cond_wait(&p->condvar, &p->mutex);
	} else {
	    // Recompute the wakeup time every time (instead of once per call to f) in case the period changges.
	    toku_timespec_t wakeup_at = p->time_of_last_call_to_f;
	    wakeup_at.tv_sec += p->period_in_seconds;
	    toku_timespec_t now;
	    toku_gettime(&now);
	    //printf("wakeup at %.6f (after %d seconds) now=%.6f\n", wakeup_at.tv_sec + wakeup_at.tv_nsec*1e-9, p->period_in_seconds, now.tv_sec + now.tv_nsec*1e-9);
	    int r = toku_cond_timedwait(&p->condvar, &p->mutex, &wakeup_at);
	    if (r!=0 && r!=ETIMEDOUT) fprintf(stderr, "%s:%d r=%d (%s)", __FILE__, __LINE__, r, strerror(r));
	    assert(r==0 || r==ETIMEDOUT);
	}
	// Now we woke up, and we should figure out what to do
	if (p->do_shutdown) {
	    toku_mutex_unlock(&p->mutex);
	    return 0;
	}
	if (p->period_in_seconds >0) {
	    // maybe do a checkpoint
	    toku_timespec_t now;
	    toku_gettime(&now);
	    toku_timespec_t time_to_call = p->time_of_last_call_to_f;
	    time_to_call.tv_sec += p->period_in_seconds;
	    int compare = timespec_compare(&time_to_call, &now);
	    //printf("compare(%.6f, %.6f)=%d\n", time_to_call.tv_sec + time_to_call.tv_nsec*1e-9, now.tv_sec+now.tv_nsec*1e-9, compare);
	    if (compare <= 0) {
		toku_mutex_unlock(&p->mutex);
		int r = p->f(p->arg);
		assert(r==0);
		toku_mutex_lock(&p->mutex);
		toku_gettime(&p->time_of_last_call_to_f); // the period is measured between calls to f.
		
	    }
	}
    }
}

int
toku_minicron_setup(struct minicron *p, u_int32_t period_in_seconds, int(*f)(void *), void *arg)
{
    p->f = f;
    p->arg = arg;
    toku_gettime(&p->time_of_last_call_to_f);
    //printf("now=%.6f", p->time_of_last_call_to_f.tv_sec + p->time_of_last_call_to_f.tv_nsec*1e-9);
    p->period_in_seconds = period_in_seconds; 
    p->do_shutdown = FALSE;
    toku_mutex_init(&p->mutex, 0);
    toku_cond_init (&p->condvar, 0);
    //printf("%s:%d setup period=%d\n", __FILE__, __LINE__, period_in_seconds);
    return toku_pthread_create(&p->thread, 0, minicron_do, p);
}
    
int
toku_minicron_change_period(struct minicron *p, u_int32_t new_period)
{
    toku_mutex_lock(&p->mutex);
    p->period_in_seconds = new_period;
    toku_cond_signal(&p->condvar);
    toku_mutex_unlock(&p->mutex);
    return 0;
}

u_int32_t
toku_minicron_get_period(struct minicron *p)
{
    toku_mutex_lock(&p->mutex);
    u_int32_t retval = toku_minicron_get_period_unlocked(p);
    toku_mutex_unlock(&p->mutex);
    return retval;
}

/* unlocked function for use by engine status which takes no locks */
u_int32_t
toku_minicron_get_period_unlocked(struct minicron *p)
{
    u_int32_t retval = p->period_in_seconds;
    return retval;
}

int
toku_minicron_shutdown(struct minicron *p) {
    toku_mutex_lock(&p->mutex);
    assert(!p->do_shutdown);
    p->do_shutdown = TRUE;
    //printf("%s:%d signalling\n", __FILE__, __LINE__);
    toku_cond_signal(&p->condvar);
    toku_mutex_unlock(&p->mutex);
    void *returned_value;
    //printf("%s:%d joining\n", __FILE__, __LINE__);
    int r = toku_pthread_join(p->thread, &returned_value);
    if (r!=0) fprintf(stderr, "%s:%d r=%d (%s)\n", __FILE__, __LINE__, r, strerror(r));
    assert(r==0);  assert(returned_value==0);
    toku_cond_destroy(&p->condvar);
    toku_mutex_destroy(&p->mutex);
    //printf("%s:%d shutdowned\n", __FILE__, __LINE__);
    return 0;
}

BOOL
toku_minicron_has_been_shutdown(struct minicron *p) {
    return p->do_shutdown;
}
