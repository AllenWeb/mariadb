/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
// read the processor time stamp register

#if defined __ICC

#define USE_RDTSC 1
#define rdtsc _rdtsc

#elif defined __i386__

#define USE_RDTSC 1

static inline unsigned long long rdtsc(void) {
    unsigned long hi, lo;
    __asm__ __volatile__ ("rdtsc\n"
                          "movl %%edx,%0\n"
			  "movl %%eax,%1" : "=r"(hi), "=r"(lo) : : "edx", "eax");
    return ((unsigned long long) hi << 32ULL) + (unsigned long long) lo;
}

#elif defined __x86_64__

#define USE_RDTSC 1

static inline unsigned long long rdtsc(void) {
    unsigned long long r;
    __asm__ __volatile__ ("rdtsc\n"
                          "shl $32,%%rdx\n"
                          "or %%rdx,%%rax\n"
			  "movq %%rax,%0" : "=r"(r) : : "edx", "eax", "rdx", "rax");
    return r;
}

#else

#define USE_RDTSC 0

#endif
