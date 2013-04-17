/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include "test.h"

#include "includes.h"

static void test_serialize(void) {
    //    struct brt source_brt;
    int nodesize = 1024;
    struct brtnode sn, *dn;
    int fd = open(__FILE__ ".brt", O_RDWR|O_CREAT|O_BINARY, S_IRWXU|S_IRWXG|S_IRWXO);
    int r;
    const u_int32_t randval = random();
    assert(fd>=0);

    //    source_brt.fd=fd;
    char *hello_string;
    sn.nodesize = nodesize;
    sn.ever_been_written = 0;
    sn.flags = 0x11223344;
    sn.thisnodename.b = 20;
    sn.disk_lsn.lsn = 789;
    sn.log_lsn.lsn  = 123456;
    sn.layout_version = BRT_LAYOUT_VERSION;
    sn.height = 1;
    sn.rand4fingerprint = randval;
    sn.local_fingerprint = 0;
    sn.u.n.n_children = 2;
    hello_string = toku_strdup("hello");
    MALLOC_N(2, sn.u.n.childinfos);
    MALLOC_N(1, sn.u.n.childkeys);
    sn.u.n.childkeys[0] = kv_pair_malloc(hello_string, 6, 0, 0); 
    sn.u.n.totalchildkeylens = 6;
    BNC_BLOCKNUM(&sn, 0).b = 30;
    BNC_BLOCKNUM(&sn, 1).b = 35;
    BNC_SUBTREE_FINGERPRINT(&sn, 0) = random();
    BNC_SUBTREE_FINGERPRINT(&sn, 1) = random();
    BNC_SUBTREE_ESTIMATES(&sn, 0).ndata = random() + (((long long)random())<<32);
    BNC_SUBTREE_ESTIMATES(&sn, 1).ndata = random() + (((long long)random())<<32);
    BNC_SUBTREE_ESTIMATES(&sn, 0).nkeys = random() + (((long long)random())<<32);
    BNC_SUBTREE_ESTIMATES(&sn, 1).nkeys = random() + (((long long)random())<<32);
    BNC_SUBTREE_ESTIMATES(&sn, 0).dsize = random() + (((long long)random())<<32);
    BNC_SUBTREE_ESTIMATES(&sn, 1).dsize = random() + (((long long)random())<<32);
    BNC_SUBTREE_ESTIMATES(&sn, 0).exact = random()%2;
    BNC_SUBTREE_ESTIMATES(&sn, 1).exact = random()%2;
    r = toku_fifo_create(&BNC_BUFFER(&sn,0)); assert(r==0);
    r = toku_fifo_create(&BNC_BUFFER(&sn,1)); assert(r==0);
    r = toku_fifo_enq(BNC_BUFFER(&sn,0), "a", 2, "aval", 5, BRT_NONE, (TXNID)0);   assert(r==0);    sn.local_fingerprint += randval*toku_calc_fingerprint_cmd(BRT_NONE, (TXNID)0, "a", 2, "aval", 5);
    r = toku_fifo_enq(BNC_BUFFER(&sn,0), "b", 2, "bval", 5, BRT_NONE, (TXNID)123); assert(r==0);    sn.local_fingerprint += randval*toku_calc_fingerprint_cmd(BRT_NONE, (TXNID)123,  "b", 2, "bval", 5);
    r = toku_fifo_enq(BNC_BUFFER(&sn,1), "x", 2, "xval", 5, BRT_NONE, (TXNID)234); assert(r==0);    sn.local_fingerprint += randval*toku_calc_fingerprint_cmd(BRT_NONE, (TXNID)234, "x", 2, "xval", 5);
    BNC_NBYTESINBUF(&sn, 0) = 2*(BRT_CMD_OVERHEAD+KEY_VALUE_OVERHEAD+2+5);
    BNC_NBYTESINBUF(&sn, 1) = 1*(BRT_CMD_OVERHEAD+KEY_VALUE_OVERHEAD+2+5);
    sn.u.n.n_bytes_in_buffers = 3*(BRT_CMD_OVERHEAD+KEY_VALUE_OVERHEAD+2+5);

    struct brt *XMALLOC(brt);
    struct brt_header *XMALLOC(brt_h);
    struct block_translation_pair *XMALLOC_N(21, btps);
    memset(btps, 0, sizeof(btps));
    brt->h = brt_h;
    brt_h->panic = 0; brt_h->panic_string = 0;
    toku_blocktable_create_new(&brt_h->blocktable);
    toku_blocktable_debug_set_translation(brt_h->blocktable, 1, btps);
    btps[20].diskoff = 4096;
    btps[20].size    = 100;
    {
	u_int64_t b;
        toku_block_alloc(brt_h->blocktable, 100, &b);
	assert(b==4096);
    }
    
    r = toku_serialize_brtnode_to(fd, make_blocknum(20), &sn, brt->h, 1, 1);  
    assert(r==0);
    
    r = toku_deserialize_brtnode_from(fd, make_blocknum(20), 0/*pass zero for hash*/, &dn, brt_h);
    assert(r==0);

    assert(dn->thisnodename.b==20);

    assert(dn->disk_lsn.lsn==123456);
    assert(dn->layout_version ==BRT_LAYOUT_VERSION);
    assert(dn->height == 1);
    assert(dn->rand4fingerprint==randval);
    assert(dn->u.n.n_children==2);
    assert(strcmp(kv_pair_key(dn->u.n.childkeys[0]), "hello")==0);
    assert(toku_brtnode_pivot_key_len(dn, dn->u.n.childkeys[0])==6);
    assert(dn->u.n.totalchildkeylens==6);
    assert(BNC_BLOCKNUM(dn,0).b==30);
    assert(BNC_BLOCKNUM(dn,1).b==35);
    {
	int i;
	for (i=0; i<2; i++) {
	    assert(BNC_SUBTREE_FINGERPRINT(dn, i)==BNC_SUBTREE_FINGERPRINT(&sn, i));
	    assert(BNC_SUBTREE_ESTIMATES(dn, i).nkeys==BNC_SUBTREE_ESTIMATES(&sn, i).nkeys);
	    assert(BNC_SUBTREE_ESTIMATES(dn, i).ndata==BNC_SUBTREE_ESTIMATES(&sn, i).ndata);
	    assert(BNC_SUBTREE_ESTIMATES(dn, i).dsize==BNC_SUBTREE_ESTIMATES(&sn, i).dsize);
	}
	assert(dn->local_fingerprint==sn.local_fingerprint);
    }
#if 0
    {
	bytevec data; ITEMLEN datalen; int type;
	r = toku_hash_find(dn->u.n.buffers[0], "a", 2, &data, &datalen, &type);
	assert(r==0);
	assert(strcmp(data,"aval")==0);
	assert(datalen==5);
        assert(type == BRT_NONE);

	r=toku_hash_find(dn->u.n.buffers[0], "b", 2, &data, &datalen, &type);
	assert(r==0);
	assert(strcmp(data,"bval")==0);
	assert(datalen==5);
        assert(type == BRT_NONE);

	r=toku_hash_find(dn->u.n.buffers[1], "x", 2, &data, &datalen, &type);
	assert(r==0);
	assert(strcmp(data,"xval")==0);
	assert(datalen==5);
        assert(type == BRT_NONE);
    }
#endif
    toku_brtnode_free(&dn);

    kv_pair_free(sn.u.n.childkeys[0]);
    toku_free(hello_string);
    toku_fifo_free(&BNC_BUFFER(&sn,0));
    toku_fifo_free(&BNC_BUFFER(&sn,1));
    toku_free(sn.u.n.childinfos);
    toku_free(sn.u.n.childkeys);

    toku_block_free(brt_h->blocktable, 4096);
    toku_blocktable_destroy(&brt_h->blocktable);
    toku_free(brt_h);
    toku_free(brt);
}

int
test_main (int argc __attribute__((__unused__)), const char *argv[] __attribute__((__unused__))) {
    toku_memory_check = 1;
    test_serialize();
    toku_malloc_cleanup();
    return 0;
}
