/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include "includes.h"

void wbuf_LEAFENTRY(struct wbuf *w, LEAFENTRY le) {
    wbuf_literal_bytes(w, le, leafentry_disksize(le));
}

void wbuf_nocrc_LEAFENTRY(struct wbuf *w, LEAFENTRY le) {
    wbuf_nocrc_literal_bytes(w, le, leafentry_disksize(le));
}
