/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ident "$Id$"
#ident "Copyright (c) 2007-2010 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include "includes.h"
#include "sort.h"
#include "threadpool.h"
#include <compress.h>

#if defined(HAVE_CILK)
#include <cilk/cilk.h>
#define cilk_worker_count (__cilkrts_get_nworkers())
#else
#define cilk_spawn
#define cilk_sync
#define cilk_for for
#define cilk_worker_count 1
#endif



static BRT_UPGRADE_STATUS_S brt_upgrade_status;

#define UPGRADE_STATUS_INIT(k,t,l) {                            \
        brt_upgrade_status.status[k].keyname = #k;              \
        brt_upgrade_status.status[k].type    = t;               \
        brt_upgrade_status.status[k].legend  = "brt upgrade: " l;       \
    }

static void
status_init(void)
{
    // Note, this function initializes the keyname, type, and legend fields.
    // Value fields are initialized to zero by compiler.
    UPGRADE_STATUS_INIT(BRT_UPGRADE_FOOTPRINT,             UINT64, "footprint");
    brt_upgrade_status.initialized = true;
}
#undef UPGRADE_STATUS_INIT

#define UPGRADE_STATUS_VALUE(x) brt_upgrade_status.status[x].value.num

void
toku_brt_upgrade_get_status(BRT_UPGRADE_STATUS s) {
    if (!brt_upgrade_status.initialized) {
        status_init();
    }
    UPGRADE_STATUS_VALUE(BRT_UPGRADE_FOOTPRINT) = toku_log_upgrade_get_footprint();
    *s = brt_upgrade_status;
}


// performance tracing
#define DO_TOKU_TRACE 0
#if DO_TOKU_TRACE

static inline void do_toku_trace(const char *cp, int len) {
    const int toku_trace_fd = -1;
    write(toku_trace_fd, cp, len);
}
#define toku_trace(a)  do_toku_trace(a, strlen(a))
#else
#define toku_trace(a)
#endif

static int num_cores = 0; // cache the number of cores for the parallelization
static struct toku_thread_pool *brt_pool = NULL;

int 
toku_brt_serialize_init(void) {
    num_cores = toku_os_get_number_active_processors();
    int r = toku_thread_pool_create(&brt_pool, num_cores); lazy_assert_zero(r);
    return 0;
}

int 
toku_brt_serialize_destroy(void) {
    toku_thread_pool_destroy(&brt_pool);
    return 0;
}

// This mutex protects pwrite from running in parallel, and also protects modifications to the block allocator.
static toku_pthread_mutex_t pwrite_mutex = TOKU_PTHREAD_MUTEX_INITIALIZER;
static int pwrite_is_locked=0;

int 
toku_pwrite_lock_init(void) {
    int r = toku_pthread_mutex_init(&pwrite_mutex, NULL); resource_assert_zero(r);
    return r;
}

int 
toku_pwrite_lock_destroy(void) {
    int r = toku_pthread_mutex_destroy(&pwrite_mutex); resource_assert_zero(r);
    return r;
}

static inline void
lock_for_pwrite (void) {
    // Locks the pwrite_mutex.
    int r = toku_pthread_mutex_lock(&pwrite_mutex); resource_assert_zero(r);
    pwrite_is_locked = 1;
}

static inline void
unlock_for_pwrite (void) {
    pwrite_is_locked = 0;
    int r = toku_pthread_mutex_unlock(&pwrite_mutex); resource_assert_zero(r);
}

enum {FILE_CHANGE_INCREMENT = (16<<20)};

static inline u_int64_t 
alignup64(u_int64_t a, u_int64_t b) {
    return ((a+b-1)/b)*b;
}

//Race condition if ydb lock is split.
//Ydb lock is held when this function is called.
//Not going to truncate and delete (redirect to devnull) at same time.
//Must be holding a read or write lock on fdlock (fd is protected)
void
toku_maybe_truncate_cachefile (CACHEFILE cf, int fd, u_int64_t size_used)
// Effect: If file size >= SIZE+32MiB, reduce file size.
// (32 instead of 16.. hysteresis).
// Return 0 on success, otherwise an error number.
{
    //Check file size before taking pwrite lock to reduce likelihood of taking
    //the pwrite lock needlessly.
    //Check file size after taking lock to avoid race conditions.
    int64_t file_size;
    if (toku_cachefile_is_dev_null_unlocked(cf)) goto done;
    {
        int r = toku_os_get_file_size(fd, &file_size);
        lazy_assert_zero(r);
        invariant(file_size >= 0);
    }
    // If file space is overallocated by at least 32M
    if ((u_int64_t)file_size >= size_used + (2*FILE_CHANGE_INCREMENT)) {
        lock_for_pwrite();
        {
            int r = toku_os_get_file_size(fd, &file_size);
            lazy_assert_zero(r);
            invariant(file_size >= 0);
        }
        if ((u_int64_t)file_size >= size_used + (2*FILE_CHANGE_INCREMENT)) {
            toku_off_t new_size = alignup64(size_used, (2*FILE_CHANGE_INCREMENT)); //Truncate to new size_used.
            invariant(new_size < file_size);
            int r = toku_cachefile_truncate(cf, new_size);
            lazy_assert_zero(r);
        }
        unlock_for_pwrite();
    }
done:
    return;
}

static u_int64_t 
umin64(u_int64_t a, u_int64_t b) {
    if (a<b) return a;
    return b;
}

int
maybe_preallocate_in_file (int fd, u_int64_t size)
// Effect: If file size is less than SIZE, make it bigger by either doubling it or growing by 16MiB whichever is less.
// Return 0 on success, otherwise an error number.
{
    int64_t file_size;
    {
        int r = toku_os_get_file_size(fd, &file_size);
        if (r != 0) { // debug #2463
            int the_errno = errno;
            fprintf(stderr, "%s:%d fd=%d size=%"PRIu64" r=%d errno=%d\n", __FUNCTION__, __LINE__, fd, size, r, the_errno); fflush(stderr);
        }
        lazy_assert_zero(r);
    }
    invariant(file_size >= 0);
    if ((u_int64_t)file_size < size) {
	const int N = umin64(size, FILE_CHANGE_INCREMENT); // Double the size of the file, or add 16MiB, whichever is less.
	char *MALLOC_N(N, wbuf);
	memset(wbuf, 0, N);
	toku_off_t start_write = alignup64(file_size, 4096);
	invariant(start_write >= file_size);
	toku_os_full_pwrite(fd, wbuf, N, start_write);
	toku_free(wbuf);
    }
    return 0;
}

static void
toku_full_pwrite_extend (int fd, const void *buf, size_t count, toku_off_t offset)
// requires that the pwrite has been locked
// On failure, this does not return (an assertion fails or something).
{
    invariant(pwrite_is_locked);
    {
        int r = maybe_preallocate_in_file(fd, offset+count);
        lazy_assert_zero(r);
    }
    toku_os_full_pwrite(fd, buf, count, offset);
}

// Don't include the sub_block header
// Overhead calculated in same order fields are written to wbuf
enum {
    node_header_overhead = (8+   // magic "tokunode" or "tokuleaf" or "tokuroll"
                            4+   // layout_version
                            4+   // layout_version_original
                            4),  // build_id
};

#include "sub_block.h"
#include "sub_block_map.h"

// uncompressed header offsets
enum {
    uncompressed_magic_offset = 0,
    uncompressed_version_offset = 8,
};

static u_int32_t
serialize_node_header_size(BRTNODE node) {
    u_int32_t retval = 0;
    retval += 8; // magic
    retval += sizeof(node->layout_version);
    retval += sizeof(node->layout_version_original);
    retval += 4; // BUILD_ID
    retval += 4; // n_children
    retval += node->n_children*8; // encode start offset and length of each partition
    retval += 4; // checksum
    return retval;
}

static void
serialize_node_header(BRTNODE node, BRTNODE_DISK_DATA ndd, struct wbuf *wbuf) {
    if (node->height == 0) 
        wbuf_nocrc_literal_bytes(wbuf, "tokuleaf", 8);
    else 
        wbuf_nocrc_literal_bytes(wbuf, "tokunode", 8);
    invariant(node->layout_version == BRT_LAYOUT_VERSION);
    wbuf_nocrc_int(wbuf, node->layout_version);
    wbuf_nocrc_int(wbuf, node->layout_version_original);
    wbuf_nocrc_uint(wbuf, BUILD_ID);
    wbuf_nocrc_int (wbuf, node->n_children);
    for (int i=0; i<node->n_children; i++) {
        assert(BP_SIZE(ndd,i)>0);
        wbuf_nocrc_int(wbuf, BP_START(ndd, i)); // save the beginning of the partition
        wbuf_nocrc_int(wbuf, BP_SIZE (ndd, i));         // and the size
    }
    // checksum the header
    u_int32_t end_to_end_checksum = x1764_memory(wbuf->buf, wbuf_get_woffset(wbuf));
    wbuf_nocrc_int(wbuf, end_to_end_checksum);
    invariant(wbuf->ndone == wbuf->size);
}

static int
wbufwriteleafentry (OMTVALUE lev, u_int32_t UU(idx), void *v) {
    LEAFENTRY le=lev;
    struct wbuf *thisw=v;
    wbuf_nocrc_LEAFENTRY(thisw, le);
    return 0;
}

static u_int32_t 
serialize_brtnode_partition_size (BRTNODE node, int i)
{
    u_int32_t result = 0;
    assert(node->bp[i].state == PT_AVAIL);
    result++; // Byte that states what the partition is
    if (node->height > 0) {
        result += 4; // size of bytes in buffer table
        result += toku_bnc_nbytesinbuf(BNC(node, i));
    }
    else {
        result += 4; // n_entries in buffer table
        result += BLB_NBYTESINBUF(node, i);
    }
    result += 4; // checksum
    return result;
}

#define BRTNODE_PARTITION_OMT_LEAVES 0xaa
#define BRTNODE_PARTITION_FIFO_MSG 0xbb

static void
serialize_nonleaf_childinfo(NONLEAF_CHILDINFO bnc, struct wbuf *wb)
{
    unsigned char ch = BRTNODE_PARTITION_FIFO_MSG;
    wbuf_nocrc_char(wb, ch);
    // serialize the FIFO, first the number of entries, then the elements
    wbuf_nocrc_int(wb, toku_bnc_n_entries(bnc));
    FIFO_ITERATE(
        bnc->buffer, key, keylen, data, datalen, type, msn, xids, is_fresh,
        {
            invariant((int)type>=0 && type<256);
            wbuf_nocrc_char(wb, (unsigned char)type);
            wbuf_nocrc_char(wb, (unsigned char)is_fresh);
            wbuf_MSN(wb, msn);
            wbuf_nocrc_xids(wb, xids);
            wbuf_nocrc_bytes(wb, key, keylen);
            wbuf_nocrc_bytes(wb, data, datalen);
        });
}

//
// Serialize the i'th partition of node into sb
// For leaf nodes, this would be the i'th basement node
// For internal nodes, this would be the i'th internal node
//
static void
serialize_brtnode_partition(BRTNODE node, int i, struct sub_block *sb) {
    assert(sb->uncompressed_size == 0);
    assert(sb->uncompressed_ptr == NULL);
    sb->uncompressed_size = serialize_brtnode_partition_size(node,i);
    sb->uncompressed_ptr = toku_xmalloc(sb->uncompressed_size);
    //
    // Now put the data into sb->uncompressed_ptr
    //
    struct wbuf wb;
    wbuf_init(&wb, sb->uncompressed_ptr, sb->uncompressed_size);
    if (node->height > 0) {
        // TODO: (Zardosht) possibly exit early if there are no messages
        serialize_nonleaf_childinfo(BNC(node, i), &wb);
    }
    else {
        unsigned char ch = BRTNODE_PARTITION_OMT_LEAVES;
        wbuf_nocrc_char(&wb, ch);
        wbuf_nocrc_uint(&wb, toku_omt_size(BLB_BUFFER(node, i)));

        //
        // iterate over leafentries and place them into the buffer
        //
        toku_omt_iterate(BLB_BUFFER(node, i), wbufwriteleafentry, &wb);
    }
    u_int32_t end_to_end_checksum = x1764_memory(sb->uncompressed_ptr, wbuf_get_woffset(&wb));
    wbuf_nocrc_int(&wb, end_to_end_checksum);
    invariant(wb.ndone == wb.size);
    invariant(sb->uncompressed_size==wb.ndone);
}

//
// Takes the data in sb->uncompressed_ptr, and compresses it 
// into a newly allocated buffer sb->compressed_ptr
// 
static void
compress_brtnode_sub_block(struct sub_block *sb, enum toku_compression_method method) {
    assert(sb->compressed_ptr == NULL);
    set_compressed_size_bound(sb, method);
    // add 8 extra bytes, 4 for compressed size,  4 for decompressed size
    sb->compressed_ptr = toku_xmalloc(sb->compressed_size_bound + 8);
    //
    // This probably seems a bit complicated. Here is what is going on.
    // In TokuDB 5.0, sub_blocks were compressed and the compressed data
    // was checksummed. The checksum did NOT include the size of the compressed data
    // and the size of the uncompressed data. The fields of sub_block only reference the
    // compressed data, and it is the responsibility of the user of the sub_block
    // to write the length
    //
    // For Dr. No, we want the checksum to also include the size of the compressed data, and the 
    // size of the decompressed data, because this data
    // may be read off of disk alone, so it must be verifiable alone.
    //
    // So, we pass in a buffer to compress_nocrc_sub_block that starts 8 bytes after the beginning
    // of sb->compressed_ptr, so we have space to put in the sizes, and then run the checksum.
    //
    sb->compressed_size = compress_nocrc_sub_block(
        sb,
        (char *)sb->compressed_ptr + 8,
        sb->compressed_size_bound,
        method
        );

    u_int32_t* extra = (u_int32_t *)(sb->compressed_ptr);
    // store the compressed and uncompressed size at the beginning
    extra[0] = toku_htod32(sb->compressed_size);
    extra[1] = toku_htod32(sb->uncompressed_size);
    // now checksum the entire thing
    sb->compressed_size += 8; // now add the eight bytes that we saved for the sizes
    sb->xsum = x1764_memory(sb->compressed_ptr,sb->compressed_size);


    //
    // This is the end result for Dr. No and forward. For brtnodes, sb->compressed_ptr contains
    // two integers at the beginning, the size and uncompressed size, and then the compressed
    // data. sb->xsum contains the checksum of this entire thing.
    // 
    // In TokuDB 5.0, sb->compressed_ptr only contained the compressed data, sb->xsum
    // checksummed only the compressed data, and the checksumming of the sizes were not
    // done here.
    //
}

//
// Returns the size needed to serialize the brtnode info
// Does not include header information that is common with rollback logs
// such as the magic, layout_version, and build_id
// Includes only node specific info such as pivot information, n_children, and so on
//
static u_int32_t
serialize_brtnode_info_size(BRTNODE node)
{
    u_int32_t retval = 0;
    retval += 8; // max_msn_applied_to_node_on_disk
    retval += 4; // nodesize
    retval += 4; // flags
    retval += 4; // height;
    retval += node->totalchildkeylens; // total length of pivots
    retval += (node->n_children-1)*4; // encode length of each pivot
    if (node->height > 0) {
        retval += node->n_children*8; // child blocknum's
    }
    retval += 4; // checksum
    return retval;
}

static void serialize_brtnode_info(BRTNODE node, 
				   SUB_BLOCK sb // output
				   ) {
    assert(sb->uncompressed_size == 0);
    assert(sb->uncompressed_ptr == NULL);
    sb->uncompressed_size = serialize_brtnode_info_size(node);
    sb->uncompressed_ptr = toku_xmalloc(sb->uncompressed_size);
    assert(sb->uncompressed_ptr);
    struct wbuf wb;
    wbuf_init(&wb, sb->uncompressed_ptr, sb->uncompressed_size);

    wbuf_MSN(&wb, node->max_msn_applied_to_node_on_disk);
    wbuf_nocrc_uint(&wb, node->nodesize);
    wbuf_nocrc_uint(&wb, node->flags);
    wbuf_nocrc_int (&wb, node->height);    
    // pivot information
    for (int i = 0; i < node->n_children-1; i++) {
        wbuf_nocrc_bytes(&wb, kv_pair_key(node->childkeys[i]), toku_brt_pivot_key_len(node->childkeys[i]));
    }
    // child blocks, only for internal nodes
    if (node->height > 0) {
        for (int i = 0; i < node->n_children; i++) {
            wbuf_nocrc_BLOCKNUM(&wb, BP_BLOCKNUM(node,i));
        }
    }

    u_int32_t end_to_end_checksum = x1764_memory(sb->uncompressed_ptr, wbuf_get_woffset(&wb));
    wbuf_nocrc_int(&wb, end_to_end_checksum);
    invariant(wb.ndone == wb.size);
    invariant(sb->uncompressed_size==wb.ndone);
}


// This is the size of the uncompressed data, not including the compression headers
unsigned int
toku_serialize_brtnode_size (BRTNODE node) {
    unsigned int result = 0;
    //
    // As of now, this seems to be called if and only if the entire node is supposed
    // to be in memory, so we will assert it.
    //
    toku_assert_entire_node_in_memory(node);
    result += serialize_node_header_size(node);
    result += serialize_brtnode_info_size(node);
    for (int i = 0; i < node->n_children; i++) {
        result += serialize_brtnode_partition_size(node,i);
    }
    return result;
}

struct array_info {
    u_int32_t offset;
    OMTVALUE* array;
};

static int
array_item (OMTVALUE lev, u_int32_t idx, void *vsi) {
    struct array_info *ai = vsi;
    ai->array[idx+ai->offset] = lev;
    return 0;
}

struct sum_info {
    unsigned int dsum;
    unsigned int count;
};

static int
sum_item (OMTVALUE lev, u_int32_t UU(idx), void *vsi) {
    LEAFENTRY le=lev;
    struct sum_info *si = vsi;
    si->count++;
    si->dsum += leafentry_disksize(le);     // TODO 4050 delete this redundant call and use le_sizes[]
    return 0;
}

// There must still be at least one child
// Requires that all messages in buffers above have been applied.
// Because all messages above have been applied, setting msn of all new basements 
// to max msn of existing basements is correct.  (There cannot be any messages in
// buffers above that still need to be applied.)
void
rebalance_brtnode_leaf(BRTNODE node, unsigned int basementnodesize)
{
    assert(node->height == 0);
    assert(node->dirty);

    uint32_t num_orig_basements = node->n_children;
    // Count number of leaf entries in this leaf (num_le).
    u_int32_t num_le = 0;  
    for (uint32_t i = 0; i < num_orig_basements; i++) {
        invariant(BLB_BUFFER(node, i));
        num_le += toku_omt_size(BLB_BUFFER(node, i));
    }

    uint32_t num_alloc = num_le ? num_le : 1;  // simplify logic below by always having at least one entry per array

    // Create an array of OMTVALUE's that store all the pointers to all the data.
    // Each element in leafpointers is a pointer to a leaf.
    OMTVALUE *XMALLOC_N(num_alloc, leafpointers);
    leafpointers[0] = NULL;

    // Capture pointers to old mempools' buffers (so they can be destroyed)
    void **XMALLOC_N(num_orig_basements, old_mempool_bases);

    u_int32_t curr_le = 0;
    for (uint32_t i = 0; i < num_orig_basements; i++) {
        OMT curr_omt = BLB_BUFFER(node, i);
        struct array_info ai;
        ai.offset = curr_le;         // index of first le in basement
	ai.array = leafpointers;
        toku_omt_iterate(curr_omt, array_item, &ai);
        curr_le += toku_omt_size(curr_omt);
	BASEMENTNODE bn = BLB(node, i);
	old_mempool_bases[i] = toku_mempool_get_base(&bn->buffer_mempool);
    }

    // Create an array that will store indexes of new pivots.
    // Each element in new_pivots is the index of a pivot key.
    // (Allocating num_le of them is overkill, but num_le is an upper bound.)
    u_int32_t *XMALLOC_N(num_alloc, new_pivots);
    new_pivots[0] = 0;

    // Each element in le_sizes is the size of the leafentry pointed to by leafpointers.
    size_t *XMALLOC_N(num_alloc, le_sizes);
    le_sizes[0] = 0;

    // Create an array that will store the size of each basement.
    // This is the sum of the leaf sizes of all the leaves in that basement.
    // We don't know how many basements there will be, so we use num_le as the upper bound.
    size_t *XMALLOC_N(num_alloc, bn_sizes);
    bn_sizes[0] = 0;

    // TODO 4050: All these arrays should be combined into a single array of some bn_info struct (pivot, msize, num_les).
    // Each entry is the number of leafentries in this basement.  (Again, num_le is overkill upper baound.)
    uint32_t *XMALLOC_N(num_alloc, num_les_this_bn);
    num_les_this_bn[0] = 0;
    
    // Figure out the new pivots.  
    // We need the index of each pivot, and for each basement we need
    // the number of leaves and the sum of the sizes of the leaves (memory requirement for basement).
    u_int32_t curr_pivot = 0;
    u_int32_t num_le_in_curr_bn = 0;
    u_int32_t bn_size_so_far = 0;
    for (u_int32_t i = 0; i < num_le; i++) {
        u_int32_t curr_le_size = leafentry_disksize(leafpointers[i]);
	le_sizes[i] = curr_le_size;
        if ((bn_size_so_far + curr_le_size > basementnodesize) && (num_le_in_curr_bn != 0)) {
            // cap off the current basement node to end with the element before i
            new_pivots[curr_pivot] = i-1;
            curr_pivot++;
            num_le_in_curr_bn = 0;
            bn_size_so_far = 0;
        }
        num_le_in_curr_bn++;
	num_les_this_bn[curr_pivot] = num_le_in_curr_bn;
        bn_size_so_far += curr_le_size;
	bn_sizes[curr_pivot] = bn_size_so_far;
    }
    // curr_pivot is now the total number of pivot keys in the leaf node
    int num_pivots   = curr_pivot;
    int num_children = num_pivots + 1;

    // now we need to fill in the new basement nodes and pivots

    // TODO: (Zardosht) this is an ugly thing right now
    // Need to figure out how to properly deal with seqinsert.
    // I am not happy with how this is being
    // handled with basement nodes
    u_int32_t tmp_seqinsert = BLB_SEQINSERT(node, num_orig_basements - 1);

    // choose the max msn applied to any basement as the max msn applied to all new basements
    MSN max_msn = MIN_MSN;
    for (uint32_t i = 0; i < num_orig_basements; i++) {
        MSN curr_msn = BLB_MAX_MSN_APPLIED(node,i);
        max_msn = (curr_msn.msn > max_msn.msn) ? curr_msn : max_msn;
    }

    // Now destroy the old basements, but do not destroy leaves
    toku_destroy_brtnode_internals(node);

    // now reallocate pieces and start filling them in
    invariant(num_children > 0);
    node->totalchildkeylens = 0;

    XCALLOC_N(num_pivots, node->childkeys);        // allocate pointers to pivot structs
    node->n_children = num_children;
    XCALLOC_N(num_children, node->bp);             // allocate pointers to basements (bp)
    for (int i = 0; i < num_children; i++) {
        set_BLB(node, i, toku_create_empty_bn());  // allocate empty basements and set bp pointers
    }

    // now we start to fill in the data

    // first the pivots
    for (int i = 0; i < num_pivots; i++) {
        LEAFENTRY curr_le_pivot = leafpointers[new_pivots[i]];
        node->childkeys[i] = kv_pair_malloc(
            le_key(curr_le_pivot),
            le_keylen(curr_le_pivot),
            0,
            0
            );
        assert(node->childkeys[i]);
        node->totalchildkeylens += toku_brt_pivot_key_len(node->childkeys[i]);
    }

    uint32_t baseindex_this_bn = 0;
    // now the basement nodes
    for (int i = 0; i < num_children; i++) {
        // put back seqinsert
        BLB_SEQINSERT(node, i) = tmp_seqinsert;

        // create start (inclusive) and end (exclusive) boundaries for data of basement node
        u_int32_t curr_start = (i==0) ? 0 : new_pivots[i-1]+1;               // index of first leaf in basement
        u_int32_t curr_end = (i==num_pivots) ? num_le : new_pivots[i]+1;     // index of first leaf in next basement
        u_int32_t num_in_bn = curr_end - curr_start;                         // number of leaves in this basement

	// create indexes for new basement
	invariant(baseindex_this_bn == curr_start);
	uint32_t num_les_to_copy = num_les_this_bn[i];
	invariant(num_les_to_copy == num_in_bn); 
	
	// construct mempool for this basement
	size_t size_this_bn = bn_sizes[i];
	BASEMENTNODE bn = BLB(node, i);
	struct mempool * mp = &bn->buffer_mempool;
	toku_mempool_construct(mp, size_this_bn);

        OMTVALUE *XMALLOC_N(num_in_bn, bn_array);
	for (uint32_t le_index = 0; le_index < num_les_to_copy; le_index++) {
	    uint32_t le_within_node = baseindex_this_bn + le_index;
	    size_t   le_size = le_sizes[le_within_node];
	    void *   new_le  = toku_mempool_malloc(mp, le_size, 1); // point to new location
	    void *   old_le  = leafpointers[le_within_node];  
	    memcpy(new_le, old_le, le_size);  // put le data at new location
	    bn_array[le_index] = new_le;      // point to new location (in new mempool)
	}

        toku_omt_destroy(&BLB_BUFFER(node, i));
        int r = toku_omt_create_steal_sorted_array(
            &BLB_BUFFER(node, i),
            &bn_array,
            num_in_bn,
            num_in_bn
            );
        invariant_zero(r);
        BLB_NBYTESINBUF(node, i) = size_this_bn;

        BP_STATE(node,i) = PT_AVAIL;
        BP_TOUCH_CLOCK(node,i);
        BLB_MAX_MSN_APPLIED(node,i) = max_msn;
	baseindex_this_bn += num_les_to_copy;  // set to index of next bn
    }
    node->max_msn_applied_to_node_on_disk = max_msn;

    // destroy buffers of old mempools
    for (uint32_t i = 0; i < num_orig_basements; i++) {
	toku_free(old_mempool_bases[i]);
    }
    toku_free(leafpointers);
    toku_free(old_mempool_bases);
    toku_free(new_pivots);
    toku_free(le_sizes);
    toku_free(bn_sizes);
    toku_free(num_les_this_bn);
}  // end of rebalance_brtnode_leaf()

static void
serialize_and_compress_partition(BRTNODE node, int childnum, SUB_BLOCK sb)
{
    serialize_brtnode_partition(node, childnum, sb);
    compress_brtnode_sub_block(sb, node->h->compression_method);
}

void
toku_create_compressed_partition_from_available(
    BRTNODE node,
    int childnum,
    SUB_BLOCK sb
    )
{
    serialize_and_compress_partition(node, childnum, sb);
    //
    // now we have an sb that would be ready for being written out,
    // but we are not writing it out, we are storing it in cache for a potentially
    // long time, so we need to do some cleanup
    //
    // The buffer created above contains metadata in the first 8 bytes, and is overallocated
    // It allocates a bound on the compressed length (evaluated before compression) as opposed
    // to just the amount of the actual compressed data. So, we create a new buffer and copy
    // just the compressed data.
    //
    u_int32_t compressed_size = toku_dtoh32(*(u_int32_t *)sb->compressed_ptr);
    void* compressed_data = toku_xmalloc(compressed_size);
    memcpy(compressed_data, (char *)sb->compressed_ptr + 8, compressed_size);
    toku_free(sb->compressed_ptr);
    sb->compressed_ptr = compressed_data;
    sb->compressed_size = compressed_size;
    if (sb->uncompressed_ptr) {
        toku_free(sb->uncompressed_ptr);
        sb->uncompressed_ptr = NULL;
    }
    
}


static void
serialize_and_compress(BRTNODE node, int npartitions, struct sub_block sb[]) {
    for (int i = 0; i < npartitions; i++) {
        serialize_and_compress_partition(node, i, &sb[i]);
    }
}

// Writes out each child to a separate malloc'd buffer, then compresses
// all of them, and writes the uncompressed header, to bytes_to_write,
// which is malloc'd.
//
int
toku_serialize_brtnode_to_memory (BRTNODE node,
                                  BRTNODE_DISK_DATA* ndd,
                                  unsigned int basementnodesize,
                                  BOOL do_rebalancing,
                          /*out*/ size_t *n_bytes_to_write,
                          /*out*/ char  **bytes_to_write)
{
    toku_assert_entire_node_in_memory(node);

    if (do_rebalancing && node->height == 0) {
        rebalance_brtnode_leaf(node, basementnodesize);
    }
    const int npartitions = node->n_children;

    // Each partition represents a compressed sub block
    // For internal nodes, a sub block is a message buffer
    // For leaf nodes, a sub block is a basement node
    struct sub_block *XMALLOC_N(npartitions, sb);
    *ndd = toku_xrealloc(*ndd, npartitions*sizeof(**ndd));
    struct sub_block sb_node_info;
    for (int i = 0; i < npartitions; i++) {
        sub_block_init(&sb[i]);;
    }
    sub_block_init(&sb_node_info);

    //
    // First, let's serialize and compress the individual sub blocks
    //
    serialize_and_compress(node, npartitions, sb);

    //
    // Now lets create a sub-block that has the common node information,
    // This does NOT include the header
    //
    serialize_brtnode_info(node, &sb_node_info);
    compress_brtnode_sub_block(&sb_node_info, node->h->compression_method);

    // now we have compressed each of our pieces into individual sub_blocks,
    // we can put the header and all the subblocks into a single buffer
    // and return it.

    // The total size of the node is:
    // size of header + disk size of the n+1 sub_block's created above
    u_int32_t total_node_size = (serialize_node_header_size(node) // uncomrpessed header
				 + sb_node_info.compressed_size   // compressed nodeinfo (without its checksum)
				 + 4);                            // nodinefo's checksum
    // store the BP_SIZESs
    for (int i = 0; i < node->n_children; i++) {
	u_int32_t len         = sb[i].compressed_size + 4; // data and checksum
        BP_SIZE (*ndd,i) = len;
	BP_START(*ndd,i) = total_node_size;
        total_node_size += sb[i].compressed_size + 4;
    }

    char *data = toku_xmalloc(total_node_size);
    char *curr_ptr = data;
    // now create the final serialized node

    // write the header
    struct wbuf wb;
    wbuf_init(&wb, curr_ptr, serialize_node_header_size(node));
    serialize_node_header(node, *ndd, &wb);
    assert(wb.ndone == wb.size);
    curr_ptr += serialize_node_header_size(node);

    // now write sb_node_info
    memcpy(curr_ptr, sb_node_info.compressed_ptr, sb_node_info.compressed_size);
    curr_ptr += sb_node_info.compressed_size;
    // write the checksum
    *(u_int32_t *)curr_ptr = toku_htod32(sb_node_info.xsum);
    curr_ptr += sizeof(sb_node_info.xsum);

    for (int i = 0; i < npartitions; i++) {
        memcpy(curr_ptr, sb[i].compressed_ptr, sb[i].compressed_size);
        curr_ptr += sb[i].compressed_size;
        // write the checksum
        *(u_int32_t *)curr_ptr = toku_htod32(sb[i].xsum);
        curr_ptr += sizeof(sb[i].xsum);
    }
    assert(curr_ptr - data == total_node_size);
    *bytes_to_write = data;
    *n_bytes_to_write = total_node_size;

    //
    // now that node has been serialized, go through sub_block's and free
    // memory
    //
    toku_free(sb_node_info.compressed_ptr);
    toku_free(sb_node_info.uncompressed_ptr);
    for (int i = 0; i < npartitions; i++) {
        toku_free(sb[i].compressed_ptr);
        toku_free(sb[i].uncompressed_ptr);
    }

    toku_free(sb);
    return 0;
}

int
toku_serialize_brtnode_to (int fd, BLOCKNUM blocknum, BRTNODE node, BRTNODE_DISK_DATA* ndd, BOOL do_rebalancing, struct brt_header *h, int UU(n_workitems), int UU(n_threads), BOOL for_checkpoint) {

    size_t n_to_write;
    char *compressed_buf = NULL;
    {
	int r = toku_serialize_brtnode_to_memory(node, ndd, h->basementnodesize, do_rebalancing,
                                                 &n_to_write, &compressed_buf);
	if (r!=0) return r;
    }

    //write_now: printf("%s:%d Writing %d bytes\n", __FILE__, __LINE__, w.ndone);
    {
	// If the node has never been written, then write the whole buffer, including the zeros
	invariant(blocknum.b>=0);
	//printf("%s:%d h=%p\n", __FILE__, __LINE__, h);
	//printf("%s:%d translated_blocknum_limit=%lu blocknum.b=%lu\n", __FILE__, __LINE__, h->translated_blocknum_limit, blocknum.b);
	//printf("%s:%d allocator=%p\n", __FILE__, __LINE__, h->block_allocator);
	//printf("%s:%d bt=%p\n", __FILE__, __LINE__, h->block_translation);
	DISKOFF offset;

        toku_blocknum_realloc_on_disk(h->blocktable, blocknum, n_to_write, &offset,
                                      h, for_checkpoint); //dirties h
	lock_for_pwrite();
	toku_full_pwrite_extend(fd, compressed_buf, n_to_write, offset);
	unlock_for_pwrite();
    }

    //printf("%s:%d wrote %d bytes for %lld size=%lld\n", __FILE__, __LINE__, w.ndone, off, size);
    toku_free(compressed_buf);
    node->dirty = 0;  // See #1957.   Must set the node to be clean after serializing it so that it doesn't get written again on the next checkpoint or eviction.
    return 0;
}

static void
deserialize_child_buffer(NONLEAF_CHILDINFO bnc, struct rbuf *rbuf,
                         DESCRIPTOR desc, brt_compare_func cmp) {
    int r;
    int n_bytes_in_buffer = 0;
    int n_in_this_buffer = rbuf_int(rbuf);
    void **fresh_offsets, **stale_offsets;
    void **broadcast_offsets;
    int nfresh = 0, nstale = 0;
    int nbroadcast_offsets = 0;
    if (cmp) {
        XMALLOC_N(n_in_this_buffer, stale_offsets);
        XMALLOC_N(n_in_this_buffer, fresh_offsets);
        XMALLOC_N(n_in_this_buffer, broadcast_offsets);
    }
    for (int i = 0; i < n_in_this_buffer; i++) {
        bytevec key; ITEMLEN keylen;
        bytevec val; ITEMLEN vallen;
        // this is weird but it's necessary to pass icc and gcc together
        unsigned char ctype = rbuf_char(rbuf);
        enum brt_msg_type type = (enum brt_msg_type) ctype;
        bool is_fresh = rbuf_char(rbuf);
        MSN msn = rbuf_msn(rbuf);
        XIDS xids;
        xids_create_from_buffer(rbuf, &xids);
        rbuf_bytes(rbuf, &key, &keylen); /* Returns a pointer into the rbuf. */
        rbuf_bytes(rbuf, &val, &vallen);
        //printf("Found %s,%s\n", (char*)key, (char*)val);
        long *dest;
        if (cmp) {
            if (brt_msg_type_applies_once(type)) {
                if (is_fresh) {
                    dest = (long *) &fresh_offsets[nfresh];
                    nfresh++;
                } else {
                    dest = (long *) &stale_offsets[nstale];
                    nstale++;
                }
            } else if (brt_msg_type_applies_all(type) || brt_msg_type_does_nothing(type)) {
                dest = (long *) &broadcast_offsets[nbroadcast_offsets];
                nbroadcast_offsets++;
            } else {
                assert(FALSE);
            }
        } else {
            dest = NULL;
        }
        r = toku_fifo_enq(bnc->buffer, key, keylen, val, vallen, type, msn, xids, is_fresh, dest); /* Copies the data into the fifo */
        lazy_assert_zero(r);
        n_bytes_in_buffer += keylen + vallen + KEY_VALUE_OVERHEAD + BRT_CMD_OVERHEAD + xids_get_serialize_size(xids);
        //printf("Inserted\n");
        xids_destroy(&xids);
    }
    invariant(rbuf->ndone == rbuf->size);

    if (cmp) {
        struct toku_fifo_entry_key_msn_cmp_extra extra = { .desc = desc, .cmp = cmp, .fifo = bnc->buffer };
        r = mergesort_r(fresh_offsets, nfresh, sizeof fresh_offsets[0], &extra, toku_fifo_entry_key_msn_cmp);
        assert_zero(r);
        toku_omt_destroy(&bnc->fresh_message_tree);
        r = toku_omt_create_steal_sorted_array(&bnc->fresh_message_tree, &fresh_offsets, nfresh, n_in_this_buffer);
        assert_zero(r);
        r = mergesort_r(stale_offsets, nstale, sizeof stale_offsets[0], &extra, toku_fifo_entry_key_msn_cmp);
        assert_zero(r);
        toku_omt_destroy(&bnc->stale_message_tree);
        r = toku_omt_create_steal_sorted_array(&bnc->stale_message_tree, &stale_offsets, nstale, n_in_this_buffer);
        assert_zero(r);
        toku_omt_destroy(&bnc->broadcast_list);
        r = toku_omt_create_steal_sorted_array(&bnc->broadcast_list, &broadcast_offsets, nbroadcast_offsets, n_in_this_buffer);
        assert_zero(r);
    }
    bnc->n_bytes_in_buffer = n_bytes_in_buffer;
}

// dump a buffer to stderr
// no locking around this for now
static void
dump_bad_block(unsigned char *vp, u_int64_t size) {
    const u_int64_t linesize = 64;
    u_int64_t n = size / linesize;
    for (u_int64_t i = 0; i < n; i++) {
        fprintf(stderr, "%p: ", vp);
	for (u_int64_t j = 0; j < linesize; j++) {
	    unsigned char c = vp[j];
	    fprintf(stderr, "%2.2X", c);
	}
	fprintf(stderr, "\n");
	vp += linesize;
    }
    size = size % linesize;
    for (u_int64_t i=0; i<size; i++) {
	if ((i % linesize) == 0)
	    fprintf(stderr, "%p: ", vp+i);
	fprintf(stderr, "%2.2X", vp[i]);
	if (((i+1) % linesize) == 0)
	    fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

BASEMENTNODE toku_create_empty_bn(void) {
    BASEMENTNODE bn = toku_create_empty_bn_no_buffer();
    int r;
    r = toku_omt_create(&bn->buffer);
    assert_zero(r);
    return bn;
}

struct mp_pair {
    void* orig_base;
    void* new_base;
    OMT omt;
};

static int fix_mp_offset(OMTVALUE v, u_int32_t i, void* extra) {
    struct mp_pair* p = extra;
    char* old_value = v;
    char *new_value = old_value - (char *)p->orig_base + (char *)p->new_base;
    toku_omt_set_at(p->omt, (OMTVALUE) new_value, i);
    return 0;
}
        
BASEMENTNODE toku_clone_bn(BASEMENTNODE orig_bn) {
    BASEMENTNODE bn = toku_create_empty_bn_no_buffer();
    bn->max_msn_applied = orig_bn->max_msn_applied;
    bn->n_bytes_in_buffer = orig_bn->n_bytes_in_buffer;
    bn->seqinsert = orig_bn->seqinsert;
    bn->stale_ancestor_messages_applied = orig_bn->stale_ancestor_messages_applied;
    bn->stat64_delta = orig_bn->stat64_delta;
    toku_mempool_clone(&orig_bn->buffer_mempool, &bn->buffer_mempool);
    toku_omt_clone_noptr(&bn->buffer, orig_bn->buffer);
    struct mp_pair p;
    p.orig_base = toku_mempool_get_base(&orig_bn->buffer_mempool);
    p.new_base = toku_mempool_get_base(&bn->buffer_mempool);
    p.omt = bn->buffer;
    toku_omt_iterate(
        bn->buffer,
        fix_mp_offset,
        &p
        );
    return bn;
}

BASEMENTNODE toku_create_empty_bn_no_buffer(void) {
    BASEMENTNODE XMALLOC(bn);
    bn->max_msn_applied.msn = 0;
    bn->buffer = NULL;
    bn->n_bytes_in_buffer = 0;
    bn->seqinsert = 0;
    bn->stale_ancestor_messages_applied = false;
    toku_mempool_zero(&bn->buffer_mempool);
    bn->stat64_delta = ZEROSTATS;
    return bn;
}

NONLEAF_CHILDINFO toku_create_empty_nl(void) {
    NONLEAF_CHILDINFO XMALLOC(cn);
    cn->n_bytes_in_buffer = 0;
    int r = toku_fifo_create(&cn->buffer); assert_zero(r);
    r = toku_omt_create(&cn->fresh_message_tree); assert_zero(r);
    r = toku_omt_create(&cn->stale_message_tree); assert_zero(r);
    r = toku_omt_create(&cn->broadcast_list); assert_zero(r);
    return cn;
}

// does NOT create OMTs, just the FIFO
NONLEAF_CHILDINFO toku_clone_nl(NONLEAF_CHILDINFO orig_childinfo) {
    NONLEAF_CHILDINFO XMALLOC(cn);
    cn->n_bytes_in_buffer = orig_childinfo->n_bytes_in_buffer;    
    cn->fresh_message_tree = NULL;
    cn->stale_message_tree = NULL;
    cn->broadcast_list = NULL;
    toku_fifo_clone(orig_childinfo->buffer, &cn->buffer);
    return cn;
}

void destroy_basement_node (BASEMENTNODE bn)
{
    // The buffer may have been freed already, in some cases.
    if (bn->buffer) {
	toku_omt_destroy(&bn->buffer);
    }
    toku_free(bn);
}

void destroy_nonleaf_childinfo (NONLEAF_CHILDINFO nl)
{
    toku_fifo_free(&nl->buffer);
    if (nl->fresh_message_tree) toku_omt_destroy(&nl->fresh_message_tree);
    if (nl->stale_message_tree) toku_omt_destroy(&nl->stale_message_tree);
    if (nl->broadcast_list) toku_omt_destroy(&nl->broadcast_list);
    toku_free(nl);
}

// 
static int
read_block_from_fd_into_rbuf(
    int fd, 
    BLOCKNUM blocknum,
    struct brt_header *h,
    struct rbuf *rb
    ) 
{
    if (h->panic) {
        toku_trace("panic set, will not read block from fd into buf");
        return h->panic;
    }
    toku_trace("deserial start nopanic");
    
    // get the file offset and block size for the block
    DISKOFF offset, size;
    toku_translate_blocknum_to_offset_size(h->blocktable, blocknum, &offset, &size);
    u_int8_t *XMALLOC_N(size, raw_block);
    rbuf_init(rb, raw_block, size);
    {
        // read the block
        ssize_t rlen = toku_os_pread(fd, raw_block, size, offset);
        lazy_assert((DISKOFF)rlen == size);
    }
    
    return 0;
}

static const int read_header_heuristic_max = 32*1024;

#define MIN(a,b) (((a)>(b)) ? (b) : (a))

static void read_brtnode_header_from_fd_into_rbuf_if_small_enough (int fd, BLOCKNUM blocknum, struct brt_header *h, struct rbuf *rb)
// Effect: If the header part of the node is small enough, then read it into the rbuf.  The rbuf will be allocated to be big enough in any case.
{
    assert(!h->panic);
    DISKOFF offset, size;
    toku_translate_blocknum_to_offset_size(h->blocktable, blocknum, &offset, &size);
    DISKOFF read_size = MIN(read_header_heuristic_max, size);
    u_int8_t *XMALLOC_N(size, raw_block);
    rbuf_init(rb, raw_block, read_size);
    {
        // read the block
        ssize_t rlen = toku_os_pread(fd, raw_block, read_size, offset);
	assert(rlen>=0);
	rbuf_init(rb, raw_block, rlen);
    }
    
}

//
// read the compressed partition into the sub_block,
// validate the checksum of the compressed data
//
static void
read_compressed_sub_block(struct rbuf *rb, struct sub_block *sb)
{
    sb->compressed_size = rbuf_int(rb);
    sb->uncompressed_size = rbuf_int(rb);
    bytevec* cp = (bytevec*)&sb->compressed_ptr;
    rbuf_literal_bytes(rb, cp, sb->compressed_size);
    sb->xsum = rbuf_int(rb);
    // let's check the checksum
    u_int32_t actual_xsum = x1764_memory((char *)sb->compressed_ptr-8, 8+sb->compressed_size);
    invariant(sb->xsum == actual_xsum);

}

static void 
read_and_decompress_sub_block(struct rbuf *rb, struct sub_block *sb)
{
    read_compressed_sub_block(rb, sb);
    sb->uncompressed_ptr = toku_xmalloc(sb->uncompressed_size);
    assert(sb->uncompressed_ptr);
    
    toku_decompress(
        sb->uncompressed_ptr,
        sb->uncompressed_size,
        sb->compressed_ptr,
        sb->compressed_size
        );
}

// verify the checksum
static void
verify_brtnode_sub_block (struct sub_block *sb)
{
    // first verify the checksum
    u_int32_t data_size = sb->uncompressed_size - 4; // checksum is 4 bytes at end
    u_int32_t stored_xsum = toku_dtoh32(*((u_int32_t *)((char *)sb->uncompressed_ptr + data_size)));
    u_int32_t actual_xsum = x1764_memory(sb->uncompressed_ptr, data_size);
    if (stored_xsum != actual_xsum) {
        dump_bad_block(sb->uncompressed_ptr, sb->uncompressed_size);
        assert(FALSE);
    }
}

// This function deserializes the data stored by serialize_brtnode_info
static void 
deserialize_brtnode_info(
    struct sub_block *sb, 
    BRTNODE node
    )
{
    // sb_node_info->uncompressed_ptr stores the serialized node information
    // this function puts that information into node

    // first verify the checksum
    verify_brtnode_sub_block(sb);
    u_int32_t data_size = sb->uncompressed_size - 4; // checksum is 4 bytes at end

    // now with the data verified, we can read the information into the node
    struct rbuf rb = {.buf = NULL, .size = 0, .ndone = 0};
    rbuf_init(&rb, sb->uncompressed_ptr, data_size);

    node->max_msn_applied_to_node_on_disk = rbuf_msn(&rb);
    node->nodesize = rbuf_int(&rb);
    node->flags = rbuf_int(&rb);
    node->height = rbuf_int(&rb);
    if (node->layout_version_read_from_disk < BRT_LAYOUT_VERSION_19) {
        (void) rbuf_int(&rb); // optimized_for_upgrade
    }

    // now create the basement nodes or childinfos, depending on whether this is a
    // leaf node or internal node    
    // now the subtree_estimates

    // n_children is now in the header, nd the allocatio of the node->bp is in deserialize_brtnode_from_rbuf.
    assert(node->bp!=NULL); // 

    // now the pivots
    node->totalchildkeylens = 0;
    if (node->n_children > 1) {
        XMALLOC_N(node->n_children - 1, node->childkeys);
        assert(node->childkeys);
        for (int i=0; i < node->n_children-1; i++) {
            bytevec childkeyptr;
            unsigned int cklen;
            rbuf_bytes(&rb, &childkeyptr, &cklen);
            node->childkeys[i] = kv_pair_malloc((void*)childkeyptr, cklen, 0, 0);
            node->totalchildkeylens += toku_brt_pivot_key_len(node->childkeys[i]);
        }
    }
    else {
	node->childkeys = NULL;
	node->totalchildkeylens = 0;
    }

    // if this is an internal node, unpack the block nums, and fill in necessary fields
    // of childinfo
    if (node->height > 0) {
        for (int i = 0; i < node->n_children; i++) {
            BP_BLOCKNUM(node,i) = rbuf_blocknum(&rb);
	    BP_WORKDONE(node, i) = 0;
        }        
    }
 
    // make sure that all the data was read
    if (data_size != rb.ndone) {
        dump_bad_block(rb.buf, rb.size);
        assert(FALSE);
    }
}

static void
setup_available_brtnode_partition(BRTNODE node, int i) {
    if (node->height == 0) {
	set_BLB(node, i, toku_create_empty_bn());
        BLB_MAX_MSN_APPLIED(node,i) = node->max_msn_applied_to_node_on_disk;
    }
    else {
	set_BNC(node, i, toku_create_empty_nl());
    }
}


// Assign the child_to_read member of the bfe from the given brt node
// that has been brought into memory.
static void
update_bfe_using_brtnode(BRTNODE node, struct brtnode_fetch_extra *bfe)
{
    if (bfe->type == brtnode_fetch_subset && bfe->search != NULL) {
        // we do not take into account prefetching yet
        // as of now, if we need a subset, the only thing
        // we can possibly require is a single basement node
        // we find out what basement node the query cares about
        // and check if it is available
        assert(bfe->search);
        bfe->child_to_read = toku_brt_search_which_child(
            &bfe->h->cmp_descriptor,
            bfe->h->compare_fun,
            node,
            bfe->search
            );
    }
}


// Using the search parameters in the bfe, this function will
// initialize all of the given brt node's partitions.
static void
setup_partitions_using_bfe(BRTNODE node,
                           struct brtnode_fetch_extra *bfe,
                           bool data_in_memory)
{
    // Leftmost and Rightmost Child bounds.
    int lc, rc;
    if (bfe->type == brtnode_fetch_subset || bfe->type == brtnode_fetch_prefetch) {
        lc = toku_bfe_leftmost_child_wanted(bfe, node);
        rc = toku_bfe_rightmost_child_wanted(bfe, node);
    } else {
        lc = -1;
        rc = -1;
    }

    //
    // setup memory needed for the node
    //
    //printf("node height %d, blocknum %"PRId64", type %d lc %d rc %d\n", node->height, node->thisnodename.b, bfe->type, lc, rc);
    for (int i = 0; i < node->n_children; i++) {
        BP_INIT_UNTOUCHED_CLOCK(node,i);
        if (data_in_memory) {
            BP_STATE(node, i) = ((toku_bfe_wants_child_available(bfe, i) || (lc <= i && i <= rc))
                                 ? PT_AVAIL : PT_COMPRESSED);
        } else {
            BP_STATE(node, i) = PT_ON_DISK;
        }
        BP_WORKDONE(node,i) = 0;

        switch (BP_STATE(node,i)) {
        case PT_AVAIL:
            setup_available_brtnode_partition(node, i);
            BP_TOUCH_CLOCK(node,i);
            continue;
        case PT_COMPRESSED:
            set_BSB(node, i, sub_block_creat());
            continue;
        case PT_ON_DISK:
            set_BNULL(node, i);
            continue;
        case PT_INVALID:
            break;
        }
        assert(FALSE);
    }
}


static void setup_brtnode_partitions(BRTNODE node, struct brtnode_fetch_extra* bfe, bool data_in_memory)
// Effect: Used when reading a brtnode into main memory, this sets up the partitions.
//   We set bfe->child_to_read as well as the BP_STATE and the data pointers (e.g., with set_BSB or set_BNULL or other set_ operations).
// Arguments:  Node: the node to set up.
//             bfe:  Describes the key range needed.
//             data_in_memory: true if we have all the data (in which case we set the BP_STATE to be either PT_AVAIL or PT_COMPRESSED depending on the bfe.
//                             false if we don't have the partitions in main memory (in which case we set the state to PT_ON_DISK.
{
    // Set bfe->child_to_read.
    update_bfe_using_brtnode(node, bfe);

    // Setup the partitions.
    setup_partitions_using_bfe(node, bfe, data_in_memory);
}


/* deserialize the partition from the sub-block's uncompressed buffer
 * and destroy the uncompressed buffer
 */
static void
deserialize_brtnode_partition(
    struct sub_block *sb,
    BRTNODE node,
    int childnum,      // which partition to deserialize
    DESCRIPTOR desc,
    brt_compare_func cmp
    )
{
    verify_brtnode_sub_block(sb);
    u_int32_t data_size = sb->uncompressed_size - 4; // checksum is 4 bytes at end

    // now with the data verified, we can read the information into the node
    struct rbuf rb = {.buf = NULL, .size = 0, .ndone = 0};
    rbuf_init(&rb, sb->uncompressed_ptr, data_size);
    unsigned char ch = rbuf_char(&rb);

    if (node->height > 0) {
        assert(ch == BRTNODE_PARTITION_FIFO_MSG);
        deserialize_child_buffer(BNC(node, childnum), &rb, desc, cmp);
        BP_WORKDONE(node, childnum) = 0;
    }
    else {
        assert(ch == BRTNODE_PARTITION_OMT_LEAVES);
        BLB_SEQINSERT(node, childnum) = 0;
        uint32_t num_entries = rbuf_int(&rb);
        uint32_t start_of_data = rb.ndone;                                // index of first byte of first leafentry
	data_size -= start_of_data;                                       // remaining bytes of leafentry data
	// TODO 3988 Count empty basements (data_size == 0)
	if (data_size == 0) {
	    // printf("#### Deserialize empty basement, childnum = %d\n", childnum);
	    invariant_zero(num_entries);                                  
	}
        OMTVALUE *XMALLOC_N(num_entries, array);                          // create array of pointers to leafentries
	BASEMENTNODE bn = BLB(node, childnum);
	toku_mempool_copy_construct(&bn->buffer_mempool, &rb.buf[rb.ndone], data_size);
	uint8_t * le_base = toku_mempool_get_base(&bn->buffer_mempool);   // point to first le in mempool
        for (u_int32_t i = 0; i < num_entries; i++) {                     // now set up the pointers in the omt
            LEAFENTRY le = (LEAFENTRY)&le_base[rb.ndone - start_of_data]; // point to durable mempool, not to transient rbuf
            u_int32_t disksize = leafentry_disksize(le);
            rb.ndone += disksize;
            invariant(rb.ndone<=rb.size);
            array[i]=(OMTVALUE)le;
        }
        u_int32_t end_of_data = rb.ndone;

        BLB_NBYTESINBUF(node, childnum) += end_of_data-start_of_data;

        // destroy old omt (bn.buffer) that was created by toku_create_empty_bn(), so we can create a new one
        toku_omt_destroy(&BLB_BUFFER(node, childnum));
        int r = toku_omt_create_steal_sorted_array(&BLB_BUFFER(node, childnum), &array, num_entries, num_entries);
        invariant_zero(r);
    }
    assert(rb.ndone == rb.size);
    toku_free(sb->uncompressed_ptr);
}

static void
decompress_and_deserialize_worker(struct rbuf curr_rbuf, struct sub_block curr_sb, BRTNODE node, int child, DESCRIPTOR desc, brt_compare_func cmp)
{
    read_and_decompress_sub_block(&curr_rbuf, &curr_sb);
    // at this point, sb->uncompressed_ptr stores the serialized node partition
    deserialize_brtnode_partition(&curr_sb, node, child, desc, cmp);
}

static void
check_and_copy_compressed_sub_block_worker(struct rbuf curr_rbuf, struct sub_block curr_sb, BRTNODE node, int child)
{
    read_compressed_sub_block(&curr_rbuf, &curr_sb);
    SUB_BLOCK bp_sb = BSB(node, child);
    bp_sb->compressed_size = curr_sb.compressed_size;
    bp_sb->uncompressed_size = curr_sb.uncompressed_size;
    bp_sb->compressed_ptr = toku_xmalloc(bp_sb->compressed_size);
    memcpy(bp_sb->compressed_ptr, curr_sb.compressed_ptr, bp_sb->compressed_size);
}

static int deserialize_brtnode_header_from_rbuf_if_small_enough (BRTNODE *brtnode,
                                                                 BRTNODE_DISK_DATA* ndd, 
                                                                 BLOCKNUM blocknum,
                                                                 u_int32_t fullhash,
                                                                 struct brtnode_fetch_extra *bfe,
                                                                 struct rbuf *rb,
                                                                 int fd)
// If we have enough information in the rbuf to construct a header, then do so.
// Also fetch in the basement node if needed.
// Return 0 if it worked.  If something goes wrong (including that we are looking at some old data format that doesn't have partitions) then return nonzero.
{
    int r;
    BRTNODE node = toku_xmalloc(sizeof(*node));

    // fill in values that are known and not stored in rb
    node->fullhash = fullhash;
    node->thisnodename = blocknum;
    node->dirty = 0;
    node->bp = NULL; // fill this in so we can free without a leak.

    if (rb->size < 24) {
        r = EINVAL;
        goto cleanup;
    }

    bytevec magic;
    rbuf_literal_bytes(rb, &magic, 8);
    if (memcmp(magic, "tokuleaf", 8)!=0 &&
        memcmp(magic, "tokunode", 8)!=0) {
        r = toku_db_badformat();
        goto cleanup;
    }

    node->layout_version_read_from_disk = rbuf_int(rb);
    if (node->layout_version_read_from_disk < BRT_FIRST_LAYOUT_VERSION_WITH_BASEMENT_NODES) {
	// This code path doesn't have to worry about upgrade.
        r = EINVAL;
        goto cleanup;
    }

    // Upgrade from 18 to 19.
    if (node->layout_version_read_from_disk == BRT_LAYOUT_VERSION_18) {
        node->layout_version = BRT_LAYOUT_VERSION;
    } else {
        // If the version is greater than the first version with
        // basement nodes, but not version 18, then just use the old
        // behavior.
        node->layout_version = node->layout_version_read_from_disk;
    }

    node->layout_version_original = rbuf_int(rb);
    node->build_id = rbuf_int(rb);
    node->n_children = rbuf_int(rb);
    // Guaranteed to be have been able to read up to here.  If n_children
    // is too big, we may have a problem, so check that we won't overflow
    // while reading the partition locations.
    unsigned int nhsize =  serialize_node_header_size(node); // we can do this because n_children is filled in.
    unsigned int needed_size = nhsize + 12; // we need 12 more so that we can read the compressed block size information that follows for the nodeinfo.
    if (needed_size > rb->size) {
	r = EINVAL;
	goto cleanup;
    }

    XMALLOC_N(node->n_children, node->bp);
    *ndd = toku_xmalloc(node->n_children*sizeof(**ndd));
    // read the partition locations
    for (int i=0; i<node->n_children; i++) {
        BP_START(*ndd,i) = rbuf_int(rb);
        BP_SIZE (*ndd,i) = rbuf_int(rb);
    }

    u_int32_t checksum = x1764_memory(rb->buf, rb->ndone);
    u_int32_t stored_checksum = rbuf_int(rb);
    if (stored_checksum != checksum) {
        dump_bad_block(rb->buf, rb->size);
        invariant(stored_checksum == checksum);
    }

    // Now we want to read the pivot information.
    struct sub_block sb_node_info;
    sub_block_init(&sb_node_info);
    sb_node_info.compressed_size = rbuf_int(rb); // we'll be able to read these because we checked the size earlier.
    sb_node_info.uncompressed_size = rbuf_int(rb);
    if (rb->size-rb->ndone < sb_node_info.compressed_size + 8) {
	r = EINVAL; // we won't
	goto cleanup;
    }
    // We got the entire header and node info!
    toku_brt_status_update_pivot_fetch_reason(bfe);

    // Finish reading compressed the sub_block
    bytevec* cp = (bytevec*)&sb_node_info.compressed_ptr;
    rbuf_literal_bytes(rb, cp, sb_node_info.compressed_size);
    sb_node_info.xsum = rbuf_int(rb);
    // let's check the checksum
    u_int32_t actual_xsum = x1764_memory((char *)sb_node_info.compressed_ptr-8, 8+sb_node_info.compressed_size);
    invariant(sb_node_info.xsum == actual_xsum);

    // Now decompress the subblock
    sb_node_info.uncompressed_ptr = toku_xmalloc(sb_node_info.uncompressed_size);
    assert(sb_node_info.uncompressed_ptr);

    toku_decompress(
        sb_node_info.uncompressed_ptr,
        sb_node_info.uncompressed_size,
        sb_node_info.compressed_ptr,
        sb_node_info.compressed_size
        );

    // at this point sb->uncompressed_ptr stores the serialized node info.
    deserialize_brtnode_info(&sb_node_info, node);
    toku_free(sb_node_info.uncompressed_ptr);
    sb_node_info.uncompressed_ptr = NULL;

    // Now we have the brtnode_info.  We have a bunch more stuff in the
    // rbuf, so we might be able to store the compressed data for some
    // objects.
    // We can proceed to deserialize the individual subblocks.
    assert(bfe->type == brtnode_fetch_none || bfe->type == brtnode_fetch_subset || bfe->type == brtnode_fetch_all || bfe->type == brtnode_fetch_prefetch);

    // setup the memory of the partitions
    // for partitions being decompressed, create either FIFO or basement node
    // for partitions staying compressed, create sub_block
    setup_brtnode_partitions(node, bfe, false);

    if (bfe->type != brtnode_fetch_none) {
        PAIR_ATTR attr;
        toku_brtnode_pf_callback(node, *ndd, bfe, fd, &attr);
    }
    // handle clock
    for (int i = 0; i < node->n_children; i++) {
        if (toku_bfe_wants_child_available(bfe, i)) {
            assert(BP_STATE(node,i) == PT_AVAIL);
            BP_TOUCH_CLOCK(node,i);
        }
    }
    *brtnode = node;
    r = 0;

 cleanup:
    if (r!=0) {
	if (node) {
            toku_free(*ndd);
	    toku_free(node->bp);
	    toku_free(node);
	}
    }
    return r;
}

// This function takes a deserialized version 13 or 14 buffer and
// constructs the associated internal, non-leaf brtnode object.  It
// also creates MSN's for older messages created in older versions
// that did not generate MSN's for messages.  These new MSN's are
// generated from the root downwards, counting backwards from MIN_MSN
// and persisted in the brt header.
static int
deserialize_and_upgrade_internal_node(BRTNODE node,
                                      struct rbuf *rb,
                                      struct brtnode_fetch_extra* bfe)
{
    int r = 0;
    int version = node->layout_version_read_from_disk;

    if(version == BRT_LAST_LAYOUT_VERSION_WITH_FINGERPRINT) {
        (void) rbuf_int(rb);  // 6. fingerprint
    }

    node->n_children = rbuf_int(rb); // 7. n_children

    // Sub-tree esitmates...
    for (int i = 0; i < node->n_children; ++i) {
        if (version == BRT_LAST_LAYOUT_VERSION_WITH_FINGERPRINT) {
            (void) rbuf_int(rb);  // 8. fingerprint
        }
        (void) rbuf_ulonglong(rb);  // 9. nkeys (ulonglong)
        (void) rbuf_ulonglong(rb);  // 10. ndata (ulonglong)
        (void) rbuf_ulonglong(rb);  // 11. dsize (ulonglong)
        (void) rbuf_char(rb);       // 12. exact (char)
    }

    node->childkeys = NULL;
    node->totalchildkeylens = 0;
    // I. Allocate keys based on number of children.
    XMALLOC_N(node->n_children - 1, node->childkeys);
    // II. Copy keys from buffer to allocated keys in brtnode.
    for (int i = 0; i < node->n_children - 1; ++i) {
        // 13. child key pointers and offsets
        bytevec childkeyptr;
        unsigned int cklen;
        rbuf_bytes(rb, &childkeyptr, &cklen);
        node->childkeys[i] = kv_pair_malloc((void*)childkeyptr,
                                            cklen,
                                            0,
                                            0);
        node->totalchildkeylens += toku_brt_pivot_key_len(node->childkeys[i]);
    }

    // Create space for the child node buffers (a.k.a. partitions).
    XMALLOC_N(node->n_children, node->bp);

    // Set the child blocknums.
    for (int i = 0; i < node->n_children; ++i) {
        // 14. blocknums
        BP_BLOCKNUM(node, i) = rbuf_blocknum(rb);
        BP_WORKDONE(node, i) = 0;
    }

    // Read in the child buffer maps.
    struct sub_block_map child_buffer_map[node->n_children];
    for (int i = 0; i < node->n_children; ++i) {
        // The following fields are read in the
        // sub_block_map_deserialize() call:
        // 15. index 16. offset 17. size
        sub_block_map_deserialize(&child_buffer_map[i], rb);
    }

    // We need to setup this node's partitions, but we can't call the
    // existing call (setup_brtnode_paritions.) because there are
    // existing optimizations that would prevent us from bringing all
    // of this node's partitions into memory.  Instead, We use the
    // existing bfe and node to set the bfe's child_to_search member.
    // Then we create a temporary bfe that needs all the nodes to make
    // sure we properly intitialize our partitions before filling them
    // in from our soon-to-be-upgraded node.
    update_bfe_using_brtnode(node, bfe);
    struct brtnode_fetch_extra temp_bfe;
    temp_bfe.type = brtnode_fetch_all;
    setup_partitions_using_bfe(node, &temp_bfe, true);

    // Cache the highest MSN generated for the message buffers.  This
    // will be set in the brtnode.
    //
    // The way we choose MSNs for upgraded messages is delicate.  The
    // field `highest_unused_msn_for_upgrade' in the header is always an
    // MSN that no message has yet.  So when we have N messages that need
    // MSNs, we decrement it by N, and then use it and the N-1 MSNs less
    // than it, but we do not use the value we decremented it to.
    //
    // In the code below, we initialize `lowest' with the value of
    // `highest_unused_msn_for_upgrade' after it is decremented, so we
    // need to be sure to increment it once before we enqueue our first
    // message.
    MSN highest_msn;
    highest_msn.msn = 0;

    // Deserialize de-compressed buffers.
    for (int i = 0; i < node->n_children; ++i) {
        NONLEAF_CHILDINFO bnc = BNC(node, i);
        int n_bytes_in_buffer = 0;
        int n_in_this_buffer = rbuf_int(rb);

        void **fresh_offsets;
        void **broadcast_offsets;
        int nfresh = 0;
        int nbroadcast_offsets = 0;

        if (bfe->h->compare_fun) {
            XMALLOC_N(n_in_this_buffer, fresh_offsets);
            // We skip 'stale' offsets for upgraded nodes.
            XMALLOC_N(n_in_this_buffer, broadcast_offsets);
        }

        // Atomically decrement the header's MSN count by the number
        // of messages in the buffer.
        MSN lowest;
        u_int64_t amount = n_in_this_buffer;
        lowest.msn = __sync_sub_and_fetch(&bfe->h->highest_unused_msn_for_upgrade.msn, amount);
        if (highest_msn.msn == 0) {
            highest_msn.msn = lowest.msn + n_in_this_buffer;
        }

        // Create the FIFO entires from the deserialized buffer.
        for (int j = 0; j < n_in_this_buffer; ++j) {
            bytevec key; ITEMLEN keylen;
            bytevec val; ITEMLEN vallen;
            unsigned char ctype = rbuf_char(rb);
            enum brt_msg_type type = (enum brt_msg_type) ctype;
            XIDS xids;
            xids_create_from_buffer(rb, &xids);
            rbuf_bytes(rb, &key, &keylen);
            rbuf_bytes(rb, &val, &vallen);

            // <CER> can we factor this out?
            long *dest;
            if (bfe->h->compare_fun) {
                if (brt_msg_type_applies_once(type)) {
                    dest = (long *) &fresh_offsets[nfresh];
                    nfresh++;
                } else if (brt_msg_type_applies_all(type) || brt_msg_type_does_nothing(type)) {
                    dest = (long *) &broadcast_offsets[nbroadcast_offsets];
                    nbroadcast_offsets++;
                } else {
                    assert(false);
                }
            } else {
                dest = NULL;
            }

            // Increment our MSN, the last message should have the
            // newest/highest MSN.  See above for a full explanation.
            lowest.msn++;
            r = toku_fifo_enq(bnc->buffer,
                              key,
                              keylen,
                              val,
                              vallen,
                              type,
                              lowest,
                              xids,
                              true,
                              dest);
            lazy_assert_zero(r);
            n_bytes_in_buffer += keylen + vallen + KEY_VALUE_OVERHEAD + xids_get_serialize_size(xids);
            xids_destroy(&xids);
        }

        if (bfe->h->compare_fun) {
            struct toku_fifo_entry_key_msn_cmp_extra extra = { .desc = &bfe->h->cmp_descriptor,
                                                               .cmp = bfe->h->compare_fun,
                                                               .fifo = bnc->buffer };
            r = mergesort_r(fresh_offsets,
                            nfresh,
                            sizeof fresh_offsets[0],
                            &extra,
                            toku_fifo_entry_key_msn_cmp);
            assert_zero(r);
            toku_omt_destroy(&bnc->fresh_message_tree);
            r = toku_omt_create_steal_sorted_array(&bnc->fresh_message_tree,
                                                   &fresh_offsets,
                                                   nfresh,
                                                   n_in_this_buffer);
            assert_zero(r);
            toku_omt_destroy(&bnc->broadcast_list);
            r = toku_omt_create_steal_sorted_array(&bnc->broadcast_list,
                                                   &broadcast_offsets,
                                                   nbroadcast_offsets,
                                                   n_in_this_buffer);
            assert_zero(r);
        }

        bnc->n_bytes_in_buffer = n_bytes_in_buffer;
    }

    // Assign the highest msn from our upgrade message FIFO queues.
    node->max_msn_applied_to_node_on_disk = highest_msn;
    // Since we assigned MSNs to this node's messages, we need to dirty it.
    node->dirty = 1;

    // Must compute the checksum now (rather than at the end, while we
    // still have the pointer to the buffer).
    if (version >= BRT_FIRST_LAYOUT_VERSION_WITH_END_TO_END_CHECKSUM) {
        u_int32_t expected_xsum = toku_dtoh32(*(u_int32_t*)(rb->buf+rb->size-4));
        u_int32_t actual_xsum   = x1764_memory(rb->buf, rb->size-4);
        if (expected_xsum != actual_xsum) {
            fprintf(stderr, "%s:%d: Bad checksum: expected = %"PRIx32", actual= %"PRIx32"\n",
                    __FUNCTION__,
                    __LINE__,
                    expected_xsum,
                    actual_xsum);
            fflush(stderr);
            return toku_db_badformat();
        }
    }

    return r;
}

// This function takes a deserialized version 13 or 14 buffer and
// constructs the associated leaf brtnode object.
static int
deserialize_and_upgrade_leaf_node(BRTNODE node,
                                  struct rbuf *rb,
                                  struct brtnode_fetch_extra* bfe)
{
    int r = 0;
    int version = node->layout_version_read_from_disk;

    // This is a leaf node, so the offsets in the buffer will be
    // different from the internal node offsets above.
    (void) rbuf_ulonglong(rb);  // 6. nkeys
    (void) rbuf_ulonglong(rb);  // 7. ndata
    (void) rbuf_ulonglong(rb);  // 8. dsize

    if (version == BRT_LAYOUT_VERSION_14) {
        (void) rbuf_int(rb);  // 9. optimized_for_upgrade
    }

    // 10. npartitions - This is really the number of leaf entries in
    // our single basement node.  There should only be 1 (ONE)
    // partition, so there shouldn't be any pivot key stored.  This
    // means the loop will not iterate.  We could remove the loop and
    // assert that the value is indeed 1.
    int npartitions = rbuf_int(rb);
    assert(npartitions == 1);

    // Set number of children to 1, since we will only have one
    // basement node.
    node->n_children = 1;
    XMALLOC_N(node->n_children, node->bp);
    // This is a malloc(0), but we need to do it in order to get a pointer
    // we can free() later.
    XMALLOC_N(node->n_children - 1, node->childkeys);
    node->totalchildkeylens = 0;

    // Create one basement node to contain all the leaf entries by
    // setting up the single partition and updating the bfe.
    update_bfe_using_brtnode(node, bfe);
    struct brtnode_fetch_extra temp_bfe;
    fill_bfe_for_full_read(&temp_bfe, bfe->h);
    setup_partitions_using_bfe(node, &temp_bfe, true);

    // 11. Deserialize the partition maps, though they are not used in the
    // newer versions of brt nodes.
    struct sub_block_map part_map[npartitions];
    for (int i = 0; i < npartitions; ++i) {
        sub_block_map_deserialize(&part_map[i], rb);
    }

    // Copy all of the leaf entries into the single basement node.

    // 12. The number of leaf entries in buffer.
    int n_in_buf = rbuf_int(rb);
    BLB_NBYTESINBUF(node,0) = 0;
    BLB_SEQINSERT(node,0) = 0;
    BASEMENTNODE bn = BLB(node, 0);

    // The current end of the buffer, read from disk and decompressed,
    // is the start of the leaf entries.
    u_int32_t start_of_data = rb->ndone;

    // 13. Read the leaf entries from the buffer, advancing the buffer
    // as we go.
    if (version <= BRT_LAYOUT_VERSION_13) {
        // Create our mempool.
        toku_mempool_construct(&bn->buffer_mempool, 0);
        OMT omt = BLB_BUFFER(node, 0);
        struct mempool *mp = &BLB_BUFFER_MEMPOOL(node, 0);
        // Loop through
        for (int i = 0; i < n_in_buf; ++i) {
            LEAFENTRY_13 le = (LEAFENTRY_13)(&rb->buf[rb->ndone]);
            u_int32_t disksize = leafentry_disksize_13(le);
            rb->ndone += disksize;
            invariant(rb->ndone<=rb->size);
            LEAFENTRY new_le;
            size_t new_le_size;
            r = toku_le_upgrade_13_14(le,
                                      &new_le_size,
                                      &new_le,
                                      omt,
                                      mp);
            assert_zero(r);
            // Copy the pointer value straight into the OMT
            r = toku_omt_insert_at(omt, (OMTVALUE) new_le, i);
            assert_zero(r);
            bn->n_bytes_in_buffer += new_le_size;
        }
    } else {
        u_int32_t end_of_data;
        u_int32_t data_size;

        // Leaf Entry creation for version 14 and above:
        // Allocate space for our leaf entry pointers.
        OMTVALUE *XMALLOC_N(n_in_buf, array);

        // Iterate over leaf entries copying their addresses into our
        // temporary array.
        for (int i = 0; i < n_in_buf; ++i) {
            LEAFENTRY le = (LEAFENTRY)(&rb->buf[rb->ndone]);
            u_int32_t disksize = leafentry_disksize(le);
            rb->ndone += disksize;
            invariant(rb->ndone <= rb->size);
            array[i] = (OMTVALUE) le;
        }

        end_of_data = rb->ndone;
        data_size = end_of_data - start_of_data;

        // Now we must create the OMT and it's associated mempool.

        // Allocate mempool in basement node and memcpy from start of
        // input/deserialized buffer.
        toku_mempool_copy_construct(&bn->buffer_mempool,
                                    &rb->buf[start_of_data],
                                    data_size);

        // Adjust the array of OMT values to point to the correct
        // position in the mempool.  The mempool should have all the
        // data at this point.
        for (int i = 0; i < n_in_buf; ++i) {
            int offset = (unsigned char *) array[i] - &rb->buf[start_of_data];
            unsigned char *mp_base = toku_mempool_get_base(&bn->buffer_mempool);
            array[i] = &mp_base[offset];
        }

        BLB_NBYTESINBUF(node, 0) = data_size;

        toku_omt_destroy(&BLB_BUFFER(node, 0));
        // Construct the omt.
        r = toku_omt_create_steal_sorted_array(&BLB_BUFFER(node, 0),
                                               &array,
                                               n_in_buf,
                                               n_in_buf);
        invariant_zero(r);
    }

    // Whatever this is must be less than the MSNs of every message above
    // it, so it's ok to take it here.
    bn->max_msn_applied = bfe->h->highest_unused_msn_for_upgrade;
    bn->stale_ancestor_messages_applied = false;
    node->max_msn_applied_to_node_on_disk = bn->max_msn_applied;

    // 14. Checksum (end to end) is only on version 14
    if (version >= BRT_FIRST_LAYOUT_VERSION_WITH_END_TO_END_CHECKSUM) {
        u_int32_t expected_xsum = rbuf_int(rb);
        u_int32_t actual_xsum = x1764_memory(rb->buf, rb->size - 4);
        if (expected_xsum != actual_xsum) {
            // TODO: Error handling.
            return 1;
        }
    }

    // We should have read the whole block by this point.
    if (rb->ndone != rb->size) {
        // TODO: Error handling.
        return 1;
    }

    return r;
}

static int
read_and_decompress_block_from_fd_into_rbuf(int fd, BLOCKNUM blocknum,
                                            struct brt_header *h,
                                            struct rbuf *rb,
                                            /* out */ int *layout_version_p);

// This function upgrades a version 14 brtnode to the current
// verison. NOTE: This code assumes the first field of the rbuf has
// already been read from the buffer (namely the layout_version of the
// brtnode.)
static int
deserialize_and_upgrade_brtnode(BRTNODE node,
                                BRTNODE_DISK_DATA* ndd,
                                BLOCKNUM blocknum,
                                struct brtnode_fetch_extra* bfe,
                                int fd)
{
    int r = 0;
    int version;

    // I. First we need to de-compress the entire node, only then can
    // we read the different sub-sections.
    struct rbuf rb;
    read_and_decompress_block_from_fd_into_rbuf(fd,
                                                blocknum,
                                                bfe->h,
                                                &rb,
                                                &version);

    // Re-read the magic field from the previous call, since we are
    // restarting with a fresh rbuf.
    {
        bytevec magic;
        rbuf_literal_bytes(&rb, &magic, 8);
    }

    // II. Start reading brtnode fields out of the decompressed buffer.

    // Copy over old version info.
    node->layout_version_read_from_disk = rbuf_int(&rb);
    version = node->layout_version_read_from_disk;
    assert(version <= BRT_LAYOUT_VERSION_14);
    // Upgrade the current version number to the current version.
    node->layout_version = BRT_LAYOUT_VERSION;

    node->layout_version_original = rbuf_int(&rb);
    node->build_id = rbuf_int(&rb);

    // The remaining offsets into the rbuf do not map to the current
    // version, so we need to fill in the blanks and ignore older
    // fields.
    node->nodesize = rbuf_int(&rb); // 1. nodesize
    node->flags = rbuf_int(&rb);    // 2. flags
    node->height = rbuf_int(&rb);   // 3. height

    // If the version is less than 14, there are two extra ints here.
    // we would need to ignore them if they are there.
    if (version == BRT_LAYOUT_VERSION_13) {
        (void) rbuf_int(&rb);       // 4. rand4
        (void) rbuf_int(&rb);       // 5. local
    }

    // The next offsets are dependent on whether this is a leaf node
    // or not.

    // III. Read in Leaf and Internal Node specific data.

    // Check height to determine whether this is a leaf node or not.
    if (node->height > 0) {
        r = deserialize_and_upgrade_internal_node(node, &rb, bfe);
    } else {
        r = deserialize_and_upgrade_leaf_node(node, &rb, bfe);
    }

    *ndd = toku_xmalloc(node->n_children*sizeof(**ndd));
    // Initialize the partition locations to zero, becuse version 14
    // and below have no notion of partitions on disk.
    for (int i=0; i<node->n_children; i++) {
        BP_START(*ndd,i) = 0;
        BP_SIZE (*ndd,i) = 0;
    }

    toku_free(rb.buf);
    return r;
}

static int
deserialize_brtnode_from_rbuf(
    BRTNODE *brtnode,
    BRTNODE_DISK_DATA* ndd,
    BLOCKNUM blocknum,
    u_int32_t fullhash,
    struct brtnode_fetch_extra* bfe,
    struct rbuf *rb,
    int fd
    )
// Effect: deserializes a brtnode that is in rb (with pointer of rb just past the magic) into a BRTNODE.
{
    int r = 0;
    BRTNODE node = toku_xmalloc(sizeof(*node));
    struct sub_block sb_node_info;
    // fill in values that are known and not stored in rb
    node->fullhash = fullhash;
    node->thisnodename = blocknum;
    node->dirty = 0;

    // now start reading from rbuf

    // first thing we do is read the header information
    bytevec magic;
    rbuf_literal_bytes(rb, &magic, 8);
    if (memcmp(magic, "tokuleaf", 8)!=0 &&
        memcmp(magic, "tokunode", 8)!=0) {
        r = toku_db_badformat();
        goto cleanup;
    }

    node->layout_version_read_from_disk = rbuf_int(rb);
    int version = node->layout_version_read_from_disk;
    assert(version >= BRT_LAYOUT_MIN_SUPPORTED_VERSION);

    // Check if we are reading in an older node version.
    if (version <= BRT_LAYOUT_VERSION_14) {
        // Perform the upgrade.
        r = deserialize_and_upgrade_brtnode(node, ndd, blocknum, bfe, fd);
        if (r != 0) {
            goto cleanup;
        }

        if (version <= BRT_LAYOUT_VERSION_13) {
            // deprecate 'TOKU_DB_VALCMP_BUILTIN'. just remove the flag
            node->flags &= ~TOKU_DB_VALCMP_BUILTIN_13;
        }

        // If everything is ok, just re-assign the brtnode and retrn.
        *brtnode = node;
        r = 0;
        goto cleanup;
    } else if (version == BRT_LAYOUT_VERSION_18) {
        // Upgrade version 18 to version 19.  This upgrade is trivial,
        // it removes the optimized for upgrade field, which has
        // already been removed in the deserialization code (see
        // deserialize_brtnode_info()).
        version = BRT_LAYOUT_VERSION;
    }

    // TODO 4053
    invariant(version == BRT_LAYOUT_VERSION);
    node->layout_version = version;
    node->layout_version_original = rbuf_int(rb);
    node->build_id = rbuf_int(rb);
    node->n_children = rbuf_int(rb);
    XMALLOC_N(node->n_children, node->bp);
    *ndd = toku_xmalloc(node->n_children*sizeof(**ndd));
    // read the partition locations
    for (int i=0; i<node->n_children; i++) {
        BP_START(*ndd,i) = rbuf_int(rb);
        BP_SIZE (*ndd,i) = rbuf_int(rb);
    }
    // verify checksum of header stored
    u_int32_t checksum = x1764_memory(rb->buf, rb->ndone);
    u_int32_t stored_checksum = rbuf_int(rb);
    if (stored_checksum != checksum) {
        dump_bad_block(rb->buf, rb->size);
        invariant(stored_checksum == checksum);
    }

    //now we read and decompress the pivot and child information
    sub_block_init(&sb_node_info);
    read_and_decompress_sub_block(rb, &sb_node_info);
    // at this point, sb->uncompressed_ptr stores the serialized node info
    deserialize_brtnode_info(&sb_node_info, node);
    toku_free(sb_node_info.uncompressed_ptr);

    // now that the node info has been deserialized, we can proceed to deserialize
    // the individual sub blocks
    assert(bfe->type == brtnode_fetch_none || bfe->type == brtnode_fetch_subset || bfe->type == brtnode_fetch_all || bfe->type == brtnode_fetch_prefetch);

    // setup the memory of the partitions
    // for partitions being decompressed, create either FIFO or basement node
    // for partitions staying compressed, create sub_block
    setup_brtnode_partitions(node, bfe, true);

    // Previously, this code was a for loop with spawns inside and a sync at the end.
    // But now the loop is parallelizeable since we don't have a dependency on the work done so far.
    cilk_for (int i = 0; i < node->n_children; i++) {
        u_int32_t curr_offset = BP_START(*ndd,i);
        u_int32_t curr_size   = BP_SIZE(*ndd,i);
        // the compressed, serialized partitions start at where rb is currently pointing,
        // which would be rb->buf + rb->ndone
        // we need to intialize curr_rbuf to point to this place
        struct rbuf curr_rbuf  = {.buf = NULL, .size = 0, .ndone = 0};
        rbuf_init(&curr_rbuf, rb->buf + curr_offset, curr_size);

        //
        // now we are at the point where we have:
        //  - read the entire compressed node off of disk,
        //  - decompressed the pivot and offset information,
        //  - have arrived at the individual partitions.
        //
        // Based on the information in bfe, we want to decompress a subset of
        // of the compressed partitions (also possibly none or possibly all)
        // The partitions that we want to decompress and make available
        // to the node, we do, the rest we simply copy in compressed
        // form into the node, and set the state of the partition to PT_COMPRESSED
        //

        struct sub_block curr_sb;
        sub_block_init(&curr_sb);

	// curr_rbuf is passed by value to decompress_and_deserialize_worker, so there's no ugly race condition.
	// This would be more obvious if curr_rbuf were an array.

        // deserialize_brtnode_info figures out what the state
        // should be and sets up the memory so that we are ready to use it

	switch (BP_STATE(node,i)) {
	case PT_AVAIL:
	    //  case where we read and decompress the partition
            decompress_and_deserialize_worker(curr_rbuf, curr_sb, node, i, &bfe->h->cmp_descriptor, bfe->h->compare_fun);
	    continue;
	case PT_COMPRESSED:
	    // case where we leave the partition in the compressed state
            check_and_copy_compressed_sub_block_worker(curr_rbuf, curr_sb, node, i);
	    continue;
	case PT_INVALID: // this is really bad
	case PT_ON_DISK: // it's supposed to be in memory.
	    assert(0);
	    continue;
        }
	assert(0);
    }
    *brtnode = node;
    r = 0;
cleanup:
    if (r != 0) {
        if (node) toku_free(node);
    }
    return r;
}

void
toku_deserialize_bp_from_disk(BRTNODE node, BRTNODE_DISK_DATA ndd, int childnum, int fd, struct brtnode_fetch_extra* bfe) {
    assert(BP_STATE(node,childnum) == PT_ON_DISK);
    assert(node->bp[childnum].ptr.tag == BCT_NULL);
    
    //
    // setup the partition
    //
    setup_available_brtnode_partition(node, childnum);
    BP_STATE(node,childnum) = PT_AVAIL;
    
    //
    // read off disk and make available in memory
    // 
    // get the file offset and block size for the block
    DISKOFF node_offset, total_node_disk_size;
    toku_translate_blocknum_to_offset_size(
        bfe->h->blocktable, 
        node->thisnodename, 
        &node_offset, 
        &total_node_disk_size
        );

    u_int32_t curr_offset = BP_START(ndd, childnum);
    u_int32_t curr_size   = BP_SIZE (ndd, childnum);
    struct rbuf rb = {.buf = NULL, .size = 0, .ndone = 0};

    u_int8_t *XMALLOC_N(curr_size, raw_block);
    rbuf_init(&rb, raw_block, curr_size);
    {
        // read the block
        ssize_t rlen = toku_os_pread(fd, raw_block, curr_size, node_offset+curr_offset);
        lazy_assert((DISKOFF)rlen == curr_size);
    }

    struct sub_block curr_sb;
    sub_block_init(&curr_sb);

    read_and_decompress_sub_block(&rb, &curr_sb);
    // at this point, sb->uncompressed_ptr stores the serialized node partition
    deserialize_brtnode_partition(&curr_sb, node, childnum, &bfe->h->cmp_descriptor, bfe->h->compare_fun);
    toku_free(raw_block);
}

// Take a brtnode partition that is in the compressed state, and make it avail
void
toku_deserialize_bp_from_compressed(BRTNODE node, int childnum,
                                    DESCRIPTOR desc, brt_compare_func cmp) {
    assert(BP_STATE(node, childnum) == PT_COMPRESSED);
    SUB_BLOCK curr_sb = BSB(node, childnum);

    assert(curr_sb->uncompressed_ptr == NULL);
    curr_sb->uncompressed_ptr = toku_xmalloc(curr_sb->uncompressed_size);

    setup_available_brtnode_partition(node, childnum);
    BP_STATE(node,childnum) = PT_AVAIL;
    // decompress the sub_block
    toku_decompress(
        curr_sb->uncompressed_ptr,
        curr_sb->uncompressed_size,
        curr_sb->compressed_ptr,
        curr_sb->compressed_size
        );
    deserialize_brtnode_partition(curr_sb, node, childnum, desc, cmp);
    toku_free(curr_sb->compressed_ptr);
    toku_free(curr_sb);
}


// Read brt node from file into struct.  Perform version upgrade if necessary.
int toku_deserialize_brtnode_from (int fd,
				   BLOCKNUM blocknum,
				   u_int32_t fullhash,
				   BRTNODE *brtnode,
				   BRTNODE_DISK_DATA* ndd,
				   struct brtnode_fetch_extra* bfe)
// Effect: Read a node in.  If possible, read just the header.
{
    toku_trace("deserial start");

    struct rbuf rb = RBUF_INITIALIZER;
    read_brtnode_header_from_fd_into_rbuf_if_small_enough(fd, blocknum, bfe->h, &rb);

    int r = deserialize_brtnode_header_from_rbuf_if_small_enough(brtnode, ndd, blocknum, fullhash, bfe, &rb, fd);
    if (r != 0) {
	toku_free(rb.buf);
	rb = RBUF_INITIALIZER;
	
	// Something went wrong, go back to doing it the old way.

	r = read_block_from_fd_into_rbuf(fd, blocknum, bfe->h, &rb);
	if (r != 0) { goto cleanup; } // if we were successful, then we are done.

	r = deserialize_brtnode_from_rbuf(brtnode, ndd, blocknum, fullhash, bfe, &rb, fd);
	if (r!=0) {
	    dump_bad_block(rb.buf,rb.size);
	}
	lazy_assert_zero(r);

    }
    toku_trace("deserial done");

cleanup:
    toku_free(rb.buf);
    return r;
}

void
toku_verify_or_set_counts(BRTNODE node) {
    if (node->height==0) {
        for (int i=0; i<node->n_children; i++) {
            lazy_assert(BLB_BUFFER(node, i));
            struct sum_info sum_info = {0,0};
            toku_omt_iterate(BLB_BUFFER(node, i), sum_item, &sum_info);
            lazy_assert(sum_info.count==toku_omt_size(BLB_BUFFER(node, i)));
            lazy_assert(sum_info.dsum==BLB_NBYTESINBUF(node, i));
        }
    }
    else {
        // nothing to do because we no longer store n_bytes_in_buffers for
        // the whole node
    }
}

static u_int32_t
serialize_brt_header_min_size (u_int32_t version) {
    u_int32_t size = 0;


    switch(version) {
    case BRT_LAYOUT_VERSION_19:
        size += 1; // compression method
	    size += sizeof(uint64_t);  // highest_unused_msn_for_upgrade
        case BRT_LAYOUT_VERSION_18:
	    size += sizeof(uint64_t);  // time_of_last_optimize_begin
	    size += sizeof(uint64_t);  // time_of_last_optimize_end
	    size += sizeof(uint32_t);  // count_of_optimize_in_progress
	    size += sizeof(MSN);       // msn_at_start_of_last_completed_optimize
            size -= 8;                 // removed num_blocks_to_upgrade_14
            size -= 8;                 // removed num_blocks_to_upgrade_13
        case BRT_LAYOUT_VERSION_17:
	    size += 16;
	    invariant(sizeof(STAT64INFO_S) == 16);
        case BRT_LAYOUT_VERSION_16:
        case BRT_LAYOUT_VERSION_15:
            size += 4;  // basement node size
            size += 8;  // num_blocks_to_upgrade_14 (previously num_blocks_to_upgrade, now one int each for upgrade from 13, 14
            size += 8;  // time of last verification
        case BRT_LAYOUT_VERSION_14:
            size += 8;  //TXNID that created
        case BRT_LAYOUT_VERSION_13:
            size += ( 4 // build_id
                     +4 // build_id_original
                     +8 // time_of_creation
                     +8 // time_of_last_modification
                    );
            // fall through
        case BRT_LAYOUT_VERSION_12:
	    size += (+8 // "tokudata"
		     +4 // version
		     +4 // original_version
		     +4 // size
		     +8 // byte order verification
		     +8 // checkpoint_count
		     +8 // checkpoint_lsn
		     +4 // tree's nodesize
		     +8 // translation_size_on_disk
		     +8 // translation_address_on_disk
		     +4 // checksum
                     +8 // Number of blocks in old version.
	             +8 // diskoff
		     +4 // flags
		   );
	    break;
        default:
            lazy_assert(FALSE);
    }
    lazy_assert(size <= BLOCK_ALLOCATOR_HEADER_RESERVE);
    return size;
}

int toku_serialize_brt_header_size (struct brt_header *h) {
    u_int32_t size = serialize_brt_header_min_size(h->layout_version);
    //There is no dynamic data.
    lazy_assert(size <= BLOCK_ALLOCATOR_HEADER_RESERVE);
    return size;
}


int toku_serialize_brt_header_to_wbuf (struct wbuf *wbuf, struct brt_header *h, DISKOFF translation_location_on_disk, DISKOFF translation_size_on_disk) {
    wbuf_literal_bytes(wbuf, "tokudata", 8);
    wbuf_network_int  (wbuf, h->layout_version); //MUST be in network order regardless of disk order
    wbuf_network_int  (wbuf, BUILD_ID); //MUST be in network order regardless of disk order
    wbuf_network_int  (wbuf, wbuf->size); //MUST be in network order regardless of disk order
    wbuf_literal_bytes(wbuf, &toku_byte_order_host, 8); //Must not translate byte order
    wbuf_ulonglong(wbuf, h->checkpoint_count);
    wbuf_LSN    (wbuf, h->checkpoint_lsn);
    wbuf_int    (wbuf, h->nodesize);

    //printf("%s:%d bta=%lu size=%lu\n", __FILE__, __LINE__, h->block_translation_address_on_disk, 4 + 16*h->translated_blocknum_limit);
    wbuf_DISKOFF(wbuf, translation_location_on_disk);
    wbuf_DISKOFF(wbuf, translation_size_on_disk);
    wbuf_BLOCKNUM(wbuf, h->root_blocknum);
    wbuf_int(wbuf, h->flags);
    wbuf_int(wbuf, h->layout_version_original);
    wbuf_int(wbuf, h->build_id_original);
    wbuf_ulonglong(wbuf, h->time_of_creation);
    wbuf_ulonglong(wbuf, h->time_of_last_modification);
    wbuf_TXNID(wbuf, h->root_xid_that_created);
    wbuf_int(wbuf, h->basementnodesize);
    wbuf_ulonglong(wbuf, h->time_of_last_verification);
    wbuf_ulonglong(wbuf, h->checkpoint_staging_stats.numrows);
    wbuf_ulonglong(wbuf, h->checkpoint_staging_stats.numbytes);
    wbuf_ulonglong(wbuf, h->time_of_last_optimize_begin);
    wbuf_ulonglong(wbuf, h->time_of_last_optimize_end);
    wbuf_int(wbuf, h->count_of_optimize_in_progress);
    wbuf_MSN(wbuf, h->msn_at_start_of_last_completed_optimize);
    wbuf_char(wbuf, (unsigned char) h->compression_method);
    wbuf_MSN(wbuf, h->highest_unused_msn_for_upgrade);
    u_int32_t checksum = x1764_finish(&wbuf->checksum);
    wbuf_int(wbuf, checksum);
    lazy_assert(wbuf->ndone == wbuf->size);
    return 0;
}

int toku_serialize_brt_header_to (int fd, struct brt_header *h) {
    int rr = 0;
    if (h->panic) return h->panic;
    lazy_assert(h->type==BRTHEADER_CHECKPOINT_INPROGRESS);
    toku_brtheader_lock(h);
    struct wbuf w_translation;
    int64_t size_translation;
    int64_t address_translation;
    {
        //Must serialize translation first, to get address,size for header.
        toku_serialize_translation_to_wbuf_unlocked(h->blocktable, &w_translation,
                                                   &address_translation,
                                                   &size_translation);
        lazy_assert(size_translation==w_translation.size);
    }
    struct wbuf w_main;
    unsigned int size_main = toku_serialize_brt_header_size (h);
    {
	wbuf_init(&w_main, toku_xmalloc(size_main), size_main);
	{
	    int r=toku_serialize_brt_header_to_wbuf(&w_main, h, address_translation, size_translation);
	    lazy_assert_zero(r);
	}
	lazy_assert(w_main.ndone==size_main);
    }
    toku_brtheader_unlock(h);
    lock_for_pwrite();
    {
        //Actual Write translation table
	toku_full_pwrite_extend(fd, w_translation.buf,
				size_translation, address_translation);
    }
    {
        //Everything but the header MUST be on disk before header starts.
        //Otherwise we will think the header is good and some blocks might not
        //yet be on disk.
        //If the header has a cachefile we need to do cachefile fsync (to
        //prevent crash if we redirected to dev null)
        //If there is no cachefile we still need to do an fsync.
        if (h->cf) {
            rr = toku_cachefile_fsync(h->cf);
        }
        else {
            rr = toku_file_fsync(fd);
        }
        if (rr==0) {
            //Alternate writing header to two locations:
            //   Beginning (0) or BLOCK_ALLOCATOR_HEADER_RESERVE
            toku_off_t main_offset;
            main_offset = (h->checkpoint_count & 0x1) ? 0 : BLOCK_ALLOCATOR_HEADER_RESERVE;
            toku_full_pwrite_extend(fd, w_main.buf, w_main.ndone, main_offset);
        }
    }
    toku_free(w_main.buf);
    toku_free(w_translation.buf);
    unlock_for_pwrite();
    return rr;
}

// not version-sensitive because we only serialize a descriptor using the current layout_version
u_int32_t
toku_serialize_descriptor_size(const DESCRIPTOR desc) {
    //Checksum NOT included in this.  Checksum only exists in header's version.
    u_int32_t size = 4; // four bytes for size of descriptor
    size += desc->dbt.size;
    return size;
}

static u_int32_t
deserialize_descriptor_size(const DESCRIPTOR desc, int layout_version) {
    //Checksum NOT included in this.  Checksum only exists in header's version.
    u_int32_t size = 4; // four bytes for size of descriptor
    if (layout_version == BRT_LAYOUT_VERSION_13)
	size += 4;   // for version 13, include four bytes of "version"
    size += desc->dbt.size;
    return size;
}

void
toku_serialize_descriptor_contents_to_wbuf(struct wbuf *wb, const DESCRIPTOR desc) {
    wbuf_bytes(wb, desc->dbt.data, desc->dbt.size);
}

//Descriptor is written to disk during toku_brt_open iff we have a new (or changed)
//descriptor.
//Descriptors are NOT written during the header checkpoint process.
int
toku_serialize_descriptor_contents_to_fd(int fd, const DESCRIPTOR desc, DISKOFF offset) {
    int r = 0;
    // make the checksum
    int64_t size = toku_serialize_descriptor_size(desc)+4; //4 for checksum
    struct wbuf w;
    wbuf_init(&w, toku_xmalloc(size), size);
    toku_serialize_descriptor_contents_to_wbuf(&w, desc);
    {
        //Add checksum
        u_int32_t checksum = x1764_finish(&w.checksum);
        wbuf_int(&w, checksum);
    }
    lazy_assert(w.ndone==w.size);
    {
        lock_for_pwrite();
        //Actual Write translation table
	toku_full_pwrite_extend(fd, w.buf, size, offset);
        unlock_for_pwrite();
    }
    toku_free(w.buf);
    return r;
}

static void
deserialize_descriptor_from_rbuf(struct rbuf *rb, DESCRIPTOR desc, int layout_version) {
    if (layout_version <= BRT_LAYOUT_VERSION_13) {
        // in older versions of TokuDB the Descriptor had a 4 byte
        // version, which we skip over
        (void) rbuf_int(rb);
    }

    u_int32_t size;
    bytevec data;
    rbuf_bytes(rb, &data, &size);
    bytevec data_copy = data;
    if (size > 0) {
        data_copy = toku_memdup(data, size); //Cannot keep the reference from rbuf. Must copy.
        lazy_assert(data_copy);
    } else {
        lazy_assert(size==0);
        data_copy = NULL;
    }
    toku_fill_dbt(&desc->dbt, data_copy, size);
}

static enum deserialize_error_code
deserialize_descriptor_from(int fd, BLOCK_TABLE bt, DESCRIPTOR desc, int layout_version) {
    enum deserialize_error_code e;
    DISKOFF offset;
    DISKOFF size;
    unsigned char *dbuf = NULL;
    toku_get_descriptor_offset_size(bt, &offset, &size);
    memset(desc, 0, sizeof(*desc));
    if (size > 0) {
        lazy_assert(size>=4); //4 for checksum
        {
            XMALLOC_N(size, dbuf);
            {
                lock_for_pwrite();
                ssize_t r = toku_os_pread(fd, dbuf, size, offset);
                lazy_assert(r==size);
                unlock_for_pwrite();
            }
            {
                // check the checksum
                u_int32_t x1764 = x1764_memory(dbuf, size-4);
                //printf("%s:%d read from %ld (x1764 offset=%ld) size=%ld\n", __FILE__, __LINE__, block_translation_address_on_disk, offset, block_translation_size_on_disk);
                u_int32_t stored_x1764 = toku_dtoh32(*(int*)(dbuf + size-4));
                if (x1764 != stored_x1764) {
                    fprintf(stderr, "Descriptor checksum failure: calc=0x%08x read=0x%08x\n", x1764, stored_x1764);
                    e = DS_XSUM_FAIL;
                    toku_free(dbuf);
                    goto exit;
                }
            }
            {
                struct rbuf rb = {.buf = dbuf, .size = size, .ndone = 0};
                //Not temporary; must have a toku_memdup'd copy.
                deserialize_descriptor_from_rbuf(&rb, desc, layout_version);
            }
            lazy_assert(deserialize_descriptor_size(desc, layout_version)+4 == size);
            toku_free(dbuf);
        }
    }
    e = DS_OK;
exit:
    return e;
}

static void
upgrade_subtree_estimates_to_stat64info(int UU(fd), struct brt_header *h)
{
    int r;
    // 15 was the last version with subtree estimates
    invariant(h->layout_version_read_from_disk <= BRT_LAYOUT_VERSION_15);
    BLOCKNUM b = h->root_blocknum;
    struct rbuf rb_s;
    struct rbuf *rb = &rb_s;
    rbuf_init(rb, NULL, 0);
    DISKOFF offset, size;
    toku_translate_blocknum_to_offset_size(h->blocktable, b, &offset, &size);
    {
        u_int8_t *XMALLOC_N(size, raw_block);
        {
            ssize_t rlen = pread(fd, raw_block, size, offset);
            lazy_assert((DISKOFF)rlen == size);
        }
        {
            // root node must be a leaf or nonleaf node
            u_int8_t *magic = raw_block + uncompressed_magic_offset;
            invariant(memcmp(magic, "tokuleaf", 8) == 0 || memcmp(magic, "tokunode", 8) == 0);
            // root node cannot have a different version from the header, if
            // the header needs to read its subtree estimates
            u_int8_t *version = raw_block + uncompressed_version_offset;
            int layout_version = toku_dtoh32(*(uint32_t*)version);
            invariant(layout_version == h->layout_version_read_from_disk);
        }
        {
            int n_sub_blocks = toku_dtoh32(*(u_int32_t*)&raw_block[node_header_overhead]);
            invariant(0 <= n_sub_blocks && n_sub_blocks <= max_sub_blocks);
            {
                u_int32_t header_length = node_header_overhead + sub_block_header_size(n_sub_blocks);
                invariant(header_length <= size);
                u_int32_t xsum = x1764_memory(raw_block, header_length);
                u_int32_t stored_xsum = toku_dtoh32(*(u_int32_t *)(raw_block + header_length));
                invariant(xsum == stored_xsum);
            }
            struct sub_block sub_block[n_sub_blocks];
            u_int32_t *sub_block_header = (u_int32_t *) &raw_block[node_header_overhead + 4];
            size_t uncompressed_size = 0;
            for (int i = 0; i < n_sub_blocks; ++i) {
                sub_block_init(&sub_block[i]);
                int64_t csize = toku_dtoh32(sub_block_header[0]);
                int64_t usize = toku_dtoh32(sub_block_header[1]);
                invariant(0 <= csize && csize < (1<<30));
                invariant(0 <= usize && usize < (1<<30));
                sub_block[i].compressed_size = csize;
                sub_block[i].uncompressed_size = usize;
                sub_block[i].xsum = toku_dtoh32(sub_block_header[2]);
                uncompressed_size += sub_block[i].uncompressed_size;
                sub_block_header += 3;
            }
            unsigned char *buf = toku_xmalloc(node_header_overhead + uncompressed_size);
            resource_assert(buf);
            rbuf_init(rb, buf, node_header_overhead + uncompressed_size);
            memcpy(rb->buf, raw_block, node_header_overhead);
            unsigned char *compressed_data = raw_block + node_header_overhead + sub_block_header_size(n_sub_blocks) + sizeof(u_int32_t);
            unsigned char *uncompressed_data = rb->buf + node_header_overhead;
            r = decompress_all_sub_blocks(n_sub_blocks, sub_block, compressed_data, uncompressed_data, num_cores, brt_pool);
            if (r != 0) {
                fprintf(stderr, "%s:%d block %"PRId64" failed %d at %p size %zu\n", __FUNCTION__, __LINE__, b.b, r, raw_block, size);
                dump_bad_block(raw_block, size);
            }
            lazy_assert_zero(r);
            rb->ndone = 0;
        }
        toku_free(raw_block);
    }
    resource_assert(rb->buf);
    bytevec magic;
    rbuf_literal_bytes(rb, &magic, 8);
    int node_version = rbuf_int(rb);
    invariant(node_version == h->layout_version_read_from_disk);
    (void) rbuf_int(rb); // layout_version_original
    (void) rbuf_int(rb); // build_id
    (void) rbuf_int(rb); // nodesize
    (void) rbuf_int(rb); // flags
    int height = rbuf_int(rb);
    if (node_version <= BRT_LAST_LAYOUT_VERSION_WITH_FINGERPRINT) {
        (void) rbuf_int(rb); // rand4fingerprint
        (void) rbuf_int(rb); // localfingerprint
        (void) rbuf_int(rb); // another fingerprint (according to deserialize_brtnode_nonleaf_from_rbuf in 5.0.8)
    }
    h->on_disk_stats = ZEROSTATS;
    if (height > 0) {
        invariant(memcmp(magic, "tokunode", 8) == 0);
        int n_children = rbuf_int(rb);
        for (int i = 0; i < n_children; ++i) {
            if (node_version <= BRT_LAST_LAYOUT_VERSION_WITH_FINGERPRINT) {
                (void) rbuf_int(rb); // child fingerprint
            }
            u_int64_t nkeys = rbuf_ulonglong(rb);
            u_int64_t ndata = rbuf_ulonglong(rb);
            invariant(nkeys == ndata);
            h->on_disk_stats.numrows += nkeys;
            h->on_disk_stats.numbytes += rbuf_ulonglong(rb);
            (void) rbuf_char(rb); // exact
        }
    } else {
        invariant(memcmp(magic, "tokuleaf", 8) == 0);
        u_int64_t nkeys = rbuf_ulonglong(rb);
        u_int64_t ndata = rbuf_ulonglong(rb);
        invariant(nkeys == ndata);
        h->on_disk_stats.numrows += nkeys;
        h->on_disk_stats.numbytes += rbuf_ulonglong(rb);
    }

    // done, discard the rest
    toku_free(rb->buf);
}

// We only deserialize brt header once and then share everything with all the brts.
static enum deserialize_error_code
deserialize_brtheader_versioned(int fd, struct rbuf *rb, struct brt_header **brth, uint32_t version)
{
    enum deserialize_error_code e = DS_OK;
    struct brt_header *h = NULL;
    invariant(version >= BRT_LAYOUT_MIN_SUPPORTED_VERSION);
    invariant(version <= BRT_LAYOUT_VERSION);
    // We already know:
    //  we have an rbuf representing the header.
    //  The checksum has been validated

    //Verification of initial elements.
    //Check magic number
    bytevec magic;
    rbuf_literal_bytes(rb, &magic, 8);
    lazy_assert(memcmp(magic,"tokudata",8)==0);

    CALLOC(h);
    if (!h) {
        e = DS_ERRNO;
        goto exit;
    }
    h->type = BRTHEADER_CURRENT;
    h->checkpoint_header = NULL;
    h->dirty = 0;
    h->panic = 0;
    h->panic_string = 0;
    toku_list_init(&h->live_brts);
    toku_list_init(&h->zombie_brts);
    toku_list_init(&h->checkpoint_before_commit_link);

    //version MUST be in network order on disk regardless of disk order
    h->layout_version_read_from_disk = rbuf_network_int(rb);
    invariant(h->layout_version_read_from_disk >= BRT_LAYOUT_MIN_SUPPORTED_VERSION);
    invariant(h->layout_version_read_from_disk <= BRT_LAYOUT_VERSION);
    h->layout_version = BRT_LAYOUT_VERSION;

    //build_id MUST be in network order on disk regardless of disk order
    h->build_id = rbuf_network_int(rb);

    //Size MUST be in network order regardless of disk order.
    u_int32_t size = rbuf_network_int(rb);
    lazy_assert(size == rb->size);

    bytevec tmp_byte_order_check;
    lazy_assert((sizeof tmp_byte_order_check) >= 8);
    rbuf_literal_bytes(rb, &tmp_byte_order_check, 8); //Must not translate byte order
    int64_t byte_order_stored = *(int64_t*)tmp_byte_order_check;
    lazy_assert(byte_order_stored == toku_byte_order_host);

    h->checkpoint_count = rbuf_ulonglong(rb);
    h->checkpoint_lsn = rbuf_lsn(rb);
    h->nodesize = rbuf_int(rb);
    DISKOFF translation_address_on_disk = rbuf_diskoff(rb);
    DISKOFF translation_size_on_disk = rbuf_diskoff(rb);
    lazy_assert(translation_address_on_disk > 0);
    lazy_assert(translation_size_on_disk > 0);

    // initialize the tree lock
    toku_brtheader_init_treelock(h);

    //Load translation table
    {
        lock_for_pwrite();
        unsigned char *XMALLOC_N(translation_size_on_disk, tbuf);
        {
            // This cast is messed up in 32-bits if the block translation
            // table is ever more than 4GB.  But in that case, the
            // translation table itself won't fit in main memory.
            ssize_t readsz = toku_os_pread(fd, tbuf, translation_size_on_disk,
                                           translation_address_on_disk);
            lazy_assert(readsz == translation_size_on_disk);
        }
        unlock_for_pwrite();
        // Create table and read in data.
        e = toku_blocktable_create_from_buffer(&h->blocktable,
                                               translation_address_on_disk,
                                               translation_size_on_disk,
                                               tbuf);
        toku_free(tbuf);
        if (e != DS_OK) {
            goto exit;
        }
    }

    h->root_blocknum = rbuf_blocknum(rb);
    h->flags = rbuf_int(rb);
    if (h->layout_version_read_from_disk <= BRT_LAYOUT_VERSION_13) {
        // deprecate 'TOKU_DB_VALCMP_BUILTIN'. just remove the flag
        h->flags &= ~TOKU_DB_VALCMP_BUILTIN_13;
    }
    h->layout_version_original = rbuf_int(rb);
    h->build_id_original = rbuf_int(rb);
    h->time_of_creation  = rbuf_ulonglong(rb);
    h->time_of_last_modification = rbuf_ulonglong(rb);
    h->time_of_last_verification = 0;
    if (h->layout_version_read_from_disk <= BRT_LAYOUT_VERSION_18) {
        // 17 was the last version with these fields, we no longer store
        // them, so read and discard them
        (void) rbuf_ulonglong(rb);  // num_blocks_to_upgrade_13
        if (h->layout_version_read_from_disk >= BRT_LAYOUT_VERSION_15) {
            (void) rbuf_ulonglong(rb);  // num_blocks_to_upgrade_14
        }
    }

    if (h->layout_version_read_from_disk >= BRT_LAYOUT_VERSION_14) {
        rbuf_TXNID(rb, &h->root_xid_that_created);
    } else {
        // fake creation during the last checkpoint
        h->root_xid_that_created = h->checkpoint_lsn.lsn;
    }

    if (h->layout_version_read_from_disk >= BRT_LAYOUT_VERSION_15) {
        h->basementnodesize = rbuf_int(rb);
        h->time_of_last_verification = rbuf_ulonglong(rb);
    } else {
        h->basementnodesize = BRT_DEFAULT_BASEMENT_NODE_SIZE;
        h->time_of_last_verification = 0;
    }

    if (h->layout_version_read_from_disk >= BRT_LAYOUT_VERSION_18) {
        h->on_disk_stats.numrows = rbuf_ulonglong(rb);
        h->on_disk_stats.numbytes = rbuf_ulonglong(rb);
        h->in_memory_stats = h->on_disk_stats;
        h->time_of_last_optimize_begin = rbuf_ulonglong(rb);
        h->time_of_last_optimize_end = rbuf_ulonglong(rb);
        h->count_of_optimize_in_progress = rbuf_int(rb);
        h->count_of_optimize_in_progress_read_from_disk = h->count_of_optimize_in_progress;
        h->msn_at_start_of_last_completed_optimize = rbuf_msn(rb);
    } else {
        upgrade_subtree_estimates_to_stat64info(fd, h);
        h->time_of_last_optimize_begin = 0;
        h->time_of_last_optimize_end = 0;
        h->count_of_optimize_in_progress = 0;
        h->count_of_optimize_in_progress_read_from_disk = 0;
        h->msn_at_start_of_last_completed_optimize = ZERO_MSN;
    }
    if (h->layout_version_read_from_disk >= BRT_LAYOUT_VERSION_19) {
        unsigned char method = rbuf_char(rb);
        h->compression_method = (enum toku_compression_method) method;
        h->highest_unused_msn_for_upgrade = rbuf_msn(rb);
    } else {
        // we hard coded zlib until 5.2, then quicklz in 5.2
        if (h->layout_version_read_from_disk < BRT_LAYOUT_VERSION_18) {
            h->compression_method = TOKU_ZLIB_METHOD;
        } else {
            h->compression_method = TOKU_QUICKLZ_METHOD;
        }
        h->highest_unused_msn_for_upgrade.msn = MIN_MSN.msn - 1;
    }

    (void) rbuf_int(rb); //Read in checksum and ignore (already verified).
    if (rb->ndone != rb->size) {
        fprintf(stderr, "Header size did not match contents.\n");
        errno = EINVAL;
        e = DS_ERRNO;
        goto exit;
    }

    invariant(h);
    invariant((uint32_t) h->layout_version_read_from_disk == version);
    e = deserialize_descriptor_from(fd, h->blocktable, &h->descriptor, version);
    if (e != DS_OK) {
        goto exit;
    }
    // copy descriptor to cmp_descriptor for #4541
    h->cmp_descriptor.dbt.size = h->descriptor.dbt.size;
    h->cmp_descriptor.dbt.data = toku_xmemdup(h->descriptor.dbt.data, h->descriptor.dbt.size);
    // Version 13 descriptors had an extra 4 bytes that we don't read
    // anymore.  Since the header is going to think it's the current
    // version if it gets written out, we need to write the descriptor in
    // the new format (without those bytes) before that happens.
    int r = toku_update_descriptor(h, &h->cmp_descriptor, fd);
    if (r != 0) {
        errno = r;
        e = DS_ERRNO;
        goto exit;
    }
exit:
    if (e != DS_OK && h != NULL) {
        toku_free(h);
        h = NULL;
    }
    *brth = h;
    return e;
}

// Simply reading the raw bytes of the header into an rbuf is insensitive
// to disk format version.  If that ever changes, then modify this.
//
// TOKUDB_DICTIONARY_NO_HEADER means we can overwrite everything in the
// file AND the header is useless
static int
deserialize_brtheader_from_fd_into_rbuf(int fd,
                                        toku_off_t offset_of_header,
                                        struct rbuf *rb,
                                        u_int64_t *checkpoint_count,
                                        LSN *checkpoint_lsn,
                                        u_int32_t * version_p,
                                        enum deserialize_error_code *e)
{
    int r = 0;
    const int64_t prefix_size = 8 + // magic ("tokudata")
                                4 + // version
                                4 + // build_id
                                4;  // size
    unsigned char prefix[prefix_size];
    rb->buf = NULL;
    int64_t n = toku_os_pread(fd, prefix, prefix_size, offset_of_header);
    if (n != prefix_size) {
        if (n==0) {
            r = TOKUDB_DICTIONARY_NO_HEADER;
        } else if (n<0) {
            r = errno;
            lazy_assert(r!=0);
        } else {
            r = EINVAL;
        }
        goto exit;
    }

    rbuf_init(rb, prefix, prefix_size);

    //Check magic number
    bytevec magic;
    rbuf_literal_bytes(rb, &magic, 8);
    if (memcmp(magic,"tokudata",8)!=0) {
        if ((*(u_int64_t*)magic) == 0) {
            r = TOKUDB_DICTIONARY_NO_HEADER;
        } else {
            r = EINVAL; //Not a tokudb file! Do not use.
        }
        goto exit;
    }

    //Version MUST be in network order regardless of disk order.
    u_int32_t version = rbuf_network_int(rb);
    *version_p = version;
    if (version < BRT_LAYOUT_MIN_SUPPORTED_VERSION) {
        r = TOKUDB_DICTIONARY_TOO_OLD; //Cannot use
        goto exit;
    } else if (version > BRT_LAYOUT_VERSION) {
        r = TOKUDB_DICTIONARY_TOO_NEW; //Cannot use
        goto exit;
    }

    //build_id MUST be in network order regardless of disk order.
    u_int32_t build_id __attribute__((__unused__)) = rbuf_network_int(rb);
    const int64_t max_header_size = BLOCK_ALLOCATOR_HEADER_RESERVE;
    int64_t min_header_size = serialize_brt_header_min_size(version);

    //Size MUST be in network order regardless of disk order.
    u_int32_t size = rbuf_network_int(rb);
    //If too big, it is corrupt.  We would probably notice during checksum
    //but may have to do a multi-gigabyte malloc+read to find out.
    //If its too small reading rbuf would crash, so verify.
    if (size > max_header_size || size < min_header_size) {
        r = TOKUDB_DICTIONARY_NO_HEADER;
        goto exit;
    }

    lazy_assert(rb->ndone==prefix_size);
    rb->size = size;
    rb->buf = toku_xmalloc(rb->size);

    n = toku_os_pread(fd, rb->buf, rb->size, offset_of_header);
    if (n != rb->size) {
        if (n < 0) {
            r = errno;
            lazy_assert(r!=0);
        } else {
            r = EINVAL; //Header might be useless (wrong size) or could be a disk read error.
        }
        goto exit;
    }
    //It's version 14 or later.  Magic looks OK.
    //We have an rbuf that represents the header.
    //Size is within acceptable bounds.

    //Verify checksum (BRT_LAYOUT_VERSION_13 or later, when checksum function changed)
    u_int32_t calculated_x1764 = x1764_memory(rb->buf, rb->size-4);
    u_int32_t stored_x1764 = toku_dtoh32(*(int*)(rb->buf+rb->size-4));
    if (calculated_x1764 != stored_x1764) {
        r = TOKUDB_DICTIONARY_NO_HEADER; //Header useless
        fprintf(stderr, "Header checksum failure: calc=0x%08x read=0x%08x\n", calculated_x1764, stored_x1764);
        *e = DS_XSUM_FAIL;
        goto exit;
    }

    //Verify byte order
    bytevec tmp_byte_order_check;
    lazy_assert((sizeof toku_byte_order_host) == 8);
    rbuf_literal_bytes(rb, &tmp_byte_order_check, 8); //Must not translate byte order
    int64_t byte_order_stored = *(int64_t*)tmp_byte_order_check;
    if (byte_order_stored != toku_byte_order_host) {
        r = TOKUDB_DICTIONARY_NO_HEADER; //Cannot use dictionary
        goto exit;
    }

    //Load checkpoint count
    *checkpoint_count = rbuf_ulonglong(rb);
    *checkpoint_lsn = rbuf_lsn(rb);
    //Restart at beginning during regular deserialization
    rb->ndone = 0;

exit:
    if (r != 0 && rb->buf != NULL) {
        if (rb->buf != prefix) { // don't free prefix, it's stack alloc'd
            toku_free(rb->buf);
        }
        rb->buf = NULL;
    }
    return r;
}

// Read brtheader from file into struct.  Read both headers and use one.
// We want the latest acceptable header whose checkpoint_lsn is no later
// than max_acceptable_lsn.
enum deserialize_error_code
toku_deserialize_brtheader_from(int fd,
                                LSN max_acceptable_lsn,
                                struct brt_header **brth)
{
    struct rbuf rb_0;
    struct rbuf rb_1;
    u_int64_t checkpoint_count_0;
    u_int64_t checkpoint_count_1;
    LSN checkpoint_lsn_0;
    LSN checkpoint_lsn_1;
    u_int32_t version_0, version_1, version = 0;
    BOOL h0_acceptable = FALSE;
    BOOL h1_acceptable = FALSE;
    struct rbuf *rb = NULL;
    int r0, r1, r;
    enum deserialize_error_code e0, e1, e;

    toku_off_t header_0_off = 0;
    e0 = DS_OK;
    r0 = deserialize_brtheader_from_fd_into_rbuf(fd, header_0_off, &rb_0, &checkpoint_count_0, &checkpoint_lsn_0, &version_0, &e0);
    if (r0 == 0 && checkpoint_lsn_0.lsn <= max_acceptable_lsn.lsn) {
        h0_acceptable = TRUE;
    }

    toku_off_t header_1_off = BLOCK_ALLOCATOR_HEADER_RESERVE;
    e1 = DS_OK;
    r1 = deserialize_brtheader_from_fd_into_rbuf(fd, header_1_off, &rb_1, &checkpoint_count_1, &checkpoint_lsn_1, &version_1, &e1);
    if (r1 == 0 && checkpoint_lsn_1.lsn <= max_acceptable_lsn.lsn) {
        h1_acceptable = TRUE;
    }

    // if either header is too new, the dictionary is unreadable
    if (r0 == TOKUDB_DICTIONARY_TOO_NEW || r1 == TOKUDB_DICTIONARY_TOO_NEW ||
        !(h0_acceptable || h1_acceptable)) {
        // We were unable to read either header or at least one is too
        // new.  Certain errors are higher priority than others. Order of
        // these if/else if is important.
        if (r0 == TOKUDB_DICTIONARY_TOO_NEW || r1 == TOKUDB_DICTIONARY_TOO_NEW) {
            r = TOKUDB_DICTIONARY_TOO_NEW;
        } else if (r0 == TOKUDB_DICTIONARY_TOO_OLD || r1 == TOKUDB_DICTIONARY_TOO_OLD) {
            r = TOKUDB_DICTIONARY_TOO_OLD;
        } else if (r0 == TOKUDB_DICTIONARY_NO_HEADER || r1 == TOKUDB_DICTIONARY_NO_HEADER) {
            r = TOKUDB_DICTIONARY_NO_HEADER;
        } else {
            r = r0 ? r0 : r1; //Arbitrarily report the error from the
                              //first header, unless it's readable
        }

        // it should not be possible for both headers to be later than the max_acceptable_lsn
        invariant(!((r0==0 && checkpoint_lsn_0.lsn > max_acceptable_lsn.lsn) &&
                    (r1==0 && checkpoint_lsn_1.lsn > max_acceptable_lsn.lsn)));
        invariant(r!=0);
        if (e0 == DS_XSUM_FAIL && e1 == DS_XSUM_FAIL) {
            fprintf(stderr, "Both header checksums failed.\n");
            e = DS_XSUM_FAIL;
        } else {
            errno = r;
            e = DS_ERRNO;
        }
        goto exit;
    }

    if (h0_acceptable && h1_acceptable) {
        if (checkpoint_count_0 > checkpoint_count_1) {
            invariant(checkpoint_count_0 == checkpoint_count_1 + 1);
            invariant(version_0 >= version_1);
            rb = &rb_0;
            version = version_0;
        }
        else {
            invariant(checkpoint_count_1 == checkpoint_count_0 + 1);
            invariant(version_1 >= version_0);
            rb = &rb_1;
            version = version_1;
        }
    } else if (h0_acceptable) {
        if (e1 == DS_XSUM_FAIL) {
            // print something reassuring
            fprintf(stderr, "Header 2 checksum failed, but header 1 ok.  Proceeding.\n");
        }
        rb = &rb_0;
        version = version_0;
    } else if (h1_acceptable) {
        if (e0 == DS_XSUM_FAIL) {
            // print something reassuring
            fprintf(stderr, "Header 1 checksum failed, but header 2 ok.  Proceeding.\n");
        }
        rb = &rb_1;
        version = version_1;
    }

    invariant(rb);
    e = deserialize_brtheader_versioned(fd, rb, brth, version);

exit:
    if (rb_0.buf) {
        toku_free(rb_0.buf);
    }
    if (rb_1.buf) {
        toku_free(rb_1.buf);
    }
    return e;
}

unsigned int 
toku_brt_pivot_key_len (struct kv_pair *pk) {
    return kv_pair_keylen(pk);
}

int 
toku_db_badformat(void) {
    return DB_BADFORMAT;
}

static size_t
serialize_rollback_log_size(ROLLBACK_LOG_NODE log) {
    size_t size = node_header_overhead //8 "tokuroll", 4 version, 4 version_original, 4 build_id
                 +8 //TXNID
                 +8 //sequence
                 +8 //blocknum
                 +8 //previous (blocknum)
                 +8 //resident_bytecount
                 +8 //memarena_size_needed_to_load
                 +log->rollentry_resident_bytecount;
    return size;
}

static void
serialize_rollback_log_node_to_buf(ROLLBACK_LOG_NODE log, char *buf, size_t calculated_size, int UU(n_sub_blocks), struct sub_block UU(sub_block[])) {
    struct wbuf wb;
    wbuf_init(&wb, buf, calculated_size);
    {   //Serialize rollback log to local wbuf
        wbuf_nocrc_literal_bytes(&wb, "tokuroll", 8);
        lazy_assert(log->layout_version == BRT_LAYOUT_VERSION);
        wbuf_nocrc_int(&wb, log->layout_version);
        wbuf_nocrc_int(&wb, log->layout_version_original);
        wbuf_nocrc_uint(&wb, BUILD_ID);
        wbuf_nocrc_TXNID(&wb, log->txnid);
        wbuf_nocrc_ulonglong(&wb, log->sequence);
        wbuf_nocrc_BLOCKNUM(&wb, log->blocknum);
        wbuf_nocrc_BLOCKNUM(&wb, log->previous);
        wbuf_nocrc_ulonglong(&wb, log->rollentry_resident_bytecount);
        //Write down memarena size needed to restore
        wbuf_nocrc_ulonglong(&wb, memarena_total_size_in_use(log->rollentry_arena));

        {
            //Store rollback logs
            struct roll_entry *item;
            size_t done_before = wb.ndone;
            for (item = log->newest_logentry; item; item = item->prev) {
                toku_logger_rollback_wbuf_nocrc_write(&wb, item);
            }
            lazy_assert(done_before + log->rollentry_resident_bytecount == wb.ndone);
        }
    }
    lazy_assert(wb.ndone == wb.size);
    lazy_assert(calculated_size==wb.ndone);
}

static int
serialize_uncompressed_block_to_memory(char * uncompressed_buf,
                                       int n_sub_blocks,
                                       struct sub_block sub_block[/*n_sub_blocks*/],
                                       enum toku_compression_method method,
                               /*out*/ size_t *n_bytes_to_write,
                               /*out*/ char  **bytes_to_write) {
    // allocate space for the compressed uncompressed_buf
    size_t compressed_len = get_sum_compressed_size_bound(n_sub_blocks, sub_block, method);
    size_t sub_block_header_len = sub_block_header_size(n_sub_blocks);
    size_t header_len = node_header_overhead + sub_block_header_len + sizeof (uint32_t); // node + sub_block + checksum
    char *XMALLOC_N(header_len + compressed_len, compressed_buf);
    if (compressed_buf == NULL)
        return errno;

    // copy the header
    memcpy(compressed_buf, uncompressed_buf, node_header_overhead);
    if (0) printf("First 4 bytes before compressing data are %02x%02x%02x%02x\n",
                  uncompressed_buf[node_header_overhead],   uncompressed_buf[node_header_overhead+1],
                  uncompressed_buf[node_header_overhead+2], uncompressed_buf[node_header_overhead+3]);

    // compress all of the sub blocks
    char *uncompressed_ptr = uncompressed_buf + node_header_overhead;
    char *compressed_ptr = compressed_buf + header_len;
    compressed_len = compress_all_sub_blocks(n_sub_blocks, sub_block, uncompressed_ptr, compressed_ptr, num_cores, brt_pool, method);

    //if (0) printf("Block %" PRId64 " Size before compressing %u, after compression %"PRIu64"\n", blocknum.b, calculated_size-node_header_overhead, (uint64_t) compressed_len);

    // serialize the sub block header
    uint32_t *ptr = (uint32_t *)(compressed_buf + node_header_overhead);
    *ptr++ = toku_htod32(n_sub_blocks);
    for (int i=0; i<n_sub_blocks; i++) {
        ptr[0] = toku_htod32(sub_block[i].compressed_size);
        ptr[1] = toku_htod32(sub_block[i].uncompressed_size);
        ptr[2] = toku_htod32(sub_block[i].xsum);
        ptr += 3;
    }

    // compute the header checksum and serialize it
    uint32_t header_length = (char *)ptr - (char *)compressed_buf;
    uint32_t xsum = x1764_memory(compressed_buf, header_length);
    *ptr = toku_htod32(xsum);

    *n_bytes_to_write = header_len + compressed_len;
    *bytes_to_write   = compressed_buf;

    return 0;
}



static int
toku_serialize_rollback_log_to_memory (ROLLBACK_LOG_NODE log,
                                       int UU(n_workitems), int UU(n_threads),
                                       enum toku_compression_method method,
                               /*out*/ size_t *n_bytes_to_write,
                               /*out*/ char  **bytes_to_write) {
    // get the size of the serialized node
    size_t calculated_size = serialize_rollback_log_size(log);

    // choose sub block parameters
    int n_sub_blocks = 0, sub_block_size = 0;
    size_t data_size = calculated_size - node_header_overhead;
    choose_sub_block_size(data_size, max_sub_blocks, &sub_block_size, &n_sub_blocks);
    lazy_assert(0 < n_sub_blocks && n_sub_blocks <= max_sub_blocks);
    lazy_assert(sub_block_size > 0);

    // set the initial sub block size for all of the sub blocks
    struct sub_block sub_block[n_sub_blocks];
    for (int i = 0; i < n_sub_blocks; i++) 
        sub_block_init(&sub_block[i]);
    set_all_sub_block_sizes(data_size, sub_block_size, n_sub_blocks, sub_block);

    // allocate space for the serialized node
    char *XMALLOC_N(calculated_size, buf);
    // serialize the node into buf
    serialize_rollback_log_node_to_buf(log, buf, calculated_size, n_sub_blocks, sub_block);

    //Compress and malloc buffer to write
    int result = serialize_uncompressed_block_to_memory(buf, n_sub_blocks, sub_block, method,
                                                        n_bytes_to_write, bytes_to_write);
    toku_free(buf);
    return result;
}

int
toku_serialize_rollback_log_to (int fd, BLOCKNUM blocknum, ROLLBACK_LOG_NODE log,
                                struct brt_header *h, int n_workitems, int n_threads,
                                BOOL for_checkpoint) {
    size_t n_to_write;
    char *compressed_buf;
    {
        int r = toku_serialize_rollback_log_to_memory(log, n_workitems, n_threads, h->compression_method, &n_to_write, &compressed_buf);
	if (r!=0) return r;
    }

    {
	lazy_assert(blocknum.b>=0);
	DISKOFF offset;
        toku_blocknum_realloc_on_disk(h->blocktable, blocknum, n_to_write, &offset,
                                      h, for_checkpoint); //dirties h
	lock_for_pwrite();
	toku_full_pwrite_extend(fd, compressed_buf, n_to_write, offset);
	unlock_for_pwrite();
    }
    toku_free(compressed_buf);
    log->dirty = 0;  // See #1957.   Must set the node to be clean after serializing it so that it doesn't get written again on the next checkpoint or eviction.
    return 0;
}

static int
deserialize_rollback_log_from_rbuf (BLOCKNUM blocknum, u_int32_t fullhash, ROLLBACK_LOG_NODE *log_p,
                                    struct brt_header *h, struct rbuf *rb) {
    ROLLBACK_LOG_NODE MALLOC(result);
    int r;
    if (result==NULL) {
	r=errno;
	if (0) { died0: toku_free(result); }
	return r;
    }

    //printf("Deserializing %lld datasize=%d\n", off, datasize);
    bytevec magic;
    rbuf_literal_bytes(rb, &magic, 8);
    lazy_assert(!memcmp(magic, "tokuroll", 8));

    result->layout_version    = rbuf_int(rb);
    lazy_assert(result->layout_version == BRT_LAYOUT_VERSION);
    result->layout_version_original = rbuf_int(rb);
    result->layout_version_read_from_disk = result->layout_version;
    result->build_id = rbuf_int(rb);
    result->dirty = FALSE;
    //TODO: Maybe add descriptor (or just descriptor version) here eventually?
    //TODO: This is hard.. everything is shared in a single dictionary.
    rbuf_TXNID(rb, &result->txnid);
    result->sequence = rbuf_ulonglong(rb);
    result->blocknum = rbuf_blocknum(rb);
    if (result->blocknum.b != blocknum.b) {
        r = toku_db_badformat();
        goto died0;
    }
    result->hash    = toku_cachetable_hash(h->cf, result->blocknum);
    if (result->hash != fullhash) {
        r = toku_db_badformat();
        goto died0;
    }
    result->previous       = rbuf_blocknum(rb);
    result->previous_hash  = toku_cachetable_hash(h->cf, result->previous);
    result->rollentry_resident_bytecount = rbuf_ulonglong(rb);

    size_t arena_initial_size = rbuf_ulonglong(rb);
    result->rollentry_arena = memarena_create_presized(arena_initial_size);
    if (0) { died1: memarena_close(&result->rollentry_arena); goto died0; }

    //Load rollback entries
    lazy_assert(rb->size > 4);
    //Start with empty list
    result->oldest_logentry = result->newest_logentry = NULL;
    while (rb->ndone < rb->size) {
        struct roll_entry *item;
        uint32_t rollback_fsize = rbuf_int(rb); //Already read 4.  Rest is 4 smaller
        bytevec item_vec;
        rbuf_literal_bytes(rb, &item_vec, rollback_fsize-4);
        unsigned char* item_buf = (unsigned char*)item_vec;
        r = toku_parse_rollback(item_buf, rollback_fsize-4, &item, result->rollentry_arena);
        if (r!=0) {
            r = toku_db_badformat();
            goto died1;
        }
        //Add to head of list
        if (result->oldest_logentry) {
            result->oldest_logentry->prev = item;
            result->oldest_logentry       = item;
            item->prev = NULL;
        }
        else {
            result->oldest_logentry = result->newest_logentry = item;
            item->prev = NULL;
        }
    }

    toku_free(rb->buf);
    rb->buf = NULL;
    *log_p = result;
    return 0;
}

static int
deserialize_rollback_log_from_rbuf_versioned (u_int32_t version, BLOCKNUM blocknum, u_int32_t fullhash,
                                              ROLLBACK_LOG_NODE *log,
                                              struct brt_header *h, struct rbuf *rb) {
    int r = 0;
    ROLLBACK_LOG_NODE rollback_log_node = NULL;
    invariant(version==BRT_LAYOUT_VERSION); //Rollback log nodes do not survive version changes.
    r = deserialize_rollback_log_from_rbuf(blocknum, fullhash, &rollback_log_node, h, rb);
    if (r==0) {
        *log = rollback_log_node;
    }
    return r;
}

static int
decompress_from_raw_block_into_rbuf(u_int8_t *raw_block, size_t raw_block_size, struct rbuf *rb, BLOCKNUM blocknum) {
    toku_trace("decompress");
    // get the number of compressed sub blocks
    int n_sub_blocks;
    n_sub_blocks = toku_dtoh32(*(u_int32_t*)(&raw_block[node_header_overhead]));

    // verify the number of sub blocks
    invariant(0 <= n_sub_blocks && n_sub_blocks <= max_sub_blocks);

    { // verify the header checksum
        u_int32_t header_length = node_header_overhead + sub_block_header_size(n_sub_blocks);
        invariant(header_length <= raw_block_size);
        u_int32_t xsum = x1764_memory(raw_block, header_length);
        u_int32_t stored_xsum = toku_dtoh32(*(u_int32_t *)(raw_block + header_length));
        invariant(xsum == stored_xsum);
    }
    int r;

    // deserialize the sub block header
    struct sub_block sub_block[n_sub_blocks];
    u_int32_t *sub_block_header = (u_int32_t *) &raw_block[node_header_overhead+4];
    for (int i = 0; i < n_sub_blocks; i++) {
        sub_block_init(&sub_block[i]);
        sub_block[i].compressed_size = toku_dtoh32(sub_block_header[0]);
        sub_block[i].uncompressed_size = toku_dtoh32(sub_block_header[1]);
        sub_block[i].xsum = toku_dtoh32(sub_block_header[2]);
        sub_block_header += 3;
    }

    // verify sub block sizes
    for (int i = 0; i < n_sub_blocks; i++) {
        u_int32_t compressed_size = sub_block[i].compressed_size;
        if (compressed_size<=0   || compressed_size>(1<<30)) { r = toku_db_badformat(); return r; }

        u_int32_t uncompressed_size = sub_block[i].uncompressed_size;
        if (0) printf("Block %" PRId64 " Compressed size = %u, uncompressed size=%u\n", blocknum.b, compressed_size, uncompressed_size);
        if (uncompressed_size<=0 || uncompressed_size>(1<<30)) { r = toku_db_badformat(); return r; }
    }

    // sum up the uncompressed size of the sub blocks
    size_t uncompressed_size = get_sum_uncompressed_size(n_sub_blocks, sub_block);

    // allocate the uncompressed buffer
    size_t size = node_header_overhead + uncompressed_size;
    unsigned char *buf = toku_xmalloc(size);
    lazy_assert(buf);
    rbuf_init(rb, buf, size);

    // copy the uncompressed node header to the uncompressed buffer
    memcpy(rb->buf, raw_block, node_header_overhead);

    // point at the start of the compressed data (past the node header, the sub block header, and the header checksum)
    unsigned char *compressed_data = raw_block + node_header_overhead + sub_block_header_size(n_sub_blocks) + sizeof (u_int32_t);

    // point at the start of the uncompressed data
    unsigned char *uncompressed_data = rb->buf + node_header_overhead;    

    // decompress all the compressed sub blocks into the uncompressed buffer
    r = decompress_all_sub_blocks(n_sub_blocks, sub_block, compressed_data, uncompressed_data, num_cores, brt_pool);
    if (r != 0) {
        fprintf(stderr, "%s:%d block %"PRId64" failed %d at %p size %lu\n", __FUNCTION__, __LINE__, blocknum.b, r, raw_block, raw_block_size);
        dump_bad_block(raw_block, raw_block_size);
    }
    lazy_assert_zero(r);

    toku_trace("decompress done");

    rb->ndone=0;

    return 0;
}

static int
decompress_from_raw_block_into_rbuf_versioned(u_int32_t version, u_int8_t *raw_block, size_t raw_block_size, struct rbuf *rb, BLOCKNUM blocknum) {
    // This function exists solely to accomodate future changes in compression.
    int r;

    switch (version) {
        case BRT_LAYOUT_VERSION_13:
        case BRT_LAYOUT_VERSION_14:
        case BRT_LAYOUT_VERSION:
            r = decompress_from_raw_block_into_rbuf(raw_block, raw_block_size, rb, blocknum);
            break;
        default:
            lazy_assert(FALSE);
    }
    return r;
}

static int
read_and_decompress_block_from_fd_into_rbuf(int fd, BLOCKNUM blocknum,
                                            struct brt_header *h,
                                            struct rbuf *rb,
                                  /* out */ int *layout_version_p) {
    int r;
    if (0) printf("Deserializing Block %" PRId64 "\n", blocknum.b);
    if (h->panic) return h->panic;

    toku_trace("deserial start nopanic");

    // get the file offset and block size for the block
    DISKOFF offset, size;
    toku_translate_blocknum_to_offset_size(h->blocktable, blocknum, &offset, &size);
    u_int8_t *XMALLOC_N(size, raw_block);
    {
        // read the (partially compressed) block
        ssize_t rlen = toku_os_pread(fd, raw_block, size, offset);
        lazy_assert((DISKOFF)rlen == size);
    }
    // get the layout_version
    int layout_version;
    {
        u_int8_t *magic = raw_block + uncompressed_magic_offset;
        if (memcmp(magic, "tokuleaf", 8)!=0 &&
            memcmp(magic, "tokunode", 8)!=0 &&
            memcmp(magic, "tokuroll", 8)!=0) {
            r = toku_db_badformat();
            goto cleanup;
        }
        u_int8_t *version = raw_block + uncompressed_version_offset;
        layout_version = toku_dtoh32(*(uint32_t*)version);
        if (layout_version < BRT_LAYOUT_MIN_SUPPORTED_VERSION || layout_version > BRT_LAYOUT_VERSION) {
            r = toku_db_badformat();
            goto cleanup;
        }
    }

    r = decompress_from_raw_block_into_rbuf_versioned(layout_version, raw_block, size, rb, blocknum);
    if (r!=0) goto cleanup;

    *layout_version_p = layout_version;
cleanup:
    if (r!=0) {
        if (rb->buf) toku_free(rb->buf);
        rb->buf = NULL;
    }
    if (raw_block) toku_free(raw_block);
    return r;
}

// Read rollback log node from file into struct.  Perform version upgrade if necessary.
int
toku_deserialize_rollback_log_from (int fd, BLOCKNUM blocknum, u_int32_t fullhash,
                                    ROLLBACK_LOG_NODE *logp, struct brt_header *h) {
    toku_trace("deserial start");

    int r;
    struct rbuf rb = {.buf = NULL, .size = 0, .ndone = 0};

    int layout_version = 0;
    r = read_and_decompress_block_from_fd_into_rbuf(fd, blocknum, h, &rb, &layout_version);
    if (r!=0) goto cleanup;

    {
        u_int8_t *magic = rb.buf + uncompressed_magic_offset;
        if (memcmp(magic, "tokuroll", 8)!=0) {
            r = toku_db_badformat();
            goto cleanup;
        }
    }

    r = deserialize_rollback_log_from_rbuf_versioned(layout_version, blocknum, fullhash, logp, h, &rb);

    toku_trace("deserial done");

cleanup:
    if (rb.buf) toku_free(rb.buf);
    return r;
}


#undef UPGRADE_STATUS_VALUE
