/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include <stdlib.h>
#include <stdio.h>
int main (int argc, char *const argv[]) {
    int i;
    for (i=0; i<1000; i++) {
	printf("%d\t%d\n", random(), random());
    }
    return 0;
}
