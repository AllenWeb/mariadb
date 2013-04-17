/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ifndef WBUF_H
#define WBUF_H
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include <memory.h>
#include <string.h>

#include <portability/toku_htonl.h>

#include "fttypes.h"
#include "x1764.h"

#define CRC_INCR

/* When serializing a value, write it into a buffer. */
/* This code requires that the buffer be big enough to hold whatever you put into it. */
/* This abstraction doesn't do a good job of hiding its internals.
 * Why?  The performance of this code is important, and we want to inline stuff */
//Why is size here an int instead of DISKOFF like in the initializer?
struct wbuf {
    unsigned char *buf;
    unsigned int  size;
    unsigned int  ndone;
    struct x1764  checksum;    // The checksum state
};

static inline void wbuf_nocrc_init (struct wbuf *w, void *buf, DISKOFF size) {
    w->buf = (unsigned char *) buf;
    w->size = size;
    w->ndone = 0;
}

static inline void wbuf_init (struct wbuf *w, void *buf, DISKOFF size) {
    wbuf_nocrc_init(w, buf, size);
    x1764_init(&w->checksum);
}

static inline size_t wbuf_get_woffset(struct wbuf *w) {
    return w->ndone;
}

/* Write a character. */
static inline void wbuf_nocrc_char (struct wbuf *w, unsigned char ch) {
    assert(w->ndone<w->size);
    w->buf[w->ndone++]=ch;
}

/* Write a character. */
static inline void wbuf_nocrc_uint8_t (struct wbuf *w, uint8_t ch) {
    assert(w->ndone<w->size);
    w->buf[w->ndone++]=ch;
}

static inline void wbuf_char (struct wbuf *w, unsigned char ch) {
    wbuf_nocrc_char (w, ch);
    x1764_add(&w->checksum, &w->buf[w->ndone-1], 1);
}

//Write an int that MUST be in network order regardless of disk order
static void wbuf_network_int (struct wbuf *w, int32_t i) __attribute__((__unused__));
static void wbuf_network_int (struct wbuf *w, int32_t i) {
    assert(w->ndone + 4 <= w->size);
    *(uint32_t*)(&w->buf[w->ndone]) = toku_htonl(i);
    x1764_add(&w->checksum, &w->buf[w->ndone], 4);
    w->ndone += 4;
}

static inline void wbuf_nocrc_int (struct wbuf *w, int32_t i) {
#if 0
    wbuf_nocrc_char(w, i>>24);
    wbuf_nocrc_char(w, i>>16);
    wbuf_nocrc_char(w, i>>8);
    wbuf_nocrc_char(w, i>>0);
#else
    assert(w->ndone + 4 <= w->size);
 #if 0
    w->buf[w->ndone+0] = i>>24;
    w->buf[w->ndone+1] = i>>16;
    w->buf[w->ndone+2] = i>>8;
    w->buf[w->ndone+3] = i>>0;
 #else
    *(uint32_t*)(&w->buf[w->ndone]) = toku_htod32(i);
 #endif
    w->ndone += 4;
#endif
}

static inline void wbuf_int (struct wbuf *w, int32_t i) {
    wbuf_nocrc_int(w, i);
    x1764_add(&w->checksum, &w->buf[w->ndone-4], 4);
}

static inline void wbuf_nocrc_uint (struct wbuf *w, uint32_t i) {
    wbuf_nocrc_int(w, (int32_t)i);
}

static inline void wbuf_uint (struct wbuf *w, uint32_t i) {
    wbuf_int(w, (int32_t)i);
}

static inline void wbuf_nocrc_literal_bytes(struct wbuf *w, bytevec bytes_bv, uint32_t nbytes) {
    const unsigned char *bytes = (const unsigned char *) bytes_bv;
#if 0
    { int i; for (i=0; i<nbytes; i++) wbuf_nocrc_char(w, bytes[i]); }
#else
    assert(w->ndone + nbytes <= w->size);
    memcpy(w->buf + w->ndone, bytes, (size_t)nbytes);
    w->ndone += nbytes;
#endif
}

static inline void wbuf_literal_bytes(struct wbuf *w, bytevec bytes_bv, uint32_t nbytes) {
    wbuf_nocrc_literal_bytes(w, bytes_bv, nbytes);
    x1764_add(&w->checksum, &w->buf[w->ndone-nbytes], nbytes);
}

static void wbuf_nocrc_bytes (struct wbuf *w, bytevec bytes_bv, uint32_t nbytes) {
    wbuf_nocrc_uint(w, nbytes);
    wbuf_nocrc_literal_bytes(w, bytes_bv, nbytes);
}

static void wbuf_bytes (struct wbuf *w, bytevec bytes_bv, uint32_t nbytes) {
    wbuf_uint(w, nbytes);
    wbuf_literal_bytes(w, bytes_bv, nbytes);
}

static void wbuf_nocrc_ulonglong (struct wbuf *w, uint64_t ull) {
    wbuf_nocrc_uint(w, (uint32_t)(ull>>32));
    wbuf_nocrc_uint(w, (uint32_t)(ull&0xFFFFFFFF));
}

static void wbuf_ulonglong (struct wbuf *w, uint64_t ull) {
    wbuf_uint(w, (uint32_t)(ull>>32));
    wbuf_uint(w, (uint32_t)(ull&0xFFFFFFFF));
}

static inline void wbuf_nocrc_uint64_t(struct wbuf *w, uint64_t ull) {
    wbuf_nocrc_ulonglong(w, ull);
}


static inline void wbuf_uint64_t(struct wbuf *w, uint64_t ull) {
    wbuf_ulonglong(w, ull);
}

static inline void wbuf_nocrc_bool (struct wbuf *w, bool b) {
    wbuf_nocrc_uint8_t(w, (uint8_t)(b ? 1 : 0));
}

static inline void wbuf_nocrc_BYTESTRING (struct wbuf *w, BYTESTRING v) {
    wbuf_nocrc_bytes(w, v.data, v.len);
}

static inline void wbuf_BYTESTRING (struct wbuf *w, BYTESTRING v) {
    wbuf_bytes(w, v.data, v.len);
}

static inline void wbuf_uint8_t (struct wbuf *w, uint8_t v) {
    wbuf_char(w, v);
}

static inline void wbuf_nocrc_uint32_t (struct wbuf *w, uint32_t v) {
    wbuf_nocrc_uint(w, v);
}

static inline void wbuf_uint32_t (struct wbuf *w, uint32_t v) {
    wbuf_uint(w, v);
}

static inline void wbuf_DISKOFF (struct wbuf *w, DISKOFF off) {
    wbuf_ulonglong(w, (uint64_t)off);
}

static inline void wbuf_BLOCKNUM (struct wbuf *w, BLOCKNUM b) {
    wbuf_ulonglong(w, b.b);
}
static inline void wbuf_nocrc_BLOCKNUM (struct wbuf *w, BLOCKNUM b) {
    wbuf_nocrc_ulonglong(w, b.b);
}

static inline void wbuf_nocrc_TXNID (struct wbuf *w, TXNID tid) {
    wbuf_nocrc_ulonglong(w, tid);
}

static inline void wbuf_TXNID (struct wbuf *w, TXNID tid) {
    wbuf_ulonglong(w, tid);
}

static inline void wbuf_nocrc_XIDP (struct wbuf *w, XIDP xid) {
    wbuf_nocrc_uint32_t(w, xid->formatID);
    wbuf_nocrc_uint8_t(w, xid->gtrid_length);
    wbuf_nocrc_uint8_t(w, xid->bqual_length);
    wbuf_nocrc_literal_bytes(w, xid->data, xid->gtrid_length+xid->bqual_length);
}

static inline void wbuf_nocrc_LSN (struct wbuf *w, LSN lsn) {
    wbuf_nocrc_ulonglong(w, lsn.lsn);
}

static inline void wbuf_LSN (struct wbuf *w, LSN lsn) {
    wbuf_ulonglong(w, lsn.lsn);
}

static inline void wbuf_MSN (struct wbuf *w, MSN msn) {
    wbuf_ulonglong(w, msn.msn);
}

static inline void wbuf_nocrc_FILENUM (struct wbuf *w, FILENUM fileid) {
    wbuf_nocrc_uint(w, fileid.fileid);
}

static inline void wbuf_FILENUM (struct wbuf *w, FILENUM fileid) {
    wbuf_uint(w, fileid.fileid);
}

// 2954
static inline void wbuf_nocrc_FILENUMS (struct wbuf *w, FILENUMS v) {
    wbuf_nocrc_uint(w, v.num);
    uint32_t i;
    for (i = 0; i < v.num; i++) {
        wbuf_nocrc_FILENUM(w, v.filenums[i]);
    }
}

// 2954
static inline void wbuf_FILENUMS (struct wbuf *w, FILENUMS v) {
    wbuf_uint(w, v.num);
    uint32_t i;
    for (i = 0; i < v.num; i++) {
        wbuf_FILENUM(w, v.filenums[i]);
    }
}


#endif
