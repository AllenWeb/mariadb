#include <stdio.h>
#include <toku_stdint.h>
#include <unistd.h>
#include <toku_assert.h>
#include "toku_os.h"

int main(void) {
    assert(toku_os_get_pagesize() == sysconf(_SC_PAGESIZE));
    return 0;
}
