/* -*- mode: C; c-basic-offset: 4 -*- */
#ifndef BLOCKTABLE_H
#define BLOCKTABLE_H
#ident "$Id$"
#ident "Copyright (c) 2007-2010 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#if defined(__cplusplus) || defined(__cilkplusplus)
extern "C" {
#endif

typedef struct block_table *BLOCK_TABLE;

//Needed by tests, brtdump
struct block_translation_pair {
    union { // If in the freelist, use next_free_blocknum, otherwise diskoff.
        DISKOFF  diskoff; 
        BLOCKNUM next_free_blocknum;
    } u;
    DISKOFF size;    // set to 0xFFFFFFFFFFFFFFFF for free
};

void toku_blocktable_create_new(BLOCK_TABLE *btp);
void toku_blocktable_create_from_buffer(BLOCK_TABLE *btp, DISKOFF location_on_disk, DISKOFF size_on_disk, unsigned char *translation_buffer);
void toku_blocktable_destroy(BLOCK_TABLE *btp);

void toku_brtheader_lock(struct brt_header *h);
void toku_brtheader_unlock(struct brt_header *h);

void toku_block_translation_note_start_checkpoint_unlocked(BLOCK_TABLE bt);
void toku_block_translation_note_end_checkpoint(BLOCK_TABLE bt, int fd, struct brt_header *h);
void toku_block_translation_note_failed_checkpoint(BLOCK_TABLE bt);
void toku_block_translation_note_skipped_checkpoint(BLOCK_TABLE bt);
void toku_block_translation_truncate_unlocked(BLOCK_TABLE bt, int fd, struct brt_header *h);
void toku_maybe_truncate_cachefile_on_open(BLOCK_TABLE bt, int fd, struct brt_header *h);

//Blocknums
void toku_allocate_blocknum(BLOCK_TABLE bt, BLOCKNUM *res, struct brt_header * h);
void toku_allocate_blocknum_unlocked(BLOCK_TABLE bt, BLOCKNUM *res, struct brt_header * h);
void toku_free_blocknum(BLOCK_TABLE bt, BLOCKNUM *b, struct brt_header * h, BOOL for_checkpoint);
void toku_verify_blocknum_allocated(BLOCK_TABLE bt, BLOCKNUM b);
void toku_block_verify_no_data_blocks_except_root_unlocked(BLOCK_TABLE bt, BLOCKNUM root);
void toku_block_verify_no_free_blocknums(BLOCK_TABLE bt);
void toku_realloc_descriptor_on_disk(BLOCK_TABLE bt, DISKOFF size, DISKOFF *offset, struct brt_header * h);
void toku_realloc_descriptor_on_disk_unlocked(BLOCK_TABLE bt, DISKOFF size, DISKOFF *offset, struct brt_header * h);
void toku_get_descriptor_offset_size(BLOCK_TABLE bt, DISKOFF *offset, DISKOFF *size);

//Blocks and Blocknums
void toku_blocknum_realloc_on_disk(BLOCK_TABLE bt, BLOCKNUM b, DISKOFF size, DISKOFF *offset, struct brt_header * h, BOOL for_checkpoint);
void toku_translate_blocknum_to_offset_size(BLOCK_TABLE bt, BLOCKNUM b, DISKOFF *offset, DISKOFF *size);

//Serialization
void toku_serialize_translation_to_wbuf_unlocked(BLOCK_TABLE bt, struct wbuf *w, int64_t *address, int64_t *size);

void toku_block_table_swap_for_redirect(BLOCK_TABLE old_bt, BLOCK_TABLE new_bt);


//DEBUG ONLY (brtdump included), tests included
void toku_blocknum_dump_translation(BLOCK_TABLE bt, BLOCKNUM b);
void toku_dump_translation_table(FILE *f, BLOCK_TABLE bt);
void toku_block_alloc(BLOCK_TABLE bt, u_int64_t size, u_int64_t *offset);
void toku_block_free(BLOCK_TABLE bt, u_int64_t offset);
typedef int(*BLOCKTABLE_CALLBACK)(BLOCKNUM b, int64_t size, int64_t address, void *extra);
enum translation_type {TRANSLATION_NONE=0,
                       TRANSLATION_CURRENT,
                       TRANSLATION_INPROGRESS,
                       TRANSLATION_CHECKPOINTED,
                       TRANSLATION_DEBUG};

int toku_blocktable_iterate(BLOCK_TABLE bt, enum translation_type type, BLOCKTABLE_CALLBACK f, void *extra, BOOL data_only, BOOL used_only); 
void toku_blocktable_internal_fragmentation(BLOCK_TABLE bt, int64_t *total_sizep, int64_t *used_sizep);

void toku_block_table_get_fragmentation_unlocked(BLOCK_TABLE bt, TOKU_DB_FRAGMENTATION report);
//Requires:  blocktable lock is held.
//Requires:  report->file_size_bytes is already filled in.

int64_t toku_block_get_blocks_in_use_unlocked(BLOCK_TABLE bt);

//Unmovable reserved first, then reallocable.
// We reserve one blocknum for the translation table itself.
enum {RESERVED_BLOCKNUM_NULL       =0,
      RESERVED_BLOCKNUM_TRANSLATION=1,
      RESERVED_BLOCKNUM_DESCRIPTOR =2,
      RESERVED_BLOCKNUMS};

#if defined(__cplusplus) || defined(__cilkplusplus)
};
#endif

#endif

