/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "$Id$"
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include "includes.h"
#include "test.h"

// this mutex is used by some of the tests to serialize access to some
// global data, especially between the test thread and the cachetable
// writeback threads

toku_mutex_t  test_mutex;

static inline void test_mutex_init(void) {
    toku_mutex_init(&test_mutex, 0);
}

static inline void test_mutex_destroy(void) {
    toku_mutex_destroy(&test_mutex);
}

static inline void test_mutex_lock(void) {
    toku_mutex_lock(&test_mutex);
}

static inline void test_mutex_unlock(void) {
    toku_mutex_unlock(&test_mutex);
}

static void maybe_flush(CACHETABLE t) {
    toku_cachetable_maybe_flush_some(t);
}

static const int test_object_size = 1;

static CACHETABLE ct;

enum { N_PRESENT_LIMIT = 4, TRIALS=20000, N_FILES=2 };
static int n_present=0;
static struct present_items {
    CACHEKEY key;
    CACHEFILE cf;
} present_items[N_PRESENT_LIMIT];

static void print_ints(void) __attribute__((__unused__));
static void print_ints(void) {
    int i;
    for (i=0; i<n_present; i++) {
	if (i==0) printf("{"); else printf(",");
	printf("{%" PRId64 ",%p}", present_items[i].key.b, present_items[i].cf);
    }
    printf("}\n");
}

static void item_becomes_present(CACHETABLE thect, CACHEFILE cf, CACHEKEY key) {
    test_mutex_lock();
    while (n_present >= N_PRESENT_LIMIT) {
        test_mutex_unlock(); toku_pthread_yield(); maybe_flush(thect); test_mutex_lock();
    }
    assert(n_present<N_PRESENT_LIMIT);
    present_items[n_present].cf     = cf;
    present_items[n_present].key    = key;
    n_present++;
    test_mutex_unlock();
}

static void item_becomes_not_present(CACHEFILE cf, CACHEKEY key) {
    int i;
    //printf("Removing {%4lld %16p}: Initially: ", key, cf); print_ints();
    test_mutex_lock();
    assert(n_present<=N_PRESENT_LIMIT);
    for (i=0; i<n_present; i++) {
	if (present_items[i].cf==cf && present_items[i].key.b==key.b) {
	    present_items[i]=present_items[n_present-1];
	    n_present--;
            test_mutex_unlock();
	    //printf("                                    Finally: "); print_ints();
	    return;
	}
    }
    printf("Whoops, %p,%" PRId64 " was already not present\n", cf ,key.b);
    abort();
    test_mutex_unlock();
}

static void file_is_not_present(CACHEFILE cf) {
    int i;
    test_mutex_lock();
    for (i=0; i<n_present; i++) {
	assert(present_items[i].cf!=cf);
    }
    test_mutex_unlock();
}


static void flush_forchain (CACHEFILE f            __attribute__((__unused__)),
                            int UU(fd),
			    CACHEKEY  key,
			    void     *value,
			    void** UU(dd),
			    void     *extra        __attribute__((__unused__)),
			    PAIR_ATTR      size         __attribute__((__unused__)),
        PAIR_ATTR* new_size      __attribute__((__unused__)),
			    BOOL      write_me     __attribute__((__unused__)),
			    BOOL      keep_me      __attribute__((__unused__)),
			    BOOL      for_checkpoint     __attribute__((__unused__)),
        BOOL UU(is_clone)
			    ) {
    if (keep_me) return;
    int *v = value;
    //toku_cachetable_print_state(ct);
    //printf("Flush %lld %d\n", key, (int)value);
    assert((long)v==(long)key.b);
    item_becomes_not_present(f, key);
    //print_ints();
}

static int fetch_forchain (CACHEFILE f, int UU(fd), CACHEKEY key, u_int32_t fullhash, void**value, 
			   void** UU(dd),
PAIR_ATTR *sizep __attribute__((__unused__)), int * dirtyp, void*extraargs) {
    assert(toku_cachetable_hash(f, key)==fullhash);
    assert((long)extraargs==(long)key.b);
    *value = (void*)(long)key.b;
    *dirtyp = 0;
    return 0;
}

static void verify_cachetable_against_present (void) {
    int i;

again:
    test_mutex_lock();
    int my_n_present = n_present;
    struct present_items my_present_items[N_PRESENT_LIMIT];
    for (i=0; i<n_present; i++)
        my_present_items[i] = present_items[i];
    test_mutex_unlock();

    for (i=0; i<my_n_present; i++) {
	void *v;
	u_int32_t fullhash = toku_cachetable_hash(my_present_items[i].cf, my_present_items[i].key);
	int r=toku_cachetable_maybe_get_and_pin_clean(my_present_items[i].cf,
						my_present_items[i].key,
						toku_cachetable_hash(my_present_items[i].cf, my_present_items[i].key),
						&v);
        if (r == -1) goto again;
	assert(r==0);
	r = toku_cachetable_unpin(my_present_items[i].cf, my_present_items[i].key, fullhash, CACHETABLE_CLEAN, make_pair_attr(test_object_size));
    }
}


static void test_chaining (void) {
    /* Make sure that the hash chain and the LRU list don't get confused. */
    CACHEFILE f[N_FILES];
    enum { FILENAME_LEN=100 };
    char fname[N_FILES][FILENAME_LEN];
    int r;
    long i, trial;
    r = toku_create_cachetable(&ct, N_PRESENT_LIMIT, ZERO_LSN, NULL_LOGGER);    assert(r==0);
    for (i=0; i<N_FILES; i++) {
	r = snprintf(fname[i], FILENAME_LEN, __SRCFILE__ ".%ld.dat", i);
	assert(r>0 && r<FILENAME_LEN);
	unlink(fname[i]);
	r = toku_cachetable_openf(&f[i], ct, fname[i], O_RDWR|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO);   assert(r==0);
	}
    for (i=0; i<N_PRESENT_LIMIT; i++) {
	int fnum = i%N_FILES;
	//printf("%s:%d Add %d\n", __SRCFILE__, __LINE__, i);
	u_int32_t fhash = toku_cachetable_hash(f[fnum], make_blocknum(i));
        CACHETABLE_WRITE_CALLBACK wc = def_write_callback((void *)i);
        wc.flush_callback = flush_forchain;
	r = toku_cachetable_put(f[fnum], make_blocknum(i), fhash, (void*)i, make_pair_attr(test_object_size), wc);
	assert(r==0);
	item_becomes_present(ct, f[fnum], make_blocknum(i));
	r = toku_cachetable_unpin(f[fnum], make_blocknum(i), fhash, CACHETABLE_CLEAN, make_pair_attr(test_object_size));
	assert(r==0);
	//print_ints();
    }
    for (trial=0; trial<TRIALS; trial++) {
        test_mutex_lock(); 
        int my_n_present = n_present;
        test_mutex_unlock();
	if (my_n_present>0) {
	    // First touch some random ones
            test_mutex_lock();
	    int whichone = random()%n_present;
            CACHEFILE whichcf = present_items[whichone].cf;
            CACHEKEY whichkey = present_items[whichone].key;
            test_mutex_unlock();
	    void *value;
	    //printf("Touching %d (%lld, %p)\n", whichone, whichkey, whichcf);
	    u_int32_t fhash = toku_cachetable_hash(whichcf, whichkey);
            CACHETABLE_WRITE_CALLBACK wc = def_write_callback((void*)(long)whichkey.b);
            wc.flush_callback = flush_forchain;
	    r = toku_cachetable_get_and_pin(whichcf,
					    whichkey,
					    fhash,
					    &value,
					    NULL,
					    wc,
					    fetch_forchain,
					    def_pf_req_callback,
					    def_pf_callback,
					    TRUE, 
                                            (void*)(long)whichkey.b
					    );
	    assert(r==0);
	    r = toku_cachetable_unpin(whichcf,
				      whichkey,
				      fhash,
				      CACHETABLE_CLEAN, make_pair_attr(test_object_size));
	    assert(r==0);
	}

	i += 1+ random()%100;
	int fnum = i%N_FILES;
	// i is always incrementing, so we need not worry about inserting a duplicate
        // if i is a duplicate, cachetable_put will return -1
	// printf("%s:%d Add {%ld,%p}\n", __SRCFILE__, __LINE__, i, f[fnum]);
	u_int32_t fhash = toku_cachetable_hash(f[fnum], make_blocknum(i));
        CACHETABLE_WRITE_CALLBACK wc = def_write_callback((void *)i);
        wc.flush_callback = flush_forchain;
	r = toku_cachetable_put(f[fnum], make_blocknum(i), fhash, (void*)i, make_pair_attr(test_object_size), wc);
        assert(r==0 || r==-1);
        if (r==0) {
            item_becomes_present(ct, f[fnum], make_blocknum(i));
            //print_ints();
            //cachetable_print_state(ct);
            r = toku_cachetable_unpin(f[fnum], make_blocknum(i), fhash, CACHETABLE_CLEAN, make_pair_attr(test_object_size));
            assert(r==0);
        }

        // now that we have a clock instead of an LRU, there
	// is no guarantee that PAIR stays in cachetable

        //long long pinned;
        //r = toku_cachetable_get_key_state(ct, make_blocknum(i), f[fnum], 0, 0, &pinned, 0);
        //assert(r==0);
        //assert(pinned == 0);
	verify_cachetable_against_present();

	if (random()%10==0) {
	    i = random()%N_FILES;
	    //printf("Close %d (%p), now n_present=%d\n", i, f[i], n_present);
	    //print_ints();
	    CACHEFILE oldcf=f[i];
	    r = toku_cachefile_close(&f[i], 0, FALSE, ZERO_LSN);                           assert(r==0);
	    file_is_not_present(oldcf);
	    r = toku_cachetable_openf(&f[i], ct, fname[i], O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO); assert(r==0);
	}
    }
    for (i=0; i<N_FILES; i++) {
	r = toku_cachefile_close(&f[i], 0, FALSE, ZERO_LSN); assert(r==0);
    }
    r = toku_cachetable_close(&ct); assert(r==0);
}

static void __attribute__((__noreturn__))
usage (const char *progname) {
    fprintf(stderr, "Usage:\n %s [-v] [-q]\n", progname);
    exit(1);
}

int
test_main (int argc, const char *argv[]) {
    default_parse_args(argc, argv);
    test_mutex_init();
    test_chaining();
    test_mutex_destroy();
    
    if (verbose) printf("ok\n");
    return 0;
}
