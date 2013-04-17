#include <stdio.h>
#include <toku_assert.h>
#include <toku_stdint.h>
#include <toku_os.h>

int verbose = 0;

int main(void) {
    uint64_t cpuhz;
    int r = toku_os_get_processor_frequency(&cpuhz);
    assert(r == 0);
    if (verbose) {
	printf("%"PRIu64"\n", cpuhz);
    }
    assert(cpuhz>100000000);
    return 0;
}
