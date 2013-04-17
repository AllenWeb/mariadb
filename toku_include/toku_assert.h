/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ifndef TOKU_ASSERT_H
#define TOKU_ASSERT_H
/* The problem with assert.h:  If NDEBUG is set then it doesn't execute the function, if NDEBUG isn't set then we get a branch that isn't taken. */
/* This version will complain if NDEBUG is set. */
/* It evaluates the argument and then calls a function  toku_do_assert() which takes all the hits for the branches not taken. */

#include <stdint.h>
#include "errno.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef NDEBUG
#error NDEBUG should not be set
#endif


void toku_assert_init(void) __attribute__((constructor));

void toku_assert_set_fpointers(int (*toku_maybe_get_engine_status_text_pointer)(char*, int), 
			       void (*toku_maybe_set_env_panic_pointer)(int, char*),
                               uint64_t num_rows);

void toku_do_assert(int /*expr*/,const char*/*expr_as_string*/,const char */*fun*/,const char*/*file*/,int/*line*/, int/*errno*/) __attribute__((__visibility__("default")));

void toku_do_assert_fail(const char*/*expr_as_string*/,const char */*fun*/,const char*/*file*/,int/*line*/, int/*errno*/) __attribute__((__visibility__("default"))) __attribute__((__noreturn__));
void toku_do_assert_zero_fail(uintptr_t/*expr*/, const char*/*expr_as_string*/,const char */*fun*/,const char*/*file*/,int/*line*/, int/*errno*/) __attribute__((__visibility__("default"))) __attribute__((__noreturn__));

// Define GCOV if you want to get test-coverage information that ignores the assert statements.
// #define GCOV

extern void (*do_assert_hook)(void); // Set this to a function you want called after printing the assertion failure message but before calling abort().  By default this is NULL.

#if defined(GCOV) || TOKU_WINDOWS
#define assert(expr)      toku_do_assert((expr) != 0, #expr, __FUNCTION__, __FILE__, __LINE__, errno)
#define assert_zero(expr) toku_do_assert((expr) == 0, #expr, __FUNCTION__, __FILE__, __LINE__, errno)
#else
#define assert(expr)      ((expr)      ? (void)0 : toku_do_assert_fail(#expr, __FUNCTION__, __FILE__, __LINE__, errno))
#define assert_zero(expr) ((expr) == 0 ? (void)0 : toku_do_assert_zero_fail((uintptr_t)(expr), #expr, __FUNCTION__, __FILE__, __LINE__, errno))
#endif

#ifdef GCOV
#define WHEN_GCOV(x) x
#define WHEN_NOT_GCOV(x)
#else
#define WHEN_GCOV(x)
#define WHEN_NOT_GCOV(x) x
#endif

#define lazy_assert(a)          assert(a)      // indicates code is incomplete 
#define lazy_assert_zero(a)     assert_zero(a) // indicates code is incomplete 
#define invariant(a)            assert(a)      // indicates a code invariant that must be true
#define invariant_null(a)       assert_zero(a) // indicates a code invariant that must be true
#define invariant_notnull(a)    assert(a)      // indicates a code invariant that must be true
#define invariant_zero(a)       assert_zero(a) // indicates a code invariant that must be true
#define resource_assert(a)      assert(a)      // indicates resource must be available, otherwise unrecoverable
#define resource_assert_zero(a) assert_zero(a) // indicates resource must be available, otherwise unrecoverable

#if defined(__cplusplus)
};
#endif

#endif
