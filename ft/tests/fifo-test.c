/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#include "includes.h"


#include "test.h"

static void
test_fifo_create (void) {
    int r;
    FIFO f;

    f = 0;
    r = toku_fifo_create(&f); 
    assert(r == 0); assert(f != 0);

    toku_fifo_free(&f);
    assert(f == 0);
}

static void
test_fifo_enq (int n) {
    int r;
    FIFO f;
    MSN startmsn = ZERO_MSN;

    f = 0;
    r = toku_fifo_create(&f); 
    assert(r == 0); assert(f != 0);

    char *thekey = 0; int thekeylen;
    char *theval = 0; int thevallen;

    // this was a function but icc cant handle it    
#define buildkey(len) { \
        thekeylen = len+1; \
        XREALLOC_N(thekeylen, thekey); \
        memset(thekey, len, thekeylen); \
    }

#define buildval(len) { \
        thevallen = len+2; \
        XREALLOC_N(thevallen, theval); \
        memset(theval, ~len, thevallen); \
    }

    for (int i=0; i<n; i++) {
        buildkey(i);
        buildval(i);
        XIDS xids;
        if (i==0)
            xids = xids_get_root_xids();
        else {
            r = xids_create_child(xids_get_root_xids(), &xids, (TXNID)i);
            assert(r==0);
        }
        MSN msn = next_dummymsn();
        if (startmsn.msn == ZERO_MSN.msn)
            startmsn = msn;
        enum ft_msg_type type = (enum ft_msg_type) i;
        r = toku_fifo_enq(f, thekey, thekeylen, theval, thevallen, type, msn, xids, true, NULL); assert(r == 0);
        xids_destroy(&xids);
    }

    int i = 0;
    FIFO_ITERATE(f, key, keylen, val, vallen, type, msn, xids, UU(is_fresh), {
        if (verbose) printf("checkit %d %d %" PRIu64 "\n", i, type, msn.msn);
        assert(msn.msn == startmsn.msn + i);
        buildkey(i);
        buildval(i);
        assert((int) keylen == thekeylen); assert(memcmp(key, thekey, keylen) == 0);
        assert((int) vallen == thevallen); assert(memcmp(val, theval, vallen) == 0);
        assert(i % 256 == (int)type);
	assert((TXNID)i==xids_get_innermost_xid(xids));
        i += 1;
    });
    assert(i == n);

    if (thekey) toku_free(thekey);
    if (theval) toku_free(theval);

    toku_fifo_free(&f);
    assert(f == 0);
}

int
test_main(int argc, const char *argv[]) {
    default_parse_args(argc, argv);
    initialize_dummymsn();
    test_fifo_create();
    test_fifo_enq(4);
    test_fifo_enq(512);
    
    return 0;
}
