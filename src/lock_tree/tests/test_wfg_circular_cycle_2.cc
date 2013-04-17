/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
// find cycles in a simple WFG

#include "test.h"
#include "wfg.h"

int main(int argc, const char *argv[]) {

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose++;
            continue;
        }
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            if (verbose > 0) verbose--;
            continue;
        }
        assert(0);
    }

    // setup
    struct wfg *wfg = wfg_new();
    struct wfg *cycles = wfg_new();

    wfg_add_edge(wfg, 1, 2);
    for (TXNID i = 1; i <= 2; i++) {
        assert(!wfg_exist_cycle_from_txnid(wfg, i));
        wfg_reinit(cycles);
        assert(wfg_find_cycles_from_txnid(wfg, i, cycles) == 0);
    }

    wfg_add_edge(wfg, 2, 3);
    for (TXNID i = 1; i <= 3; i++) {
        assert(!wfg_exist_cycle_from_txnid(wfg, i));
        wfg_reinit(cycles);
        assert(wfg_find_cycles_from_txnid(wfg, i, cycles) == 0);
    }

    wfg_add_edge(wfg, 3, 4);
    for (TXNID i = 1; i <= 4; i++) {
        assert(!wfg_exist_cycle_from_txnid(wfg, i));
        wfg_reinit(cycles);
        assert(wfg_find_cycles_from_txnid(wfg, i, cycles) == 0);
    }

    wfg_add_edge(wfg, 4, 1);
    for (TXNID i = 1; i <= 4; i++) {
        assert(wfg_exist_cycle_from_txnid(wfg, i));
        wfg_reinit(cycles);
        assert(wfg_find_cycles_from_txnid(wfg, i, cycles) == 1);
        if (verbose) wfg_print(cycles);
    }

    wfg_add_edge(wfg, 1, 5);
    wfg_add_edge(wfg, 5, 6);
    for (TXNID i = 1; i <= 4; i++) {
        assert(wfg_exist_cycle_from_txnid(wfg, i));
        wfg_reinit(cycles);
        assert(wfg_find_cycles_from_txnid(wfg, i, cycles) == 1);
        if (verbose) wfg_print(cycles);
    }
    for (TXNID i = 5; i <= 6; i++) {
        assert(!wfg_exist_cycle_from_txnid(wfg, i));
        wfg_reinit(cycles);
        assert(wfg_find_cycles_from_txnid(wfg, i, cycles) == 0);
        if (verbose) wfg_print(cycles);
    }

    wfg_add_edge(wfg, 6, 1);
    assert(wfg_exist_cycle_from_txnid(wfg, 1));
    wfg_reinit(cycles);
    assert(wfg_find_cycles_from_txnid(wfg, 1, cycles) == 2);
    if (verbose) wfg_print(cycles);

    wfg_free(wfg);
    wfg_free(cycles);

    return 0;
}
