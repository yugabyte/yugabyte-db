/*--------------------------------------------------------------------
 * guc.c
 *
 * Support for grand unified configuration scheme, including SET
 * command, configuration file, and command line options.
 * See src/backend/utils/misc/README for more information.
 *
 *
 * Copyright (c) 2000-2022, PostgreSQL Global Development Group
 * Written by Peter Eisentraut <peter_e@gmx.net>.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/guc.c
 *
 *--------------------------------------------------------------------
 */
#include "postgres.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <limits.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#ifndef WIN32
#include <sys/mman.h>
#endif
#include <sys/stat.h>
#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif
#include <unistd.h>

#include "access/commit_ts.h"
#include "access/gin.h"
#include "access/rmgr.h"
#include "access/tableam.h"
#include "access/toast_compression.h"
#include "access/transam.h"
#include "access/twophase.h"
#include "access/xact.h"
#include "access/xlog_internal.h"
#include "access/xlogprefetcher.h"
#include "access/xlogrecovery.h"
#include "catalog/namespace.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_parameter_acl.h"
#include "catalog/storage.h"
#include "commands/async.h"
#include "commands/prepare.h"
#include "commands/tablespace.h"
#include "commands/trigger.h"
#include "commands/user.h"
#include "commands/vacuum.h"
#include "commands/variable.h"
#include "common/string.h"
#include "funcapi.h"
#include "jit/jit.h"
#include "libpq/auth.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"
#include "optimizer/cost.h"
#include "optimizer/geqo.h"
#include "optimizer/optimizer.h"
#include "optimizer/paths.h"
#include "optimizer/planmain.h"
#include "parser/parse_expr.h"
#include "parser/parse_type.h"
#include "parser/parser.h"
#include "parser/scansup.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#include "postmaster/bgworker_internals.h"
#include "postmaster/bgwriter.h"
#include "postmaster/postmaster.h"
#include "postmaster/startup.h"
#include "postmaster/syslogger.h"
#include "postmaster/walwriter.h"
#include "replication/logicallauncher.h"
#include "replication/reorderbuffer.h"
#include "replication/slot.h"
#include "replication/syncrep.h"
#include "replication/walreceiver.h"
#include "replication/walsender.h"
#include "storage/bufmgr.h"
#include "storage/dsm_impl.h"
#include "storage/fd.h"
#include "storage/large_object.h"
#include "storage/pg_shmem.h"
#include "storage/predicate.h"
#include "storage/proc.h"
#include "storage/standby.h"
#include "tcop/tcopprot.h"
#include "tsearch/ts_cache.h"
#include "utils/acl.h"
#include "utils/backend_status.h"
#include "utils/builtins.h"
#include "utils/bytea.h"
#include "utils/float.h"
#include "utils/guc_tables.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"
#include "utils/pg_lsn.h"
#include "utils/plancache.h"
#include "utils/portal.h"
#include "utils/ps_status.h"
#include "utils/queryjumble.h"
#include "utils/rls.h"
#include "utils/snapmgr.h"
#include "utils/tzparser.h"
#include "utils/inval.h"
#include "utils/varlena.h"
#include "utils/xml.h"

/* Yugabyte includes */
#include "access/heaptoast.h"
#include "access/yb_scan.h"
#include "commands/copy.h"
#include "executor/ybcModifyTable.h"
#include "tcop/pquery.h"
#include "pg_yb_utils.h"
#include "yb_ash.h"
#include "yb_query_diagnostics.h"

#ifndef PG_KRB_SRVTAB
#define PG_KRB_SRVTAB ""
#endif

#define CONFIG_FILENAME "postgresql.conf"
#define HBA_FILENAME	"pg_hba.conf"
#define IDENT_FILENAME	"pg_ident.conf"

#ifdef EXEC_BACKEND
#define CONFIG_EXEC_PARAMS "global/config_exec_params"
#define CONFIG_EXEC_PARAMS_NEW "global/config_exec_params.new"
#endif

/*
 * Precision with which REAL type guc values are to be printed for GUC
 * serialization.
 */
#define REALTYPE_PRECISION 17

/* XXX these should appear in other modules' header files */
extern bool Log_disconnections;
extern int	CommitDelay;
extern int	CommitSiblings;
extern char *default_tablespace;
extern char *temp_tablespaces;
extern bool ignore_checksum_failure;
extern bool ignore_invalid_pages;
extern bool synchronize_seqscans;

#ifdef TRACE_SYNCSCAN
extern bool trace_syncscan;
#endif
#ifdef DEBUG_BOUNDED_SORT
extern bool optimize_bounded_sort;
#endif

static double yb_transaction_priority_lower_bound = 0.0;
static double yb_transaction_priority_upper_bound = 1.0;
static double yb_transaction_priority = 0.0;

static int	GUC_check_errcode_value;

static List *reserved_class_prefix = NIL;

/* global variables for check hook support */
char	   *GUC_check_errmsg_string;
char	   *GUC_check_errdetail_string;
char	   *GUC_check_errhint_string;

static void do_serialize(char **destptr, Size *maxbytes, const char *fmt,...) pg_attribute_printf(3, 4);

static void set_config_sourcefile(const char *name, char *sourcefile,
								  int sourceline);
static bool call_bool_check_hook(struct config_bool *conf, bool *newval,
								 void **extra, GucSource source, int elevel);
static bool call_int_check_hook(struct config_int *conf, int *newval,
								void **extra, GucSource source, int elevel);
static bool call_oid_check_hook(struct config_oid *conf, Oid *newval,
								void **extra, GucSource source, int elevel);
static bool call_real_check_hook(struct config_real *conf, double *newval,
								 void **extra, GucSource source, int elevel);
static bool call_string_check_hook(struct config_string *conf, char **newval,
								   void **extra, GucSource source, int elevel);
static bool call_enum_check_hook(struct config_enum *conf, int *newval,
								 void **extra, GucSource source, int elevel);

static bool check_log_destination(char **newval, void **extra, GucSource source);
static void assign_log_destination(const char *newval, void *extra);

static bool check_wal_consistency_checking(char **newval, void **extra,
										   GucSource source);
static void assign_wal_consistency_checking(const char *newval, void *extra);

static bool check_default_replica_identity(char **newval, void **extra,
							   GucSource source);

#ifdef HAVE_SYSLOG
static int	syslog_facility = LOG_LOCAL0;
#else
static int	syslog_facility = 0;
#endif

static void assign_syslog_facility(int newval, void *extra);
static void assign_syslog_ident(const char *newval, void *extra);
static void assign_session_replication_role(int newval, void *extra);
static bool check_temp_buffers(int *newval, void **extra, GucSource source);
static bool check_bonjour(bool *newval, void **extra, GucSource source);
static bool check_ssl(bool *newval, void **extra, GucSource source);
static bool check_stage_log_stats(bool *newval, void **extra, GucSource source);
static bool check_log_stats(bool *newval, void **extra, GucSource source);
static bool check_canonical_path(char **newval, void **extra, GucSource source);
static bool check_timezone_abbreviations(char **newval, void **extra, GucSource source);
static void assign_timezone_abbreviations(const char *newval, void *extra);
static void pg_timezone_abbrev_initialize(void);
static const char *show_archive_command(void);
static void assign_tcp_keepalives_idle(int newval, void *extra);
static void assign_tcp_keepalives_interval(int newval, void *extra);
static void assign_tcp_keepalives_count(int newval, void *extra);
static void assign_tcp_user_timeout(int newval, void *extra);
static const char *show_tcp_keepalives_idle(void);
static const char *show_tcp_keepalives_interval(void);
static const char *show_tcp_keepalives_count(void);
static const char *show_tcp_user_timeout(void);
static bool check_yb_explicit_row_locking_batch_size(int *newval, void **extra, GucSource source);
static bool check_maxconnections(int *newval, void **extra, GucSource source);
static const char *yb_show_maxconnections(void);
static bool check_max_worker_processes(int *newval, void **extra, GucSource source);
static bool check_autovacuum_max_workers(int *newval, void **extra, GucSource source);
static bool check_max_wal_senders(int *newval, void **extra, GucSource source);
static bool check_autovacuum_work_mem(int *newval, void **extra, GucSource source);
static bool check_effective_io_concurrency(int *newval, void **extra, GucSource source);
static bool check_maintenance_io_concurrency(int *newval, void **extra, GucSource source);
static bool check_huge_page_size(int *newval, void **extra, GucSource source);
static bool check_client_connection_check_interval(int *newval, void **extra, GucSource source);
static void assign_maintenance_io_concurrency(int newval, void *extra);
static bool check_application_name(char **newval, void **extra, GucSource source);
static void assign_application_name(const char *newval, void *extra);
static bool check_cluster_name(char **newval, void **extra, GucSource source);
static const char *show_unix_socket_permissions(void);
static const char *show_log_file_mode(void);
static const char *show_data_directory_mode(void);
static const char *show_in_hot_standby(void);
static bool check_backtrace_functions(char **newval, void **extra, GucSource source);
static void assign_backtrace_functions(const char *newval, void *extra);
static bool check_recovery_target_timeline(char **newval, void **extra, GucSource source);
static void assign_recovery_target_timeline(const char *newval, void *extra);
static bool check_recovery_target(char **newval, void **extra, GucSource source);
static void assign_recovery_target(const char *newval, void *extra);
static bool check_recovery_target_xid(char **newval, void **extra, GucSource source);
static void assign_recovery_target_xid(const char *newval, void *extra);
static bool check_recovery_target_time(char **newval, void **extra, GucSource source);
static void assign_recovery_target_time(const char *newval, void *extra);
static bool check_recovery_target_name(char **newval, void **extra, GucSource source);
static void assign_recovery_target_name(const char *newval, void *extra);
static bool check_recovery_target_lsn(char **newval, void **extra, GucSource source);
static void assign_recovery_target_lsn(const char *newval, void *extra);
static bool check_primary_slot_name(char **newval, void **extra, GucSource source);
static bool check_default_with_oids(bool *newval, void **extra, GucSource source);

static bool check_transaction_priority_lower_bound(double *newval, void **extra, GucSource source);
extern void YBCAssignTransactionPriorityLowerBound(double newval, void* extra);
static bool check_transaction_priority_upper_bound(double *newval, void **extra, GucSource source);
extern void YBCAssignTransactionPriorityUpperBound(double newval, void* extra);
extern double YBCGetTransactionPriority();
extern TxnPriorityRequirement YBCGetTransactionPriorityType();
static bool yb_check_no_txn(int* newval, void **extra, GucSource source);

static void assign_yb_pg_batch_detection_mechanism(int new_value, void *extra);
static void assign_ysql_upgrade_mode(bool newval, void *extra);

static bool check_max_backoff(int *max_backoff_msecs, void **extra, GucSource source);
static bool check_min_backoff(int *min_backoff_msecs, void **extra, GucSource source);
static bool check_backoff_multiplier(double *multiplier, void **extra, GucSource source);
static bool yb_check_toast_catcache_threshold(int *newval, void **extra, GucSource source);
static void check_reserved_prefixes(const char *varName);

/* Private functions in guc-file.l that need to be called from guc.c */
static ConfigVariable *ProcessConfigFileInternal(GucContext context,
												 bool applySettings, int elevel);

/*
 * Track whether there were any deferred checks for custom resource managers
 * specified in wal_consistency_checking.
 */
static bool check_wal_consistency_checking_deferred = false;

/*
 * Options for enum values defined in this module.
 *
 * NOTE! Option values may not contain double quotes!
 */

static const struct config_enum_entry bytea_output_options[] = {
	{"escape", BYTEA_OUTPUT_ESCAPE, false},
	{"hex", BYTEA_OUTPUT_HEX, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(bytea_output_options) == (BYTEA_OUTPUT_HEX + 2),
				 "array length mismatch");

/*
 * We have different sets for client and server message level options because
 * they sort slightly different (see "log" level), and because "fatal"/"panic"
 * aren't sensible for client_min_messages.
 */
static const struct config_enum_entry client_message_level_options[] = {
	{"debug5", DEBUG5, false},
	{"debug4", DEBUG4, false},
	{"debug3", DEBUG3, false},
	{"debug2", DEBUG2, false},
	{"debug1", DEBUG1, false},
	{"debug", DEBUG2, true},
	{"log", LOG, false},
	{"info", INFO, true},
	{"notice", NOTICE, false},
	{"warning", WARNING, false},
	{"error", ERROR, false},
	{NULL, 0, false}
};

static const struct config_enum_entry server_message_level_options[] = {
	{"debug5", DEBUG5, false},
	{"debug4", DEBUG4, false},
	{"debug3", DEBUG3, false},
	{"debug2", DEBUG2, false},
	{"debug1", DEBUG1, false},
	{"debug", DEBUG2, true},
	{"info", INFO, false},
	{"notice", NOTICE, false},
	{"warning", WARNING, false},
	{"error", ERROR, false},
	{"log", LOG, false},
	{"fatal", FATAL, false},
	{"panic", PANIC, false},
	{NULL, 0, false}
};

static const struct config_enum_entry intervalstyle_options[] = {
	{"postgres", INTSTYLE_POSTGRES, false},
	{"postgres_verbose", INTSTYLE_POSTGRES_VERBOSE, false},
	{"sql_standard", INTSTYLE_SQL_STANDARD, false},
	{"iso_8601", INTSTYLE_ISO_8601, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(intervalstyle_options) == (INTSTYLE_ISO_8601 + 2),
				 "array length mismatch");

static const struct config_enum_entry log_error_verbosity_options[] = {
	{"terse", PGERROR_TERSE, false},
	{"default", PGERROR_DEFAULT, false},
	{"verbose", PGERROR_VERBOSE, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(log_error_verbosity_options) == (PGERROR_VERBOSE + 2),
				 "array length mismatch");

static const struct config_enum_entry log_statement_options[] = {
	{"none", LOGSTMT_NONE, false},
	{"ddl", LOGSTMT_DDL, false},
	{"mod", LOGSTMT_MOD, false},
	{"all", LOGSTMT_ALL, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(log_statement_options) == (LOGSTMT_ALL + 2),
				 "array length mismatch");

static const struct config_enum_entry isolation_level_options[] = {
	{"serializable", XACT_SERIALIZABLE, false},
	{"repeatable read", XACT_REPEATABLE_READ, false},
	{"read committed", XACT_READ_COMMITTED, false},
	{"read uncommitted", XACT_READ_UNCOMMITTED, false},
	{NULL, 0}
};

static const struct config_enum_entry session_replication_role_options[] = {
	{"origin", SESSION_REPLICATION_ROLE_ORIGIN, false},
	{"replica", SESSION_REPLICATION_ROLE_REPLICA, false},
	{"local", SESSION_REPLICATION_ROLE_LOCAL, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(session_replication_role_options) == (SESSION_REPLICATION_ROLE_LOCAL + 2),
				 "array length mismatch");

static const struct config_enum_entry syslog_facility_options[] = {
#ifdef HAVE_SYSLOG
	{"local0", LOG_LOCAL0, false},
	{"local1", LOG_LOCAL1, false},
	{"local2", LOG_LOCAL2, false},
	{"local3", LOG_LOCAL3, false},
	{"local4", LOG_LOCAL4, false},
	{"local5", LOG_LOCAL5, false},
	{"local6", LOG_LOCAL6, false},
	{"local7", LOG_LOCAL7, false},
#else
	{"none", 0, false},
#endif
	{NULL, 0}
};

static const struct config_enum_entry track_function_options[] = {
	{"none", TRACK_FUNC_OFF, false},
	{"pl", TRACK_FUNC_PL, false},
	{"all", TRACK_FUNC_ALL, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(track_function_options) == (TRACK_FUNC_ALL + 2),
				 "array length mismatch");

static const struct config_enum_entry stats_fetch_consistency[] = {
	{"none", PGSTAT_FETCH_CONSISTENCY_NONE, false},
	{"cache", PGSTAT_FETCH_CONSISTENCY_CACHE, false},
	{"snapshot", PGSTAT_FETCH_CONSISTENCY_SNAPSHOT, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(stats_fetch_consistency) == (PGSTAT_FETCH_CONSISTENCY_SNAPSHOT + 2),
				 "array length mismatch");

static const struct config_enum_entry xmlbinary_options[] = {
	{"base64", XMLBINARY_BASE64, false},
	{"hex", XMLBINARY_HEX, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(xmlbinary_options) == (XMLBINARY_HEX + 2),
				 "array length mismatch");

static const struct config_enum_entry xmloption_options[] = {
	{"content", XMLOPTION_CONTENT, false},
	{"document", XMLOPTION_DOCUMENT, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(xmloption_options) == (XMLOPTION_CONTENT + 2),
				 "array length mismatch");

/*
 * Although only "on", "off", and "safe_encoding" are documented, we
 * accept all the likely variants of "on" and "off".
 */
static const struct config_enum_entry backslash_quote_options[] = {
	{"safe_encoding", BACKSLASH_QUOTE_SAFE_ENCODING, false},
	{"on", BACKSLASH_QUOTE_ON, false},
	{"off", BACKSLASH_QUOTE_OFF, false},
	{"true", BACKSLASH_QUOTE_ON, true},
	{"false", BACKSLASH_QUOTE_OFF, true},
	{"yes", BACKSLASH_QUOTE_ON, true},
	{"no", BACKSLASH_QUOTE_OFF, true},
	{"1", BACKSLASH_QUOTE_ON, true},
	{"0", BACKSLASH_QUOTE_OFF, true},
	{NULL, 0, false}
};

/*
 * Although only "on", "off", and "auto" are documented, we accept
 * all the likely variants of "on" and "off".
 */
static const struct config_enum_entry compute_query_id_options[] = {
	{"auto", COMPUTE_QUERY_ID_AUTO, false},
	{"regress", COMPUTE_QUERY_ID_REGRESS, false},
	{"on", COMPUTE_QUERY_ID_ON, false},
	{"off", COMPUTE_QUERY_ID_OFF, false},
	{"true", COMPUTE_QUERY_ID_ON, true},
	{"false", COMPUTE_QUERY_ID_OFF, true},
	{"yes", COMPUTE_QUERY_ID_ON, true},
	{"no", COMPUTE_QUERY_ID_OFF, true},
	{"1", COMPUTE_QUERY_ID_ON, true},
	{"0", COMPUTE_QUERY_ID_OFF, true},
	{NULL, 0, false}
};

/*
 * Although only "on", "off", and "partition" are documented, we
 * accept all the likely variants of "on" and "off".
 */
static const struct config_enum_entry constraint_exclusion_options[] = {
	{"partition", CONSTRAINT_EXCLUSION_PARTITION, false},
	{"on", CONSTRAINT_EXCLUSION_ON, false},
	{"off", CONSTRAINT_EXCLUSION_OFF, false},
	{"true", CONSTRAINT_EXCLUSION_ON, true},
	{"false", CONSTRAINT_EXCLUSION_OFF, true},
	{"yes", CONSTRAINT_EXCLUSION_ON, true},
	{"no", CONSTRAINT_EXCLUSION_OFF, true},
	{"1", CONSTRAINT_EXCLUSION_ON, true},
	{"0", CONSTRAINT_EXCLUSION_OFF, true},
	{NULL, 0, false}
};

/*
 * Although only "on", "off", "remote_apply", "remote_write", and "local" are
 * documented, we accept all the likely variants of "on" and "off".
 */
static const struct config_enum_entry synchronous_commit_options[] = {
	{"local", SYNCHRONOUS_COMMIT_LOCAL_FLUSH, false},
	{"remote_write", SYNCHRONOUS_COMMIT_REMOTE_WRITE, false},
	{"remote_apply", SYNCHRONOUS_COMMIT_REMOTE_APPLY, false},
	{"on", SYNCHRONOUS_COMMIT_ON, false},
	{"off", SYNCHRONOUS_COMMIT_OFF, false},
	{"true", SYNCHRONOUS_COMMIT_ON, true},
	{"false", SYNCHRONOUS_COMMIT_OFF, true},
	{"yes", SYNCHRONOUS_COMMIT_ON, true},
	{"no", SYNCHRONOUS_COMMIT_OFF, true},
	{"1", SYNCHRONOUS_COMMIT_ON, true},
	{"0", SYNCHRONOUS_COMMIT_OFF, true},
	{NULL, 0, false}
};

/*
 * Although only "on", "off", "try" are documented, we accept all the likely
 * variants of "on" and "off".
 */
static const struct config_enum_entry huge_pages_options[] = {
	{"off", HUGE_PAGES_OFF, false},
	{"on", HUGE_PAGES_ON, false},
	{"try", HUGE_PAGES_TRY, false},
	{"true", HUGE_PAGES_ON, true},
	{"false", HUGE_PAGES_OFF, true},
	{"yes", HUGE_PAGES_ON, true},
	{"no", HUGE_PAGES_OFF, true},
	{"1", HUGE_PAGES_ON, true},
	{"0", HUGE_PAGES_OFF, true},
	{NULL, 0, false}
};

static const struct config_enum_entry recovery_prefetch_options[] = {
	{"off", RECOVERY_PREFETCH_OFF, false},
	{"on", RECOVERY_PREFETCH_ON, false},
	{"try", RECOVERY_PREFETCH_TRY, false},
	{"true", RECOVERY_PREFETCH_ON, true},
	{"false", RECOVERY_PREFETCH_OFF, true},
	{"yes", RECOVERY_PREFETCH_ON, true},
	{"no", RECOVERY_PREFETCH_OFF, true},
	{"1", RECOVERY_PREFETCH_ON, true},
	{"0", RECOVERY_PREFETCH_OFF, true},
	{NULL, 0, false}
};

static const struct config_enum_entry force_parallel_mode_options[] = {
	{"off", FORCE_PARALLEL_OFF, false},
	{"on", FORCE_PARALLEL_ON, false},
	{"regress", FORCE_PARALLEL_REGRESS, false},
	{"true", FORCE_PARALLEL_ON, true},
	{"false", FORCE_PARALLEL_OFF, true},
	{"yes", FORCE_PARALLEL_ON, true},
	{"no", FORCE_PARALLEL_OFF, true},
	{"1", FORCE_PARALLEL_ON, true},
	{"0", FORCE_PARALLEL_OFF, true},
	{NULL, 0, false}
};

static const struct config_enum_entry plan_cache_mode_options[] = {
	{"auto", PLAN_CACHE_MODE_AUTO, false},
	{"force_generic_plan", PLAN_CACHE_MODE_FORCE_GENERIC_PLAN, false},
	{"force_custom_plan", PLAN_CACHE_MODE_FORCE_CUSTOM_PLAN, false},
	{NULL, 0, false}
};

static const struct config_enum_entry password_encryption_options[] = {
	{"md5", PASSWORD_TYPE_MD5, false},
	{"scram-sha-256", PASSWORD_TYPE_SCRAM_SHA_256, false},
	{NULL, 0, false}
};

const struct config_enum_entry ssl_protocol_versions_info[] = {
	{"", PG_TLS_ANY, false},
	{"TLSv1", PG_TLS1_VERSION, false},
	{"TLSv1.1", PG_TLS1_1_VERSION, false},
	{"TLSv1.2", PG_TLS1_2_VERSION, false},
	{"TLSv1.3", PG_TLS1_3_VERSION, false},
	{NULL, 0, false}
};

StaticAssertDecl(lengthof(ssl_protocol_versions_info) == (PG_TLS1_3_VERSION + 2),
				 "array length mismatch");

static struct config_enum_entry recovery_init_sync_method_options[] = {
	{"fsync", RECOVERY_INIT_SYNC_METHOD_FSYNC, false},
#ifdef HAVE_SYNCFS
	{"syncfs", RECOVERY_INIT_SYNC_METHOD_SYNCFS, false},
#endif
	{NULL, 0, false}
};

const struct config_enum_entry yb_pg_batch_detection_mechanism_options[] = {
  {"detect_by_peeking", DETECT_BY_PEEKING, false},
  {"assume_all_batch_executions", ASSUME_ALL_BATCH_EXECUTIONS, false},
  {"ignore_batch_delete_and_update_may_fail", IGNORE_BATCH_DELETE_AND_UPDATE_MAY_FAIL, false},
  {NULL, 0, false}
};

static struct config_enum_entry shared_memory_options[] = {
#ifndef WIN32
	{"sysv", SHMEM_TYPE_SYSV, false},
#endif
#ifndef EXEC_BACKEND
	{"mmap", SHMEM_TYPE_MMAP, false},
#endif
#ifdef WIN32
	{"windows", SHMEM_TYPE_WINDOWS, false},
#endif
	{NULL, 0, false}
};

static struct config_enum_entry default_toast_compression_options[] = {
	{"pglz", TOAST_PGLZ_COMPRESSION, false},
#ifdef  USE_LZ4
	{"lz4", TOAST_LZ4_COMPRESSION, false},
#endif
	{NULL, 0, false}
};

static const struct config_enum_entry wal_compression_options[] = {
	{"pglz", WAL_COMPRESSION_PGLZ, false},
#ifdef USE_LZ4
	{"lz4", WAL_COMPRESSION_LZ4, false},
#endif
#ifdef USE_ZSTD
	{"zstd", WAL_COMPRESSION_ZSTD, false},
#endif
	{"on", WAL_COMPRESSION_PGLZ, false},
	{"off", WAL_COMPRESSION_NONE, false},
	{"true", WAL_COMPRESSION_PGLZ, true},
	{"false", WAL_COMPRESSION_NONE, true},
	{"yes", WAL_COMPRESSION_PGLZ, true},
	{"no", WAL_COMPRESSION_NONE, true},
	{"1", WAL_COMPRESSION_PGLZ, true},
	{"0", WAL_COMPRESSION_NONE, true},
	{NULL, 0, false}
};

const struct config_enum_entry yb_read_after_commit_visibility_options[] = {
  {"strict", YB_STRICT_READ_AFTER_COMMIT_VISIBILITY, false},
  {"relaxed", YB_RELAXED_READ_AFTER_COMMIT_VISIBILITY, false},
  {NULL, 0, false}
};

/*
 * Options for enum values stored in other modules
 */
extern const struct config_enum_entry wal_level_options[];
extern const struct config_enum_entry archive_mode_options[];
extern const struct config_enum_entry recovery_target_action_options[];
extern const struct config_enum_entry sync_method_options[];
extern const struct config_enum_entry dynamic_shared_memory_options[];

/*
 * GUC option variables that are exported from this module
 */
bool		log_duration = false;
bool		Debug_print_plan = false;
bool		Debug_print_parse = false;
bool		Debug_print_rewritten = false;
bool		Debug_pretty_print = true;

bool		log_parser_stats = false;
bool		log_planner_stats = false;
bool		log_executor_stats = false;
bool		log_statement_stats = false;	/* this is sort of all three above
											 * together */
bool		log_btree_build_stats = false;
char	   *event_source;

bool		row_security;
bool		check_function_bodies = true;

/*
 * This GUC exists solely for backward compatibility, check its definition for
 * details.
 */
bool		default_with_oids = false;
bool		session_auth_is_superuser;
bool		yb_enable_memory_tracking = true;

int			log_min_error_statement = ERROR;
int			log_min_messages = WARNING;
int			client_min_messages = NOTICE;
int			log_min_duration_sample = -1;
int			log_min_duration_statement = -1;
int			log_parameter_max_length = -1;
int			log_parameter_max_length_on_error = 0;
int			log_temp_files = -1;
double		log_statement_sample_rate = 1.0;
double		log_xact_sample_rate = 0;
int			trace_recovery_messages = LOG;
char	   *backtrace_functions;
char	   *backtrace_symbol_list;

int			temp_file_limit = -1;

int			num_temp_buffers = 1024;

char	   *cluster_name = "";
char	   *ConfigFileName;
char	   *HbaFileName;
char	   *IdentFileName;
char	   *external_pid_file;

char	   *pgstat_temp_directory;

char	   *application_name;

int			tcp_keepalives_idle;
int			tcp_keepalives_interval;
int			tcp_keepalives_count;
int			tcp_user_timeout;

/*
 * SSL renegotiation was been removed in PostgreSQL 9.5, but we tolerate it
 * being set to zero (meaning never renegotiate) for backward compatibility.
 * This avoids breaking compatibility with clients that have never supported
 * renegotiation and therefore always try to zero it.
 */
int			ssl_renegotiation_limit;

/*
 * This really belongs in pg_shmem.c, but is defined here so that it doesn't
 * need to be duplicated in all the different implementations of pg_shmem.c.
 */
int			huge_pages;
int			huge_page_size;

/*
 * These variables are all dummies that don't do anything, except in some
 * cases provide the value for SHOW to display.  The real state is elsewhere
 * and is kept in sync by assign_hooks.
 */
static char *syslog_ident_str;
static double phony_random_seed;
static char *client_encoding_string;
static char *datestyle_string;
static char *locale_collate;
static char *locale_ctype;
static char *server_encoding_string;
static char *server_version_string;
static int	server_version_num;
static char *timezone_string;
static char *log_timezone_string;
static char *timezone_abbreviations_string;
static char *data_directory;
static char *session_authorization_string;
static int	max_function_args;
static int	max_index_keys;
static int	max_identifier_length;
static int	block_size;
static int	segment_size;
static int	shared_memory_size_mb;
static int	shared_memory_size_in_huge_pages;
static int	wal_block_size;
static bool data_checksums;
static bool integer_datetimes;
static bool assert_enabled;
static bool in_hot_standby;
static char *recovery_target_timeline_string;
static char *recovery_target_string;
static char *recovery_target_xid_string;
static char *recovery_target_name_string;
static char *recovery_target_lsn_string;

static char *yb_effective_transaction_isolation_level_string;
static char *yb_xcluster_consistency_level_string;
static char *yb_read_time_string;

/* should be static, but commands/variable.c needs to get at this */
char	   *role_string;


/*
 * Displayable names for context types (enum GucContext)
 *
 * Note: these strings are deliberately not localized.
 */
const char *const GucContext_Names[] =
{
	 /* PGC_INTERNAL */ "internal",
	 /* PGC_POSTMASTER */ "postmaster",
	 /* PGC_SIGHUP */ "sighup",
	 /* PGC_SU_BACKEND */ "superuser-backend",
	 /* PGC_BACKEND */ "backend",
	 /* PGC_SUSET */ "superuser",
	 /* PGC_USERSET */ "user"
};

StaticAssertDecl(lengthof(GucContext_Names) == (PGC_USERSET + 1),
				 "array length mismatch");

/*
 * Displayable names for source types (enum GucSource)
 *
 * Note: these strings are deliberately not localized.
 */
const char *const GucSource_Names[] =
{
	 /* PGC_S_DEFAULT */ "default",
	 /* PGC_S_DYNAMIC_DEFAULT */ "default",
	 /* PGC_S_ENV_VAR */ "environment variable",
	 /* PGC_S_FILE */ "configuration file",
	 /* PGC_S_ARGV */ "command line",
	 /* PGC_S_GLOBAL */ "global",
	 /* PGC_S_DATABASE */ "database",
	 /* PGC_S_USER */ "user",
	 /* PGC_S_DATABASE_USER */ "database user",
	 /* PGC_S_CLIENT */ "client",
	 /* PGC_S_OVERRIDE */ "override",
	 /* PGC_S_INTERACTIVE */ "interactive",
	 /* PGC_S_TEST */ "test",
	 /* PGC_S_SESSION */ "session"
};

StaticAssertDecl(lengthof(GucSource_Names) == (PGC_S_SESSION + 1),
				 "array length mismatch");

/*
 * Displayable names for the groupings defined in enum config_group
 */
const char *const config_group_names[] =
{
	/* UNGROUPED */
	gettext_noop("Ungrouped"),
	/* FILE_LOCATIONS */
	gettext_noop("File Locations"),
	/* CONN_AUTH_SETTINGS */
	gettext_noop("Connections and Authentication / Connection Settings"),
	/* CONN_AUTH_AUTH */
	gettext_noop("Connections and Authentication / Authentication"),
	/* CONN_AUTH_SSL */
	gettext_noop("Connections and Authentication / SSL"),
	/* RESOURCES_MEM */
	gettext_noop("Resource Usage / Memory"),
	/* RESOURCES_DISK */
	gettext_noop("Resource Usage / Disk"),
	/* RESOURCES_KERNEL */
	gettext_noop("Resource Usage / Kernel Resources"),
	/* RESOURCES_VACUUM_DELAY */
	gettext_noop("Resource Usage / Cost-Based Vacuum Delay"),
	/* RESOURCES_BGWRITER */
	gettext_noop("Resource Usage / Background Writer"),
	/* RESOURCES_ASYNCHRONOUS */
	gettext_noop("Resource Usage / Asynchronous Behavior"),
	/* WAL_SETTINGS */
	gettext_noop("Write-Ahead Log / Settings"),
	/* WAL_CHECKPOINTS */
	gettext_noop("Write-Ahead Log / Checkpoints"),
	/* WAL_ARCHIVING */
	gettext_noop("Write-Ahead Log / Archiving"),
	/* WAL_RECOVERY */
	gettext_noop("Write-Ahead Log / Recovery"),
	/* WAL_ARCHIVE_RECOVERY */
	gettext_noop("Write-Ahead Log / Archive Recovery"),
	/* WAL_RECOVERY_TARGET */
	gettext_noop("Write-Ahead Log / Recovery Target"),
	/* REPLICATION_SENDING */
	gettext_noop("Replication / Sending Servers"),
	/* REPLICATION_PRIMARY */
	gettext_noop("Replication / Primary Server"),
	/* REPLICATION_STANDBY */
	gettext_noop("Replication / Standby Servers"),
	/* REPLICATION_SUBSCRIBERS */
	gettext_noop("Replication / Subscribers"),
	/* QUERY_TUNING_METHOD */
	gettext_noop("Query Tuning / Planner Method Configuration"),
	/* QUERY_TUNING_COST */
	gettext_noop("Query Tuning / Planner Cost Constants"),
	/* QUERY_TUNING_GEQO */
	gettext_noop("Query Tuning / Genetic Query Optimizer"),
	/* QUERY_TUNING_OTHER */
	gettext_noop("Query Tuning / Other Planner Options"),
	/* LOGGING_WHERE */
	gettext_noop("Reporting and Logging / Where to Log"),
	/* LOGGING_WHEN */
	gettext_noop("Reporting and Logging / When to Log"),
	/* LOGGING_WHAT */
	gettext_noop("Reporting and Logging / What to Log"),
	/* PROCESS_TITLE */
	gettext_noop("Reporting and Logging / Process Title"),
	/* STATS_MONITORING */
	gettext_noop("Statistics / Monitoring"),
	/* STATS_CUMULATIVE */
	gettext_noop("Statistics / Cumulative Query and Index Statistics"),
	/* AUTOVACUUM */
	gettext_noop("Autovacuum"),
	/* CLIENT_CONN_STATEMENT */
	gettext_noop("Client Connection Defaults / Statement Behavior"),
	/* CLIENT_CONN_LOCALE */
	gettext_noop("Client Connection Defaults / Locale and Formatting"),
	/* CLIENT_CONN_PRELOAD */
	gettext_noop("Client Connection Defaults / Shared Library Preloading"),
	/* CLIENT_CONN_OTHER */
	gettext_noop("Client Connection Defaults / Other Defaults"),
	/* LOCK_MANAGEMENT */
	gettext_noop("Lock Management"),
	/* COMPAT_OPTIONS_PREVIOUS */
	gettext_noop("Version and Platform Compatibility / Previous PostgreSQL Versions"),
	/* COMPAT_OPTIONS_CLIENT */
	gettext_noop("Version and Platform Compatibility / Other Platforms and Clients"),
	/* ERROR_HANDLING */
	gettext_noop("Error Handling"),
	/* PRESET_OPTIONS */
	gettext_noop("Preset Options"),
	/* CUSTOM_OPTIONS */
	gettext_noop("Customized Options"),
	/* DEVELOPER_OPTIONS */
	gettext_noop("Developer Options"),
	/* help_config wants this array to be null-terminated */
	NULL
};

StaticAssertDecl(lengthof(config_group_names) == (DEVELOPER_OPTIONS + 2),
				 "array length mismatch");

/*
 * Displayable names for GUC variable types (enum config_type)
 *
 * Note: these strings are deliberately not localized.
 */
const char *const config_type_names[] =
{
	 /* PGC_BOOL */ "bool",
	 /* PGC_INT */ "integer",
	 /* PGC_OID */ "oid",
	 /* PGC_REAL */ "real",
	 /* PGC_STRING */ "string",
	 /* PGC_ENUM */ "enum"
};

StaticAssertDecl(lengthof(config_type_names) == (PGC_ENUM + 1),
				 "array length mismatch");

/*
 * Unit conversion tables.
 *
 * There are two tables, one for memory units, and another for time units.
 * For each supported conversion from one unit to another, we have an entry
 * in the table.
 *
 * To keep things simple, and to avoid possible roundoff error,
 * conversions are never chained.  There needs to be a direct conversion
 * between all units (of the same type).
 *
 * The conversions for each base unit must be kept in order from greatest to
 * smallest human-friendly unit; convert_xxx_from_base_unit() rely on that.
 * (The order of the base-unit groups does not matter.)
 */
#define MAX_UNIT_LEN		3	/* length of longest recognized unit string */

typedef struct
{
	char		unit[MAX_UNIT_LEN + 1]; /* unit, as a string, like "kB" or
										 * "min" */
	int			base_unit;		/* GUC_UNIT_XXX */
	double		multiplier;		/* Factor for converting unit -> base_unit */
} unit_conversion;

/* Ensure that the constants in the tables don't overflow or underflow */
#if BLCKSZ < 1024 || BLCKSZ > (1024*1024)
#error BLCKSZ must be between 1KB and 1MB
#endif
#if XLOG_BLCKSZ < 1024 || XLOG_BLCKSZ > (1024*1024)
#error XLOG_BLCKSZ must be between 1KB and 1MB
#endif

static const char *memory_units_hint = gettext_noop("Valid units for this parameter are \"B\", \"kB\", \"MB\", \"GB\", and \"TB\".");

static const unit_conversion memory_unit_conversion_table[] =
{
	{"TB", GUC_UNIT_BYTE, 1024.0 * 1024.0 * 1024.0 * 1024.0},
	{"GB", GUC_UNIT_BYTE, 1024.0 * 1024.0 * 1024.0},
	{"MB", GUC_UNIT_BYTE, 1024.0 * 1024.0},
	{"kB", GUC_UNIT_BYTE, 1024.0},
	{"B", GUC_UNIT_BYTE, 1.0},

	{"TB", GUC_UNIT_KB, 1024.0 * 1024.0 * 1024.0},
	{"GB", GUC_UNIT_KB, 1024.0 * 1024.0},
	{"MB", GUC_UNIT_KB, 1024.0},
	{"kB", GUC_UNIT_KB, 1.0},
	{"B", GUC_UNIT_KB, 1.0 / 1024.0},

	{"TB", GUC_UNIT_MB, 1024.0 * 1024.0},
	{"GB", GUC_UNIT_MB, 1024.0},
	{"MB", GUC_UNIT_MB, 1.0},
	{"kB", GUC_UNIT_MB, 1.0 / 1024.0},
	{"B", GUC_UNIT_MB, 1.0 / (1024.0 * 1024.0)},

	{"TB", GUC_UNIT_BLOCKS, (1024.0 * 1024.0 * 1024.0) / (BLCKSZ / 1024)},
	{"GB", GUC_UNIT_BLOCKS, (1024.0 * 1024.0) / (BLCKSZ / 1024)},
	{"MB", GUC_UNIT_BLOCKS, 1024.0 / (BLCKSZ / 1024)},
	{"kB", GUC_UNIT_BLOCKS, 1.0 / (BLCKSZ / 1024)},
	{"B", GUC_UNIT_BLOCKS, 1.0 / BLCKSZ},

	{"TB", GUC_UNIT_XBLOCKS, (1024.0 * 1024.0 * 1024.0) / (XLOG_BLCKSZ / 1024)},
	{"GB", GUC_UNIT_XBLOCKS, (1024.0 * 1024.0) / (XLOG_BLCKSZ / 1024)},
	{"MB", GUC_UNIT_XBLOCKS, 1024.0 / (XLOG_BLCKSZ / 1024)},
	{"kB", GUC_UNIT_XBLOCKS, 1.0 / (XLOG_BLCKSZ / 1024)},
	{"B", GUC_UNIT_XBLOCKS, 1.0 / XLOG_BLCKSZ},

	{""}						/* end of table marker */
};

static const char *time_units_hint = gettext_noop("Valid units for this parameter are \"us\", \"ms\", \"s\", \"min\", \"h\", and \"d\".");

static const unit_conversion time_unit_conversion_table[] =
{
	{"d", GUC_UNIT_MS, 1000 * 60 * 60 * 24},
	{"h", GUC_UNIT_MS, 1000 * 60 * 60},
	{"min", GUC_UNIT_MS, 1000 * 60},
	{"s", GUC_UNIT_MS, 1000},
	{"ms", GUC_UNIT_MS, 1},
	{"us", GUC_UNIT_MS, 1.0 / 1000},

	{"d", GUC_UNIT_S, 60 * 60 * 24},
	{"h", GUC_UNIT_S, 60 * 60},
	{"min", GUC_UNIT_S, 60},
	{"s", GUC_UNIT_S, 1},
	{"ms", GUC_UNIT_S, 1.0 / 1000},
	{"us", GUC_UNIT_S, 1.0 / (1000 * 1000)},

	{"d", GUC_UNIT_MIN, 60 * 24},
	{"h", GUC_UNIT_MIN, 60},
	{"min", GUC_UNIT_MIN, 1},
	{"s", GUC_UNIT_MIN, 1.0 / 60},
	{"ms", GUC_UNIT_MIN, 1.0 / (1000 * 60)},
	{"us", GUC_UNIT_MIN, 1.0 / (1000 * 1000 * 60)},

	{""}						/* end of table marker */
};

/*
 * Contents of GUC tables
 *
 * See src/backend/utils/misc/README for design notes.
 *
 * TO ADD AN OPTION:
 *
 * 1. Declare a global variable of type bool, int, double, or char*
 *	  and make use of it.
 *
 * 2. Decide at what times it's safe to set the option. See guc.h for
 *	  details.
 *
 * 3. Decide on a name, a default value, upper and lower bounds (if
 *	  applicable), etc.
 *
 * 4. Add a record below.
 *
 * 5. Add it to src/backend/utils/misc/postgresql.conf.sample, if
 *	  appropriate.
 *
 * 6. Don't forget to document the option (at least in config.sgml).
 *
 * 7. If it's a new GUC_LIST_QUOTE option, you must add it to
 *	  variable_is_guc_list_quote() in src/bin/pg_dump/dumputils.c.
 */


/******** option records follow ********/

static struct config_bool ConfigureNamesBool[] =
{
	{
		{"enable_seqscan", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of sequential-scan plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_seqscan,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_indexscan", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of index-scan plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_indexscan,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_indexonlyscan", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of index-only-scan plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_indexonlyscan,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_bitmapscan", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of bitmap-scan plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_bitmapscan,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_tidscan", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of TID scan plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_tidscan,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_sort", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of explicit sort steps."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_sort,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_incremental_sort", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of incremental sort steps."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_incremental_sort,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_hashagg", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of hashed aggregation plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_hashagg,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_material", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of materialization."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_material,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_memoize", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of memoization."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_memoize,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_nestloop", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of nested-loop join plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_nestloop,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_mergejoin", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of merge join plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_mergejoin,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_hashjoin", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of hash join plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_hashjoin,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_gathermerge", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of gather merge plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_gathermerge,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_partitionwise_join", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables partitionwise join."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_partitionwise_join,
		false,
		NULL, NULL, NULL
	},
	{
		{"enable_partitionwise_aggregate", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables partitionwise aggregation and grouping."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_partitionwise_aggregate,
		false,
		NULL, NULL, NULL
	},
	{
		{"enable_parallel_append", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of parallel append plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_parallel_append,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_parallel_hash", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of parallel hash plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_parallel_hash,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_bnl_enable_hashing", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables batched nested loop joins to use hashing to "
						 "process its matches."),
			NULL
		},
		&yb_bnl_enable_hashing,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_bnl_optimize_first_batch", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables batched nested loop joins to predict the "			 	 "size of its first batch and optimize if it's "
						 "smaller than yb_bnl_batch_size."),
			NULL
		},
		&yb_bnl_optimize_first_batch,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_lock_pk_single_rpc", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Use single RPC to select and lock when PK is specified."),
			gettext_noop("If possible (no conflicting filters in the plan), use a single RPC to "
						 "select and lock, when a locking clause is provided, in isolation levels "
						 "REPEATABLE READ and READ COMMITTED.")
		},
		&yb_lock_pk_single_rpc,
		false,
		NULL, NULL, NULL
	},
	{
		{"yb_use_hash_splitting_by_default", PGC_USERSET, QUERY_TUNING_OTHER,
			 gettext_noop("Enables hash splitting as the default method for primary "
					   "key and index sorting in LSM indexes"),
			 gettext_noop("When set to true, the default sorting for the first "
					  "primary/index key column in LSM indexes is HASH, "
					  "Setting this to false changes the default to ASC, "
					  "making it compatible with standard PostgreSQL behavior. "
					  "This setting is useful for optimizing query "
					  "performance, especially for migrations from PostgreSQL "
					  "or scenarios where index-based sorting and sharding "
					  "behavior are critical."),

	 },
		&yb_use_hash_splitting_by_default,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_prefer_bnl", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("If enabled, planner will force a preference of batched"
						" nested loop join plans over classic nested loop"
						" join plans."),
			NULL
		},
		&yb_prefer_bnl,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_enable_batchednl", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of batched nested-loop "
							 "join plans."),
			NULL
		},
		&yb_enable_batchednl,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_enable_parallel_append", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of parallel append plans "
						 "if YB is enabled."),
			NULL
		},
		&yb_enable_parallel_append,
		false,
		NULL, NULL, NULL
	},
	{
		{"yb_enable_bitmapscan", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of YB bitmap-scan plans."),
			gettext_noop("To use YB Bitmap Scans, both yb_enable_bitmapscan "
						 "and enable_bitmapscan must be true.")
		},
		&yb_enable_bitmapscan,
		false,
		NULL, NULL, NULL
	},
	{
		{"enable_partition_pruning", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables plan-time and execution-time partition pruning."),
			gettext_noop("Allows the query planner and executor to compare partition "
						 "bounds to conditions in the query to determine which "
						 "partitions must be scanned."),
			GUC_EXPLAIN
		},
		&enable_partition_pruning,
		true,
		NULL, NULL, NULL
	},
	{
		{"enable_async_append", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables the planner's use of async append plans."),
			NULL,
			GUC_EXPLAIN
		},
		&enable_async_append,
		true,
		NULL, NULL, NULL
	},
	{
		{"geqo", PGC_USERSET, QUERY_TUNING_GEQO,
			gettext_noop("Enables genetic query optimization."),
			gettext_noop("This algorithm attempts to do planning without "
						 "exhaustive searching."),
			GUC_EXPLAIN
		},
		&enable_geqo,
		true,
		NULL, NULL, NULL
	},
	{
		/* Not for general use --- used by SET SESSION AUTHORIZATION */
		{"is_superuser", PGC_INTERNAL, UNGROUPED,
			gettext_noop("Shows whether the current user is a superuser."),
			NULL,
			GUC_REPORT | GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&session_auth_is_superuser,
		false,
		NULL, NULL, NULL
	},
	{
		{"bonjour", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Enables advertising the server via Bonjour."),
			NULL
		},
		&enable_bonjour,
		false,
		check_bonjour, NULL, NULL
	},
	{
		{"track_commit_timestamp", PGC_POSTMASTER, REPLICATION_SENDING,
			gettext_noop("Collects transaction commit time."),
			NULL
		},
		&track_commit_timestamp,
		false,
		NULL, NULL, NULL
	},
	{
		{"ssl", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Enables SSL connections."),
			NULL
		},
		&EnableSSL,
		false,
		check_ssl, NULL, NULL
	},
	{
		{"ssl_passphrase_command_supports_reload", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Controls whether ssl_passphrase_command is called during server reload."),
			NULL
		},
		&ssl_passphrase_command_supports_reload,
		false,
		NULL, NULL, NULL
	},
	{
		{"ssl_prefer_server_ciphers", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Give priority to server ciphersuite order."),
			NULL
		},
		&SSLPreferServerCiphers,
		true,
		NULL, NULL, NULL
	},
	{
		{"fsync", PGC_SIGHUP, WAL_SETTINGS,
			gettext_noop("Forces synchronization of updates to disk."),
			gettext_noop("The server will use the fsync() system call in several places to make "
						 "sure that updates are physically written to disk. This insures "
						 "that a database cluster will recover to a consistent state after "
						 "an operating system or hardware crash.")
		},
		&enableFsync,
		true,
		NULL, NULL, NULL
	},
	{
		{"ignore_checksum_failure", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Continues processing after a checksum failure."),
			gettext_noop("Detection of a checksum failure normally causes PostgreSQL to "
						 "report an error, aborting the current transaction. Setting "
						 "ignore_checksum_failure to true causes the system to ignore the failure "
						 "(but still report a warning), and continue processing. This "
						 "behavior could cause crashes or other serious problems. Only "
						 "has an effect if checksums are enabled."),
			GUC_NOT_IN_SAMPLE
		},
		&ignore_checksum_failure,
		false,
		NULL, NULL, NULL
	},
	{
		{"zero_damaged_pages", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Continues processing past damaged page headers."),
			gettext_noop("Detection of a damaged page header normally causes PostgreSQL to "
						 "report an error, aborting the current transaction. Setting "
						 "zero_damaged_pages to true causes the system to instead report a "
						 "warning, zero out the damaged page, and continue processing. This "
						 "behavior will destroy data, namely all the rows on the damaged page."),
			GUC_NOT_IN_SAMPLE
		},
		&zero_damaged_pages,
		false,
		NULL, NULL, NULL
	},
	{
		{"ignore_invalid_pages", PGC_POSTMASTER, DEVELOPER_OPTIONS,
			gettext_noop("Continues recovery after an invalid pages failure."),
			gettext_noop("Detection of WAL records having references to "
						 "invalid pages during recovery causes PostgreSQL to "
						 "raise a PANIC-level error, aborting the recovery. "
						 "Setting ignore_invalid_pages to true causes "
						 "the system to ignore invalid page references "
						 "in WAL records (but still report a warning), "
						 "and continue recovery. This behavior may cause "
						 "crashes, data loss, propagate or hide corruption, "
						 "or other serious problems. Only has an effect "
						 "during recovery or in standby mode."),
			GUC_NOT_IN_SAMPLE
		},
		&ignore_invalid_pages,
		false,
		NULL, NULL, NULL
	},
	{
		{"full_page_writes", PGC_SIGHUP, WAL_SETTINGS,
			gettext_noop("Writes full pages to WAL when first modified after a checkpoint."),
			gettext_noop("A page write in process during an operating system crash might be "
						 "only partially written to disk.  During recovery, the row changes "
						 "stored in WAL are not enough to recover.  This option writes "
						 "pages when first modified after a checkpoint to WAL so full recovery "
						 "is possible.")
		},
		&fullPageWrites,
		true,
		NULL, NULL, NULL
	},

	{
		{"wal_log_hints", PGC_POSTMASTER, WAL_SETTINGS,
			gettext_noop("Writes full pages to WAL when first modified after a checkpoint, even for a non-critical modification."),
			NULL
		},
		&wal_log_hints,
		false,
		NULL, NULL, NULL
	},

	{
		{"wal_init_zero", PGC_SUSET, WAL_SETTINGS,
			gettext_noop("Writes zeroes to new WAL files before first use."),
			NULL
		},
		&wal_init_zero,
		true,
		NULL, NULL, NULL
	},

	{
		{"wal_recycle", PGC_SUSET, WAL_SETTINGS,
			gettext_noop("Recycles WAL files by renaming them."),
			NULL
		},
		&wal_recycle,
		true,
		NULL, NULL, NULL
	},

	{
		{"log_checkpoints", PGC_SIGHUP, LOGGING_WHAT,
			gettext_noop("Logs each checkpoint."),
			NULL
		},
		&log_checkpoints,
		true,
		NULL, NULL, NULL
	},
	{
		{"log_connections", PGC_SU_BACKEND, LOGGING_WHAT,
			gettext_noop("Logs each successful connection."),
			NULL
		},
		&Log_connections,
		false,
		NULL, NULL, NULL
	},
	{
		{"log_disconnections", PGC_SU_BACKEND, LOGGING_WHAT,
			gettext_noop("Logs end of a session, including duration."),
			NULL
		},
		&Log_disconnections,
		false,
		NULL, NULL, NULL
	},
	{
		{"log_replication_commands", PGC_SUSET, LOGGING_WHAT,
			gettext_noop("Logs each replication command."),
			NULL
		},
		&log_replication_commands,
		false,
		NULL, NULL, NULL
	},
	{
		{"debug_assertions", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows whether the running server has assertion checks enabled."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&assert_enabled,
#ifdef USE_ASSERT_CHECKING
		true,
#else
		false,
#endif
		NULL, NULL, NULL
	},

	{
		{"exit_on_error", PGC_USERSET, ERROR_HANDLING_OPTIONS,
			gettext_noop("Terminate session on any error."),
			NULL
		},
		&ExitOnAnyError,
		false,
		NULL, NULL, NULL
	},
	{
		{"restart_after_crash", PGC_SIGHUP, ERROR_HANDLING_OPTIONS,
			gettext_noop("Reinitialize server after backend crash."),
			NULL
		},
		&restart_after_crash,
		true,
		NULL, NULL, NULL
	},
	{
		{"remove_temp_files_after_crash", PGC_SIGHUP, DEVELOPER_OPTIONS,
			gettext_noop("Remove temporary files after backend crash."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&remove_temp_files_after_crash,
		true,
		NULL, NULL, NULL
	},

	{
		{"log_duration", PGC_SUSET, LOGGING_WHAT,
			gettext_noop("Logs the duration of each completed SQL statement."),
			NULL
		},
		&log_duration,
		false,
		NULL, NULL, NULL
	},
	{
		{"debug_print_parse", PGC_USERSET, LOGGING_WHAT,
			gettext_noop("Logs each query's parse tree."),
			NULL
		},
		&Debug_print_parse,
		false,
		NULL, NULL, NULL
	},
	{
		{"debug_print_rewritten", PGC_USERSET, LOGGING_WHAT,
			gettext_noop("Logs each query's rewritten parse tree."),
			NULL
		},
		&Debug_print_rewritten,
		false,
		NULL, NULL, NULL
	},
	{
		{"debug_print_plan", PGC_USERSET, LOGGING_WHAT,
			gettext_noop("Logs each query's execution plan."),
			NULL
		},
		&Debug_print_plan,
		false,
		NULL, NULL, NULL
	},
	{
		{"debug_pretty_print", PGC_USERSET, LOGGING_WHAT,
			gettext_noop("Indents parse and plan tree displays."),
			NULL
		},
		&Debug_pretty_print,
		true,
		NULL, NULL, NULL
	},
	{
		{"log_parser_stats", PGC_SUSET, STATS_MONITORING,
			gettext_noop("Writes parser performance statistics to the server log."),
			NULL
		},
		&log_parser_stats,
		false,
		check_stage_log_stats, NULL, NULL
	},
	{
		{"log_planner_stats", PGC_SUSET, STATS_MONITORING,
			gettext_noop("Writes planner performance statistics to the server log."),
			NULL
		},
		&log_planner_stats,
		false,
		check_stage_log_stats, NULL, NULL
	},
	{
		{"log_executor_stats", PGC_SUSET, STATS_MONITORING,
			gettext_noop("Writes executor performance statistics to the server log."),
			NULL
		},
		&log_executor_stats,
		false,
		check_stage_log_stats, NULL, NULL
	},
	{
		{"log_statement_stats", PGC_SUSET, STATS_MONITORING,
			gettext_noop("Writes cumulative performance statistics to the server log."),
			NULL
		},
		&log_statement_stats,
		false,
		check_log_stats, NULL, NULL
	},
#ifdef BTREE_BUILD_STATS
	{
		{"log_btree_build_stats", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Logs system resource usage statistics (memory and CPU) on various B-tree operations."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&log_btree_build_stats,
		false,
		NULL, NULL, NULL
	},
#endif

	{
		{"track_activities", PGC_SUSET, STATS_CUMULATIVE,
			gettext_noop("Collects information about executing commands."),
			gettext_noop("Enables the collection of information on the currently "
						 "executing command of each session, along with "
						 "the time at which that command began execution.")
		},
		&pgstat_track_activities,
		true,
		NULL, NULL, NULL
	},
	{
		{"track_counts", PGC_SUSET, STATS_CUMULATIVE,
			gettext_noop("Collects statistics on database activity."),
			NULL
		},
		&pgstat_track_counts,
		true,
		NULL, NULL, NULL
	},
	{
		{"track_io_timing", PGC_SUSET, STATS_CUMULATIVE,
			gettext_noop("Collects timing statistics for database I/O activity."),
			NULL
		},
		&track_io_timing,
		false,
		NULL, NULL, NULL
	},
	{
		{"track_wal_io_timing", PGC_SUSET, STATS_CUMULATIVE,
			gettext_noop("Collects timing statistics for WAL I/O activity."),
			NULL
		},
		&track_wal_io_timing,
		false,
		NULL, NULL, NULL
	},

	{
		{"update_process_title", PGC_SUSET, PROCESS_TITLE,
			gettext_noop("Updates the process title to show the active SQL command."),
			gettext_noop("Enables updating of the process title every time a new SQL command is received by the server.")
		},
		&update_process_title,
#ifdef WIN32
		false,
#else
		true,
#endif
		NULL, NULL, NULL
	},

	{
		{"autovacuum", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Starts the autovacuum subprocess."),
			NULL
		},
		&autovacuum_start_daemon,
		true,
		NULL, NULL, NULL
	},

	{
		{"trace_notify", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Generates debugging output for LISTEN and NOTIFY."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&Trace_notify,
		false,
		NULL, NULL, NULL
	},

#ifdef LOCK_DEBUG
	{
		{"trace_locks", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Emits information about lock usage."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&Trace_locks,
		false,
		NULL, NULL, NULL
	},
	{
		{"trace_userlocks", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Emits information about user lock usage."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&Trace_userlocks,
		false,
		NULL, NULL, NULL
	},
	{
		{"trace_lwlocks", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Emits information about lightweight lock usage."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&Trace_lwlocks,
		false,
		NULL, NULL, NULL
	},
	{
		{"debug_deadlocks", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Dumps information about all current locks when a deadlock timeout occurs."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&Debug_deadlocks,
		false,
		NULL, NULL, NULL
	},
#endif

	{
		{"log_lock_waits", PGC_SUSET, LOGGING_WHAT,
			gettext_noop("Logs long lock waits."),
			NULL
		},
		&log_lock_waits,
		false,
		NULL, NULL, NULL
	},
	{
		{"log_recovery_conflict_waits", PGC_SIGHUP, LOGGING_WHAT,
			gettext_noop("Logs standby recovery conflict waits."),
			NULL
		},
		&log_recovery_conflict_waits,
		false,
		NULL, NULL, NULL
	},
	{
		{"log_hostname", PGC_SIGHUP, LOGGING_WHAT,
			gettext_noop("Logs the host name in the connection logs."),
			gettext_noop("By default, connection logs only show the IP address "
						 "of the connecting host. If you want them to show the host name you "
						 "can turn this on, but depending on your host name resolution "
						 "setup it might impose a non-negligible performance penalty.")
		},
		&log_hostname,
		false,
		NULL, NULL, NULL
	},
	{
		{"transform_null_equals", PGC_USERSET, COMPAT_OPTIONS_CLIENT,
			gettext_noop("Treats \"expr=NULL\" as \"expr IS NULL\"."),
			gettext_noop("When turned on, expressions of the form expr = NULL "
						 "(or NULL = expr) are treated as expr IS NULL, that is, they "
						 "return true if expr evaluates to the null value, and false "
						 "otherwise. The correct behavior of expr = NULL is to always "
						 "return null (unknown).")
		},
		&Transform_null_equals,
		false,
		NULL, NULL, NULL
	},
	{
		{"db_user_namespace", PGC_SIGHUP, CONN_AUTH_AUTH,
			gettext_noop("Enables per-database user names."),
			NULL
		},
		&Db_user_namespace,
		false,
		NULL, NULL, NULL
	},
	{
		{"default_transaction_read_only", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the default read-only status of new transactions."),
			NULL,
			GUC_REPORT
		},
		&DefaultXactReadOnly,
		false,
		NULL, NULL, NULL
	},
	{
		{"transaction_read_only", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the current transaction's read-only status."),
			NULL,
			GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&XactReadOnly,
		false,
		check_transaction_read_only, assign_transaction_read_only, NULL
	},
	{
		{"default_transaction_deferrable", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the default deferrable status of new transactions."),
			NULL
		},
		&DefaultXactDeferrable,
		false,
		NULL, NULL, NULL
	},
	{
		{"transaction_deferrable", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Whether to defer a read-only serializable transaction until it can be executed with no possible serialization failures."),
			NULL,
			GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&XactDeferrable,
		false,
		check_transaction_deferrable, assign_transaction_deferrable, NULL
	},
	{
		{"row_security", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Enable row security."),
			gettext_noop("When enabled, row security will be applied to all users.")
		},
		&row_security,
		true,
		NULL, NULL, NULL
	},
	{
		{"check_function_bodies", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Check routine bodies during CREATE FUNCTION and CREATE PROCEDURE."),
			NULL
		},
		&check_function_bodies,
		true,
		NULL, NULL, NULL
	},
	{
		{"array_nulls", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("Enable input of NULL elements in arrays."),
			gettext_noop("When turned on, unquoted NULL in an array input "
						 "value means a null value; "
						 "otherwise it is taken literally.")
		},
		&Array_nulls,
		true,
		NULL, NULL, NULL
	},

	/*
	 * WITH OIDS support, and consequently default_with_oids, was removed in
	 * PostgreSQL 12, but we tolerate the parameter being set to false to
	 * avoid unnecessarily breaking older dump files.
	 */
	{
		{"default_with_oids", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("WITH OIDS is no longer supported; this can only be false."),
			NULL,
			GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE
		},
		&default_with_oids,
		false,
		check_default_with_oids, NULL, NULL
	},
	{
		{"logging_collector", PGC_POSTMASTER, LOGGING_WHERE,
			gettext_noop("Start a subprocess to capture stderr output and/or csvlogs into log files."),
			NULL
		},
		&Logging_collector,
		false,
		NULL, NULL, NULL
	},
	{
		{"log_truncate_on_rotation", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Truncate existing log files of same name during log rotation."),
			NULL
		},
		&Log_truncate_on_rotation,
		false,
		NULL, NULL, NULL
	},

#ifdef TRACE_SORT
	{
		{"trace_sort", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Emit information about resource usage in sorting."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&trace_sort,
		false,
		NULL, NULL, NULL
	},
#endif

#ifdef TRACE_SYNCSCAN
	/* this is undocumented because not exposed in a standard build */
	{
		{"trace_syncscan", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Generate debugging output for synchronized scanning."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&trace_syncscan,
		false,
		NULL, NULL, NULL
	},
#endif

#ifdef DEBUG_BOUNDED_SORT
	/* this is undocumented because not exposed in a standard build */
	{
		{
			"optimize_bounded_sort", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enable bounded sorting using heap sort."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_EXPLAIN
		},
		&optimize_bounded_sort,
		true,
		NULL, NULL, NULL
	},
#endif

#ifdef WAL_DEBUG
	{
		{"wal_debug", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Emit WAL-related debugging output."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&XLOG_DEBUG,
		false,
		NULL, NULL, NULL
	},
#endif

	{
		{"integer_datetimes", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows whether datetimes are integer based."),
			NULL,
			GUC_REPORT | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&integer_datetimes,
		true,
		NULL, NULL, NULL
	},

	{
		{"krb_caseins_users", PGC_SIGHUP, CONN_AUTH_AUTH,
			gettext_noop("Sets whether Kerberos and GSSAPI user names should be treated as case-insensitive."),
			NULL
		},
		&pg_krb_caseins_users,
		false,
		NULL, NULL, NULL
	},

	{
		{"escape_string_warning", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("Warn about backslash escapes in ordinary string literals."),
			NULL
		},
		&escape_string_warning,
		true,
		NULL, NULL, NULL
	},

	{
		{"standard_conforming_strings", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("Causes '...' strings to treat backslashes literally."),
			NULL,
			GUC_REPORT
		},
		&standard_conforming_strings,
		true,
		NULL, NULL, NULL
	},

	{
		{"synchronize_seqscans", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("Enable synchronized sequential scans."),
			NULL
		},
		&synchronize_seqscans,
		true,
		NULL, NULL, NULL
	},

	{
		{"recovery_target_inclusive", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Sets whether to include or exclude transaction with recovery target."),
			NULL
		},
		&recoveryTargetInclusive,
		true,
		NULL, NULL, NULL
	},

	{
		{"hot_standby", PGC_POSTMASTER, REPLICATION_STANDBY,
			gettext_noop("Allows connections and queries during recovery."),
			NULL
		},
		&EnableHotStandby,
		true,
		NULL, NULL, NULL
	},

	{
		{"hot_standby_feedback", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Allows feedback from a hot standby to the primary that will avoid query conflicts."),
			NULL
		},
		&hot_standby_feedback,
		false,
		NULL, NULL, NULL
	},

	{
		{"in_hot_standby", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows whether hot standby is currently active."),
			NULL,
			GUC_REPORT | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&in_hot_standby,
		false,
		NULL, NULL, show_in_hot_standby
	},

	{
		{"allow_system_table_mods", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Allows modifications of the structure of system tables."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&allowSystemTableMods,
		false,
		NULL, NULL, NULL
	},

	{
		{"ignore_system_indexes", PGC_BACKEND, DEVELOPER_OPTIONS,
			gettext_noop("Disables reading from system indexes."),
			gettext_noop("It does not prevent updating the indexes, so it is safe "
						 "to use.  The worst consequence is slowness."),
			GUC_NOT_IN_SAMPLE
		},
		&IgnoreSystemIndexes,
		false,
		NULL, NULL, NULL
	},

	{
		{"allow_in_place_tablespaces", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Allows tablespaces directly inside pg_tblspc, for testing."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&allow_in_place_tablespaces,
		false,
		NULL, NULL, NULL
	},

	{
		{"lo_compat_privileges", PGC_SUSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("Enables backward compatibility mode for privilege checks on large objects."),
			gettext_noop("Skips privilege checks when reading or modifying large objects, "
						 "for compatibility with PostgreSQL releases prior to 9.0.")
		},
		&lo_compat_privileges,
		false,
		NULL, NULL, NULL
	},

	{
		{"quote_all_identifiers", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("When generating SQL fragments, quote all identifiers."),
			NULL,
		},
		&quote_all_identifiers,
		false,
		NULL, NULL, NULL
	},

	{
		{"data_checksums", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows whether data checksums are turned on for this cluster."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE | GUC_RUNTIME_COMPUTED
		},
		&data_checksums,
		false,
		NULL, NULL, NULL
	},

	{
		{"syslog_sequence_numbers", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Add sequence number to syslog messages to avoid duplicate suppression."),
			NULL
		},
		&syslog_sequence_numbers,
		true,
		NULL, NULL, NULL
	},

	{
		{"syslog_split_messages", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Split messages sent to syslog by lines and to fit into 1024 bytes."),
			NULL
		},
		&syslog_split_messages,
		true,
		NULL, NULL, NULL
	},

	{
		{"parallel_leader_participation", PGC_USERSET, RESOURCES_ASYNCHRONOUS,
			gettext_noop("Controls whether Gather and Gather Merge also run subplans."),
			gettext_noop("Should gather nodes also run subplans or just gather tuples?"),
			GUC_EXPLAIN
		},
		&parallel_leader_participation,
		true,
		NULL, NULL, NULL
	},

	{
		{"jit", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Allow JIT compilation."),
			NULL,
			GUC_EXPLAIN
		},
		&jit_enabled,
		false,
		NULL, NULL, NULL
	},

	{
		{"jit_debugging_support", PGC_SU_BACKEND, DEVELOPER_OPTIONS,
			gettext_noop("Register JIT-compiled functions with debugger."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&jit_debugging_support,
		false,

		/*
		 * This is not guaranteed to be available, but given it's a developer
		 * oriented option, it doesn't seem worth adding code checking
		 * availability.
		 */
		NULL, NULL, NULL
	},

	{
		{"jit_dump_bitcode", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Write out LLVM bitcode to facilitate JIT debugging."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&jit_dump_bitcode,
		false,
		NULL, NULL, NULL
	},

	{
		{"jit_expressions", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Allow JIT compilation of expressions."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&jit_expressions,
		true,
		NULL, NULL, NULL
	},

	{
		{"jit_profiling_support", PGC_SU_BACKEND, DEVELOPER_OPTIONS,
			gettext_noop("Register JIT-compiled functions with perf profiler."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&jit_profiling_support,
		false,

		/*
		 * This is not guaranteed to be available, but given it's a developer
		 * oriented option, it doesn't seem worth adding code checking
		 * availability.
		 */
		NULL, NULL, NULL
	},

	{
		{"jit_tuple_deforming", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Allow JIT compilation of tuple deforming."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&jit_tuple_deforming,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_debug_report_error_stacktrace", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Append stacktrace information for error messages."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_debug_report_error_stacktrace,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_debug_log_catcache_events", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Log details for every catalog cache event such as a cache miss or cache invalidation/refresh."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_debug_log_catcache_events,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_debug_log_internal_restarts", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Log details for internal restarts such as read-restarts, cache-invalidation restarts, or txn restarts."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_debug_log_internal_restarts,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_debug_log_docdb_requests", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Log the contents of all internal (protobuf) requests to DocDB."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_debug_log_docdb_requests,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_create_with_table_oid", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Enables the ability to set table oids when creating tables or indexes."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_create_with_table_oid,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_docdb_tracing", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Enables tracing for the commands in this session."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_docdb_tracing,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_silence_advisory_locks_not_supported_error", PGC_USERSET, LOCK_MANAGEMENT,
			gettext_noop("Silence the advisory locks not supported error message."),
			gettext_noop(
					"Enable this with high caution. It was added to avoid disruption for users who were "
					"already using advisory locks but seeing success messages without the lock really being "
					"acquired. Such users should take the necessary steps to modify their application to "
					"remove usage of advisory locks. See https://github.com/yugabyte/yugabyte-db/issues/3642 "
					"for details."),
			GUC_NOT_IN_SAMPLE
		},
		&yb_silence_advisory_locks_not_supported_error,
		false,
		NULL, NULL, NULL
	},

	{
		{"data_sync_retry", PGC_POSTMASTER, ERROR_HANDLING_OPTIONS,
			gettext_noop("Whether to continue running after a failure to sync data files."),
		},
		&data_sync_retry,
		false,
		NULL, NULL, NULL
	},

	{
		{"wal_receiver_create_temp_slot", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets whether a WAL receiver should create a temporary replication slot if no permanent slot is configured."),
		},
		&wal_receiver_create_temp_slot,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_read_from_followers", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Allow any statement that generates a read request to go to any node."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_read_from_followers,
		false,
		check_follower_reads, assign_follower_reads, NULL
	},

	{
		{"yb_follower_reads_behavior_before_fixing_20482", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Controls whether ysql follower reads that is enabled "
						 "inside a transaction block should take effect in the same "
						 "transaction or not. Prior to fixing #20482 the behavior was that "
						 "the change does not affect the current transaction but only "
						 "affects subsequent transactions. The flag is intended to be used if "
						 "there is a customer who relies on the old behavior."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_follower_reads_behavior_before_fixing_20482,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_non_ddl_txn_for_sys_tables_allowed", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Enables the use of regular transactions for operating on system catalog tables in case a DDL transaction has not been started."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_non_ddl_txn_for_sys_tables_allowed,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_format_funcs_include_yb_metadata", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Include DocDB metadata (such as tablet splits) in formatting functions exporting system catalog information."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_format_funcs_include_yb_metadata,
		false,
		NULL, NULL, NULL
	},
	{
		{"yb_enable_geolocation_costing", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Allow the optimizer to cost and choose between duplicate indexes based on locality"),
			NULL
		},
		&yb_enable_geolocation_costing,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_pushdown_strict_inequality", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("If true, strict inequality filters are pushed down."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_pushdown_strict_inequality,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_pushdown_is_not_null", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("If true, IS NOT NULL is pushed down."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_pushdown_is_not_null,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_pg_locks", PGC_SUSET, LOCK_MANAGEMENT,
			gettext_noop("Enable the pg_locks view. This view provides information about the locks held by active postgres sessions."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_pg_locks,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_replication_commands", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Enable the replication commands for Publication and Replication Slots."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_replication_commands,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_replication_slot_consumption", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Enable consumption of changes via replication slots. "
						 "This feature is currently in active development and "
						 "should not be enabled."),
			NULL,
			GUC_NOT_IN_SAMPLE,
		},
		&yb_enable_replication_slot_consumption,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_replica_identity", PGC_SUSET, REPLICATION_SENDING,
			gettext_noop("Allow changing replica identity via ALTER TABLE command"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_replica_identity,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_allow_replication_slot_lsn_types", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Allow specifying LSN type while creating replication slot"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_allow_replication_slot_lsn_types,
		true,
		NULL, NULL, NULL
	},

	{
		{"ysql_upgrade_mode", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Enter a special mode designed specifically for YSQL cluster upgrades. "
						 "Allows creating new system tables with given relation and type OID. "
						 "Do NOT use this unless you know exactly what you're doing, consequences "
						 "may be dire!"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&IsYsqlUpgrade,
		false,
		NULL, assign_ysql_upgrade_mode, NULL
	},

	{
		{"yb_binary_restore", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Enter a special mode designed specifically for YSQL binary restore."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_binary_restore,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_ignore_pg_class_oids", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Ignores requests to set pg_class OIDs in yb_binary_restore mode"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_ignore_pg_class_oids,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_test_system_catalogs_creation", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Relaxes some internal sanity checks for system "
						 "catalogs to allow creating them."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_system_catalogs_creation,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_test_fail_next_ddl", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("When set, the next DDL will fail right before "
						 "commit."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_fail_next_ddl,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_test_fail_all_drops", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("When set, all drops will fail"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_fail_all_drops,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_test_fail_next_inc_catalog_version", PGC_USERSET,DEVELOPER_OPTIONS,
			gettext_noop("When set, the next increment catalog version will "
						 "fail right before it's done. This only works when "
						 "catalog version is stored in pg_yb_catalog_version."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_fail_next_inc_catalog_version,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_test_fail_table_rewrite_after_creation", PGC_USERSET,
			DEVELOPER_OPTIONS,
			gettext_noop("When set, DDLs that rewrite tables/indexes will"
						 " fail after the new table is created."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_fail_table_rewrite_after_creation,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_test_stay_in_global_catalog_version_mode", PGC_SUSET,
			DEVELOPER_OPTIONS,
			gettext_noop("When set, this PG backend will stay in global "
						 "catalog version mode. Used in testing to simulate "
						 "a lagging PG backend during the finalization phase "
						 "of cluster upgrade to a new release that has the "
						 "per-database catalog version mode on by default."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_stay_in_global_catalog_version_mode,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_test_table_rewrite_keep_old_table", PGC_SUSET,
			DEVELOPER_OPTIONS,
			gettext_noop("When set, DDLs that rewrite tables/indexes will"
						 " not drop the old relfilenode/DocDB table."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_table_rewrite_keep_old_table,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_force_catalog_update_on_next_ddl", PGC_USERSET,
			DEVELOPER_OPTIONS,
			gettext_noop("Make the next DDL update the catalog in force mode "
			"which allows it to operate even during ysql major catalog "
			"upgrades. WARNING: This is a dangerous option and should be used "
			"only for DDLs on temp tables, and other transient objects."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_force_catalog_update_on_next_ddl,
		false,
		NULL, NULL, NULL
	},

	{
		{"force_global_transaction", PGC_USERSET, UNGROUPED,
			gettext_noop("Forces use of global transaction table."),
			NULL
		},
		&yb_force_global_transaction,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_disable_transactional_writes", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the boolean flag to disable transaction writes."),
			NULL
		},
		&yb_disable_transactional_writes,
		false,
		NULL, NULL, NULL
	},
	{
		{"suppress_nonpg_logs", PGC_SIGHUP, LOGGING_WHAT,
			gettext_noop("Suppresses non-Postgres logs from appearing in the Postgres log file."),
			NULL
		},
		&suppress_nonpg_logs,
		false,
		NULL, NULL, NULL
	},
	{
		{"yb_enable_optimizer_statistics", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables use of the PostgreSQL selectivity estimation which utilizes "
			"table statistics collected with ANALYZE. When disabled, a simpler heuristics based "
			"selectivity estimation is used."),
			NULL
		},
		&yb_enable_optimizer_statistics,
		false,
		NULL, NULL, NULL
	},
	{
		{"yb_enable_expression_pushdown", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Push supported expressions down to DocDB for evaluation."),
			NULL
		},
		&yb_enable_expression_pushdown,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_enable_distinct_pushdown", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Push supported DISTINCT operations to DocDB."),
			NULL
		},
		&yb_enable_distinct_pushdown,
		true,
		NULL, NULL, NULL
	},

	{
		/* Intended for rolling upgrade scenarios; tied to an auto-flag. */
		{"yb_enable_index_aggregate_pushdown", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Push supported index aggregate operations to DocDB."),
			gettext_noop("This affects IndexScan, not IndexOnlyScan."),
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_index_aggregate_pushdown,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_hash_batch_in", PGC_USERSET, QUERY_TUNING_METHOD,
		gettext_noop("GUC variable that enables batching RPCs of generated for IN queries on hash "
					 "keys issued to the same tablets."),
		NULL
		},
		&yb_enable_hash_batch_in,
		true,
		NULL, NULL, NULL
	},
	{
		{"yb_bypass_cond_recheck", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("If true then condition rechecking is bypassed at YSQL if the condition is bound to DocDB."),
			NULL
		},
		&yb_bypass_cond_recheck,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_upsert_mode", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the boolean flag to enable or disable upsert mode for writes."),
			NULL
		},
		&yb_enable_upsert_mode,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_planner_custom_plan_for_partition_pruning", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("If enabled, choose custom plan over generic plan "
						 " for prepared statements based on the number of "
						 "partition pruned."),
			NULL
		},
		&enable_choose_custom_plan_for_partition_pruning,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_make_next_ddl_statement_nonbreaking", PGC_SUSET, CUSTOM_OPTIONS,
			gettext_noop("When set, the next ddl statement will not cause "
						 "running transactions to abort. This only affects "
						 "the next ddl statement and resets automatically."),
			NULL
		},
		&yb_make_next_ddl_statement_nonbreaking,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_make_next_ddl_statement_nonincrementing", PGC_SUSET, CUSTOM_OPTIONS,
			gettext_noop("When set, the next ddl statement will not cause "
						 "catalog version to increment. This only affects "
						 "the next ddl statement and resets automatically."),
			NULL
		},
		&yb_make_next_ddl_statement_nonincrementing,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_plpgsql_disable_prefetch_in_for_query", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Disable prefetching in a PLPGSQL FOR loop over a query."),
			NULL
		},
		&yb_plpgsql_disable_prefetch_in_for_query,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_sequence_pushdown", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Allow nextval() to fetch the value range and advance "
						 "the sequence value in a single operation."),
			NULL
		},
		&yb_enable_sequence_pushdown,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_memory_tracking", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Enables tracking of memory consumption of the PostgreSQL "
						  "process. This enhances garbage collection behaviour and memory usage "
						  "observability."),
			NULL
		},
		&yb_enable_memory_tracking,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_disable_wait_for_backends_catalog_version", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Disable waiting for backends to have up-to-date "
						 "pg_catalog. This could cause correctness issues."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_disable_wait_for_backends_catalog_version,
		false,
		NULL, NULL, NULL
	},

	{
		/* YB: Not for general use */
		{"yb_is_client_ysqlconnmgr", PGC_BACKEND, UNGROUPED,
			gettext_noop("Identifies that connection is created by "
						"Ysql Connection Manager."),
			NULL
		},
		&yb_is_client_ysqlconnmgr,
		false,
		yb_is_client_ysqlconnmgr_check_hook, yb_is_client_ysqlconnmgr_assign_hook, NULL
	},

	{
		{"yb_enable_base_scans_cost_model", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Enables YB cost model for Sequential and Index scans. "
						 "This feature is currently in preview."),
			NULL
		},
		&yb_enable_base_scans_cost_model,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_add_column_missing_default", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Enable using the default value for existing rows"
						 " after an ADD COLUMN ... DEFAULT operation."),
			NULL
		},
		&yb_enable_add_column_missing_default,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_ddl_rollback_enabled", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("If set, any DDL that involves DocDB schema changes will have those "
						 "changes rolled back upon failure."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_ddl_rollback_enabled,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_ddl_atomicity_infra", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Used along side with yb_ddl_rollback_enabled to control "
						 "whether DDL atomicity is enabled."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_ddl_atomicity_infra,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_explain_hide_non_deterministic_fields", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("If set, all fields that vary from run to run are hidden from "
						 "the output of EXPLAIN"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_explain_hide_non_deterministic_fields,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_alter_table_rewrite", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Enable ALTER TABLE rewrite operations"),
			NULL
		},
		&yb_enable_alter_table_rewrite,
		true,
		NULL, NULL, NULL
	},

	{
		/* YB: Not for general use */
		{"yb_use_tserver_key_auth", PGC_BACKEND, UNGROUPED,
			gettext_noop("If set, the client connection will be authenticated via "
						 "'yb-tserver-key' auth"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_use_tserver_key_auth,
		false,
		yb_use_tserver_key_auth_check_hook, NULL, NULL
	},

	{
		{"yb_enable_saop_pushdown", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Push supported scalar array operations down "
						 "to DocDB for evaluation."),
			NULL
		},
		&yb_enable_saop_pushdown,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_ash", PGC_POSTMASTER, STATS_MONITORING,
			gettext_noop("Enable Active Session History for sampling and instrumenting YSQL "
						 "and YCQL queries, and various background activities."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_ash,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_update_optimization_infra", PGC_SIGHUP, QUERY_TUNING_OTHER,
			gettext_noop("Enables optimizations of YSQL UPDATE queries. This includes "
						 "(but not limited to) skipping redundant secondary index updates "
						 "and redundant constraint checks."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_update_optimization_options.has_infra,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_skip_redundant_update_ops", PGC_SIGHUP, QUERY_TUNING_OTHER,
			gettext_noop("Enables the comparison of old and new values of columns specified in the "
						 "SET clause of YSQL UPDATE queries to skip redundant secondary index "
						 "updates and redundant constraint checks."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_update_optimization_options.is_enabled,
		false,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_inplace_index_update", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Enables the in-place update of non-key columns of secondary indexes "
						 "when key columns of the index are not updated. This is useful when "
						 "updating the included columns in a covering index among others."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_inplace_index_update,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_fkey_catcache", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Enable preloading of foreign key information into the relation cache."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_fkey_catcache,
		true,
		NULL, NULL, NULL
	},

	{
		{"yb_enable_nop_alter_role_optimization", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Enable nop alter role statement optimization to avoid catalog version "
						 "increment if the alter role statement does not involve any change."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_enable_nop_alter_role_optimization,
		true,
		NULL, NULL, NULL
	},

	/* End-of-list marker */
	{
		{NULL, 0, 0, NULL, NULL}, NULL, false, NULL, NULL, NULL
	}
};


static struct config_int ConfigureNamesInt[] =
{
	{
		{"archive_timeout", PGC_SIGHUP, WAL_ARCHIVING,
			gettext_noop("Sets the amount of time to wait before forcing a "
						 "switch to the next WAL file."),
			NULL,
			GUC_UNIT_S
		},
		&XLogArchiveTimeout,
		0, 0, INT_MAX / 2,
		NULL, NULL, NULL
	},

	{
		{"post_auth_delay", PGC_BACKEND, DEVELOPER_OPTIONS,
			gettext_noop("Sets the amount of time to wait after "
						 "authentication on connection startup."),
			gettext_noop("This allows attaching a debugger to the process."),
			GUC_NOT_IN_SAMPLE | GUC_UNIT_S
		},
		&PostAuthDelay,
		0, 0, INT_MAX / 1000000,
		NULL, NULL, NULL
	},
	{
		{"yb_bnl_batch_size", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Batch size of nested loop joins"),
			gettext_noop("Set to 1 to always use simple nested loop joins"),
			GUC_NOT_IN_SAMPLE
		},
		&yb_bnl_batch_size,
		1024, 1, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"yb_explicit_row_locking_batch_size", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Batch size of explicit row locking"),
			gettext_noop("Set to 1 to conserve default behavior, "
							"batching is disabled by default."),
			GUC_NOT_IN_SAMPLE
		},
		&yb_explicit_row_locking_batch_size,
		1024, 1, INT_MAX,
		check_yb_explicit_row_locking_batch_size, NULL, NULL
	},
	{
		{"default_statistics_target", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Sets the default statistics target."),
			gettext_noop("This applies to table columns that have not had a "
						 "column-specific target set via ALTER TABLE SET STATISTICS.")
		},
		&default_statistics_target,
		100, 1, 10000,
		NULL, NULL, NULL
	},
	{
		{"from_collapse_limit", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Sets the FROM-list size beyond which subqueries "
						 "are not collapsed."),
			gettext_noop("The planner will merge subqueries into upper "
						 "queries if the resulting FROM list would have no more than "
						 "this many items."),
			GUC_EXPLAIN
		},
		&from_collapse_limit,
		8, 1, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"join_collapse_limit", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Sets the FROM-list size beyond which JOIN "
						 "constructs are not flattened."),
			gettext_noop("The planner will flatten explicit JOIN "
						 "constructs into lists of FROM items whenever a "
						 "list of no more than this many items would result."),
			GUC_EXPLAIN
		},
		&join_collapse_limit,
		8, 1, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"geqo_threshold", PGC_USERSET, QUERY_TUNING_GEQO,
			gettext_noop("Sets the threshold of FROM items beyond which GEQO is used."),
			NULL,
			GUC_EXPLAIN
		},
		&geqo_threshold,
		12, 2, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"geqo_effort", PGC_USERSET, QUERY_TUNING_GEQO,
			gettext_noop("GEQO: effort is used to set the default for other GEQO parameters."),
			NULL,
			GUC_EXPLAIN
		},
		&Geqo_effort,
		DEFAULT_GEQO_EFFORT, MIN_GEQO_EFFORT, MAX_GEQO_EFFORT,
		NULL, NULL, NULL
	},
	{
		{"geqo_pool_size", PGC_USERSET, QUERY_TUNING_GEQO,
			gettext_noop("GEQO: number of individuals in the population."),
			gettext_noop("Zero selects a suitable default value."),
			GUC_EXPLAIN
		},
		&Geqo_pool_size,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"geqo_generations", PGC_USERSET, QUERY_TUNING_GEQO,
			gettext_noop("GEQO: number of iterations of the algorithm."),
			gettext_noop("Zero selects a suitable default value."),
			GUC_EXPLAIN
		},
		&Geqo_generations,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		/* This is PGC_SUSET to prevent hiding from log_lock_waits. */
		{"deadlock_timeout", PGC_SUSET, LOCK_MANAGEMENT,
			gettext_noop("Sets the time to wait on a lock before checking for deadlock."),
			NULL,
			GUC_UNIT_MS
		},
		&DeadlockTimeout,
		1000, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_locks_min_txn_age", PGC_USERSET, LOCK_MANAGEMENT,
			gettext_noop("Sets the minimum transaction age for results from pg_locks."),
			NULL,
			GUC_UNIT_MS
		},
		&yb_locks_min_txn_age,
		1000, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_locks_max_transactions", PGC_USERSET, LOCK_MANAGEMENT,
			gettext_noop("Sets the maximum number of transactions for which to return rows in pg_locks."),
			NULL
		},
		&yb_locks_max_transactions,
		16, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_walsender_poll_sleep_duration_nonempty_ms", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Time in milliseconds for which Walsender waits before"
						 " fetching the next batch of changes from the CDC"
						 " service in case the last received response was"
						 " non-empty."),
			NULL,
			GUC_UNIT_MS
		},
		&yb_walsender_poll_sleep_duration_nonempty_ms,
		1, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_walsender_poll_sleep_duration_empty_ms", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Time in milliseconds for which Walsender waits before"
						 " fetching the next batch of changes from the CDC"
						 " service in case the last received response was"
						 " empty."),
			NULL,
			GUC_UNIT_MS
		},
		&yb_walsender_poll_sleep_duration_empty_ms,
		1 * 1000, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_reorderbuffer_max_changes_in_memory", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Maximum number of changes kept in memory per transaction "
						 "in reorder buffer, which is used in streaming changes via "
						 "logical replication. After that, changes are spooled to disk."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_reorderbuffer_max_changes_in_memory,
		4096, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_locks_txn_locks_per_tablet", PGC_USERSET, LOCK_MANAGEMENT,
		 gettext_noop("Sets the maximum number of rows per transaction per tablet to return in pg_locks."),
		 NULL
		},
		&yb_locks_txn_locks_per_tablet,
		200, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"ysql_conn_mgr_sticky_object_count", PGC_INTERNAL, CUSTOM_OPTIONS,
			gettext_noop("Shows the count of database objects that require a sticky connection within this session."),
			NULL
		},
		&ysql_conn_mgr_sticky_object_count,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"max_standby_archive_delay", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the maximum delay before canceling queries when a hot standby server is processing archived WAL data."),
			NULL,
			GUC_UNIT_MS
		},
		&max_standby_archive_delay,
		30 * 1000, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"max_standby_streaming_delay", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the maximum delay before canceling queries when a hot standby server is processing streamed WAL data."),
			NULL,
			GUC_UNIT_MS
		},
		&max_standby_streaming_delay,
		30 * 1000, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"recovery_min_apply_delay", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the minimum delay for applying changes during recovery."),
			NULL,
			GUC_UNIT_MS
		},
		&recovery_min_apply_delay,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"wal_receiver_status_interval", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the maximum interval between WAL receiver status reports to the sending server."),
			NULL,
			GUC_UNIT_S
		},
		&wal_receiver_status_interval,
		10, 0, INT_MAX / 1000,
		NULL, NULL, NULL
	},

	{
		{"wal_receiver_timeout", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the maximum wait time to receive data from the sending server."),
			NULL,
			GUC_UNIT_MS
		},
		&wal_receiver_timeout,
		60 * 1000, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"max_connections", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the maximum number of concurrent connections."),
			NULL
		},
		&MaxConnections,
		100, 1, MAX_BACKENDS,
		check_maxconnections, NULL, yb_show_maxconnections
	},

	{
		/* see max_connections */
		{"superuser_reserved_connections", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the number of connection slots reserved for superusers."),
			NULL
		},
		&ReservedBackends,
		3, 0, MAX_BACKENDS,
		NULL, NULL, NULL
	},

	{
		{"min_dynamic_shared_memory", PGC_POSTMASTER, RESOURCES_MEM,
			gettext_noop("Amount of dynamic shared memory reserved at startup."),
			NULL,
			GUC_UNIT_MB
		},
		&min_dynamic_shared_memory,
		0, 0, (int) Min((size_t) INT_MAX, SIZE_MAX / (1024 * 1024)),
		NULL, NULL, NULL
	},

	/*
	 * We sometimes multiply the number of shared buffers by two without
	 * checking for overflow, so we mustn't allow more than INT_MAX / 2.
	 */
	{
		{"shared_buffers", PGC_POSTMASTER, RESOURCES_MEM,
			gettext_noop("Sets the number of shared memory buffers used by the server."),
			NULL,
			GUC_UNIT_BLOCKS
		},
		&NBuffers,
		16384, 16, INT_MAX / 2,
		NULL, NULL, NULL
	},

	{
		{"shared_memory_size", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the size of the server's main shared memory area (rounded up to the nearest MB)."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE | GUC_UNIT_MB | GUC_RUNTIME_COMPUTED
		},
		&shared_memory_size_mb,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"shared_memory_size_in_huge_pages", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the number of huge pages needed for the main shared memory area."),
			gettext_noop("-1 indicates that the value could not be determined."),
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE | GUC_RUNTIME_COMPUTED
		},
		&shared_memory_size_in_huge_pages,
		-1, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"temp_buffers", PGC_USERSET, RESOURCES_MEM,
			gettext_noop("Sets the maximum number of temporary buffers used by each session."),
			NULL,
			GUC_UNIT_BLOCKS | GUC_EXPLAIN
		},
		&num_temp_buffers,
		1024, 100, INT_MAX / 2,
		check_temp_buffers, NULL, NULL
	},

	{
		{"port", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the TCP port the server listens on."),
			NULL
		},
		&PostPortNumber,
		DEF_PGPORT, 1, 65535,
		NULL, NULL, NULL
	},

	{
		{"unix_socket_permissions", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the access permissions of the Unix-domain socket."),
			gettext_noop("Unix-domain sockets use the usual Unix file system "
						 "permission set. The parameter value is expected "
						 "to be a numeric mode specification in the form "
						 "accepted by the chmod and umask system calls. "
						 "(To use the customary octal format the number must "
						 "start with a 0 (zero).)")
		},
		&Unix_socket_permissions,
		0777, 0000, 0777,
		NULL, NULL, show_unix_socket_permissions
	},

	{
		{"log_file_mode", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the file permissions for log files."),
			gettext_noop("The parameter value is expected "
						 "to be a numeric mode specification in the form "
						 "accepted by the chmod and umask system calls. "
						 "(To use the customary octal format the number must "
						 "start with a 0 (zero).)")
		},
		&Log_file_mode,
		0600, 0000, 0777,
		NULL, NULL, show_log_file_mode
	},


	{
		{"data_directory_mode", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the mode of the data directory."),
			gettext_noop("The parameter value is a numeric mode specification "
						 "in the form accepted by the chmod and umask system "
						 "calls. (To use the customary octal format the number "
						 "must start with a 0 (zero).)"),
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE | GUC_RUNTIME_COMPUTED
		},
		&data_directory_mode,
		0700, 0000, 0777,
		NULL, NULL, show_data_directory_mode
	},

	{
		{"work_mem", PGC_USERSET, RESOURCES_MEM,
			gettext_noop("Sets the maximum memory to be used for query workspaces."),
			gettext_noop("This much memory can be used by each internal "
						 "sort operation and hash table before switching to "
						 "temporary disk files."),
			GUC_UNIT_KB | GUC_EXPLAIN
		},
		&work_mem,
		4096, 64, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	{
		{"maintenance_work_mem", PGC_USERSET, RESOURCES_MEM,
			gettext_noop("Sets the maximum memory to be used for maintenance operations."),
			gettext_noop("This includes operations such as VACUUM and CREATE INDEX."),
			GUC_UNIT_KB
		},
		&maintenance_work_mem,
		65536, 1024, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	{
		{"logical_decoding_work_mem", PGC_USERSET, RESOURCES_MEM,
			gettext_noop("Sets the maximum memory to be used for logical decoding."),
			gettext_noop("This much memory can be used by each internal "
						 "reorder buffer before spilling to disk."),
			GUC_UNIT_KB
		},
		&logical_decoding_work_mem,
		65536, 64, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	/*
	 * We use the hopefully-safely-small value of 100kB as the compiled-in
	 * default for max_stack_depth.  InitializeGUCOptions will increase it if
	 * possible, depending on the actual platform-specific stack limit.
	 */
	{
		{"max_stack_depth", PGC_SUSET, RESOURCES_MEM,
			gettext_noop("Sets the maximum stack depth, in kilobytes."),
			NULL,
			GUC_UNIT_KB
		},
		&max_stack_depth,
		100, 100, MAX_KILOBYTES,
		check_max_stack_depth, assign_max_stack_depth, NULL
	},

	{
		{"temp_file_limit", PGC_SUSET, RESOURCES_DISK,
			gettext_noop("Limits the total size of all temporary files used by each process."),
			gettext_noop("-1 means no limit."),
			GUC_UNIT_KB
		},
		&temp_file_limit,
		1024 * 1024, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"vacuum_cost_page_hit", PGC_USERSET, RESOURCES_VACUUM_DELAY,
			gettext_noop("Vacuum cost for a page found in the buffer cache."),
			NULL
		},
		&VacuumCostPageHit,
		1, 0, 10000,
		NULL, NULL, NULL
	},

	{
		{"vacuum_cost_page_miss", PGC_USERSET, RESOURCES_VACUUM_DELAY,
			gettext_noop("Vacuum cost for a page not found in the buffer cache."),
			NULL
		},
		&VacuumCostPageMiss,
		2, 0, 10000,
		NULL, NULL, NULL
	},

	{
		{"vacuum_cost_page_dirty", PGC_USERSET, RESOURCES_VACUUM_DELAY,
			gettext_noop("Vacuum cost for a page dirtied by vacuum."),
			NULL
		},
		&VacuumCostPageDirty,
		20, 0, 10000,
		NULL, NULL, NULL
	},

	{
		{"vacuum_cost_limit", PGC_USERSET, RESOURCES_VACUUM_DELAY,
			gettext_noop("Vacuum cost amount available before napping."),
			NULL
		},
		&VacuumCostLimit,
		200, 1, 10000,
		NULL, NULL, NULL
	},

	{
		{"autovacuum_vacuum_cost_limit", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Vacuum cost amount available before napping, for autovacuum."),
			NULL
		},
		&autovacuum_vac_cost_limit,
		-1, -1, 10000,
		NULL, NULL, NULL
	},

	{
		{"max_files_per_process", PGC_POSTMASTER, RESOURCES_KERNEL,
			gettext_noop("Sets the maximum number of simultaneously open files for each server process."),
			NULL
		},
		&max_files_per_process,
		1000, 64, INT_MAX,
		NULL, NULL, NULL
	},

	/*
	 * See also CheckRequiredParameterValues() if this parameter changes
	 */
	{
		{"max_prepared_transactions", PGC_POSTMASTER, RESOURCES_MEM,
			gettext_noop("Sets the maximum number of simultaneously prepared transactions."),
			NULL
		},
		&max_prepared_xacts,
		0, 0, MAX_BACKENDS,
		NULL, NULL, NULL
	},

#ifdef LOCK_DEBUG
	{
		{"trace_lock_oidmin", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Sets the minimum OID of tables for tracking locks."),
			gettext_noop("Is used to avoid output on system tables."),
			GUC_NOT_IN_SAMPLE
		},
		&Trace_lock_oidmin,
		FirstNormalObjectId, 0, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"trace_lock_table", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Sets the OID of the table with unconditionally lock tracing."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&Trace_lock_table,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},
#endif

	{
		{"statement_timeout", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the maximum allowed duration of any statement."),
			gettext_noop("A value of 0 turns off the timeout."),
			GUC_UNIT_MS
		},
		&StatementTimeout,
		0, 0, INT_MAX,
		NULL, YBCSetTimeout, NULL
	},


	{
		{"retry_min_backoff", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the minimum backoff in milliseconds between retries."),
			NULL,
			GUC_UNIT_MS
		},
		&RetryMinBackoffMsecs,
		10, 0, INT_MAX,
		check_min_backoff, NULL, NULL
	},

	{
		{"retry_max_backoff", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the maximum backoff in milliseconds between retries."),
			NULL,
			GUC_UNIT_MS
		},
		&RetryMaxBackoffMsecs,
		1000, 0, INT_MAX,
		check_max_backoff, NULL, NULL
	},

	{
		{"lock_timeout", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the maximum allowed duration of any wait for a lock."),
			gettext_noop("A value of 0 turns off the timeout."),
			GUC_UNIT_MS
		},
		&LockTimeout,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"idle_in_transaction_session_timeout", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the maximum allowed idle time between queries, when in a transaction."),
			gettext_noop("A value of 0 turns off the timeout."),
			GUC_UNIT_MS
		},
		&IdleInTransactionSessionTimeout,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"idle_session_timeout", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the maximum allowed idle time between queries, when not in a transaction."),
			gettext_noop("A value of 0 turns off the timeout."),
			GUC_UNIT_MS
		},
		&IdleSessionTimeout,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"vacuum_freeze_min_age", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Minimum age at which VACUUM should freeze a table row."),
			NULL
		},
		&vacuum_freeze_min_age,
		50000000, 0, 1000000000,
		NULL, NULL, NULL
	},

	{
		{"vacuum_freeze_table_age", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Age at which VACUUM should scan whole table to freeze tuples."),
			NULL
		},
		&vacuum_freeze_table_age,
		150000000, 0, 2000000000,
		NULL, NULL, NULL
	},

	{
		{"vacuum_multixact_freeze_min_age", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Minimum age at which VACUUM should freeze a MultiXactId in a table row."),
			NULL
		},
		&vacuum_multixact_freeze_min_age,
		5000000, 0, 1000000000,
		NULL, NULL, NULL
	},

	{
		{"vacuum_multixact_freeze_table_age", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Multixact age at which VACUUM should scan whole table to freeze tuples."),
			NULL
		},
		&vacuum_multixact_freeze_table_age,
		150000000, 0, 2000000000,
		NULL, NULL, NULL
	},

	{
		{"vacuum_defer_cleanup_age", PGC_SIGHUP, REPLICATION_PRIMARY,
			gettext_noop("Number of transactions by which VACUUM and HOT cleanup should be deferred, if any."),
			NULL
		},
		&vacuum_defer_cleanup_age,
		0, 0, 1000000,			/* see ComputeXidHorizons */
		NULL, NULL, NULL
	},
	{
		{"vacuum_failsafe_age", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Age at which VACUUM should trigger failsafe to avoid a wraparound outage."),
			NULL
		},
		&vacuum_failsafe_age,
		1600000000, 0, 2100000000,
		NULL, NULL, NULL
	},
	{
		{"vacuum_multixact_failsafe_age", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Multixact age at which VACUUM should trigger failsafe to avoid a wraparound outage."),
			NULL
		},
		&vacuum_multixact_failsafe_age,
		1600000000, 0, 2100000000,
		NULL, NULL, NULL
	},

	/*
	 * See also CheckRequiredParameterValues() if this parameter changes
	 */
	{
		{"max_locks_per_transaction", PGC_POSTMASTER, LOCK_MANAGEMENT,
			gettext_noop("Sets the maximum number of locks per transaction."),
			gettext_noop("The shared lock table is sized on the assumption that "
						 "at most max_locks_per_transaction * max_connections distinct "
						 "objects will need to be locked at any one time.")
		},
		&max_locks_per_xact,
		64, 10, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"max_pred_locks_per_transaction", PGC_POSTMASTER, LOCK_MANAGEMENT,
			gettext_noop("Sets the maximum number of predicate locks per transaction."),
			gettext_noop("The shared predicate lock table is sized on the assumption that "
						 "at most max_pred_locks_per_transaction * max_connections distinct "
						 "objects will need to be locked at any one time.")
		},
		&max_predicate_locks_per_xact,
		64, 10, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"max_pred_locks_per_relation", PGC_SIGHUP, LOCK_MANAGEMENT,
			gettext_noop("Sets the maximum number of predicate-locked pages and tuples per relation."),
			gettext_noop("If more than this total of pages and tuples in the same relation are locked "
						 "by a connection, those locks are replaced by a relation-level lock.")
		},
		&max_predicate_locks_per_relation,
		-2, INT_MIN, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"max_pred_locks_per_page", PGC_SIGHUP, LOCK_MANAGEMENT,
			gettext_noop("Sets the maximum number of predicate-locked tuples per page."),
			gettext_noop("If more than this number of tuples on the same page are locked "
						 "by a connection, those locks are replaced by a page-level lock.")
		},
		&max_predicate_locks_per_page,
		2, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"authentication_timeout", PGC_SIGHUP, CONN_AUTH_AUTH,
			gettext_noop("Sets the maximum allowed time to complete client authentication."),
			NULL,
			GUC_UNIT_S
		},
		&AuthenticationTimeout,
		60, 1, 600,
		NULL, NULL, NULL
	},

	{
		/* Not for general use */
		{"pre_auth_delay", PGC_SIGHUP, DEVELOPER_OPTIONS,
			gettext_noop("Sets the amount of time to wait before "
						 "authentication on connection startup."),
			gettext_noop("This allows attaching a debugger to the process."),
			GUC_NOT_IN_SAMPLE | GUC_UNIT_S
		},
		&PreAuthDelay,
		0, 0, 60,
		NULL, NULL, NULL
	},

	{
		{"wal_decode_buffer_size", PGC_POSTMASTER, WAL_RECOVERY,
			gettext_noop("Buffer size for reading ahead in the WAL during recovery."),
			gettext_noop("Maximum distance to read ahead in the WAL to prefetch referenced data blocks."),
			GUC_UNIT_BYTE
		},
		&wal_decode_buffer_size,
		512 * 1024, 64 * 1024, MaxAllocSize,
		NULL, NULL, NULL
	},

	{
		{"wal_keep_size", PGC_SIGHUP, REPLICATION_SENDING,
			gettext_noop("Sets the size of WAL files held for standby servers."),
			NULL,
			GUC_UNIT_MB
		},
		&wal_keep_size_mb,
		0, 0, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	{
		{"min_wal_size", PGC_SIGHUP, WAL_CHECKPOINTS,
			gettext_noop("Sets the minimum size to shrink the WAL to."),
			NULL,
			GUC_UNIT_MB
		},
		&min_wal_size_mb,
		DEFAULT_MIN_WAL_SEGS * (DEFAULT_XLOG_SEG_SIZE / (1024 * 1024)),
		2, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	{
		{"max_wal_size", PGC_SIGHUP, WAL_CHECKPOINTS,
			gettext_noop("Sets the WAL size that triggers a checkpoint."),
			NULL,
			GUC_UNIT_MB
		},
		&max_wal_size_mb,
		DEFAULT_MAX_WAL_SEGS * (DEFAULT_XLOG_SEG_SIZE / (1024 * 1024)),
		2, MAX_KILOBYTES,
		NULL, assign_max_wal_size, NULL
	},

	{
		{"checkpoint_timeout", PGC_SIGHUP, WAL_CHECKPOINTS,
			gettext_noop("Sets the maximum time between automatic WAL checkpoints."),
			NULL,
			GUC_UNIT_S
		},
		&CheckPointTimeout,
		300, 30, 86400,
		NULL, NULL, NULL
	},

	{
		{"checkpoint_warning", PGC_SIGHUP, WAL_CHECKPOINTS,
			gettext_noop("Sets the maximum time before warning if checkpoints "
						 "triggered by WAL volume happen too frequently."),
			gettext_noop("Write a message to the server log if checkpoints "
						 "caused by the filling of WAL segment files happen more "
						 "frequently than this amount of time. "
						 "Zero turns off the warning."),
			GUC_UNIT_S
		},
		&CheckPointWarning,
		30, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"checkpoint_flush_after", PGC_SIGHUP, WAL_CHECKPOINTS,
			gettext_noop("Number of pages after which previously performed writes are flushed to disk."),
			NULL,
			GUC_UNIT_BLOCKS
		},
		&checkpoint_flush_after,
		DEFAULT_CHECKPOINT_FLUSH_AFTER, 0, WRITEBACK_MAX_PENDING_FLUSHES,
		NULL, NULL, NULL
	},

	{
		{"wal_buffers", PGC_POSTMASTER, WAL_SETTINGS,
			gettext_noop("Sets the number of disk-page buffers in shared memory for WAL."),
			NULL,
			GUC_UNIT_XBLOCKS
		},
		&XLOGbuffers,
		-1, -1, (INT_MAX / XLOG_BLCKSZ),
		check_wal_buffers, NULL, NULL
	},

	{
		{"wal_writer_delay", PGC_SIGHUP, WAL_SETTINGS,
			gettext_noop("Time between WAL flushes performed in the WAL writer."),
			NULL,
			GUC_UNIT_MS
		},
		&WalWriterDelay,
		200, 1, 10000,
		NULL, NULL, NULL
	},

	{
		{"wal_writer_flush_after", PGC_SIGHUP, WAL_SETTINGS,
			gettext_noop("Amount of WAL written out by WAL writer that triggers a flush."),
			NULL,
			GUC_UNIT_XBLOCKS
		},
		&WalWriterFlushAfter,
		(1024 * 1024) / XLOG_BLCKSZ, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"wal_skip_threshold", PGC_USERSET, WAL_SETTINGS,
			gettext_noop("Minimum size of new file to fsync instead of writing WAL."),
			NULL,
			GUC_UNIT_KB
		},
		&wal_skip_threshold,
		2048, 0, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	{
		{"max_wal_senders", PGC_POSTMASTER, REPLICATION_SENDING,
			gettext_noop("Sets the maximum number of simultaneously running WAL sender processes."),
			NULL
		},
		&max_wal_senders,
		10, 0, MAX_BACKENDS,
		check_max_wal_senders, NULL, NULL
	},

	{
		/* see max_wal_senders */
		{"max_replication_slots", PGC_POSTMASTER, REPLICATION_SENDING,
			gettext_noop("Sets the maximum number of simultaneously defined replication slots."),
			NULL
		},
		&max_replication_slots,
		10, 0, MAX_BACKENDS /* XXX? */ ,
		NULL, yb_assign_max_replication_slots, NULL
	},

	{
		{"max_slot_wal_keep_size", PGC_SIGHUP, REPLICATION_SENDING,
			gettext_noop("Sets the maximum WAL size that can be reserved by replication slots."),
			gettext_noop("Replication slots will be marked as failed, and segments released "
						 "for deletion or recycling, if this much space is occupied by WAL "
						 "on disk."),
			GUC_UNIT_MB
		},
		&max_slot_wal_keep_size_mb,
		-1, -1, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	{
		{"wal_sender_timeout", PGC_USERSET, REPLICATION_SENDING,
			gettext_noop("Sets the maximum time to wait for WAL replication."),
			NULL,
			GUC_UNIT_MS
		},
		&wal_sender_timeout,
		60 * 1000, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"commit_delay", PGC_SUSET, WAL_SETTINGS,
			gettext_noop("Sets the delay in microseconds between transaction commit and "
						 "flushing WAL to disk."),
			NULL
			/* we have no microseconds designation, so can't supply units here */
		},
		&CommitDelay,
		0, 0, 100000,
		NULL, NULL, NULL
	},

	{
		{"commit_siblings", PGC_USERSET, WAL_SETTINGS,
			gettext_noop("Sets the minimum number of concurrent open transactions "
						 "required before performing commit_delay."),
			NULL
		},
		&CommitSiblings,
		5, 0, 1000,
		NULL, NULL, NULL
	},

	{
		{"extra_float_digits", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the number of digits displayed for floating-point values."),
			gettext_noop("This affects real, double precision, and geometric data types. "
						 "A zero or negative parameter value is added to the standard "
						 "number of digits (FLT_DIG or DBL_DIG as appropriate). "
						 "Any value greater than zero selects precise output mode.")
		},
		&extra_float_digits,
		1, -15, 3,
		NULL, NULL, NULL
	},

	{
		{"log_min_duration_sample", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Sets the minimum execution time above which "
						 "a sample of statements will be logged."
						 " Sampling is determined by log_statement_sample_rate."),
			gettext_noop("Zero logs a sample of all queries. -1 turns this feature off."),
			GUC_UNIT_MS
		},
		&log_min_duration_sample,
		-1, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"log_min_duration_statement", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Sets the minimum execution time above which "
						 "all statements will be logged."),
			gettext_noop("Zero prints all queries. -1 turns this feature off."),
			GUC_UNIT_MS
		},
		&log_min_duration_statement,
		-1, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"log_autovacuum_min_duration", PGC_SIGHUP, LOGGING_WHAT,
			gettext_noop("Sets the minimum execution time above which "
						 "autovacuum actions will be logged."),
			gettext_noop("Zero prints all actions. -1 turns autovacuum logging off."),
			GUC_UNIT_MS
		},
		&Log_autovacuum_min_duration,
		600000, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"log_parameter_max_length", PGC_SUSET, LOGGING_WHAT,
			gettext_noop("Sets the maximum length in bytes of data logged for bind "
						 "parameter values when logging statements."),
			gettext_noop("-1 to print values in full."),
			GUC_UNIT_BYTE
		},
		&log_parameter_max_length,
		-1, -1, INT_MAX / 2,
		NULL, NULL, NULL
	},

	{
		{"log_parameter_max_length_on_error", PGC_USERSET, LOGGING_WHAT,
			gettext_noop("Sets the maximum length in bytes of data logged for bind "
						 "parameter values when logging statements, on error."),
			gettext_noop("-1 to print values in full."),
			GUC_UNIT_BYTE
		},
		&log_parameter_max_length_on_error,
		0, -1, INT_MAX / 2,
		NULL, NULL, NULL
	},

	{
		{"bgwriter_delay", PGC_SIGHUP, RESOURCES_BGWRITER,
			gettext_noop("Background writer sleep time between rounds."),
			NULL,
			GUC_UNIT_MS
		},
		&BgWriterDelay,
		200, 10, 10000,
		NULL, NULL, NULL
	},

	{
		{"bgwriter_lru_maxpages", PGC_SIGHUP, RESOURCES_BGWRITER,
			gettext_noop("Background writer maximum number of LRU pages to flush per round."),
			NULL
		},
		&bgwriter_lru_maxpages,
		100, 0, INT_MAX / 2,	/* Same upper limit as shared_buffers */
		NULL, NULL, NULL
	},

	{
		{"bgwriter_flush_after", PGC_SIGHUP, RESOURCES_BGWRITER,
			gettext_noop("Number of pages after which previously performed writes are flushed to disk."),
			NULL,
			GUC_UNIT_BLOCKS
		},
		&bgwriter_flush_after,
		DEFAULT_BGWRITER_FLUSH_AFTER, 0, WRITEBACK_MAX_PENDING_FLUSHES,
		NULL, NULL, NULL
	},

	{
		{"effective_io_concurrency",
			PGC_USERSET,
			RESOURCES_ASYNCHRONOUS,
			gettext_noop("Number of simultaneous requests that can be handled efficiently by the disk subsystem."),
			NULL,
			GUC_EXPLAIN
		},
		&effective_io_concurrency,
#ifdef USE_PREFETCH
		1,
#else
		0,
#endif
		0, MAX_IO_CONCURRENCY,
		check_effective_io_concurrency, NULL, NULL
	},

	{
		{"maintenance_io_concurrency",
			PGC_USERSET,
			RESOURCES_ASYNCHRONOUS,
			gettext_noop("A variant of effective_io_concurrency that is used for maintenance work."),
			NULL,
			GUC_EXPLAIN
		},
		&maintenance_io_concurrency,
#ifdef USE_PREFETCH
		10,
#else
		0,
#endif
		0, MAX_IO_CONCURRENCY,
		check_maintenance_io_concurrency, assign_maintenance_io_concurrency,
		NULL
	},

	{
		{"backend_flush_after", PGC_USERSET, RESOURCES_ASYNCHRONOUS,
			gettext_noop("Number of pages after which previously performed writes are flushed to disk."),
			NULL,
			GUC_UNIT_BLOCKS
		},
		&backend_flush_after,
		DEFAULT_BACKEND_FLUSH_AFTER, 0, WRITEBACK_MAX_PENDING_FLUSHES,
		NULL, NULL, NULL
	},

	{
		{"max_worker_processes",
			PGC_POSTMASTER,
			RESOURCES_ASYNCHRONOUS,
			gettext_noop("Maximum number of concurrent worker processes."),
			NULL,
		},
		&max_worker_processes,
		8, 0, MAX_BACKENDS,
		check_max_worker_processes, NULL, NULL
	},

	{
		{"max_logical_replication_workers",
			PGC_POSTMASTER,
			REPLICATION_SUBSCRIBERS,
			gettext_noop("Maximum number of logical replication worker processes."),
			NULL,
		},
		&max_logical_replication_workers,
		4, 0, MAX_BACKENDS,
		NULL, NULL, NULL
	},

	{
		{"max_sync_workers_per_subscription",
			PGC_SIGHUP,
			REPLICATION_SUBSCRIBERS,
			gettext_noop("Maximum number of table synchronization workers per subscription."),
			NULL,
		},
		&max_sync_workers_per_subscription,
		2, 0, MAX_BACKENDS,
		NULL, NULL, NULL
	},

	{
		{"log_rotation_age", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the amount of time to wait before forcing "
						 "log file rotation."),
			NULL,
			GUC_UNIT_MIN
		},
		&Log_RotationAge,
		HOURS_PER_DAY * MINS_PER_HOUR, 0, INT_MAX / SECS_PER_MINUTE,
		NULL, NULL, NULL
	},

	{
		{"log_rotation_size", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the maximum size a log file can reach before "
						 "being rotated."),
			NULL,
			GUC_UNIT_KB
		},
		&Log_RotationSize,
		10 * 1024, 0, INT_MAX / 1024,
		NULL, NULL, NULL
	},

	{
		{"max_function_args", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the maximum number of function arguments."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&max_function_args,
		FUNC_MAX_ARGS, FUNC_MAX_ARGS, FUNC_MAX_ARGS,
		NULL, NULL, NULL
	},

	{
		{"max_index_keys", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the maximum number of index keys."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&max_index_keys,
		INDEX_MAX_KEYS, INDEX_MAX_KEYS, INDEX_MAX_KEYS,
		NULL, NULL, NULL
	},

	{
		{"max_identifier_length", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the maximum identifier length."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&max_identifier_length,
		NAMEDATALEN - 1, NAMEDATALEN - 1, NAMEDATALEN - 1,
		NULL, NULL, NULL
	},

	{
		{"block_size", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the size of a disk block."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&block_size,
		BLCKSZ, BLCKSZ, BLCKSZ,
		NULL, NULL, NULL
	},

	{
		{"segment_size", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the number of pages per disk file."),
			NULL,
			GUC_UNIT_BLOCKS | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&segment_size,
		RELSEG_SIZE, RELSEG_SIZE, RELSEG_SIZE,
		NULL, NULL, NULL
	},

	{
		{"wal_block_size", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the block size in the write ahead log."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&wal_block_size,
		XLOG_BLCKSZ, XLOG_BLCKSZ, XLOG_BLCKSZ,
		NULL, NULL, NULL
	},

	{
		{"wal_retrieve_retry_interval", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the time to wait before retrying to retrieve WAL "
						 "after a failed attempt."),
			NULL,
			GUC_UNIT_MS
		},
		&wal_retrieve_retry_interval,
		5000, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"wal_segment_size", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the size of write ahead log segments."),
			NULL,
			GUC_UNIT_BYTE | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE | GUC_RUNTIME_COMPUTED
		},
		&wal_segment_size,
		DEFAULT_XLOG_SEG_SIZE,
		WalSegMinSize,
		WalSegMaxSize,
		NULL, NULL, NULL
	},

	{
		{"autovacuum_naptime", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Time to sleep between autovacuum runs."),
			NULL,
			GUC_UNIT_S
		},
		&autovacuum_naptime,
		60, 1, INT_MAX / 1000,
		NULL, NULL, NULL
	},
	{
		{"autovacuum_vacuum_threshold", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Minimum number of tuple updates or deletes prior to vacuum."),
			NULL
		},
		&autovacuum_vac_thresh,
		50, 0, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"autovacuum_vacuum_insert_threshold", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Minimum number of tuple inserts prior to vacuum, or -1 to disable insert vacuums."),
			NULL
		},
		&autovacuum_vac_ins_thresh,
		1000, -1, INT_MAX,
		NULL, NULL, NULL
	},
	{
		{"autovacuum_analyze_threshold", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Minimum number of tuple inserts, updates, or deletes prior to analyze."),
			NULL
		},
		&autovacuum_anl_thresh,
		50, 0, INT_MAX,
		NULL, NULL, NULL
	},
	{
		/* see varsup.c for why this is PGC_POSTMASTER not PGC_SIGHUP */
		{"autovacuum_freeze_max_age", PGC_POSTMASTER, AUTOVACUUM,
			gettext_noop("Age at which to autovacuum a table to prevent transaction ID wraparound."),
			NULL
		},
		&autovacuum_freeze_max_age,

		/* see vacuum_failsafe_age if you change the upper-limit value. */
		200000000, 100000, 2000000000,
		NULL, NULL, NULL
	},
	{
		/* see multixact.c for why this is PGC_POSTMASTER not PGC_SIGHUP */
		{"autovacuum_multixact_freeze_max_age", PGC_POSTMASTER, AUTOVACUUM,
			gettext_noop("Multixact age at which to autovacuum a table to prevent multixact wraparound."),
			NULL
		},
		&autovacuum_multixact_freeze_max_age,
		400000000, 10000, 2000000000,
		NULL, NULL, NULL
	},
	{
		/* see max_connections */
		{"autovacuum_max_workers", PGC_POSTMASTER, AUTOVACUUM,
			gettext_noop("Sets the maximum number of simultaneously running autovacuum worker processes."),
			NULL
		},
		&autovacuum_max_workers,
		3, 1, MAX_BACKENDS,
		check_autovacuum_max_workers, NULL, NULL
	},

	{
		{"max_parallel_maintenance_workers", PGC_USERSET, RESOURCES_ASYNCHRONOUS,
			gettext_noop("Sets the maximum number of parallel processes per maintenance operation."),
			NULL
		},
		&max_parallel_maintenance_workers,
		2, 0, 1024,
		NULL, NULL, NULL
	},

	{
		{"max_parallel_workers_per_gather", PGC_USERSET, RESOURCES_ASYNCHRONOUS,
			gettext_noop("Sets the maximum number of parallel processes per executor node."),
			NULL,
			GUC_EXPLAIN
		},
		&max_parallel_workers_per_gather,
		2, 0, MAX_PARALLEL_WORKER_LIMIT,
		NULL, NULL, NULL
	},

	{
		{"max_parallel_workers", PGC_USERSET, RESOURCES_ASYNCHRONOUS,
			gettext_noop("Sets the maximum number of parallel workers that can be active at one time."),
			NULL,
			GUC_EXPLAIN
		},
		&max_parallel_workers,
		8, 0, MAX_PARALLEL_WORKER_LIMIT,
		NULL, NULL, NULL
	},

	{
		{"autovacuum_work_mem", PGC_SIGHUP, RESOURCES_MEM,
			gettext_noop("Sets the maximum memory to be used by each autovacuum worker process."),
			NULL,
			GUC_UNIT_KB
		},
		&autovacuum_work_mem,
		-1, -1, MAX_KILOBYTES,
		check_autovacuum_work_mem, NULL, NULL
	},

	{
		{"old_snapshot_threshold", PGC_POSTMASTER, RESOURCES_ASYNCHRONOUS,
			gettext_noop("Time before a snapshot is too old to read pages changed after the snapshot was taken."),
			gettext_noop("A value of -1 disables this feature."),
			GUC_UNIT_MIN
		},
		&old_snapshot_threshold,
		-1, -1, MINS_PER_HOUR * HOURS_PER_DAY * 60,
		NULL, NULL, NULL
	},

	{
		{"tcp_keepalives_idle", PGC_USERSET, CONN_AUTH_SETTINGS,
			gettext_noop("Time between issuing TCP keepalives."),
			gettext_noop("A value of 0 uses the system default."),
			GUC_UNIT_S
		},
		&tcp_keepalives_idle,
		0, 0, INT_MAX,
		NULL, assign_tcp_keepalives_idle, show_tcp_keepalives_idle
	},

	{
		{"tcp_keepalives_interval", PGC_USERSET, CONN_AUTH_SETTINGS,
			gettext_noop("Time between TCP keepalive retransmits."),
			gettext_noop("A value of 0 uses the system default."),
			GUC_UNIT_S
		},
		&tcp_keepalives_interval,
		0, 0, INT_MAX,
		NULL, assign_tcp_keepalives_interval, show_tcp_keepalives_interval
	},

	{
		{"ssl_renegotiation_limit", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("SSL renegotiation is no longer supported; this can only be 0."),
			NULL,
			GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE,
		},
		&ssl_renegotiation_limit,
		0, 0, 0,
		NULL, NULL, NULL
	},

	{
		{"tcp_keepalives_count", PGC_USERSET, CONN_AUTH_SETTINGS,
			gettext_noop("Maximum number of TCP keepalive retransmits."),
			gettext_noop("Number of consecutive keepalive retransmits that can be "
						 "lost before a connection is considered dead. A value of 0 uses the "
						 "system default."),
		},
		&tcp_keepalives_count,
		0, 0, INT_MAX,
		NULL, assign_tcp_keepalives_count, show_tcp_keepalives_count
	},

	{
		{"gin_fuzzy_search_limit", PGC_USERSET, CLIENT_CONN_OTHER,
			gettext_noop("Sets the maximum allowed result for exact search by GIN."),
			NULL,
			0
		},
		&GinFuzzySearchLimit,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"effective_cache_size", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's assumption about the total size of the data caches."),
			gettext_noop("That is, the total size of the caches (kernel cache and shared buffers) used for PostgreSQL data files. "
						 "This is measured in disk pages, which are normally 8 kB each."),
			GUC_UNIT_BLOCKS | GUC_EXPLAIN,
		},
		&effective_cache_size,
		DEFAULT_EFFECTIVE_CACHE_SIZE, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"min_parallel_table_scan_size", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the minimum amount of table data for a parallel scan."),
			gettext_noop("If the planner estimates that it will read a number of table pages too small to reach this limit, a parallel scan will not be considered."),
			GUC_UNIT_BLOCKS | GUC_EXPLAIN,
		},
		&min_parallel_table_scan_size,
		(8 * 1024 * 1024) / BLCKSZ, 0, INT_MAX / 3,
		NULL, NULL, NULL
	},

	{
		{"min_parallel_index_scan_size", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the minimum amount of index data for a parallel scan."),
			gettext_noop("If the planner estimates that it will read a number of index pages too small to reach this limit, a parallel scan will not be considered."),
			GUC_UNIT_BLOCKS | GUC_EXPLAIN,
		},
		&min_parallel_index_scan_size,
		(512 * 1024) / BLCKSZ, 0, INT_MAX / 3,
		NULL, NULL, NULL
	},

	{
		/* Can't be set in postgresql.conf */
		{"server_version_num", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the server version as an integer."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&server_version_num,
		PG_VERSION_NUM, PG_VERSION_NUM, PG_VERSION_NUM,
		NULL, NULL, NULL
	},

	{
		{"log_temp_files", PGC_SUSET, LOGGING_WHAT,
			gettext_noop("Log the use of temporary files larger than this number of kilobytes."),
			gettext_noop("Zero logs all files. The default is -1 (turning this feature off)."),
			GUC_UNIT_KB
		},
		&log_temp_files,
		-1, -1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"track_activity_query_size", PGC_POSTMASTER, STATS_CUMULATIVE,
			gettext_noop("Sets the size reserved for pg_stat_activity.query, in bytes."),
			NULL,
			GUC_UNIT_BYTE
		},
		&pgstat_track_activity_query_size,
		1024, 100, 1048576,
		NULL, NULL, NULL
	},

	{
		{"gin_pending_list_limit", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the maximum size of the pending list for GIN index."),
			NULL,
			GUC_UNIT_KB
		},
		&gin_pending_list_limit,
		4096, 64, MAX_KILOBYTES,
		NULL, NULL, NULL
	},

	{
		{"tcp_user_timeout", PGC_USERSET, CONN_AUTH_SETTINGS,
			gettext_noop("TCP user timeout."),
			gettext_noop("A value of 0 uses the system default."),
			GUC_UNIT_MS
		},
		&tcp_user_timeout,
		0, 0, INT_MAX,
		NULL, assign_tcp_user_timeout, show_tcp_user_timeout
	},

	{
		{"huge_page_size", PGC_POSTMASTER, RESOURCES_MEM,
			gettext_noop("The size of huge page that should be requested."),
			NULL,
			GUC_UNIT_KB
		},
		&huge_page_size,
		0, 0, INT_MAX,
		check_huge_page_size, NULL, NULL
	},

	{
		{"debug_discard_caches", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Aggressively flush system caches for debugging purposes."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&debug_discard_caches,
#ifdef DISCARD_CACHES_ENABLED
		/* Set default based on older compile-time-only cache clobber macros */
#if defined(CLOBBER_CACHE_RECURSIVELY)
		3,
#elif defined(CLOBBER_CACHE_ALWAYS)
		1,
#else
		0,
#endif
		0, 5,
#else							/* not DISCARD_CACHES_ENABLED */
		0, 0, 0,
#endif							/* not DISCARD_CACHES_ENABLED */
		NULL, NULL, NULL
	},

	{
		{"client_connection_check_interval", PGC_USERSET, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the time interval between checks for disconnection while running queries."),
			NULL,
			GUC_UNIT_MS
		},
		&client_connection_check_interval,
		0, 0, INT_MAX,
		check_client_connection_check_interval, NULL, NULL
	},

	{
		{"log_startup_progress_interval", PGC_SIGHUP, LOGGING_WHEN,
			gettext_noop("Time between progress updates for "
						 "long-running startup operations."),
			gettext_noop("0 turns this feature off."),
			GUC_UNIT_MS,
		},
		&log_startup_progress_interval,
		10000, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_default_copy_from_rows_per_transaction", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the batch number of rows to copy from the source to table."),
			NULL,
			0
		},
		&yb_default_copy_from_rows_per_transaction,
		DEFAULT_BATCH_ROWS_PER_TRANSACTION, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"ysql_session_max_batch_size", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the maximum batch size for writes that YSQL can buffer before flushing to tablet servers."),
			gettext_noop("If this is 0, YSQL will use the gflag ysql_session_max_batch_size. If non-zero, this session variable will supersede the value of the gflag."),
			0
		},
		&ysql_session_max_batch_size,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		/* TODO(jason): once it becomes stable, this can be PGC_USERSET. */
		{"yb_insert_on_conflict_read_batch_size", PGC_SUSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Maximum batch size for arbiter index reads during INSERT ON CONFLICT."),
			gettext_noop("A value of 1 disables this feature."),
			0
		},
		&yb_insert_on_conflict_read_batch_size,
		1, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"ysql_max_in_flight_ops", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Maximum number of in-flight operations allowed from YSQL to tablet servers"),
			NULL,
		},
		&ysql_max_in_flight_ops,
		10000, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_follower_read_staleness_ms", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the staleness (in ms) to be used for performing follower reads."),
			NULL,
			0
		},
		&yb_follower_read_staleness_ms,
		30000, 0, INT_MAX,
		check_follower_read_staleness_ms, assign_follower_read_staleness_ms, NULL
	},

	{
		{"yb_index_state_flags_update_delay", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Delay in milliseconds between stages of online index"
						 " build."),
			gettext_noop("Set high to give online transactions more time to"
						 " complete."),
			GUC_UNIT_MS
		},
		&yb_index_state_flags_update_delay,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_wait_for_backends_catalog_version_timeout", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Timeout in milliseconds to wait for backends to reach"
						 " desired catalog versions."),
			gettext_noop("The actual time spent may be longer than that by as"
						 " much as master flag"
						 " wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms."
						 " Setting to zero or less results in no timeout."
						 " Currently used by concurrent CREATE INDEX."),
			GUC_UNIT_MS
		},
		&yb_wait_for_backends_catalog_version_timeout,
		5 * 60 * 1000, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_test_planner_custom_plan_threshold", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("The number of times to force custom plan generation "
						 "for prepared statements before considering a "
						 "generic plan."),
			NULL
		},
		&yb_test_planner_custom_plan_threshold,
		5, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_fetch_row_limit", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Maximum number of rows to fetch per scan. 0 = No limit"),
			NULL
		},
		&yb_fetch_row_limit,
		1024, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_fetch_size_limit", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Maximum size of a fetch response. 0 = No limit"),
			NULL, GUC_UNIT_BYTE
		},
		&yb_fetch_size_limit,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_update_num_cols_to_compare", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Maximum number of columns whose data is to be"
						 " compared while seeking to optimize updates."
						 " If set to 0, all applicable columns in the table"
						 " will be compared."),
			NULL
		},
		&yb_update_optimization_options.num_cols_to_compare,
		50, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_update_max_cols_size_to_compare", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Maximum size in bytes of columns whose data is to be"
						 " compared while seeking to optimize updates."
						 " If set to 0, no size limit is applied."),
			NULL, GUC_UNIT_BYTE
		},
		&yb_update_optimization_options.max_cols_size_to_compare,
		10 * 1024, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_parallel_range_rows", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("The number of rows to plan per parallel worker"),
			NULL
		},
		&yb_parallel_range_rows,
		0, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_max_query_layer_retries", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Max number of internal query layer retries of a statement"),
			gettext_noop("Max number of query layer retries of a statement for the following errors: "
						 "serialization error (40001), \"Restart read required\" (40001), "
						 "deadlock detected (40P01). In Repeatable Read and Serializable isolation levels, the "
						 "query layer only retries errors faced in the first statement of a transation. In "
						 "READ COMMITTED isolation, the query layer has the ability to do retries for any "
						 "statement in a transaction. Retries are not possible if some response data has "
						 "already been sent to the client while the query is still executing. This happens if "
						 "the output buffer, the size of which is configurable using the TServer gflag "
						 "ysql_output_buffer_size, has filled atleast once and is flushed."),
		},
		&yb_max_query_layer_retries,
		60, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_ash_circular_buffer_size", PGC_POSTMASTER, STATS_MONITORING,
			gettext_noop("Size (in KiBs) of ASH circular buffer that stores the samples"),
			gettext_noop("If this is 0, the size will be calculated based on the number of cores"),
			GUC_UNIT_KB
		},
		&yb_ash_circular_buffer_size,
		0, 0, INT_MAX,
		yb_ash_circular_buffer_size_check_hook, NULL, NULL
	},

	{
		{"yb_ash_sampling_interval_ms", PGC_SIGHUP, STATS_MONITORING,
			gettext_noop("Time (in milliseconds) between two consecutive sampling events"),
			NULL,
			GUC_UNIT_MS
		},
		&yb_ash_sampling_interval_ms,
		1000, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_ash_sample_size", PGC_SIGHUP, STATS_MONITORING,
			gettext_noop("Number of samples captured from each component per sampling event"),
			NULL
		},
		&yb_ash_sample_size,
		500, 0, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_toast_catcache_threshold", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Size threshold in bytes for a catcache tuple to be compressed."),
			NULL
		},
		&yb_toast_catcache_threshold,
		-1, -1, INT_MAX,
		yb_check_toast_catcache_threshold, NULL, NULL
	},

	{
		{"yb_parallel_range_size", PGC_USERSET, QUERY_TUNING_METHOD,
			gettext_noop("Approximate size of parallel range for DocDB relation scans"),
			NULL,
			GUC_UNIT_BYTE
		},
		&yb_parallel_range_size,
		1024 * 1024, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_query_diagnostics_bg_worker_interval_ms", PGC_POSTMASTER, STATS_MONITORING,
			gettext_noop("Time (in milliseconds) for which the query diagnostic's background worker sleeps"),
			NULL,
			GUC_UNIT_MS
		},
		&yb_query_diagnostics_bg_worker_interval_ms,
		1000, 1, INT_MAX,
		NULL, NULL, NULL
	},

	{
		{"yb_query_diagnostics_circular_buffer_size", PGC_POSTMASTER, STATS_MONITORING,
			gettext_noop("Size of query diagnostics circular buffer that stores statuses of bundles"),
			gettext_noop("The circular buffer is filled sequentially until "
									"it reaches this size, then it wraps around and "
									"starts overwriting the oldest entries."),
			GUC_UNIT_KB
		},
		&yb_query_diagnostics_circular_buffer_size,
		64, 1, INT_MAX,
		NULL, NULL, NULL
	},

	/* End-of-list marker */
	{
		{NULL, 0, 0, NULL, NULL}, NULL, 0, 0, 0, NULL, NULL, NULL
	}
};

static struct config_oid ConfigureNamesOid[] =
{
	/* End-of-list marker */
	{
		{NULL, 0, 0, NULL, NULL}, NULL, 0, 0, 0, NULL, NULL, NULL
	}
};

static struct config_real ConfigureNamesReal[] =
{
	{
		{"seq_page_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the cost of a "
						 "sequentially fetched disk page."),
			NULL,
			GUC_EXPLAIN
		},
		&seq_page_cost,
		DEFAULT_SEQ_PAGE_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"random_page_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the cost of a "
						 "nonsequentially fetched disk page."),
			NULL,
			GUC_EXPLAIN
		},
		&random_page_cost,
		DEFAULT_RANDOM_PAGE_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"cpu_tuple_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the cost of "
						 "processing each tuple (row)."),
			NULL,
			GUC_EXPLAIN
		},
		&cpu_tuple_cost,
		DEFAULT_CPU_TUPLE_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"cpu_index_tuple_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the cost of "
						 "processing each index entry during an index scan."),
			NULL,
			GUC_EXPLAIN
		},
		&cpu_index_tuple_cost,
		DEFAULT_CPU_INDEX_TUPLE_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"cpu_operator_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the cost of "
						 "processing each operator or function call."),
			NULL,
			GUC_EXPLAIN
		},
		&cpu_operator_cost,
		DEFAULT_CPU_OPERATOR_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"parallel_tuple_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the cost of "
						 "passing each tuple (row) from worker to leader backend."),
			NULL,
			GUC_EXPLAIN
		},
		&parallel_tuple_cost,
		DEFAULT_PARALLEL_TUPLE_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"parallel_setup_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the cost of "
						 "starting up worker processes for parallel query."),
			NULL,
			GUC_EXPLAIN
		},
		&parallel_setup_cost,
		DEFAULT_PARALLEL_SETUP_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"yb_network_fetch_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Sets the planner's estimate of the fixed cost of "
							 "fetching a batch of rows from a YB relation"),
			NULL
		},
		&yb_network_fetch_cost,
		YB_DEFAULT_FETCH_COST, 0, DBL_MAX,
		NULL, NULL, NULL
	},
	{
		{"jit_above_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Perform JIT compilation if query is more expensive."),
			gettext_noop("-1 disables JIT compilation."),
			GUC_EXPLAIN
		},
		&jit_above_cost,
		100000, -1, DBL_MAX,
		NULL, NULL, NULL
	},

	{
		{"jit_optimize_above_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Optimize JIT-compiled functions if query is more expensive."),
			gettext_noop("-1 disables optimization."),
			GUC_EXPLAIN
		},
		&jit_optimize_above_cost,
		500000, -1, DBL_MAX,
		NULL, NULL, NULL
	},

	{
		{"jit_inline_above_cost", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("Perform JIT inlining if query is more expensive."),
			gettext_noop("-1 disables inlining."),
			GUC_EXPLAIN
		},
		&jit_inline_above_cost,
		500000, -1, DBL_MAX,
		NULL, NULL, NULL
	},

	{
		{"cursor_tuple_fraction", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Sets the planner's estimate of the fraction of "
						 "a cursor's rows that will be retrieved."),
			NULL,
			GUC_EXPLAIN
		},
		&cursor_tuple_fraction,
		DEFAULT_CURSOR_TUPLE_FRACTION, 0.0, 1.0,
		NULL, NULL, NULL
	},

	{
		{"recursive_worktable_factor", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Sets the planner's estimate of the average size "
						 "of a recursive query's working table."),
			NULL,
			GUC_EXPLAIN
		},
		&recursive_worktable_factor,
		DEFAULT_RECURSIVE_WORKTABLE_FACTOR, 0.001, 1000000.0,
		NULL, NULL, NULL
	},

	{
		{"geqo_selection_bias", PGC_USERSET, QUERY_TUNING_GEQO,
			gettext_noop("GEQO: selective pressure within the population."),
			NULL,
			GUC_EXPLAIN
		},
		&Geqo_selection_bias,
		DEFAULT_GEQO_SELECTION_BIAS,
		MIN_GEQO_SELECTION_BIAS, MAX_GEQO_SELECTION_BIAS,
		NULL, NULL, NULL
	},
	{
		{"geqo_seed", PGC_USERSET, QUERY_TUNING_GEQO,
			gettext_noop("GEQO: seed for random path selection."),
			NULL,
			GUC_EXPLAIN
		},
		&Geqo_seed,
		0.0, 0.0, 1.0,
		NULL, NULL, NULL
	},

	{
		{"hash_mem_multiplier", PGC_USERSET, RESOURCES_MEM,
			gettext_noop("Multiple of work_mem to use for hash tables."),
			NULL,
			GUC_EXPLAIN
		},
		&hash_mem_multiplier,
		2.0, 1.0, 1000.0,
		NULL, NULL, NULL
	},

	{
		{"bgwriter_lru_multiplier", PGC_SIGHUP, RESOURCES_BGWRITER,
			gettext_noop("Multiple of the average buffer usage to free per round."),
			NULL
		},
		&bgwriter_lru_multiplier,
		2.0, 0.0, 10.0,
		NULL, NULL, NULL
	},

	{
		{"seed", PGC_USERSET, UNGROUPED,
			gettext_noop("Sets the seed for random-number generation."),
			NULL,
			GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&phony_random_seed,
		0.0, -1.0, 1.0,
		check_random_seed, assign_random_seed, show_random_seed
	},

	{
		{"vacuum_cost_delay", PGC_USERSET, RESOURCES_VACUUM_DELAY,
			gettext_noop("Vacuum cost delay in milliseconds."),
			NULL,
			GUC_UNIT_MS
		},
		&VacuumCostDelay,
		0, 0, 100,
		NULL, NULL, NULL
	},

	{
		{"autovacuum_vacuum_cost_delay", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Vacuum cost delay in milliseconds, for autovacuum."),
			NULL,
			GUC_UNIT_MS
		},
		&autovacuum_vac_cost_delay,
		2, -1, 100,
		NULL, NULL, NULL
	},

	{
		{"autovacuum_vacuum_scale_factor", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Number of tuple updates or deletes prior to vacuum as a fraction of reltuples."),
			NULL
		},
		&autovacuum_vac_scale,
		0.2, 0.0, 100.0,
		NULL, NULL, NULL
	},

	{
		{"autovacuum_vacuum_insert_scale_factor", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Number of tuple inserts prior to vacuum as a fraction of reltuples."),
			NULL
		},
		&autovacuum_vac_ins_scale,
		0.2, 0.0, 100.0,
		NULL, NULL, NULL
	},

	{
		{"autovacuum_analyze_scale_factor", PGC_SIGHUP, AUTOVACUUM,
			gettext_noop("Number of tuple inserts, updates, or deletes prior to analyze as a fraction of reltuples."),
			NULL
		},
		&autovacuum_anl_scale,
		0.1, 0.0, 100.0,
		NULL, NULL, NULL
	},

	{
		{"checkpoint_completion_target", PGC_SIGHUP, WAL_CHECKPOINTS,
			gettext_noop("Time spent flushing dirty buffers during checkpoint, as fraction of checkpoint interval."),
			NULL
		},
		&CheckPointCompletionTarget,
		0.9, 0.0, 1.0,
		NULL, assign_checkpoint_completion_target, NULL
	},

	{
		{"log_statement_sample_rate", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Fraction of statements exceeding log_min_duration_sample to be logged."),
			gettext_noop("Use a value between 0.0 (never log) and 1.0 (always log).")
		},
		&log_statement_sample_rate,
		1.0, 0.0, 1.0,
		NULL, NULL, NULL
	},

	{
		{"log_transaction_sample_rate", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Sets the fraction of transactions from which to log all statements."),
			gettext_noop("Use a value between 0.0 (never log) and 1.0 (log all "
						 "statements for all transactions).")
		},
		&log_xact_sample_rate,
		0.0, 0.0, 1.0,
		NULL, NULL, NULL
	},
	{
		{"yb_transaction_priority_lower_bound", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets lower bound for priority used by transactions of this session"),
			NULL
		},
		&yb_transaction_priority_lower_bound,
		0.0, 0.0, 1.0,
		check_transaction_priority_lower_bound, YBCAssignTransactionPriorityLowerBound, NULL
	},
	{
		{"yb_transaction_priority_upper_bound", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets upper bound for priority used by transactions of this session"),
			NULL
		},
		&yb_transaction_priority_upper_bound,
		1.0, 0.0, 1.0,
		check_transaction_priority_upper_bound, YBCAssignTransactionPriorityUpperBound, NULL
	},
	{
		{"yb_transaction_priority", PGC_INTERNAL, CLIENT_CONN_STATEMENT,
			gettext_noop(
					"[DEPRECATED - instead use the yb_get_current_transaction_priority() function]. Gets the "
					"transaction priority used by the current active distributed transaction in the session. "
					"If no distributed transaction is active, return 0"),
			NULL
		},
		&yb_transaction_priority,
		0.0, 0.0, 1.0,
		NULL, NULL, yb_fetch_current_transaction_priority
	},

	{
		{"retry_backoff_multiplier", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the multiplier used to calculate the retry backoff."),
			NULL,
			GUC_UNIT_MS
		},
		&RetryBackoffMultiplier,
		1.2, 1.0, 1e10,
		check_backoff_multiplier, NULL, NULL
	},

	{
		{"log_statement_sample_rate", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Fraction of statements exceeding log_min_duration_sample to be logged."),
			gettext_noop("Use a value between 0.0 (never log) and 1.0 (always log).")
		},
		&log_statement_sample_rate,
		1.0, 0.0, 1.0,
		NULL, NULL, NULL
	},

	{
		{"log_transaction_sample_rate", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Set the fraction of transactions to log for new transactions."),
			gettext_noop("Logs all statements from a fraction of transactions. "
						 "Use a value between 0.0 (never log) and 1.0 (log all "
						 "statements for all transactions).")
		},
		&log_xact_sample_rate,
		0.0, 0.0, 1.0,
		NULL, NULL, NULL
	},

	{
		{"yb_test_ybgin_disable_cost_factor", PGC_USERSET, QUERY_TUNING_COST,
			gettext_noop("The multiplier to disable_cost to add when costing"
						 " ybgin index scans that may not be supported."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_ybgin_disable_cost_factor,
		2.0, 0.0, 10.0,
		NULL, NULL, NULL
	},

	/* End-of-list marker */
	{
		{NULL, 0, 0, NULL, NULL}, NULL, 0.0, 0.0, 0.0, NULL, NULL, NULL
	}
};


static struct config_string ConfigureNamesString[] =
{
	{
		{"archive_command", PGC_SIGHUP, WAL_ARCHIVING,
			gettext_noop("Sets the shell command that will be called to archive a WAL file."),
			gettext_noop("This is used only if \"archive_library\" is not set.")
		},
		&XLogArchiveCommand,
		"",
		NULL, NULL, show_archive_command
	},

	{
		{"archive_library", PGC_SIGHUP, WAL_ARCHIVING,
			gettext_noop("Sets the library that will be called to archive a WAL file."),
			gettext_noop("An empty string indicates that \"archive_command\" should be used.")
		},
		&XLogArchiveLibrary,
		"",
		NULL, NULL, NULL
	},

	{
		{"restore_command", PGC_SIGHUP, WAL_ARCHIVE_RECOVERY,
			gettext_noop("Sets the shell command that will be called to retrieve an archived WAL file."),
			NULL
		},
		&recoveryRestoreCommand,
		"",
		NULL, NULL, NULL
	},

	{
		{"archive_cleanup_command", PGC_SIGHUP, WAL_ARCHIVE_RECOVERY,
			gettext_noop("Sets the shell command that will be executed at every restart point."),
			NULL
		},
		&archiveCleanupCommand,
		"",
		NULL, NULL, NULL
	},

	{
		{"recovery_end_command", PGC_SIGHUP, WAL_ARCHIVE_RECOVERY,
			gettext_noop("Sets the shell command that will be executed once at the end of recovery."),
			NULL
		},
		&recoveryEndCommand,
		"",
		NULL, NULL, NULL
	},

	{
		{"recovery_target_timeline", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Specifies the timeline to recover into."),
			NULL
		},
		&recovery_target_timeline_string,
		"latest",
		check_recovery_target_timeline, assign_recovery_target_timeline, NULL
	},

	{
		{"recovery_target", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Set to \"immediate\" to end recovery as soon as a consistent state is reached."),
			NULL
		},
		&recovery_target_string,
		"",
		check_recovery_target, assign_recovery_target, NULL
	},
	{
		{"recovery_target_xid", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Sets the transaction ID up to which recovery will proceed."),
			NULL
		},
		&recovery_target_xid_string,
		"",
		check_recovery_target_xid, assign_recovery_target_xid, NULL
	},
	{
		{"recovery_target_time", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Sets the time stamp up to which recovery will proceed."),
			NULL
		},
		&recovery_target_time_string,
		"",
		check_recovery_target_time, assign_recovery_target_time, NULL
	},
	{
		{"recovery_target_name", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Sets the named restore point up to which recovery will proceed."),
			NULL
		},
		&recovery_target_name_string,
		"",
		check_recovery_target_name, assign_recovery_target_name, NULL
	},
	{
		{"recovery_target_lsn", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Sets the LSN of the write-ahead log location up to which recovery will proceed."),
			NULL
		},
		&recovery_target_lsn_string,
		"",
		check_recovery_target_lsn, assign_recovery_target_lsn, NULL
	},

	{
		{"promote_trigger_file", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Specifies a file name whose presence ends recovery in the standby."),
			NULL
		},
		&PromoteTriggerFile,
		"",
		NULL, NULL, NULL
	},

	{
		{"primary_conninfo", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the connection string to be used to connect to the sending server."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&PrimaryConnInfo,
		"",
		NULL, NULL, NULL
	},

	{
		{"primary_slot_name", PGC_SIGHUP, REPLICATION_STANDBY,
			gettext_noop("Sets the name of the replication slot to use on the sending server."),
			NULL
		},
		&PrimarySlotName,
		"",
		check_primary_slot_name, NULL, NULL
	},

	{
		{"client_encoding", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the client's character set encoding."),
			NULL,
			GUC_IS_NAME | GUC_REPORT
		},
		&client_encoding_string,
		"SQL_ASCII",
		check_client_encoding, assign_client_encoding, NULL
	},

	{
		{"log_line_prefix", PGC_SIGHUP, LOGGING_WHAT,
			gettext_noop("Controls information prefixed to each log line."),
			gettext_noop("If blank, no prefix is used.")
		},
		&Log_line_prefix,
		"%m [%p] ",
		NULL, NULL, NULL
	},

	{
		{"log_timezone", PGC_SIGHUP, LOGGING_WHAT,
			gettext_noop("Sets the time zone to use in log messages."),
			NULL
		},
		&log_timezone_string,
		"GMT",
		check_log_timezone, assign_log_timezone, show_log_timezone
	},

	{
		{"DateStyle", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the display format for date and time values."),
			gettext_noop("Also controls interpretation of ambiguous "
						 "date inputs."),
			GUC_LIST_INPUT | GUC_REPORT
		},
		&datestyle_string,
		"ISO, MDY",
		check_datestyle, assign_datestyle, NULL
	},

	{
		{"default_table_access_method", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the default table access method for new tables."),
			NULL,
			GUC_IS_NAME
		},
		&default_table_access_method,
		DEFAULT_TABLE_ACCESS_METHOD,
		check_default_table_access_method, NULL, NULL
	},

	{
		{"default_tablespace", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the default tablespace to create tables and indexes in."),
			gettext_noop("An empty string selects the database's default tablespace."),
			GUC_IS_NAME
		},
		&default_tablespace,
		"",
		check_default_tablespace, NULL, NULL
	},

	{
		{"temp_tablespaces", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the tablespace(s) to use for temporary tables and sort files."),
			NULL,
			GUC_LIST_INPUT | GUC_LIST_QUOTE
		},
		&temp_tablespaces,
		"",
		check_temp_tablespaces, assign_temp_tablespaces, NULL
	},

	{
		{"dynamic_library_path", PGC_SUSET, CLIENT_CONN_OTHER,
			gettext_noop("Sets the path for dynamically loadable modules."),
			gettext_noop("If a dynamically loadable module needs to be opened and "
						 "the specified name does not have a directory component (i.e., the "
						 "name does not contain a slash), the system will search this path for "
						 "the specified file."),
			GUC_SUPERUSER_ONLY
		},
		&Dynamic_library_path,
		"$libdir",
		NULL, NULL, NULL
	},

	{
		{"krb_server_keyfile", PGC_SIGHUP, CONN_AUTH_AUTH,
			gettext_noop("Sets the location of the Kerberos server key file."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&pg_krb_server_keyfile,
		PG_KRB_SRVTAB,
		NULL, NULL, NULL
	},

	{
		{"bonjour_name", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the Bonjour service name."),
			NULL
		},
		&bonjour_name,
		"",
		NULL, NULL, NULL
	},

	/* See main.c about why defaults for LC_foo are not all alike */

	{
		{"lc_collate", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the collation order locale."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&locale_collate,
		"C",
		NULL, NULL, NULL
	},

	{
		{"lc_ctype", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the character classification and case conversion locale."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&locale_ctype,
		"C",
		NULL, NULL, NULL
	},

	{
		{"lc_messages", PGC_SUSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the language in which messages are displayed."),
			NULL
		},
		&locale_messages,
		"",
		check_locale_messages, assign_locale_messages, NULL
	},

	{
		{"lc_monetary", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the locale for formatting monetary amounts."),
			NULL
		},
		&locale_monetary,
		"C",
		check_locale_monetary, assign_locale_monetary, NULL
	},

	{
		{"lc_numeric", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the locale for formatting numbers."),
			NULL
		},
		&locale_numeric,
		"C",
		check_locale_numeric, assign_locale_numeric, NULL
	},

	{
		{"lc_time", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the locale for formatting date and time values."),
			NULL
		},
		&locale_time,
		"C",
		check_locale_time, assign_locale_time, NULL
	},

	{
		{"session_preload_libraries", PGC_SUSET, CLIENT_CONN_PRELOAD,
			gettext_noop("Lists shared libraries to preload into each backend."),
			NULL,
			GUC_LIST_INPUT | GUC_LIST_QUOTE | GUC_SUPERUSER_ONLY
		},
		&session_preload_libraries_string,
		"",
		NULL, NULL, NULL
	},

	{
		{"shared_preload_libraries", PGC_POSTMASTER, CLIENT_CONN_PRELOAD,
			gettext_noop("Lists shared libraries to preload into server."),
			NULL,
			GUC_LIST_INPUT | GUC_LIST_QUOTE | GUC_SUPERUSER_ONLY
		},
		&shared_preload_libraries_string,
		"",
		NULL, NULL, NULL
	},

	{
		{"local_preload_libraries", PGC_USERSET, CLIENT_CONN_PRELOAD,
			gettext_noop("Lists unprivileged shared libraries to preload into each backend."),
			NULL,
			GUC_LIST_INPUT | GUC_LIST_QUOTE
		},
		&local_preload_libraries_string,
		"",
		NULL, NULL, NULL
	},

	{
		{"search_path", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the schema search order for names that are not schema-qualified."),
			NULL,
			GUC_LIST_INPUT | GUC_LIST_QUOTE | GUC_EXPLAIN
		},
		&namespace_search_path,
		"\"$user\", public",
		check_search_path, assign_search_path, NULL
	},

	{
		/* Can't be set in postgresql.conf */
		{"server_encoding", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the server (database) character set encoding."),
			NULL,
			GUC_IS_NAME | GUC_REPORT | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&server_encoding_string,
		"SQL_ASCII",
		NULL, NULL, NULL
	},

	{
		/* Can't be set in postgresql.conf */
		{"server_version", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the server version."),
			NULL,
			GUC_REPORT | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&server_version_string,
		PG_VERSION,
		NULL, NULL, NULL
	},

	{
		/* Not for general use --- used by SET ROLE */
		{"role", PGC_USERSET, UNGROUPED,
			gettext_noop("Sets the current role."),
			NULL,
			GUC_IS_NAME | GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE | GUC_NOT_WHILE_SEC_REST
		},
		&role_string,
		"none",
		check_role, assign_role, show_role
	},

	{
		/* Not for general use --- used by SET SESSION AUTHORIZATION */
		{"session_authorization", PGC_USERSET, UNGROUPED,
			gettext_noop("Sets the session user name."),
			NULL,
			GUC_IS_NAME | GUC_REPORT | GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE | GUC_NOT_WHILE_SEC_REST
		},
		&session_authorization_string,
		NULL,
		check_session_authorization, assign_session_authorization, NULL
	},

	{
		{"log_destination", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the destination for server log output."),
			gettext_noop("Valid values are combinations of \"stderr\", "
						 "\"syslog\", \"csvlog\", \"jsonlog\", and \"eventlog\", "
						 "depending on the platform."),
			GUC_LIST_INPUT
		},
		&Log_destination_string,
		"stderr",
		check_log_destination, assign_log_destination, NULL
	},
	{
		{"log_directory", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the destination directory for log files."),
			gettext_noop("Can be specified as relative to the data directory "
						 "or as absolute path."),
			GUC_SUPERUSER_ONLY
		},
		&Log_directory,
		"log",
		check_canonical_path, NULL, NULL
	},
	{
		{"log_filename", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the file name pattern for log files."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&Log_filename,
		"postgresql-%Y-%m-%d_%H%M%S.log",
		NULL, NULL, NULL
	},

	{
		{"syslog_ident", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the program name used to identify PostgreSQL "
						 "messages in syslog."),
			NULL
		},
		&syslog_ident_str,
		"postgres",
		NULL, assign_syslog_ident, NULL
	},

	{
		{"event_source", PGC_POSTMASTER, LOGGING_WHERE,
			gettext_noop("Sets the application name used to identify "
						 "PostgreSQL messages in the event log."),
			NULL
		},
		&event_source,
		DEFAULT_EVENT_SOURCE,
		NULL, NULL, NULL
	},

	{
		{"TimeZone", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the time zone for displaying and interpreting time stamps."),
			NULL,
			GUC_REPORT
		},
		&timezone_string,
		"GMT",
		check_timezone, assign_timezone, show_timezone
	},
	{
		{"timezone_abbreviations", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Selects a file of time zone abbreviations."),
			NULL
		},
		&timezone_abbreviations_string,
		NULL,
		check_timezone_abbreviations, assign_timezone_abbreviations, NULL
	},

	{
		{"yb_effective_transaction_isolation_level", PGC_INTERNAL, CLIENT_CONN_STATEMENT,
			gettext_noop(
					"[DEPRECATED - instead use the yb_get_effective_transaction_isolation_level() function]. "
					"Shows the effective YugabyteDB transaction isolation level used by the current active "
					"transaction in the session."),
			NULL,
			GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&yb_effective_transaction_isolation_level_string,
		"default",
		NULL, NULL, yb_fetch_effective_transaction_isolation_level
	},

	{
		{"yb_xcluster_consistency_level", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Controls the consistency level of xCluster replicated databases."),
			gettext_noop("Valid values are \"database\" and \"tablet\".")
		},
		&yb_xcluster_consistency_level_string,
		"database",
		check_yb_xcluster_consistency_level, assign_yb_xcluster_consistency_level, NULL
	},

	{
		{"yb_read_time", PGC_SUSET, CLIENT_CONN_STATEMENT,
			gettext_noop(
				"Allows querying the database as of a point in time in the past."
				" Takes a unix timestamp in microseconds."
				" Zero means reading data as of current time."),
			gettext_noop(
				"User should set this variable with caution. Currently, it can"
				" only read old data without schema changes. In other words, it should not be"
				" set to a timestamp before a DDL operation has been performed."
				" Potential corruption can happen in case (1) the variable is set to a timestamp"
				" before most recent DDL. (2) DDL is performed while it is set to nonzero.")
		},
		&yb_read_time_string,
		"0",
		check_yb_read_time, assign_yb_read_time, NULL
	},

	{
		{"unix_socket_group", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the owning group of the Unix-domain socket."),
			gettext_noop("The owning user of the socket is always the user "
						 "that starts the server.")
		},
		&Unix_socket_group,
		"",
		NULL, NULL, NULL
	},

	{
		{"unix_socket_directories", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the directories where Unix-domain sockets will be created."),
			NULL,
			GUC_LIST_INPUT | GUC_LIST_QUOTE | GUC_SUPERUSER_ONLY
		},
		&Unix_socket_directories,
#ifdef HAVE_UNIX_SOCKETS
		DEFAULT_PGSOCKET_DIR,
#else
		"",
#endif
		NULL, NULL, NULL
	},

	{
		{"listen_addresses", PGC_POSTMASTER, CONN_AUTH_SETTINGS,
			gettext_noop("Sets the host name or IP address(es) to listen to."),
			NULL,
			GUC_LIST_INPUT
		},
		&ListenAddresses,
		"localhost",
		NULL, NULL, NULL
	},

	{
		/*
		 * Can't be set by ALTER SYSTEM as it can lead to recursive definition
		 * of data_directory.
		 */
		{"data_directory", PGC_POSTMASTER, FILE_LOCATIONS,
			gettext_noop("Sets the server's data directory."),
			NULL,
			GUC_SUPERUSER_ONLY | GUC_DISALLOW_IN_AUTO_FILE
		},
		&data_directory,
		NULL,
		NULL, NULL, NULL
	},

	{
		{"config_file", PGC_POSTMASTER, FILE_LOCATIONS,
			gettext_noop("Sets the server's main configuration file."),
			NULL,
			GUC_DISALLOW_IN_FILE | GUC_SUPERUSER_ONLY
		},
		&ConfigFileName,
		NULL,
		NULL, NULL, NULL
	},

	{
		{"hba_file", PGC_POSTMASTER, FILE_LOCATIONS,
			gettext_noop("Sets the server's \"hba\" configuration file."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&HbaFileName,
		NULL,
		NULL, NULL, NULL
	},

	{
		{"ident_file", PGC_POSTMASTER, FILE_LOCATIONS,
			gettext_noop("Sets the server's \"ident\" configuration file."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&IdentFileName,
		NULL,
		NULL, NULL, NULL
	},

	{
		{"external_pid_file", PGC_POSTMASTER, FILE_LOCATIONS,
			gettext_noop("Writes the postmaster PID to the specified file."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&external_pid_file,
		NULL,
		check_canonical_path, NULL, NULL
	},

	{
		{"ssl_library", PGC_INTERNAL, PRESET_OPTIONS,
			gettext_noop("Shows the name of the SSL library."),
			NULL,
			GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&ssl_library,
#ifdef USE_SSL
		"OpenSSL",
#else
		"",
#endif
		NULL, NULL, NULL
	},

	{
		{"ssl_cert_file", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Location of the SSL server certificate file."),
			NULL
		},
		&ssl_cert_file,
		"server.crt",
		NULL, NULL, NULL
	},

	{
		{"ssl_key_file", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Location of the SSL server private key file."),
			NULL
		},
		&ssl_key_file,
		"server.key",
		NULL, NULL, NULL
	},

	{
		{"ssl_ca_file", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Location of the SSL certificate authority file."),
			NULL
		},
		&ssl_ca_file,
		"",
		NULL, NULL, NULL
	},

	{
		{"ssl_crl_file", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Location of the SSL certificate revocation list file."),
			NULL
		},
		&ssl_crl_file,
		"",
		NULL, NULL, NULL
	},

	{
		{"ssl_crl_dir", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Location of the SSL certificate revocation list directory."),
			NULL
		},
		&ssl_crl_dir,
		"",
		NULL, NULL, NULL
	},

	{
		{"synchronous_standby_names", PGC_SIGHUP, REPLICATION_PRIMARY,
			gettext_noop("Number of synchronous standbys and list of names of potential synchronous ones."),
			NULL,
			GUC_LIST_INPUT
		},
		&SyncRepStandbyNames,
		"",
		check_synchronous_standby_names, assign_synchronous_standby_names, NULL
	},

	{
		{"default_text_search_config", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets default text search configuration."),
			NULL
		},
		&TSCurrentConfig,
		"pg_catalog.simple",
		check_TSCurrentConfig, assign_TSCurrentConfig, NULL
	},

	{
		{"ssl_ciphers", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Sets the list of allowed SSL ciphers."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&SSLCipherSuites,
#ifdef USE_OPENSSL
		"HIGH:MEDIUM:+3DES:!aNULL",
#else
		"none",
#endif
		NULL, NULL, NULL
	},

	{
		{"ssl_ecdh_curve", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Sets the curve to use for ECDH."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&SSLECDHCurve,
#ifdef USE_SSL
		"prime256v1",
#else
		"none",
#endif
		NULL, NULL, NULL
	},

	{
		{"ssl_dh_params_file", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Location of the SSL DH parameters file."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&ssl_dh_params_file,
		"",
		NULL, NULL, NULL
	},

	{
		{"ssl_passphrase_command", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Command to obtain passphrases for SSL."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&ssl_passphrase_command,
		"",
		NULL, NULL, NULL
	},

	{
		{"application_name", PGC_USERSET, LOGGING_WHAT,
			gettext_noop("Sets the application name to be reported in statistics and logs."),
			NULL,
			GUC_IS_NAME | GUC_REPORT | GUC_NOT_IN_SAMPLE
		},
		&application_name,
		"",
		check_application_name, assign_application_name, NULL
	},

	{
		{"cluster_name", PGC_POSTMASTER, PROCESS_TITLE,
			gettext_noop("Sets the name of the cluster, which is included in the process title."),
			NULL,
			GUC_IS_NAME
		},
		&cluster_name,
		"",
		check_cluster_name, NULL, NULL
	},

	{
		{"wal_consistency_checking", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Sets the WAL resource managers for which WAL consistency checks are done."),
			gettext_noop("Full-page images will be logged for all data blocks and cross-checked against the results of WAL replay."),
			GUC_LIST_INPUT | GUC_NOT_IN_SAMPLE
		},
		&wal_consistency_checking_string,
		"",
		check_wal_consistency_checking, assign_wal_consistency_checking, NULL
	},

	{
		{"jit_provider", PGC_POSTMASTER, CLIENT_CONN_PRELOAD,
			gettext_noop("JIT provider to use."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&jit_provider,
		"llvmjit",
		NULL, NULL, NULL
	},

	{
		{"backtrace_functions", PGC_SUSET, DEVELOPER_OPTIONS,
			gettext_noop("Log backtrace for errors in these functions."),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&backtrace_functions,
		"",
		check_backtrace_functions, assign_backtrace_functions, NULL
	},

	{
		{"yb_test_block_index_phase", PGC_SIGHUP, DEVELOPER_OPTIONS,
			gettext_noop("Block the given index creation phase."),
			gettext_noop("Valid values are \"indislive\", \"indisready\", "
						 "\"backfill\", and \"postbackfill\". "
						 "Any other value is ignored."),
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_block_index_phase,
		"",
		/* Could add a check function, but it's not worth the bother. */
		NULL, NULL, NULL
	},

	{
		{"yb_test_fail_index_state_change", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Fails index backfill at given stage."),
			gettext_noop("Valid values are \"indisready\" and \"postbackfill\"."
						 "Any other value is ignored."),
			GUC_NOT_IN_SAMPLE
		},
		&yb_test_fail_index_state_change,
		"",
		NULL, NULL, NULL
	},

	{
		{"yb_default_replica_identity", PGC_SUSET, REPLICATION_SENDING,
			gettext_noop("Default replica identity at the time of table creation"),
			NULL,
			GUC_NOT_IN_SAMPLE
		},
		&yb_default_replica_identity,
		"CHANGE",
		check_default_replica_identity, NULL, NULL
	},

	/* End-of-list marker */
	{
		{NULL, 0, 0, NULL, NULL}, NULL, NULL, NULL, NULL, NULL
	}
};


static struct config_enum ConfigureNamesEnum[] =
{
	{
		{"backslash_quote", PGC_USERSET, COMPAT_OPTIONS_PREVIOUS,
			gettext_noop("Sets whether \"\\'\" is allowed in string literals."),
			NULL
		},
		&backslash_quote,
		BACKSLASH_QUOTE_SAFE_ENCODING, backslash_quote_options,
		NULL, NULL, NULL
	},

	{
		{"bytea_output", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the output format for bytea."),
			NULL
		},
		&bytea_output,
		BYTEA_OUTPUT_HEX, bytea_output_options,
		NULL, NULL, NULL
	},

	{
		{"client_min_messages", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the message levels that are sent to the client."),
			gettext_noop("Each level includes all the levels that follow it. The later"
						 " the level, the fewer messages are sent.")
		},
		&client_min_messages,
		NOTICE, client_message_level_options,
		NULL, NULL, NULL
	},

	{
		{"compute_query_id", PGC_SUSET, STATS_MONITORING,
			gettext_noop("Enables in-core computation of query identifiers."),
			NULL
		},
		&compute_query_id,
		COMPUTE_QUERY_ID_AUTO, compute_query_id_options,
		NULL, NULL, NULL
	},

	{
		{"constraint_exclusion", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Enables the planner to use constraints to optimize queries."),
			gettext_noop("Table scans will be skipped if their constraints"
						 " guarantee that no rows match the query."),
			GUC_EXPLAIN
		},
		&constraint_exclusion,
		CONSTRAINT_EXCLUSION_PARTITION, constraint_exclusion_options,
		NULL, NULL, NULL
	},

	{
		{"default_toast_compression", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the default compression method for compressible values."),
			NULL
		},
		&default_toast_compression,
		TOAST_PGLZ_COMPRESSION,
		default_toast_compression_options,
		NULL, NULL, NULL
	},

	{
		{"default_transaction_isolation", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the transaction isolation level of each new transaction."),
			NULL
		},
		&DefaultXactIsoLevel,
		XACT_READ_COMMITTED, isolation_level_options,
		check_yb_default_xact_isolation, NULL, NULL
	},

	{
		{"transaction_isolation", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the current transaction's isolation level."),
			NULL,
			GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE | GUC_DISALLOW_IN_FILE
		},
		&XactIsoLevel,
		XACT_READ_COMMITTED, isolation_level_options,
		check_XactIsoLevel, yb_assign_XactIsoLevel, NULL
	},

	{
		{"IntervalStyle", PGC_USERSET, CLIENT_CONN_LOCALE,
			gettext_noop("Sets the display format for interval values."),
			NULL,
			GUC_REPORT
		},
		&IntervalStyle,
		INTSTYLE_POSTGRES, intervalstyle_options,
		NULL, NULL, NULL
	},

	{
		{"log_error_verbosity", PGC_SUSET, LOGGING_WHAT,
			gettext_noop("Sets the verbosity of logged messages."),
			NULL
		},
		&Log_error_verbosity,
		PGERROR_DEFAULT, log_error_verbosity_options,
		NULL, NULL, NULL
	},

	{
		{"log_min_messages", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Sets the message levels that are logged."),
			gettext_noop("Each level includes all the levels that follow it. The later"
						 " the level, the fewer messages are sent.")
		},
		&log_min_messages,
		WARNING, server_message_level_options,
		NULL, NULL, NULL
	},

	{
		{"log_min_error_statement", PGC_SUSET, LOGGING_WHEN,
			gettext_noop("Causes all statements generating error at or above this level to be logged."),
			gettext_noop("Each level includes all the levels that follow it. The later"
						 " the level, the fewer messages are sent.")
		},
		&log_min_error_statement,
		ERROR, server_message_level_options,
		NULL, NULL, NULL
	},

	{
		{"log_statement", PGC_SUSET, LOGGING_WHAT,
			gettext_noop("Sets the type of statements logged."),
			NULL
		},
		&log_statement,
		LOGSTMT_NONE, log_statement_options,
		NULL, NULL, NULL
	},

	{
		{"syslog_facility", PGC_SIGHUP, LOGGING_WHERE,
			gettext_noop("Sets the syslog \"facility\" to be used when syslog enabled."),
			NULL
		},
		&syslog_facility,
#ifdef HAVE_SYSLOG
		LOG_LOCAL0,
#else
		0,
#endif
		syslog_facility_options,
		NULL, assign_syslog_facility, NULL
	},

	{
		{"session_replication_role", PGC_SUSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets the session's behavior for triggers and rewrite rules."),
			NULL
		},
		&SessionReplicationRole,
		SESSION_REPLICATION_ROLE_ORIGIN, session_replication_role_options,
		NULL, assign_session_replication_role, NULL
	},

	{
		{"synchronous_commit", PGC_USERSET, WAL_SETTINGS,
			gettext_noop("Sets the current transaction's synchronization level."),
			NULL
		},
		&synchronous_commit,
		SYNCHRONOUS_COMMIT_ON, synchronous_commit_options,
		NULL, assign_synchronous_commit, NULL
	},

	{
		{"archive_mode", PGC_POSTMASTER, WAL_ARCHIVING,
			gettext_noop("Allows archiving of WAL files using archive_command."),
			NULL
		},
		&XLogArchiveMode,
		ARCHIVE_MODE_OFF, archive_mode_options,
		NULL, NULL, NULL
	},

	{
		{"recovery_target_action", PGC_POSTMASTER, WAL_RECOVERY_TARGET,
			gettext_noop("Sets the action to perform upon reaching the recovery target."),
			NULL
		},
		&recoveryTargetAction,
		RECOVERY_TARGET_ACTION_PAUSE, recovery_target_action_options,
		NULL, NULL, NULL
	},

	{
		{"trace_recovery_messages", PGC_SIGHUP, DEVELOPER_OPTIONS,
			gettext_noop("Enables logging of recovery-related debugging information."),
			gettext_noop("Each level includes all the levels that follow it. The later"
						 " the level, the fewer messages are sent."),
			GUC_NOT_IN_SAMPLE,
		},
		&trace_recovery_messages,

		/*
		 * client_message_level_options allows too many values, really, but
		 * it's not worth having a separate options array for this.
		 */
		LOG, client_message_level_options,
		NULL, NULL, NULL
	},

	{
		{"track_functions", PGC_SUSET, STATS_CUMULATIVE,
			gettext_noop("Collects function-level statistics on database activity."),
			NULL
		},
		&pgstat_track_functions,
		TRACK_FUNC_OFF, track_function_options,
		NULL, NULL, NULL
	},


	{
		{"stats_fetch_consistency", PGC_USERSET, STATS_CUMULATIVE,
			gettext_noop("Sets the consistency of accesses to statistics data."),
			NULL
		},
		&pgstat_fetch_consistency,
		PGSTAT_FETCH_CONSISTENCY_CACHE, stats_fetch_consistency,
		NULL, NULL, NULL
	},

	{
		{"wal_compression", PGC_SUSET, WAL_SETTINGS,
			gettext_noop("Compresses full-page writes written in WAL file with specified method."),
			NULL
		},
		&wal_compression,
		WAL_COMPRESSION_NONE, wal_compression_options,
		NULL, NULL, NULL
	},

	{
		{"wal_level", PGC_POSTMASTER, WAL_SETTINGS,
			gettext_noop("Sets the level of information written to the WAL."),
			NULL
		},
		&wal_level,
		/*
		 * YB NOTE: wal_level is not applicable to YB. So for user experience,
		 * we set the default to logical, so that any logical replication
		 * client doesn't throw any errors based on the value of the wal_level.
		 */
		WAL_LEVEL_LOGICAL, wal_level_options,
		NULL, NULL, NULL
	},

	{
		{"dynamic_shared_memory_type", PGC_POSTMASTER, RESOURCES_MEM,
			gettext_noop("Selects the dynamic shared memory implementation used."),
			NULL
		},
		&dynamic_shared_memory_type,
		DEFAULT_DYNAMIC_SHARED_MEMORY_TYPE, dynamic_shared_memory_options,
		NULL, NULL, NULL
	},

	{
		{"shared_memory_type", PGC_POSTMASTER, RESOURCES_MEM,
			gettext_noop("Selects the shared memory implementation used for the main shared memory region."),
			NULL
		},
		&shared_memory_type,
		DEFAULT_SHARED_MEMORY_TYPE, shared_memory_options,
		NULL, NULL, NULL
	},

	{
		{"wal_sync_method", PGC_SIGHUP, WAL_SETTINGS,
			gettext_noop("Selects the method used for forcing WAL updates to disk."),
			NULL
		},
		&sync_method,
		DEFAULT_SYNC_METHOD, sync_method_options,
		NULL, assign_xlog_sync_method, NULL
	},

	{
		{"xmlbinary", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets how binary values are to be encoded in XML."),
			NULL
		},
		&xmlbinary,
		XMLBINARY_BASE64, xmlbinary_options,
		NULL, NULL, NULL
	},

	{
		{"xmloption", PGC_USERSET, CLIENT_CONN_STATEMENT,
			gettext_noop("Sets whether XML data in implicit parsing and serialization "
						 "operations is to be considered as documents or content fragments."),
			NULL
		},
		&xmloption,
		XMLOPTION_CONTENT, xmloption_options,
		NULL, NULL, NULL
	},

	{
		{"huge_pages", PGC_POSTMASTER, RESOURCES_MEM,
			gettext_noop("Use of huge pages on Linux or Windows."),
			NULL
		},
		&huge_pages,
		HUGE_PAGES_TRY, huge_pages_options,
		NULL, NULL, NULL
	},

	{
		{"recovery_prefetch", PGC_SIGHUP, WAL_RECOVERY,
			gettext_noop("Prefetch referenced blocks during recovery."),
			gettext_noop("Look ahead in the WAL to find references to uncached data.")
		},
		&recovery_prefetch,
		RECOVERY_PREFETCH_TRY, recovery_prefetch_options,
		check_recovery_prefetch, assign_recovery_prefetch, NULL
	},

	{
		{"force_parallel_mode", PGC_USERSET, DEVELOPER_OPTIONS,
			gettext_noop("Forces use of parallel query facilities."),
			gettext_noop("If possible, run query using a parallel worker and with parallel restrictions."),
			GUC_NOT_IN_SAMPLE | GUC_EXPLAIN
		},
		&force_parallel_mode,
		FORCE_PARALLEL_OFF, force_parallel_mode_options,
		NULL, NULL, NULL
	},

	{
		{"password_encryption", PGC_USERSET, CONN_AUTH_AUTH,
			gettext_noop("Chooses the algorithm for encrypting passwords."),
			NULL
		},
		&Password_encryption,
		/*
		 * YB_TODO: Change encryption method back to 'scram-sha-256' from 'md5'.
		 * Currently YSQL Connection Manager times out when using scram passwords for unknown reasons.
		 */
		PASSWORD_TYPE_MD5, password_encryption_options,
		NULL, NULL, NULL
	},

	{
		{"plan_cache_mode", PGC_USERSET, QUERY_TUNING_OTHER,
			gettext_noop("Controls the planner's selection of custom or generic plan."),
			gettext_noop("Prepared statements can have custom and generic plans, and the planner "
						 "will attempt to choose which is better.  This can be set to override "
						 "the default behavior."),
			GUC_EXPLAIN
		},
		&plan_cache_mode,
		PLAN_CACHE_MODE_AUTO, plan_cache_mode_options,
		NULL, NULL, NULL
	},

	{
		{"ssl_min_protocol_version", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Sets the minimum SSL/TLS protocol version to use."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&ssl_min_protocol_version,
		PG_TLS1_2_VERSION,
		ssl_protocol_versions_info + 1, /* don't allow PG_TLS_ANY */
		NULL, NULL, NULL
	},

	{
		{"ssl_max_protocol_version", PGC_SIGHUP, CONN_AUTH_SSL,
			gettext_noop("Sets the maximum SSL/TLS protocol version to use."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&ssl_max_protocol_version,
		PG_TLS_ANY,
		ssl_protocol_versions_info,
		NULL, NULL, NULL
	},

	{
		{"recovery_init_sync_method", PGC_SIGHUP, ERROR_HANDLING_OPTIONS,
			gettext_noop("Sets the method for synchronizing the data directory before crash recovery."),
		},
		&recovery_init_sync_method,
		RECOVERY_INIT_SYNC_METHOD_FSYNC, recovery_init_sync_method_options,
		NULL, NULL, NULL
	},

	{
		{"yb_pg_batch_detection_mechanism", PGC_USERSET, COMPAT_OPTIONS_CLIENT,
			gettext_noop("The drivers use message protocol to communicate "
						 "with PG. The driver does not inform PG in advance "
						 "about a Batch execution. We need to identify a batch "
						 "because in that case the single-shard optimization "
						 "should be disabled. Postgres drivers pipeline "
						 "messages and we exploit this to peek the message "
						 "following 'Execute' to detect a batch. This may "
						 "lead to some unforeseen bugs, so this GUC provides "
						 "a way to disable the single-shard optimization "
						 "completely or go back to the behavior before "
						 "#16446 was fixed."),
			NULL,
			GUC_SUPERUSER_ONLY
		},
		&yb_pg_batch_detection_mechanism,
		DETECT_BY_PEEKING,
		yb_pg_batch_detection_mechanism_options,
		NULL, assign_yb_pg_batch_detection_mechanism, NULL
	},

	{
		/*
		 * Read-after-commit-visibility guarantee: any client issued read
		 * should see all data that was committed before the read request
		 * was issued (even in the presence of clock skew between nodes).
		 * In other words, the following example should always work:
		 * (1) User X commits some data (for which the db picks a commit
		 * 	timestamp say ht1)
		 * (2) Then user X communicates to user Y to inform about the commit
		 * 	via a channel outside the database (say a phone call)
		 * (3) Then user Y issues a read to some YB node which picks a
		 * 	read time (less than ht1 due to clock skew)
		 * (4) Then it should not happen that user Y gets an output without
		 * 	the data that user Y was informed about.
		 */
		{
			"yb_read_after_commit_visibility", PGC_USERSET, CUSTOM_OPTIONS,
			gettext_noop("Control read-after-commit-visibility guarantee."),
			gettext_noop(
				"This GUC is intended as a crutch for users migrating from PostgreSQL and new to"
				" read restart errors. Users can now largely avoid these errors when"
				" read-after-commit-visibility guarantee is not a strong requirement."
				" This option cannot be set from within a transaction block."
				" Configure one of the following options:"
				" (a) strict: Default Behavior. The read-after-commit-visibility guarantee is"
				" maintained by the database. However, users may see read restart errors that"
				" show \"ERROR:  Query error: Restart read required at: ...\". The database"
				" attempts to retry on such errors internally but that is not always possible."
				" (b) relaxed: With this option, the read-after-commit-visibility guarantee is"
				" relaxed. Read only statements/transactions do not see read restart errors but"
				" may miss recent updates with staleness bounded by clock skew."
			),
			0
		},
		&yb_read_after_commit_visibility,
		YB_STRICT_READ_AFTER_COMMIT_VISIBILITY,
		yb_read_after_commit_visibility_options,
		yb_check_no_txn, NULL, NULL
	},

	/* End-of-list marker */
	{
		{NULL, 0, 0, NULL, NULL}, NULL, 0, NULL, NULL, NULL, NULL
	}
};

/******** end of options list ********/


/*
 * To allow continued support of obsolete names for GUC variables, we apply
 * the following mappings to any unrecognized name.  Note that an old name
 * should be mapped to a new one only if the new variable has very similar
 * semantics to the old.
 */
static const char *const map_old_guc_names[] = {
	"sort_mem", "work_mem",
	"vacuum_mem", "maintenance_work_mem",
	NULL
};

/*
 * Contains list of GUC variables that both fall under PGC_SUSET context
 * and can be modified by the yb_db_admin role. This is needed to allow
 * yb_db_admin to modify PG_SUSET variables without being a superuser itself.
 */
static const char *const YbDbAdminVariables[] = {
	"session_replication_role",
	"yb_make_next_ddl_statement_nonbreaking",
	"yb_make_next_ddl_statement_nonincrementing",
};


/*
 * Actual lookup of variables is done through this single, sorted array.
 */
static struct config_generic **guc_variables;

/* Current number of variables contained in the vector */
static int	num_guc_variables;

/* Vector capacity */
static int	size_guc_variables;


static bool guc_dirty;			/* true if need to do commit/abort work */

static bool reporting_enabled;	/* true to enable GUC_REPORT */

static bool report_needed;		/* true if any GUC_REPORT reports are needed */

static int	GUCNestLevel = 0;	/* 1 when in main transaction */


static int	guc_var_compare(const void *a, const void *b);
static int	guc_name_compare(const char *namea, const char *nameb);
static void InitializeGUCOptionsFromEnvironment(void);
static void InitializeOneGUCOption(struct config_generic *gconf);
static void push_old_value(struct config_generic *gconf, GucAction action);
static void ReportGUCOption(struct config_generic *record);
static void reapply_stacked_values(struct config_generic *variable,
								   struct config_string *pHolder,
								   GucStack *stack,
								   const char *curvalue,
								   GucContext curscontext, GucSource cursource,
								   Oid cursrole);
static void ShowGUCConfigOption(const char *name, DestReceiver *dest);
static void ShowAllGUCConfig(DestReceiver *dest);
static char *_ShowOption(struct config_generic *record, bool use_units);
static bool validate_option_array_item(const char *name, const char *value,
									   bool skipIfNoPermissions);
static void write_auto_conf_file(int fd, const char *filename, ConfigVariable *head_p);
static void replace_auto_config_value(ConfigVariable **head_p, ConfigVariable **tail_p,
									  const char *name, const char *value);


/*
 * Some infrastructure for checking malloc/strdup/realloc calls
 */
static void *
guc_malloc(int elevel, size_t size)
{
	void	   *data;

	/* Avoid unportable behavior of malloc(0) */
	if (size == 0)
		size = 1;
	data = malloc(size);
	if (data == NULL)
		ereport(elevel,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));
	return data;
}

static void *
guc_realloc(int elevel, void *old, size_t size)
{
	void	   *data;

	/* Avoid unportable behavior of realloc(NULL, 0) */
	if (old == NULL && size == 0)
		size = 1;
	data = realloc(old, size);
	if (data == NULL)
		ereport(elevel,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));
	return data;
}

static char *
guc_strdup(int elevel, const char *src)
{
	char	   *data;

	data = strdup(src);
	if (data == NULL)
		ereport(elevel,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));
	return data;
}


/*
 * Detect whether strval is referenced anywhere in a GUC string item
 */
static bool
string_field_used(struct config_string *conf, char *strval)
{
	GucStack   *stack;

	if (strval == *(conf->variable) ||
		strval == conf->reset_val ||
		strval == conf->boot_val)
		return true;
	for (stack = conf->gen.stack; stack; stack = stack->prev)
	{
		if (strval == stack->prior.val.stringval ||
			strval == stack->masked.val.stringval)
			return true;
	}
	return false;
}

/*
 * Support for assigning to a field of a string GUC item.  Free the prior
 * value if it's not referenced anywhere else in the item (including stacked
 * states).
 */
static void
set_string_field(struct config_string *conf, char **field, char *newval)
{
	char	   *oldval = *field;

	/* Do the assignment */
	*field = newval;

	/* Free old value if it's not NULL and isn't referenced anymore */
	if (oldval && !string_field_used(conf, oldval))
		free(oldval);
}

/*
 * Detect whether an "extra" struct is referenced anywhere in a GUC item
 */
static bool
extra_field_used(struct config_generic *gconf, void *extra)
{
	GucStack   *stack;

	if (extra == gconf->extra)
		return true;
	switch (gconf->vartype)
	{
		case PGC_BOOL:
			if (extra == ((struct config_bool *) gconf)->reset_extra)
				return true;
			break;
		case PGC_INT:
			if (extra == ((struct config_int *) gconf)->reset_extra)
				return true;
			break;
		case PGC_OID:
			if (extra == ((struct config_oid*) gconf)->reset_extra)
				return true;
			break;
		case PGC_REAL:
			if (extra == ((struct config_real *) gconf)->reset_extra)
				return true;
			break;
		case PGC_STRING:
			if (extra == ((struct config_string *) gconf)->reset_extra)
				return true;
			break;
		case PGC_ENUM:
			if (extra == ((struct config_enum *) gconf)->reset_extra)
				return true;
			break;
	}
	for (stack = gconf->stack; stack; stack = stack->prev)
	{
		if (extra == stack->prior.extra ||
			extra == stack->masked.extra)
			return true;
	}

	return false;
}

/*
 * Support for assigning to an "extra" field of a GUC item.  Free the prior
 * value if it's not referenced anywhere else in the item (including stacked
 * states).
 */
static void
set_extra_field(struct config_generic *gconf, void **field, void *newval)
{
	void	   *oldval = *field;

	/* Do the assignment */
	*field = newval;

	/* Free old value if it's not NULL and isn't referenced anymore */
	if (oldval && !extra_field_used(gconf, oldval))
		free(oldval);
}

/*
 * Support for copying a variable's active value into a stack entry.
 * The "extra" field associated with the active value is copied, too.
 *
 * NB: be sure stringval and extra fields of a new stack entry are
 * initialized to NULL before this is used, else we'll try to free() them.
 */
static void
set_stack_value(struct config_generic *gconf, config_var_value *val)
{
	switch (gconf->vartype)
	{
		case PGC_BOOL:
			val->val.boolval =
				*((struct config_bool *) gconf)->variable;
			break;
		case PGC_INT:
			val->val.intval =
				*((struct config_int *) gconf)->variable;
			break;
		case PGC_OID:
			val->val.oidval =
				*((struct config_oid *) gconf)->variable;
			break;
		case PGC_REAL:
			val->val.realval =
				*((struct config_real *) gconf)->variable;
			break;
		case PGC_STRING:
			set_string_field((struct config_string *) gconf,
							 &(val->val.stringval),
							 *((struct config_string *) gconf)->variable);
			break;
		case PGC_ENUM:
			val->val.enumval =
				*((struct config_enum *) gconf)->variable;
			break;
	}
	set_extra_field(gconf, &(val->extra), gconf->extra);
}

/*
 * Support for discarding a no-longer-needed value in a stack entry.
 * The "extra" field associated with the stack entry is cleared, too.
 */
static void
discard_stack_value(struct config_generic *gconf, config_var_value *val)
{
	switch (gconf->vartype)
	{
		case PGC_BOOL:
		case PGC_INT:
		case PGC_OID:
		case PGC_REAL:
		case PGC_ENUM:
			/* no need to do anything */
			break;
		case PGC_STRING:
			set_string_field((struct config_string *) gconf,
							 &(val->val.stringval),
							 NULL);
			break;
	}
	set_extra_field(gconf, &(val->extra), NULL);
}


/*
 * Fetch the sorted array pointer (exported for help_config.c's use ONLY)
 */
struct config_generic **
get_guc_variables(void)
{
	return guc_variables;
}


/*
 * Build the sorted array.  This is split out so that it could be
 * re-executed after startup (e.g., we could allow loadable modules to
 * add vars, and then we'd need to re-sort).
 */
void
build_guc_variables(void)
{
	int			size_vars;
	int			num_vars = 0;
	struct config_generic **guc_vars;
	int			i;

	for (i = 0; ConfigureNamesBool[i].gen.name; i++)
	{
		struct config_bool *conf = &ConfigureNamesBool[i];

		/* Rather than requiring vartype to be filled in by hand, do this: */
		conf->gen.vartype = PGC_BOOL;
		num_vars++;
	}

	for (i = 0; ConfigureNamesInt[i].gen.name; i++)
	{
		struct config_int *conf = &ConfigureNamesInt[i];

		conf->gen.vartype = PGC_INT;
		num_vars++;
	}

	for (i = 0; ConfigureNamesOid[i].gen.name; i++)
	{
		struct config_oid *conf = &ConfigureNamesOid[i];

		conf->gen.vartype = PGC_OID;
		num_vars++;
	}

	for (i = 0; ConfigureNamesReal[i].gen.name; i++)
	{
		struct config_real *conf = &ConfigureNamesReal[i];

		conf->gen.vartype = PGC_REAL;
		num_vars++;
	}

	for (i = 0; ConfigureNamesString[i].gen.name; i++)
	{
		struct config_string *conf = &ConfigureNamesString[i];

		conf->gen.vartype = PGC_STRING;
		num_vars++;
	}

	for (i = 0; ConfigureNamesEnum[i].gen.name; i++)
	{
		struct config_enum *conf = &ConfigureNamesEnum[i];

		conf->gen.vartype = PGC_ENUM;
		num_vars++;
	}

	/*
	 * Create table with 20% slack
	 */
	size_vars = num_vars + num_vars / 4;

	guc_vars = (struct config_generic **)
		guc_malloc(FATAL, size_vars * sizeof(struct config_generic *));

	num_vars = 0;

	for (i = 0; ConfigureNamesBool[i].gen.name; i++)
		guc_vars[num_vars++] = &ConfigureNamesBool[i].gen;

	for (i = 0; ConfigureNamesInt[i].gen.name; i++)
		guc_vars[num_vars++] = &ConfigureNamesInt[i].gen;

	for (i = 0; ConfigureNamesOid[i].gen.name; i++)
		guc_vars[num_vars++] = &ConfigureNamesOid[i].gen;

	for (i = 0; ConfigureNamesReal[i].gen.name; i++)
		guc_vars[num_vars++] = &ConfigureNamesReal[i].gen;

	for (i = 0; ConfigureNamesString[i].gen.name; i++)
		guc_vars[num_vars++] = &ConfigureNamesString[i].gen;

	for (i = 0; ConfigureNamesEnum[i].gen.name; i++)
		guc_vars[num_vars++] = &ConfigureNamesEnum[i].gen;

	if (guc_variables)
		free(guc_variables);
	guc_variables = guc_vars;
	num_guc_variables = num_vars;
	size_guc_variables = size_vars;
	qsort((void *) guc_variables, num_guc_variables,
		  sizeof(struct config_generic *), guc_var_compare);
}

/*
 * Add a new GUC variable to the list of known variables. The
 * list is expanded if needed.
 */
static bool
add_guc_variable(struct config_generic *var, int elevel)
{
	if (num_guc_variables + 1 >= size_guc_variables)
	{
		/*
		 * Increase the vector by 25%
		 */
		int			size_vars = size_guc_variables + size_guc_variables / 4;
		struct config_generic **guc_vars;

		if (size_vars == 0)
		{
			size_vars = 100;
			guc_vars = (struct config_generic **)
				guc_malloc(elevel, size_vars * sizeof(struct config_generic *));
		}
		else
		{
			guc_vars = (struct config_generic **)
				guc_realloc(elevel, guc_variables, size_vars * sizeof(struct config_generic *));
		}

		if (guc_vars == NULL)
			return false;		/* out of memory */

		guc_variables = guc_vars;
		size_guc_variables = size_vars;
	}
	guc_variables[num_guc_variables++] = var;
	qsort((void *) guc_variables, num_guc_variables,
		  sizeof(struct config_generic *), guc_var_compare);
	return true;
}

/*
 * Decide whether a proposed custom variable name is allowed.
 *
 * It must be two or more identifiers separated by dots, where the rules
 * for what is an identifier agree with scan.l.  (If you change this rule,
 * adjust the errdetail in find_option().)
 */
static bool
valid_custom_variable_name(const char *name)
{
	bool		saw_sep = false;
	bool		name_start = true;

	for (const char *p = name; *p; p++)
	{
		if (*p == GUC_QUALIFIER_SEPARATOR)
		{
			if (name_start)
				return false;	/* empty name component */
			saw_sep = true;
			name_start = true;
		}
		else if (strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
						"abcdefghijklmnopqrstuvwxyz_", *p) != NULL ||
				 IS_HIGHBIT_SET(*p))
		{
			/* okay as first or non-first character */
			name_start = false;
		}
		else if (!name_start && strchr("0123456789$", *p) != NULL)
			 /* okay as non-first character */ ;
		else
			return false;
	}
	if (name_start)
		return false;			/* empty name component */
	/* OK if we found at least one separator */
	return saw_sep;
}

/*
 * Create and add a placeholder variable for a custom variable name.
 */
static struct config_generic *
add_placeholder_variable(const char *name, int elevel)
{
	size_t		sz = sizeof(struct config_string) + sizeof(char *);
	struct config_string *var;
	struct config_generic *gen;

	var = (struct config_string *) guc_malloc(elevel, sz);
	if (var == NULL)
		return NULL;
	memset(var, 0, sz);
	gen = &var->gen;

	gen->name = guc_strdup(elevel, name);
	if (gen->name == NULL)
	{
		free(var);
		return NULL;
	}

	gen->context = PGC_USERSET;
	gen->group = CUSTOM_OPTIONS;
	gen->short_desc = "GUC placeholder variable";
	gen->flags = GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE | GUC_CUSTOM_PLACEHOLDER;
	gen->vartype = PGC_STRING;

	/*
	 * The char* is allocated at the end of the struct since we have no
	 * 'static' place to point to.  Note that the current value, as well as
	 * the boot and reset values, start out NULL.
	 */
	var->variable = (char **) (var + 1);

	if (!add_guc_variable((struct config_generic *) var, elevel))
	{
		free(unconstify(char *, gen->name));
		free(var);
		return NULL;
	}

	return gen;
}

/*
 * Look up option "name".  If it exists, return a pointer to its record.
 * Otherwise, if create_placeholders is true and name is a valid-looking
 * custom variable name, we'll create and return a placeholder record.
 * Otherwise, if skip_errors is true, then we silently return NULL for
 * an unrecognized or invalid name.  Otherwise, the error is reported at
 * error level elevel (and we return NULL if that's less than ERROR).
 *
 * Note: internal errors, primarily out-of-memory, draw an elevel-level
 * report and NULL return regardless of skip_errors.  Hence, callers must
 * handle a NULL return whenever elevel < ERROR, but they should not need
 * to emit any additional error message.  (In practice, internal errors
 * can only happen when create_placeholders is true, so callers passing
 * false need not think terribly hard about this.)
 */
static struct config_generic *
find_option(const char *name, bool create_placeholders, bool skip_errors,
			int elevel)
{
#ifdef ADDRESS_SANITIZER
	struct config_generic config_placeholder;
	config_placeholder.name = name;
	const char **key = &config_placeholder.name;
#else
	const char **key = &name;
#endif
	struct config_generic **res;
	int			i;

	Assert(name);

	/*
	 * By equating const char ** with struct config_generic *, we are assuming
	 * the name field is first in config_generic.
	 */
	res = (struct config_generic **) bsearch((void *) &key,
											 (void *) guc_variables,
											 num_guc_variables,
											 sizeof(struct config_generic *),
											 guc_var_compare);
	if (res)
		return *res;

	/*
	 * See if the name is an obsolete name for a variable.  We assume that the
	 * set of supported old names is short enough that a brute-force search is
	 * the best way.
	 */
	for (i = 0; map_old_guc_names[i] != NULL; i += 2)
	{
		if (guc_name_compare(name, map_old_guc_names[i]) == 0)
			return find_option(map_old_guc_names[i + 1], false,
							   skip_errors, elevel);
	}

	if (create_placeholders)
	{
		/*
		 * Check if the name is valid, and if so, add a placeholder.  If it
		 * doesn't contain a separator, don't assume that it was meant to be a
		 * placeholder.
		 */
		const char *sep = strchr(name, GUC_QUALIFIER_SEPARATOR);

		if (sep != NULL)
		{
			size_t		classLen = sep - name;
			ListCell   *lc;

			/* The name must be syntactically acceptable ... */
			if (!valid_custom_variable_name(name))
			{
				if (!skip_errors)
					ereport(elevel,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("invalid configuration parameter name \"%s\"",
									name),
							 errdetail("Custom parameter names must be two or more simple identifiers separated by dots.")));
				return NULL;
			}
			/* ... and it must not match any previously-reserved prefix */
			foreach(lc, reserved_class_prefix)
			{
				const char *rcprefix = lfirst(lc);

				if (strlen(rcprefix) == classLen &&
					strncmp(name, rcprefix, classLen) == 0)
				{
					if (!skip_errors)
						ereport(elevel,
								(errcode(ERRCODE_INVALID_NAME),
								 errmsg("invalid configuration parameter name \"%s\"",
										name),
								 errdetail("\"%s\" is a reserved prefix.",
										   rcprefix)));
					return NULL;
				}
			}
			/* OK, create it */
			return add_placeholder_variable(name, elevel);
		}
	}

	/* Unknown name */
	if (!skip_errors)
		ereport(elevel,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("unrecognized configuration parameter \"%s\"",
						name)));
	return NULL;
}


/*
 * comparator for qsorting and bsearching guc_variables array
 */
static int
guc_var_compare(const void *a, const void *b)
{
	const struct config_generic *confa = *(struct config_generic *const *) a;
	const struct config_generic *confb = *(struct config_generic *const *) b;

	return guc_name_compare(confa->name, confb->name);
}

/*
 * the bare comparison function for GUC names
 */
static int
guc_name_compare(const char *namea, const char *nameb)
{
	/*
	 * The temptation to use strcasecmp() here must be resisted, because the
	 * array ordering has to remain stable across setlocale() calls. So, build
	 * our own with a simple ASCII-only downcasing.
	 */
	while (*namea && *nameb)
	{
		char		cha = *namea++;
		char		chb = *nameb++;

		if (cha >= 'A' && cha <= 'Z')
			cha += 'a' - 'A';
		if (chb >= 'A' && chb <= 'Z')
			chb += 'a' - 'A';
		if (cha != chb)
			return cha - chb;
	}
	if (*namea)
		return 1;				/* a is longer */
	if (*nameb)
		return -1;				/* b is longer */
	return 0;
}


/*
 * Convert a GUC name to the form that should be used in pg_parameter_acl.
 *
 * We need to canonicalize entries since, for example, case should not be
 * significant.  In addition, we apply the map_old_guc_names[] mapping so that
 * any obsolete names will be converted when stored in a new PG version.
 * Note however that this function does not verify legality of the name.
 *
 * The result is a palloc'd string.
 */
char *
convert_GUC_name_for_parameter_acl(const char *name)
{
	char	   *result;

	/* Apply old-GUC-name mapping. */
	for (int i = 0; map_old_guc_names[i] != NULL; i += 2)
	{
		if (guc_name_compare(name, map_old_guc_names[i]) == 0)
		{
			name = map_old_guc_names[i + 1];
			break;
		}
	}

	/* Apply case-folding that matches guc_name_compare(). */
	result = pstrdup(name);
	for (char *ptr = result; *ptr != '\0'; ptr++)
	{
		char		ch = *ptr;

		if (ch >= 'A' && ch <= 'Z')
		{
			ch += 'a' - 'A';
			*ptr = ch;
		}
	}

	return result;
}

/*
 * Check whether we should allow creation of a pg_parameter_acl entry
 * for the given name.  (This can be applied either before or after
 * canonicalizing it.)
 */
bool
check_GUC_name_for_parameter_acl(const char *name)
{
	/* OK if the GUC exists. */
	if (find_option(name, false, true, DEBUG1) != NULL)
		return true;
	/* Otherwise, it'd better be a valid custom GUC name. */
	if (valid_custom_variable_name(name))
		return true;
	return false;
}


/*
 * Initialize GUC options during program startup.
 *
 * Note that we cannot read the config file yet, since we have not yet
 * processed command-line switches.
 */
void
InitializeGUCOptions(void)
{
	int			i;

	/*
	 * Before log_line_prefix could possibly receive a nonempty setting, make
	 * sure that timezone processing is minimally alive (see elog.c).
	 */
	pg_timezone_initialize();

	/*
	 * Build sorted array of all GUC variables.
	 */
	build_guc_variables();

	/*
	 * Load all variables with their compiled-in defaults, and initialize
	 * status fields as needed.
	 */
	for (i = 0; i < num_guc_variables; i++)
	{
		InitializeOneGUCOption(guc_variables[i]);
	}

	guc_dirty = false;

	reporting_enabled = false;

	/*
	 * Prevent any attempt to override the transaction modes from
	 * non-interactive sources.
	 */
	SetConfigOption("transaction_isolation", "read committed",
					PGC_POSTMASTER, PGC_S_OVERRIDE);
	SetConfigOption("transaction_read_only", "no",
					PGC_POSTMASTER, PGC_S_OVERRIDE);
	SetConfigOption("transaction_deferrable", "no",
					PGC_POSTMASTER, PGC_S_OVERRIDE);

	/*
	 * For historical reasons, some GUC parameters can receive defaults from
	 * environment variables.  Process those settings.
	 */
	InitializeGUCOptionsFromEnvironment();
}

/*
 * If any custom resource managers were specified in the
 * wal_consistency_checking GUC, processing was deferred. Now that
 * shared_preload_libraries have been loaded, process wal_consistency_checking
 * again.
 */
void
InitializeWalConsistencyChecking(void)
{
	Assert(process_shared_preload_libraries_done);

	if (check_wal_consistency_checking_deferred)
	{
		struct config_generic *guc;

		guc = find_option("wal_consistency_checking", false, false, ERROR);

		check_wal_consistency_checking_deferred = false;

		set_config_option_ext("wal_consistency_checking",
							  wal_consistency_checking_string,
							  guc->scontext, guc->source, guc->srole,
							  GUC_ACTION_SET, true, ERROR, false);

		/* checking should not be deferred again */
		Assert(!check_wal_consistency_checking_deferred);
	}
}

/*
 * Assign any GUC values that can come from the server's environment.
 *
 * This is called from InitializeGUCOptions, and also from ProcessConfigFile
 * to deal with the possibility that a setting has been removed from
 * postgresql.conf and should now get a value from the environment.
 * (The latter is a kludge that should probably go away someday; if so,
 * fold this back into InitializeGUCOptions.)
 */
static void
InitializeGUCOptionsFromEnvironment(void)
{
	char	   *env;
	long		stack_rlimit;

	env = getenv("PGPORT");
	if (env != NULL)
		SetConfigOption("port", env, PGC_POSTMASTER, PGC_S_ENV_VAR);

	env = getenv("PGDATESTYLE");
	if (env != NULL)
		SetConfigOption("datestyle", env, PGC_POSTMASTER, PGC_S_ENV_VAR);

	env = getenv("PGCLIENTENCODING");
	if (env != NULL)
		SetConfigOption("client_encoding", env, PGC_POSTMASTER, PGC_S_ENV_VAR);

	/*
	* YB: For backwards compatibility, set the value of yb_fetch_row_limit
	* to the value of ysql_prefetch_limit (which is deprecated).
	*/
	env = getenv("FLAGS_ysql_prefetch_limit");
	if (env != NULL)
		SetConfigOption("yb_fetch_row_limit", env,
				PGC_POSTMASTER, PGC_S_ENV_VAR);

	/*
	 * rlimit isn't exactly an "environment variable", but it behaves about
	 * the same.  If we can identify the platform stack depth rlimit, increase
	 * default stack depth setting up to whatever is safe (but at most 2MB).
	 * Report the value's source as PGC_S_DYNAMIC_DEFAULT if it's 2MB, or as
	 * PGC_S_ENV_VAR if it's reflecting the rlimit limit.
	 */
	stack_rlimit = get_stack_depth_rlimit();
	if (stack_rlimit > 0)
	{
		long		new_limit = (stack_rlimit - STACK_DEPTH_SLOP) / 1024L;

		if (new_limit > 100)
		{
			GucSource	source;
			char		limbuf[16];

			if (new_limit < 2048)
				source = PGC_S_ENV_VAR;
			else
			{
				new_limit = 2048;
				source = PGC_S_DYNAMIC_DEFAULT;
			}
			snprintf(limbuf, sizeof(limbuf), "%ld", new_limit);
			SetConfigOption("max_stack_depth", limbuf,
							PGC_POSTMASTER, source);
		}
	}
}

/*
 * Initialize one GUC option variable to its compiled-in default.
 *
 * Note: the reason for calling check_hooks is not that we think the boot_val
 * might fail, but that the hooks might wish to compute an "extra" struct.
 */
static void
InitializeOneGUCOption(struct config_generic *gconf)
{
	gconf->status = 0;
	gconf->source = PGC_S_DEFAULT;
	gconf->reset_source = PGC_S_DEFAULT;
	gconf->scontext = PGC_INTERNAL;
	gconf->reset_scontext = PGC_INTERNAL;
	gconf->srole = BOOTSTRAP_SUPERUSERID;
	gconf->reset_srole = BOOTSTRAP_SUPERUSERID;
	gconf->stack = NULL;
	gconf->extra = NULL;
	gconf->last_reported = NULL;
	gconf->sourcefile = NULL;
	gconf->sourceline = 0;

	switch (gconf->vartype)
	{
		case PGC_BOOL:
			{
				struct config_bool *conf = (struct config_bool *) gconf;
				bool		newval = conf->boot_val;
				void	   *extra = NULL;

				if (!call_bool_check_hook(conf, &newval, &extra,
										  PGC_S_DEFAULT, LOG))
					elog(FATAL, "failed to initialize %s to %d",
						 conf->gen.name, (int) newval);
				if (conf->assign_hook)
					conf->assign_hook(newval, extra);
				*conf->variable = conf->reset_val = newval;
				conf->gen.extra = conf->reset_extra = extra;
				break;
			}
		case PGC_INT:
			{
				struct config_int *conf = (struct config_int *) gconf;
				int			newval = conf->boot_val;
				void	   *extra = NULL;

				Assert(newval >= conf->min);
				Assert(newval <= conf->max);
				if (!call_int_check_hook(conf, &newval, &extra,
										 PGC_S_DEFAULT, LOG))
					elog(FATAL, "failed to initialize %s to %d",
						 conf->gen.name, newval);
				if (conf->assign_hook)
					conf->assign_hook(newval, extra);
				*conf->variable = conf->reset_val = newval;
				conf->gen.extra = conf->reset_extra = extra;
				break;
			}
		case PGC_OID:
			{
				struct config_oid *conf = (struct config_oid *) gconf;
				Oid			newval = conf->boot_val;
				void	   *extra = NULL;

				Assert(newval >= conf->min);
				Assert(newval <= conf->max);
				if (!call_oid_check_hook(conf, &newval, &extra,
										 PGC_S_DEFAULT, LOG))
					elog(FATAL, "failed to initialize %s to %u",
						 conf->gen.name, newval);
				if (conf->assign_hook)
					conf->assign_hook(newval, extra);
				*conf->variable = conf->reset_val = newval;
				conf->gen.extra = conf->reset_extra = extra;
				break;
			}
		case PGC_REAL:
			{
				struct config_real *conf = (struct config_real *) gconf;
				double		newval = conf->boot_val;
				void	   *extra = NULL;

				Assert(newval >= conf->min);
				Assert(newval <= conf->max);
				if (!call_real_check_hook(conf, &newval, &extra,
										  PGC_S_DEFAULT, LOG))
					elog(FATAL, "failed to initialize %s to %g",
						 conf->gen.name, newval);
				if (conf->assign_hook)
					conf->assign_hook(newval, extra);
				*conf->variable = conf->reset_val = newval;
				conf->gen.extra = conf->reset_extra = extra;
				break;
			}
		case PGC_STRING:
			{
				struct config_string *conf = (struct config_string *) gconf;
				char	   *newval;
				void	   *extra = NULL;

				/* non-NULL boot_val must always get strdup'd */
				if (conf->boot_val != NULL)
					newval = guc_strdup(FATAL, conf->boot_val);
				else
					newval = NULL;

				if (!call_string_check_hook(conf, &newval, &extra,
											PGC_S_DEFAULT, LOG))
					elog(FATAL, "failed to initialize %s to \"%s\"",
						 conf->gen.name, newval ? newval : "");
				if (conf->assign_hook)
					conf->assign_hook(newval, extra);
				*conf->variable = conf->reset_val = newval;
				conf->gen.extra = conf->reset_extra = extra;
				break;
			}
		case PGC_ENUM:
			{
				struct config_enum *conf = (struct config_enum *) gconf;
				int			newval = conf->boot_val;
				void	   *extra = NULL;

				if (!call_enum_check_hook(conf, &newval, &extra,
										  PGC_S_DEFAULT, LOG))
					elog(FATAL, "failed to initialize %s to %d",
						 conf->gen.name, newval);
				if (conf->assign_hook)
					conf->assign_hook(newval, extra);
				*conf->variable = conf->reset_val = newval;
				conf->gen.extra = conf->reset_extra = extra;
				break;
			}
	}
}


/*
 * Select the configuration files and data directory to be used, and
 * do the initial read of postgresql.conf.
 *
 * This is called after processing command-line switches.
 *		userDoption is the -D switch value if any (NULL if unspecified).
 *		progname is just for use in error messages.
 *
 * Returns true on success; on failure, prints a suitable error message
 * to stderr and returns false.
 */
bool
SelectConfigFiles(const char *userDoption, const char *progname)
{
	char	   *configdir;
	char	   *fname;
	struct stat stat_buf;

	/* configdir is -D option, or $PGDATA if no -D */
	if (userDoption)
		configdir = make_absolute_path(userDoption);
	else
		configdir = make_absolute_path(getenv("PGDATA"));

	if (configdir && stat(configdir, &stat_buf) != 0)
	{
		write_stderr("%s: could not access directory \"%s\": %s\n",
					 progname,
					 configdir,
					 strerror(errno));
		if (errno == ENOENT)
			write_stderr("Run initdb or pg_basebackup to initialize a PostgreSQL data directory.\n");
		return false;
	}

	/*
	 * Find the configuration file: if config_file was specified on the
	 * command line, use it, else use configdir/postgresql.conf.  In any case
	 * ensure the result is an absolute path, so that it will be interpreted
	 * the same way by future backends.
	 */
	if (ConfigFileName)
		fname = make_absolute_path(ConfigFileName);
	else if (configdir)
	{
		fname = guc_malloc(FATAL,
						   strlen(configdir) + strlen(CONFIG_FILENAME) + 2);
		sprintf(fname, "%s/%s", configdir, CONFIG_FILENAME);
	}
	else
	{
		write_stderr("%s does not know where to find the server configuration file.\n"
					 "You must specify the --config-file or -D invocation "
					 "option or set the PGDATA environment variable.\n",
					 progname);
		return false;
	}

	/*
	 * Set the ConfigFileName GUC variable to its final value, ensuring that
	 * it can't be overridden later.
	 */
	SetConfigOption("config_file", fname, PGC_POSTMASTER, PGC_S_OVERRIDE);
	free(fname);

	/*
	 * Now read the config file for the first time.
	 */
	if (stat(ConfigFileName, &stat_buf) != 0)
	{
		write_stderr("%s: could not access the server configuration file \"%s\": %s\n",
					 progname, ConfigFileName, strerror(errno));
		free(configdir);
		return false;
	}

	/*
	 * Read the configuration file for the first time.  This time only the
	 * data_directory parameter is picked up to determine the data directory,
	 * so that we can read the PG_AUTOCONF_FILENAME file next time.
	 */
	ProcessConfigFile(PGC_POSTMASTER);

	/*
	 * If the data_directory GUC variable has been set, use that as DataDir;
	 * otherwise use configdir if set; else punt.
	 *
	 * Note: SetDataDir will copy and absolute-ize its argument, so we don't
	 * have to.
	 */
	if (data_directory)
		SetDataDir(data_directory);
	else if (configdir)
		SetDataDir(configdir);
	else
	{
		write_stderr("%s does not know where to find the database system data.\n"
					 "This can be specified as \"data_directory\" in \"%s\", "
					 "or by the -D invocation option, or by the "
					 "PGDATA environment variable.\n",
					 progname, ConfigFileName);
		return false;
	}

	/*
	 * Reflect the final DataDir value back into the data_directory GUC var.
	 * (If you are wondering why we don't just make them a single variable,
	 * it's because the EXEC_BACKEND case needs DataDir to be transmitted to
	 * child backends specially.  XXX is that still true?  Given that we now
	 * chdir to DataDir, EXEC_BACKEND can read the config file without knowing
	 * DataDir in advance.)
	 */
	SetConfigOption("data_directory", DataDir, PGC_POSTMASTER, PGC_S_OVERRIDE);

	/*
	 * Now read the config file a second time, allowing any settings in the
	 * PG_AUTOCONF_FILENAME file to take effect.  (This is pretty ugly, but
	 * since we have to determine the DataDir before we can find the autoconf
	 * file, the alternatives seem worse.)
	 */
	ProcessConfigFile(PGC_POSTMASTER);

	/*
	 * If timezone_abbreviations wasn't set in the configuration file, install
	 * the default value.  We do it this way because we can't safely install a
	 * "real" value until my_exec_path is set, which may not have happened
	 * when InitializeGUCOptions runs, so the bootstrap default value cannot
	 * be the real desired default.
	 */
	pg_timezone_abbrev_initialize();

	/*
	 * Figure out where pg_hba.conf is, and make sure the path is absolute.
	 */
	if (HbaFileName)
		fname = make_absolute_path(HbaFileName);
	else if (configdir)
	{
		fname = guc_malloc(FATAL,
						   strlen(configdir) + strlen(HBA_FILENAME) + 2);
		sprintf(fname, "%s/%s", configdir, HBA_FILENAME);
	}
	else
	{
		write_stderr("%s does not know where to find the \"hba\" configuration file.\n"
					 "This can be specified as \"hba_file\" in \"%s\", "
					 "or by the -D invocation option, or by the "
					 "PGDATA environment variable.\n",
					 progname, ConfigFileName);
		return false;
	}
	SetConfigOption("hba_file", fname, PGC_POSTMASTER, PGC_S_OVERRIDE);
	free(fname);

	/*
	 * Likewise for pg_ident.conf.
	 */
	if (IdentFileName)
		fname = make_absolute_path(IdentFileName);
	else if (configdir)
	{
		fname = guc_malloc(FATAL,
						   strlen(configdir) + strlen(IDENT_FILENAME) + 2);
		sprintf(fname, "%s/%s", configdir, IDENT_FILENAME);
	}
	else
	{
		write_stderr("%s does not know where to find the \"ident\" configuration file.\n"
					 "This can be specified as \"ident_file\" in \"%s\", "
					 "or by the -D invocation option, or by the "
					 "PGDATA environment variable.\n",
					 progname, ConfigFileName);
		return false;
	}
	SetConfigOption("ident_file", fname, PGC_POSTMASTER, PGC_S_OVERRIDE);
	free(fname);

	free(configdir);

	return true;
}


/*
 * Reset all options to their saved default values (implements RESET ALL)
 */
void
ResetAllOptions(void)
{
	int			i;

	for (i = 0; i < num_guc_variables; i++)
	{
		struct config_generic *gconf = guc_variables[i];

		/* Don't reset non-SET-able values */
		if (gconf->context != PGC_SUSET &&
			gconf->context != PGC_USERSET)
			continue;
		/* Don't reset if special exclusion from RESET ALL */
		if (gconf->flags & GUC_NO_RESET_ALL)
			continue;
		/* No need to reset if wasn't SET */
		if (gconf->source <= PGC_S_OVERRIDE)
			continue;

		/* Save old value to support transaction abort */
		push_old_value(gconf, GUC_ACTION_SET);

		switch (gconf->vartype)
		{
			case PGC_BOOL:
				{
					struct config_bool *conf = (struct config_bool *) gconf;

					if (conf->assign_hook)
						conf->assign_hook(conf->reset_val,
										  conf->reset_extra);
					*conf->variable = conf->reset_val;
					set_extra_field(&conf->gen, &conf->gen.extra,
									conf->reset_extra);
					break;
				}
			case PGC_INT:
				{
					struct config_int *conf = (struct config_int *) gconf;

					if (conf->assign_hook)
						conf->assign_hook(conf->reset_val,
										  conf->reset_extra);
					*conf->variable = conf->reset_val;
					set_extra_field(&conf->gen, &conf->gen.extra,
									conf->reset_extra);
					break;
				}
			case PGC_OID:
				{
					struct config_oid *conf = (struct config_oid *) gconf;

					if (conf->assign_hook)
						conf->assign_hook(conf->reset_val,
										  conf->reset_extra);
					*conf->variable = conf->reset_val;
					set_extra_field(&conf->gen, &conf->gen.extra,
									conf->reset_extra);
					break;
				}
			case PGC_REAL:
				{
					struct config_real *conf = (struct config_real *) gconf;

					if (conf->assign_hook)
						conf->assign_hook(conf->reset_val,
										  conf->reset_extra);
					*conf->variable = conf->reset_val;
					set_extra_field(&conf->gen, &conf->gen.extra,
									conf->reset_extra);
					break;
				}
			case PGC_STRING:
				{
					struct config_string *conf = (struct config_string *) gconf;

					if (conf->assign_hook)
						conf->assign_hook(conf->reset_val,
										  conf->reset_extra);
					set_string_field(conf, conf->variable, conf->reset_val);
					set_extra_field(&conf->gen, &conf->gen.extra,
									conf->reset_extra);
					break;
				}
			case PGC_ENUM:
				{
					struct config_enum *conf = (struct config_enum *) gconf;

					if (conf->assign_hook)
						conf->assign_hook(conf->reset_val,
										  conf->reset_extra);
					*conf->variable = conf->reset_val;
					set_extra_field(&conf->gen, &conf->gen.extra,
									conf->reset_extra);
					break;
				}
		}

		gconf->source = gconf->reset_source;
		gconf->scontext = gconf->reset_scontext;
		gconf->srole = gconf->reset_srole;

		if ((gconf->flags & GUC_REPORT && !YbIsClientYsqlConnMgr()) ||
			(YbIsClientYsqlConnMgr() && gconf->context > PGC_BACKEND))
		{
			gconf->status |= GUC_NEEDS_REPORT;
			report_needed = true;
		}
	}
}


/*
 * push_old_value
 *		Push previous state during transactional assignment to a GUC variable.
 */
static void
push_old_value(struct config_generic *gconf, GucAction action)
{
	GucStack   *stack;

	/* If we're not inside a nest level, do nothing */
	if (GUCNestLevel == 0)
		return;

	/* Do we already have a stack entry of the current nest level? */
	stack = gconf->stack;
	if (stack && stack->nest_level >= GUCNestLevel)
	{
		/* Yes, so adjust its state if necessary */
		Assert(stack->nest_level == GUCNestLevel);
		switch (action)
		{
			case GUC_ACTION_SET:
				/* SET overrides any prior action at same nest level */
				if (stack->state == GUC_SET_LOCAL)
				{
					/* must discard old masked value */
					discard_stack_value(gconf, &stack->masked);
				}
				stack->state = GUC_SET;
				break;
			case GUC_ACTION_LOCAL:
				if (stack->state == GUC_SET)
				{
					/* SET followed by SET LOCAL, remember SET's value */
					stack->masked_scontext = gconf->scontext;
					stack->masked_srole = gconf->srole;
					set_stack_value(gconf, &stack->masked);
					stack->state = GUC_SET_LOCAL;
				}
				/* in all other cases, no change to stack entry */
				break;
			case GUC_ACTION_SAVE:
				/* Could only have a prior SAVE of same variable */
				Assert(stack->state == GUC_SAVE);
				break;
		}
		Assert(guc_dirty);		/* must be set already */
		return;
	}

	/*
	 * Push a new stack entry
	 *
	 * We keep all the stack entries in TopTransactionContext for simplicity.
	 */
	stack = (GucStack *) MemoryContextAllocZero(TopTransactionContext,
												sizeof(GucStack));

	stack->prev = gconf->stack;
	stack->nest_level = GUCNestLevel;
	switch (action)
	{
		case GUC_ACTION_SET:
			stack->state = GUC_SET;
			break;
		case GUC_ACTION_LOCAL:
			stack->state = GUC_LOCAL;
			break;
		case GUC_ACTION_SAVE:
			stack->state = GUC_SAVE;
			break;
	}
	stack->source = gconf->source;
	stack->scontext = gconf->scontext;
	stack->srole = gconf->srole;
	set_stack_value(gconf, &stack->prior);

	gconf->stack = stack;

	/* Ensure we remember to pop at end of xact */
	guc_dirty = true;
}


/*
 * Do GUC processing at main transaction start.
 */
void
AtStart_GUC(void)
{
	/*
	 * The nest level should be 0 between transactions; if it isn't, somebody
	 * didn't call AtEOXact_GUC, or called it with the wrong nestLevel.  We
	 * throw a warning but make no other effort to clean up.
	 */
	if (GUCNestLevel != 0)
		elog(WARNING, "GUC nest level = %d at transaction start",
			 GUCNestLevel);
	GUCNestLevel = 1;
}

/*
 * Enter a new nesting level for GUC values.  This is called at subtransaction
 * start, and when entering a function that has proconfig settings, and in
 * some other places where we want to set GUC variables transiently.
 * NOTE we must not risk error here, else subtransaction start will be unhappy.
 */
int
NewGUCNestLevel(void)
{
	return ++GUCNestLevel;
}

/*
 * Do GUC processing at transaction or subtransaction commit or abort, or
 * when exiting a function that has proconfig settings, or when undoing a
 * transient assignment to some GUC variables.  (The name is thus a bit of
 * a misnomer; perhaps it should be ExitGUCNestLevel or some such.)
 * During abort, we discard all GUC settings that were applied at nesting
 * levels >= nestLevel.  nestLevel == 1 corresponds to the main transaction.
 */
void
AtEOXact_GUC(bool isCommit, int nestLevel)
{
	bool		still_dirty;
	int			i;

	/*
	 * Note: it's possible to get here with GUCNestLevel == nestLevel-1 during
	 * abort, if there is a failure during transaction start before
	 * AtStart_GUC is called.
	 */
	Assert(nestLevel > 0 &&
		   (nestLevel <= GUCNestLevel ||
			(nestLevel == GUCNestLevel + 1 && !isCommit)));

	/* Quick exit if nothing's changed in this transaction */
	if (!guc_dirty)
	{
		GUCNestLevel = nestLevel - 1;
		return;
	}

	still_dirty = false;
	for (i = 0; i < num_guc_variables; i++)
	{
		struct config_generic *gconf = guc_variables[i];
		GucStack   *stack;

		/*
		 * Process and pop each stack entry within the nest level. To simplify
		 * fmgr_security_definer() and other places that use GUC_ACTION_SAVE,
		 * we allow failure exit from code that uses a local nest level to be
		 * recovered at the surrounding transaction or subtransaction abort;
		 * so there could be more than one stack entry to pop.
		 */
		while ((stack = gconf->stack) != NULL &&
			   stack->nest_level >= nestLevel)
		{
			GucStack   *prev = stack->prev;
			bool		restorePrior = false;
			bool		restoreMasked = false;
			bool		changed;

			/*
			 * In this next bit, if we don't set either restorePrior or
			 * restoreMasked, we must "discard" any unwanted fields of the
			 * stack entries to avoid leaking memory.  If we do set one of
			 * those flags, unused fields will be cleaned up after restoring.
			 */
			if (!isCommit)		/* if abort, always restore prior value */
				restorePrior = true;
			else if (stack->state == GUC_SAVE)
				restorePrior = true;
			else if (stack->nest_level == 1)
			{
				/* transaction commit */
				if (stack->state == GUC_SET_LOCAL)
					restoreMasked = true;
				else if (stack->state == GUC_SET)
				{
					/* we keep the current active value */
					discard_stack_value(gconf, &stack->prior);
				}
				else			/* must be GUC_LOCAL */
					restorePrior = true;
			}
			else if (prev == NULL ||
					 prev->nest_level < stack->nest_level - 1)
			{
				/* decrement entry's level and do not pop it */
				stack->nest_level--;
				continue;
			}
			else
			{
				/*
				 * We have to merge this stack entry into prev. See README for
				 * discussion of this bit.
				 */
				switch (stack->state)
				{
					case GUC_SAVE:
						Assert(false);	/* can't get here */
						break;

					case GUC_SET:
						/* next level always becomes SET */
						discard_stack_value(gconf, &stack->prior);
						if (prev->state == GUC_SET_LOCAL)
							discard_stack_value(gconf, &prev->masked);
						prev->state = GUC_SET;
						break;

					case GUC_LOCAL:
						if (prev->state == GUC_SET)
						{
							/* LOCAL migrates down */
							prev->masked_scontext = stack->scontext;
							prev->masked_srole = stack->srole;
							prev->masked = stack->prior;
							prev->state = GUC_SET_LOCAL;
						}
						else
						{
							/* else just forget this stack level */
							discard_stack_value(gconf, &stack->prior);
						}
						break;

					case GUC_SET_LOCAL:
						/* prior state at this level no longer wanted */
						discard_stack_value(gconf, &stack->prior);
						/* copy down the masked state */
						prev->masked_scontext = stack->masked_scontext;
						prev->masked_srole = stack->masked_srole;
						if (prev->state == GUC_SET_LOCAL)
							discard_stack_value(gconf, &prev->masked);
						prev->masked = stack->masked;
						prev->state = GUC_SET_LOCAL;
						break;
				}
			}

			changed = false;

			if (restorePrior || restoreMasked)
			{
				/* Perform appropriate restoration of the stacked value */
				config_var_value newvalue;
				GucSource	newsource;
				GucContext	newscontext;
				Oid			newsrole;

				if (restoreMasked)
				{
					newvalue = stack->masked;
					newsource = PGC_S_SESSION;
					newscontext = stack->masked_scontext;
					newsrole = stack->masked_srole;
				}
				else
				{
					newvalue = stack->prior;
					newsource = stack->source;
					newscontext = stack->scontext;
					newsrole = stack->srole;
				}

				switch (gconf->vartype)
				{
					case PGC_BOOL:
						{
							struct config_bool *conf = (struct config_bool *) gconf;
							bool		newval = newvalue.val.boolval;
							void	   *newextra = newvalue.extra;

							if (*conf->variable != newval ||
								conf->gen.extra != newextra)
							{
								if (conf->assign_hook)
									conf->assign_hook(newval, newextra);
								*conf->variable = newval;
								set_extra_field(&conf->gen, &conf->gen.extra,
												newextra);
								changed = true;
							}
							break;
						}
					case PGC_INT:
						{
							struct config_int *conf = (struct config_int *) gconf;
							int			newval = newvalue.val.intval;
							void	   *newextra = newvalue.extra;

							if (*conf->variable != newval ||
								conf->gen.extra != newextra)
							{
								if (conf->assign_hook)
									conf->assign_hook(newval, newextra);
								*conf->variable = newval;
								set_extra_field(&conf->gen, &conf->gen.extra,
												newextra);
								changed = true;
							}
							break;
						}
					case PGC_OID:
						{
							struct config_oid *conf = (struct config_oid *) gconf;
							Oid			newval = newvalue.val.oidval;
							void	   *newextra = newvalue.extra;

							if (*conf->variable != newval ||
								conf->gen.extra != newextra)
							{
								if (conf->assign_hook)
									conf->assign_hook(newval, newextra);
								*conf->variable = newval;
								set_extra_field(&conf->gen, &conf->gen.extra,
												newextra);
								changed = true;
							}
							break;
						}
					case PGC_REAL:
						{
							struct config_real *conf = (struct config_real *) gconf;
							double		newval = newvalue.val.realval;
							void	   *newextra = newvalue.extra;

							if (*conf->variable != newval ||
								conf->gen.extra != newextra)
							{
								if (conf->assign_hook)
									conf->assign_hook(newval, newextra);
								*conf->variable = newval;
								set_extra_field(&conf->gen, &conf->gen.extra,
												newextra);
								changed = true;
							}
							break;
						}
					case PGC_STRING:
						{
							struct config_string *conf = (struct config_string *) gconf;
							char	   *newval = newvalue.val.stringval;
							void	   *newextra = newvalue.extra;

							if (*conf->variable != newval ||
								conf->gen.extra != newextra)
							{
								if (conf->assign_hook)
									conf->assign_hook(newval, newextra);
								set_string_field(conf, conf->variable, newval);
								set_extra_field(&conf->gen, &conf->gen.extra,
												newextra);
								changed = true;
								if (conf->gen.flags & GUC_YB_CUSTOM_STICKY)
								{
									elog(LOG, "Making connection sticky for %s",
										conf->gen.name);
									yb_ysql_conn_mgr_sticky_guc = true;
								}
							}

							/*
							 * Release stacked values if not used anymore. We
							 * could use discard_stack_value() here, but since
							 * we have type-specific code anyway, might as
							 * well inline it.
							 */
							set_string_field(conf, &stack->prior.val.stringval, NULL);
							set_string_field(conf, &stack->masked.val.stringval, NULL);
							break;
						}
					case PGC_ENUM:
						{
							struct config_enum *conf = (struct config_enum *) gconf;
							int			newval = newvalue.val.enumval;
							void	   *newextra = newvalue.extra;

							if (*conf->variable != newval ||
								conf->gen.extra != newextra)
							{
								if (conf->assign_hook)
									conf->assign_hook(newval, newextra);
								*conf->variable = newval;
								set_extra_field(&conf->gen, &conf->gen.extra,
												newextra);
								changed = true;
							}
							break;
						}
				}

				/*
				 * Release stacked extra values if not used anymore.
				 */
				set_extra_field(gconf, &(stack->prior.extra), NULL);
				set_extra_field(gconf, &(stack->masked.extra), NULL);

				/* And restore source information */
				gconf->source = newsource;
				gconf->scontext = newscontext;
				gconf->srole = newsrole;
			}

			/* Finish popping the state stack */
			gconf->stack = prev;
			pfree(stack);

			/* Report new value if we changed it */
			if (changed &&
				((gconf->flags & GUC_REPORT && !YbIsClientYsqlConnMgr()) ||
				(YbIsClientYsqlConnMgr()  && gconf->context > PGC_BACKEND)))
			{
				gconf->status |= GUC_NEEDS_REPORT;
				report_needed = true;
			}
		}						/* end of stack-popping loop */

		if (stack != NULL)
			still_dirty = true;
	}

	/* If there are no remaining stack entries, we can reset guc_dirty */
	guc_dirty = still_dirty;

	/* Update nesting level */
	GUCNestLevel = nestLevel - 1;
}


/*
 * Start up automatic reporting of changes to variables marked GUC_REPORT.
 * This is executed at completion of backend startup.
 */
void
BeginReportingGUCOptions(void)
{
	int			i;

	/*
	 * Don't do anything unless talking to an interactive frontend.
	 */
	if (whereToSendOutput != DestRemote)
		return;

	reporting_enabled = true;

	/*
	 * Hack for in_hot_standby: set the GUC value true if appropriate.  This
	 * is kind of an ugly place to do it, but there's few better options.
	 *
	 * (This could be out of date by the time we actually send it, in which
	 * case the next ReportChangedGUCOptions call will send a duplicate
	 * report.)
	 */
	if (RecoveryInProgress())
		SetConfigOption("in_hot_standby", "true",
						PGC_INTERNAL, PGC_S_OVERRIDE);

	/* Transmit initial values of interesting variables */
	for (i = 0; i < num_guc_variables; i++)
	{
		struct config_generic *conf = guc_variables[i];

		if (conf->flags & GUC_REPORT)
			ReportGUCOption(conf);
	}

	report_needed = false;
}

/*
 * ReportChangedGUCOptions: report recently-changed GUC_REPORT variables
 *
 * This is called just before we wait for a new client query.
 *
 * By handling things this way, we ensure that a ParameterStatus message
 * is sent at most once per variable per query, even if the variable
 * changed multiple times within the query.  That's quite possible when
 * using features such as function SET clauses.  Function SET clauses
 * also tend to cause values to change intraquery but eventually revert
 * to their prevailing values; ReportGUCOption is responsible for avoiding
 * redundant reports in such cases.
 */
void
ReportChangedGUCOptions(void)
{
	/* Quick exit if not (yet) enabled */
	if (!reporting_enabled)
		return;

	/*
	 * Since in_hot_standby isn't actually changed by normal GUC actions, we
	 * need a hack to check whether a new value needs to be reported to the
	 * client.  For speed, we rely on the assumption that it can never
	 * transition from false to true.
	 */
	if (in_hot_standby && !RecoveryInProgress())
		SetConfigOption("in_hot_standby", "false",
						PGC_INTERNAL, PGC_S_OVERRIDE);

	/* Quick exit if no values have been changed */
	if (!report_needed)
		return;

	/* Transmit new values of interesting variables */
	for (int i = 0; i < num_guc_variables; i++)
	{
		struct config_generic *conf = guc_variables[i];

		if (((conf->flags & GUC_REPORT) || YbIsClientYsqlConnMgr()) && (conf->status & GUC_NEEDS_REPORT))
			ReportGUCOption(conf);
	}

	report_needed = false;
}

/*
 * ReportGUCOption: if appropriate, transmit option value to frontend
 *
 * We need not transmit the value if it's the same as what we last
 * transmitted.  However, clear the NEEDS_REPORT flag in any case.
 */
static void
ReportGUCOption(struct config_generic *record)
{
	char	   *val = _ShowOption(record, false);

	if (record->last_reported == NULL ||
		strcmp(val, record->last_reported) != 0)
	{
		StringInfoData msgbuf;

		pq_beginmessage(&msgbuf, 'S');
		pq_sendstring(&msgbuf, record->name);
		pq_sendstring(&msgbuf, val);
		pq_endmessage(&msgbuf);

		/*
		 * We need a long-lifespan copy.  If strdup() fails due to OOM, we'll
		 * set last_reported to NULL and thereby possibly make a duplicate
		 * report later.
		 */
		if (record->last_reported)
			free(record->last_reported);
		record->last_reported = strdup(val);

	}

	pfree(val);

	record->status &= ~GUC_NEEDS_REPORT;
}

/*
 * Convert a value from one of the human-friendly units ("kB", "min" etc.)
 * to the given base unit.  'value' and 'unit' are the input value and unit
 * to convert from (there can be trailing spaces in the unit string).
 * The converted value is stored in *base_value.
 * It's caller's responsibility to round off the converted value as necessary
 * and check for out-of-range.
 *
 * Returns true on success, false if the input unit is not recognized.
 */
static bool
convert_to_base_unit(double value, const char *unit,
					 int base_unit, double *base_value)
{
	char		unitstr[MAX_UNIT_LEN + 1];
	int			unitlen;
	const unit_conversion *table;
	int			i;

	/* extract unit string to compare to table entries */
	unitlen = 0;
	while (*unit != '\0' && !isspace((unsigned char) *unit) &&
		   unitlen < MAX_UNIT_LEN)
		unitstr[unitlen++] = *(unit++);
	unitstr[unitlen] = '\0';
	/* allow whitespace after unit */
	while (isspace((unsigned char) *unit))
		unit++;
	if (*unit != '\0')
		return false;			/* unit too long, or garbage after it */

	/* now search the appropriate table */
	if (base_unit & GUC_UNIT_MEMORY)
		table = memory_unit_conversion_table;
	else
		table = time_unit_conversion_table;

	for (i = 0; *table[i].unit; i++)
	{
		if (base_unit == table[i].base_unit &&
			strcmp(unitstr, table[i].unit) == 0)
		{
			double		cvalue = value * table[i].multiplier;

			/*
			 * If the user gave a fractional value such as "30.1GB", round it
			 * off to the nearest multiple of the next smaller unit, if there
			 * is one.
			 */
			if (*table[i + 1].unit &&
				base_unit == table[i + 1].base_unit)
				cvalue = rint(cvalue / table[i + 1].multiplier) *
					table[i + 1].multiplier;

			*base_value = cvalue;
			return true;
		}
	}
	return false;
}

/*
 * Convert an integer value in some base unit to a human-friendly unit.
 *
 * The output unit is chosen so that it's the greatest unit that can represent
 * the value without loss.  For example, if the base unit is GUC_UNIT_KB, 1024
 * is converted to 1 MB, but 1025 is represented as 1025 kB.
 */
static void
convert_int_from_base_unit(int64 base_value, int base_unit,
						   int64 *value, const char **unit)
{
	const unit_conversion *table;
	int			i;

	*unit = NULL;

	if (base_unit & GUC_UNIT_MEMORY)
		table = memory_unit_conversion_table;
	else
		table = time_unit_conversion_table;

	for (i = 0; *table[i].unit; i++)
	{
		if (base_unit == table[i].base_unit)
		{
			/*
			 * Accept the first conversion that divides the value evenly.  We
			 * assume that the conversions for each base unit are ordered from
			 * greatest unit to the smallest!
			 */
			if (table[i].multiplier <= 1.0 ||
				base_value % (int64) table[i].multiplier == 0)
			{
				*value = (int64) rint(base_value / table[i].multiplier);
				*unit = table[i].unit;
				break;
			}
		}
	}

	Assert(*unit != NULL);
}

/*
 * Convert a floating-point value in some base unit to a human-friendly unit.
 *
 * Same as above, except we have to do the math a bit differently, and
 * there's a possibility that we don't find any exact divisor.
 */
static void
convert_real_from_base_unit(double base_value, int base_unit,
							double *value, const char **unit)
{
	const unit_conversion *table;
	int			i;

	*unit = NULL;

	if (base_unit & GUC_UNIT_MEMORY)
		table = memory_unit_conversion_table;
	else
		table = time_unit_conversion_table;

	for (i = 0; *table[i].unit; i++)
	{
		if (base_unit == table[i].base_unit)
		{
			/*
			 * Accept the first conversion that divides the value evenly; or
			 * if there is none, use the smallest (last) target unit.
			 *
			 * What we actually care about here is whether snprintf with "%g"
			 * will print the value as an integer, so the obvious test of
			 * "*value == rint(*value)" is too strict; roundoff error might
			 * make us choose an unreasonably small unit.  As a compromise,
			 * accept a divisor that is within 1e-8 of producing an integer.
			 */
			*value = base_value / table[i].multiplier;
			*unit = table[i].unit;
			if (*value > 0 &&
				fabs((rint(*value) / *value) - 1.0) <= 1e-8)
				break;
		}
	}

	Assert(*unit != NULL);
}

/*
 * Return the name of a GUC's base unit (e.g. "ms") given its flags.
 * Return NULL if the GUC is unitless.
 */
static const char *
get_config_unit_name(int flags)
{
	switch (flags & (GUC_UNIT_MEMORY | GUC_UNIT_TIME))
	{
		case 0:
			return NULL;		/* GUC has no units */
		case GUC_UNIT_BYTE:
			return "B";
		case GUC_UNIT_KB:
			return "kB";
		case GUC_UNIT_MB:
			return "MB";
		case GUC_UNIT_BLOCKS:
			{
				static char bbuf[8];

				/* initialize if first time through */
				if (bbuf[0] == '\0')
					snprintf(bbuf, sizeof(bbuf), "%dkB", BLCKSZ / 1024);
				return bbuf;
			}
		case GUC_UNIT_XBLOCKS:
			{
				static char xbuf[8];

				/* initialize if first time through */
				if (xbuf[0] == '\0')
					snprintf(xbuf, sizeof(xbuf), "%dkB", XLOG_BLCKSZ / 1024);
				return xbuf;
			}
		case GUC_UNIT_MS:
			return "ms";
		case GUC_UNIT_S:
			return "s";
		case GUC_UNIT_MIN:
			return "min";
		default:
			elog(ERROR, "unrecognized GUC units value: %d",
				 flags & (GUC_UNIT_MEMORY | GUC_UNIT_TIME));
			return NULL;
	}
}


/*
 * Try to parse value as an integer.  The accepted formats are the
 * usual decimal, octal, or hexadecimal formats, as well as floating-point
 * formats (which will be rounded to integer after any units conversion).
 * Optionally, the value can be followed by a unit name if "flags" indicates
 * a unit is allowed.
 *
 * If the string parses okay, return true, else false.
 * If okay and result is not NULL, return the value in *result.
 * If not okay and hintmsg is not NULL, *hintmsg is set to a suitable
 * HINT message, or NULL if no hint provided.
 */
bool
parse_int(const char *value, int *result, int flags, const char **hintmsg)
{
	/*
	 * We assume here that double is wide enough to represent any integer
	 * value with adequate precision.
	 */
	double		val;
	char	   *endptr;

	/* To suppress compiler warnings, always set output params */
	if (result)
		*result = 0;
	if (hintmsg)
		*hintmsg = NULL;

	/*
	 * Try to parse as an integer (allowing octal or hex input).  If the
	 * conversion stops at a decimal point or 'e', or overflows, re-parse as
	 * float.  This should work fine as long as we have no unit names starting
	 * with 'e'.  If we ever do, the test could be extended to check for a
	 * sign or digit after 'e', but for now that's unnecessary.
	 */
	errno = 0;
	val = strtol(value, &endptr, 0);
	if (*endptr == '.' || *endptr == 'e' || *endptr == 'E' ||
		errno == ERANGE)
	{
		errno = 0;
		val = strtod(value, &endptr);
	}

	if (endptr == value || errno == ERANGE)
		return false;			/* no HINT for these cases */

	/* reject NaN (infinities will fail range check below) */
	if (isnan(val))
		return false;			/* treat same as syntax error; no HINT */

	/* allow whitespace between number and unit */
	while (isspace((unsigned char) *endptr))
		endptr++;

	/* Handle possible unit */
	if (*endptr != '\0')
	{
		if ((flags & GUC_UNIT) == 0)
			return false;		/* this setting does not accept a unit */

		if (!convert_to_base_unit(val,
								  endptr, (flags & GUC_UNIT),
								  &val))
		{
			/* invalid unit, or garbage after the unit; set hint and fail. */
			if (hintmsg)
			{
				if (flags & GUC_UNIT_MEMORY)
					*hintmsg = memory_units_hint;
				else
					*hintmsg = time_units_hint;
			}
			return false;
		}
	}

	/* Round to int, then check for overflow */
	val = rint(val);

	if (val > INT_MAX || val < INT_MIN)
	{
		if (hintmsg)
			*hintmsg = gettext_noop("Value exceeds integer range.");
		return false;
	}

	if (result)
		*result = (int) val;
	return true;
}

/*
 * Try to parse value as an Oid, only accepts a decimal format.
 *
 * If the string parses okay, return true, else false.
 * If okay and result is not NULL, return the value in *result.
 * If not okay and hintmsg is not NULL, *hintmsg is set to a suitable
 *	HINT message, or NULL if no hint provided.
 *
 * YB note: This is adapted from parse_int, with unit input removed.
 */
bool
parse_oid(const char *value, Oid *result, const char **hintmsg)
{
	int64		val;
	char	   *endptr;

	/* To suppress compiler warnings, always set output params */
	if (result)
		*result = InvalidOid;
	if (hintmsg)
		*hintmsg = NULL;

	/* We assume here that int64 is at least as wide as long */
	errno = 0;
	val = strtol(value, &endptr, 0);

	if (endptr == value)
		return false;			/* no HINT for integer syntax error */

	if (errno == ERANGE || val != (int64) ((Oid) val))
	{
		if (hintmsg)
			*hintmsg = val < 0 ? gettext_noop("Value cannot be negative.")
							   : gettext_noop("Value exceeds Oid range.");
		return false;
	}

	if (result)
		*result = (Oid) val;
	return true;
}


/*
 * Try to parse value as a floating point number in the usual format.
 * Optionally, the value can be followed by a unit name if "flags" indicates
 * a unit is allowed.
 *
 * If the string parses okay, return true, else false.
 * If okay and result is not NULL, return the value in *result.
 * If not okay and hintmsg is not NULL, *hintmsg is set to a suitable
 * HINT message, or NULL if no hint provided.
 */
bool
parse_real(const char *value, double *result, int flags, const char **hintmsg)
{
	double		val;
	char	   *endptr;

	/* To suppress compiler warnings, always set output params */
	if (result)
		*result = 0;
	if (hintmsg)
		*hintmsg = NULL;

	errno = 0;
	val = strtod(value, &endptr);

	if (endptr == value || errno == ERANGE)
		return false;			/* no HINT for these cases */

	/* reject NaN (infinities will fail range checks later) */
	if (isnan(val))
		return false;			/* treat same as syntax error; no HINT */

	/* allow whitespace between number and unit */
	while (isspace((unsigned char) *endptr))
		endptr++;

	/* Handle possible unit */
	if (*endptr != '\0')
	{
		if ((flags & GUC_UNIT) == 0)
			return false;		/* this setting does not accept a unit */

		if (!convert_to_base_unit(val,
								  endptr, (flags & GUC_UNIT),
								  &val))
		{
			/* invalid unit, or garbage after the unit; set hint and fail. */
			if (hintmsg)
			{
				if (flags & GUC_UNIT_MEMORY)
					*hintmsg = memory_units_hint;
				else
					*hintmsg = time_units_hint;
			}
			return false;
		}
	}

	if (result)
		*result = val;
	return true;
}


/*
 * Lookup the name for an enum option with the selected value.
 * Should only ever be called with known-valid values, so throws
 * an elog(ERROR) if the enum option is not found.
 *
 * The returned string is a pointer to static data and not
 * allocated for modification.
 */
const char *
config_enum_lookup_by_value(struct config_enum *record, int val)
{
	const struct config_enum_entry *entry;

	for (entry = record->options; entry && entry->name; entry++)
	{
		if (entry->val == val)
			return entry->name;
	}

	elog(ERROR, "could not find enum option %d for %s",
		 val, record->gen.name);
	return NULL;				/* silence compiler */
}


/*
 * Lookup the value for an enum option with the selected name
 * (case-insensitive).
 * If the enum option is found, sets the retval value and returns
 * true. If it's not found, return false and retval is set to 0.
 */
bool
config_enum_lookup_by_name(struct config_enum *record, const char *value,
						   int *retval)
{
	const struct config_enum_entry *entry;

	for (entry = record->options; entry && entry->name; entry++)
	{
		if (pg_strcasecmp(value, entry->name) == 0)
		{
			*retval = entry->val;
			return true;
		}
	}

	*retval = 0;
	return false;
}


/*
 * Return a list of all available options for an enum, excluding
 * hidden ones, separated by the given separator.
 * If prefix is non-NULL, it is added before the first enum value.
 * If suffix is non-NULL, it is added to the end of the string.
 */
static char *
config_enum_get_options(struct config_enum *record, const char *prefix,
						const char *suffix, const char *separator)
{
	const struct config_enum_entry *entry;
	StringInfoData retstr;
	int			seplen;

	initStringInfo(&retstr);
	appendStringInfoString(&retstr, prefix);

	seplen = strlen(separator);
	for (entry = record->options; entry && entry->name; entry++)
	{
		if (!entry->hidden)
		{
			appendStringInfoString(&retstr, entry->name);
			appendBinaryStringInfo(&retstr, separator, seplen);
		}
	}

	/*
	 * All the entries may have been hidden, leaving the string empty if no
	 * prefix was given. This indicates a broken GUC setup, since there is no
	 * use for an enum without any values, so we just check to make sure we
	 * don't write to invalid memory instead of actually trying to do
	 * something smart with it.
	 */
	if (retstr.len >= seplen)
	{
		/* Replace final separator */
		retstr.data[retstr.len - seplen] = '\0';
		retstr.len -= seplen;
	}

	appendStringInfoString(&retstr, suffix);

	return retstr.data;
}

/*
 * Parse and validate a proposed value for the specified configuration
 * parameter.
 *
 * This does built-in checks (such as range limits for an integer parameter)
 * and also calls any check hook the parameter may have.
 *
 * record: GUC variable's info record
 * name: variable name (should match the record of course)
 * value: proposed value, as a string
 * source: identifies source of value (check hooks may need this)
 * elevel: level to log any error reports at
 * newval: on success, converted parameter value is returned here
 * newextra: on success, receives any "extra" data returned by check hook
 *	(caller must initialize *newextra to NULL)
 *
 * Returns true if OK, false if not (or throws error, if elevel >= ERROR)
 */
static bool
parse_and_validate_value(struct config_generic *record,
						 const char *name, const char *value,
						 GucSource source, int elevel,
						 union config_var_val *newval, void **newextra)
{
	switch (record->vartype)
	{
		case PGC_BOOL:
			{
				struct config_bool *conf = (struct config_bool *) record;

				if (!parse_bool(value, &newval->boolval))
				{
					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("parameter \"%s\" requires a Boolean value",
									name)));
					return false;
				}

				if (!call_bool_check_hook(conf, &newval->boolval, newextra,
										  source, elevel))
					return false;
			}
			break;
		case PGC_INT:
			{
				struct config_int *conf = (struct config_int *) record;
				const char *hintmsg;

				if (!parse_int(value, &newval->intval,
							   conf->gen.flags, &hintmsg))
				{
					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("invalid value for parameter \"%s\": \"%s\"",
									name, value),
							 hintmsg ? errhint("%s", _(hintmsg)) : 0));
					return false;
				}

				if (newval->intval < conf->min || newval->intval > conf->max)
				{
					const char *unit = get_config_unit_name(conf->gen.flags);

					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("%d%s%s is outside the valid range for parameter \"%s\" (%d .. %d)",
									newval->intval,
									unit ? " " : "",
									unit ? unit : "",
									name,
									conf->min, conf->max)));
					return false;
				}

				if (!call_int_check_hook(conf, &newval->intval, newextra,
										 source, elevel))
					return false;
			}
			break;
		case PGC_OID:
			{
				struct config_oid *conf = (struct config_oid *) record;
				const char *hintmsg;

				if (!parse_oid(value, &newval->oidval, &hintmsg))
				{
					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("invalid value for parameter \"%s\": \"%s\"",
									name, value),
							 hintmsg ? errhint("%s", _(hintmsg)) : 0));
					return false;
				}

				if (newval->oidval < conf->min || newval->oidval > conf->max)
				{
					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("%u is outside the valid range for parameter \"%s\" (%d .. %d)",
									newval->oidval, name,
									conf->min, conf->max)));
					return false;
				}

				if (!call_oid_check_hook(conf, &newval->oidval, newextra,
										 source, elevel))
					return false;
			}
			break;
		case PGC_REAL:
			{
				struct config_real *conf = (struct config_real *) record;
				const char *hintmsg;

				if (!parse_real(value, &newval->realval,
								conf->gen.flags, &hintmsg))
				{
					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("invalid value for parameter \"%s\": \"%s\"",
									name, value),
							 hintmsg ? errhint("%s", _(hintmsg)) : 0));
					return false;
				}

				if (newval->realval < conf->min || newval->realval > conf->max)
				{
					const char *unit = get_config_unit_name(conf->gen.flags);

					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("%g%s%s is outside the valid range for parameter \"%s\" (%g .. %g)",
									newval->realval,
									unit ? " " : "",
									unit ? unit : "",
									name,
									conf->min, conf->max)));
					return false;
				}

				if (!call_real_check_hook(conf, &newval->realval, newextra,
										  source, elevel))
					return false;
			}
			break;
		case PGC_STRING:
			{
				struct config_string *conf = (struct config_string *) record;

				/*
				 * The value passed by the caller could be transient, so we
				 * always strdup it.
				 */
				newval->stringval = guc_strdup(elevel, value);
				if (newval->stringval == NULL)
					return false;

				/*
				 * The only built-in "parsing" check we have is to apply
				 * truncation if GUC_IS_NAME.
				 */
				if (conf->gen.flags & GUC_IS_NAME)
					truncate_identifier(newval->stringval,
										strlen(newval->stringval),
										true);

				if (!call_string_check_hook(conf, &newval->stringval, newextra,
											source, elevel))
				{
					free(newval->stringval);
					newval->stringval = NULL;
					return false;
				}
			}
			break;
		case PGC_ENUM:
			{
				struct config_enum *conf = (struct config_enum *) record;

				if (!config_enum_lookup_by_name(conf, value, &newval->enumval))
				{
					char	   *hintmsg;

					hintmsg = config_enum_get_options(conf,
													  "Available values: ",
													  ".", ", ");

					ereport(elevel,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("invalid value for parameter \"%s\": \"%s\"",
									name, value),
							 hintmsg ? errhint("%s", _(hintmsg)) : 0));

					if (hintmsg)
						pfree(hintmsg);
					return false;
				}

				if (!call_enum_check_hook(conf, &newval->enumval, newextra,
										  source, elevel))
					return false;
			}
			break;
	}

	return true;
}


/*
 * set_config_option: sets option `name' to given value.
 *
 * The value should be a string, which will be parsed and converted to
 * the appropriate data type.  The context and source parameters indicate
 * in which context this function is being called, so that it can apply the
 * access restrictions properly.
 *
 * If value is NULL, set the option to its default value (normally the
 * reset_val, but if source == PGC_S_DEFAULT we instead use the boot_val).
 *
 * action indicates whether to set the value globally in the session, locally
 * to the current top transaction, or just for the duration of a function call.
 *
 * If changeVal is false then don't really set the option but do all
 * the checks to see if it would work.
 *
 * elevel should normally be passed as zero, allowing this function to make
 * its standard choice of ereport level.  However some callers need to be
 * able to override that choice; they should pass the ereport level to use.
 *
 * is_reload should be true only when called from read_nondefault_variables()
 * or RestoreGUCState(), where we are trying to load some other process's
 * GUC settings into a new process.
 *
 * Return value:
 *	+1: the value is valid and was successfully applied.
 *	0:	the name or value is invalid (but see below).
 *	-1: the value was not applied because of context, priority, or changeVal.
 *
 * If there is an error (non-existing option, invalid value) then an
 * ereport(ERROR) is thrown *unless* this is called for a source for which
 * we don't want an ERROR (currently, those are defaults, the config file,
 * and per-database or per-user settings, as well as callers who specify
 * a less-than-ERROR elevel).  In those cases we write a suitable error
 * message via ereport() and return 0.
 *
 * See also SetConfigOption for an external interface.
 */
int
set_config_option(const char *name, const char *value,
				  GucContext context, GucSource source,
				  GucAction action, bool changeVal, int elevel,
				  bool is_reload)
{
	Oid			srole;

	/*
	 * Non-interactive sources should be treated as having all privileges,
	 * except for PGC_S_CLIENT.  Note in particular that this is true for
	 * pg_db_role_setting sources (PGC_S_GLOBAL etc): we assume a suitable
	 * privilege check was done when the pg_db_role_setting entry was made.
	 */
	if (source >= PGC_S_INTERACTIVE || source == PGC_S_CLIENT)
		srole = GetUserId();
	else
		srole = BOOTSTRAP_SUPERUSERID;

	return set_config_option_ext(name, value,
								 context, source, srole,
								 action, changeVal, elevel,
								 is_reload);
}

/*
 * set_config_option_ext: sets option `name' to given value.
 *
 * This API adds the ability to explicitly specify which role OID
 * is considered to be setting the value.  Most external callers can use
 * set_config_option() and let it determine that based on the GucSource,
 * but there are a few that are supplying a value that was determined
 * in some special way and need to override the decision.  Also, when
 * restoring a previously-assigned value, it's important to supply the
 * same role OID that set the value originally; so all guc.c callers
 * that are doing that type of thing need to call this directly.
 *
 * Generally, srole should be GetUserId() when the source is a SQL operation,
 * or BOOTSTRAP_SUPERUSERID if the source is a config file or similar.
 */
int
set_config_option_ext(const char *name, const char *value,
					  GucContext context, GucSource source, Oid srole,
					  GucAction action, bool changeVal, int elevel,
					  bool is_reload)
{
	struct config_generic *record;
	union config_var_val newval_union;
	void	   *newextra = NULL;
	bool		prohibitValueChange = false;
	bool		makeDefault;

	if (source == YSQL_CONN_MGR)
		Assert(YbIsClientYsqlConnMgr());

	/*
	 * For session_authorization and role, only make the connection sticky if
	 * the value is modified by a client-issued SET statement. We cannot use
	 * the assign hook to do so, as postgres utilizes it at connection startup.
	 */
	if (source == PGC_S_SESSION &&
		YbIsClientYsqlConnMgr() &&
		((strncmp(name, "session_authorization", strlen("session_authorization")) == 0) ||
		(strncmp(name, "role", strlen("role")) == 0)))
		{
			elog(LOG, "Making connection sticky for setting %s", name);
			yb_ysql_conn_mgr_sticky_guc = true;
		}

	if (elevel == 0)
	{
		if (source == PGC_S_DEFAULT || source == PGC_S_FILE)
		{
			/*
			 * To avoid cluttering the log, only the postmaster bleats loudly
			 * about problems with the config file.
			 */
			elevel = IsUnderPostmaster ? DEBUG3 : LOG;
		}
		else if (source == PGC_S_GLOBAL ||
				 source == PGC_S_DATABASE ||
				 source == PGC_S_USER ||
				 source == PGC_S_DATABASE_USER)
			elevel = WARNING;
		else
			elevel = ERROR;
	}

	/*
	 * GUC_ACTION_SAVE changes are acceptable during a parallel operation,
	 * because the current worker will also pop the change.  We're probably
	 * dealing with a function having a proconfig entry.  Only the function's
	 * body should observe the change, and peer workers do not share in the
	 * execution of a function call started by this worker.
	 *
	 * Other changes might need to affect other workers, so forbid them.
	 */
	if (IsInParallelMode() && changeVal && action != GUC_ACTION_SAVE)
		ereport(elevel,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				 errmsg("cannot set parameters during a parallel operation")));

	record = find_option(name, true, false, elevel);
	if (record == NULL)
		return 0;

	/*
	 * Check if the option can be set at this time. See guc.h for the precise
	 * rules.
	 */
	switch (record->context)
	{
		case PGC_INTERNAL:
			if (context != PGC_INTERNAL)
			{
				ereport(elevel,
						(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
						 errmsg("parameter \"%s\" cannot be changed",
								name)));
				return 0;
			}
			break;
		case PGC_POSTMASTER:
			if (context == PGC_SIGHUP)
			{
				/*
				 * We are re-reading a PGC_POSTMASTER variable from
				 * postgresql.conf.  We can't change the setting, so we should
				 * give a warning if the DBA tries to change it.  However,
				 * because of variant formats, canonicalization by check
				 * hooks, etc, we can't just compare the given string directly
				 * to what's stored.  Set a flag to check below after we have
				 * the final storable value.
				 */
				prohibitValueChange = true;
			}
			else if (context != PGC_POSTMASTER)
			{
				ereport(elevel,
						(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
						 errmsg("parameter \"%s\" cannot be changed without restarting the server",
								name)));
				return 0;
			}
			break;
		case PGC_SIGHUP:
			if (context != PGC_SIGHUP && context != PGC_POSTMASTER)
			{
				ereport(elevel,
						(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
						 errmsg("parameter \"%s\" cannot be changed now",
								name)));
				return 0;
			}

			/*
			 * Hmm, the idea of the SIGHUP context is "ought to be global, but
			 * can be changed after postmaster start". But there's nothing
			 * that prevents a crafty administrator from sending SIGHUP
			 * signals to individual backends only.
			 */
			break;
		case PGC_SU_BACKEND:
			if (context == PGC_BACKEND)
			{
				/*
				 * Check whether the requesting user has been granted
				 * privilege to set this GUC.
				 */
				AclResult	aclresult;

				aclresult = pg_parameter_aclcheck(name, srole, ACL_SET);
				if (aclresult != ACLCHECK_OK)
				{
					/* No granted privilege */
					ereport(elevel,
							(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
							 errmsg("permission denied to set parameter \"%s\"",
									name)));
					return 0;
				}
			}
			/* fall through to process the same as PGC_BACKEND */
			switch_fallthrough();
		case PGC_BACKEND:
			if (context == PGC_SIGHUP)
			{
				/*
				 * If a PGC_BACKEND or PGC_SU_BACKEND parameter is changed in
				 * the config file, we want to accept the new value in the
				 * postmaster (whence it will propagate to
				 * subsequently-started backends), but ignore it in existing
				 * backends.  This is a tad klugy, but necessary because we
				 * don't re-read the config file during backend start.
				 *
				 * In EXEC_BACKEND builds, this works differently: we load all
				 * non-default settings from the CONFIG_EXEC_PARAMS file
				 * during backend start.  In that case we must accept
				 * PGC_SIGHUP settings, so as to have the same value as if
				 * we'd forked from the postmaster.  This can also happen when
				 * using RestoreGUCState() within a background worker that
				 * needs to have the same settings as the user backend that
				 * started it. is_reload will be true when either situation
				 * applies.
				 */
				if (IsUnderPostmaster && !is_reload)
					return -1;
			}
			else if (context != PGC_POSTMASTER &&
					 context != PGC_BACKEND &&
					 context != PGC_SU_BACKEND &&
					 source != PGC_S_CLIENT)
			{
				ereport(elevel,
						(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
						 errmsg("parameter \"%s\" cannot be set after connection start",
								name)));
				return 0;
			}
			break;
		case PGC_SUSET:
			if (context == PGC_USERSET || context == PGC_BACKEND)
			{
				/*
				 * Check whether the requesting user has been granted
				 * privilege to set this GUC.
				 */
				AclResult	aclresult;

				aclresult = pg_parameter_aclcheck(name, srole, ACL_SET);
				if (aclresult != ACLCHECK_OK)
				{
					/* No granted privilege */
					ereport(elevel,
							(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
							 errmsg("permission denied to set parameter \"%s\"",
									name)));
					return 0;
				}
			}
			break;
		case PGC_USERSET:
			/* always okay */
			break;
	}

	/*
	 * Disallow changing GUC_NOT_WHILE_SEC_REST values if we are inside a
	 * security restriction context.  We can reject this regardless of the GUC
	 * context or source, mainly because sources that it might be reasonable
	 * to override for won't be seen while inside a function.
	 *
	 * Note: variables marked GUC_NOT_WHILE_SEC_REST should usually be marked
	 * GUC_NO_RESET_ALL as well, because ResetAllOptions() doesn't check this.
	 * An exception might be made if the reset value is assumed to be "safe".
	 *
	 * Note: this flag is currently used for "session_authorization" and
	 * "role".  We need to prohibit changing these inside a local userid
	 * context because when we exit it, GUC won't be notified, leaving things
	 * out of sync.  (This could be fixed by forcing a new GUC nesting level,
	 * but that would change behavior in possibly-undesirable ways.)  Also, we
	 * prohibit changing these in a security-restricted operation because
	 * otherwise RESET could be used to regain the session user's privileges.
	 */
	if (record->flags & GUC_NOT_WHILE_SEC_REST)
	{
		if (InLocalUserIdChange())
		{
			/*
			 * Phrasing of this error message is historical, but it's the most
			 * common case.
			 */
			ereport(elevel,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("cannot set parameter \"%s\" within security-definer function",
							name)));
			return 0;
		}
		if (InSecurityRestrictedOperation())
		{
			ereport(elevel,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("cannot set parameter \"%s\" within security-restricted operation",
							name)));
			return 0;
		}
	}

	/*
	 * Should we set reset/stacked values?	(If so, the behavior is not
	 * transactional.)	This is done either when we get a default value from
	 * the database's/user's/client's default settings or when we reset a
	 * value to its default.
	 */
	makeDefault = changeVal && (source <= PGC_S_OVERRIDE) &&
		((value != NULL) || source == PGC_S_DEFAULT);

	/*
	 * Ignore attempted set if overridden by previously processed setting.
	 * However, if changeVal is false then plow ahead anyway since we are
	 * trying to find out if the value is potentially good, not actually use
	 * it. Also keep going if makeDefault is true, since we may want to set
	 * the reset/stacked values even if we can't set the variable itself.
	 *
	 * If the previous source was YSQL_CONN_MGR and current source is
	 * PGC_S_SESSION, then don't ignore the set attempt.
	 *
	 * Also, YSQL_CONN_MGR is given highest priority.
	 */
	if (record->source > source && record->source != YSQL_CONN_MGR)
	{
		if (changeVal && !makeDefault)
		{
			elog(DEBUG3, "\"%s\": setting ignored because previous source is higher priority",
				 name);
			return -1;
		}
		changeVal = false;
	}

	/*
	 * Evaluate value and set variable.
	 */
	switch (record->vartype)
	{
		case PGC_BOOL:
			{
				struct config_bool *conf = (struct config_bool *) record;

#define newval (newval_union.boolval)

				if (value)
				{
					if (!parse_and_validate_value(record, name, value,
												  source, elevel,
												  &newval_union, &newextra))
						return 0;
				}
				else if (source == PGC_S_DEFAULT)
				{
					newval = conf->boot_val;
					if (!call_bool_check_hook(conf, &newval, &newextra,
											  source, elevel))
						return 0;
				}
				else
				{
					newval = conf->reset_val;
					newextra = conf->reset_extra;
					source = conf->gen.reset_source;
					context = conf->gen.reset_scontext;
					srole = conf->gen.reset_srole;
				}

				if (prohibitValueChange)
				{
					/* Release newextra, unless it's reset_extra */
					if (newextra && !extra_field_used(&conf->gen, newextra))
						free(newextra);

					if (*conf->variable != newval)
					{
						record->status |= GUC_PENDING_RESTART;
						ereport(elevel,
								(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
								 errmsg("parameter \"%s\" cannot be changed without restarting the server",
										name)));
						return 0;
					}
					record->status &= ~GUC_PENDING_RESTART;
					return -1;
				}

				if (changeVal)
				{
					/* Save old value to support transaction abort */
					if (!makeDefault)
						push_old_value(&conf->gen, action);

					if (conf->assign_hook)
						conf->assign_hook(newval, newextra);
					*conf->variable = newval;
					set_extra_field(&conf->gen, &conf->gen.extra,
									newextra);
					conf->gen.source = source;
					conf->gen.scontext = context;
					conf->gen.srole = srole;
				}
				if (makeDefault)
				{
					GucStack   *stack;

					if (conf->gen.reset_source <= source)
					{
						conf->reset_val = newval;
						set_extra_field(&conf->gen, &conf->reset_extra,
										newextra);
						conf->gen.reset_source = source;
						conf->gen.reset_scontext = context;
						conf->gen.reset_srole = srole;
					}
					for (stack = conf->gen.stack; stack; stack = stack->prev)
					{
						if (stack->source <= source)
						{
							stack->prior.val.boolval = newval;
							set_extra_field(&conf->gen, &stack->prior.extra,
											newextra);
							stack->source = source;
							stack->scontext = context;
							stack->srole = srole;
						}
					}
				}

				/* Perhaps we didn't install newextra anywhere */
				if (newextra && !extra_field_used(&conf->gen, newextra))
					free(newextra);
				break;

#undef newval
			}

		case PGC_INT:
			{
				struct config_int *conf = (struct config_int *) record;

#define newval (newval_union.intval)

				if (value)
				{
					if (!parse_and_validate_value(record, name, value,
												  source, elevel,
												  &newval_union, &newextra))
						return 0;
				}
				else if (source == PGC_S_DEFAULT)
				{
					newval = conf->boot_val;
					if (!call_int_check_hook(conf, &newval, &newextra,
											 source, elevel))
						return 0;
				}
				else
				{
					newval = conf->reset_val;
					newextra = conf->reset_extra;
					source = conf->gen.reset_source;
					context = conf->gen.reset_scontext;
					srole = conf->gen.reset_srole;
				}

				if (prohibitValueChange)
				{
					/* Release newextra, unless it's reset_extra */
					if (newextra && !extra_field_used(&conf->gen, newextra))
						free(newextra);

					if (*conf->variable != newval)
					{
						record->status |= GUC_PENDING_RESTART;
						ereport(elevel,
								(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
								 errmsg("parameter \"%s\" cannot be changed without restarting the server",
										name)));
						return 0;
					}
					record->status &= ~GUC_PENDING_RESTART;
					return -1;
				}

				if (changeVal)
				{
					/* Save old value to support transaction abort */
					if (!makeDefault)
						push_old_value(&conf->gen, action);

					if (conf->assign_hook)
						conf->assign_hook(newval, newextra);
					*conf->variable = newval;
					set_extra_field(&conf->gen, &conf->gen.extra,
									newextra);
					conf->gen.source = source;
					conf->gen.scontext = context;
					conf->gen.srole = srole;
				}
				if (makeDefault)
				{
					GucStack   *stack;

					if (conf->gen.reset_source <= source)
					{
						conf->reset_val = newval;
						set_extra_field(&conf->gen, &conf->reset_extra,
										newextra);
						conf->gen.reset_source = source;
						conf->gen.reset_scontext = context;
						conf->gen.reset_srole = srole;
					}
					for (stack = conf->gen.stack; stack; stack = stack->prev)
					{
						if (stack->source <= source)
						{
							stack->prior.val.intval = newval;
							set_extra_field(&conf->gen, &stack->prior.extra,
											newextra);
							stack->source = source;
							stack->scontext = context;
							stack->srole = srole;
						}
					}
				}

				/* Perhaps we didn't install newextra anywhere */
				if (newextra && !extra_field_used(&conf->gen, newextra))
					free(newextra);
				break;

#undef newval
			}

		case PGC_OID:
			{
				struct config_oid *conf = (struct config_oid *) record;

#define newval (newval_union.oidval)

				if (value)
				{
					if (!parse_and_validate_value(record, name, value,
												  source, elevel,
												  &newval_union, &newextra))
						return 0;
				}
				else if (source == PGC_S_DEFAULT)
				{
					newval = conf->boot_val;
					if (!call_oid_check_hook(conf, &newval, &newextra,
											 source, elevel))
						return 0;
				}
				else
				{
					newval = conf->reset_val;
					newextra = conf->reset_extra;
					source = conf->gen.reset_source;
					context = conf->gen.reset_scontext;
				}

				if (prohibitValueChange)
				{
					if (*conf->variable != newval)
					{
						record->status |= GUC_PENDING_RESTART;
						ereport(elevel,
								(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
								 errmsg("parameter \"%s\" cannot be changed without restarting the server",
										name)));
						return 0;
					}
					record->status &= ~GUC_PENDING_RESTART;
					return -1;
				}

				if (changeVal)
				{
					/* Save old value to support transaction abort */
					if (!makeDefault)
						push_old_value(&conf->gen, action);

					if (conf->assign_hook)
						conf->assign_hook(newval, newextra);
					*conf->variable = newval;
					set_extra_field(&conf->gen, &conf->gen.extra,
									newextra);
					conf->gen.source = source;
					conf->gen.scontext = context;
				}
				if (makeDefault)
				{
					GucStack   *stack;

					if (conf->gen.reset_source <= source)
					{
						conf->reset_val = newval;
						set_extra_field(&conf->gen, &conf->reset_extra,
										newextra);
						conf->gen.reset_source = source;
						conf->gen.reset_scontext = context;
					}
					for (stack = conf->gen.stack; stack; stack = stack->prev)
					{
						if (stack->source <= source)
						{
							stack->prior.val.oidval = newval;
							set_extra_field(&conf->gen, &stack->prior.extra,
											newextra);
							stack->source = source;
							stack->scontext = context;
						}
					}
				}

				/* Perhaps we didn't install newextra anywhere */
				if (newextra && !extra_field_used(&conf->gen, newextra))
					free(newextra);
				break;

#undef newval
			}

		case PGC_REAL:
			{
				struct config_real *conf = (struct config_real *) record;

#define newval (newval_union.realval)

				if (value)
				{
					if (!parse_and_validate_value(record, name, value,
												  source, elevel,
												  &newval_union, &newextra))
						return 0;
				}
				else if (source == PGC_S_DEFAULT)
				{
					newval = conf->boot_val;
					if (!call_real_check_hook(conf, &newval, &newextra,
											  source, elevel))
						return 0;
				}
				else
				{
					newval = conf->reset_val;
					newextra = conf->reset_extra;
					source = conf->gen.reset_source;
					context = conf->gen.reset_scontext;
					srole = conf->gen.reset_srole;
				}

				if (prohibitValueChange)
				{
					/* Release newextra, unless it's reset_extra */
					if (newextra && !extra_field_used(&conf->gen, newextra))
						free(newextra);

					if (*conf->variable != newval)
					{
						record->status |= GUC_PENDING_RESTART;
						ereport(elevel,
								(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
								 errmsg("parameter \"%s\" cannot be changed without restarting the server",
										name)));
						return 0;
					}
					record->status &= ~GUC_PENDING_RESTART;
					return -1;
				}

				if (changeVal)
				{
					/* Save old value to support transaction abort */
					if (!makeDefault)
						push_old_value(&conf->gen, action);

					if (conf->assign_hook)
						conf->assign_hook(newval, newextra);
					*conf->variable = newval;
					set_extra_field(&conf->gen, &conf->gen.extra,
									newextra);
					conf->gen.source = source;
					conf->gen.scontext = context;
					conf->gen.srole = srole;
				}
				if (makeDefault)
				{
					GucStack   *stack;

					if (conf->gen.reset_source <= source)
					{
						conf->reset_val = newval;
						set_extra_field(&conf->gen, &conf->reset_extra,
										newextra);
						conf->gen.reset_source = source;
						conf->gen.reset_scontext = context;
						conf->gen.reset_srole = srole;
					}
					for (stack = conf->gen.stack; stack; stack = stack->prev)
					{
						if (stack->source <= source)
						{
							stack->prior.val.realval = newval;
							set_extra_field(&conf->gen, &stack->prior.extra,
											newextra);
							stack->source = source;
							stack->scontext = context;
							stack->srole = srole;
						}
					}
				}

				/* Perhaps we didn't install newextra anywhere */
				if (newextra && !extra_field_used(&conf->gen, newextra))
					free(newextra);
				break;

#undef newval
			}

		case PGC_STRING:
			{
				struct config_string *conf = (struct config_string *) record;

#define newval (newval_union.stringval)

				if (value)
				{
					if (!parse_and_validate_value(record, name, value,
												  source, elevel,
												  &newval_union, &newextra))
						return 0;
				}
				else if (source == PGC_S_DEFAULT)
				{
					/* non-NULL boot_val must always get strdup'd */
					if (conf->boot_val != NULL)
					{
						newval = guc_strdup(elevel, conf->boot_val);
						if (newval == NULL)
							return 0;
					}
					else
						newval = NULL;

					if (!call_string_check_hook(conf, &newval, &newextra,
												source, elevel))
					{
						free(newval);
						return 0;
					}
				}
				else
				{
					/*
					 * strdup not needed, since reset_val is already under
					 * guc.c's control
					 */
					newval = conf->reset_val;
					newextra = conf->reset_extra;
					source = conf->gen.reset_source;
					context = conf->gen.reset_scontext;
					srole = conf->gen.reset_srole;
				}

				if (prohibitValueChange)
				{
					bool		newval_different;

					/* newval shouldn't be NULL, so we're a bit sloppy here */
					newval_different = (*conf->variable == NULL ||
										newval == NULL ||
										strcmp(*conf->variable, newval) != 0);

					/* Release newval, unless it's reset_val */
					if (newval && !string_field_used(conf, newval))
						free(newval);
					/* Release newextra, unless it's reset_extra */
					if (newextra && !extra_field_used(&conf->gen, newextra))
						free(newextra);

					if (newval_different)
					{
						record->status |= GUC_PENDING_RESTART;
						ereport(elevel,
								(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
								 errmsg("parameter \"%s\" cannot be changed without restarting the server",
										name)));
						return 0;
					}
					record->status &= ~GUC_PENDING_RESTART;
					return -1;
				}

				if (changeVal)
				{
					/* Save old value to support transaction abort */
					if (!makeDefault)
						push_old_value(&conf->gen, action);

					if (conf->assign_hook)
						conf->assign_hook(newval, newextra);
					set_string_field(conf, conf->variable, newval);
					set_extra_field(&conf->gen, &conf->gen.extra,
									newextra);
					conf->gen.source = source;
					conf->gen.scontext = context;
					conf->gen.srole = srole;
					if (conf->gen.flags & GUC_YB_CUSTOM_STICKY)
					{
						elog(LOG, "Making connection sticky for setting %s", name);
						yb_ysql_conn_mgr_sticky_guc = true;
					}
				}

				if (makeDefault)
				{
					GucStack   *stack;

					if (conf->gen.reset_source <= source)
					{
						set_string_field(conf, &conf->reset_val, newval);
						set_extra_field(&conf->gen, &conf->reset_extra,
										newextra);
						conf->gen.reset_source = source;
						conf->gen.reset_scontext = context;
						conf->gen.reset_srole = srole;
					}
					for (stack = conf->gen.stack; stack; stack = stack->prev)
					{
						if (stack->source <= source)
						{
							set_string_field(conf, &stack->prior.val.stringval,
											 newval);
							set_extra_field(&conf->gen, &stack->prior.extra,
											newextra);
							stack->source = source;
							stack->scontext = context;
							stack->srole = srole;
						}
					}
				}

				/* Perhaps we didn't install newval anywhere */
				if (newval && !string_field_used(conf, newval))
					free(newval);
				/* Perhaps we didn't install newextra anywhere */
				if (newextra && !extra_field_used(&conf->gen, newextra))
					free(newextra);
				break;

#undef newval
			}

		case PGC_ENUM:
			{
				struct config_enum *conf = (struct config_enum *) record;

#define newval (newval_union.enumval)

				if (value)
				{
					if (!parse_and_validate_value(record, name, value,
												  source, elevel,
												  &newval_union, &newextra))
						return 0;
				}
				else if (source == PGC_S_DEFAULT)
				{
					newval = conf->boot_val;
					if (!call_enum_check_hook(conf, &newval, &newextra,
											  source, elevel))
						return 0;
				}
				else
				{
					newval = conf->reset_val;
					newextra = conf->reset_extra;
					source = conf->gen.reset_source;
					context = conf->gen.reset_scontext;
					srole = conf->gen.reset_srole;
				}

				if (prohibitValueChange)
				{
					/* Release newextra, unless it's reset_extra */
					if (newextra && !extra_field_used(&conf->gen, newextra))
						free(newextra);

					if (*conf->variable != newval)
					{
						record->status |= GUC_PENDING_RESTART;
						ereport(elevel,
								(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
								 errmsg("parameter \"%s\" cannot be changed without restarting the server",
										name)));
						return 0;
					}
					record->status &= ~GUC_PENDING_RESTART;
					return -1;
				}

				if (changeVal)
				{
					/* Save old value to support transaction abort */
					if (!makeDefault)
						push_old_value(&conf->gen, action);

					if (conf->assign_hook)
						conf->assign_hook(newval, newextra);
					*conf->variable = newval;
					set_extra_field(&conf->gen, &conf->gen.extra,
									newextra);
					conf->gen.source = source;
					conf->gen.scontext = context;
					conf->gen.srole = srole;
				}
				if (makeDefault)
				{
					GucStack   *stack;

					if (conf->gen.reset_source <= source)
					{
						conf->reset_val = newval;
						set_extra_field(&conf->gen, &conf->reset_extra,
										newextra);
						conf->gen.reset_source = source;
						conf->gen.reset_scontext = context;
						conf->gen.reset_srole = srole;
					}
					for (stack = conf->gen.stack; stack; stack = stack->prev)
					{
						if (stack->source <= source)
						{
							stack->prior.val.enumval = newval;
							set_extra_field(&conf->gen, &stack->prior.extra,
											newextra);
							stack->source = source;
							stack->scontext = context;
							stack->srole = srole;
						}
					}
				}

				/* Perhaps we didn't install newextra anywhere */
				if (newextra && !extra_field_used(&conf->gen, newextra))
					free(newextra);
				break;

#undef newval
			}
	}

	if (changeVal &&
		((record->flags & GUC_REPORT && !YbIsClientYsqlConnMgr()) ||
		(YbIsClientYsqlConnMgr() &&
		record->context > PGC_BACKEND &&
		!(action & GUC_ACTION_LOCAL))))
	{
		record->status |= GUC_NEEDS_REPORT;
		report_needed = true;
	}

	/*
	 * Session parameter set by any source will be allowed to be stored in the
	 * shared memory. But the context must be a `SET STATEMENT` (i.e. PGC_SUSET
	 * or PGC_USERSET).
	 *
	 * Limitation:
	 * While PGC_INTERNAL, PGC_POSTMASTER and PGC_SIGHUP will be common to all
	 * the users and thus need not be changed. If logical connection uses
	 * PGC_SU_BACKEND, PGC_BACKEND context to set a session parameter, (i.e. via
	 * the use startup packet to set a session parameter) then it won't be
	 * supported.
	 *
	 * TODO (janand) #18884 Support the setting of session parameter via startup
	 * packet.
	 *
	 * TODO (janand) #18885 For RESET command add an identifier,
	 * 		for better memory management in shared memory.
	 *
	 * PGC_SUSET is used in case of a super user use a SET statement.
	 */
	if (changeVal && 			/* Add only if the parameter value is changed */
		source != YSQL_CONN_MGR && /* Don't add the parameter to the changed
									* list, if it is set from YSQL CONN MGR */
		(context == PGC_SUSET || context == PGC_USERSET || /* SET statement */
		 value == NULL))								   /* RESET statement */
	{
		YbAddToChangedSessionParametersList(name);
	}

	return changeVal ? 1 : -1;
}

/*
 * Set the fields for source file and line number the setting came from.
 */
static void
set_config_sourcefile(const char *name, char *sourcefile, int sourceline)
{
	struct config_generic *record;
	int			elevel;

	/*
	 * To avoid cluttering the log, only the postmaster bleats loudly about
	 * problems with the config file.
	 */
	elevel = IsUnderPostmaster ? DEBUG3 : LOG;

	record = find_option(name, true, false, elevel);
	/* should not happen */
	if (record == NULL)
		return;

	sourcefile = guc_strdup(elevel, sourcefile);
	if (record->sourcefile)
		free(record->sourcefile);
	record->sourcefile = sourcefile;
	record->sourceline = sourceline;
}

/*
 * Set a config option to the given value.
 *
 * See also set_config_option; this is just the wrapper to be called from
 * outside GUC.  (This function should be used when possible, because its API
 * is more stable than set_config_option's.)
 *
 * Note: there is no support here for setting source file/line, as it
 * is currently not needed.
 */
void
SetConfigOption(const char *name, const char *value,
				GucContext context, GucSource source)
{
	(void) set_config_option(name, value, context, source,
							 GUC_ACTION_SET, true, 0, false);
}



/*
 * Fetch the current value of the option `name', as a string.
 *
 * If the option doesn't exist, return NULL if missing_ok is true (NOTE that
 * this cannot be distinguished from a string variable with a NULL value!),
 * otherwise throw an ereport and don't return.
 *
 * If restrict_privileged is true, we also enforce that only superusers and
 * members of the pg_read_all_settings role can see GUC_SUPERUSER_ONLY
 * variables.  This should only be passed as true in user-driven calls.
 *
 * The string is *not* allocated for modification and is really only
 * valid until the next call to configuration related functions.
 */
const char *
GetConfigOption(const char *name, bool missing_ok, bool restrict_privileged)
{
	struct config_generic *record;
	static char buffer[256];

	record = find_option(name, false, missing_ok, ERROR);
	if (record == NULL)
		return NULL;
	if (restrict_privileged &&
		(record->flags & GUC_SUPERUSER_ONLY) &&
		!has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_SETTINGS))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser or have privileges of pg_read_all_settings to examine \"%s\"",
						name)));

	switch (record->vartype)
	{
		case PGC_BOOL:
			return *((struct config_bool *) record)->variable ? "on" : "off";

		case PGC_INT:
			snprintf(buffer, sizeof(buffer), "%d",
					 *((struct config_int *) record)->variable);
			return buffer;

		case PGC_OID:
			snprintf(buffer, sizeof(buffer), "%u",
					 *((struct config_oid *) record)->variable);
			return buffer;

		case PGC_REAL:
			snprintf(buffer, sizeof(buffer), "%g",
					 *((struct config_real *) record)->variable);
			return buffer;

		case PGC_STRING:
			return *((struct config_string *) record)->variable;

		case PGC_ENUM:
			return config_enum_lookup_by_value((struct config_enum *) record,
											   *((struct config_enum *) record)->variable);
	}
	return NULL;
}

/*
 * Get the RESET value associated with the given option.
 *
 * Note: this is not re-entrant, due to use of static result buffer;
 * not to mention that a string variable could have its reset_val changed.
 * Beware of assuming the result value is good for very long.
 */
const char *
GetConfigOptionResetString(const char *name)
{
	struct config_generic *record;
	static char buffer[256];

	record = find_option(name, false, false, ERROR);
	Assert(record != NULL);
	if ((record->flags & GUC_SUPERUSER_ONLY) &&
		!has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_SETTINGS))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser or have privileges of pg_read_all_settings to examine \"%s\"",
						name)));

	switch (record->vartype)
	{
		case PGC_BOOL:
			return ((struct config_bool *) record)->reset_val ? "on" : "off";

		case PGC_INT:
			snprintf(buffer, sizeof(buffer), "%d",
					 ((struct config_int *) record)->reset_val);
			return buffer;

		case PGC_OID:
			snprintf(buffer, sizeof(buffer), "%u",
					 ((struct config_oid *) record)->reset_val);
			return buffer;

		case PGC_REAL:
			snprintf(buffer, sizeof(buffer), "%g",
					 ((struct config_real *) record)->reset_val);
			return buffer;

		case PGC_STRING:
			return ((struct config_string *) record)->reset_val;

		case PGC_ENUM:
			return config_enum_lookup_by_value((struct config_enum *) record,
											   ((struct config_enum *) record)->reset_val);
	}
	return NULL;
}

/*
 * Get the GUC flags associated with the given option.
 *
 * If the option doesn't exist, return 0 if missing_ok is true,
 * otherwise throw an ereport and don't return.
 */
int
GetConfigOptionFlags(const char *name, bool missing_ok)
{
	struct config_generic *record;

	record = find_option(name, false, missing_ok, ERROR);
	if (record == NULL)
		return 0;
	return record->flags;
}


/*
 * flatten_set_variable_args
 *		Given a parsenode List as emitted by the grammar for SET,
 *		convert to the flat string representation used by GUC.
 *
 * We need to be told the name of the variable the args are for, because
 * the flattening rules vary (ugh).
 *
 * The result is NULL if args is NIL (i.e., SET ... TO DEFAULT), otherwise
 * a palloc'd string.
 */
static char *
flatten_set_variable_args(const char *name, List *args)
{
	struct config_generic *record;
	int			flags;
	StringInfoData buf;
	ListCell   *l;

	/* Fast path if just DEFAULT */
	if (args == NIL)
		return NULL;

	/*
	 * Get flags for the variable; if it's not known, use default flags.
	 * (Caller might throw error later, but not our business to do so here.)
	 */
	record = find_option(name, false, true, WARNING);
	if (record)
		flags = record->flags;
	else
		flags = 0;

	/* Complain if list input and non-list variable */
	if ((flags & GUC_LIST_INPUT) == 0 &&
		list_length(args) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("SET %s takes only one argument", name)));

	initStringInfo(&buf);

	/*
	 * Each list member may be a plain A_Const node, or an A_Const within a
	 * TypeCast; the latter case is supported only for ConstInterval arguments
	 * (for SET TIME ZONE).
	 */
	foreach(l, args)
	{
		Node	   *arg = (Node *) lfirst(l);
		char	   *val;
		TypeName   *typeName = NULL;
		A_Const    *con;

		if (l != list_head(args))
			appendStringInfoString(&buf, ", ");

		if (IsA(arg, TypeCast))
		{
			TypeCast   *tc = (TypeCast *) arg;

			arg = tc->arg;
			typeName = tc->typeName;
		}

		if (!IsA(arg, A_Const))
			elog(ERROR, "unrecognized node type: %d", (int) nodeTag(arg));
		con = (A_Const *) arg;

		switch (nodeTag(&con->val))
		{
			case T_Integer:
				appendStringInfo(&buf, "%d", intVal(&con->val));
				break;
			case T_Float:
				/* represented as a string, so just copy it */
				appendStringInfoString(&buf, castNode(Float, &con->val)->fval);
				break;
			case T_String:
				val = strVal(&con->val);
				if (typeName != NULL)
				{
					/*
					 * Must be a ConstInterval argument for TIME ZONE. Coerce
					 * to interval and back to normalize the value and account
					 * for any typmod.
					 */
					Oid			typoid;
					int32		typmod;
					Datum		interval;
					char	   *intervalout;

					typenameTypeIdAndMod(NULL, typeName, &typoid, &typmod);
					Assert(typoid == INTERVALOID);

					interval =
						DirectFunctionCall3(interval_in,
											CStringGetDatum(val),
											ObjectIdGetDatum(InvalidOid),
											Int32GetDatum(typmod));

					intervalout =
						DatumGetCString(DirectFunctionCall1(interval_out,
															interval));
					appendStringInfo(&buf, "INTERVAL '%s'", intervalout);
				}
				else
				{
					/*
					 * Plain string literal or identifier.  For quote mode,
					 * quote it if it's not a vanilla identifier.
					 */
					if (flags & GUC_LIST_QUOTE)
						appendStringInfoString(&buf, quote_identifier(val));
					else
						appendStringInfoString(&buf, val);
				}
				break;
			default:
				elog(ERROR, "unrecognized node type: %d",
					 (int) nodeTag(&con->val));
				break;
		}
	}

	return buf.data;
}

/*
 * Write updated configuration parameter values into a temporary file.
 * This function traverses the list of parameters and quotes the string
 * values before writing them.
 */
static void
write_auto_conf_file(int fd, const char *filename, ConfigVariable *head)
{
	StringInfoData buf;
	ConfigVariable *item;

	initStringInfo(&buf);

	/* Emit file header containing warning comment */
	appendStringInfoString(&buf, "# Do not edit this file manually!\n");
	appendStringInfoString(&buf, "# It will be overwritten by the ALTER SYSTEM command.\n");

	errno = 0;
	if (write(fd, buf.data, buf.len) != buf.len)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write to file \"%s\": %m", filename)));
	}

	/* Emit each parameter, properly quoting the value */
	for (item = head; item != NULL; item = item->next)
	{
		char	   *escaped;

		resetStringInfo(&buf);

		appendStringInfoString(&buf, item->name);
		appendStringInfoString(&buf, " = '");

		escaped = escape_single_quotes_ascii(item->value);
		if (!escaped)
			ereport(ERROR,
					(errcode(ERRCODE_OUT_OF_MEMORY),
					 errmsg("out of memory")));
		appendStringInfoString(&buf, escaped);
		free(escaped);

		appendStringInfoString(&buf, "'\n");

		errno = 0;
		if (write(fd, buf.data, buf.len) != buf.len)
		{
			/* if write didn't set errno, assume problem is no disk space */
			if (errno == 0)
				errno = ENOSPC;
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m", filename)));
		}
	}

	/* fsync before considering the write to be successful */
	if (pg_fsync(fd) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", filename)));

	pfree(buf.data);
}

/*
 * Update the given list of configuration parameters, adding, replacing
 * or deleting the entry for item "name" (delete if "value" == NULL).
 */
static void
replace_auto_config_value(ConfigVariable **head_p, ConfigVariable **tail_p,
						  const char *name, const char *value)
{
	ConfigVariable *item,
			   *next,
			   *prev = NULL;

	/*
	 * Remove any existing match(es) for "name".  Normally there'd be at most
	 * one, but if external tools have modified the config file, there could
	 * be more.
	 */
	for (item = *head_p; item != NULL; item = next)
	{
		next = item->next;
		if (guc_name_compare(item->name, name) == 0)
		{
			/* found a match, delete it */
			if (prev)
				prev->next = next;
			else
				*head_p = next;
			if (next == NULL)
				*tail_p = prev;

			pfree(item->name);
			pfree(item->value);
			pfree(item->filename);
			pfree(item);
		}
		else
			prev = item;
	}

	/* Done if we're trying to delete it */
	if (value == NULL)
		return;

	/* OK, append a new entry */
	item = palloc(sizeof *item);
	item->name = pstrdup(name);
	item->value = pstrdup(value);
	item->errmsg = NULL;
	item->filename = pstrdup("");	/* new item has no location */
	item->sourceline = 0;
	item->ignore = false;
	item->applied = false;
	item->next = NULL;

	if (*head_p == NULL)
		*head_p = item;
	else
		(*tail_p)->next = item;
	*tail_p = item;
}


/*
 * Execute ALTER SYSTEM statement.
 *
 * Read the old PG_AUTOCONF_FILENAME file, merge in the new variable value,
 * and write out an updated file.  If the command is ALTER SYSTEM RESET ALL,
 * we can skip reading the old file and just write an empty file.
 *
 * An LWLock is used to serialize updates of the configuration file.
 *
 * In case of an error, we leave the original automatic
 * configuration file (PG_AUTOCONF_FILENAME) intact.
 */
void
AlterSystemSetConfigFile(AlterSystemStmt *altersysstmt)
{
	char	   *name;
	char	   *value;
	bool		resetall = false;
	ConfigVariable *head = NULL;
	ConfigVariable *tail = NULL;
	volatile int Tmpfd;
	char		AutoConfFileName[MAXPGPATH];
	char		AutoConfTmpFileName[MAXPGPATH];

	/*
	 * Extract statement arguments
	 */
	name = altersysstmt->setstmt->name;

	switch (altersysstmt->setstmt->kind)
	{
		case VAR_SET_VALUE:
			value = ExtractSetVariableArgs(altersysstmt->setstmt);
			break;

		case VAR_SET_DEFAULT:
		case VAR_RESET:
			value = NULL;
			break;

		case VAR_RESET_ALL:
			value = NULL;
			resetall = true;
			break;

		default:
			elog(ERROR, "unrecognized alter system stmt type: %d",
				 altersysstmt->setstmt->kind);
			break;
	}

	/*
	 * Check permission to run ALTER SYSTEM on the target variable
	 */
	if (!superuser())
	{
		if (resetall)
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("permission denied to perform ALTER SYSTEM RESET ALL")));
		else
		{
			AclResult	aclresult;

			aclresult = pg_parameter_aclcheck(name, GetUserId(),
											  ACL_ALTER_SYSTEM);
			if (aclresult != ACLCHECK_OK)
				ereport(ERROR,
						(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
						 errmsg("permission denied to set parameter \"%s\"",
								name)));
		}
	}

	/*
	 * Unless it's RESET_ALL, validate the target variable and value
	 */
	if (!resetall)
	{
		struct config_generic *record;

		record = find_option(name, false, false, ERROR);
		Assert(record != NULL);

		/*
		 * Don't allow parameters that can't be set in configuration files to
		 * be set in PG_AUTOCONF_FILENAME file.
		 */
		if ((record->context == PGC_INTERNAL) ||
			(record->flags & GUC_DISALLOW_IN_FILE) ||
			(record->flags & GUC_DISALLOW_IN_AUTO_FILE))
			ereport(ERROR,
					(errcode(ERRCODE_CANT_CHANGE_RUNTIME_PARAM),
					 errmsg("parameter \"%s\" cannot be changed",
							name)));

		/*
		 * If a value is specified, verify that it's sane.
		 */
		if (value)
		{
			union config_var_val newval;
			void	   *newextra = NULL;

			/* Check that it's acceptable for the indicated parameter */
			if (!parse_and_validate_value(record, name, value,
										  PGC_S_FILE, ERROR,
										  &newval, &newextra))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid value for parameter \"%s\": \"%s\"",
								name, value)));

			if (record->vartype == PGC_STRING && newval.stringval != NULL)
				free(newval.stringval);
			if (newextra)
				free(newextra);

			/*
			 * We must also reject values containing newlines, because the
			 * grammar for config files doesn't support embedded newlines in
			 * string literals.
			 */
			if (strchr(value, '\n'))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("parameter value for ALTER SYSTEM must not contain a newline")));
		}
	}

	/*
	 * PG_AUTOCONF_FILENAME and its corresponding temporary file are always in
	 * the data directory, so we can reference them by simple relative paths.
	 */
	snprintf(AutoConfFileName, sizeof(AutoConfFileName), "%s",
			 PG_AUTOCONF_FILENAME);
	snprintf(AutoConfTmpFileName, sizeof(AutoConfTmpFileName), "%s.%s",
			 AutoConfFileName,
			 "tmp");

	/*
	 * Only one backend is allowed to operate on PG_AUTOCONF_FILENAME at a
	 * time.  Use AutoFileLock to ensure that.  We must hold the lock while
	 * reading the old file contents.
	 */
	LWLockAcquire(AutoFileLock, LW_EXCLUSIVE);

	/*
	 * If we're going to reset everything, then no need to open or parse the
	 * old file.  We'll just write out an empty list.
	 */
	if (!resetall)
	{
		struct stat st;

		if (stat(AutoConfFileName, &st) == 0)
		{
			/* open old file PG_AUTOCONF_FILENAME */
			FILE	   *infile;

			infile = AllocateFile(AutoConfFileName, "r");
			if (infile == NULL)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not open file \"%s\": %m",
								AutoConfFileName)));

			/* parse it */
			if (!ParseConfigFp(infile, AutoConfFileName, 0, LOG, &head, &tail))
				ereport(ERROR,
						(errcode(ERRCODE_CONFIG_FILE_ERROR),
						 errmsg("could not parse contents of file \"%s\"",
								AutoConfFileName)));

			FreeFile(infile);
		}

		/*
		 * Now, replace any existing entry with the new value, or add it if
		 * not present.
		 */
		replace_auto_config_value(&head, &tail, name, value);
	}

	/*
	 * Invoke the post-alter hook for setting this GUC variable.  GUCs
	 * typically do not have corresponding entries in pg_parameter_acl, so we
	 * call the hook using the name rather than a potentially-non-existent
	 * OID.  Nonetheless, we pass ParameterAclRelationId so that this call
	 * context can be distinguished from others.  (Note that "name" will be
	 * NULL in the RESET ALL case.)
	 *
	 * We do this here rather than at the end, because ALTER SYSTEM is not
	 * transactional.  If the hook aborts our transaction, it will be cleaner
	 * to do so before we touch any files.
	 */
	InvokeObjectPostAlterHookArgStr(ParameterAclRelationId, name,
									ACL_ALTER_SYSTEM,
									altersysstmt->setstmt->kind,
									false);

	/*
	 * To ensure crash safety, first write the new file data to a temp file,
	 * then atomically rename it into place.
	 *
	 * If there is a temp file left over due to a previous crash, it's okay to
	 * truncate and reuse it.
	 */
	Tmpfd = BasicOpenFile(AutoConfTmpFileName,
						  O_CREAT | O_RDWR | O_TRUNC);
	if (Tmpfd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m",
						AutoConfTmpFileName)));

	/*
	 * Use a TRY block to clean up the file if we fail.  Since we need a TRY
	 * block anyway, OK to use BasicOpenFile rather than OpenTransientFile.
	 */
	PG_TRY();
	{
		/* Write and sync the new contents to the temporary file */
		write_auto_conf_file(Tmpfd, AutoConfTmpFileName, head);

		/* Close before renaming; may be required on some platforms */
		close(Tmpfd);
		Tmpfd = -1;

		/*
		 * As the rename is atomic operation, if any problem occurs after this
		 * at worst it can lose the parameters set by last ALTER SYSTEM
		 * command.
		 */
		durable_rename(AutoConfTmpFileName, AutoConfFileName, ERROR);
	}
	PG_CATCH();
	{
		/* Close file first, else unlink might fail on some platforms */
		if (Tmpfd >= 0)
			close(Tmpfd);

		/* Unlink, but ignore any error */
		(void) unlink(AutoConfTmpFileName);

		PG_RE_THROW();
	}
	PG_END_TRY();

	FreeConfigVariables(head);

	LWLockRelease(AutoFileLock);
}

/*
 * SET command
 */
void
ExecSetVariableStmt(VariableSetStmt *stmt, bool isTopLevel)
{
	GucAction	action = stmt->is_local ? GUC_ACTION_LOCAL : GUC_ACTION_SET;
	bool 		YbDbAdminCanSet = false;

	if (IsYbDbAdminUser(GetUserId()))
	{
		for (size_t i = 0;
			 i < sizeof(YbDbAdminVariables) / sizeof(YbDbAdminVariables[0]);
			 i++)
		{
			if (stmt->name && strcmp(YbDbAdminVariables[i], stmt->name) == 0)
			{
				YbDbAdminCanSet = true;
				break;
			}
		}
	}

	/*
	 * Workers synchronize these parameters at the start of the parallel
	 * operation; then, we block SET during the operation.
	 */
	if (IsInParallelMode())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TRANSACTION_STATE),
				 errmsg("cannot set parameters during a parallel operation")));

	switch (stmt->kind)
	{
		case VAR_SET_VALUE:
		case VAR_SET_CURRENT:
			if (stmt->is_local)
				WarnNoTransactionBlock(isTopLevel, "SET LOCAL");
			(void) set_config_option(stmt->name,
									 ExtractSetVariableArgs(stmt),
									 (superuser() || YbDbAdminCanSet
									  ? PGC_SUSET : PGC_USERSET),
									 PGC_S_SESSION,
									 action, true, 0, false);
			check_reserved_prefixes(stmt->name);
			break;
		case VAR_SET_MULTI:

			/*
			 * Special-case SQL syntaxes.  The TRANSACTION and SESSION
			 * CHARACTERISTICS cases effectively set more than one variable
			 * per statement.  TRANSACTION SNAPSHOT only takes one argument,
			 * but we put it here anyway since it's a special case and not
			 * related to any GUC variable.
			 */
			if (strcmp(stmt->name, "TRANSACTION") == 0)
			{
				ListCell   *head;

				WarnNoTransactionBlock(isTopLevel, "SET TRANSACTION");

				foreach(head, stmt->args)
				{
					DefElem    *item = (DefElem *) lfirst(head);

					if (strcmp(item->defname, "transaction_isolation") == 0)
						SetPGVariable("transaction_isolation",
									  list_make1(item->arg), stmt->is_local);
					else if (strcmp(item->defname, "transaction_read_only") == 0)
						SetPGVariable("transaction_read_only",
									  list_make1(item->arg), stmt->is_local);
					else if (strcmp(item->defname, "transaction_deferrable") == 0)
						SetPGVariable("transaction_deferrable",
									  list_make1(item->arg), stmt->is_local);
					else
						elog(ERROR, "unexpected SET TRANSACTION element: %s",
							 item->defname);
				}
			}
			else if (strcmp(stmt->name, "SESSION CHARACTERISTICS") == 0)
			{
				ListCell   *head;

				foreach(head, stmt->args)
				{
					DefElem    *item = (DefElem *) lfirst(head);

					if (strcmp(item->defname, "transaction_isolation") == 0)
						SetPGVariable("default_transaction_isolation",
									  list_make1(item->arg), stmt->is_local);
					else if (strcmp(item->defname, "transaction_read_only") == 0)
						SetPGVariable("default_transaction_read_only",
									  list_make1(item->arg), stmt->is_local);
					else if (strcmp(item->defname, "transaction_deferrable") == 0)
						SetPGVariable("default_transaction_deferrable",
									  list_make1(item->arg), stmt->is_local);
					else
						elog(ERROR, "unexpected SET SESSION element: %s",
							 item->defname);
				}
			}
			else if (strcmp(stmt->name, "TRANSACTION SNAPSHOT") == 0)
			{
				A_Const    *con = linitial_node(A_Const, stmt->args);

				if (stmt->is_local)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("SET LOCAL TRANSACTION SNAPSHOT is not implemented")));

				WarnNoTransactionBlock(isTopLevel, "SET TRANSACTION");
				ImportSnapshot(strVal(&con->val));
			}
			else
				elog(ERROR, "unexpected SET MULTI element: %s",
					 stmt->name);
			break;
		case VAR_SET_DEFAULT:
			if (stmt->is_local)
				WarnNoTransactionBlock(isTopLevel, "SET LOCAL");
			switch_fallthrough();
		case VAR_RESET:
			if (strcmp(stmt->name, "transaction_isolation") == 0)
				WarnNoTransactionBlock(isTopLevel, "RESET TRANSACTION");

			(void) set_config_option(stmt->name,
									 NULL,
									 (superuser() || YbDbAdminCanSet
									  ? PGC_SUSET : PGC_USERSET),
									 PGC_S_SESSION,
									 action, true, 0, false);

			check_reserved_prefixes(stmt->name);
			break;
		case VAR_RESET_ALL:
			ResetAllOptions();
			break;
	}

	/* Invoke the post-alter hook for setting this GUC variable, by name. */
	InvokeObjectPostAlterHookArgStr(ParameterAclRelationId, stmt->name,
									ACL_SET, stmt->kind, false);
}

/*
 * Get the value to assign for a VariableSetStmt, or NULL if it's RESET.
 * The result is palloc'd.
 *
 * This is exported for use by actions such as ALTER ROLE SET.
 */
char *
ExtractSetVariableArgs(VariableSetStmt *stmt)
{
	switch (stmt->kind)
	{
		case VAR_SET_VALUE:
			return flatten_set_variable_args(stmt->name, stmt->args);
		case VAR_SET_CURRENT:
			return GetConfigOptionByName(stmt->name, NULL, false);
		default:
			return NULL;
	}
}

/*
 * SetPGVariable - SET command exported as an easily-C-callable function.
 *
 * This provides access to SET TO value, as well as SET TO DEFAULT (expressed
 * by passing args == NIL), but not SET FROM CURRENT functionality.
 */
void
SetPGVariable(const char *name, List *args, bool is_local)
{
	char	   *argstring = flatten_set_variable_args(name, args);

	/* Note SET DEFAULT (argstring == NULL) is equivalent to RESET */
	(void) set_config_option(name,
							 argstring,
							 (superuser() ? PGC_SUSET : PGC_USERSET),
							 PGC_S_SESSION,
							 is_local ? GUC_ACTION_LOCAL : GUC_ACTION_SET,
							 true, 0, false);
}

/*
 * SET command wrapped as a SQL callable function.
 */
Datum
set_config_by_name(PG_FUNCTION_ARGS)
{
	char	   *name;
	char	   *value;
	char	   *new_value;
	bool		is_local;

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("SET requires parameter name")));

	/* Get the GUC variable name */
	name = TextDatumGetCString(PG_GETARG_DATUM(0));

	/* Get the desired value or set to NULL for a reset request */
	if (PG_ARGISNULL(1))
		value = NULL;
	else
		value = TextDatumGetCString(PG_GETARG_DATUM(1));

	/*
	 * Get the desired state of is_local. Default to false if provided value
	 * is NULL
	 */
	if (PG_ARGISNULL(2))
		is_local = false;
	else
		is_local = PG_GETARG_BOOL(2);

	/* Note SET DEFAULT (argstring == NULL) is equivalent to RESET */
	(void) set_config_option(name,
							 value,
							 (superuser() ? PGC_SUSET : PGC_USERSET),
							 PGC_S_SESSION,
							 is_local ? GUC_ACTION_LOCAL : GUC_ACTION_SET,
							 true, 0, false);

	/* get the new current value */
	new_value = GetConfigOptionByName(name, NULL, false);

	/* Convert return string to text */
	PG_RETURN_TEXT_P(cstring_to_text(new_value));
}


/*
 * Common code for DefineCustomXXXVariable subroutines: allocate the
 * new variable's config struct and fill in generic fields.
 */
static struct config_generic *
init_custom_variable(const char *name,
					 const char *short_desc,
					 const char *long_desc,
					 GucContext context,
					 int flags,
					 enum config_type type,
					 size_t sz)
{
	struct config_generic *gen;

	/*
	 * Only allow custom PGC_POSTMASTER variables to be created during shared
	 * library preload; any later than that, we can't ensure that the value
	 * doesn't change after startup.  This is a fatal elog if it happens; just
	 * erroring out isn't safe because we don't know what the calling loadable
	 * module might already have hooked into.
	 */
	if (context == PGC_POSTMASTER &&
		!process_shared_preload_libraries_in_progress)
		elog(FATAL, "cannot create PGC_POSTMASTER variables after startup");

	/*
	 * We can't support custom GUC_LIST_QUOTE variables, because the wrong
	 * things would happen if such a variable were set or pg_dump'd when the
	 * defining extension isn't loaded.  Again, treat this as fatal because
	 * the loadable module may be partly initialized already.
	 */
	if (flags & GUC_LIST_QUOTE)
		elog(FATAL, "extensions cannot define GUC_LIST_QUOTE variables");

	/*
	 * Before pljava commit 398f3b876ed402bdaec8bc804f29e2be95c75139
	 * (2015-12-15), two of that module's PGC_USERSET variables facilitated
	 * trivial escalation to superuser privileges.  Restrict the variables to
	 * protect sites that have yet to upgrade pljava.
	 */
	if (context == PGC_USERSET &&
		(strcmp(name, "pljava.classpath") == 0 ||
		 strcmp(name, "pljava.vmoptions") == 0))
		context = PGC_SUSET;

	gen = (struct config_generic *) guc_malloc(ERROR, sz);
	memset(gen, 0, sz);

	gen->name = guc_strdup(ERROR, name);
	gen->context = context;
	gen->group = CUSTOM_OPTIONS;
	gen->short_desc = short_desc;
	gen->long_desc = long_desc;
	gen->flags = flags;
	gen->vartype = type;

	return gen;
}

/*
 * Common code for DefineCustomXXXVariable subroutines: insert the new
 * variable into the GUC variable array, replacing any placeholder.
 */
static void
define_custom_variable(struct config_generic *variable)
{
	const char *name = variable->name;
#ifdef ADDRESS_SANITIZER
	struct config_generic config_placeholder;
	config_placeholder.name = name;
	const char **nameAddr = &config_placeholder.name;
#else
	const char **nameAddr = &name;
#endif
	struct config_string *pHolder;
	struct config_generic **res;

	/*
	 * See if there's a placeholder by the same name.
	 */
	res = (struct config_generic **) bsearch((void *) &nameAddr,
											 (void *) guc_variables,
											 num_guc_variables,
											 sizeof(struct config_generic *),
											 guc_var_compare);
	if (res == NULL)
	{
		/*
		 * No placeholder to replace, so we can just add it ... but first,
		 * make sure it's initialized to its default value.
		 */
		InitializeOneGUCOption(variable);
		add_guc_variable(variable, ERROR);
		return;
	}

	/*
	 * This better be a placeholder
	 */
	if (((*res)->flags & GUC_CUSTOM_PLACEHOLDER) == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("attempt to redefine parameter \"%s\"", name)));

	Assert((*res)->vartype == PGC_STRING);
	pHolder = (struct config_string *) (*res);

	/*
	 * First, set the variable to its default value.  We must do this even
	 * though we intend to immediately apply a new value, since it's possible
	 * that the new value is invalid.
	 */
	InitializeOneGUCOption(variable);

	/*
	 * Replace the placeholder. We aren't changing the name, so no re-sorting
	 * is necessary
	 */
	*res = variable;

	/*
	 * Assign the string value(s) stored in the placeholder to the real
	 * variable.  Essentially, we need to duplicate all the active and stacked
	 * values, but with appropriate validation and datatype adjustment.
	 *
	 * If an assignment fails, we report a WARNING and keep going.  We don't
	 * want to throw ERROR for bad values, because it'd bollix the add-on
	 * module that's presumably halfway through getting loaded.  In such cases
	 * the default or previous state will become active instead.
	 */

	/* First, apply the reset value if any */
	if (pHolder->reset_val)
		(void) set_config_option_ext(name, pHolder->reset_val,
									 pHolder->gen.reset_scontext,
									 pHolder->gen.reset_source,
									 pHolder->gen.reset_srole,
									 GUC_ACTION_SET, true, WARNING, false);
	/* That should not have resulted in stacking anything */
	Assert(variable->stack == NULL);

	/* Now, apply current and stacked values, in the order they were stacked */
	reapply_stacked_values(variable, pHolder, pHolder->gen.stack,
						   *(pHolder->variable),
						   pHolder->gen.scontext, pHolder->gen.source,
						   pHolder->gen.srole);

	/* Also copy over any saved source-location information */
	if (pHolder->gen.sourcefile)
		set_config_sourcefile(name, pHolder->gen.sourcefile,
							  pHolder->gen.sourceline);

	/*
	 * Free up as much as we conveniently can of the placeholder structure.
	 * (This neglects any stack items, so it's possible for some memory to be
	 * leaked.  Since this can only happen once per session per variable, it
	 * doesn't seem worth spending much code on.)
	 */
	set_string_field(pHolder, pHolder->variable, NULL);
	set_string_field(pHolder, &pHolder->reset_val, NULL);

	free(pHolder);
}

/*
 * Recursive subroutine for define_custom_variable: reapply non-reset values
 *
 * We recurse so that the values are applied in the same order as originally.
 * At each recursion level, apply the upper-level value (passed in) in the
 * fashion implied by the stack entry.
 */
static void
reapply_stacked_values(struct config_generic *variable,
					   struct config_string *pHolder,
					   GucStack *stack,
					   const char *curvalue,
					   GucContext curscontext, GucSource cursource,
					   Oid cursrole)
{
	const char *name = variable->name;
	GucStack   *oldvarstack = variable->stack;

	if (stack != NULL)
	{
		/* First, recurse, so that stack items are processed bottom to top */
		reapply_stacked_values(variable, pHolder, stack->prev,
							   stack->prior.val.stringval,
							   stack->scontext, stack->source, stack->srole);

		/* See how to apply the passed-in value */
		switch (stack->state)
		{
			case GUC_SAVE:
				(void) set_config_option_ext(name, curvalue,
											 curscontext, cursource, cursrole,
											 GUC_ACTION_SAVE, true,
											 WARNING, false);
				break;

			case GUC_SET:
				(void) set_config_option_ext(name, curvalue,
											 curscontext, cursource, cursrole,
											 GUC_ACTION_SET, true,
											 WARNING, false);
				break;

			case GUC_LOCAL:
				(void) set_config_option_ext(name, curvalue,
											 curscontext, cursource, cursrole,
											 GUC_ACTION_LOCAL, true,
											 WARNING, false);
				break;

			case GUC_SET_LOCAL:
				/* first, apply the masked value as SET */
				(void) set_config_option_ext(name, stack->masked.val.stringval,
											 stack->masked_scontext,
											 PGC_S_SESSION,
											 stack->masked_srole,
											 GUC_ACTION_SET, true,
											 WARNING, false);
				/* then apply the current value as LOCAL */
				(void) set_config_option_ext(name, curvalue,
											 curscontext, cursource, cursrole,
											 GUC_ACTION_LOCAL, true,
											 WARNING, false);
				break;
		}

		/* If we successfully made a stack entry, adjust its nest level */
		if (variable->stack != oldvarstack)
			variable->stack->nest_level = stack->nest_level;
	}
	else
	{
		/*
		 * We are at the end of the stack.  If the active/previous value is
		 * different from the reset value, it must represent a previously
		 * committed session value.  Apply it, and then drop the stack entry
		 * that set_config_option will have created under the impression that
		 * this is to be just a transactional assignment.  (We leak the stack
		 * entry.)
		 */
		if (curvalue != pHolder->reset_val ||
			curscontext != pHolder->gen.reset_scontext ||
			cursource != pHolder->gen.reset_source ||
			cursrole != pHolder->gen.reset_srole)
		{
			(void) set_config_option_ext(name, curvalue,
										 curscontext, cursource, cursrole,
										 GUC_ACTION_SET, true, WARNING, false);
			variable->stack = NULL;
		}
	}
}

/*
 * Functions for extensions to call to define their custom GUC variables.
 */
void
DefineCustomBoolVariable(const char *name,
						 const char *short_desc,
						 const char *long_desc,
						 bool *valueAddr,
						 bool bootValue,
						 GucContext context,
						 int flags,
						 GucBoolCheckHook check_hook,
						 GucBoolAssignHook assign_hook,
						 GucShowHook show_hook)
{
	struct config_bool *var;

	var = (struct config_bool *)
		init_custom_variable(name, short_desc, long_desc, context, flags,
							 PGC_BOOL, sizeof(struct config_bool));
	var->variable = valueAddr;
	var->boot_val = bootValue;
	var->reset_val = bootValue;
	var->check_hook = check_hook;
	var->assign_hook = assign_hook;
	var->show_hook = show_hook;
	define_custom_variable(&var->gen);
}

void
DefineCustomIntVariable(const char *name,
						const char *short_desc,
						const char *long_desc,
						int *valueAddr,
						int bootValue,
						int minValue,
						int maxValue,
						GucContext context,
						int flags,
						GucIntCheckHook check_hook,
						GucIntAssignHook assign_hook,
						GucShowHook show_hook)
{
	struct config_int *var;

	var = (struct config_int *)
		init_custom_variable(name, short_desc, long_desc, context, flags,
							 PGC_INT, sizeof(struct config_int));
	var->variable = valueAddr;
	var->boot_val = bootValue;
	var->reset_val = bootValue;
	var->min = minValue;
	var->max = maxValue;
	var->check_hook = check_hook;
	var->assign_hook = assign_hook;
	var->show_hook = show_hook;
	define_custom_variable(&var->gen);
}

void
DefineCustomOidVariable(const char *name,
						const char *short_desc,
						const char *long_desc,
						Oid *valueAddr,
						Oid bootValue,
						Oid minValue,
						Oid maxValue,
						GucContext context,
						int flags,
						GucOidCheckHook check_hook,
						GucOidAssignHook assign_hook,
						GucShowHook show_hook)
{
	struct config_oid *var;

	var = (struct config_oid *)
		init_custom_variable(name, short_desc, long_desc, context, flags,
							 PGC_OID, sizeof(struct config_oid));
	var->variable = valueAddr;
	var->boot_val = bootValue;
	var->reset_val = bootValue;
	var->min = minValue;
	var->max = maxValue;
	var->check_hook = check_hook;
	var->assign_hook = assign_hook;
	var->show_hook = show_hook;
	define_custom_variable(&var->gen);
}

void
DefineCustomRealVariable(const char *name,
						 const char *short_desc,
						 const char *long_desc,
						 double *valueAddr,
						 double bootValue,
						 double minValue,
						 double maxValue,
						 GucContext context,
						 int flags,
						 GucRealCheckHook check_hook,
						 GucRealAssignHook assign_hook,
						 GucShowHook show_hook)
{
	struct config_real *var;

	var = (struct config_real *)
		init_custom_variable(name, short_desc, long_desc, context, flags,
							 PGC_REAL, sizeof(struct config_real));
	var->variable = valueAddr;
	var->boot_val = bootValue;
	var->reset_val = bootValue;
	var->min = minValue;
	var->max = maxValue;
	var->check_hook = check_hook;
	var->assign_hook = assign_hook;
	var->show_hook = show_hook;
	define_custom_variable(&var->gen);
}

void
DefineCustomStringVariable(const char *name,
						   const char *short_desc,
						   const char *long_desc,
						   char **valueAddr,
						   const char *bootValue,
						   GucContext context,
						   int flags,
						   GucStringCheckHook check_hook,
						   GucStringAssignHook assign_hook,
						   GucShowHook show_hook)
{
	struct config_string *var;

	var = (struct config_string *)
		init_custom_variable(name, short_desc, long_desc, context, flags,
							 PGC_STRING, sizeof(struct config_string));
	var->variable = valueAddr;
	var->boot_val = bootValue;
	var->check_hook = check_hook;
	var->assign_hook = assign_hook;
	var->show_hook = show_hook;
	define_custom_variable(&var->gen);

	/* make custom string variables sticky for connection manager */
	var->gen.flags |= GUC_YB_CUSTOM_STICKY;
}

void
DefineCustomEnumVariable(const char *name,
						 const char *short_desc,
						 const char *long_desc,
						 int *valueAddr,
						 int bootValue,
						 const struct config_enum_entry *options,
						 GucContext context,
						 int flags,
						 GucEnumCheckHook check_hook,
						 GucEnumAssignHook assign_hook,
						 GucShowHook show_hook)
{
	struct config_enum *var;

	var = (struct config_enum *)
		init_custom_variable(name, short_desc, long_desc, context, flags,
							 PGC_ENUM, sizeof(struct config_enum));
	var->variable = valueAddr;
	var->boot_val = bootValue;
	var->reset_val = bootValue;
	var->options = options;
	var->check_hook = check_hook;
	var->assign_hook = assign_hook;
	var->show_hook = show_hook;
	define_custom_variable(&var->gen);
}

/*
 * Mark the given GUC prefix as "reserved".
 *
 * This deletes any existing placeholders matching the prefix,
 * and then prevents new ones from being created.
 * Extensions should call this after they've defined all of their custom
 * GUCs, to help catch misspelled config-file entries.
 */
void
MarkGUCPrefixReserved(const char *className)
{
	int			classLen = strlen(className);
	int			i;
	MemoryContext oldcontext;

	/*
	 * Check for existing placeholders.  We must actually remove invalid
	 * placeholders, else future parallel worker startups will fail.  (We
	 * don't bother trying to free associated memory, since this shouldn't
	 * happen often.)
	 */
	for (i = 0; i < num_guc_variables; i++)
	{
		struct config_generic *var = guc_variables[i];

		if ((var->flags & GUC_CUSTOM_PLACEHOLDER) != 0 &&
			strncmp(className, var->name, classLen) == 0 &&
			var->name[classLen] == GUC_QUALIFIER_SEPARATOR)
		{
			ereport(WARNING,
					(errcode(ERRCODE_INVALID_NAME),
					 errmsg("invalid configuration parameter name \"%s\", removing it",
							var->name),
					 errdetail("\"%s\" is now a reserved prefix.",
							   className)));
			num_guc_variables--;
			memmove(&guc_variables[i], &guc_variables[i + 1],
					(num_guc_variables - i) * sizeof(struct config_generic *));
		}
	}

	/* And remember the name so we can prevent future mistakes. */
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);
	reserved_class_prefix = lappend(reserved_class_prefix, pstrdup(className));
	MemoryContextSwitchTo(oldcontext);
}

/*
 * Check a setting name against prefixes previously reserved by
 * EmitWarningsOnPlaceholders() and throw a warning if matching.
 */
static void
check_reserved_prefixes(const char *varName)
{
	char	   *sep = strchr(varName, GUC_QUALIFIER_SEPARATOR);

	if (sep)
	{
		size_t		classLen = sep - varName;
		ListCell   *lc;

		foreach(lc, reserved_class_prefix)
		{
			char	   *rcprefix = lfirst(lc);

			if (strncmp(varName, rcprefix, classLen) == 0)
			{
				for (int i = 0; i < num_guc_variables; i++)
				{
					struct config_generic *var = guc_variables[i];

					if ((var->flags & GUC_CUSTOM_PLACEHOLDER) != 0 &&
						strcmp(varName, var->name) == 0)
					{
						ereport(WARNING,
								(errcode(ERRCODE_UNDEFINED_OBJECT),
								 errmsg("unrecognized configuration parameter \"%s\"", var->name),
								 errdetail("\"%.*s\" is a reserved prefix.", (int) classLen, var->name)));
					}
				}
			}
		}
	}
}

/*
 * SHOW command
 */
void
GetPGVariable(const char *name, DestReceiver *dest)
{
	if (guc_name_compare(name, "all") == 0)
		ShowAllGUCConfig(dest);
	else
		ShowGUCConfigOption(name, dest);
}

TupleDesc
GetPGVariableResultDesc(const char *name)
{
	TupleDesc	tupdesc;

	if (guc_name_compare(name, "all") == 0)
	{
		/* need a tuple descriptor representing three TEXT columns */
		tupdesc = CreateTemplateTupleDesc(3);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "name",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "setting",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "description",
						   TEXTOID, -1, 0);
	}
	else
	{
		const char *varname;

		/* Get the canonical spelling of name */
		(void) GetConfigOptionByName(name, &varname, false);

		/* need a tuple descriptor representing a single TEXT column */
		tupdesc = CreateTemplateTupleDesc(1);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, varname,
						   TEXTOID, -1, 0);
	}
	return tupdesc;
}


/*
 * SHOW command
 */
static void
ShowGUCConfigOption(const char *name, DestReceiver *dest)
{
	TupOutputState *tstate;
	TupleDesc	tupdesc;
	const char *varname;
	char	   *value;

	/* Get the value and canonical spelling of name */
	value = GetConfigOptionByName(name, &varname, false);

	/* need a tuple descriptor representing a single TEXT column */
	tupdesc = CreateTemplateTupleDesc(1);
	TupleDescInitBuiltinEntry(tupdesc, (AttrNumber) 1, varname,
							  TEXTOID, -1, 0);

	/* prepare for projection of tuples */
	tstate = begin_tup_output_tupdesc(dest, tupdesc, &TTSOpsVirtual);

	/* Send it */
	do_text_output_oneline(tstate, value);

	end_tup_output(tstate);
}

/*
 * SHOW ALL command
 */
static void
ShowAllGUCConfig(DestReceiver *dest)
{
	int			i;
	TupOutputState *tstate;
	TupleDesc	tupdesc;
	Datum		values[3];
	bool		isnull[3] = {false, false, false};

	/* need a tuple descriptor representing three TEXT columns */
	tupdesc = CreateTemplateTupleDesc(3);
	TupleDescInitBuiltinEntry(tupdesc, (AttrNumber) 1, "name",
							  TEXTOID, -1, 0);
	TupleDescInitBuiltinEntry(tupdesc, (AttrNumber) 2, "setting",
							  TEXTOID, -1, 0);
	TupleDescInitBuiltinEntry(tupdesc, (AttrNumber) 3, "description",
							  TEXTOID, -1, 0);

	/* prepare for projection of tuples */
	tstate = begin_tup_output_tupdesc(dest, tupdesc, &TTSOpsVirtual);

	for (i = 0; i < num_guc_variables; i++)
	{
		struct config_generic *conf = guc_variables[i];
		char	   *setting;

		if ((conf->flags & GUC_NO_SHOW_ALL) ||
			((conf->flags & GUC_SUPERUSER_ONLY) &&
			 !has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_SETTINGS)))
			continue;

		/* assign to the values array */
		values[0] = PointerGetDatum(cstring_to_text(conf->name));

		setting = _ShowOption(conf, true);
		if (setting)
		{
			values[1] = PointerGetDatum(cstring_to_text(setting));
			isnull[1] = false;
		}
		else
		{
			values[1] = PointerGetDatum(NULL);
			isnull[1] = true;
		}

		if (conf->short_desc)
		{
			values[2] = PointerGetDatum(cstring_to_text(conf->short_desc));
			isnull[2] = false;
		}
		else
		{
			values[2] = PointerGetDatum(NULL);
			isnull[2] = true;
		}

		/* send it to dest */
		do_tup_output(tstate, values, isnull);

		/* clean up */
		pfree(DatumGetPointer(values[0]));
		if (setting)
		{
			pfree(setting);
			pfree(DatumGetPointer(values[1]));
		}
		if (conf->short_desc)
			pfree(DatumGetPointer(values[2]));
	}

	end_tup_output(tstate);
}

/*
 * Return an array of modified GUC options to show in EXPLAIN.
 *
 * We only report options related to query planning (marked with GUC_EXPLAIN),
 * with values different from their built-in defaults.
 */
struct config_generic **
get_explain_guc_options(int *num)
{
	struct config_generic **result;

	*num = 0;

	/*
	 * While only a fraction of all the GUC variables are marked GUC_EXPLAIN,
	 * it doesn't seem worth dynamically resizing this array.
	 */
	result = palloc(sizeof(struct config_generic *) * num_guc_variables);

	for (int i = 0; i < num_guc_variables; i++)
	{
		bool		modified;
		struct config_generic *conf = guc_variables[i];

		/* return only parameters marked for inclusion in explain */
		if (!(conf->flags & GUC_EXPLAIN))
			continue;

		/* return only options visible to the current user */
		if ((conf->flags & GUC_NO_SHOW_ALL) ||
			((conf->flags & GUC_SUPERUSER_ONLY) &&
			 !has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_SETTINGS)))
			continue;

		/* return only options that are different from their boot values */
		modified = false;

		switch (conf->vartype)
		{
			case PGC_BOOL:
				{
					struct config_bool *lconf = (struct config_bool *) conf;

					modified = (lconf->boot_val != *(lconf->variable));
				}
				break;

			case PGC_INT:
				{
					struct config_int *lconf = (struct config_int *) conf;

					modified = (lconf->boot_val != *(lconf->variable));
				}
				break;

			case PGC_REAL:
				{
					struct config_real *lconf = (struct config_real *) conf;

					modified = (lconf->boot_val != *(lconf->variable));
				}
				break;

			case PGC_STRING:
				{
					struct config_string *lconf = (struct config_string *) conf;

					modified = (strcmp(lconf->boot_val, *(lconf->variable)) != 0);
				}
				break;

			case PGC_ENUM:
				{
					struct config_enum *lconf = (struct config_enum *) conf;

					modified = (lconf->boot_val != *(lconf->variable));
				}
				break;

			default:
				elog(ERROR, "unexpected GUC type: %d", conf->vartype);
		}

		if (!modified)
			continue;

		/* OK, report it */
		result[*num] = conf;
		*num = *num + 1;
	}

	return result;
}

/*
 * Return GUC variable value by name; optionally return canonical form of
 * name.  If the GUC is unset, then throw an error unless missing_ok is true,
 * in which case return NULL.  Return value is palloc'd (but *varname isn't).
 */
char *
GetConfigOptionByName(const char *name, const char **varname, bool missing_ok)
{
	struct config_generic *record;

	record = find_option(name, false, missing_ok, ERROR);
	if (record == NULL)
	{
		if (varname)
			*varname = NULL;
		return NULL;
	}

	if ((record->flags & GUC_SUPERUSER_ONLY) &&
		!has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_SETTINGS))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser or have privileges of pg_read_all_settings to examine \"%s\"",
						name)));

	if (varname)
		*varname = record->name;

	return _ShowOption(record, true);
}

/*
 * Return some of the flags associated to the specified GUC in the shape of
 * a text array, and NULL if it does not exist.  An empty array is returned
 * if the GUC exists without any meaningful flags to show.
 */
Datum
pg_settings_get_flags(PG_FUNCTION_ARGS)
{
#define MAX_GUC_FLAGS	5
	char	   *varname = TextDatumGetCString(PG_GETARG_DATUM(0));
	struct config_generic *record;
	int			cnt = 0;
	Datum		flags[MAX_GUC_FLAGS];
	ArrayType  *a;

	record = find_option(varname, false, true, ERROR);

	/* return NULL if no such variable */
	if (record == NULL)
		PG_RETURN_NULL();

	if (record->flags & GUC_EXPLAIN)
		flags[cnt++] = CStringGetTextDatum("EXPLAIN");
	if (record->flags & GUC_NO_RESET_ALL)
		flags[cnt++] = CStringGetTextDatum("NO_RESET_ALL");
	if (record->flags & GUC_NO_SHOW_ALL)
		flags[cnt++] = CStringGetTextDatum("NO_SHOW_ALL");
	if (record->flags & GUC_NOT_IN_SAMPLE)
		flags[cnt++] = CStringGetTextDatum("NOT_IN_SAMPLE");
	if (record->flags & GUC_RUNTIME_COMPUTED)
		flags[cnt++] = CStringGetTextDatum("RUNTIME_COMPUTED");

	Assert(cnt <= MAX_GUC_FLAGS);

	/* Returns the record as Datum */
	a = construct_array(flags, cnt, TEXTOID, -1, false, TYPALIGN_INT);
	PG_RETURN_ARRAYTYPE_P(a);
}

/*
 * Return GUC variable value by variable number; optionally return canonical
 * form of name.  Return value is palloc'd.
 */
void
GetConfigOptionByNum(int varnum, const char **values, bool *noshow)
{
	char		buffer[256];
	struct config_generic *conf;

	/* check requested variable number valid */
	Assert((varnum >= 0) && (varnum < num_guc_variables));

	conf = guc_variables[varnum];

	if (noshow)
	{
		if ((conf->flags & GUC_NO_SHOW_ALL) ||
			((conf->flags & GUC_SUPERUSER_ONLY) &&
			 !has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_SETTINGS)))
			*noshow = true;
		else
			*noshow = false;
	}

	/* first get the generic attributes */

	/* name */
	values[0] = conf->name;

	/* setting: use _ShowOption in order to avoid duplicating the logic */
	values[1] = _ShowOption(conf, false);

	/* unit, if any (NULL is fine) */
	values[2] = get_config_unit_name(conf->flags);

	/* group */
	values[3] = _(config_group_names[conf->group]);

	/* short_desc */
	values[4] = conf->short_desc != NULL ? _(conf->short_desc) : NULL;

	/* extra_desc */
	values[5] = conf->long_desc != NULL ? _(conf->long_desc) : NULL;

	/* context */
	values[6] = GucContext_Names[conf->context];

	/* vartype */
	values[7] = config_type_names[conf->vartype];

	/* source */
	values[8] = GucSource_Names[conf->source];

	/* now get the type specific attributes */
	switch (conf->vartype)
	{
		case PGC_BOOL:
			{
				struct config_bool *lconf = (struct config_bool *) conf;

				/* min_val */
				values[9] = NULL;

				/* max_val */
				values[10] = NULL;

				/* enumvals */
				values[11] = NULL;

				/* boot_val */
				values[12] = pstrdup(lconf->boot_val ? "on" : "off");

				/* reset_val */
				values[13] = pstrdup(lconf->reset_val ? "on" : "off");
			}
			break;

		case PGC_OID:
			{
				struct config_oid *lconf = (struct config_oid *) conf;

				/* min_val */
				snprintf(buffer, sizeof(buffer), "%u", lconf->min);
				values[9] = pstrdup(buffer);

				/* max_val */
				snprintf(buffer, sizeof(buffer), "%u", lconf->max);
				values[10] = pstrdup(buffer);

				/* enumvals */
				values[11] = NULL;

				/* boot_val */
				snprintf(buffer, sizeof(buffer), "%u", lconf->boot_val);
				values[12] = pstrdup(buffer);

				/* reset_val */
				snprintf(buffer, sizeof(buffer), "%u", lconf->reset_val);
				values[13] = pstrdup(buffer);
			}
			break;


		case PGC_INT:
			{
				struct config_int *lconf = (struct config_int *) conf;

				/* min_val */
				snprintf(buffer, sizeof(buffer), "%d", lconf->min);
				values[9] = pstrdup(buffer);

				/* max_val */
				snprintf(buffer, sizeof(buffer), "%d", lconf->max);
				values[10] = pstrdup(buffer);

				/* enumvals */
				values[11] = NULL;

				/* boot_val */
				snprintf(buffer, sizeof(buffer), "%d", lconf->boot_val);
				values[12] = pstrdup(buffer);

				/* reset_val */
				snprintf(buffer, sizeof(buffer), "%d", lconf->reset_val);
				values[13] = pstrdup(buffer);
			}
			break;

		case PGC_REAL:
			{
				struct config_real *lconf = (struct config_real *) conf;

				/* min_val */
				snprintf(buffer, sizeof(buffer), "%g", lconf->min);
				values[9] = pstrdup(buffer);

				/* max_val */
				snprintf(buffer, sizeof(buffer), "%g", lconf->max);
				values[10] = pstrdup(buffer);

				/* enumvals */
				values[11] = NULL;

				/* boot_val */
				snprintf(buffer, sizeof(buffer), "%g", lconf->boot_val);
				values[12] = pstrdup(buffer);

				/* reset_val */
				snprintf(buffer, sizeof(buffer), "%g", lconf->reset_val);
				values[13] = pstrdup(buffer);
			}
			break;

		case PGC_STRING:
			{
				struct config_string *lconf = (struct config_string *) conf;

				/* min_val */
				values[9] = NULL;

				/* max_val */
				values[10] = NULL;

				/* enumvals */
				values[11] = NULL;

				/* boot_val */
				if (lconf->boot_val == NULL)
					values[12] = NULL;
				else
					values[12] = pstrdup(lconf->boot_val);

				/* reset_val */
				if (lconf->reset_val == NULL)
					values[13] = NULL;
				else
					values[13] = pstrdup(lconf->reset_val);
			}
			break;

		case PGC_ENUM:
			{
				struct config_enum *lconf = (struct config_enum *) conf;

				/* min_val */
				values[9] = NULL;

				/* max_val */
				values[10] = NULL;

				/* enumvals */

				/*
				 * NOTE! enumvals with double quotes in them are not
				 * supported!
				 */
				values[11] = config_enum_get_options((struct config_enum *) conf,
													 "{\"", "\"}", "\",\"");

				/* boot_val */
				values[12] = pstrdup(config_enum_lookup_by_value(lconf,
																 lconf->boot_val));

				/* reset_val */
				values[13] = pstrdup(config_enum_lookup_by_value(lconf,
																 lconf->reset_val));
			}
			break;

		default:
			{
				/*
				 * should never get here, but in case we do, set 'em to NULL
				 */

				/* min_val */
				values[9] = NULL;

				/* max_val */
				values[10] = NULL;

				/* enumvals */
				values[11] = NULL;

				/* boot_val */
				values[12] = NULL;

				/* reset_val */
				values[13] = NULL;
			}
			break;
	}

	/*
	 * If the setting came from a config file, set the source location. For
	 * security reasons, we don't show source file/line number for
	 * insufficiently-privileged users.
	 */
	if (conf->source == PGC_S_FILE &&
		has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_SETTINGS))
	{
		values[14] = conf->sourcefile;
		snprintf(buffer, sizeof(buffer), "%d", conf->sourceline);
		values[15] = pstrdup(buffer);
	}
	else
	{
		values[14] = NULL;
		values[15] = NULL;
	}

	values[16] = (conf->status & GUC_PENDING_RESTART) ? "t" : "f";
}

/*
 * Return the total number of GUC variables
 */
int
GetNumConfigOptions(void)
{
	return num_guc_variables;
}

/*
 * show_config_by_name - equiv to SHOW X command but implemented as
 * a function.
 */
Datum
show_config_by_name(PG_FUNCTION_ARGS)
{
	char	   *varname = TextDatumGetCString(PG_GETARG_DATUM(0));
	char	   *varval;

	/* Get the value */
	varval = GetConfigOptionByName(varname, NULL, false);

	/* Convert to text */
	PG_RETURN_TEXT_P(cstring_to_text(varval));
}

/*
 * show_config_by_name_missing_ok - equiv to SHOW X command but implemented as
 * a function.  If X does not exist, suppress the error and just return NULL
 * if missing_ok is true.
 */
Datum
show_config_by_name_missing_ok(PG_FUNCTION_ARGS)
{
	char	   *varname = TextDatumGetCString(PG_GETARG_DATUM(0));
	bool		missing_ok = PG_GETARG_BOOL(1);
	char	   *varval;

	/* Get the value */
	varval = GetConfigOptionByName(varname, NULL, missing_ok);

	/* return NULL if no such variable */
	if (varval == NULL)
		PG_RETURN_NULL();

	/* Convert to text */
	PG_RETURN_TEXT_P(cstring_to_text(varval));
}

/*
 * show_all_settings - equiv to SHOW ALL command but implemented as
 * a Table Function.
 */
#define NUM_PG_SETTINGS_ATTS	17

Datum
show_all_settings(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	TupleDesc	tupdesc;
	int			call_cntr;
	int			max_calls;
	AttInMetadata *attinmeta;
	MemoryContext oldcontext;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/*
		 * need a tuple descriptor representing NUM_PG_SETTINGS_ATTS columns
		 * of the appropriate types
		 */
		tupdesc = CreateTemplateTupleDesc(NUM_PG_SETTINGS_ATTS);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "name",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "setting",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "unit",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "category",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "short_desc",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "extra_desc",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7, "context",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 8, "vartype",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 9, "source",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 10, "min_val",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 11, "max_val",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 12, "enumvals",
						   TEXTARRAYOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 13, "boot_val",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 14, "reset_val",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 15, "sourcefile",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 16, "sourceline",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 17, "pending_restart",
						   BOOLOID, -1, 0);

		/*
		 * Generate attribute metadata needed later to produce tuples from raw
		 * C strings
		 */
		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		/* total number of tuples to be returned */
		funcctx->max_calls = GetNumConfigOptions();

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	call_cntr = funcctx->call_cntr;
	max_calls = funcctx->max_calls;
	attinmeta = funcctx->attinmeta;

	if (call_cntr < max_calls)	/* do when there is more left to send */
	{
		char	   *values[NUM_PG_SETTINGS_ATTS];
		bool		noshow;
		HeapTuple	tuple;
		Datum		result;

		/*
		 * Get the next visible GUC variable name and value
		 */
		do
		{
			GetConfigOptionByNum(call_cntr, (const char **) values, &noshow);
			if (noshow)
			{
				/* bump the counter and get the next config setting */
				call_cntr = ++funcctx->call_cntr;

				/* make sure we haven't gone too far now */
				if (call_cntr >= max_calls)
					SRF_RETURN_DONE(funcctx);
			}
		} while (noshow);

		/* build a tuple */
		tuple = BuildTupleFromCStrings(attinmeta, values);

		/* make the tuple into a datum */
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}
	else
	{
		/* do when there is no more left */
		SRF_RETURN_DONE(funcctx);
	}
}

/*
 * show_all_file_settings
 *
 * Returns a table of all parameter settings in all configuration files
 * which includes the config file pathname, the line number, a sequence number
 * indicating the order in which the settings were encountered, the parameter
 * name and value, a bool showing if the value could be applied, and possibly
 * an associated error message.  (For problems such as syntax errors, the
 * parameter name/value might be NULL.)
 *
 * Note: no filtering is done here, instead we depend on the GRANT system
 * to prevent unprivileged users from accessing this function or the view
 * built on top of it.
 */
Datum
show_all_file_settings(PG_FUNCTION_ARGS)
{
#define NUM_PG_FILE_SETTINGS_ATTS 7
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	ConfigVariable *conf;
	int			seqno;

	/* Scan the config files using current context as workspace */
	conf = ProcessConfigFileInternal(PGC_SIGHUP, false, DEBUG3);

	/* Build a tuplestore to return our results in */
	InitMaterializedSRF(fcinfo, 0);

	/* Process the results and create a tuplestore */
	for (seqno = 1; conf != NULL; conf = conf->next, seqno++)
	{
		Datum		values[NUM_PG_FILE_SETTINGS_ATTS];
		bool		nulls[NUM_PG_FILE_SETTINGS_ATTS];

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		/* sourcefile */
		if (conf->filename)
			values[0] = PointerGetDatum(cstring_to_text(conf->filename));
		else
			nulls[0] = true;

		/* sourceline (not meaningful if no sourcefile) */
		if (conf->filename)
			values[1] = Int32GetDatum(conf->sourceline);
		else
			nulls[1] = true;

		/* seqno */
		values[2] = Int32GetDatum(seqno);

		/* name */
		if (conf->name)
			values[3] = PointerGetDatum(cstring_to_text(conf->name));
		else
			nulls[3] = true;

		/* setting */
		if (conf->value)
			values[4] = PointerGetDatum(cstring_to_text(conf->value));
		else
			nulls[4] = true;

		/* applied */
		values[5] = BoolGetDatum(conf->applied);

		/* error */
		if (conf->errmsg)
			values[6] = PointerGetDatum(cstring_to_text(conf->errmsg));
		else
			nulls[6] = true;

		/* shove row into tuplestore */
		tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc, values, nulls);
	}

	return (Datum) 0;
}

static char *
_ShowOption(struct config_generic *record, bool use_units)
{
	char		buffer[256];
	const char *val;

	switch (record->vartype)
	{
		case PGC_BOOL:
			{
				struct config_bool *conf = (struct config_bool *) record;

				if (conf->show_hook)
					val = conf->show_hook();
				else
					val = *conf->variable ? "on" : "off";
			}
			break;

		case PGC_INT:
			{
				struct config_int *conf = (struct config_int *) record;

				if (conf->show_hook)
					val = conf->show_hook();
				else
				{
					/*
					 * Use int64 arithmetic to avoid overflows in units
					 * conversion.
					 */
					int64		result = *conf->variable;
					const char *unit;

					if (use_units && result > 0 && (record->flags & GUC_UNIT))
						convert_int_from_base_unit(result,
												   record->flags & GUC_UNIT,
												   &result, &unit);
					else
						unit = "";

					snprintf(buffer, sizeof(buffer), INT64_FORMAT "%s",
							 result, unit);
					val = buffer;
				}
			}
			break;

		case PGC_OID:
			/* YB_TODO(alex@yugabyte) Is this case still needed for Pg13+ */
			{
				struct config_oid *conf = (struct config_oid *) record;

				if (conf->show_hook)
					val = conf->show_hook();
				else
				{
					/*
					 * Use int64 arithmetic to avoid overflows in units
					 * conversion.
					 */
					int64		result = *conf->variable;
					const char *unit;

					if (use_units && result > 0 && (record->flags & GUC_UNIT))
					{
						/* YB_TODO(alex@yugabyte)
						 * - Check if calling "convert_int" is correct.
						 * - If not correct, implement a function for convert_oid.
						 */
						convert_int_from_base_unit(result, record->flags & GUC_UNIT,
												   &result, &unit);
					}
					else
						unit = "";

					snprintf(buffer, sizeof(buffer), INT64_FORMAT "%s",
							 result, unit);
					val = buffer;
				}
			}
			break;

		case PGC_REAL:
			{
				struct config_real *conf = (struct config_real *) record;

				if (conf->show_hook)
					val = conf->show_hook();
				else
				{
					double		result = *conf->variable;
					const char *unit;

					if (use_units && result > 0 && (record->flags & GUC_UNIT))
						convert_real_from_base_unit(result,
													record->flags & GUC_UNIT,
													&result, &unit);
					else
						unit = "";

					snprintf(buffer, sizeof(buffer), "%g%s",
							 result, unit);
					val = buffer;
				}
			}
			break;

		case PGC_STRING:
			{
				struct config_string *conf = (struct config_string *) record;

				if (conf->show_hook)
					val = conf->show_hook();
				else if (*conf->variable && **conf->variable)
					val = *conf->variable;
				else
					val = "";
			}
			break;

		case PGC_ENUM:
			{
				struct config_enum *conf = (struct config_enum *) record;

				if (conf->show_hook)
					val = conf->show_hook();
				else
					val = config_enum_lookup_by_value(conf, *conf->variable);
			}
			break;

		default:
			/* just to keep compiler quiet */
			val = "???";
			break;
	}

	return pstrdup(val);
}


#ifdef EXEC_BACKEND

/*
 *	These routines dump out all non-default GUC options into a binary
 *	file that is read by all exec'ed backends.  The format is:
 *
 *		variable name, string, null terminated
 *		variable value, string, null terminated
 *		variable sourcefile, string, null terminated (empty if none)
 *		variable sourceline, integer
 *		variable source, integer
 *		variable scontext, integer
*		variable srole, OID
 */
static void
write_one_nondefault_variable(FILE *fp, struct config_generic *gconf)
{
	if (gconf->source == PGC_S_DEFAULT)
		return;

	fprintf(fp, "%s", gconf->name);
	fputc(0, fp);

	switch (gconf->vartype)
	{
		case PGC_BOOL:
			{
				struct config_bool *conf = (struct config_bool *) gconf;

				if (*conf->variable)
					fprintf(fp, "true");
				else
					fprintf(fp, "false");
			}
			break;

		case PGC_INT:
			{
				struct config_int *conf = (struct config_int *) gconf;

				fprintf(fp, "%d", *conf->variable);
			}
			break;

		case PGC_OID:
			{
				struct config_oid *conf = (struct config_oid *) gconf;

				fprintf(fp, "%u", *conf->variable);
			}
			break;

		case PGC_REAL:
			{
				struct config_real *conf = (struct config_real *) gconf;

				fprintf(fp, "%.17g", *conf->variable);
			}
			break;

		case PGC_STRING:
			{
				struct config_string *conf = (struct config_string *) gconf;

				fprintf(fp, "%s", *conf->variable);
			}
			break;

		case PGC_ENUM:
			{
				struct config_enum *conf = (struct config_enum *) gconf;

				fprintf(fp, "%s",
						config_enum_lookup_by_value(conf, *conf->variable));
			}
			break;
	}

	fputc(0, fp);

	if (gconf->sourcefile)
		fprintf(fp, "%s", gconf->sourcefile);
	fputc(0, fp);

	fwrite(&gconf->sourceline, 1, sizeof(gconf->sourceline), fp);
	fwrite(&gconf->source, 1, sizeof(gconf->source), fp);
	fwrite(&gconf->scontext, 1, sizeof(gconf->scontext), fp);
	fwrite(&gconf->srole, 1, sizeof(gconf->srole), fp);
}

void
write_nondefault_variables(GucContext context)
{
	int			elevel;
	FILE	   *fp;
	int			i;

	Assert(context == PGC_POSTMASTER || context == PGC_SIGHUP);

	elevel = (context == PGC_SIGHUP) ? LOG : ERROR;

	/*
	 * Open file
	 */
	fp = AllocateFile(CONFIG_EXEC_PARAMS_NEW, "w");
	if (!fp)
	{
		ereport(elevel,
				(errcode_for_file_access(),
				 errmsg("could not write to file \"%s\": %m",
						CONFIG_EXEC_PARAMS_NEW)));
		return;
	}

	for (i = 0; i < num_guc_variables; i++)
	{
		write_one_nondefault_variable(fp, guc_variables[i]);
	}

	if (FreeFile(fp))
	{
		ereport(elevel,
				(errcode_for_file_access(),
				 errmsg("could not write to file \"%s\": %m",
						CONFIG_EXEC_PARAMS_NEW)));
		return;
	}

	/*
	 * Put new file in place.  This could delay on Win32, but we don't hold
	 * any exclusive locks.
	 */
	rename(CONFIG_EXEC_PARAMS_NEW, CONFIG_EXEC_PARAMS);
}


/*
 *	Read string, including null byte from file
 *
 *	Return NULL on EOF and nothing read
 */
static char *
read_string_with_null(FILE *fp)
{
	int			i = 0,
				ch,
				maxlen = 256;
	char	   *str = NULL;

	do
	{
		if ((ch = fgetc(fp)) == EOF)
		{
			if (i == 0)
				return NULL;
			else
				elog(FATAL, "invalid format of exec config params file");
		}
		if (i == 0)
			str = guc_malloc(FATAL, maxlen);
		else if (i == maxlen)
			str = guc_realloc(FATAL, str, maxlen *= 2);
		str[i++] = ch;
	} while (ch != 0);

	return str;
}


/*
 *	This routine loads a previous postmaster dump of its non-default
 *	settings.
 */
void
read_nondefault_variables(void)
{
	FILE	   *fp;
	char	   *varname,
			   *varvalue,
			   *varsourcefile;
	int			varsourceline;
	GucSource	varsource;
	GucContext	varscontext;
	Oid			varsrole;

	/*
	 * Open file
	 */
	fp = AllocateFile(CONFIG_EXEC_PARAMS, "r");
	if (!fp)
	{
		/* File not found is fine */
		if (errno != ENOENT)
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not read from file \"%s\": %m",
							CONFIG_EXEC_PARAMS)));
		return;
	}

	for (;;)
	{
		struct config_generic *record;

		if ((varname = read_string_with_null(fp)) == NULL)
			break;

		if ((record = find_option(varname, true, false, FATAL)) == NULL)
			elog(FATAL, "failed to locate variable \"%s\" in exec config params file", varname);

		if ((varvalue = read_string_with_null(fp)) == NULL)
			elog(FATAL, "invalid format of exec config params file");
		if ((varsourcefile = read_string_with_null(fp)) == NULL)
			elog(FATAL, "invalid format of exec config params file");
		if (fread(&varsourceline, 1, sizeof(varsourceline), fp) != sizeof(varsourceline))
			elog(FATAL, "invalid format of exec config params file");
		if (fread(&varsource, 1, sizeof(varsource), fp) != sizeof(varsource))
			elog(FATAL, "invalid format of exec config params file");
		if (fread(&varscontext, 1, sizeof(varscontext), fp) != sizeof(varscontext))
			elog(FATAL, "invalid format of exec config params file");
		if (fread(&varsrole, 1, sizeof(varsrole), fp) != sizeof(varsrole))
			elog(FATAL, "invalid format of exec config params file");

		(void) set_config_option_ext(varname, varvalue,
									 varscontext, varsource, varsrole,
									 GUC_ACTION_SET, true, 0, true);
		if (varsourcefile[0])
			set_config_sourcefile(varname, varsourcefile, varsourceline);

		free(varname);
		free(varvalue);
		free(varsourcefile);
	}

	FreeFile(fp);
}
#endif							/* EXEC_BACKEND */

/*
 * can_skip_gucvar:
 * Decide whether SerializeGUCState can skip sending this GUC variable,
 * or whether RestoreGUCState can skip resetting this GUC to default.
 *
 * It is somewhat magical and fragile that the same test works for both cases.
 * Realize in particular that we are very likely selecting different sets of
 * GUCs on the leader and worker sides!  Be sure you've understood the
 * comments here and in RestoreGUCState thoroughly before changing this.
 */
static bool
can_skip_gucvar(struct config_generic *gconf)
{
	/*
	 * We can skip GUCs that are guaranteed to have the same values in leaders
	 * and workers.  (Note it is critical that the leader and worker have the
	 * same idea of which GUCs fall into this category.  It's okay to consider
	 * context and name for this purpose, since those are unchanging
	 * properties of a GUC.)
	 *
	 * PGC_POSTMASTER variables always have the same value in every child of a
	 * particular postmaster, so the worker will certainly have the right
	 * value already.  Likewise, PGC_INTERNAL variables are set by special
	 * mechanisms (if indeed they aren't compile-time constants).  So we may
	 * always skip these.
	 *
	 * Role must be handled specially because its current value can be an
	 * invalid value (for instance, if someone dropped the role since we set
	 * it).  So if we tried to serialize it normally, we might get a failure.
	 * We skip it here, and use another mechanism to ensure the worker has the
	 * right value.
	 *
	 * For all other GUCs, we skip if the GUC has its compiled-in default
	 * value (i.e., source == PGC_S_DEFAULT).  On the leader side, this means
	 * we don't send GUCs that have their default values, which typically
	 * saves lots of work.  On the worker side, this means we don't need to
	 * reset the GUC to default because it already has that value.  See
	 * comments in RestoreGUCState for more info.
	 */
	return gconf->context == PGC_POSTMASTER ||
		gconf->context == PGC_INTERNAL || gconf->source == PGC_S_DEFAULT ||
		strcmp(gconf->name, "role") == 0;
}

/*
 * estimate_variable_size:
 *		Compute space needed for dumping the given GUC variable.
 *
 * It's OK to overestimate, but not to underestimate.
 */
static Size
estimate_variable_size(struct config_generic *gconf)
{
	Size		size;
	Size		valsize = 0;

	/* Skippable GUCs consume zero space. */
	if (can_skip_gucvar(gconf))
		return 0;

	/* Name, plus trailing zero byte. */
	size = strlen(gconf->name) + 1;

	/* Get the maximum display length of the GUC value. */
	switch (gconf->vartype)
	{
		case PGC_BOOL:
			{
				valsize = 5;	/* max(strlen('true'), strlen('false')) */
			}
			break;

		case PGC_INT:
			{
				struct config_int *conf = (struct config_int *) gconf;

				/*
				 * Instead of getting the exact display length, use max
				 * length.  Also reduce the max length for typical ranges of
				 * small values.  Maximum value is 2147483647, i.e. 10 chars.
				 * Include one byte for sign.
				 */
				if (Abs(*conf->variable) < 1000)
					valsize = 3 + 1;
				else
					valsize = 10 + 1;
			}
			break;

		case PGC_OID:
			{
				struct config_oid *conf = (struct config_oid *) gconf;

				/*
				 * Instead of getting the exact display length, use max
				 * length.  Also reduce the max length for typical ranges of
				 * small values.  Maximum value is 4294967295, i.e. 10 chars.
				 */
				if ((*conf->variable) < 1000)
					valsize = 3;
				else
					valsize = 10;
			}
			break;

		case PGC_REAL:
			{
				/*
				 * We are going to print it with %e with REALTYPE_PRECISION
				 * fractional digits.  Account for sign, leading digit,
				 * decimal point, and exponent with up to 3 digits.  E.g.
				 * -3.99329042340000021e+110
				 */
				valsize = 1 + 1 + 1 + REALTYPE_PRECISION + 5;
			}
			break;

		case PGC_STRING:
			{
				struct config_string *conf = (struct config_string *) gconf;

				/*
				 * If the value is NULL, we transmit it as an empty string.
				 * Although this is not physically the same value, GUC
				 * generally treats a NULL the same as empty string.
				 */
				if (*conf->variable)
					valsize = strlen(*conf->variable);
				else
					valsize = 0;
			}
			break;

		case PGC_ENUM:
			{
				struct config_enum *conf = (struct config_enum *) gconf;

				valsize = strlen(config_enum_lookup_by_value(conf, *conf->variable));
			}
			break;
	}

	/* Allow space for terminating zero-byte for value */
	size = add_size(size, valsize + 1);

	if (gconf->sourcefile)
		size = add_size(size, strlen(gconf->sourcefile));

	/* Allow space for terminating zero-byte for sourcefile */
	size = add_size(size, 1);

	/* Include line whenever file is nonempty. */
	if (gconf->sourcefile && gconf->sourcefile[0])
		size = add_size(size, sizeof(gconf->sourceline));

	size = add_size(size, sizeof(gconf->source));
	size = add_size(size, sizeof(gconf->scontext));
	size = add_size(size, sizeof(gconf->srole));

	return size;
}

/*
 * EstimateGUCStateSpace:
 * Returns the size needed to store the GUC state for the current process
 */
Size
EstimateGUCStateSpace(void)
{
	Size		size;
	int			i;

	/* Add space reqd for saving the data size of the guc state */
	size = sizeof(Size);

	/* Add up the space needed for each GUC variable */
	for (i = 0; i < num_guc_variables; i++)
		size = add_size(size,
						estimate_variable_size(guc_variables[i]));

	return size;
}

/*
 * do_serialize:
 * Copies the formatted string into the destination.  Moves ahead the
 * destination pointer, and decrements the maxbytes by that many bytes. If
 * maxbytes is not sufficient to copy the string, error out.
 */
static void
do_serialize(char **destptr, Size *maxbytes, const char *fmt,...)
{
	va_list		vargs;
	int			n;

	if (*maxbytes <= 0)
		elog(ERROR, "not enough space to serialize GUC state");

	va_start(vargs, fmt);
	n = vsnprintf(*destptr, *maxbytes, fmt, vargs);
	va_end(vargs);

	if (n < 0)
	{
		/* Shouldn't happen. Better show errno description. */
		elog(ERROR, "vsnprintf failed: %m with format string \"%s\"", fmt);
	}
	if (n >= *maxbytes)
	{
		/* This shouldn't happen either, really. */
		elog(ERROR, "not enough space to serialize GUC state");
	}

	/* Shift the destptr ahead of the null terminator */
	*destptr += n + 1;
	*maxbytes -= n + 1;
}

/* Binary copy version of do_serialize() */
static void
do_serialize_binary(char **destptr, Size *maxbytes, void *val, Size valsize)
{
	if (valsize > *maxbytes)
		elog(ERROR, "not enough space to serialize GUC state");

	memcpy(*destptr, val, valsize);
	*destptr += valsize;
	*maxbytes -= valsize;
}

/*
 * serialize_variable:
 * Dumps name, value and other information of a GUC variable into destptr.
 */
static void
serialize_variable(char **destptr, Size *maxbytes,
				   struct config_generic *gconf)
{
	/* Ignore skippable GUCs. */
	if (can_skip_gucvar(gconf))
		return;

	do_serialize(destptr, maxbytes, "%s", gconf->name);

	switch (gconf->vartype)
	{
		case PGC_BOOL:
			{
				struct config_bool *conf = (struct config_bool *) gconf;

				do_serialize(destptr, maxbytes,
							 (*conf->variable ? "true" : "false"));
			}
			break;

		case PGC_INT:
			{
				struct config_int *conf = (struct config_int *) gconf;

				do_serialize(destptr, maxbytes, "%d", *conf->variable);
			}
			break;

		case PGC_OID:
			{
				struct config_oid *conf = (struct config_oid *) gconf;

				do_serialize(destptr, maxbytes, "%u", *conf->variable);
			}
			break;

		case PGC_REAL:
			{
				struct config_real *conf = (struct config_real *) gconf;

				do_serialize(destptr, maxbytes, "%.*e",
							 REALTYPE_PRECISION, *conf->variable);
			}
			break;

		case PGC_STRING:
			{
				struct config_string *conf = (struct config_string *) gconf;

				/* NULL becomes empty string, see estimate_variable_size() */
				do_serialize(destptr, maxbytes, "%s",
							 *conf->variable ? *conf->variable : "");
			}
			break;

		case PGC_ENUM:
			{
				struct config_enum *conf = (struct config_enum *) gconf;

				do_serialize(destptr, maxbytes, "%s",
							 config_enum_lookup_by_value(conf, *conf->variable));
			}
			break;
	}

	do_serialize(destptr, maxbytes, "%s",
				 (gconf->sourcefile ? gconf->sourcefile : ""));

	if (gconf->sourcefile && gconf->sourcefile[0])
		do_serialize_binary(destptr, maxbytes, &gconf->sourceline,
							sizeof(gconf->sourceline));

	do_serialize_binary(destptr, maxbytes, &gconf->source,
						sizeof(gconf->source));
	do_serialize_binary(destptr, maxbytes, &gconf->scontext,
						sizeof(gconf->scontext));
	do_serialize_binary(destptr, maxbytes, &gconf->srole,
						sizeof(gconf->srole));
}

/*
 * SerializeGUCState:
 * Dumps the complete GUC state onto the memory location at start_address.
 */
void
SerializeGUCState(Size maxsize, char *start_address)
{
	char	   *curptr;
	Size		actual_size;
	Size		bytes_left;
	int			i;

	/* Reserve space for saving the actual size of the guc state */
	Assert(maxsize > sizeof(actual_size));
	curptr = start_address + sizeof(actual_size);
	bytes_left = maxsize - sizeof(actual_size);

	for (i = 0; i < num_guc_variables; i++)
		serialize_variable(&curptr, &bytes_left, guc_variables[i]);

	/* Store actual size without assuming alignment of start_address. */
	actual_size = maxsize - bytes_left - sizeof(actual_size);
	memcpy(start_address, &actual_size, sizeof(actual_size));
}

/*
 * read_gucstate:
 * Actually it does not read anything, just returns the srcptr. But it does
 * move the srcptr past the terminating zero byte, so that the caller is ready
 * to read the next string.
 */
static char *
read_gucstate(char **srcptr, char *srcend)
{
	char	   *retptr = *srcptr;
	char	   *ptr;

	if (*srcptr >= srcend)
		elog(ERROR, "incomplete GUC state");

	/* The string variables are all null terminated */
	for (ptr = *srcptr; ptr < srcend && *ptr != '\0'; ptr++)
		;

	if (ptr >= srcend)
		elog(ERROR, "could not find null terminator in GUC state");

	/* Set the new position to the byte following the terminating NUL */
	*srcptr = ptr + 1;

	return retptr;
}

/* Binary read version of read_gucstate(). Copies into dest */
static void
read_gucstate_binary(char **srcptr, char *srcend, void *dest, Size size)
{
	if (*srcptr + size > srcend)
		elog(ERROR, "incomplete GUC state");

	memcpy(dest, *srcptr, size);
	*srcptr += size;
}

void YbSetParallelWorker()
{
	yb_is_parallel_worker = true;
	elog(LOG, "yb_is_parallel_worker has been set to true");
}

/*
 * Callback used to add a context message when reporting errors that occur
 * while trying to restore GUCs in parallel workers.
 */
static void
guc_restore_error_context_callback(void *arg)
{
	char	  **error_context_name_and_value = (char **) arg;

	if (error_context_name_and_value)
		errcontext("while setting parameter \"%s\" to \"%s\"",
				   error_context_name_and_value[0],
				   error_context_name_and_value[1]);
}

/*
 * RestoreGUCState:
 * Reads the GUC state at the specified address and sets this process's
 * GUCs to match.
 *
 * Note that this provides the worker with only a very shallow view of the
 * leader's GUC state: we'll know about the currently active values, but not
 * about stacked or reset values.  That's fine since the worker is just
 * executing one part of a query, within which the active values won't change
 * and the stacked values are invisible.
 */
void
RestoreGUCState(void *gucstate)
{
	char	   *varname,
			   *varvalue,
			   *varsourcefile;
	int			varsourceline;
	GucSource	varsource;
	GucContext	varscontext;
	Oid			varsrole;
	char	   *srcptr = (char *) gucstate;
	char	   *srcend;
	Size		len;
	int			i;
	ErrorContextCallback error_context_callback;

	/*
	 * First, ensure that all potentially-shippable GUCs are reset to their
	 * default values.  We must not touch those GUCs that the leader will
	 * never ship, while there is no need to touch those that are shippable
	 * but already have their default values.  Thus, this ends up being the
	 * same test that SerializeGUCState uses, even though the sets of
	 * variables involved may well be different since the leader's set of
	 * variables-not-at-default-values can differ from the set that are
	 * not-default in this freshly started worker.
	 *
	 * Once we have set all the potentially-shippable GUCs to default values,
	 * restoring the GUCs that the leader sent (because they had non-default
	 * values over there) leads us to exactly the set of GUC values that the
	 * leader has.  This is true even though the worker may have initially
	 * absorbed postgresql.conf settings that the leader hasn't yet seen, or
	 * ALTER USER/DATABASE SET settings that were established after the leader
	 * started.
	 *
	 * Note that ensuring all the potential target GUCs are at PGC_S_DEFAULT
	 * also ensures that set_config_option won't refuse to set them because of
	 * source-priority comparisons.
	 */
	for (i = 0; i < num_guc_variables; i++)
	{
		struct config_generic *gconf = guc_variables[i];

		/* Do nothing if non-shippable or if already at PGC_S_DEFAULT. */
		if (can_skip_gucvar(gconf))
			continue;

		/*
		 * We can use InitializeOneGUCOption to reset the GUC to default, but
		 * first we must free any existing subsidiary data to avoid leaking
		 * memory.  The stack must be empty, but we have to clean up all other
		 * fields.  Beware that there might be duplicate value or "extra"
		 * pointers.
		 */
		Assert(gconf->stack == NULL);
		if (gconf->extra)
			free(gconf->extra);
		if (gconf->last_reported)	/* probably can't happen */
			free(gconf->last_reported);
		if (gconf->sourcefile)
			free(gconf->sourcefile);
		switch (gconf->vartype)
		{
			case PGC_BOOL:
				{
					struct config_bool *conf = (struct config_bool *) gconf;

					if (conf->reset_extra && conf->reset_extra != gconf->extra)
						free(conf->reset_extra);
					break;
				}
			case PGC_INT:
				{
					struct config_int *conf = (struct config_int *) gconf;

					if (conf->reset_extra && conf->reset_extra != gconf->extra)
						free(conf->reset_extra);
					break;
				}
			case PGC_REAL:
				{
					struct config_real *conf = (struct config_real *) gconf;

					if (conf->reset_extra && conf->reset_extra != gconf->extra)
						free(conf->reset_extra);
					break;
				}
			case PGC_STRING:
				{
					struct config_string *conf = (struct config_string *) gconf;

					if (*conf->variable)
						free(*conf->variable);
					if (conf->reset_val && conf->reset_val != *conf->variable)
						free(conf->reset_val);
					if (conf->reset_extra && conf->reset_extra != gconf->extra)
						free(conf->reset_extra);
					break;
				}
			case PGC_ENUM:
				{
					struct config_enum *conf = (struct config_enum *) gconf;

					if (conf->reset_extra && conf->reset_extra != gconf->extra)
						free(conf->reset_extra);
					break;
				}
			case PGC_OID:
			{
				/* YB_TODO(alex@yugabyte)
				 * If PGC_OID is still needed, please implement this case in this function.
				 */
			}
		}
		/* Now we can reset the struct to PGS_S_DEFAULT state. */
		InitializeOneGUCOption(gconf);
	}

	/* First item is the length of the subsequent data */
	memcpy(&len, gucstate, sizeof(len));

	srcptr += sizeof(len);
	srcend = srcptr + len;

	/* If the GUC value check fails, we want errors to show useful context. */
	error_context_callback.callback = guc_restore_error_context_callback;
	error_context_callback.previous = error_context_stack;
	error_context_callback.arg = NULL;
	error_context_stack = &error_context_callback;

	/* Restore all the listed GUCs. */
	while (srcptr < srcend)
	{
		int			result;
		char	   *error_context_name_and_value[2];

		varname = read_gucstate(&srcptr, srcend);
		varvalue = read_gucstate(&srcptr, srcend);
		varsourcefile = read_gucstate(&srcptr, srcend);
		if (varsourcefile[0])
			read_gucstate_binary(&srcptr, srcend,
								 &varsourceline, sizeof(varsourceline));
		else
			varsourceline = 0;
		read_gucstate_binary(&srcptr, srcend,
							 &varsource, sizeof(varsource));
		read_gucstate_binary(&srcptr, srcend,
							 &varscontext, sizeof(varscontext));
		read_gucstate_binary(&srcptr, srcend,
							 &varsrole, sizeof(varsrole));

		error_context_name_and_value[0] = varname;
		error_context_name_and_value[1] = varvalue;
		error_context_callback.arg = &error_context_name_and_value[0];
		result = set_config_option_ext(varname, varvalue,
									   varscontext, varsource, varsrole,
									   GUC_ACTION_SET, true, ERROR, true);
		if (result <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("parameter \"%s\" could not be set", varname)));
		if (varsourcefile[0])
			set_config_sourcefile(varname, varsourcefile, varsourceline);
		error_context_callback.arg = NULL;
	}

	error_context_stack = error_context_callback.previous;
}

/*
 * A little "long argument" simulation, although not quite GNU
 * compliant. Takes a string of the form "some-option=some value" and
 * returns name = "some_option" and value = "some value" in malloc'ed
 * storage. Note that '-' is converted to '_' in the option name. If
 * there is no '=' in the input string then value will be NULL.
 */
void
ParseLongOption(const char *string, char **name, char **value)
{
	size_t		equal_pos;
	char	   *cp;

	AssertArg(string);
	AssertArg(name);
	AssertArg(value);

	equal_pos = strcspn(string, "=");

	if (string[equal_pos] == '=')
	{
		*name = guc_malloc(FATAL, equal_pos + 1);
		strlcpy(*name, string, equal_pos + 1);

		*value = guc_strdup(FATAL, &string[equal_pos + 1]);
	}
	else
	{
		/* no equal sign in string */
		*name = guc_strdup(FATAL, string);
		*value = NULL;
	}

	for (cp = *name; *cp; cp++)
		if (*cp == '-')
			*cp = '_';
}


/*
 * Handle options fetched from pg_db_role_setting.setconfig,
 * pg_proc.proconfig, etc.  Caller must specify proper context/source/action.
 *
 * The array parameter must be an array of TEXT (it must not be NULL).
 */
void
ProcessGUCArray(ArrayType *array,
				GucContext context, GucSource source, GucAction action)
{
	int			i;

	Assert(array != NULL);
	Assert(ARR_ELEMTYPE(array) == TEXTOID);
	Assert(ARR_NDIM(array) == 1);
	Assert(ARR_LBOUND(array)[0] == 1);

	for (i = 1; i <= ARR_DIMS(array)[0]; i++)
	{
		Datum		d;
		bool		isnull;
		char	   *s;
		char	   *name;
		char	   *value;
		char	   *namecopy;
		char	   *valuecopy;

		d = array_ref(array, 1, &i,
					  -1 /* varlenarray */ ,
					  -1 /* TEXT's typlen */ ,
					  false /* TEXT's typbyval */ ,
					  TYPALIGN_INT /* TEXT's typalign */ ,
					  &isnull);

		if (isnull)
			continue;

		s = TextDatumGetCString(d);

		ParseLongOption(s, &name, &value);
		if (!value)
		{
			ereport(WARNING,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("could not parse setting for parameter \"%s\"",
							name)));
			free(name);
			continue;
		}

		/* free malloc'd strings immediately to avoid leak upon error */
		namecopy = pstrdup(name);
		free(name);
		valuecopy = pstrdup(value);
		free(value);

		(void) set_config_option(namecopy, valuecopy,
								 context, source,
								 action, true, 0, false);

		pfree(namecopy);
		pfree(valuecopy);
		pfree(s);
	}
}


/*
 * Add an entry to an option array.  The array parameter may be NULL
 * to indicate the current table entry is NULL.
 */
ArrayType *
GUCArrayAdd(ArrayType *array, const char *name, const char *value)
{
	struct config_generic *record;
	Datum		datum;
	char	   *newval;
	ArrayType  *a;

	Assert(name);
	Assert(value);

	/* test if the option is valid and we're allowed to set it */
	(void) validate_option_array_item(name, value, false);

	/* normalize name (converts obsolete GUC names to modern spellings) */
	record = find_option(name, false, true, WARNING);
	if (record)
		name = record->name;

	/* build new item for array */
	newval = psprintf("%s=%s", name, value);
	datum = CStringGetTextDatum(newval);

	if (array)
	{
		int			index;
		bool		isnull;
		int			i;

		Assert(ARR_ELEMTYPE(array) == TEXTOID);
		Assert(ARR_NDIM(array) == 1);
		Assert(ARR_LBOUND(array)[0] == 1);

		index = ARR_DIMS(array)[0] + 1; /* add after end */

		for (i = 1; i <= ARR_DIMS(array)[0]; i++)
		{
			Datum		d;
			char	   *current;

			d = array_ref(array, 1, &i,
						  -1 /* varlenarray */ ,
						  -1 /* TEXT's typlen */ ,
						  false /* TEXT's typbyval */ ,
						  TYPALIGN_INT /* TEXT's typalign */ ,
						  &isnull);
			if (isnull)
				continue;
			current = TextDatumGetCString(d);

			/* check for match up through and including '=' */
			if (strncmp(current, newval, strlen(name) + 1) == 0)
			{
				index = i;
				break;
			}
		}

		a = array_set(array, 1, &index,
					  datum,
					  false,
					  -1 /* varlena array */ ,
					  -1 /* TEXT's typlen */ ,
					  false /* TEXT's typbyval */ ,
					  TYPALIGN_INT /* TEXT's typalign */ );
	}
	else
		a = construct_array(&datum, 1,
							TEXTOID,
							-1, false, TYPALIGN_INT);

	return a;
}


/*
 * Delete an entry from an option array.  The array parameter may be NULL
 * to indicate the current table entry is NULL.  Also, if the return value
 * is NULL then a null should be stored.
 */
ArrayType *
GUCArrayDelete(ArrayType *array, const char *name)
{
	struct config_generic *record;
	ArrayType  *newarray;
	int			i;
	int			index;

	Assert(name);

	/* test if the option is valid and we're allowed to set it */
	(void) validate_option_array_item(name, NULL, false);

	/* normalize name (converts obsolete GUC names to modern spellings) */
	record = find_option(name, false, true, WARNING);
	if (record)
		name = record->name;

	/* if array is currently null, then surely nothing to delete */
	if (!array)
		return NULL;

	newarray = NULL;
	index = 1;

	for (i = 1; i <= ARR_DIMS(array)[0]; i++)
	{
		Datum		d;
		char	   *val;
		bool		isnull;

		d = array_ref(array, 1, &i,
					  -1 /* varlenarray */ ,
					  -1 /* TEXT's typlen */ ,
					  false /* TEXT's typbyval */ ,
					  TYPALIGN_INT /* TEXT's typalign */ ,
					  &isnull);
		if (isnull)
			continue;
		val = TextDatumGetCString(d);

		/* ignore entry if it's what we want to delete */
		if (strncmp(val, name, strlen(name)) == 0
			&& val[strlen(name)] == '=')
			continue;

		/* else add it to the output array */
		if (newarray)
			newarray = array_set(newarray, 1, &index,
								 d,
								 false,
								 -1 /* varlenarray */ ,
								 -1 /* TEXT's typlen */ ,
								 false /* TEXT's typbyval */ ,
								 TYPALIGN_INT /* TEXT's typalign */ );
		else
			newarray = construct_array(&d, 1,
									   TEXTOID,
									   -1, false, TYPALIGN_INT);

		index++;
	}

	return newarray;
}


/*
 * Given a GUC array, delete all settings from it that our permission
 * level allows: if superuser, delete them all; if regular user, only
 * those that are PGC_USERSET or we have permission to set
 */
ArrayType *
GUCArrayReset(ArrayType *array)
{
	ArrayType  *newarray;
	int			i;
	int			index;

	/* if array is currently null, nothing to do */
	if (!array)
		return NULL;

	/* if we're superuser, we can delete everything, so just do it */
	if (superuser())
		return NULL;

	newarray = NULL;
	index = 1;

	for (i = 1; i <= ARR_DIMS(array)[0]; i++)
	{
		Datum		d;
		char	   *val;
		char	   *eqsgn;
		bool		isnull;

		d = array_ref(array, 1, &i,
					  -1 /* varlenarray */ ,
					  -1 /* TEXT's typlen */ ,
					  false /* TEXT's typbyval */ ,
					  TYPALIGN_INT /* TEXT's typalign */ ,
					  &isnull);
		if (isnull)
			continue;
		val = TextDatumGetCString(d);

		eqsgn = strchr(val, '=');
		*eqsgn = '\0';

		/* skip if we have permission to delete it */
		if (validate_option_array_item(val, NULL, true))
			continue;

		/* else add it to the output array */
		if (newarray)
			newarray = array_set(newarray, 1, &index,
								 d,
								 false,
								 -1 /* varlenarray */ ,
								 -1 /* TEXT's typlen */ ,
								 false /* TEXT's typbyval */ ,
								 TYPALIGN_INT /* TEXT's typalign */ );
		else
			newarray = construct_array(&d, 1,
									   TEXTOID,
									   -1, false, TYPALIGN_INT);

		index++;
		pfree(val);
	}

	return newarray;
}

/*
 * Validate a proposed option setting for GUCArrayAdd/Delete/Reset.
 *
 * name is the option name.  value is the proposed value for the Add case,
 * or NULL for the Delete/Reset cases.  If skipIfNoPermissions is true, it's
 * not an error to have no permissions to set the option.
 *
 * Returns true if OK, false if skipIfNoPermissions is true and user does not
 * have permission to change this option (all other error cases result in an
 * error being thrown).
 */
static bool
validate_option_array_item(const char *name, const char *value,
						   bool skipIfNoPermissions)

{
	struct config_generic *gconf;

	/*
	 * There are three cases to consider:
	 *
	 * name is a known GUC variable.  Check the value normally, check
	 * permissions normally (i.e., allow if variable is USERSET, or if it's
	 * SUSET and user is superuser or holds ACL_SET permissions).
	 *
	 * name is not known, but exists or can be created as a placeholder (i.e.,
	 * it has a valid custom name).  We allow this case if you're a superuser,
	 * otherwise not.  Superusers are assumed to know what they're doing. We
	 * can't allow it for other users, because when the placeholder is
	 * resolved it might turn out to be a SUSET variable.  (With currently
	 * available infrastructure, we can actually handle such cases within the
	 * current session --- but once an entry is made in pg_db_role_setting,
	 * it's assumed to be fully validated.)
	 *
	 * name is not known and can't be created as a placeholder.  Throw error,
	 * unless skipIfNoPermissions is true, in which case return false.
	 */
	gconf = find_option(name, true, skipIfNoPermissions, ERROR);
	if (!gconf)
	{
		/* not known, failed to make a placeholder */
		return false;
	}

	if (gconf->flags & GUC_CUSTOM_PLACEHOLDER)
	{
		/*
		 * We cannot do any meaningful check on the value, so only permissions
		 * are useful to check.
		 */
		if (superuser() ||
			pg_parameter_aclcheck(name, GetUserId(), ACL_SET) == ACLCHECK_OK)
			return true;
		if (skipIfNoPermissions)
			return false;
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to set parameter \"%s\"", name)));
	}

	/* manual permissions check so we can avoid an error being thrown */
	if (gconf->context == PGC_USERSET)
		 /* ok */ ;
	else if (gconf->context == PGC_SUSET &&
			 (superuser() ||
			  pg_parameter_aclcheck(name, GetUserId(), ACL_SET) == ACLCHECK_OK))
		 /* ok */ ;
	else if (skipIfNoPermissions)
		return false;
	/* if a permissions error should be thrown, let set_config_option do it */

	/* test for permissions and valid option value */
	(void) set_config_option(name, value,
							 superuser() ? PGC_SUSET : PGC_USERSET,
							 PGC_S_TEST, GUC_ACTION_SET, false, 0, false);

	return true;
}


/*
 * Called by check_hooks that want to override the normal
 * ERRCODE_INVALID_PARAMETER_VALUE SQLSTATE for check hook failures.
 *
 * Note that GUC_check_errmsg() etc are just macros that result in a direct
 * assignment to the associated variables.  That is ugly, but forced by the
 * limitations of C's macro mechanisms.
 */
void
GUC_check_errcode(int sqlerrcode)
{
	GUC_check_errcode_value = sqlerrcode;
}


/*
 * Convenience functions to manage calling a variable's check_hook.
 * These mostly take care of the protocol for letting check hooks supply
 * portions of the error report on failure.
 */

static bool
call_bool_check_hook(struct config_bool *conf, bool *newval, void **extra,
					 GucSource source, int elevel)
{
	/* Quick success if no hook */
	if (!conf->check_hook)
		return true;

	/* Reset variables that might be set by hook */
	GUC_check_errcode_value = ERRCODE_INVALID_PARAMETER_VALUE;
	GUC_check_errmsg_string = NULL;
	GUC_check_errdetail_string = NULL;
	GUC_check_errhint_string = NULL;

	if (!conf->check_hook(newval, extra, source))
	{
		ereport(elevel,
				(errcode(GUC_check_errcode_value),
				 GUC_check_errmsg_string ?
				 errmsg_internal("%s", GUC_check_errmsg_string) :
				 errmsg("invalid value for parameter \"%s\": %d",
						conf->gen.name, (int) *newval),
				 GUC_check_errdetail_string ?
				 errdetail_internal("%s", GUC_check_errdetail_string) : 0,
				 GUC_check_errhint_string ?
				 errhint("%s", GUC_check_errhint_string) : 0));
		/* Flush any strings created in ErrorContext */
		FlushErrorState();
		return false;
	}

	return true;
}

static bool
call_int_check_hook(struct config_int *conf, int *newval, void **extra,
					GucSource source, int elevel)
{
	/* Quick success if no hook */
	if (!conf->check_hook)
		return true;

	/* Reset variables that might be set by hook */
	GUC_check_errcode_value = ERRCODE_INVALID_PARAMETER_VALUE;
	GUC_check_errmsg_string = NULL;
	GUC_check_errdetail_string = NULL;
	GUC_check_errhint_string = NULL;

	if (!conf->check_hook(newval, extra, source))
	{
		ereport(elevel,
				(errcode(GUC_check_errcode_value),
				 GUC_check_errmsg_string ?
				 errmsg_internal("%s", GUC_check_errmsg_string) :
				 errmsg("invalid value for parameter \"%s\": %d",
						conf->gen.name, *newval),
				 GUC_check_errdetail_string ?
				 errdetail_internal("%s", GUC_check_errdetail_string) : 0,
				 GUC_check_errhint_string ?
				 errhint("%s", GUC_check_errhint_string) : 0));
		/* Flush any strings created in ErrorContext */
		FlushErrorState();
		return false;
	}

	return true;
}

static bool
call_oid_check_hook(struct config_oid *conf, Oid *newval, void **extra,
					GucSource source, int elevel)
{
	/* Quick success if no hook */
	if (!conf->check_hook)
		return true;

	/* Reset variables that might be set by hook */
	GUC_check_errcode_value = ERRCODE_INVALID_PARAMETER_VALUE;
	GUC_check_errmsg_string = NULL;
	GUC_check_errdetail_string = NULL;
	GUC_check_errhint_string = NULL;

	if (!conf->check_hook(newval, extra, source))
	{
		ereport(elevel,
				(errcode(GUC_check_errcode_value),
				 GUC_check_errmsg_string ?
				 errmsg_internal("%s", GUC_check_errmsg_string) :
				 errmsg("invalid value for parameter \"%s\": %u",
						conf->gen.name, *newval),
				 GUC_check_errdetail_string ?
				 errdetail_internal("%s", GUC_check_errdetail_string) : 0,
				 GUC_check_errhint_string ?
				 errhint("%s", GUC_check_errhint_string) : 0));
		/* Flush any strings created in ErrorContext */
		FlushErrorState();
		return false;
	}

	return true;
}

static bool
call_real_check_hook(struct config_real *conf, double *newval, void **extra,
					 GucSource source, int elevel)
{
	/* Quick success if no hook */
	if (!conf->check_hook)
		return true;

	/* Reset variables that might be set by hook */
	GUC_check_errcode_value = ERRCODE_INVALID_PARAMETER_VALUE;
	GUC_check_errmsg_string = NULL;
	GUC_check_errdetail_string = NULL;
	GUC_check_errhint_string = NULL;

	if (!conf->check_hook(newval, extra, source))
	{
		ereport(elevel,
				(errcode(GUC_check_errcode_value),
				 GUC_check_errmsg_string ?
				 errmsg_internal("%s", GUC_check_errmsg_string) :
				 errmsg("invalid value for parameter \"%s\": %g",
						conf->gen.name, *newval),
				 GUC_check_errdetail_string ?
				 errdetail_internal("%s", GUC_check_errdetail_string) : 0,
				 GUC_check_errhint_string ?
				 errhint("%s", GUC_check_errhint_string) : 0));
		/* Flush any strings created in ErrorContext */
		FlushErrorState();
		return false;
	}

	return true;
}

static bool
call_string_check_hook(struct config_string *conf, char **newval, void **extra,
					   GucSource source, int elevel)
{
	volatile bool result = true;

	/* Quick success if no hook */
	if (!conf->check_hook)
		return true;

	/*
	 * If elevel is ERROR, or if the check_hook itself throws an elog
	 * (undesirable, but not always avoidable), make sure we don't leak the
	 * already-malloc'd newval string.
	 */
	PG_TRY();
	{
		/* Reset variables that might be set by hook */
		GUC_check_errcode_value = ERRCODE_INVALID_PARAMETER_VALUE;
		GUC_check_errmsg_string = NULL;
		GUC_check_errdetail_string = NULL;
		GUC_check_errhint_string = NULL;

		if (!conf->check_hook(newval, extra, source))
		{
			ereport(elevel,
					(errcode(GUC_check_errcode_value),
					 GUC_check_errmsg_string ?
					 errmsg_internal("%s", GUC_check_errmsg_string) :
					 errmsg("invalid value for parameter \"%s\": \"%s\"",
							conf->gen.name, *newval ? *newval : ""),
					 GUC_check_errdetail_string ?
					 errdetail_internal("%s", GUC_check_errdetail_string) : 0,
					 GUC_check_errhint_string ?
					 errhint("%s", GUC_check_errhint_string) : 0));
			/* Flush any strings created in ErrorContext */
			FlushErrorState();
			result = false;
		}
	}
	PG_CATCH();
	{
		free(*newval);
		PG_RE_THROW();
	}
	PG_END_TRY();

	return result;
}

static bool
call_enum_check_hook(struct config_enum *conf, int *newval, void **extra,
					 GucSource source, int elevel)
{
	/* Quick success if no hook */
	if (!conf->check_hook)
		return true;

	/* Reset variables that might be set by hook */
	GUC_check_errcode_value = ERRCODE_INVALID_PARAMETER_VALUE;
	GUC_check_errmsg_string = NULL;
	GUC_check_errdetail_string = NULL;
	GUC_check_errhint_string = NULL;

	if (!conf->check_hook(newval, extra, source))
	{
		ereport(elevel,
				(errcode(GUC_check_errcode_value),
				 GUC_check_errmsg_string ?
				 errmsg_internal("%s", GUC_check_errmsg_string) :
				 errmsg("invalid value for parameter \"%s\": \"%s\"",
						conf->gen.name,
						config_enum_lookup_by_value(conf, *newval)),
				 GUC_check_errdetail_string ?
				 errdetail_internal("%s", GUC_check_errdetail_string) : 0,
				 GUC_check_errhint_string ?
				 errhint("%s", GUC_check_errhint_string) : 0));
		/* Flush any strings created in ErrorContext */
		FlushErrorState();
		return false;
	}

	return true;
}


/*
 * check_hook, assign_hook and show_hook subroutines
 */

static bool
check_wal_consistency_checking(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	bool		newwalconsistency[RM_MAX_ID + 1];

	/* Initialize the array */
	MemSet(newwalconsistency, 0, (RM_MAX_ID + 1) * sizeof(bool));

	/* Need a modifiable copy of string */
	rawstring = pstrdup(*newval);

	/* Parse string into list of identifiers */
	if (!SplitIdentifierString(rawstring, ',', &elemlist))
	{
		/* syntax error in list */
		GUC_check_errdetail("List syntax is invalid.");
		pfree(rawstring);
		list_free(elemlist);
		return false;
	}

	foreach(l, elemlist)
	{
		char	   *tok = (char *) lfirst(l);
		bool		found = false;
		int			rmid;

		/* Check for 'all'. */
		if (pg_strcasecmp(tok, "all") == 0)
		{
			for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
				if (RmgrIdExists(rmid) && GetRmgr(rmid).rm_mask != NULL)
					newwalconsistency[rmid] = true;
			found = true;
		}
		else
		{
			/*
			 * Check if the token matches with any individual resource
			 * manager.
			 */
			for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
			{
				if (RmgrIdExists(rmid) && GetRmgr(rmid).rm_mask != NULL &&
					pg_strcasecmp(tok, GetRmgr(rmid).rm_name) == 0)
				{
					newwalconsistency[rmid] = true;
					found = true;
				}
			}
		}

		/* If a valid resource manager is found, check for the next one. */
		if (!found)
		{
			/*
			 * Perhaps it's a custom resource manager. If so, defer checking
			 * until InitializeWalConsistencyChecking().
			 */
			if (!process_shared_preload_libraries_done)
			{
				check_wal_consistency_checking_deferred = true;
			}
			else
			{
				GUC_check_errdetail("Unrecognized key word: \"%s\".", tok);
				pfree(rawstring);
				list_free(elemlist);
				return false;
			}
		}
	}

	pfree(rawstring);
	list_free(elemlist);

	/* assign new value */
	*extra = guc_malloc(ERROR, (RM_MAX_ID + 1) * sizeof(bool));
	memcpy(*extra, newwalconsistency, (RM_MAX_ID + 1) * sizeof(bool));
	return true;
}

static bool check_default_replica_identity(char **newval, void **extra, GucSource source)
{
	char* rawstring;
	bool is_valid;

	rawstring = pstrdup(*newval);
	is_valid = strcmp(rawstring, "FULL") == 0 ||
			strcmp(rawstring, "DEFAULT") == 0 ||
			strcmp(rawstring, "NOTHING") == 0 ||
			strcmp(rawstring, "CHANGE") == 0;

	pfree(rawstring);
	return is_valid;
}

static void
assign_wal_consistency_checking(const char *newval, void *extra)
{
	/*
	 * If some checks were deferred, it's possible that the checks will fail
	 * later during InitializeWalConsistencyChecking(). But in that case, the
	 * postmaster will exit anyway, so it's safe to proceed with the
	 * assignment.
	 *
	 * Any built-in resource managers specified are assigned immediately,
	 * which affects WAL created before shared_preload_libraries are
	 * processed. Any custom resource managers specified won't be assigned
	 * until after shared_preload_libraries are processed, but that's OK
	 * because WAL for a custom resource manager can't be written before the
	 * module is loaded anyway.
	 */
	wal_consistency_checking = extra;
}

static bool
check_log_destination(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	int			newlogdest = 0;
	int		   *myextra;

	/* Need a modifiable copy of string */
	rawstring = pstrdup(*newval);

	/* Parse string into list of identifiers */
	if (!SplitIdentifierString(rawstring, ',', &elemlist))
	{
		/* syntax error in list */
		GUC_check_errdetail("List syntax is invalid.");
		pfree(rawstring);
		list_free(elemlist);
		return false;
	}

	foreach(l, elemlist)
	{
		char	   *tok = (char *) lfirst(l);

		if (pg_strcasecmp(tok, "stderr") == 0)
			newlogdest |= LOG_DESTINATION_STDERR;
		else if (pg_strcasecmp(tok, "csvlog") == 0)
			newlogdest |= LOG_DESTINATION_CSVLOG;
		else if (pg_strcasecmp(tok, "jsonlog") == 0)
			newlogdest |= LOG_DESTINATION_JSONLOG;
#ifdef HAVE_SYSLOG
		else if (pg_strcasecmp(tok, "syslog") == 0)
			newlogdest |= LOG_DESTINATION_SYSLOG;
#endif
#ifdef WIN32
		else if (pg_strcasecmp(tok, "eventlog") == 0)
			newlogdest |= LOG_DESTINATION_EVENTLOG;
#endif
		else
		{
			GUC_check_errdetail("Unrecognized key word: \"%s\".", tok);
			pfree(rawstring);
			list_free(elemlist);
			return false;
		}
	}

	pfree(rawstring);
	list_free(elemlist);

	myextra = (int *) guc_malloc(ERROR, sizeof(int));
	*myextra = newlogdest;
	*extra = (void *) myextra;

	return true;
}

static void
assign_log_destination(const char *newval, void *extra)
{
	Log_destination = *((int *) extra);
}

static void
assign_syslog_facility(int newval, void *extra)
{
#ifdef HAVE_SYSLOG
	set_syslog_parameters(syslog_ident_str ? syslog_ident_str : "postgres",
						  newval);
#endif
	/* Without syslog support, just ignore it */
}

static void
assign_syslog_ident(const char *newval, void *extra)
{
#ifdef HAVE_SYSLOG
	set_syslog_parameters(newval, syslog_facility);
#endif
	/* Without syslog support, it will always be set to "none", so ignore */
}


static void
assign_session_replication_role(int newval, void *extra)
{
	/*
	 * Must flush the plan cache when changing replication role; but don't
	 * flush unnecessarily.
	 */
	if (SessionReplicationRole != newval)
		ResetPlanCache();
}

static bool
check_temp_buffers(int *newval, void **extra, GucSource source)
{
	/*
	 * Once local buffers have been initialized, it's too late to change this.
	 * However, if this is only a test call, allow it.
	 */
	if (source != PGC_S_TEST && NLocBuffer && NLocBuffer != *newval)
	{
		GUC_check_errdetail("\"temp_buffers\" cannot be changed after any temporary tables have been accessed in the session.");
		return false;
	}
	return true;
}

static bool
check_bonjour(bool *newval, void **extra, GucSource source)
{
#ifndef USE_BONJOUR
	if (*newval)
	{
		GUC_check_errmsg("Bonjour is not supported by this build");
		return false;
	}
#endif
	return true;
}

static bool
check_ssl(bool *newval, void **extra, GucSource source)
{
#ifndef USE_SSL
	if (*newval)
	{
		GUC_check_errmsg("SSL is not supported by this build");
		return false;
	}
#endif
	return true;
}

static bool
check_stage_log_stats(bool *newval, void **extra, GucSource source)
{
	if (*newval && log_statement_stats)
	{
		GUC_check_errdetail("Cannot enable parameter when \"log_statement_stats\" is true.");
		return false;
	}
	return true;
}

static bool
check_log_stats(bool *newval, void **extra, GucSource source)
{
	if (*newval &&
		(log_parser_stats || log_planner_stats || log_executor_stats))
	{
		GUC_check_errdetail("Cannot enable \"log_statement_stats\" when "
							"\"log_parser_stats\", \"log_planner_stats\", "
							"or \"log_executor_stats\" is true.");
		return false;
	}
	return true;
}

static bool
check_canonical_path(char **newval, void **extra, GucSource source)
{
	/*
	 * Since canonicalize_path never enlarges the string, we can just modify
	 * newval in-place.  But watch out for NULL, which is the default value
	 * for external_pid_file.
	 */
	if (*newval)
		canonicalize_path(*newval);
	return true;
}

static bool
check_timezone_abbreviations(char **newval, void **extra, GucSource source)
{
	/*
	 * The boot_val given above for timezone_abbreviations is NULL. When we
	 * see this we just do nothing.  If this value isn't overridden from the
	 * config file then pg_timezone_abbrev_initialize() will eventually
	 * replace it with "Default".  This hack has two purposes: to avoid
	 * wasting cycles loading values that might soon be overridden from the
	 * config file, and to avoid trying to read the timezone abbrev files
	 * during InitializeGUCOptions().  The latter doesn't work in an
	 * EXEC_BACKEND subprocess because my_exec_path hasn't been set yet and so
	 * we can't locate PGSHAREDIR.
	 */
	if (*newval == NULL)
	{
		Assert(source == PGC_S_DEFAULT);
		return true;
	}

	/* OK, load the file and produce a malloc'd TimeZoneAbbrevTable */
	*extra = load_tzoffsets(*newval);

	/* tzparser.c returns NULL on failure, reporting via GUC_check_errmsg */
	if (!*extra)
		return false;

	return true;
}

static void
assign_timezone_abbreviations(const char *newval, void *extra)
{
	/* Do nothing for the boot_val default of NULL */
	if (!extra)
		return;

	InstallTimeZoneAbbrevs((TimeZoneAbbrevTable *) extra);
}

/*
 * pg_timezone_abbrev_initialize --- set default value if not done already
 *
 * This is called after initial loading of postgresql.conf.  If no
 * timezone_abbreviations setting was found therein, select default.
 * If a non-default value is already installed, nothing will happen.
 *
 * This can also be called from ProcessConfigFile to establish the default
 * value after a postgresql.conf entry for it is removed.
 */
static void
pg_timezone_abbrev_initialize(void)
{
	SetConfigOption("timezone_abbreviations", "Default",
					PGC_POSTMASTER, PGC_S_DYNAMIC_DEFAULT);
}

static const char *
show_archive_command(void)
{
	if (XLogArchivingActive())
		return XLogArchiveCommand;
	else
		return "(disabled)";
}

static void
assign_tcp_keepalives_idle(int newval, void *extra)
{
	/*
	 * The kernel API provides no way to test a value without setting it; and
	 * once we set it we might fail to unset it.  So there seems little point
	 * in fully implementing the check-then-assign GUC API for these
	 * variables.  Instead we just do the assignment on demand.  pqcomm.c
	 * reports any problems via ereport(LOG).
	 *
	 * This approach means that the GUC value might have little to do with the
	 * actual kernel value, so we use a show_hook that retrieves the kernel
	 * value rather than trusting GUC's copy.
	 */
	(void) pq_setkeepalivesidle(newval, MyProcPort);
}

static const char *
show_tcp_keepalives_idle(void)
{
	/* See comments in assign_tcp_keepalives_idle */
	static char nbuf[16];

	snprintf(nbuf, sizeof(nbuf), "%d", pq_getkeepalivesidle(MyProcPort));
	return nbuf;
}

static void
assign_tcp_keepalives_interval(int newval, void *extra)
{
	/* See comments in assign_tcp_keepalives_idle */
	(void) pq_setkeepalivesinterval(newval, MyProcPort);
}

static const char *
show_tcp_keepalives_interval(void)
{
	/* See comments in assign_tcp_keepalives_idle */
	static char nbuf[16];

	snprintf(nbuf, sizeof(nbuf), "%d", pq_getkeepalivesinterval(MyProcPort));
	return nbuf;
}

static void
assign_tcp_keepalives_count(int newval, void *extra)
{
	/* See comments in assign_tcp_keepalives_idle */
	(void) pq_setkeepalivescount(newval, MyProcPort);
}

static const char *
show_tcp_keepalives_count(void)
{
	/* See comments in assign_tcp_keepalives_idle */
	static char nbuf[16];

	snprintf(nbuf, sizeof(nbuf), "%d", pq_getkeepalivescount(MyProcPort));
	return nbuf;
}

static void
assign_tcp_user_timeout(int newval, void *extra)
{
	/* See comments in assign_tcp_keepalives_idle */
	(void) pq_settcpusertimeout(newval, MyProcPort);
}

static const char *
show_tcp_user_timeout(void)
{
	/* See comments in assign_tcp_keepalives_idle */
	static char nbuf[16];

	snprintf(nbuf, sizeof(nbuf), "%d", pq_gettcpusertimeout(MyProcPort));
	return nbuf;
}

static bool
check_yb_explicit_row_locking_batch_size(int *newval, void **extra, GucSource source)
{
	return *newval > 0;
}

static bool
check_maxconnections(int *newval, void **extra, GucSource source)
{
	if (*newval + autovacuum_max_workers + 1 +
		max_worker_processes + max_wal_senders > MAX_BACKENDS)
		return false;
	return true;
}

/*
 * For YB-managed (cloud), the cloud user won't be aware of superuser.
 * When YB shows max_connections, the connections reserved for superusers (and
 * other backends) are hidden from cloud users.
 * The reference of the relations can be found in postmaster.c.
 */
static const char *
yb_show_maxconnections(void)
{
	static char buf[32];

	int64 yb_adj_max_con = MaxConnections;
	if (IsYugaByteEnabled() && !superuser())
	{
		yb_adj_max_con -= (ReservedBackends + max_wal_senders);
	}

	snprintf(buf, sizeof(buf), INT64_FORMAT, yb_adj_max_con);
	return buf;
}

static bool
check_autovacuum_max_workers(int *newval, void **extra, GucSource source)
{
	if (MaxConnections + *newval + 1 +
		max_worker_processes + max_wal_senders > MAX_BACKENDS)
		return false;
	return true;
}

static bool
check_max_wal_senders(int *newval, void **extra, GucSource source)
{
	if (MaxConnections + autovacuum_max_workers + 1 +
		max_worker_processes + *newval > MAX_BACKENDS)
		return false;
	return true;
}

static bool
check_autovacuum_work_mem(int *newval, void **extra, GucSource source)
{
	/*
	 * -1 indicates fallback.
	 *
	 * If we haven't yet changed the boot_val default of -1, just let it be.
	 * Autovacuum will look to maintenance_work_mem instead.
	 */
	if (*newval == -1)
		return true;

	/*
	 * We clamp manually-set values to at least 1MB.  Since
	 * maintenance_work_mem is always set to at least this value, do the same
	 * here.
	 */
	if (*newval < 1024)
		*newval = 1024;

	return true;
}

static bool
check_max_worker_processes(int *newval, void **extra, GucSource source)
{
	if (MaxConnections + autovacuum_max_workers + 1 +
		*newval + max_wal_senders > MAX_BACKENDS)
		return false;
	return true;
}

static bool
check_effective_io_concurrency(int *newval, void **extra, GucSource source)
{
#ifndef USE_PREFETCH
	if (*newval != 0)
	{
		GUC_check_errdetail("effective_io_concurrency must be set to 0 on platforms that lack posix_fadvise().");
		return false;
	}
#endif							/* USE_PREFETCH */
	return true;
}

static bool
check_maintenance_io_concurrency(int *newval, void **extra, GucSource source)
{
#ifndef USE_PREFETCH
	if (*newval != 0)
	{
		GUC_check_errdetail("maintenance_io_concurrency must be set to 0 on platforms that lack posix_fadvise().");
		return false;
	}
#endif							/* USE_PREFETCH */
	return true;
}

static bool
check_huge_page_size(int *newval, void **extra, GucSource source)
{
#if !(defined(MAP_HUGE_MASK) && defined(MAP_HUGE_SHIFT))
	/* Recent enough Linux only, for now.  See GetHugePageSize(). */
	if (*newval != 0)
	{
		GUC_check_errdetail("huge_page_size must be 0 on this platform.");
		return false;
	}
#endif
	return true;
}

static bool
check_client_connection_check_interval(int *newval, void **extra, GucSource source)
{
	if (!WaitEventSetCanReportClosed() && *newval != 0)
	{
		GUC_check_errdetail("client_connection_check_interval must be set to 0 on this platform.");
		return false;
	}
	return true;
}

static void
assign_maintenance_io_concurrency(int newval, void *extra)
{
#ifdef USE_PREFETCH
	/*
	 * Reconfigure recovery prefetching, because a setting it depends on
	 * changed.
	 */
	maintenance_io_concurrency = newval;
	if (AmStartupProcess())
		XLogPrefetchReconfigure();
#endif
}

static bool
check_application_name(char **newval, void **extra, GucSource source)
{
	/* Only allow clean ASCII chars in the application name */
	pg_clean_ascii(*newval);

	return true;
}

static void
assign_application_name(const char *newval, void *extra)
{
	/* Update the pg_stat_activity view */
	pgstat_report_appname(newval);
}

static bool
check_cluster_name(char **newval, void **extra, GucSource source)
{
	/* Only allow clean ASCII chars in the cluster name */
	pg_clean_ascii(*newval);

	return true;
}

static const char *
show_unix_socket_permissions(void)
{
	static char buf[12];

	snprintf(buf, sizeof(buf), "%04o", Unix_socket_permissions);
	return buf;
}

static const char *
show_log_file_mode(void)
{
	static char buf[12];

	snprintf(buf, sizeof(buf), "%04o", Log_file_mode);
	return buf;
}

static const char *
show_data_directory_mode(void)
{
	static char buf[12];

	snprintf(buf, sizeof(buf), "%04o", data_directory_mode);
	return buf;
}

static const char *
show_in_hot_standby(void)
{
	/*
	 * We display the actual state based on shared memory, so that this GUC
	 * reports up-to-date state if examined intra-query.  The underlying
	 * variable in_hot_standby changes only when we transmit a new value to
	 * the client.
	 */
	return RecoveryInProgress() ? "on" : "off";
}

/*
 * We split the input string, where commas separate function names
 * and certain whitespace chars are ignored, into a \0-separated (and
 * \0\0-terminated) list of function names.  This formulation allows
 * easy scanning when an error is thrown while avoiding the use of
 * non-reentrant strtok(), as well as keeping the output data in a
 * single palloc() chunk.
 */
static bool
check_backtrace_functions(char **newval, void **extra, GucSource source)
{
	int			newvallen = strlen(*newval);
	char	   *someval;
	int			validlen;
	int			i;
	int			j;

	/*
	 * Allow characters that can be C identifiers and commas as separators, as
	 * well as some whitespace for readability.
	 */
	validlen = strspn(*newval,
					  "0123456789_"
					  "abcdefghijklmnopqrstuvwxyz"
					  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					  ", \n\t");
	if (validlen != newvallen)
	{
		GUC_check_errdetail("invalid character");
		return false;
	}

	if (*newval[0] == '\0')
	{
		*extra = NULL;
		return true;
	}

	/*
	 * Allocate space for the output and create the copy.  We could discount
	 * whitespace chars to save some memory, but it doesn't seem worth the
	 * trouble.
	 */
	someval = guc_malloc(ERROR, newvallen + 1 + 1);
	for (i = 0, j = 0; i < newvallen; i++)
	{
		if ((*newval)[i] == ',')
			someval[j++] = '\0';	/* next item */
		else if ((*newval)[i] == ' ' ||
				 (*newval)[i] == '\n' ||
				 (*newval)[i] == '\t')
			;					/* ignore these */
		else
			someval[j++] = (*newval)[i];	/* copy anything else */
	}

	/* two \0s end the setting */
	someval[j] = '\0';
	someval[j + 1] = '\0';

	*extra = someval;
	return true;
}

static void
assign_backtrace_functions(const char *newval, void *extra)
{
	backtrace_symbol_list = (char *) extra;
}

static bool
check_recovery_target_timeline(char **newval, void **extra, GucSource source)
{
	RecoveryTargetTimeLineGoal rttg;
	RecoveryTargetTimeLineGoal *myextra;

	if (strcmp(*newval, "current") == 0)
		rttg = RECOVERY_TARGET_TIMELINE_CONTROLFILE;
	else if (strcmp(*newval, "latest") == 0)
		rttg = RECOVERY_TARGET_TIMELINE_LATEST;
	else
	{
		rttg = RECOVERY_TARGET_TIMELINE_NUMERIC;

		errno = 0;
		strtoul(*newval, NULL, 0);
		if (errno == EINVAL || errno == ERANGE)
		{
			GUC_check_errdetail("recovery_target_timeline is not a valid number.");
			return false;
		}
	}

	myextra = (RecoveryTargetTimeLineGoal *) guc_malloc(ERROR, sizeof(RecoveryTargetTimeLineGoal));
	*myextra = rttg;
	*extra = (void *) myextra;

	return true;
}

static void
assign_recovery_target_timeline(const char *newval, void *extra)
{
	recoveryTargetTimeLineGoal = *((RecoveryTargetTimeLineGoal *) extra);
	if (recoveryTargetTimeLineGoal == RECOVERY_TARGET_TIMELINE_NUMERIC)
		recoveryTargetTLIRequested = (TimeLineID) strtoul(newval, NULL, 0);
	else
		recoveryTargetTLIRequested = 0;
}

/*
 * Recovery target settings: Only one of the several recovery_target* settings
 * may be set.  Setting a second one results in an error.  The global variable
 * recoveryTarget tracks which kind of recovery target was chosen.  Other
 * variables store the actual target value (for example a string or a xid).
 * The assign functions of the parameters check whether a competing parameter
 * was already set.  But we want to allow setting the same parameter multiple
 * times.  We also want to allow unsetting a parameter and setting a different
 * one, so we unset recoveryTarget when the parameter is set to an empty
 * string.
 */

static void
pg_attribute_noreturn()
error_multiple_recovery_targets(void)
{
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("multiple recovery targets specified"),
			 errdetail("At most one of recovery_target, recovery_target_lsn, recovery_target_name, recovery_target_time, recovery_target_xid may be set.")));
}

static bool
check_recovery_target(char **newval, void **extra, GucSource source)
{
	if (strcmp(*newval, "immediate") != 0 && strcmp(*newval, "") != 0)
	{
		GUC_check_errdetail("The only allowed value is \"immediate\".");
		return false;
	}
	return true;
}

static void
assign_recovery_target(const char *newval, void *extra)
{
	if (recoveryTarget != RECOVERY_TARGET_UNSET &&
		recoveryTarget != RECOVERY_TARGET_IMMEDIATE)
		error_multiple_recovery_targets();

	if (newval && strcmp(newval, "") != 0)
		recoveryTarget = RECOVERY_TARGET_IMMEDIATE;
	else
		recoveryTarget = RECOVERY_TARGET_UNSET;
}

static bool
check_recovery_target_xid(char **newval, void **extra, GucSource source)
{
	if (strcmp(*newval, "") != 0)
	{
		TransactionId xid;
		TransactionId *myextra;

		errno = 0;
		xid = (TransactionId) strtou64(*newval, NULL, 0);
		if (errno == EINVAL || errno == ERANGE)
			return false;

		myextra = (TransactionId *) guc_malloc(ERROR, sizeof(TransactionId));
		*myextra = xid;
		*extra = (void *) myextra;
	}
	return true;
}

static void
assign_recovery_target_xid(const char *newval, void *extra)
{
	if (recoveryTarget != RECOVERY_TARGET_UNSET &&
		recoveryTarget != RECOVERY_TARGET_XID)
		error_multiple_recovery_targets();

	if (newval && strcmp(newval, "") != 0)
	{
		recoveryTarget = RECOVERY_TARGET_XID;
		recoveryTargetXid = *((TransactionId *) extra);
	}
	else
		recoveryTarget = RECOVERY_TARGET_UNSET;
}

/*
 * The interpretation of the recovery_target_time string can depend on the
 * time zone setting, so we need to wait until after all GUC processing is
 * done before we can do the final parsing of the string.  This check function
 * only does a parsing pass to catch syntax errors, but we store the string
 * and parse it again when we need to use it.
 */
static bool
check_recovery_target_time(char **newval, void **extra, GucSource source)
{
	if (strcmp(*newval, "") != 0)
	{
		/* reject some special values */
		if (strcmp(*newval, "now") == 0 ||
			strcmp(*newval, "today") == 0 ||
			strcmp(*newval, "tomorrow") == 0 ||
			strcmp(*newval, "yesterday") == 0)
		{
			return false;
		}

		/*
		 * parse timestamp value (see also timestamptz_in())
		 */
		{
			char	   *str = *newval;
			fsec_t		fsec;
			struct pg_tm tt,
					   *tm = &tt;
			int			tz;
			int			dtype;
			int			nf;
			int			dterr;
			char	   *field[MAXDATEFIELDS];
			int			ftype[MAXDATEFIELDS];
			char		workbuf[MAXDATELEN + MAXDATEFIELDS];
			TimestampTz timestamp;

			dterr = ParseDateTime(str, workbuf, sizeof(workbuf),
								  field, ftype, MAXDATEFIELDS, &nf);
			if (dterr == 0)
				dterr = DecodeDateTime(field, ftype, nf, &dtype, tm, &fsec, &tz);
			if (dterr != 0)
				return false;
			if (dtype != DTK_DATE)
				return false;

			if (tm2timestamp(tm, fsec, &tz, &timestamp) != 0)
			{
				GUC_check_errdetail("timestamp out of range: \"%s\"", str);
				return false;
			}
		}
	}
	return true;
}

static void
assign_recovery_target_time(const char *newval, void *extra)
{
	if (recoveryTarget != RECOVERY_TARGET_UNSET &&
		recoveryTarget != RECOVERY_TARGET_TIME)
		error_multiple_recovery_targets();

	if (newval && strcmp(newval, "") != 0)
		recoveryTarget = RECOVERY_TARGET_TIME;
	else
		recoveryTarget = RECOVERY_TARGET_UNSET;
}

static bool
check_recovery_target_name(char **newval, void **extra, GucSource source)
{
	/* Use the value of newval directly */
	if (strlen(*newval) >= MAXFNAMELEN)
	{
		GUC_check_errdetail("%s is too long (maximum %d characters).",
							"recovery_target_name", MAXFNAMELEN - 1);
		return false;
	}
	return true;
}

static void
assign_recovery_target_name(const char *newval, void *extra)
{
	if (recoveryTarget != RECOVERY_TARGET_UNSET &&
		recoveryTarget != RECOVERY_TARGET_NAME)
		error_multiple_recovery_targets();

	if (newval && strcmp(newval, "") != 0)
	{
		recoveryTarget = RECOVERY_TARGET_NAME;
		recoveryTargetName = newval;
	}
	else
		recoveryTarget = RECOVERY_TARGET_UNSET;
}

static bool
check_recovery_target_lsn(char **newval, void **extra, GucSource source)
{
	if (strcmp(*newval, "") != 0)
	{
		XLogRecPtr	lsn;
		XLogRecPtr *myextra;
		bool		have_error = false;

		lsn = pg_lsn_in_internal(*newval, &have_error);
		if (have_error)
			return false;

		myextra = (XLogRecPtr *) guc_malloc(ERROR, sizeof(XLogRecPtr));
		*myextra = lsn;
		*extra = (void *) myextra;
	}
	return true;
}

static void
assign_recovery_target_lsn(const char *newval, void *extra)
{
	if (recoveryTarget != RECOVERY_TARGET_UNSET &&
		recoveryTarget != RECOVERY_TARGET_LSN)
		error_multiple_recovery_targets();

	if (newval && strcmp(newval, "") != 0)
	{
		recoveryTarget = RECOVERY_TARGET_LSN;
		recoveryTargetLSN = *((XLogRecPtr *) extra);
	}
	else
		recoveryTarget = RECOVERY_TARGET_UNSET;
}

static bool
check_primary_slot_name(char **newval, void **extra, GucSource source)
{
	if (*newval && strcmp(*newval, "") != 0 &&
		!ReplicationSlotValidateName(*newval, WARNING))
		return false;

	return true;
}

static bool
check_default_with_oids(bool *newval, void **extra, GucSource source)
{
	if (*newval)
	{
		/* check the GUC's definition for an explanation */
		GUC_check_errcode(ERRCODE_FEATURE_NOT_SUPPORTED);
		GUC_check_errmsg("tables declared WITH OIDS are not supported");

		return false;
	}

	return true;
}

static bool
check_transaction_priority_lower_bound(double *newval, void **extra, GucSource source)
{
	if (*newval > yb_transaction_priority_upper_bound) {
		GUC_check_errdetail("must be less than or equal to yb_transaction_priority_upper_bound (%f).",
							yb_transaction_priority_upper_bound);
		return false;
	}

	if (IsYBReadCommitted() || YBIsWaitQueueEnabled())
	{
		ereport(NOTICE,
						(errmsg("priorities don't exist for read committed isolation transations, the "
										"transaction will wait for conflicting transactions to commit before "
										"proceeding"),
						 errdetail("this also applies to other isolation levels if using Wait-on-Conflict "
											"concurrency control")));
	}
	return true;
}

static bool
check_transaction_priority_upper_bound(double *newval, void **extra, GucSource source)
{
	if (*newval < yb_transaction_priority_lower_bound) {
		GUC_check_errdetail("must be greater than or equal to yb_transaction_priority_lower_bound (%f).",
							yb_transaction_priority_lower_bound);
		return false;
	}

	if (IsYBReadCommitted() || YBIsWaitQueueEnabled())
	{
		ereport(NOTICE,
						(errmsg("priorities don't exist for read committed isolation transations, the "
										"transaction will wait for conflicting transactions to commit before "
										"proceeding"),
						 errdetail("this also applies to other isolation levels if using Wait-on-Conflict "
											"concurrency control")));
	}
	return true;
}

static void
assign_yb_pg_batch_detection_mechanism(int new_value, void *extra)
{
	yb_pg_batch_detection_mechanism = new_value;
}

static void
assign_ysql_upgrade_mode(bool newval, void *extra)
{
	/*
	 * YSQL upgrade mode also enables/disables allowSystemTableMods.
	 * Note that PG doesn't allow user to change it at runtime.
	 *
	 * While we reuse allowSystemTableMods to cut some corners for
	 * YSQL upgrade, we do alter the semantics of it to imitate tables
	 * created by initdb rather than by user.
	 */
	allowSystemTableMods = newval;
}

static bool
check_max_backoff(int *max_backoff_msecs, void **extra, GucSource source)
{
	if (*max_backoff_msecs < 0)
	{
		GUC_check_errdetail("must be greater than or equal to 0.");
		return false;
	}

	return true;
}

static bool
check_min_backoff(int *min_backoff_msecs, void **extra, GucSource source)
{
	if (*min_backoff_msecs < 0)
	{
		GUC_check_errdetail("must be greater than or equal to 0.");
		return false;
	}

	return true;
}

static bool
check_backoff_multiplier(double *multiplier, void **extra, GucSource source)
{
	if (*multiplier < 1)
	{
		GUC_check_errdetail("must be greater than or equal to 1.");
		return false;
	}

	return true;
}

static bool
yb_check_toast_catcache_threshold(int *newVal, void **extra, GucSource source)
{
	if (*newVal != -1 && *newVal < 128) {
		GUC_check_errdetail("must greater than or equal to 128 bytes, or -1 to disable.");
		return false;
	}
	return true;
}

/*
 * YB: yb_check_no_txn
 *
 * Do not allow users to set yb_read_after_commit_visibility
 * from within a txn block.
 */
static bool
yb_check_no_txn(int *newVal, void **extra, GucSource source)
{
	if (IsTransactionBlock())
	{
		GUC_check_errdetail("Cannot be set within a txn block.");
		return false;
	}

	/*
	 * If YSQL Connection Manager is enabled, make the connection sticky
	 * for any variables that can only be set outside the context of an
	 * explicit transaction block.
	 */
	if (YbIsClientYsqlConnMgr())
	{
		elog(LOG, "Making connection sticky for non-transactional GUC variable");
		yb_ysql_conn_mgr_sticky_guc = true;
	}
	return true;
}


#include "guc-file.c"
