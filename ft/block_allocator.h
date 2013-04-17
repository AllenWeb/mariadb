/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ifndef BLOCK_ALLOCATOR_H
#define  BLOCK_ALLOCATOR_H

#ident "$Id$"
#ident "Copyright (c) 2007-2010 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include "fttypes.h"

#if defined(__cplusplus) || defined(__cilkplusplus)
extern "C" {
#endif

#define BLOCK_ALLOCATOR_ALIGNMENT 4096
// How much must be reserved at the beginning for the block?
//  The actual header is 8+4+4+8+8_4+8+ the length of the db names + 1 pointer for each root.
//  So 4096 should be enough.
#define BLOCK_ALLOCATOR_HEADER_RESERVE 4096
#if (BLOCK_ALLOCATOR_HEADER_RESERVE % BLOCK_ALLOCATOR_ALIGNMENT) != 0
#error
#endif

// Block allocator.
// Overview: A block allocator manages the allocation of variable-sized blocks.
// The translation of block numbers to addresses is handled elsewhere.
// The allocation of block numbers is handled elsewhere.

// We can create a block allocator.
// When creating a block allocator we also specify a certain-sized
// block at the beginning that is preallocated (and cannot be allocated
// or freed)

// We can allocate blocks of a particular size at a particular location.
// We can allocate blocks of a particular size at a location chosen by the allocator.
// We can free blocks.
// We can determine the size of a block.


#define BLOCK_ALLOCATOR_TOTAL_HEADER_RESERVE (2*BLOCK_ALLOCATOR_HEADER_RESERVE)

typedef struct block_allocator *BLOCK_ALLOCATOR;

void create_block_allocator (BLOCK_ALLOCATOR * ba, u_int64_t reserve_at_beginning, u_int64_t alignment);
// Effect: Create a block allocator, in which the first RESERVE_AT_BEGINNING bytes are not put into a block.
//  All blocks be start on a multiple of ALIGNMENT.
//  Aborts if we run out of memory.
// Parameters
//  ba (OUT):                        Result stored here.
//  reserve_at_beginning (IN)        Size of reserved block at beginning.  This size does not have to be aligned.
//  alignment (IN)                   Block alignment.

void destroy_block_allocator (BLOCK_ALLOCATOR *ba);
// Effect: Destroy a block allocator at *ba.
//  Also, set *ba=NULL.
// Rationale:  If there was only one copy of the pointer, this kills that copy too.
// Paramaters:
//  ba (IN/OUT):


void block_allocator_alloc_block_at (BLOCK_ALLOCATOR ba, u_int64_t size, u_int64_t offset);
// Effect: Allocate a block of the specified size at a particular offset.
//  Aborts if anything goes wrong.
//  The performance of this function may be as bad as Theta(N), where N is the number of blocks currently in use.
// Usage note: To allocate several blocks (e.g., when opening a BRT),  use block_allocator_alloc_blocks_at().
// Requires: The resulting block may not overlap any other allocated block.
//  And the offset must be a multiple of the block alignment.
// Parameters:
//  ba (IN/OUT): The block allocator.  (Modifies ba.)
//  size (IN):   The size of the block.
//  offset (IN): The location of the block.


struct block_allocator_blockpair {
    u_int64_t offset;
    u_int64_t size;
};
void block_allocator_alloc_blocks_at (BLOCK_ALLOCATOR ba, u_int64_t n_blocks, struct block_allocator_blockpair *pairs);
// Effect: Take pairs in any order, and add them all, as if we did block_allocator_alloc_block() on each pair.
//  This should run in time O(N + M log M) where N is the number of blocks in ba, and M is the number of new blocks.
// Modifies: pairs (sorts them).

void block_allocator_alloc_block (BLOCK_ALLOCATOR ba, u_int64_t size, u_int64_t *offset);
// Effect: Allocate a block of the specified size at an address chosen by the allocator.
//  Aborts if anything goes wrong.
//  The block address will be a multiple of the alignment.
// Parameters:
//  ba (IN/OUT):  The block allocator.   (Modifies ba.)
//  size (IN):    The size of the block.  (The size does not have to be aligned.)
//  offset (OUT): The location of the block.

void block_allocator_free_block (BLOCK_ALLOCATOR ba, u_int64_t offset);
// Effect: Free the block at offset.
// Requires: There must be a block currently allocated at that offset.
// Parameters:
//  ba (IN/OUT): The block allocator.  (Modifies ba.)
//  offset (IN): The offset of the block.


u_int64_t block_allocator_block_size (BLOCK_ALLOCATOR ba, u_int64_t offset);
// Effect: Return the size of the block that starts at offset.
// Requires: There must be a block currently allocated at that offset.
// Parameters:
//  ba (IN/OUT): The block allocator.  (Modifies ba.)
//  offset (IN): The offset of the block.

void block_allocator_validate (BLOCK_ALLOCATOR ba);
// Effect: Check to see if the block allocator is OK.  This may take a long time.
// Usage Hints: Probably only use this for unit tests.

void block_allocator_print (BLOCK_ALLOCATOR ba);
// Effect: Print information about the block allocator.
// Rationale: This is probably useful only for debugging.

u_int64_t block_allocator_allocated_limit (BLOCK_ALLOCATOR ba);
// Effect: Return the unallocated block address of "infinite" size.
//  That is, return the smallest address that is above all the allocated blocks.
// Rationale: When writing the root FIFO we don't know how big the block is.
//  So we start at the "infinite" block, write the fifo, and then
//  allocate_block_at of the correct size and offset to account for the root FIFO.

int block_allocator_get_nth_block_in_layout_order (BLOCK_ALLOCATOR ba, u_int64_t b, u_int64_t *offset, u_int64_t *size);
// Effect: Consider the blocks in sorted order.  The reserved block at the beginning is number 0.  The next one is number 1 and so forth.
//  Return the offset and size of the block with that number.
//  Return 0 if there is a block that big, return nonzero if b is too big.
// Rationale: This is probably useful only for tests.

void block_allocator_get_unused_statistics(BLOCK_ALLOCATOR ba, TOKU_DB_FRAGMENTATION report);
// Effect:  Fill in report to indicate how the file is used.
// Requires: 
//  report->file_size_bytes is filled in
//  report->data_bytes is filled in
//  report->checkpoint_bytes_additional is filled in

void block_allocator_merge_blockpairs_into (u_int64_t d,       struct block_allocator_blockpair dst[/*d*/],
				       u_int64_t s, const struct block_allocator_blockpair src[/*s*/]);
// Effect: Merge dst[d] and src[s] into dst[d+s], merging in place.
//   Initially dst and src hold sorted arrays (sorted by increasing offset).
//   Finally dst contains all d+s elements sorted in order.
// Requires: 
//   dst and src are sorted.
//   dst must be large enough.
//   No blocks may overlap.
// Rationale: This is exposed so it can be tested by a glass box tester.  Otherwise it would be static (file-scope) function inside block_allocator.c

#if defined(__cplusplus) || defined(__cilkplusplus)
};
#endif

#endif
