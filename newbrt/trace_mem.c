#include "toku_portability.h"
#include "rdtsc.h"
#include "trace_mem.h"

// customize this as required
#define NTRACE 0
#if NTRACE
static struct toku_trace {
    const char *str;
    int n;
    unsigned long long ts;
} toku_trace[NTRACE];

static int toku_next_trace = 0;
#endif

void toku_add_trace_mem (const char *str __attribute__((unused)),
			 int n __attribute__((unused))) {
#if USE_RDTSC && NTRACE
    int i = toku_next_trace++;
    if (toku_next_trace >= NTRACE) toku_next_trace = 0;
    toku_trace[i].ts = rdtsc();
    toku_trace[i].str = str;
    toku_trace[i].n = n;
#endif
}

void toku_print_trace_mem(void) {
#if NTRACE
    int i = toku_next_trace;
    do {
        if (toku_trace[i].str)
            printf("%llu %s:%d\n", toku_trace[i].ts, toku_trace[i].str, toku_trace[i].n);
        i++;
        if (i >= NTRACE) i = 0;
    } while (i != toku_next_trace);
#endif
}
