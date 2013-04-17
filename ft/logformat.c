/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2011 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

/* This file defines the logformat in an executable fashion.
 * This code is used to generate
 *   The code that writes into the log.
 *   The code that reads the log and prints it to stdout (the log_print utility)
 *   The code that reads the log for recovery.
 *   The struct definitions.
 *   The Latex documentation.
 */
#include <toku_portability.h>
#include <toku_assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


typedef struct field {
    char *type;
    char *name;
    char *format; // optional format string
} F;

#define NULLFIELD {0,0,0}
#define FA (F[])

struct logtype {
    char *name;
    unsigned int command_and_flags;
    struct field *fields;
};

// In the fields, don't mention the command, the LSN, the CRC or the trailing LEN.

const struct logtype rollbacks[] = {
    //TODO: #2037 Add dname
    {"fdelete", 'U', FA{{"u_int8_t",   "file_was_open", 0},
			{"FILENUM",    "filenum", 0},
			{"BYTESTRING", "iname", 0},
			NULLFIELD}},
    //TODO: #2037 Add dname
    {"fcreate", 'F', FA{{"FILENUM", "filenum", 0},
			{"BYTESTRING", "iname", 0},
			NULLFIELD}},
    // cmdinsert is used to insert a key-value pair into a NODUP DB.  For rollback we don't need the data.
    {"cmdinsert", 'i', FA{
			  {"FILENUM", "filenum", 0},
			  {"BYTESTRING", "key", 0},
			  NULLFIELD}},
    {"cmddelete", 'd', FA{
			  {"FILENUM", "filenum", 0},
			  {"BYTESTRING", "key", 0},
			  NULLFIELD}},
    {"rollinclude", 'r', FA{{"TXNID",     "xid", 0},
                            {"u_int64_t", "num_nodes", 0},
                            {"BLOCKNUM",  "spilled_head", 0},
                            {"u_int32_t", "spilled_head_hash", 0},
                            {"BLOCKNUM",  "spilled_tail", 0},
                            {"u_int32_t", "spilled_tail_hash", 0},
                            NULLFIELD}},
    {"load", 'l', FA{{"BYTESTRING", "old_iname", 0},
                     {"BYTESTRING", "new_iname", 0},
                     NULLFIELD}},
    // #2954
    {"hot_index", 'h', FA{{"FILENUMS",  "hot_index_filenums", 0},
                          NULLFIELD}},
    {"dictionary_redirect", 'R', FA{{"FILENUM", "old_filenum", 0},
                                    {"FILENUM", "new_filenum", 0},
                                    NULLFIELD}},
    {"cmdupdate", 'u', FA{{"FILENUM", "filenum", 0},
                          {"BYTESTRING", "key", 0},
                          NULLFIELD}},
    {"cmdupdatebroadcast", 'B', FA{{"FILENUM", "filenum", 0},
                                   {"BOOL",    "is_resetting_op", 0},
                                   NULLFIELD}},
    {"change_fdescriptor", 'D', FA{{"FILENUM",    "filenum", 0},
                            {"BYTESTRING", "old_descriptor", 0},
                            NULLFIELD}},
    {0,0,FA{NULLFIELD}}
};

const struct logtype logtypes[] = {
    // Records produced by checkpoints
#if 0 // no longer used, but reserve the type
    {"local_txn_checkpoint", 'c', FA{{"TXNID",      "xid", 0}, NULLFIELD}},
#endif
    {"begin_checkpoint", 'x', FA{{"u_int64_t", "timestamp", 0}, NULLFIELD}},
    {"end_checkpoint",   'X', FA{{"TXNID", "xid", 0},           // xid is LSN of begin_checkpoint
				 {"u_int64_t", "timestamp", 0},
				 {"u_int32_t", "num_fassociate_entries", 0}, // how many files were checkpointed
				 {"u_int32_t", "num_xstillopen_entries", 0},  // how many txns  were checkpointed
				 NULLFIELD}},  
    //TODO: #2037 Add dname
    {"fassociate",  'f', FA{{"FILENUM", "filenum", 0},
                            {"u_int32_t",  "treeflags", 0},
			    {"BYTESTRING", "iname", 0},   // pathname of file
			    NULLFIELD}},
    //We do not use a TXNINFO struct since recovery log has
    //FILENUMS and TOKUTXN has FTs (for open_fts)
    {"xstillopen", 's', FA{{"TXNID", "xid", 0}, 
                           {"TXNID", "parentxid", 0}, 
                           {"u_int64_t", "rollentry_raw_count", 0}, 
                           {"FILENUMS",  "open_filenums", 0},
                           {"u_int8_t",  "force_fsync_on_commit", 0}, 
                           {"u_int64_t", "num_rollback_nodes", 0}, 
                           {"u_int64_t", "num_rollentries", 0}, 
                           {"BLOCKNUM",  "spilled_rollback_head", 0}, 
                           {"BLOCKNUM",  "spilled_rollback_tail", 0}, 
                           {"BLOCKNUM",  "current_rollback", 0}, 
                           NULLFIELD}}, // record all transactions
    // prepared txns need a gid
    {"xstillopenprepared", 'p', FA{{"TXNID", "xid", 0},
				   {"XIDP",  "xa_xid", 0}, // prepared transactions need a gid, and have no parentxid.
				   {"u_int64_t", "rollentry_raw_count", 0}, 
				   {"FILENUMS",  "open_filenums", 0},
				   {"u_int8_t",  "force_fsync_on_commit", 0}, 
				   {"u_int64_t", "num_rollback_nodes", 0}, 
				   {"u_int64_t", "num_rollentries", 0}, 
				   {"BLOCKNUM",  "spilled_rollback_head", 0}, 
				   {"BLOCKNUM",  "spilled_rollback_tail", 0}, 
				   {"BLOCKNUM",  "current_rollback", 0}, 
				   NULLFIELD}}, // record all transactions
    {"suppress_rollback", 'S', FA{{"FILENUM",    "filenum", 0},
                                  {"TXNID",      "xid", 0},
                                  NULLFIELD}},
    // Records produced by transactions
    {"xbegin", 'b', FA{{"TXNID", "parentxid", 0},NULLFIELD}},
    {"xcommit",'C', FA{{"TXNID", "xid", 0},NULLFIELD}},
    {"xprepare",'P', FA{{"TXNID", "xid", 0}, {"XIDP", "xa_xid", 0}, NULLFIELD}},
    {"xabort", 'q', FA{{"TXNID", "xid", 0},NULLFIELD}},
    //TODO: #2037 Add dname
    {"fcreate", 'F', FA{{"TXNID",      "xid", 0},
                        {"FILENUM",    "filenum", 0},
			{"BYTESTRING", "iname", 0},
			{"u_int32_t",  "mode",  "0%o"},
                        {"u_int32_t",  "treeflags", 0},
                        {"u_int32_t", "nodesize", 0},
                        {"u_int32_t", "basementnodesize", 0},
                        {"u_int32_t", "compression_method", 0},
			NULLFIELD}},
    //TODO: #2037 Add dname
    {"fopen",   'O', FA{{"BYTESTRING", "iname", 0},
			{"FILENUM",    "filenum", 0},
                        {"u_int32_t",  "treeflags", 0},
			NULLFIELD}},
    //TODO: #2037 Add dname
    {"fclose",   'e', FA{{"BYTESTRING", "iname", 0},
                         {"FILENUM",    "filenum", 0},
                         NULLFIELD}},
    //TODO: #2037 Add dname
    {"fdelete", 'U', FA{{"TXNID",      "xid", 0},
			{"BYTESTRING", "iname", 0},
			NULLFIELD}},
    {"enq_insert", 'I', FA{{"FILENUM",    "filenum", 0},
                           {"TXNID",      "xid", 0},
                           {"BYTESTRING", "key", 0},
                           {"BYTESTRING", "value", 0},
                           NULLFIELD}},
    {"enq_insert_no_overwrite", 'i', FA{{"FILENUM",    "filenum", 0},
                                        {"TXNID",      "xid", 0},
                                        {"BYTESTRING", "key", 0},
                                        {"BYTESTRING", "value", 0},
                                        NULLFIELD}},
    {"enq_delete_any", 'E', FA{{"FILENUM",    "filenum", 0},
                               {"TXNID",      "xid", 0},
                               {"BYTESTRING", "key", 0},
                               NULLFIELD}},
    {"enq_insert_multiple", 'm', FA{{"FILENUM",    "src_filenum", 0},
                                    {"FILENUMS",   "dest_filenums", 0},
                                    {"TXNID",      "xid", 0},
                                    {"BYTESTRING", "src_key", 0},
                                    {"BYTESTRING", "src_val", 0},
                                    NULLFIELD}},
    {"enq_delete_multiple", 'M', FA{{"FILENUM",    "src_filenum", 0},
                                    {"FILENUMS",   "dest_filenums", 0},
                                    {"TXNID",      "xid", 0},
                                    {"BYTESTRING", "src_key", 0},
                                    {"BYTESTRING", "src_val", 0},
                                    NULLFIELD}},
    {"comment", 'T', FA{{"u_int64_t", "timestamp", 0},
                        {"BYTESTRING", "comment", 0},
                        NULLFIELD}},
    {"shutdown", 'Q', FA{{"u_int64_t", "timestamp", 0},
                         NULLFIELD}},
    {"load", 'l', FA{{"TXNID",      "xid", 0},
                     {"BYTESTRING", "old_iname", 0},
                     {"BYTESTRING", "new_iname", 0},
                     NULLFIELD}},
    // #2954
    {"hot_index", 'h', FA{{"TXNID",     "xid", 0},
                          {"FILENUMS",  "hot_index_filenums", 0},
                          NULLFIELD}},
    {"enq_update", 'u', FA{{"FILENUM",    "filenum", 0},
                           {"TXNID",      "xid", 0},
                           {"BYTESTRING", "key", 0},
                           {"BYTESTRING", "extra", 0},
                           NULLFIELD}},
    {"enq_updatebroadcast", 'B', FA{{"FILENUM",    "filenum", 0},
                                    {"TXNID",      "xid", 0},
                                    {"BYTESTRING", "extra", 0},
                                    {"BOOL",       "is_resetting_op", 0},
                                    NULLFIELD}},
    {"change_fdescriptor", 'D', FA{{"FILENUM",    "filenum", 0},
                            {"TXNID",      "xid", 0},
                            {"BYTESTRING", "old_descriptor", 0},
                            {"BYTESTRING", "new_descriptor", 0},
                            {"BOOL",       "update_cmp_descriptor", 0},
                            NULLFIELD}},
    {0,0,FA{NULLFIELD}}
};


#define DO_STRUCTS(lt, array, body) do {	\
    const struct logtype *lt;    \
    for (lt=&array[0]; lt->name; lt++) {	\
	body; \
    } } while (0)

#define DO_ROLLBACKS(lt, body) DO_STRUCTS(lt, rollbacks, body)

#define DO_LOGTYPES(lt, body) DO_STRUCTS(lt, logtypes, body)

#define DO_LOGTYPES_AND_ROLLBACKS(lt, body) (DO_ROLLBACKS(lt,body), DO_LOGTYPES(lt, body))

#define DO_FIELDS(fld, lt, body) do { \
    struct field *fld; \
    for (fld=lt->fields; fld->type; fld++) { \
        body; \
    } } while (0)


static void __attribute__((format (printf, 3, 4))) fprintf2 (FILE *f1, FILE *f2, const char *format, ...) {
    va_list ap;
    int r;
    va_start(ap, format);
    r=vfprintf(f1, format, ap); assert(r>=0);
    va_end(ap);
    va_start(ap, format);
    r=vfprintf(f2, format, ap); assert(r>=0);
    va_end(ap);
}

FILE *hf=0, *cf=0, *pf=0;

static void
generate_enum_internal (char *enum_name, char *enum_prefix, const struct logtype *lts) {
    char used_cmds[256];
    int count=0;
    memset(used_cmds, 0, 256);
    fprintf(hf, "enum %s {", enum_name);
    DO_STRUCTS(lt, lts,
		{
		    unsigned char cmd = (unsigned char)(lt->command_and_flags&0xff);
		    if (count!=0) fprintf(hf, ",");
		    count++;
		    fprintf(hf, "\n");
		    fprintf(hf,"    %s_%-16s = '%c'", enum_prefix, lt->name, cmd);
		    if (used_cmds[cmd]!=0) { fprintf(stderr, "%s:%d: error: Command %d (%c) was used twice (second time for %s)\n", __FILE__, __LINE__, cmd, cmd, lt->name); abort(); }
		    used_cmds[cmd]=1;
		});
    fprintf(hf, "\n};\n\n");

}

static void
generate_enum (void) {
    generate_enum_internal("lt_cmd", "LT", logtypes);
    generate_enum_internal("rt_cmd", "RT", rollbacks);
}

static void
generate_log_struct (void) {
    DO_LOGTYPES(lt,
		{  fprintf(hf, "struct logtype_%s {\n", lt->name);
		    fprintf(hf, "  %-16s lsn;\n", "LSN");
		    DO_FIELDS(field_type, lt,
			      fprintf(hf, "  %-16s %s;\n", field_type->type, field_type->name));
		    fprintf(hf, "  %-16s crc;\n", "u_int32_t");
		    fprintf(hf, "  %-16s len;\n", "u_int32_t");
		    fprintf(hf, "};\n");
		    //fprintf(hf, "void toku_recover_%s (LSN lsn", lt->name);
		    //DO_FIELDS(field_type, lt, fprintf(hf, ", %s %s", field_type->type, field_type->name));
		    //fprintf(hf, ");\n");
		});
    DO_ROLLBACKS(lt,
		 {  fprintf(hf, "struct rolltype_%s {\n", lt->name);
		     DO_FIELDS(field_type, lt,
			       fprintf(hf, "  %-16s %s;\n", field_type->type, field_type->name));
		     fprintf(hf, "};\n");
		     fprintf(hf, "int toku_rollback_%s (", lt->name);
		     DO_FIELDS(field_type, lt, fprintf(hf, "%s %s,", field_type->type, field_type->name));
		     fprintf(hf, "TOKUTXN txn, YIELDF yield, void*yield_v, LSN oplsn);\n");
		     fprintf(hf, "int toku_commit_%s (", lt->name);
		     DO_FIELDS(field_type, lt, fprintf(hf, "%s %s,", field_type->type, field_type->name));
		     fprintf(hf, "TOKUTXN txn, YIELDF yield, void*yield_v, LSN oplsn);\n");
		 });
    fprintf(hf, "struct log_entry {\n");
    fprintf(hf, "  enum lt_cmd cmd;\n");
    fprintf(hf, "  union {\n");
    DO_LOGTYPES(lt, fprintf(hf,"    struct logtype_%s %s;\n", lt->name, lt->name));
    fprintf(hf, "  } u;\n");
    fprintf(hf, "};\n");

    fprintf(hf, "struct roll_entry {\n");
    fprintf(hf, "  enum rt_cmd cmd;\n");
    fprintf(hf, "  struct roll_entry *prev; /* for in-memory list of log entries.  Threads from newest to oldest. */\n");
    fprintf(hf, "  union {\n");
    DO_ROLLBACKS(lt, fprintf(hf,"    struct rolltype_%s %s;\n", lt->name, lt->name));
    fprintf(hf, "  } u;\n");
    fprintf(hf, "};\n");

}

static void
generate_dispatch (void) {
    fprintf(hf, "#define rolltype_dispatch(s, funprefix) ({ switch((s)->cmd) {\\\n");
    DO_ROLLBACKS(lt, fprintf(hf, "  case RT_%s: funprefix ## %s (&(s)->u.%s); break;\\\n", lt->name, lt->name, lt->name));
    fprintf(hf, " }})\n");

    fprintf(hf, "#define logtype_dispatch_assign(s, funprefix, var, ...) do { switch((s)->cmd) {\\\n");
    DO_LOGTYPES(lt, fprintf(hf, "  case LT_%s: var = funprefix ## %s (&(s)->u.%s, __VA_ARGS__); break;\\\n", lt->name, lt->name, lt->name));
    fprintf(hf, " }} while (0)\n");

    fprintf(hf, "#define rolltype_dispatch_assign(s, funprefix, var, ...) do { \\\n");
    fprintf(hf, "  switch((s)->cmd) {\\\n");
    DO_ROLLBACKS(lt, {
		    fprintf(hf, "  case RT_%s: var = funprefix ## %s (", lt->name, lt->name);
		    int fieldcount=0;
		    DO_FIELDS(field_type, lt, {
				if (fieldcount>0) fprintf(hf, ",");
				fprintf(hf, "(s)->u.%s.%s", lt->name, field_type->name);
				fieldcount++;
			    });
		    fprintf(hf, ", __VA_ARGS__); break;\\\n");
		});
    fprintf(hf, "  default: assert(0);} } while (0)\n");

    fprintf(hf, "#define logtype_dispatch_args(s, funprefix, ...) do { switch((s)->cmd) {\\\n");
    DO_LOGTYPES(lt,
		{
		    fprintf(hf, "  case LT_%s: funprefix ## %s ((s)->u.%s.lsn", lt->name, lt->name, lt->name);
		    DO_FIELDS(field_type, lt, fprintf(hf, ",(s)->u.%s.%s", lt->name, field_type->name));
		    fprintf(hf, ", __VA_ARGS__); break;\\\n");
		});
    fprintf(hf, " }} while (0)\n");
}

static void
generate_get_timestamp(void) {
    fprintf(cf, "static u_int64_t toku_get_timestamp(void) {\n");
    fprintf(cf, "  struct timeval tv; int r = gettimeofday(&tv, NULL);\n");
    fprintf(cf, "  assert(r==0);\n");
    fprintf(cf, "  return tv.tv_sec * 1000000ULL + tv.tv_usec;\n");
    fprintf(cf, "}\n");
}
		
static void
generate_log_writer (void) {
    fprintf(cf, "static u_int64_t toku_lsn_increment=1;\nvoid toku_set_lsn_increment (uint64_t incr) { assert(incr>0 && incr< (16LL<<32)); toku_lsn_increment=incr; }\n");
    generate_get_timestamp();
    DO_LOGTYPES(lt, {
            fprintf(hf, "static const size_t toku_log_%s_overhead = (+4+1+8", lt->name);
            DO_FIELDS(field_type, lt, fprintf(hf, "+%lu", sizeof (field_type->type)));
            fprintf(hf, "+8);\n");
			fprintf2(cf, hf, "int toku_log_%s (TOKULOGGER logger, LSN *lsnp, int do_fsync", lt->name);
			DO_FIELDS(field_type, lt, fprintf2(cf, hf, ", %s %s", field_type->type, field_type->name));
			fprintf(hf, ");\n");
			fprintf(cf, ") {\n");
			fprintf(cf, "  int r = 0;\n");
			fprintf(cf, "  if (logger==0) return 0;\n");
			fprintf(cf, "  if (!logger->write_log_files) {\n");
			fprintf(cf, "    ml_lock(&logger->input_lock);\n");
			fprintf(cf, "    logger->lsn.lsn += toku_lsn_increment;\n");
			fprintf(cf, "    if (lsnp) *lsnp=logger->lsn;\n");
			fprintf(cf, "    ml_unlock(&logger->input_lock);\n");
			fprintf(cf, "    return 0;\n");
			fprintf(cf, "  }\n");
			fprintf(cf, "  const unsigned int buflen= (+4 // len at the beginning\n");
			fprintf(cf, "                              +1 // log command\n");
			fprintf(cf, "                              +8 // lsn\n");
			DO_FIELDS(field_type, lt,
				  fprintf(cf, "                              +toku_logsizeof_%s(%s)\n", field_type->type, field_type->name));
			fprintf(cf, "                              +8 // crc + len\n");
			fprintf(cf, "                     );\n");
			fprintf(cf, "  struct wbuf wbuf;\n");
			fprintf(cf, "  ml_lock(&logger->input_lock);\n");
			fprintf(cf, "  r = toku_logger_make_space_in_inbuf(logger, buflen);\n");
			fprintf(cf, "  if (r!=0) goto panic;\n");
			fprintf(cf, "  wbuf_nocrc_init(&wbuf, logger->inbuf.buf+logger->inbuf.n_in_buf, buflen);\n");
			fprintf(cf, "  wbuf_nocrc_int(&wbuf, buflen);\n");
			fprintf(cf, "  wbuf_nocrc_char(&wbuf, '%c');\n", (char)(0xff&lt->command_and_flags));
			fprintf(cf, "  logger->lsn.lsn += toku_lsn_increment;\n");
			fprintf(cf, "  LSN lsn =logger->lsn;\n");
			fprintf(cf, "  logger->inbuf.max_lsn_in_buf = lsn;\n");
			fprintf(cf, "  wbuf_nocrc_LSN(&wbuf, lsn);\n");
			fprintf(cf, "  if (lsnp) *lsnp=lsn;\n");
			DO_FIELDS(field_type, lt,
                                  if (strcmp(field_type->name, "timestamp") == 0)
                                      fprintf(cf, "  if (timestamp == 0) timestamp = toku_get_timestamp();\n");
				  fprintf(cf, "  wbuf_nocrc_%s(&wbuf, %s);\n", field_type->type, field_type->name));
			fprintf(cf, "  wbuf_nocrc_int(&wbuf, x1764_memory(wbuf.buf, wbuf.ndone));\n");
			fprintf(cf, "  wbuf_nocrc_int(&wbuf, buflen);\n");
			fprintf(cf, "  assert(wbuf.ndone==buflen);\n");
			fprintf(cf, "  logger->inbuf.n_in_buf += buflen;\n");
			fprintf(cf, "  r = toku_logger_maybe_fsync(logger, lsn, do_fsync);\n");
			fprintf(cf, "  if (r!=0) goto panic;\n");
			fprintf(cf, "  return 0;\n");
			fprintf(cf, " panic:\n");
			fprintf(cf, "  toku_logger_panic(logger, r);\n");
			fprintf(cf, "  return r;\n");
			fprintf(cf, "}\n\n");
		    });
}

static void
generate_log_reader (void) {
    DO_LOGTYPES(lt, {
			fprintf(cf, "static int toku_log_fread_%s (FILE *infile, u_int32_t len1, struct logtype_%s *data, struct x1764 *checksum)", lt->name, lt->name);
			fprintf(cf, " {\n");
			fprintf(cf, "  int r=0;\n");
			fprintf(cf, "  u_int32_t actual_len=5; // 1 for the command, 4 for the first len.\n");
			fprintf(cf, "  r=toku_fread_%-16s(infile, &data->%-16s, checksum, &actual_len); if (r!=0) return r;\n", "LSN", "lsn");
			DO_FIELDS(field_type, lt,
				  fprintf(cf, "  r=toku_fread_%-16s(infile, &data->%-16s, checksum, &actual_len); if (r!=0) return r;\n", field_type->type, field_type->name));
			fprintf(cf, "  u_int32_t checksum_in_file, len_in_file;\n");
			fprintf(cf, "  r=toku_fread_u_int32_t_nocrclen(infile, &checksum_in_file); actual_len+=4;   if (r!=0) return r;\n");
			fprintf(cf, "  r=toku_fread_u_int32_t_nocrclen(infile, &len_in_file);    actual_len+=4;   if (r!=0) return r;\n");
			fprintf(cf, "  if (checksum_in_file!=x1764_finish(checksum) || len_in_file!=actual_len || len1 != len_in_file) return DB_BADFORMAT;\n");
			fprintf(cf, "  return 0;\n");
			fprintf(cf, "}\n\n");
		    });
    fprintf2(cf, hf, "int toku_log_fread (FILE *infile, struct log_entry *le)");
    fprintf(hf, ";\n");
    fprintf(cf, " {\n");
    fprintf(cf, "  u_int32_t len1; int r;\n");
    fprintf(cf, "  u_int32_t ignorelen=0;\n");
    fprintf(cf, "  struct x1764 checksum;\n");
    fprintf(cf, "  x1764_init(&checksum);\n");
    fprintf(cf, "  r = toku_fread_u_int32_t(infile, &len1, &checksum, &ignorelen); if (r!=0) return r;\n");
    fprintf(cf, "  int cmd=fgetc(infile);\n");
    fprintf(cf, "  if (cmd==EOF) return EOF;\n");
    fprintf(cf, "  char cmdchar = (char)cmd;\n");
    fprintf(cf, "  x1764_add(&checksum, &cmdchar, 1);\n");
    fprintf(cf, "  le->cmd=(enum lt_cmd)cmd;\n");
    fprintf(cf, "  switch ((enum lt_cmd)cmd) {\n");
    DO_LOGTYPES(lt, {
			fprintf(cf, "  case LT_%s:\n", lt->name);
			fprintf(cf, "    return toku_log_fread_%s (infile, len1, &le->u.%s, &checksum);\n", lt->name, lt->name);
		    });
    fprintf(cf, "  };\n");
    fprintf(cf, "  return DB_BADFORMAT;\n"); // Should read past the record using the len field.
    fprintf(cf, "}\n\n");
    //fprintf2(cf, hf, "// Return 0 if there is something to read, return -1 if nothing to read, abort if an error.\n");
    fprintf2(cf, hf, "// Return 0 if there is something to read, -1 if nothing to read, >0 on error\n");
    fprintf2(cf, hf, "int toku_log_fread_backward (FILE *infile, struct log_entry *le)");
    fprintf(hf, ";\n");
    fprintf(cf, "{\n");
    fprintf(cf, "  memset(le, 0, sizeof(*le));\n");
    fprintf(cf, "  {\n    long pos = ftell(infile);\n    if (pos<=12) return -1;\n  }\n");
    fprintf(cf, "  int r = fseek(infile, -4, SEEK_CUR); \n");//              assert(r==0);\n");
    fprintf(cf, "  if (r!=0) return errno;\n");
    fprintf(cf, "  u_int32_t len;\n");
    fprintf(cf, "  r = toku_fread_u_int32_t_nocrclen(infile, &len); \n");//  assert(r==0);\n");
    fprintf(cf, "  if (r!=0) return 1;\n");
    fprintf(cf, "  r = fseek(infile, -(int)len, SEEK_CUR) ;  \n");//         assert(r==0);\n");
    fprintf(cf, "  if (r!=0) return errno;\n");
    fprintf(cf, "  r = toku_log_fread(infile, le); \n");//                   assert(r==0);\n");
    fprintf(cf, "  if (r!=0) return 1;\n");
    fprintf(cf, "  r = fseek(infile, -(int)len, SEEK_CUR); \n");//           assert(r==0);\n");
    fprintf(cf, "  if (r!=0) return errno;\n");
    fprintf(cf, "  return 0;\n");
    fprintf(cf, "}\n\n");

    DO_LOGTYPES(lt, ({
            fprintf(cf, "static void toku_log_free_log_entry_%s_resources (struct logtype_%s *data", lt->name, lt->name);
	    if (!lt->fields->type) fprintf(cf, " __attribute__((__unused__))");
            fprintf(cf, ") {\n");
            DO_FIELDS(field_type, lt,
		      fprintf(cf, "    toku_free_%s(data->%s);\n", field_type->type, field_type->name);
		      );
	    fprintf(cf, "}\n\n");
	    }));
    fprintf2(cf, hf, "void toku_log_free_log_entry_resources (struct log_entry *le)");
    fprintf(hf, ";\n");
    fprintf(cf, " {\n");
    fprintf(cf, "    switch ((enum lt_cmd)le->cmd) {\n");
    DO_LOGTYPES(lt, {
            fprintf(cf, "    case LT_%s:\n", lt->name);
            fprintf(cf, "        return toku_log_free_log_entry_%s_resources (&(le->u.%s));\n", lt->name, lt->name);
        });
    fprintf(cf, "    };\n");
    fprintf(cf, "    return;\n");
    fprintf(cf, "}\n\n");
}

static void
generate_logprint (void) {
    unsigned maxnamelen=0;
    fprintf2(pf, hf, "int toku_logprint_one_record(FILE *outf, FILE *f)");
    fprintf(hf, ";\n");
    fprintf(pf, " {\n");
    fprintf(pf, "    int cmd, r;\n");
    fprintf(pf, "    u_int32_t len1, crc_in_file;\n");
    fprintf(pf, "    u_int32_t ignorelen=0;\n");
    fprintf(pf, "    struct x1764 checksum;\n");
    fprintf(pf, "    x1764_init(&checksum);\n");
    fprintf(pf, "    r=toku_fread_u_int32_t(f, &len1, &checksum, &ignorelen);\n");
    fprintf(pf, "    if (r==EOF) return EOF;\n");
    fprintf(pf, "    cmd=fgetc(f);\n");
    fprintf(pf, "    if (cmd==EOF) return DB_BADFORMAT;\n");
    fprintf(pf, "    u_int32_t len_in_file, len=1+4; // cmd + len1\n");
    fprintf(pf, "    char charcmd = (char)cmd;\n");
    fprintf(pf, "    x1764_add(&checksum, &charcmd, 1);\n");
    fprintf(pf, "    switch ((enum lt_cmd)cmd) {\n");
    DO_LOGTYPES(lt, { if (strlen(lt->name)>maxnamelen) maxnamelen=strlen(lt->name); });
    DO_LOGTYPES(lt, {
		unsigned char cmd = (unsigned char)(0xff&lt->command_and_flags);
		fprintf(pf, "    case LT_%s: \n", lt->name);
		// We aren't using the log reader here because we want better diagnostics as soon as things go wrong.
		fprintf(pf, "        fprintf(outf, \"%%-%us \", \"%s\");\n", maxnamelen, lt->name);
		if (isprint(cmd)) fprintf(pf,"        fprintf(outf, \" '%c':\");\n", cmd);
		else                      fprintf(pf,"        fprintf(outf, \"0%03o:\");\n", cmd);
		fprintf(pf, "        r = toku_logprint_%-16s(outf, f, \"lsn\", &checksum, &len, 0);     if (r!=0) return r;\n", "LSN");
		DO_FIELDS(field_type, lt, {
			    fprintf(pf, "        r = toku_logprint_%-16s(outf, f, \"%s\", &checksum, &len,", field_type->type, field_type->name);
			    if (field_type->format) fprintf(pf, "\"%s\"", field_type->format);
			    else            fprintf(pf, "0");
			    fprintf(pf, "); if (r!=0) return r;\n");
			});
		fprintf(pf, "        {\n");
		fprintf(pf, "          u_int32_t actual_murmur = x1764_finish(&checksum);\n");
		fprintf(pf, "          r = toku_fread_u_int32_t_nocrclen (f, &crc_in_file); len+=4; if (r!=0) return r;\n");
		fprintf(pf, "          fprintf(outf, \" crc=%%08x\", crc_in_file);\n");
		fprintf(pf, "          if (crc_in_file!=actual_murmur) fprintf(outf, \" checksum=%%08x\", actual_murmur);\n");
		fprintf(pf, "          r = toku_fread_u_int32_t_nocrclen (f, &len_in_file); len+=4; if (r!=0) return r;\n");
		fprintf(pf, "          fprintf(outf, \" len=%%u\", len_in_file);\n");
		fprintf(pf, "          if (len_in_file!=len) fprintf(outf, \" actual_len=%%u\", len);\n");
		fprintf(pf, "          if (len_in_file!=len || crc_in_file!=actual_murmur) return DB_BADFORMAT;\n");
		fprintf(pf, "        };\n");
		fprintf(pf, "        fprintf(outf, \"\\n\");\n");
		fprintf(pf, "        return 0;;\n\n");
	    });
    fprintf(pf, "    }\n");
    fprintf(pf, "    fprintf(outf, \"Unknown command %%d ('%%c')\", cmd, cmd);\n");
    fprintf(pf, "    return DB_BADFORMAT;\n");
    fprintf(pf, "}\n\n");
}

static void
generate_rollbacks (void) {
    DO_ROLLBACKS(lt, {
		    fprintf2(cf, hf, "int toku_logger_save_rollback_%s (TOKUTXN txn", lt->name);
		    DO_FIELDS(field_type, lt, {
                        if ( strcmp(field_type->type, "BYTESTRING") == 0 ) {
                            fprintf2(cf, hf, ", BYTESTRING *%s_ptr", field_type->name);
                        } 
                        else if ( strcmp(field_type->type, "FILENUMS") == 0 ) {
                            fprintf2(cf, hf, ", FILENUMS *%s_ptr", field_type->name);
                        }
                        else {
                            fprintf2(cf, hf, ", %s %s", field_type->type, field_type->name);
                        }
                    });

		    fprintf(hf, ");\n");
		    fprintf(cf, ") {\n");
                    fprintf(cf, "  ROLLBACK_LOG_NODE log;\n");
                    fprintf(cf, "  toku_get_and_pin_rollback_log_for_new_entry(txn, &log);\n");
		    // 'memdup' all BYTESTRINGS here
		    DO_FIELDS(field_type, lt, {
                        if ( strcmp(field_type->type, "BYTESTRING") == 0 ) {
                            fprintf(cf, "  BYTESTRING %s   = {\n"
                                    "    .len  = %s_ptr->len,\n"
                                    "    .data = toku_memdup_in_rollback(log, %s_ptr->data, %s_ptr->len)\n"
                                    "  };\n",
                                    field_type->name, field_type->name, field_type->name, field_type->name);
                        }
                        if ( strcmp(field_type->type, "FILENUMS") == 0 ) {
                            fprintf(cf, "  FILENUMS %s   = {\n"
                                    "    .num  = %s_ptr->num,\n"
                                    "    .filenums = toku_memdup_in_rollback(log, %s_ptr->filenums, %s_ptr->num * (sizeof (FILENUM)))\n"
                                    "  };\n",
                                    field_type->name, field_type->name, field_type->name, field_type->name);
                        }
                    });
		    {
			int count=0;
			fprintf(cf, "  u_int32_t rollback_fsize = toku_logger_rollback_fsize_%s(", lt->name);
			DO_FIELDS(field_type, lt, fprintf(cf, "%s%s", (count++>0)?", ":"", field_type->name));
			fprintf(cf, ");\n");
		    }
		    fprintf(cf, "  struct roll_entry *v;\n");
		    fprintf(cf, "  size_t mem_needed = sizeof(v->u.%s) + __builtin_offsetof(struct roll_entry, u.%s);\n", lt->name, lt->name);
		    fprintf(cf, "  v = toku_malloc_in_rollback(log, mem_needed);\n");
		    fprintf(cf, "  if (v==0) return errno;\n");
		    fprintf(cf, "  v->cmd = (enum rt_cmd)%u;\n", lt->command_and_flags&0xff);
		    DO_FIELDS(field_type, lt, fprintf(cf, "  v->u.%s.%s = %s;\n", lt->name, field_type->name, field_type->name));
		    fprintf(cf, "  v->prev = log->newest_logentry;\n");
		    fprintf(cf, "  if (log->oldest_logentry==NULL) log->oldest_logentry=v;\n");
		    fprintf(cf, "  log->newest_logentry = v;\n");
		    fprintf(cf, "  log->rollentry_resident_bytecount += rollback_fsize;\n");
		    fprintf(cf, "  txn->rollentry_raw_count          += rollback_fsize;\n");
                    fprintf(cf, "  txn->num_rollentries++;\n");
                    fprintf(cf, "  log->dirty = TRUE;\n");
		    fprintf(cf, "  // spill and unpin assert success internally\n");
		    fprintf(cf, "  toku_maybe_spill_rollbacks(txn, log);\n");
		    fprintf(cf, "  toku_rollback_log_unpin(txn, log);\n");
		    fprintf(cf, "  return 0;\n}\n");
	    });

    DO_ROLLBACKS(lt, {
		fprintf2(cf, hf, "void toku_logger_rollback_wbuf_nocrc_write_%s (struct wbuf *wbuf", lt->name);
		DO_FIELDS(field_type, lt, fprintf2(cf, hf, ", %s %s", field_type->type, field_type->name));
		fprintf2(cf, hf, ")");
		fprintf(hf, ";\n");
		fprintf(cf, " {\n");

                {
                    int count=0;
                    fprintf(cf, "  u_int32_t rollback_fsize = toku_logger_rollback_fsize_%s(", lt->name);
                    DO_FIELDS(field_type, lt, fprintf(cf, "%s%s", (count++>0)?", ":"", field_type->name));
                    fprintf(cf, ");\n");
                    fprintf(cf, "  wbuf_nocrc_int(wbuf, rollback_fsize);\n");
                }
		fprintf(cf, "  wbuf_nocrc_char(wbuf, '%c');\n", (char)(0xff&lt->command_and_flags));
		DO_FIELDS(field_type, lt, fprintf(cf, "  wbuf_nocrc_%s(wbuf, %s);\n", field_type->type, field_type->name));
		fprintf(cf, "}\n");
	    });
    fprintf2(cf, hf, "void toku_logger_rollback_wbuf_nocrc_write (struct wbuf *wbuf, struct roll_entry *r)");
    fprintf(hf, ";\n");
    fprintf(cf, " {\n  switch (r->cmd) {\n");
    DO_ROLLBACKS(lt, {
		fprintf(cf, "    case RT_%s: toku_logger_rollback_wbuf_nocrc_write_%s(wbuf", lt->name, lt->name);
		DO_FIELDS(field_type, lt, fprintf(cf, ", r->u.%s.%s", lt->name, field_type->name));
		fprintf(cf, "); return;\n");
	    });
    fprintf(cf, "  }\n  assert(0);\n");
    fprintf(cf, "}\n");
    DO_ROLLBACKS(lt, {
		fprintf2(cf, hf, "u_int32_t toku_logger_rollback_fsize_%s (", lt->name);
		int count=0;
		DO_FIELDS(field_type, lt, fprintf2(cf, hf, "%s%s %s", (count++>0)?", ":"", field_type->type, field_type->name));
		fprintf(hf, ");\n");
		fprintf(cf, ") {\n");
		fprintf(cf, "  return 1 /* the cmd*/\n");
		fprintf(cf, "         + 4 /* the int at the end saying the size */");
		DO_FIELDS(field_type, lt,
			  fprintf(cf, "\n         + toku_logsizeof_%s(%s)", field_type->type, field_type->name));
		fprintf(cf, ";\n}\n");
	    });
    fprintf2(cf, hf, "u_int32_t toku_logger_rollback_fsize(struct roll_entry *item)");
    fprintf(hf, ";\n");
    fprintf(cf, "{\n  switch(item->cmd) {\n");
    DO_ROLLBACKS(lt, {
		fprintf(cf, "    case RT_%s: return toku_logger_rollback_fsize_%s(", lt->name, lt->name);
		int count=0;
		DO_FIELDS(field_type, lt, fprintf(cf, "%sitem->u.%s.%s", (count++>0)?", ":"", lt->name, field_type->name));
		fprintf(cf, ");\n");
	    });
    fprintf(cf, "  }\n  assert(0);\n  return 0;\n");
    fprintf(cf, "}\n");

    fprintf2(cf, hf, "int toku_parse_rollback(unsigned char *buf, u_int32_t n_bytes, struct roll_entry **itemp, MEMARENA ma)");
    fprintf(hf, ";\n");
    fprintf(cf, " {\n  assert(n_bytes>0);\n  struct roll_entry *item;\n  enum rt_cmd cmd = (enum rt_cmd)(buf[0]);\n  size_t mem_needed;\n");
    fprintf(cf, "  struct rbuf rc = {buf, n_bytes, 1};\n");
    fprintf(cf, "  switch(cmd) {\n");
    DO_ROLLBACKS(lt, {
		fprintf(cf, "  case RT_%s:\n", lt->name);
		fprintf(cf, "    mem_needed = sizeof(item->u.%s) + __builtin_offsetof(struct roll_entry, u.%s);\n", lt->name, lt->name);
                fprintf(cf, "    item = malloc_in_memarena(ma, mem_needed);\n");
                fprintf(cf, "    item->cmd = cmd;\n");
		DO_FIELDS(field_type, lt, fprintf(cf, "    rbuf_ma_%s(&rc, ma, &item->u.%s.%s);\n", field_type->type, lt->name, field_type->name));
		fprintf(cf, "    *itemp = item;\n");
		fprintf(cf, "    return 0;\n");
	});
    fprintf(cf, "  }\n  return EINVAL;\n}\n");
}

static void
generate_log_entry_functions(void) {
    fprintf(hf, "LSN toku_log_entry_get_lsn(struct log_entry *);\n");
    fprintf(cf, "LSN toku_log_entry_get_lsn(struct log_entry *le) {\n");
    fprintf(cf, "    return le->u.begin_checkpoint.lsn;\n");
    fprintf(cf, "}\n");
}

const char codefile[] = "log_code.c";
const char printfile[] = "log_print.c";
const char headerfile[] = "log_header.h";
int main (int argc, const char *const argv[]) {
    assert(argc==2); // the single argument is the directory into which to put things
    const char *dir = argv[1];
    size_t codepathlen   = sizeof(codefile) + strlen(dir) + 4;
    size_t printpathlen  = sizeof(printfile) + strlen(dir) + 4; 
    size_t headerpathlen = sizeof(headerfile) + strlen(dir) + 4; 
    char codepath[codepathlen];
    char printpath[printpathlen];
    char headerpath[headerpathlen];
    { int r = snprintf(codepath,   codepathlen,   "%s/%s", argv[1], codefile);    assert(r<(int)codepathlen); }
    { int r = snprintf(printpath,  printpathlen,  "%s/%s", argv[1], printfile);   assert(r<(int)printpathlen); }
    { int r = snprintf(headerpath, headerpathlen, "%s/%s", argv[1], headerfile);  assert(r<(int)headerpathlen); }
    chmod(codepath, S_IRUSR|S_IWUSR);
    chmod(headerpath, S_IRUSR|S_IWUSR);
    unlink(codepath);
    unlink(headerpath);
    cf = fopen(codepath, "w");
    if (cf==0) { int r = errno; printf("fopen of %s failed because of errno=%d (%s)\n", codepath, r, strerror(r)); } // sometimes this is failing, so let's make a better diagnostic
    assert(cf!=0);
    hf = fopen(headerpath, "w");     assert(hf!=0);
    pf = fopen(printpath, "w");   assert(pf!=0);
    fprintf2(cf, hf, "/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */\n");
    fprintf2(cf, hf, "// vim: expandtab:ts=8:sw=4:softtabstop=4:\n");
    fprintf(hf, "#ifndef LOG_HEADER_H\n");
    fprintf(hf, "#define  LOG_HEADER_H\n");
    fprintf2(cf, hf, "/* Do not edit this file.  This code generated by logformat.c.  Copyright 2007-2010 Tokutek.    */\n");
    fprintf2(cf, hf, "#ident \"Copyright (c) 2007-2010 Tokutek Inc.  All rights reserved.\"\n");
    fprintf2(cf, pf, "#include <ft/includes.h>\n");
    fprintf(hf, "#include <ft/ft-internal.h>\n");
    fprintf(hf, "#include <ft/memarena.h>\n");
    fprintf(hf, "#if defined(__cplusplus)\nextern \"C\" {\n#endif\n");
    generate_enum();
    generate_log_struct();
    generate_dispatch();
    generate_log_writer();
    generate_log_reader();
    generate_rollbacks();
    generate_log_entry_functions();
    generate_logprint();
    fprintf(hf, "#if defined(__cplusplus)\n};\n#endif\n");
    fprintf(hf, "#endif\n");
    {
	int r=fclose(hf); assert(r==0);
	r=fclose(cf); assert(r==0);
        r=fclose(pf); assert(r==0);
	// Make it tougher to modify by mistake
	chmod(codepath, S_IRUSR|S_IRGRP|S_IROTH);
	chmod(headerpath, S_IRUSR|S_IRGRP|S_IROTH);
    }
    return 0;
}

