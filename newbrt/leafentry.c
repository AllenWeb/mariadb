#ident "$Id$"
#ident "Copyright (c) 2007, 2008, 2009 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include "includes.h"

u_int32_t toku_le_crc(LEAFENTRY v) {
    return x1764_memory(v, leafentry_memsize(v));
}

static void *
le_malloc(OMT omt, struct mempool *mp, size_t size, void **maybe_free)
{
    if (omt)
	return mempool_malloc_from_omt(omt, mp, size, maybe_free);
    else
	return toku_malloc(size);
}

int
le_committed (u_int32_t klen, void* kval, u_int32_t dlen, void* dval, u_int32_t *resultsize, u_int32_t *disksize, LEAFENTRY *result,
	      OMT omt, struct mempool *mp, void **maybe_free) {
    size_t size = 9+klen+dlen;
    unsigned char *lec=le_malloc(omt, mp, size, maybe_free);
    assert(lec);
    lec[0] = LE_COMMITTED;
    putint(lec+1, klen);
    memcpy(lec+1+4, kval, klen);
    putint(lec+1+4+klen, dlen);
    memcpy(lec+1+4+klen+4, dval, dlen);
    *resultsize=size;
    *disksize  = 1 + 4 + 4 + klen + dlen;
    *result=(LEAFENTRY)lec;
    return 0;
}

int le_both (TXNID xid, u_int32_t klen, void* kval, u_int32_t clen, void* cval, u_int32_t plen, void* pval,
	     u_int32_t *resultsize, u_int32_t *disksize, LEAFENTRY *result,
	     OMT omt, struct mempool *mp, void **maybe_free) {
    size_t size = 1+8+4*3+klen+clen+plen;
    unsigned char *lec=le_malloc(omt, mp, size, maybe_free);
    assert(lec);
    lec[0] = LE_BOTH;
    putint64(lec+1,          xid);
    putint  (lec+1+8,                       klen);
    memcpy  (lec+1+8+4,               kval, klen);
    putint  (lec+1+8+4+klen,                clen);
    memcpy  (lec+1+8+4+klen+4,        cval, clen);
    putint  (lec+1+8+4+klen+4+clen,         plen);
    memcpy  (lec+1+8+4+klen+4+clen+4, pval, plen);
    *resultsize=size;
    *disksize  = 1 + 8 + 4*3 + klen + clen + plen;
    *result=(LEAFENTRY)lec;
    return 0;

}

int
le_provdel (TXNID xid, u_int32_t klen, void* kval, u_int32_t dlen, void* dval,
	    u_int32_t *memsize, u_int32_t *disksize, LEAFENTRY *result,
	    OMT omt, struct mempool *mp, void **maybe_free) {
    size_t size = 1 + 8 + 2*4 + klen + dlen;
    unsigned char *lec= le_malloc(omt, mp, size, maybe_free);
    assert(lec);
    lec[0] = LE_PROVDEL;
    putint64(lec+1,          xid);
    putint  (lec+1+8,                       klen);
    memcpy  (lec+1+8+4,               kval, klen);
    putint  (lec+1+8+4+klen,                dlen);
    memcpy  (lec+1+8+4+klen+4,        dval, dlen);
    *memsize=size;
    *disksize  = 1 + 4 + 4 + 8 + klen + dlen;
    *result=(LEAFENTRY)lec;
    return 0;
}

int
le_provpair (TXNID xid, u_int32_t klen, void* kval, u_int32_t plen, void* pval, u_int32_t *memsize, u_int32_t *disksize, LEAFENTRY *result,
	     OMT omt, struct mempool *mp, void **maybe_free) {
    size_t size = 1 + 8 + 2*4 + klen + plen;
    unsigned char *lec= le_malloc(omt, mp, size, maybe_free);
    assert(lec);
    lec[0] = LE_PROVPAIR;
    putint64(lec+1,          xid);
    putint  (lec+1+8,                       klen);
    memcpy  (lec+1+8+4,               kval, klen);
    putint  (lec+1+8+4+klen,                plen);
    memcpy  (lec+1+8+4+klen+4,        pval, plen);
    *memsize=size;
    *disksize  = 1 + 4 + 4 + 8 + klen + plen;
    *result=(LEAFENTRY)lec;
    return 0;
}

static u_int32_t memsize_le_committed (u_int32_t keylen, void *key __attribute__((__unused__)),
				       u_int32_t vallen, void *val __attribute__((__unused__))) {
    return 1+ 2*4 + keylen + vallen;
}

static u_int32_t memsize_le_both (TXNID txnid __attribute__((__unused__)),
				  u_int32_t klen, void *kval __attribute__((__unused__)),
				  u_int32_t clen, void *cval __attribute__((__unused__)),
				  u_int32_t plen, void *pval __attribute__((__unused__))) {
    return 1 + 8 + 4*3 + klen + clen + plen;
}

static u_int32_t memsize_le_provdel (TXNID txnid __attribute__((__unused__)),
				     u_int32_t klen, void *kval __attribute__((__unused__)),
				     u_int32_t clen, void *cval __attribute__((__unused__))) {
    return 1 + 8 + 4*2 + klen + clen;
}

static u_int32_t memsize_le_provpair (TXNID txnid __attribute__((__unused__)),
				     u_int32_t klen, void *kval __attribute__((__unused__)),
				     u_int32_t plen, void *pval __attribute__((__unused__))) {
    return 1 + 8 + 4*2 + klen + plen;
}

u_int32_t leafentry_memsize (LEAFENTRY le) {
    LESWITCHCALL(le, memsize);
    abort(); return 0;  // make certain compilers happy
}

static u_int32_t disksize_le_committed (u_int32_t keylen, void *key __attribute__((__unused__)),
				       u_int32_t vallen, void *val __attribute__((__unused__))) {
    return 1 + 4 + 4 + keylen + vallen;
}

static u_int32_t disksize_le_both (TXNID txnid __attribute__((__unused__)),
				  u_int32_t klen, void *kval __attribute__((__unused__)),
				  u_int32_t clen, void *cval __attribute__((__unused__)),
				  u_int32_t plen, void *pval __attribute__((__unused__))) {
    return 1 + 8 + 4*3 + klen + clen + plen;
}

static u_int32_t disksize_le_provdel (TXNID txnid __attribute__((__unused__)),
				     u_int32_t klen, void *kval __attribute__((__unused__)),
				     u_int32_t clen, void *cval __attribute__((__unused__))) {
    return 1 + 8 + 4 + 4 + klen + clen;
}

static u_int32_t disksize_le_provpair (TXNID txnid __attribute__((__unused__)),
				       u_int32_t klen, void *kval __attribute__((__unused__)),
				       u_int32_t plen, void *pval __attribute__((__unused__))) {
    return 1 + 8 + 4 + 4 + klen + plen;
}


static u_int32_t
leafentry_disksize_internal (LEAFENTRY le) {
    LESWITCHCALL(le, disksize);
    abort(); return 0;  // make certain compilers happy
}

u_int32_t leafentry_disksize (LEAFENTRY le) {
    u_int32_t d = leafentry_disksize_internal(le);
#if 0
    // this computation is currently identical to the _disksize_internal
    u_int32_t m = leafentry_memsize(le);
    assert(m==d);
#endif
    return d;
}

u_int32_t toku_logsizeof_LEAFENTRY (LEAFENTRY le) {
    return leafentry_disksize(le);
}

int toku_fread_LEAFENTRY(FILE *f, LEAFENTRY *le, struct x1764 *checksum, u_int32_t *len) {
    u_int8_t state;
    int r = toku_fread_u_int8_t (f, &state, checksum, len); if (r!=0) return r;
    TXNID xid;
    BYTESTRING a,b,c;
    u_int32_t memsize, disksize;
    switch ((enum le_state)state) {
    case LE_COMMITTED:
	r = toku_fread_BYTESTRING(f, &a, checksum, len);  if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &b, checksum, len);  if (r!=0) return r;
	r = le_committed(a.len, a.data, b.len, b.data,
			 &memsize, &disksize, le,
			 0, 0, 0);
	toku_free_BYTESTRING(a);
	toku_free_BYTESTRING(b);
	return r;
    case LE_BOTH:
	r = toku_fread_TXNID(f, &xid, checksum, len);     if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &a, checksum, len);  if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &b, checksum, len);  if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &c, checksum, len);  if (r!=0) return r;
	r = le_both(xid, a.len, a.data, b.len, b.data, c.len, c.data,
		    &memsize, &disksize, le,
		    0, 0, 0);
	toku_free_BYTESTRING(a);
	toku_free_BYTESTRING(b);
	toku_free_BYTESTRING(c);
	return r;
    case LE_PROVDEL:
	r = toku_fread_TXNID(f, &xid, checksum, len);     if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &a, checksum, len);  if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &b, checksum, len);  if (r!=0) return r;
	r = le_provdel(xid, a.len, a.data, b.len, b.data,
		       &memsize, &disksize, le,
		       0, 0, 0);
	toku_free_BYTESTRING(a);
	toku_free_BYTESTRING(b);
	return r;
    case LE_PROVPAIR:
	r = toku_fread_TXNID(f, &xid, checksum, len);     if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &a, checksum, len);  if (r!=0) return r;
	r = toku_fread_BYTESTRING(f, &b, checksum, len);  if (r!=0) return r;
	r = le_provpair(xid, a.len, a.data, b.len, b.data,
			&memsize, &disksize, le,
			0, 0, 0);
	toku_free_BYTESTRING(a);
	toku_free_BYTESTRING(b);
	return r;
    }
    return DB_BADFORMAT;
}

static int print_le_committed (u_int32_t keylen, void *key, u_int32_t vallen, void *val, FILE *outf) {
    fprintf(outf, "{C: ");
    toku_print_BYTESTRING(outf, keylen, key);
    toku_print_BYTESTRING(outf, vallen, val);
    fprintf(outf, "}");
    return 0;
}

static int print_le_both (TXNID xid, u_int32_t klen, void *kval, u_int32_t clen, void *cval, u_int32_t plen, void *pval, FILE *outf) {
    fprintf(outf, "{B: ");
    fprintf(outf, " xid=%" PRIu64, xid);
    fprintf(outf, " key=");
    toku_print_BYTESTRING(outf, klen, kval);
    toku_print_BYTESTRING(outf, clen, cval);
    fprintf(outf, " provisional=");
    toku_print_BYTESTRING(outf, plen, pval);
    fprintf(outf, "}");
    return 0;
}

static int print_le_provdel (TXNID xid, u_int32_t klen, void *kval, u_int32_t clen, void *cval, FILE *outf) {
    fprintf(outf, "{D: ");
    fprintf(outf, " xid=%" PRIu64, xid);
    fprintf(outf, " key=");
    toku_print_BYTESTRING(outf, klen, kval);
    fprintf(outf, " committed=");
    toku_print_BYTESTRING(outf, clen, cval);
    fprintf(outf, "}");
    return 0;
}

static int print_le_provpair (TXNID xid, u_int32_t klen, void *kval, u_int32_t plen, void *pval, FILE *outf) {
    fprintf(outf, "{P: ");
    fprintf(outf, " xid=%" PRIu64, xid);
    fprintf(outf, " key=");
    toku_print_BYTESTRING(outf, klen, kval);
    fprintf(outf, " provisional=");
    toku_print_BYTESTRING(outf, plen, pval);
    fprintf(outf, "}");
    return 0;
}

int print_leafentry (FILE *outf, LEAFENTRY v) {
    if (!v) { printf("NULL"); return 0; }
    LESWITCHCALL(v, print, outf);
    abort(); return 0;  // make certain compilers happy
}

int toku_logprint_LEAFENTRY (FILE *outf, FILE *inf, const char *fieldname, struct x1764 *checksum, u_int32_t *len, const char *format __attribute__((__unused__))) {
    LEAFENTRY v;
    int r = toku_fread_LEAFENTRY(inf, &v, checksum, len);
    if (r!=0) return r;
    fprintf(outf, " %s=", fieldname);
    print_leafentry(outf, v);
    toku_free(v);
    return 0;
}

void wbuf_LEAFENTRY(struct wbuf *w, LEAFENTRY le) {
    wbuf_literal_bytes(w, le, leafentry_disksize(le));
}

void rbuf_LEAFENTRY(struct rbuf *r, u_int32_t *resultsize, u_int32_t *disksize, LEAFENTRY *lep) {
    LEAFENTRY le = (LEAFENTRY)(&r->buf[r->ndone]);
    u_int32_t siz = leafentry_disksize(le);
    bytevec bytes;
    rbuf_literal_bytes(r, &bytes, siz);
    *lep = toku_memdup(le, siz);
    assert(*lep);
    *resultsize = siz;
    *disksize   = siz;
    return;
}

// LEAFENTRUse toku_free()
void toku_free_LEAFENTRY(LEAFENTRY le) {
    toku_free(le);
}


int le_is_provdel(LEAFENTRY le) {
    return get_le_state(le)==LE_PROVDEL;
}

void* latest_key_le_committed (u_int32_t UU(keylen), void *key, u_int32_t UU(vallen), void *UU(val)) {
    return key;
}
void* latest_key_le_both (TXNID UU(xid), u_int32_t UU(klen), void *kval, u_int32_t UU(clen), void *UU(cval), u_int32_t UU(plen), void *UU(pval)) {
    return kval;
}
void* latest_key_le_provdel (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval)) {
    return 0; // for provisional delete, there is no *latest* key, so return NULL
}
void* latest_key_le_provpair (TXNID UU(xid), u_int32_t UU(klen), void *kval, u_int32_t UU(plen), void *UU(pval)) {
    return kval;
}
void* le_latest_key (LEAFENTRY le) {
    LESWITCHCALL(le, latest_key);
    abort(); return 0;  // make certain compilers happy
}

u_int32_t latest_keylen_le_committed (u_int32_t keylen, void *UU(key), u_int32_t UU(vallen), void *UU(val)) {
    return keylen;
}
u_int32_t latest_keylen_le_both (TXNID UU(xid), u_int32_t klen, void *UU(kval), u_int32_t UU(clen), void *UU(cval), u_int32_t UU(plen), void *UU(pval)) {
    return klen;
}
u_int32_t latest_keylen_le_provdel (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval)) {
    return 0; // for provisional delete, there is no *latest* key, so return 0.  What else can we do?
}
u_int32_t latest_keylen_le_provpair (TXNID UU(xid), u_int32_t klen, void *UU(kval), u_int32_t UU(plen), void *UU(pval)) {
    return klen;
}
u_int32_t le_latest_keylen (LEAFENTRY le) {
    LESWITCHCALL(le, latest_keylen);
    abort(); return 0;  // make certain compilers happy
}

void* latest_val_le_committed (u_int32_t UU(keylen), void *UU(key), u_int32_t UU(vallen), void *UU(val)) {
    return val;
}
void* latest_val_le_both (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval), u_int32_t UU(plen), void *pval) {
    return pval;
}
void* latest_val_le_provdel (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval)) {
    return 0; // for provisional delete, there is no *latest* key, so return NULL
}
void* latest_val_le_provpair (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(plen), void *pval) {
    return pval;
}
void* le_latest_val (LEAFENTRY le) {
    LESWITCHCALL(le, latest_val);
    abort(); return 0;  // make certain compilers happy
}

u_int32_t latest_vallen_le_committed (u_int32_t UU(keylen), void *UU(key), u_int32_t vallen, void *UU(val)) {
    return vallen;
}
u_int32_t latest_vallen_le_both (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval), u_int32_t plen, void *UU(pval)) {
    return plen;
}
u_int32_t latest_vallen_le_provdel (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval)) {
    return 0; // for provisional delete, there is no *latest* key, so return 0.  What else can we do?
}
u_int32_t latest_vallen_le_provpair (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t plen, void *UU(pval)) {
    return plen;
}
u_int32_t le_latest_vallen (LEAFENTRY le) {
    LESWITCHCALL(le, latest_vallen);
    abort(); return 0;  // make certain compilers happy
}

void* any_key_le_committed (u_int32_t UU(keylen), void *key, u_int32_t UU(vallen), void *UU(val)) {
    return key;
}
void* any_key_le_both (TXNID UU(xid), u_int32_t UU(klen), void *kval, u_int32_t UU(clen), void *UU(cval), u_int32_t UU(plen), void *UU(pval)) {
    return kval;
}
void* any_key_le_provdel (TXNID UU(xid), u_int32_t UU(klen), void *kval, u_int32_t UU(clen), void *UU(cval)) {
    return kval;
}
void* any_key_le_provpair (TXNID UU(xid), u_int32_t UU(klen), void *kval, u_int32_t UU(plen), void *UU(pval)) {
    return kval;
}
void* le_any_key (LEAFENTRY le) {
    LESWITCHCALL(le, any_key);
    abort(); return 0;  // make certain compilers happy
}

u_int32_t any_keylen_le_committed (u_int32_t keylen, void *UU(key), u_int32_t UU(vallen), void *UU(val)) {
    return keylen;
}
u_int32_t any_keylen_le_both (TXNID UU(xid), u_int32_t klen, void *UU(kval), u_int32_t UU(clen), void *UU(cval), u_int32_t UU(plen), void *UU(pval)) {
    return klen;
}
u_int32_t any_keylen_le_provdel (TXNID UU(xid), u_int32_t klen, void *UU(kval), u_int32_t UU(clen), void *UU(cval)) {
    return klen;
}
u_int32_t any_keylen_le_provpair (TXNID UU(xid), u_int32_t klen, void *UU(kval), u_int32_t UU(plen), void *UU(pval)) {
    return klen;
}
u_int32_t le_any_keylen (LEAFENTRY le) {
    LESWITCHCALL(le, any_keylen);
    abort(); return 0;  // make certain compilers happy
}

void* any_val_le_committed (u_int32_t UU(keylen), void *UU(key), u_int32_t UU(vallen), void *UU(val)) {
    return val;
}
void* any_val_le_both (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval), u_int32_t UU(plen), void *pval) {
    return pval;
}
void* any_val_le_provdel (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *cval) {
    return cval;
}
void* any_val_le_provpair (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(plen), void *pval) {
    return pval;
}
void* le_any_val (LEAFENTRY le) {
    LESWITCHCALL(le, any_val);
    abort(); return 0;  // make certain compilers happy
}

u_int32_t any_vallen_le_committed (u_int32_t UU(keylen), void *UU(key), u_int32_t vallen, void *UU(val)) {
    return vallen;
}
u_int32_t any_vallen_le_both (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval), u_int32_t plen, void *UU(pval)) {
    return plen;
}
u_int32_t any_vallen_le_provdel (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t clen, void *UU(cval)) {
    return clen; // for provisional delete, there is no *any* key, so return 0.  What else can we do?
}
u_int32_t any_vallen_le_provpair (TXNID UU(xid), u_int32_t UU(klen), void *UU(kval), u_int32_t plen, void *UU(pval)) {
    return plen;
}
u_int32_t le_any_vallen (LEAFENTRY le) {
    LESWITCHCALL(le, any_vallen);
    abort(); return 0;  // make certain compilers happy
}


u_int64_t any_xid_le_committed (u_int32_t UU(keylen), void *UU(key), u_int32_t UU(vallen), void *UU(val)) {
    return 0;
}

u_int64_t any_xid_le_both (TXNID xid, u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval), u_int32_t UU(plen), void *UU(pval)) {
    return xid;
}

u_int64_t any_xid_le_provdel (TXNID xid, u_int32_t UU(klen), void *UU(kval), u_int32_t UU(clen), void *UU(cval)) {
    return xid;
}

u_int64_t any_xid_le_provpair (TXNID xid, u_int32_t UU(klen), void *UU(kval), u_int32_t UU(plen), void *UU(pval)) {
    return xid;
}

u_int64_t le_any_xid (LEAFENTRY le) {
    LESWITCHCALL(le, any_xid);
    abort(); return 0;  // make certain compilers happy
}
