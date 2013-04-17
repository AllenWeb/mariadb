/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include "test.h"

// create and close, making sure that everything is deallocated properly.

int
test_main (int argc __attribute__((__unused__)),
	  const char *argv[] __attribute__((__unused__))) {
    int r;
    char logname[TOKU_PATH_MAX+1];
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    r = toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU);                               assert(r==0);
    TOKULOGGER logger;
    r = toku_logger_create(&logger);                                 assert(r == 0);
    r = toku_logger_open(TOKU_TEST_FILENAME, logger);                             assert(r == 0);

    {
	ml_lock(&logger->input_lock);
	toku_logger_make_space_in_inbuf(logger, 5);
	snprintf(logger->inbuf.buf+logger->inbuf.n_in_buf, 5, "a1234");
	logger->inbuf.n_in_buf+=5;
	logger->lsn.lsn++;
	logger->inbuf.max_lsn_in_buf = logger->lsn;
	ml_unlock(&logger->input_lock);
    }

    r = toku_logger_close(&logger);                                  assert(r == 0);
    {
	toku_struct_stat statbuf;
        sprintf(logname, "%s/log000000000000.tokulog%d", TOKU_TEST_FILENAME, TOKU_LOG_VERSION);
	r = toku_stat(logname, &statbuf);
	assert(r==0);
	assert(statbuf.st_size==12+5);
    }
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    return 0;
}
