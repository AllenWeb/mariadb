/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "$Id$"

#include "test.h"

#include <stdio.h>
#include <stdlib.h>

#include <toku_pthread.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <db.h>

#include "threaded_stress_test_helpers.h"

// The intent of this test is to measure the throughput of db->puts
// with multiple threads.

static void
stress_table(DB_ENV* env, DB** dbp, struct cli_args *cli_args) {
    if (verbose) printf("starting creation of pthreads\n");
    const int num_threads = cli_args->num_put_threads;
    struct arg myargs[num_threads];
    operation_t put_op = (cli_args->serial_insert
                          ? serial_put_op
                          : random_put_op_singledb);
    struct serial_put_extra spe[num_threads];
    ZERO_ARRAY(spe);
    for (int i = 0; i < num_threads; i++) {
        arg_init(&myargs[i], dbp, env, cli_args);
        myargs[i].operation = put_op;
        if (cli_args->serial_insert) {
            myargs[i].operation_extra = &spe[i];
        }
    }
    run_workers(myargs, num_threads, cli_args->time_of_test, false, cli_args);
}

int
test_main(int argc, char *const argv[]) {
    struct cli_args args = get_default_args_for_perf();
    args.num_elements = 0;  // want to start with empty DBs
    parse_stress_test_args(argc, argv, &args);
    stress_test_main(&args);
    return 0;
}
