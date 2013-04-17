/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#if !defined(TOKUDB_COMMON_H)
#define TOKUDB_COMMON_H

#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <db.h>
#include <inttypes.h>
#include <signal.h>
#include <memory.h>

#define SET_BITS(bitvector, bits)      ((bitvector) |= (bits))
#define REMOVE_BITS(bitvector, bits)   ((bitvector) &= ~(bits))
#define IS_SET_ANY(bitvector, bits)    ((bitvector) & (bits))
#define IS_SET_ALL(bitvector, bits)    (((bitvector) & (bits)) == (bits))

#define IS_POWER_OF_2(num)             ((num) > 0 && ((num) & ((num) - 1)) == 0)

#endif /* #if !defined(TOKUDB_COMMON_H) */
