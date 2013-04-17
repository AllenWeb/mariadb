/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
/* Benchmark various hash functions. */

#include <sys/time.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <toku_assert.h>

#define N 200000000
char *buf;

static double tdiff (struct timeval *a, struct timeval *b) {
    return a->tv_sec - b->tv_sec + (1e-6)*(a->tv_usec - b->tv_usec);
}

#define measure_bandwidth(str, body) ({		        \
    int c; 				  	        \
    struct timeval start,end;                           \
    gettimeofday(&start, 0);                            \
    body;                                               \
    gettimeofday(&end, 0);                              \
    double diff = tdiff(&end, &start);                  \
    printf("%s=%08x %d bytes in %8.6fs for %8.3fMB/s\n", str, c, N, diff, N*(1e-6)/diff); \
	})

int sum32 (int start, void *buf, int bytecount)  {
    int *ibuf = buf;
    assert(bytecount%4==0);
    while (bytecount>0) {
	start+=*ibuf;
	ibuf++;
	bytecount-=4;
    }
    return start;
}

static const u_int32_t m = 0x5bd1e995;
static const int r = 24;
static const u_int32_t seed = 0x3dd3b51a;

#define USE_ZERO_CHECKSUM 0

static u_int32_t MurmurHash2 ( const void * key, int len)
{
    if (USE_ZERO_CHECKSUM) return 0;

    // 'm' and 'r' are mixing constants generated offline.
    // They're not really 'magic', they just happen to work well.


    // Initialize the hash to a 'random' value

    u_int32_t h = seed;

    // Mix 4 bytes at a time into the hash

    const unsigned char * data = (const unsigned char *)key;

    while(len >= 4)
	{
	    u_int32_t k = *(u_int32_t *)data;

	    k *= m; 
	    k ^= k >> r; 
	    k *= m; 
		
	    h *= m; 
	    h ^= k;

	    data += 4;
	    len -= 4;
	}
	
    // Handle the last few bytes of the input array

    switch(len)
	{
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
	    h *= m;
	};

    // Do a few final mixes of the hash to ensure the last few
    // bytes are well-incorporated.

    h ^= h >> 29;
    h *= m;
    h ^= h >> 31;

    return h;
} 

struct murmur {
    int n_bytes_in_k;  // How many bytes in k
    u_int32_t k;       // These are the extra bytes.   Bytes are shifted into the low-order bits.
    u_int32_t h;       // The hash so far (up to the most recent 4-byte boundary)
};

void murmur_init (struct murmur *mm) {
    mm->n_bytes_in_k=0;
    mm->k  =0;
    mm->h = seed;
}

#define MIX() ({ k *= m; k ^= k >> r; k *= m; h *= m; h ^= k; })
#define LD1() data[0]
#define LD2() ((data[0]<<8)  | data[1])
#define LD3() ((data[0]<<16) | (data[1]<<8) | data[2])
#define ADD1_0()  (mm->k =           LD1())
#define ADD1()    (mm->k = (k<<8)  | LD1())
#define ADD2_0()  (mm->k =           LD2())
#define ADD2()    (mm->k = (k<<16) | LD2())
#define ADD3_0()  (mm->k =           LD3())
#define ADD3()    (mm->k = (k<<24) | LD3())

void murmur_add (struct murmur *mm, const void * key, unsigned int len) {
    if (USE_ZERO_CHECKSUM) return;
    if (len==0) return;
    const int n_bytes_in_k =  mm->n_bytes_in_k;
    u_int32_t k = mm->k;
    const unsigned char *data = key;
    u_int32_t h = mm->h;
    switch (n_bytes_in_k) {
    case 0:
	switch (len) {
	case 1:  ADD1_0(); mm->n_bytes_in_k = 1;                       mm->h=h; return;
	case 2:  ADD2_0(); mm->n_bytes_in_k = 2;                       mm->h=h; return;
	case 3:  ADD3_0(); mm->n_bytes_in_k = 3;                       mm->h=h; return;
	default: break;
	}
	break;
    case 1:
	switch (len) {
	case 1:  ADD1(); mm->n_bytes_in_k = 2;                         mm->h=h; return;
	case 2:  ADD2(); mm->n_bytes_in_k = 3;                         mm->h=h; return;
	case 3:  ADD3(); mm->n_bytes_in_k = 0; MIX();                  mm->h=h; return;
	default: ADD3(); mm->n_bytes_in_k = 0; MIX(); len-=3; data+=3; break;
	}
	break;
    case 2:
	switch (len) {
	case 1:  ADD1(); mm->n_bytes_in_k = 3;                         mm->h=h; return;
	case 2:  ADD2(); mm->n_bytes_in_k = 0; MIX();                  mm->h=h; return;
	default: ADD2(); mm->n_bytes_in_k = 0; MIX(); len-=2; data+=2; break;
	}
	break;
    case 3:
	switch (len) {
	case 1:  ADD1(); mm->n_bytes_in_k = 0; MIX();                  mm->h=h; return;
	default: ADD1(); mm->n_bytes_in_k = 0; MIX(); len--; data++;   break;
	}
	break;
    default: assert(0);
    }

    // We've used up the partial bytes at the beginning of k.
    assert(mm->n_bytes_in_k==0);
    while (len >= 4) {
	u_int32_t k = toku_dtoh32(*(u_int32_t *)data);
	//printf(" oldh=%08x k=%08x", h, k);

	k *= m; 
	k ^= k >> r; 
	k *= m; 
		
	h *= m; 
	h ^= k;

	data += 4;
	len -= 4;
	//printf(" h=%08x\n", h);
    }
    mm->h=h;
    //printf("%s:%d h=%08x\n", __FILE__, __LINE__, h);
    {
	u_int32_t k=0;
	switch (len) {
	case 3: k =  *data << 16;  data++;
	case 2: k |= *data << 8;   data++;
	case 1: k |= *data;
	}
	mm->k = k;
	mm->n_bytes_in_k = len;
	//printf("now extra=%08x (%d bytes) n_bytes=%d\n", mm->k, len, mm->n_bytes_in_k);

    }
}

u_int32_t murmur_finish (struct murmur *mm) {
    if (USE_ZERO_CHECKSUM) return 0;
    u_int32_t h = mm->h;
    if (mm->n_bytes_in_k>0) {
	h ^= mm->k;
	h *= m;
    }
    if (0) {
	// The real murmur function does this extra mixing at the end.  We don't need that for fingerprint.
	h ^= h >> 29;
	h *= m;
	h ^= h >> 31;
    }
    return h;
}

struct sum84 {
    u_int32_t sum;
    int i;
};
void sum84_init (struct sum84 *s) { s->sum=0; s->i=0; };
void sum84_add  (struct sum84 *s, char *buf, int count) {
    while (s->i%4!=0 && count>0) {
	char v = *buf;
	s->sum ^= v << (s->i%4)*8;
	buf++; count--; s->i++;
    }
    while (count>4) {
	s->sum ^= *(int*)buf;
	buf+=4; count-=4;
    }
    while (count>0) {
	char v = *buf;
	s->sum ^= v << (s->i%4)*8;
	buf++; count--; s->i++;
    }
}
int sum84_finish (struct sum84 *s) {
    return s->sum;
}

u_int32_t xor8_add  (u_int32_t x, char *buf, int count) {
    while (count>4) {
	x ^= *(int*)buf;
	buf+=4; count-=4;
    }
    while (count>0) {
	char v = *buf;
	x ^= v;
	buf++; count--;
    }
    return x;
}
u_int32_t xor8_finish (u_int32_t x) {
    return (x ^ (x>>8) ^ (x>>16) ^ (x>>24))&0xff;
}

u_int64_t xor8_64_add  (u_int64_t x, char *buf, int count) {
    while (count>8) {
	x ^= *(u_int64_t*)buf;
	buf+=8; count-=8;
    }
    while (count>0) {
	char v = *buf;
	x ^= v;
	buf++; count--;
    }
    return x;
}
u_int32_t xor8_64_finish (u_int64_t x) {
    return (x ^ (x>>8) ^ (x>>16) ^ (x>>24) ^ (x>>32) ^ (x>>40) ^ (x>>48) ^ (x>>56))&0xff;
}

static void measure_bandwidths (void) {
    measure_bandwidth("crc32    ", c=crc32(0, buf, N));
    measure_bandwidth("sum32    ", c=sum32(0, buf, N));
    measure_bandwidth("murmur   ", c=MurmurHash2(buf, N));
    measure_bandwidth("murmurf  ", ({ struct murmur mm; murmur_init(&mm); murmur_add(&mm, buf, N); c=murmur_finish(&mm); }));
    measure_bandwidth("sum84    ", ({ struct sum84 s; sum84_init(&s); sum84_add(&s, buf, N); c=sum84_finish(&s); }));
    measure_bandwidth("xor32    ", ({ c=0; int j; for(j=0; j<N/4; j++) c^=*(int*)buf+j*4; }));
    measure_bandwidth("xor8     ", c=xor8_finish(xor8_add(0, buf, N)));
    measure_bandwidth("xor8_64  ", c=xor8_64_finish(xor8_64_add(0, buf, N)));
    measure_bandwidth("crc32by1 ", ({ c=0; int j; for(j=0; j<N; j++) c=crc32(c, buf+j, 1); }));
    measure_bandwidth("crc32by2 ", ({ c=0; int j; for(j=0; j<N; j+=2) c=crc32(c, buf+j, 2); }));
    measure_bandwidth("sum8by1  ", ({ c=0; int j; for(j=0; j<N; j++) c+=buf[j]; }));
    measure_bandwidth("murmurby1", ({ struct murmur mm; murmur_init(&mm); int j; for(j=0; j<N; j++) murmur_add(&mm, buf+j, 1); c=murmur_finish(&mm); }));
    measure_bandwidth("murmurby2", ({ struct murmur mm; murmur_init(&mm); int j; for(j=0; j<N; j+=2) murmur_add(&mm, buf+j, 2); c=murmur_finish(&mm); }));
    measure_bandwidth("sum84by1 ", ({ struct sum84 s; sum84_init(&s); int j; for(j=0; j<N; j++) sum84_add(&s, buf+j, 1); c=sum84_finish(&s); }));
    measure_bandwidth("xor8by1  ", ({ int j; c=0; for(j=0; j<N; j++) c=xor8_add(c, buf+j, 1); c=xor8_finish(c); }));
    measure_bandwidth("xor864by1", ({ int j; u_int64_t x=0; for(j=0; j<N; j++) x=xor8_64_add(x, buf+j, 1); c=xor8_64_finish(x); }));
}

int main (int argc __attribute__((__unused__)), char *argv[] __attribute__((__unused__))) {
    buf = malloc(N);
    int i;
    for (i=0; i<N; i++) buf[i]=random();
    measure_bandwidths();
    return 0;
}
