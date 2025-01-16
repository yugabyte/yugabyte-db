/*--------------------------------------------------------------------
 * guc.h
 *
 * External declarations pertaining to backend/utils/misc/guc.c and
 * backend/utils/misc/guc-file.l
 *
 * Copyright (c) 2000-2022, PostgreSQL Global Development Group
 * Written by Peter Eisentraut <peter_e@gmx.net>.
 *
 * src/include/utils/guc.h
 *--------------------------------------------------------------------
 */
#ifndef GUC_H
#define GUC_H

#include "nodes/parsenodes.h"
#include "tcop/dest.h"
#include "utils/array.h"


/* upper limit for GUC variables measured in kilobytes of memory */
/* note that various places assume the byte size fits in a "long" variable */
#if SIZEOF_SIZE_T > 4 && SIZEOF_LONG > 4
#define MAX_KILOBYTES	INT_MAX
#else
#define MAX_KILOBYTES	(INT_MAX / 1024)
#endif

/*
 * Automatic configuration file name for ALTER SYSTEM.
 * This file will be used to store values of configuration parameters
 * set by ALTER SYSTEM command.
 */
#define PG_AUTOCONF_FILENAME		"postgresql.auto.conf"

/*
 * Certain options can only be set at certain times. The rules are
 * like this:
 *
 * INTERNAL options cannot be set by the user at all, but only through
 * internal processes ("server_version" is an example).  These are GUC
 * variables only so they can be shown by SHOW, etc.
 *
 * POSTMASTER options can only be set when the postmaster starts,
 * either from the configuration file or the command line.
 *
 * SIGHUP options can only be set at postmaster startup or by changing
 * the configuration file and sending the HUP signal to the postmaster
 * or a backend process. (Notice that the signal receipt will not be
 * evaluated immediately. The postmaster and the backend check it at a
 * certain point in their main loop. It's safer to wait than to read a
 * file asynchronously.)
 *
 * BACKEND and SU_BACKEND options can only be set at postmaster startup,
 * from the configuration file, or by client request in the connection
 * startup packet (e.g., from libpq's PGOPTIONS variable).  SU_BACKEND
 * options can be set from the startup packet only when the user is a
 * superuser.  Furthermore, an already-started backend will ignore changes
 * to such an option in the configuration file.  The idea is that these
 * options are fixed for a given backend once it's started, but they can
 * vary across backends.
 *
 * SUSET options can be set at postmaster startup, with the SIGHUP
 * mechanism, or from the startup packet or SQL if you're a superuser.
 *
 * USERSET options can be set by anyone any time.
 */
typedef enum
{
	PGC_INTERNAL,
	PGC_POSTMASTER,
	PGC_SIGHUP,
	PGC_SU_BACKEND,
	PGC_BACKEND,
	PGC_SUSET,
	PGC_USERSET
} GucContext;

/*
 * The following type records the source of the current setting.  A
 * new setting can only take effect if the previous setting had the
 * same or lower level.  (E.g, changing the config file doesn't
 * override the postmaster command line.)  Tracking the source allows us
 * to process sources in any convenient order without affecting results.
 * Sources <= PGC_S_OVERRIDE will set the default used by RESET, as well
 * as the current value.
 *
 * PGC_S_INTERACTIVE isn't actually a source value, but is the
 * dividing line between "interactive" and "non-interactive" sources for
 * error reporting purposes.
 *
 * PGC_S_TEST is used when testing values to be used later.  For example,
 * ALTER DATABASE/ROLE tests proposed per-database or per-user defaults this
 * way, and CREATE FUNCTION tests proposed function SET clauses this way.
 * This is an interactive case, but it needs its own source value because
 * some assign hooks need to make different validity checks in this case.
 * In particular, references to nonexistent database objects generally
 * shouldn't throw hard errors in this case, at most NOTICEs, since the
 * objects might exist by the time the setting is used for real.
 *
 * When setting the value of a non-compile-time-constant PGC_INTERNAL option,
 * source == PGC_S_DYNAMIC_DEFAULT should typically be used so that the value
 * will show as "default" in pg_settings.  If there is a specific reason not
 * to want that, use source == PGC_S_OVERRIDE.
 *
 * NB: see GucSource_Names in guc.c if you change this.
 */
typedef enum
{
	PGC_S_DEFAULT,				/* hard-wired default ("boot_val") */
	PGC_S_DYNAMIC_DEFAULT,		/* default computed during initialization */
	PGC_S_ENV_VAR,				/* postmaster environment variable */
	PGC_S_FILE,					/* postgresql.conf */
	PGC_S_ARGV,					/* postmaster command line */
	PGC_S_GLOBAL,				/* global in-database setting */
	PGC_S_DATABASE,				/* per-database setting */
	PGC_S_USER,					/* per-user setting */
	PGC_S_DATABASE_USER,		/* per-user-and-database setting */
	PGC_S_CLIENT,				/* from client connection request */
	PGC_S_OVERRIDE,				/* special case to forcibly set default */
	PGC_S_INTERACTIVE,			/* dividing line for error reporting */
	PGC_S_TEST,					/* test per-database or per-user setting */
	PGC_S_SESSION,				/* SET command */
	YSQL_CONN_MGR				/* SET SESSION PARAMETER packet */
} GucSource;

/*
 * Parsing the configuration file(s) will return a list of name-value pairs
 * with source location info.  We also abuse this data structure to carry
 * error reports about the config files.  An entry reporting an error will
 * have errmsg != NULL, and might have NULLs for name, value, and/or filename.
 *
 * If "ignore" is true, don't attempt to apply the item (it might be an error
 * report, or an item we determined to be duplicate).  "applied" is set true
 * if we successfully applied, or could have applied, the setting.
 */
typedef struct ConfigVariable
{
	char	   *name;
	char	   *value;
	char	   *errmsg;
	char	   *filename;
	int			sourceline;
	bool		ignore;
	bool		applied;
	struct ConfigVariable *next;
} ConfigVariable;

extern bool ParseConfigFile(const char *config_file, bool strict,
							const char *calling_file, int calling_lineno,
							int depth, int elevel,
							ConfigVariable **head_p, ConfigVariable **tail_p);
extern bool ParseConfigFp(FILE *fp, const char *config_file,
						  int depth, int elevel,
						  ConfigVariable **head_p, ConfigVariable **tail_p);
extern bool ParseConfigDirectory(const char *includedir,
								 const char *calling_file, int calling_lineno,
								 int depth, int elevel,
								 ConfigVariable **head_p,
								 ConfigVariable **tail_p);
extern void FreeConfigVariables(ConfigVariable *list);
extern char *DeescapeQuotedString(const char *s);

/*
 * The possible values of an enum variable are specified by an array of
 * name-value pairs.  The "hidden" flag means the value is accepted but
 * won't be displayed when guc.c is asked for a list of acceptable values.
 */
struct config_enum_entry
{
	const char *name;
	int			val;
	bool		hidden;
};

/*
 * Signatures for per-variable check/assign/show hook functions
 */
typedef bool (*GucBoolCheckHook) (bool *newval, void **extra, GucSource source);
typedef bool (*GucIntCheckHook) (int *newval, void **extra, GucSource source);
typedef bool (*GucRealCheckHook) (double *newval, void **extra, GucSource source);
typedef bool (*GucStringCheckHook) (char **newval, void **extra, GucSource source);
typedef bool (*GucEnumCheckHook) (int *newval, void **extra, GucSource source);

typedef void (*GucBoolAssignHook) (bool newval, void *extra);
typedef void (*GucIntAssignHook) (int newval, void *extra);
typedef void (*GucRealAssignHook) (double newval, void *extra);
typedef void (*GucStringAssignHook) (const char *newval, void *extra);
typedef void (*GucEnumAssignHook) (int newval, void *extra);

typedef const char *(*GucShowHook) (void);

typedef bool (*YbGucOidCheckHook) (Oid *newval, void **extra, GucSource source);
typedef void (*YbGucOidAssignHook) (Oid newval, void *extra);

/*
 * Miscellaneous
 */
typedef enum
{
	/* Types of set_config_option actions */
	GUC_ACTION_SET,				/* regular SET command */
	GUC_ACTION_LOCAL,			/* SET LOCAL command */
	GUC_ACTION_SAVE				/* function SET option, or temp assignment */
} GucAction;

#define GUC_QUALIFIER_SEPARATOR '.'

/*
 * bit values in "flags" of a GUC variable
 */
#define GUC_LIST_INPUT			0x0001	/* input can be list format */
#define GUC_LIST_QUOTE			0x0002	/* double-quote list elements */
#define GUC_NO_SHOW_ALL			0x0004	/* exclude from SHOW ALL */
#define GUC_NO_RESET_ALL		0x0008	/* exclude from RESET ALL */
#define GUC_REPORT				0x0010	/* auto-report changes to client */
#define GUC_NOT_IN_SAMPLE		0x0020	/* not in postgresql.conf.sample */
#define GUC_DISALLOW_IN_FILE	0x0040	/* can't set in postgresql.conf */
#define GUC_CUSTOM_PLACEHOLDER	0x0080	/* placeholder for custom variable */
#define GUC_SUPERUSER_ONLY		0x0100	/* show only to superusers */
#define GUC_IS_NAME				0x0200	/* limit string to NAMEDATALEN-1 */
#define GUC_NOT_WHILE_SEC_REST	0x0400	/* can't set if security restricted */
#define GUC_DISALLOW_IN_AUTO_FILE 0x0800	/* can't set in
											 * PG_AUTOCONF_FILENAME */

#define GUC_UNIT_KB				0x1000	/* value is in kilobytes */
#define GUC_UNIT_BLOCKS			0x2000	/* value is in blocks */
#define GUC_UNIT_XBLOCKS		0x3000	/* value is in xlog blocks */
#define GUC_UNIT_MB				0x4000	/* value is in megabytes */
#define GUC_UNIT_BYTE			0x8000	/* value is in bytes */
#define GUC_UNIT_MEMORY			0xF000	/* mask for size-related units */

#define GUC_UNIT_MS			   0x10000	/* value is in milliseconds */
#define GUC_UNIT_S			   0x20000	/* value is in seconds */
#define GUC_UNIT_MIN		   0x30000	/* value is in minutes */
#define GUC_UNIT_TIME		   0xF0000	/* mask for time-related units */

#define GUC_EXPLAIN			  0x100000	/* include in explain */

/*
 * GUC_RUNTIME_COMPUTED is intended for runtime-computed GUCs that are only
 * available via 'postgres -C' if the server is not running.
 */
#define GUC_RUNTIME_COMPUTED  0x200000

#define GUC_UNIT				(GUC_UNIT_MEMORY | GUC_UNIT_TIME)

#define GUC_YB_CUSTOM_STICKY	0x400000 /* stickiness for custom string
										  * variables */


/* GUC vars that are actually declared in guc.c, rather than elsewhere */
extern PGDLLIMPORT bool Debug_print_plan;
extern PGDLLIMPORT bool Debug_print_parse;
extern PGDLLIMPORT bool Debug_print_rewritten;
extern PGDLLIMPORT bool Debug_pretty_print;

extern PGDLLIMPORT bool log_parser_stats;
extern PGDLLIMPORT bool log_planner_stats;
extern PGDLLIMPORT bool log_executor_stats;
extern PGDLLIMPORT bool log_statement_stats;
extern PGDLLIMPORT bool log_btree_build_stats;

extern PGDLLIMPORT bool check_function_bodies;
extern PGDLLIMPORT bool session_auth_is_superuser;

extern PGDLLIMPORT bool log_duration;
extern PGDLLIMPORT int log_parameter_max_length;
extern PGDLLIMPORT int log_parameter_max_length_on_error;
extern PGDLLIMPORT int log_min_error_statement;
extern PGDLLIMPORT int log_min_messages;
extern PGDLLIMPORT int client_min_messages;

extern PGDLLIMPORT int log_min_duration_sample;
extern PGDLLIMPORT int log_min_duration_statement;
extern PGDLLIMPORT int log_temp_files;
extern PGDLLIMPORT double log_statement_sample_rate;
extern PGDLLIMPORT double log_xact_sample_rate;
extern PGDLLIMPORT char *backtrace_functions;
extern PGDLLIMPORT char *backtrace_symbol_list;

extern PGDLLIMPORT int temp_file_limit;

extern PGDLLIMPORT int num_temp_buffers;

extern PGDLLIMPORT char *cluster_name;
extern PGDLLIMPORT char *ConfigFileName;
extern PGDLLIMPORT char *HbaFileName;
extern PGDLLIMPORT char *IdentFileName;
extern PGDLLIMPORT char *external_pid_file;

extern PGDLLIMPORT char *application_name;

extern PGDLLIMPORT int tcp_keepalives_idle;
extern PGDLLIMPORT int tcp_keepalives_interval;
extern PGDLLIMPORT int tcp_keepalives_count;
extern PGDLLIMPORT int tcp_user_timeout;

#ifdef TRACE_SORT
extern PGDLLIMPORT bool trace_sort;
#endif

extern PGDLLIMPORT bool yb_enable_memory_tracking;
extern PGDLLIMPORT int	yb_bnl_batch_size;
extern PGDLLIMPORT bool  yb_bnl_optimize_first_batch;
extern PGDLLIMPORT bool  yb_bnl_enable_hashing;
extern PGDLLIMPORT int yb_explicit_row_locking_batch_size;
extern PGDLLIMPORT bool yb_lock_pk_single_rpc;
extern PGDLLIMPORT int yb_toast_catcache_threshold;
extern PGDLLIMPORT bool yb_enable_fkey_catcache;

/*
 * Functions exported by guc.c
 */
extern void SetConfigOption(const char *name, const char *value,
							GucContext context, GucSource source);

extern void DefineCustomBoolVariable(const char *name,
									 const char *short_desc,
									 const char *long_desc,
									 bool *valueAddr,
									 bool bootValue,
									 GucContext context,
									 int flags,
									 GucBoolCheckHook check_hook,
									 GucBoolAssignHook assign_hook,
									 GucShowHook show_hook);

extern void DefineCustomIntVariable(const char *name,
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
									GucShowHook show_hook);

extern void DefineCustomOidVariable(const char *name,
									const char *short_desc,
									const char *long_desc,
									Oid *valueAddr,
									Oid bootValue,
									Oid minValue,
									Oid maxValue,
									GucContext context,
									int flags,
									YbGucOidCheckHook check_hook,
									YbGucOidAssignHook assign_hook,
									GucShowHook show_hook);

extern void DefineCustomRealVariable(const char *name,
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
									 GucShowHook show_hook);

extern void DefineCustomStringVariable(const char *name,
									   const char *short_desc,
									   const char *long_desc,
									   char **valueAddr,
									   const char *bootValue,
									   GucContext context,
									   int flags,
									   GucStringCheckHook check_hook,
									   GucStringAssignHook assign_hook,
									   GucShowHook show_hook);

extern void DefineCustomEnumVariable(const char *name,
									 const char *short_desc,
									 const char *long_desc,
									 int *valueAddr,
									 int bootValue,
									 const struct config_enum_entry *options,
									 GucContext context,
									 int flags,
									 GucEnumCheckHook check_hook,
									 GucEnumAssignHook assign_hook,
									 GucShowHook show_hook);

extern void MarkGUCPrefixReserved(const char *className);

/* old name for MarkGUCPrefixReserved, for backwards compatibility: */
#define EmitWarningsOnPlaceholders(className) MarkGUCPrefixReserved(className)

extern const char *GetConfigOption(const char *name, bool missing_ok,
								   bool restrict_privileged);
extern const char *GetConfigOptionResetString(const char *name);
extern int	GetConfigOptionFlags(const char *name, bool missing_ok);
extern void ProcessConfigFile(GucContext context);
extern char *convert_GUC_name_for_parameter_acl(const char *name);
extern bool check_GUC_name_for_parameter_acl(const char *name);
extern void InitializeGUCOptions(void);
extern void InitializeWalConsistencyChecking(void);
extern bool SelectConfigFiles(const char *userDoption, const char *progname);
extern void ResetAllOptions(void);
extern void AtStart_GUC(void);
extern int	NewGUCNestLevel(void);
extern void AtEOXact_GUC(bool isCommit, int nestLevel);
extern void BeginReportingGUCOptions(void);
extern void ReportChangedGUCOptions(void);
extern void ParseLongOption(const char *string, char **name, char **value);
extern bool parse_int(const char *value, int *result, int flags,
					  const char **hintmsg);
extern bool parse_oid(const char *value, Oid *result, const char **hintmsg);
extern bool parse_real(const char *value, double *result, int flags,
					   const char **hintmsg);
extern int	set_config_option(const char *name, const char *value,
							  GucContext context, GucSource source,
							  GucAction action, bool changeVal, int elevel,
							  bool is_reload);
extern int	set_config_option_ext(const char *name, const char *value,
								  GucContext context, GucSource source,
								  Oid srole,
								  GucAction action, bool changeVal, int elevel,
								  bool is_reload);
extern void AlterSystemSetConfigFile(AlterSystemStmt *altersysstmt);
extern char *GetConfigOptionByName(const char *name, const char **varname,
								   bool missing_ok);
extern void GetConfigOptionByNum(int varnum, const char **values, bool *noshow);
extern int	GetNumConfigOptions(void);

extern void SetPGVariable(const char *name, List *args, bool is_local);
extern void GetPGVariable(const char *name, DestReceiver *dest);
extern TupleDesc GetPGVariableResultDesc(const char *name);

extern void ExecSetVariableStmt(VariableSetStmt *stmt, bool isTopLevel);
extern char *ExtractSetVariableArgs(VariableSetStmt *stmt);

extern void ProcessGUCArray(ArrayType *array,
							GucContext context, GucSource source, GucAction action);
extern ArrayType *GUCArrayAdd(ArrayType *array, const char *name, const char *value);
extern ArrayType *GUCArrayDelete(ArrayType *array, const char *name);
extern ArrayType *GUCArrayReset(ArrayType *array);

#ifdef EXEC_BACKEND
extern void write_nondefault_variables(GucContext context);
extern void read_nondefault_variables(void);
#endif

extern void YbSetParallelWorker();

/* GUC serialization */
extern Size EstimateGUCStateSpace(void);
extern void SerializeGUCState(Size maxsize, char *start_address);
extern void RestoreGUCState(void *gucstate);

/* Support for messages reported from GUC check hooks */

extern PGDLLIMPORT char *GUC_check_errmsg_string;
extern PGDLLIMPORT char *GUC_check_errdetail_string;
extern PGDLLIMPORT char *GUC_check_errhint_string;

extern void GUC_check_errcode(int sqlerrcode);

#define GUC_check_errmsg \
	pre_format_elog_string(errno, TEXTDOMAIN), \
	GUC_check_errmsg_string = format_elog_string

#define GUC_check_errdetail \
	pre_format_elog_string(errno, TEXTDOMAIN), \
	GUC_check_errdetail_string = format_elog_string

#define GUC_check_errhint \
	pre_format_elog_string(errno, TEXTDOMAIN), \
	GUC_check_errhint_string = format_elog_string


/*
 * The following functions are not in guc.c, but are declared here to avoid
 * having to include guc.h in some widely used headers that it really doesn't
 * belong in.
 */

/* in commands/tablespace.c */
extern bool check_default_tablespace(char **newval, void **extra, GucSource source);
extern bool check_temp_tablespaces(char **newval, void **extra, GucSource source);
extern void assign_temp_tablespaces(const char *newval, void *extra);

/* in catalog/namespace.c */
extern bool check_search_path(char **newval, void **extra, GucSource source);
extern void assign_search_path(const char *newval, void *extra);

/* in access/transam/xlog.c */
extern bool check_wal_buffers(int *newval, void **extra, GucSource source);
extern void assign_xlog_sync_method(int new_sync_method, void *extra);

/* in access/transam/xlogprefetcher.c */
extern bool check_recovery_prefetch(int *new_value, void **extra, GucSource source);
extern void assign_recovery_prefetch(int new_value, void *extra);

#endif							/* GUC_H */
