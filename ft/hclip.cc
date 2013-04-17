/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

// Clip horizontally (100 chars by default)
#include <stdio.h>
#include <stdlib.h>
int main (int argc, char *const argv[]) {
    int targcol=100;
    int c;
    int col=0;
    if (argc>1) {
	targcol=atoi(argv[1]);
    }
    while ((c=getchar())!=EOF) {
	if (c=='\n') { col=0; putchar(c); }
	else if (col==targcol) { col++; printf("..."); }
	else if (col>targcol) { col++; }
	else { col++; putchar(c); }
    }
    return 0;
}
