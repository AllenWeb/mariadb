/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include "includes.h"

// Calculate the fingerprint for a kvpair
static void toku_calc_more_murmur_kvpair (struct x1764 *mm, const void *key, int keylen, const void *val, int vallen) {
    int i;
    i = toku_htonl(keylen);
    x1764_add(mm,  (void*)&i, 4);
    x1764_add(mm,  key, keylen);
    i = toku_htonl(vallen);
    x1764_add(mm, (void*)&i, 4);
    x1764_add(mm, val, vallen);
}

#if 0
 u_int32_t toku_calccrc32_kvpair (const void *key, int keylen, const void *val, int vallen) {
    return toku_calc_more_crc32_kvpair(toku_null_crc, key, keylen, val, vallen);
}

u_int32_t toku_calccrc32_kvpair_struct (const struct kv_pair *kvp) {
    return toku_calccrc32_kvpair(kv_pair_key_const(kvp), kv_pair_keylen(kvp),
				 kv_pair_val_const(kvp), kv_pair_vallen(kvp));
}
#endif

u_int32_t toku_calc_fingerprint_cmd (u_int32_t type, TXNID xid, const void *key, u_int32_t keylen, const void *val, u_int32_t vallen) {
    unsigned char type_c = (unsigned char)type;
    unsigned int a = toku_htonl(xid>>32);
    unsigned int b = toku_htonl(xid&0xffffffff);
    struct x1764 mm;
    x1764_init(&mm);
    x1764_add(&mm, &type_c, 1);
    x1764_add(&mm, &a, 4);
    x1764_add(&mm, &b, 4);
    toku_calc_more_murmur_kvpair(&mm, key, keylen, val, vallen);
    return x1764_finish(&mm);
}
