/*-------------------------------------------------------------------------
 *
 * tcopprot.h
 *	  prototypes for postgres.c.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/tcop/tcopprot.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef TCOPPROT_H
#define TCOPPROT_H

#include "nodes/params.h"
#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"
#include "storage/procsignal.h"
#include "utils/guc.h"
#include "utils/queryenvironment.h"


/* Required daylight between max_stack_depth and the kernel limit, in bytes */
#define STACK_DEPTH_SLOP (512 * 1024L)

extern PGDLLIMPORT CommandDest whereToSendOutput;
extern PGDLLIMPORT const char *debug_query_string;
extern PGDLLIMPORT int max_stack_depth;
extern PGDLLIMPORT int PostAuthDelay;
extern PGDLLIMPORT int client_connection_check_interval;

/* GUC-configurable parameters */

typedef enum
{
	LOGSTMT_NONE,				/* log no statements */
	LOGSTMT_DDL,				/* log data definition statements */
	LOGSTMT_MOD,				/* log modification statements, plus DDL */
	LOGSTMT_ALL					/* log all statements */
} LogStmtLevel;

extern PGDLLIMPORT int log_statement;

/* Flags for restrict_nonsystem_relation_kind value */
#define RESTRICT_RELKIND_VIEW			0x01
#define RESTRICT_RELKIND_FOREIGN_TABLE	0x02

extern PGDLLIMPORT int restrict_nonsystem_relation_kind;

extern List *pg_parse_query(const char *query_string);
extern List *pg_rewrite_query(Query *query);
extern List *pg_analyze_and_rewrite_fixedparams(RawStmt *parsetree,
												const char *query_string,
												const Oid *paramTypes, int numParams,
												QueryEnvironment *queryEnv);
extern List *pg_analyze_and_rewrite_varparams(RawStmt *parsetree,
											  const char *query_string,
											  Oid **paramTypes,
											  int *numParams,
											  QueryEnvironment *queryEnv);
extern List *pg_analyze_and_rewrite_withcb(RawStmt *parsetree,
										   const char *query_string,
										   ParserSetupHook parserSetup,
										   void *parserSetupArg,
										   QueryEnvironment *queryEnv);
extern PlannedStmt *pg_plan_query(Query *querytree, const char *query_string,
								  int cursorOptions,
								  ParamListInfo boundParams);
extern List *pg_plan_queries(List *querytrees, const char *query_string,
							 int cursorOptions,
							 ParamListInfo boundParams);

extern bool check_max_stack_depth(int *newval, void **extra, GucSource source);
extern void assign_max_stack_depth(int newval, void *extra);
extern bool check_restrict_nonsystem_relation_kind(char **newval, void **extra,
												   GucSource source);
extern void assign_restrict_nonsystem_relation_kind(const char *newval, void *extra);

extern void die(SIGNAL_ARGS);
extern void quickdie(SIGNAL_ARGS) pg_attribute_noreturn();
extern void StatementCancelHandler(SIGNAL_ARGS);
extern void FloatExceptionHandler(SIGNAL_ARGS) pg_attribute_noreturn();
extern void RecoveryConflictInterrupt(ProcSignalReason reason); /* called from SIGUSR1
																 * handler */
extern void ProcessClientReadInterrupt(bool blocked);
extern void ProcessClientWriteInterrupt(bool blocked);

extern void process_postgres_switches(int argc, char *argv[],
									  GucContext ctx, const char **dbname);
extern void PostgresSingleUserMain(int argc, char *argv[],
								   const char *username) pg_attribute_noreturn();
extern void PostgresMain(const char *dbname,
						 const char *username) pg_attribute_noreturn();
extern long get_stack_depth_rlimit(void);
extern void ResetUsage(void);
extern void ShowUsage(const char *title);
extern int	check_log_duration(char *msec_str, bool was_logged);
extern void set_debug_options(int debug_flag,
							  GucContext context, GucSource source);
extern bool set_plan_disabling_options(const char *arg,
									   GucContext context, GucSource source);
extern const char *get_stats_option_name(const char *arg);

#endif							/* TCOPPROT_H */
