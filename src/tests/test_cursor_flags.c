/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <sys/stat.h>
#include <db.h>


static void
test_cursor_flags (int cursor_flags, int expectr) {
    if (verbose) printf("test_cursor_flags:%d %d\n", cursor_flags, expectr);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = ENVDIR "/" "test.cursor.delete.brt";
    int r;

    unlink(fname);

    r = db_create(&db, null_env, 0); assert(r == 0);
    db->set_errfile(db,0); // Turn off those annoying errors
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666); assert(r == 0);

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, cursor_flags);
    assert(r == expectr);

    if (r == 0) {
        r = cursor->c_close(cursor); assert(r == 0);
    }

    r = db->close(db, 0); assert(r == 0);
}

int
test_main(int argc, const char *argv[]) {

    parse_args(argc, argv);
  
    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);
    
    test_cursor_flags(0, 0);
    test_cursor_flags(~0, EINVAL);

    return 0;
}
