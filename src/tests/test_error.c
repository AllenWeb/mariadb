/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."
#include "test.h"
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

char const* expect_errpfx;
int n_handle_error=0;

static void
handle_error (const DB_ENV *UU(dbenv), const char *errpfx, const char *UU(msg)) {
    assert(errpfx==expect_errpfx);
    n_handle_error++;
}
int
test_main (int argc, const char *argv[]) {
    parse_args(argc, argv);

#if defined(OSX)
    if (verbose) printf("Warning: fmemopen does not exist in OSX!\n");
#else
    
    system("rm -rf " ENVDIR);
    int r=toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO); assert(r==0);

    {
	DB_ENV *env;
	r = db_env_create(&env, 0); assert(r==0);
	env->set_errfile(env,0); // Turn off those annoying errors
	r = env->open(env, ENVDIR, -1, 0644);
	assert(r==EINVAL);
	assert(n_handle_error==0);
	r = env->close(env, 0); assert(r==0);
    }

    int do_errfile, do_errcall,do_errpfx;
    for (do_errpfx=0; do_errpfx<2; do_errpfx++) {
	for (do_errfile=0; do_errfile<2; do_errfile++) {
	    for (do_errcall=0; do_errcall<2; do_errcall++) {
		char errfname[] = __FILE__ ".errs";
		unlink(errfname);
		{
		    DB_ENV *env;
		    FILE *write_here = fopen(errfname, "w");
		    assert(write_here);
		    n_handle_error=0;
		    r = db_env_create(&env, 0); assert(r==0);
		    if (do_errpfx) {
			expect_errpfx="whoopi";
			env->set_errpfx(env, expect_errpfx);
		    } else {
			expect_errpfx=0;
		    }
		    env->set_errfile(env,0); // Turn off those annoying errors
		    if (do_errfile)
			env->set_errfile(env, write_here);
		    if (do_errcall) 
			env->set_errcall(env, handle_error);
		    r = env->open(env, ENVDIR, -1, 0644);
		    assert(r==EINVAL);
		    r = env->close(env, 0); assert(r==0);
		    fclose(write_here);
		}
		{
		    FILE *read_here = fopen(errfname, "r");
		    assert(read_here);
		    char buf[10000];
		    int buflen = fread(buf, 1, sizeof(buf)-1, read_here);
		    assert(buflen>=0);
		    buf[buflen]=0;
		    if (do_errfile) {
			if (do_errpfx) {
			    assert(strncmp(buf,"whoopi:",6)==0);
			} else {
			    assert(buf[0]!=0); 
			    assert(buf[0]!=':');
			}
			assert(buf[strlen(buf)-1]=='\n');
		    } else {
			assert(buf[0]==0);
		    }
		    if (do_errcall) {
			assert(n_handle_error==1);
		    } else {
			assert(n_handle_error==0);
		    }
		    fclose(read_here);
		}
		unlink(errfname);
	    }
	}
    }
#endif
    return 0;
}
