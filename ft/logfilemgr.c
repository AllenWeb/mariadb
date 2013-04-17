/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2010 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include "includes.h"

// for now, implement with singlely-linked-list
//   first = oldest  (delete from beginning)
//   last  = newest  (add to end)

struct lfm_entry {
    TOKULOGFILEINFO lf_info;
    struct lfm_entry *next;
};

struct toku_logfilemgr {
    struct lfm_entry *first;
    struct lfm_entry *last;
    int n_entries;
};

int toku_logfilemgr_create(TOKULOGFILEMGR *lfm) {
    // malloc a logfilemgr
    TOKULOGFILEMGR mgr = toku_xmalloc(sizeof(struct toku_logfilemgr));
    mgr->first = NULL;
    mgr->last = NULL;
    mgr->n_entries = 0;    
    *lfm = mgr;
    return 0;
}

int toku_logfilemgr_destroy(TOKULOGFILEMGR *lfm) {
    int r=0;
    if ( *lfm != NULL ) { // be tolerant of being passed a NULL
        TOKULOGFILEMGR mgr = *lfm;
        while ( mgr->n_entries > 0 ) {
            toku_logfilemgr_delete_oldest_logfile_info(mgr);
        }
        toku_free(*lfm);
        *lfm = NULL;
    }
    return r;
}

int toku_logfilemgr_init(TOKULOGFILEMGR lfm, const char *log_dir) {
    assert(lfm!=0);

    int r;
    int n_logfiles;
    char **logfiles;
    r = toku_logger_find_logfiles(log_dir, &logfiles, &n_logfiles);
    if (r!=0)
        return r;

    TOKULOGCURSOR cursor;
    struct log_entry *entry;
    TOKULOGFILEINFO lf_info;
    long long index = -1;
    char *basename;
    LSN tmp_lsn = {0};
    for(int i=0;i<n_logfiles;i++){
        lf_info = toku_xmalloc(sizeof(struct toku_logfile_info));
        // find the index
	// basename is the filename of the i-th logfile
        basename = strrchr(logfiles[i], '/') + 1;
        int version;
        r = sscanf(basename, "log%lld.tokulog%d", &index, &version);
        assert(r==2);  // found index and version
        assert(version>=TOKU_LOG_MIN_SUPPORTED_VERSION);
        assert(version<=TOKU_LOG_VERSION);
        lf_info->index = index;
        lf_info->version = version;
        // find last LSN in logfile
        r = toku_logcursor_create_for_file(&cursor, log_dir, basename);
        if (r!=0) 
            return r;
        r = toku_logcursor_last(cursor, &entry);  // set "entry" to last log entry in logfile
        if ( r == 0 ) {
            lf_info->maxlsn = toku_log_entry_get_lsn(entry);
            tmp_lsn = lf_info->maxlsn;
        }
        else {
            lf_info->maxlsn = tmp_lsn; // handle empty logfile (no LSN in file) case
        }

        // add to logfilemgr
        toku_logfilemgr_add_logfile_info(lfm, lf_info);
        toku_logcursor_destroy(&cursor);
    }
    for(int i=0;i<n_logfiles;i++) {
        toku_free(logfiles[i]);
    }
    toku_free(logfiles);
    return 0;
}

int toku_logfilemgr_num_logfiles(TOKULOGFILEMGR lfm) {
    assert(lfm);
    return lfm->n_entries;
}

int toku_logfilemgr_add_logfile_info(TOKULOGFILEMGR lfm, TOKULOGFILEINFO lf_info) {
    assert(lfm);
    struct lfm_entry *entry = toku_xmalloc(sizeof(struct lfm_entry));
    entry->lf_info = lf_info;
    entry->next = NULL;
    if ( lfm->n_entries != 0 )
        lfm->last->next = entry;
    lfm->last = entry;
    lfm->n_entries++;
    if (lfm->n_entries == 1 ) {
        lfm->first = lfm->last;
    }
    return 0;
}

TOKULOGFILEINFO toku_logfilemgr_get_oldest_logfile_info(TOKULOGFILEMGR lfm) {
    assert(lfm);
    return lfm->first->lf_info;
}

void toku_logfilemgr_delete_oldest_logfile_info(TOKULOGFILEMGR lfm) {
    assert(lfm);
    if ( lfm->n_entries > 0 ) {
        struct lfm_entry *entry = lfm->first;
        toku_free(entry->lf_info);
        lfm->first = entry->next;
        toku_free(entry);
        lfm->n_entries--;
        if ( lfm->n_entries == 0 ) {
            lfm->last = lfm->first = NULL;
        }
    }
}

LSN toku_logfilemgr_get_last_lsn(TOKULOGFILEMGR lfm) {
    assert(lfm);
    if ( lfm->n_entries == 0 ) {
        LSN lsn;
        lsn.lsn = 0;
        return lsn;
    }
    return lfm->last->lf_info->maxlsn;
}

void toku_logfilemgr_update_last_lsn(TOKULOGFILEMGR lfm, LSN lsn) {
    assert(lfm);
    assert(lfm->last!=NULL);
    lfm->last->lf_info->maxlsn = lsn;
}

void toku_logfilemgr_print(TOKULOGFILEMGR lfm) {
    assert(lfm);
    printf("toku_logfilemgr_print [%p] : %d entries \n", lfm, lfm->n_entries);
    struct lfm_entry *entry = lfm->first;
    for (int i=0;i<lfm->n_entries;i++) {
        printf("  entry %d : index = %"PRId64", maxlsn = %"PRIu64"\n", i, entry->lf_info->index, entry->lf_info->maxlsn.lsn);
        entry = entry->next;
    }
}
