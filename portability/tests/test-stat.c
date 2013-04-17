/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#include <toku_portability.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <toku_assert.h>
#include <sys/stat.h>
#include <errno.h>

static void test_stat(char *dirname, int result, int ex_errno) {
    int r;
    toku_struct_stat buf;
    r = toku_stat(dirname, &buf);
    //printf("stat %s %d %d\n", dirname, r, errno); fflush(stdout);
    assert(r==result);
    if (r!=0) assert(errno == ex_errno);
}

int main(void) {
    int r;

    test_stat(".", 0, 0);
    test_stat("./", 0, 0);

    r = system("rm -rf testdir"); assert(r==0);
    test_stat("testdir", -1, ENOENT);
    test_stat("testdir/", -1, ENOENT);
    test_stat("testdir/foo", -1, ENOENT);
    test_stat("testdir/foo/", -1, ENOENT);
    r = toku_os_mkdir("testdir", S_IRWXU);
    assert(r == 0);
    test_stat("testdir/foo", -1, ENOENT);
    test_stat("testdir/foo/", -1, ENOENT);
    r = system("touch testdir/foo"); assert(r==0);
    test_stat("testdir/foo", 0, 0);
    test_stat("testdir/foo/", -1, ENOTDIR);

    test_stat("testdir", 0, 0);

    test_stat("./testdir", 0, 0);

    test_stat("./testdir/", 0, 0);

    test_stat("/", 0, 0);

    test_stat("/usr", 0, 0);
    test_stat("/usr/", 0, 0);

    return 0;
}
