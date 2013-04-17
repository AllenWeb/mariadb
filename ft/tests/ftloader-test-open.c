/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "$Id$"
#ident "Copyright (c) 2010 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

// The purpose of this test is to find memory leaks in the ft_loader_open function.  Right now, it finds leaks in some very simple
// cases. 

#define DONT_DEPRECATE_MALLOC
#include "test.h"
#include "ftloader.h"
#include "ftloader-internal.h"
#include "memory.h"

#if defined(__cplusplus)
extern "C" {
#endif

static int my_malloc_count = 0;
static int my_malloc_trigger = 0;

static void set_my_malloc_trigger(int n) {
    my_malloc_count = 0;
    my_malloc_trigger = n;
}

static void *my_malloc(size_t n) {
    my_malloc_count++;
    if (my_malloc_count == my_malloc_trigger) {
        errno = ENOSPC;
        return NULL;
    } else
        return malloc(n);
}

static int my_compare(DB *UU(desc), const DBT *UU(akey), const DBT *UU(bkey)) {
    return EINVAL;
}

static void test_loader_open(int ndbs) {
    int r;
    FTLOADER loader;

    // open the ft_loader. this runs the extractor.
    FT_HANDLE brts[ndbs];
    DB* dbs[ndbs];
    const char *fnames[ndbs];
    ft_compare_func compares[ndbs];
    for (int i = 0; i < ndbs; i++) {
        brts[i] = NULL;
        dbs[i] = NULL;
        fnames[i] = "";
        compares[i] = my_compare;
    }

    toku_set_func_malloc(my_malloc);

    int i;
    for (i = 0; ; i++) {
        set_my_malloc_trigger(i+1);

        r = toku_ft_loader_open(&loader, NULL, NULL, NULL, ndbs, brts, dbs, fnames, compares, "", ZERO_LSN, TXNID_NONE);
        if (r == 0)
            break;
    }

    if (verbose) printf("i=%d\n", i);
    
    r = toku_ft_loader_abort(loader, TRUE);
    assert(r == 0);
}

int test_main (int argc, const char *argv[]) {
    const char *progname=argv[0];
    argc--; argv++;
    while (argc>0) {
	if (strcmp(argv[0],"-v")==0) {
	    verbose=1;
	} else if (strcmp(argv[0],"-q")==0) {
	    verbose=0;
	} else if (argc!=1) {
	    fprintf(stderr, "Usage:\n %s [-v] [-q]\n", progname);
	    exit(1);
	}
        else {
            break;
        }
	argc--; argv++;
    }

    test_loader_open(0);
    test_loader_open(1);

    return 0;
}

#if defined(__cplusplus)
}
#endif
