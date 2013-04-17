#ifndef MEMORY_H
#define MEMORY_H

#if defined __cplusplus
extern "C" {
#endif

#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdlib.h>
#include <toku_portability.h>

/* Tokutek memory allocation functions and macros.
 * These are functions for malloc and free */

/* Generally: errno is set to 0 or a value to indicate problems. */

enum typ_tag { TYP_BRTNODE = 0xdead0001,
	       TYP_CACHETABLE, TYP_PAIR, /* for cachetables */
	       TYP_PMA,
	       TYP_GPMA,
               TYP_TOKULOGGER,
	       TYP_TOKUTXN,
	       TYP_LEAFENTRY
};

/* Everything should call toku_malloc() instead of malloc(), and toku_calloc() instead of calloc() */
void *toku_calloc(size_t nmemb, size_t size)  __attribute__((__visibility__("default")));
void *toku_xcalloc(size_t nmemb, size_t size)  __attribute__((__visibility__("default")));
void *toku_malloc(size_t size)  __attribute__((__visibility__("default")));

// xmalloc aborts instead of return NULL if we run out of memory
void *toku_xmalloc(size_t size);
void *toku_xrealloc(void*, size_t size);

/* toku_tagmalloc() performs a malloc(size), but fills in the first 4 bytes with typ.
 * This "tag" is useful if you are debugging and run across a void* that is
 * really a (struct foo *), and you want to figure out what it is.
 */
void *toku_tagmalloc(size_t size, enum typ_tag typ);
void toku_free(void*) __attribute__((__visibility__("default")));
/* toku_free_n() should be used if the caller knows the size of the malloc'd object. */
void toku_free_n(void*, size_t size);
void *toku_realloc(void *, size_t size)  __attribute__((__visibility__("default")));

/* MALLOC is a macro that helps avoid a common error:
 * Suppose I write
 *    struct foo *x = malloc(sizeof(struct foo));
 * That works fine.  But if I change it to this, I've probably made an mistake:
 *    struct foo *x = malloc(sizeof(struct bar));
 * It can get worse, since one might have something like
 *    struct foo *x = malloc(sizeof(struct foo *))
 * which looks reasonable, but it allocoates enough to hold a pointer instead of the amount needed for the struct.
 * So instead, write
 *    struct foo *MALLOC(x);
 * and you cannot go wrong.
 */
#define MALLOC(v) v = toku_malloc(sizeof(*v))
/* MALLOC_N is like calloc(Except no 0ing of data):  It makes an array.  Write
 *   int *MALLOC_N(5,x);
 * to make an array of 5 integers.
 */
#define MALLOC_N(n,v) v = toku_malloc((n)*sizeof(*v))

//CALLOC_N is like calloc with auto-figuring out size of members
#define CALLOC_N(n,v) v = toku_calloc((n), sizeof(*v)) 

#define CALLOC(v) CALLOC_N(1,v)

#define REALLOC_N(n,v) v = toku_realloc(v, (n)*sizeof(*v))

// XMALLOC macros are like MALLOC except they abort if the operation fails
#define XMALLOC(v) v = toku_xmalloc(sizeof(*v))
#define XMALLOC_N(n,v) v = toku_xmalloc((n)*sizeof(*v))
#define XCALLOC_N(n,v) v = toku_xcalloc((n), (sizeof(*v)))

#define XCALLOC(v) XCALLOC_N(1,(v))
#define XREALLOC_N(n,v) v = toku_xrealloc(v, (n)*sizeof(*v))

/* If you have a type such as 
 *    struct pma *PMA;
 * and you define a corresponding int constant, such as
 *    enum typ_tag { TYP_PMA };  
 * then if you do
 *     TAGMALLOC(PMA,v);
 * you declare a variable v of type PMA and malloc a struct pma, and fill
 * in that "tag" with toku_tagmalloc().
 */
#define TAGMALLOC(t,v) t v = toku_tagmalloc(sizeof(*v), TYP_ ## t);

/* Copy memory.  Analogous to strdup() */
void *toku_memdup (const void *v, size_t len);
/* Toku-version of strdup.  Use this so that it calls toku_malloc() */
char *toku_strdup (const char *s)   __attribute__((__visibility__("default")));

/* Copy memory.  Analogous to strdup() Crashes instead of returning NULL */
void *toku_xmemdup (const void *v, size_t len) __attribute__((__visibility__("default")));
/* Toku-version of strdup.  Use this so that it calls toku_xmalloc()  Crashes instead of returning NULL */
char *toku_xstrdup (const char *s)   __attribute__((__visibility__("default")));

void toku_malloc_cleanup (void); /* Before exiting, call this function to free up any internal data structures from toku_malloc.  Otherwise valgrind will complain of memory leaks. */

/* Check to see if everything malloc'd was free.  Might be a no-op depending on how memory.c is configured. */
void toku_memory_check_all_free (void);
/* Check to see if memory is "sane".  Might be a no-op.  Probably better to simply use valgrind. */
void toku_do_memory_check(void);

extern int toku_memory_check; // Set to nonzero to get a (much) slower version of malloc that does (much) more checking.

int toku_get_n_items_malloced(void); /* How many items are malloc'd but not free'd.  May return 0 depending on the configuration of memory.c */
void toku_print_malloced_items(void); /* Try to print some malloced-but-not-freed items.  May be a noop. */
void toku_malloc_report (void); /* report on statistics about number of mallocs.  Maybe a no-op. */ 

// For memory-debug.c  Set this to an array of integers that say which mallocs should return NULL and ENOMEM.
// The array is terminated by a -1.
extern int *toku_dead_mallocs;
extern int toku_malloc_counter; // so you can reset it
extern int toku_realloc_counter;
extern int toku_calloc_counter;
extern int toku_free_counter;

#if defined __cplusplus
};
#endif

#endif
