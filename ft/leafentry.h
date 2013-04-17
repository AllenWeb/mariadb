/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ifndef LEAFENTRY_H
#define LEAFENTRY_H
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."


/*
 * Header file for Leafentries
 */

#include <toku_portability.h>
#include "rbuf.h"
#include "x1764.h"
#include "mempool.h"


#if 0
    Memory format of packed nodup leaf entry
    CONSTANTS:
        num_uxrs
        keylen
    Run-time-constants
        voffset of val/vallen??? (for le_any_val) This must be small if it is interpreted as voffset = realoffset_of_val - keylen
            GOOD performance optimization.
            ALSO good for simplicity (no having to scan packed version)
        key[]
    variable length
        
        
    Memory format of packed dup leaf entry
    CONSTANTS:
        num_uxrs
        keylen
        vallen
    Run-time-constants
        key[]
        val[]
#endif
#if TOKU_WINDOWS
#pragma pack(push, 1)
#endif

//
// enum of possible values for LEAFENTRY->type field
// LE_CLEAN means that there is a single committed value in a format that saves disk space
// LE_MVCC means that there may be multiple committed values or there are provisional values
//
enum { LE_CLEAN = 0, LE_MVCC = 1 };

// This is an on-disk format.  static_asserts verify everything is packed and aligned correctly.
struct __attribute__ ((__packed__)) leafentry {
    struct leafentry_clean {
        uint32_t vallen;
        uint8_t  key_val[0];     //Actual key, then actual val
    }; // For the case where LEAFENTRY->type is LE_CLEAN
    static_assert(4 == sizeof(leafentry::leafentry_clean), "leafentry_clean size is wrong");
    static_assert(4 == __builtin_offsetof(leafentry::leafentry_clean, key_val), "key_val is in the wrong place");
    struct __attribute__ ((__packed__)) leafentry_mvcc {
        uint32_t num_cxrs; // number of committed transaction records
        uint8_t  num_pxrs; // number of provisional transaction records
        uint8_t key_xrs[0]; //Actual key,
                             //then TXNIDs of XRs relevant for reads:
                             //  if provisional XRs exist, store OUTERMOST TXNID
                             //  store committed TXNIDs, from most recently committed to least recently committed (newest first)
                             //then lengths of XRs relevant for reads (length is at most 1<<31, MSB is 1 for insert, 0 for delete):
                             //  if provisional XRs exist (num_pxrs>0), store length and insert/delete flag associated with INNERMOST TXNID
                             //  store length and insert/delete flag associated with each committed TXNID, in same order as above (newest first)
                             //then data of XRs relevant for reads
                             //  if provisional XRs exist (num_pxrs>0), store data associated with INNERMOST provisional TXNID
                             //  store data associated with committed TXNIDs (all committed data, newest committed values first)
                             //if provisional XRs still exist (that is, num_puxrs > 1, so INNERMOST provisional TXNID != OUTERMOST provisional TXNID):
                             //  for OUTERMOST provisional XR:
                             //    1 byte: store type (insert/delete/placeholder)
                             //    4 bytes: length (if type is INSERT, no length stored if placeholder or delete)
                             //    data
                             //  for rest of provisional stack (if num_pxrs > 2), from second-outermost to second-innermost (outermost is stored above, innermost is stored separately):
                             //   8 bytes: TXNID
                             //   1 byte: store type (insert/delete/placeholder)
                             //   4 bytes: length (if type is INSERT)
                             //   data
                             //  for INNERMOST provisional XR:
                             //   8 bytes: TXNID
                             //   (innermost data and length with insert/delete flag are stored above, cannot be a placeholder)
    }; // For the case where LEAFENTRY->type is LE_MVCC
    static_assert(5 == sizeof(leafentry::leafentry_mvcc), "leafentry_mvcc size is wrong");
    static_assert(5 == __builtin_offsetof(leafentry::leafentry_mvcc, key_xrs), "key_xrs is in the wrong place");

    uint8_t  type;    // type is LE_CLEAN or LE_MVCC
    uint32_t keylen;
    union __attribute__ ((__packed__)) {
        struct leafentry_clean clean;
        struct leafentry_mvcc mvcc;
    } u;
};
static_assert(10 == sizeof(leafentry), "leafentry size is wrong");
static_assert(5 == __builtin_offsetof(leafentry, u), "union is in the wrong place");
#if TOKU_WINDOWS
#pragma pack(pop)
#endif

#define LE_CLEAN_MEMSIZE(_keylen, _vallen)                       \
    (sizeof(((LEAFENTRY)NULL)->type)            /* type */       \
    +sizeof(((LEAFENTRY)NULL)->keylen)          /* keylen */     \
    +sizeof(((LEAFENTRY)NULL)->u.clean.vallen)  /* vallen */     \
    +(_keylen)                                    /* actual key */     \
    +(_vallen))                                   /* actual val */

#define LE_MVCC_COMMITTED_HEADER_MEMSIZE                          \
    (sizeof(((LEAFENTRY)NULL)->type)            /* type */        \
    +sizeof(((LEAFENTRY)NULL)->keylen)          /* keylen */      \
    +sizeof(((LEAFENTRY)NULL)->u.mvcc.num_cxrs) /* committed */   \
    +sizeof(((LEAFENTRY)NULL)->u.mvcc.num_pxrs) /* provisional */ \
    +sizeof(TXNID)                              /* transaction */ \
    +sizeof(uint32_t)                           /* length+bit */  \
    +sizeof(uint32_t))                          /* length+bit */ 

#define LE_MVCC_COMMITTED_MEMSIZE(_keylen, _vallen)    \
    (LE_MVCC_COMMITTED_HEADER_MEMSIZE                \
    +(_keylen)                        /* actual key */ \
    +(_vallen))                       /* actual val */


typedef struct leafentry *LEAFENTRY;
typedef struct leafentry_13 *LEAFENTRY_13;

size_t leafentry_memsize (LEAFENTRY le); // the size of a leafentry in memory.
size_t leafentry_disksize (LEAFENTRY le); // this is the same as logsizeof_LEAFENTRY.  The size of a leafentry on disk.
void wbuf_LEAFENTRY(struct wbuf *w, LEAFENTRY le);
void wbuf_nocrc_LEAFENTRY(struct wbuf *w, LEAFENTRY le);
int print_leafentry (FILE *outf, LEAFENTRY v); // Print a leafentry out in human-readable form.

int le_latest_is_del(LEAFENTRY le); // Return true if it is a provisional delete.
bool le_is_clean(LEAFENTRY le); //Return how many xids exist (0 does not count)
bool le_has_xids(LEAFENTRY le, XIDS xids); // Return true transaction represented by xids is still provisional in this leafentry (le's xid stack is a superset or equal to xids)
uint32_t le_latest_keylen (LEAFENTRY le); // Return the latest keylen.
void*     le_latest_val (LEAFENTRY le); // Return the latest val (return NULL for provisional deletes)
uint32_t le_latest_vallen (LEAFENTRY le); // Return the latest vallen.  Returns 0 for provisional deletes.
void* le_latest_val_and_len (LEAFENTRY le, uint32_t *len);

 // Return any key or value (even if it's only provisional).
void* le_key (LEAFENTRY le);
uint32_t le_keylen (LEAFENTRY le);
void* le_key_and_len (LEAFENTRY le, uint32_t *len);

uint64_t le_outermost_uncommitted_xid (LEAFENTRY le);

void
le_committed_mvcc(uint8_t *key, uint32_t keylen,
                  uint8_t *val, uint32_t vallen,
                  TXNID xid,
                  void (*bytes)(struct dbuf *dbuf, const void *bytes, int nbytes),
                  struct dbuf *d);
void
le_clean(uint8_t *key, uint32_t keylen,
         uint8_t *val, uint32_t vallen,
         void (*bytes)(struct dbuf *dbuf, const void *bytes, int nbytes),
         struct dbuf *d);


//Callback contract:
//      Function checks to see if id is accepted by context.
//  Returns:
//      0:  context ignores this entry, id.
//      TOKUDB_ACCEPT: context accepts id
//      r|r!=0&&r!=TOKUDB_ACCEPT:  Quit early, return r, because something unexpected went wrong (error case)
typedef int(*LE_ITERATE_CALLBACK)(TXNID id, TOKUTXN context);

int le_iterate_is_del(LEAFENTRY le, LE_ITERATE_CALLBACK f, bool *is_empty, TOKUTXN context);

int le_iterate_val(LEAFENTRY le, LE_ITERATE_CALLBACK f, void** valpp, uint32_t *vallenp, TOKUTXN context);


size_t
leafentry_disksize_13(LEAFENTRY_13 le);
int 
toku_le_upgrade_13_14(LEAFENTRY_13 old_leafentry, // NULL if there was no stored data.
		      size_t *new_leafentry_memorysize, 
		      LEAFENTRY *new_leafentry_p,
		      OMT omt,
		      struct mempool *mp);



#endif

