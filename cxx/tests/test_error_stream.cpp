/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#include <iostream>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <db_cxx.h>
#include <memory.h>

int verbose;

#define DIR  __FILE__ ".dir"
#define FNAME "test.tdb"

int test_error_stream(void) {
    int r;

    system("rm -rf " DIR);
    toku_os_mkdir(DIR, 0777);

    DbEnv env(DB_CXX_NO_EXCEPTIONS);
    env.set_errpfx("my_env_error_stream");
    env.set_error_stream(&std::cerr);
    
    r = env.open(DIR, DB_INIT_MPOOL + DB_CREATE + DB_PRIVATE, 0777); assert(r == 0);
    r = env.open(DIR, DB_INIT_MPOOL + DB_CREATE + DB_PRIVATE, 0777); assert(r == EINVAL);

    Db db(&env, 0);
    db.set_errpfx("my_db_error_stream");
    db.set_error_stream(&std::cerr);
    r = db.open(NULL, FNAME, 0, DB_BTREE, DB_CREATE, 0777); assert(r == 0);
    r = db.close(0); assert(r == 0);
    r = db.close(0); assert(r == EINVAL);
    r = env.close(0); assert(r == 0);
    r = env.close(0); assert(r == EINVAL);
    return 0;
}

int usage() {
    printf("test_error_stream [-v] [--verbose]\n");
    return 1;
}

int main(int argc, char *argv[]) {
    for (int i=1; i<argc; i++) {
        char *arg = argv[i];
        if (0 == strcmp(arg, "-h") || 0 == strcmp(arg, "--help")) {
            return usage();
        }
        if (0 == strcmp(arg, "-v") || 0 == strcmp(arg, "--verbose")) {
            verbose = 1; continue;
        }
    }

    return test_error_stream();
}
