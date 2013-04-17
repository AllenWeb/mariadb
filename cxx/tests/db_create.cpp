/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#include <assert.h>
#include <db_cxx.h>

char *data_dir, *env_dir=NULL;

static int dbcreate(char *dbfile, char *dbname, int dbflags, int argc, char *argv[]) {
    int r;
    DbEnv *env = new DbEnv(DB_CXX_NO_EXCEPTIONS);
    if (data_dir) {
        r = env->set_data_dir(data_dir); assert(r == 0);
    }
    r = env->set_redzone(0); assert(r==0);
    r = env->open(env_dir ? env_dir : ".", DB_INIT_MPOOL + DB_CREATE + DB_PRIVATE, 0777); assert(r == 0);

    Db *db = new Db(env, DB_CXX_NO_EXCEPTIONS);
    r = db->set_flags(dbflags); assert(r == 0);
    r = db->open(0, dbfile, dbname, DB_BTREE, DB_CREATE, 0777);
    if (r != 0) {
        printf("db->open %s(%s) %d %s\n", dbfile, dbname, r, db_strerror(r));
	db->close(0);  delete db;
        env->close(0); delete env;
        return 1;
    }

    int i = 0;
    while (i < argc) {
        char *k = argv[i++];
        if (i < argc) {
            char *v = argv[i++];
            Dbt key(k, strlen(k)); Dbt val(v, strlen(v));
            r = db->put(0, &key, &val, 0); assert(r == 0);
        }
    }
            
    r = db->close(0); assert(r == 0);
    delete db;
    r = env->close(0); assert(r == 0);
    delete env;
    
    return 0;
}

static int usage() {
    fprintf(stderr, "db_create [-s DBNAME] DBFILE [KEY VAL]*\n");
    fprintf(stderr, "[--set_data_dir DIRNAME]\n");
    return 1;
}

int main(int argc, char *argv[]) {
    char *dbname = 0;
    int dbflags = 0;

    int i;
    for (i=1; i<argc; i++) {
        char *arg = argv[i];
        if (0 == strcmp(arg, "-h") || 0 == strcmp(arg, "--help"))
            return usage();
        if (0 == strcmp(arg, "-s")) {
            if (i+1 >= argc)
                return usage();
            dbname = argv[++i];
            continue;
        }
	if (0 == strcmp(arg, "--env_dir")) {
            if (i+1 >= argc)
                return usage();
	    env_dir = argv[++i];
	    continue;
	}
        if (0 == strcmp(arg, "--set_data_dir")) {
            if (i+1 >= argc)
                return usage();
            data_dir = argv[++i];
            continue;
        }
	if (arg[0]=='-') {
	    printf("I don't understand this argument: %s\n", arg);
	    return 1;
	}
        break;
    }

    if (i >= argc)
        return usage();
    char *dbfile = argv[i++];
    return dbcreate(dbfile, dbname, dbflags, argc-i, &argv[i]);
}

