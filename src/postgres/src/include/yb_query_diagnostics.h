/*-------------------------------------------------------------------------
 *
 * yb_query_diagnostics.c
 *    Utilities for Query Diagnostics/Yugabyte (Postgres layer) integration
 *    that have to be defined on the PostgreSQL side.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_query_diagnostics.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef YB_QUERY_DIAGNOSTICS_H
#define YB_QUERY_DIAGNOSTICS_H

#include "postgres.h"

#include "executor/instrument.h"
#include "jit/jit.h"
#include "storage/s_lock.h"
#include "utils/guc.h"
#include "utils/queryjumble.h"
#include "utils/timestamp.h"

#define YB_QD_MAX_BIND_VARS_LEN 2048
#define YB_QD_MAX_PGSS_LEN 2048
#define YB_QD_MAX_PGSS_QUERY_LEN 1024
#define YB_QD_DESCRIPTION_LEN 128
#define YB_QD_MAX_SCHEMA_OIDS 10
#define YB_QD_MAX_CONSTANTS 100

/* Status codes for query diagnostics bundles in yb_query_diagnostics view */
#define YB_DIAGNOSTICS_SUCCESS			 0
#define YB_DIAGNOSTICS_IN_PROGRESS		 1
#define YB_DIAGNOSTICS_ERROR			 2
#define YB_DIAGNOSTICS_CANCELLED		 3

/*
 * Currently, if the explain plan is larger than 16KB, we truncate it.
 * Github issue #23720: handles queries with explain plans excedding 16KB.
 */
#define YB_QD_MAX_EXPLAIN_PLAN_LEN 16384

/* GUC variables */
extern bool yb_enable_query_diagnostics;
extern int	yb_query_diagnostics_bg_worker_interval_ms;
extern int	yb_query_diagnostics_circular_buffer_size;

/*
 * Enum to distinguish between planning and execution statistics for pg_stat_statements.
 * Used to index into arrays of counters to track metrics separately for these phases.
 *
 * Note that this needs to be in sync with pgssStoreKind of pg_stat_statements.c
 */
 typedef enum YbQdPgssStoreKind
{
	YB_QD_PGSS_INVALID = -1,

	YB_QD_PGSS_PLAN = 0,
	YB_QD_PGSS_EXEC,

	YB_QD_PGSS_NUMKIND				/* Must be last value of this enum */
} YbQdPgssStoreKind;

typedef struct YbPgssCounters
{
	int64		calls[YB_QD_PGSS_NUMKIND];			/* # of times executed */
	double		total_time[YB_QD_PGSS_NUMKIND];		/* total planning/execution time, in msec */
	double		min_time[YB_QD_PGSS_NUMKIND];		/* minimum planning/execution time in msec */
	double		max_time[YB_QD_PGSS_NUMKIND];		/* maximum planning/execution time in msec */
	double		mean_time[YB_QD_PGSS_NUMKIND];		/* mean planning/execution time in msec */
	double		sum_var_time[YB_QD_PGSS_NUMKIND];	/* sum of variances in planning/execution time in msec */
	int64		rows;			/* total # of retrieved or affected rows */
	int64		shared_blks_hit;	/* # of shared buffer hits */
	int64		shared_blks_read;	/* # of shared disk blocks read */
	int64		shared_blks_dirtied;	/* # of shared disk blocks dirtied */
	int64		shared_blks_written;	/* # of shared disk blocks written */
	int64		local_blks_hit; /* # of local buffer hits */
	int64		local_blks_read;	/* # of local disk blocks read */
	int64		local_blks_dirtied; /* # of local disk blocks dirtied */
	int64		local_blks_written; /* # of local disk blocks written */
	int64		temp_blks_read; /* # of temp blocks read */
	int64		temp_blks_written;	/* # of temp blocks written */
	double		blk_read_time;	/* time spent reading blocks, in msec */
	double		blk_write_time; /* time spent writing blocks, in msec */
	double		temp_blk_read_time; /* time spent reading temp blocks, in msec */
	double		temp_blk_write_time; /* time spent writing temp blocks, in msec */
	int64		wal_records;	/* # of WAL records generated */
	int64		wal_fpi;		/* # of WAL full page images generated */
	uint64		wal_bytes;		/* total amount of WAL generated in bytes */
	int64		jit_functions;	/* total number of JIT functions emitted */
	double		jit_generation_time;	/* total time to generate jit code */
	int64		jit_inlining_count; /* number of times inlining time has been > 0 */
	double		jit_inlining_time;	/* total time to inline jit code */
	int64		jit_optimization_count; /* number of times optimization time has been > 0 */
	double		jit_optimization_time;	/* total time to optimize jit code */
	int64		jit_emission_count; /* number of times emission time has been > 0 */
	double		jit_emission_time;	/* total time to emit jit code */
} YbPgssCounters;

typedef struct YbQueryDiagnosticsPgss
{
	YbPgssCounters counters;		/* the statistics for this query */
	Size		query_offset;	/* query text offset in external file */
	int			query_len;		/* # of valid bytes in query string, or -1 */
} YbQueryDiagnosticsPgss;

typedef struct YbQueryDiagnosticsParams
{
	/* Hash code to identify identical normalized queries */
	int64		query_id;

	/* Indicates the duration for which the bundle will run */
	int			diagnostics_interval_sec;

	/* Percentage of queries to be explain’ed */
	int			explain_sample_rate;

	/* Whether to run EXPLAIN ANALYZE on the query */
	bool		explain_analyze;

	/* Whether to run EXPLAIN (DIST) on the query */
	bool		explain_dist;

	/* Whether to run EXPLAIN (DEBUG) on the query */
	bool		explain_debug;

	/*
	 * Minimum duration for a query to be considered for bundling bind
	 * variables
	 */
	int			bind_var_query_min_duration_ms;
} YbQueryDiagnosticsParams;

/*
 * Structure to hold the parameters and configuration metadata for a query diagnostic bundle
 */
typedef struct YbQueryDiagnosticsMetadata
{
	/* Stores the parameters passed to the yb_query_diagnostics() function */
	YbQueryDiagnosticsParams params;

	/* Path to the folder where the bundle is stored */
	char		path[MAXPGPATH];

	/* Database name (required for dumping schema details) */
	char		db_name[NAMEDATALEN];

	/* Time when the query diagnostics bundle started */
	TimestampTz start_time;

	/* Whether the directory has been created */
	bool		directory_created;

	/* Only schema details remain pending; all other diagnostics data has been flushed */
	bool		flush_only_schema_details;
} YbQueryDiagnosticsMetadata;

/*
 * Structure to represent each entry within the hash table.
 */
typedef struct YbQueryDiagnosticsEntry
{
	/* Stores parameter and configuration metadata of this bundle */
	YbQueryDiagnosticsMetadata metadata;

	/* Protects following fields only: */
	slock_t		mutex;

	/* Holds the bind_variables data until flushed to disc */
	char		bind_vars[YB_QD_MAX_BIND_VARS_LEN];

	/* Holds the pg_stat_statements data until flushed to disc */
	YbQueryDiagnosticsPgss pgss;

	/* Holds the explain plan data until flushed to disc */
	char		explain_plan[YB_QD_MAX_EXPLAIN_PLAN_LEN];

	/* Holds the schema oids until flushed to disc */
	Oid			schema_oids[YB_QD_MAX_SCHEMA_OIDS];
} YbQueryDiagnosticsEntry;

extern TimestampTz *yb_pgss_last_reset_time;

typedef int (*YbGetNormalizedQueryFuncPtr) (Size query_offset, int pgss_query_len,
											char *normalized_query);
extern YbGetNormalizedQueryFuncPtr yb_get_normalized_query;

typedef void (*YbPgssFillInConstantLengths) (JumbleState *jstate, const char *query, int query_loc);
extern YbPgssFillInConstantLengths yb_qd_fill_in_constant_lengths;

extern void YbQueryDiagnosticsInstallHook(void);
extern Size YbQueryDiagnosticsShmemSize(void);
extern void YbQueryDiagnosticsShmemInit(void);
extern void YbQueryDiagnosticsMain(Datum main_arg);
extern void YbSetPgssNormalizedQueryText(int64 query_id, const Size query_offset, int query_len);
extern void YbQueryDiagnosticsAppendToDescription(char *description, const char *format, ...);
extern void YbQueryDiagnosticsDatabaseConnectionWorkerMain(Datum main_arg);
extern void YbQueryDiagnosticsAccumulatePgss(int64 query_id, YbQdPgssStoreKind kind,
											 double total_time, uint64 rows,
											 const BufferUsage *bufusage,
											 const WalUsage *walusage,
											 const struct JitInstrumentation *jitusage);
#endif							/* YB_QUERY_DIAGNOSTICS_H */
