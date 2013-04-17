/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2010 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

/* Dump the log from stdin to stdout. */

#include "includes.h"

#if 0
static u_int32_t crc=0;
static u_int32_t actual_len=0;

static int get_char(void) {
    int v = getchar();
    if (v==EOF) return v;
    unsigned char c = v;
    crc=toku_crc32(crc, &c, 1);
    actual_len++;
    return v;
}

static u_int32_t get_uint32 (void) {
    u_int32_t a = get_char();
    u_int32_t b = get_char();
    u_int32_t c = get_char();
    u_int32_t d = get_char();
    return (a<<24)|(b<<16)|(c<<8)|d;
}

static u_int64_t get_uint64 (void) {
    u_int32_t hi = get_uint32();
    u_int32_t lo = get_uint32();
    return ((((long long)hi) << 32)
	    |
	    lo);
}

static void transcribe_lsn (void) {
    long long value = get_uint64();
    printf(" lsn=%lld", value);
}

static void transcribe_txnid (void) {
    long long value = get_uint64();
    printf(" txnid=%lld", value);
}

static void transcribe_fileid (void) {
    u_int32_t value = get_uint32();
    printf(" fileid=%d", value);
}


static void transcribe_diskoff (void) {
    long long value = get_uint64();
    printf(" diskoff=%lld", value);
}

static void transcribe_crc32 (void) {
    u_int32_t oldcrc=crc;
    u_int32_t l = get_uint32();
    printf(" crc=%08x", l);
    assert(l==oldcrc);
}

static void transcribe_mode (void) {
    u_int32_t value = get_uint32();
    printf(" mode=0%o", value);
}

static void transcribe_filenum(void) {
    u_int32_t value = get_uint32();
    printf(" filenum=%d", value);
}

static u_int32_t len1;
static void transcribe_len1 (void) {
    len1 = get_uint32();
    //printf(" len=%d", len1);
}

static void transcribe_len (void) {
    u_int32_t l = get_uint32();
    printf(" len=%d", l);
    if (l!=actual_len) printf(" actual_len=%d", actual_len);
    assert(l==actual_len);
    assert(len1==actual_len);
}

static void transcribe_key_or_data (char *what) {
    u_int32_t l = get_uint32();
    unsigned int i;
    printf(" %s(%d):\"", what, l);
    for (i=0; i<l; i++) {
	u_int32_t c = get_char();
	if (c=='\\') printf("\\\\");
	else if (c=='\n') printf("\\n");
	else if (c==' ') printf("\\ ");
	else if (c=='"') printf("\"\"");
	else if (isprint(c)) printf("%c", c);
	else printf("\\%02x", c);
    }
    printf("\"");
}
	
static void transcribe_header (void) {
    printf(" {size=%d", get_uint32());
    printf(" flags=%d", get_uint32());
    printf(" nodesize=%d", get_uint32());
    printf(" freelist=%" PRId64, get_uint64());
    printf(" unused_memory=%" PRId64,get_uint64());
    int n_roots=get_uint32();
    printf(" n_named_roots=%d", n_roots);
    if (n_roots>0) {
	abort();
    } else {
	printf(" root=%" PRId64, get_uint64());
    }
    printf("}");
}
#endif

static void newmain (int count) {
    int i;
    u_int32_t version;
    int r = toku_read_and_print_logmagic(stdin, &version);
    for (i=0; i!=count; i++) {
	r = toku_logprint_one_record(stdout, stdin);
	if (r==EOF) break;
	if (r!=0) {
	    fflush(stdout);
	    fprintf(stderr, "Problem in log err=%d\n", r);
	    exit(1);
	}
    }
}

int main (int argc, char *const argv[]) {
    int count=-1;
    while (argc>1) {
	if (strcmp(argv[1], "--oldcode")==0) {
	    fprintf(stderr,"Old code no longer works.\n");
	    exit(1);
	} else {
	    count = atoi(argv[1]);
	}
	argc--; argv++;
    }
    newmain(count);
    return 0;
}

