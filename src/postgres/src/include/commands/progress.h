/*-------------------------------------------------------------------------
 *
 * progress.h
 *	  Constants used with the progress reporting facilities defined in
 *	  backend_status.h.  These are possibly interesting to extensions, so we
 *	  expose them via this header file.  Note that if you update these
 *	  constants, you probably also need to update the views based on them
 *	  in system_views.sql.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/progress.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PROGRESS_H
#define PROGRESS_H

/* Progress parameters for (lazy) vacuum */
#define PROGRESS_VACUUM_PHASE					0
#define PROGRESS_VACUUM_TOTAL_HEAP_BLKS			1
#define PROGRESS_VACUUM_HEAP_BLKS_SCANNED		2
#define PROGRESS_VACUUM_HEAP_BLKS_VACUUMED		3
#define PROGRESS_VACUUM_NUM_INDEX_VACUUMS		4
#define PROGRESS_VACUUM_MAX_DEAD_TUPLES			5
#define PROGRESS_VACUUM_NUM_DEAD_TUPLES			6

/* Phases of vacuum (as advertised via PROGRESS_VACUUM_PHASE) */
#define PROGRESS_VACUUM_PHASE_SCAN_HEAP			1
#define PROGRESS_VACUUM_PHASE_VACUUM_INDEX		2
#define PROGRESS_VACUUM_PHASE_VACUUM_HEAP		3
#define PROGRESS_VACUUM_PHASE_INDEX_CLEANUP		4
#define PROGRESS_VACUUM_PHASE_TRUNCATE			5
#define PROGRESS_VACUUM_PHASE_FINAL_CLEANUP		6

/* Progress parameters for analyze */
#define PROGRESS_ANALYZE_PHASE						0
#define PROGRESS_ANALYZE_BLOCKS_TOTAL				1
#define PROGRESS_ANALYZE_BLOCKS_DONE				2
#define PROGRESS_ANALYZE_EXT_STATS_TOTAL			3
#define PROGRESS_ANALYZE_EXT_STATS_COMPUTED			4
#define PROGRESS_ANALYZE_CHILD_TABLES_TOTAL			5
#define PROGRESS_ANALYZE_CHILD_TABLES_DONE			6
#define PROGRESS_ANALYZE_CURRENT_CHILD_TABLE_RELID	7

/* Phases of analyze (as advertised via PROGRESS_ANALYZE_PHASE) */
#define PROGRESS_ANALYZE_PHASE_ACQUIRE_SAMPLE_ROWS		1
#define PROGRESS_ANALYZE_PHASE_ACQUIRE_SAMPLE_ROWS_INH	2
#define PROGRESS_ANALYZE_PHASE_COMPUTE_STATS			3
#define PROGRESS_ANALYZE_PHASE_COMPUTE_EXT_STATS		4
#define PROGRESS_ANALYZE_PHASE_FINALIZE_ANALYZE			5

/* Progress parameters for cluster */
#define PROGRESS_CLUSTER_COMMAND				0
#define PROGRESS_CLUSTER_PHASE					1
#define PROGRESS_CLUSTER_INDEX_RELID			2
#define PROGRESS_CLUSTER_HEAP_TUPLES_SCANNED	3
#define PROGRESS_CLUSTER_HEAP_TUPLES_WRITTEN	4
#define PROGRESS_CLUSTER_TOTAL_HEAP_BLKS		5
#define PROGRESS_CLUSTER_HEAP_BLKS_SCANNED		6
#define PROGRESS_CLUSTER_INDEX_REBUILD_COUNT	7

/* Phases of cluster (as advertised via PROGRESS_CLUSTER_PHASE) */
#define PROGRESS_CLUSTER_PHASE_SEQ_SCAN_HEAP	1
#define PROGRESS_CLUSTER_PHASE_INDEX_SCAN_HEAP	2
#define PROGRESS_CLUSTER_PHASE_SORT_TUPLES		3
#define PROGRESS_CLUSTER_PHASE_WRITE_NEW_HEAP	4
#define PROGRESS_CLUSTER_PHASE_SWAP_REL_FILES	5
#define PROGRESS_CLUSTER_PHASE_REBUILD_INDEX	6
#define PROGRESS_CLUSTER_PHASE_FINAL_CLEANUP	7

/* Commands of PROGRESS_CLUSTER */
#define PROGRESS_CLUSTER_COMMAND_CLUSTER		1
#define PROGRESS_CLUSTER_COMMAND_VACUUM_FULL	2

/* Progress parameters for CREATE INDEX */
/* 3, 4 and 5 reserved for "waitfor" metrics */
#define PROGRESS_CREATEIDX_COMMAND				0
#define PROGRESS_CREATEIDX_INDEX_OID			6
#define PROGRESS_CREATEIDX_ACCESS_METHOD_OID	8
#define PROGRESS_CREATEIDX_PHASE				9	/* AM-agnostic phase # */
#define PROGRESS_CREATEIDX_SUBPHASE				10	/* phase # filled by AM */
#define PROGRESS_CREATEIDX_TUPLES_TOTAL			11
#define PROGRESS_CREATEIDX_TUPLES_DONE			12
#define PROGRESS_CREATEIDX_PARTITIONS_TOTAL		13
#define PROGRESS_CREATEIDX_PARTITIONS_DONE		14
/* 15 and 16 reserved for "block number" metrics */

/* Phases of CREATE INDEX (as advertised via PROGRESS_CREATEIDX_PHASE) */
#define PROGRESS_CREATEIDX_PHASE_WAIT_1			1
#define PROGRESS_CREATEIDX_PHASE_BUILD			2
#define PROGRESS_CREATEIDX_PHASE_WAIT_2			3
#define PROGRESS_CREATEIDX_PHASE_VALIDATE_IDXSCAN	4
#define PROGRESS_CREATEIDX_PHASE_VALIDATE_SORT		5
#define PROGRESS_CREATEIDX_PHASE_VALIDATE_TABLESCAN	6
#define PROGRESS_CREATEIDX_PHASE_WAIT_3			7
#define PROGRESS_CREATEIDX_PHASE_WAIT_4			8
#define PROGRESS_CREATEIDX_PHASE_WAIT_5			9

/*
 * Subphases of CREATE INDEX, for index_build.
 */
#define PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE	1
/* Additional phases are defined by each AM */

/* Commands of PROGRESS_CREATEIDX */
#define PROGRESS_CREATEIDX_COMMAND_CREATE			1
#define PROGRESS_CREATEIDX_COMMAND_CREATE_CONCURRENTLY	2
#define PROGRESS_CREATEIDX_COMMAND_REINDEX		3
#define PROGRESS_CREATEIDX_COMMAND_REINDEX_CONCURRENTLY	4

/* Lock holder wait counts */
#define PROGRESS_WAITFOR_TOTAL					3
#define PROGRESS_WAITFOR_DONE					4
#define PROGRESS_WAITFOR_CURRENT_PID			5

/* Block numbers in a generic relation scan */
#define PROGRESS_SCAN_BLOCKS_TOTAL				15
#define PROGRESS_SCAN_BLOCKS_DONE				16

/* Progress parameters for pg_basebackup */
#define PROGRESS_BASEBACKUP_PHASE				0
#define PROGRESS_BASEBACKUP_BACKUP_TOTAL			1
#define PROGRESS_BASEBACKUP_BACKUP_STREAMED			2
#define PROGRESS_BASEBACKUP_TBLSPC_TOTAL			3
#define PROGRESS_BASEBACKUP_TBLSPC_STREAMED			4

/* Phases of pg_basebackup (as advertised via PROGRESS_BASEBACKUP_PHASE) */
#define PROGRESS_BASEBACKUP_PHASE_WAIT_CHECKPOINT		1
#define PROGRESS_BASEBACKUP_PHASE_ESTIMATE_BACKUP_SIZE		2
#define PROGRESS_BASEBACKUP_PHASE_STREAM_BACKUP			3
#define PROGRESS_BASEBACKUP_PHASE_WAIT_WAL_ARCHIVE		4
#define PROGRESS_BASEBACKUP_PHASE_TRANSFER_WAL			5

/* Progress parameters for PROGRESS_COPY */
#define PROGRESS_COPY_BYTES_PROCESSED 0
#define PROGRESS_COPY_BYTES_TOTAL 1
#define PROGRESS_COPY_TUPLES_PROCESSED 2
#define PROGRESS_COPY_TUPLES_EXCLUDED 3
#define PROGRESS_COPY_COMMAND 4
#define PROGRESS_COPY_TYPE 5
/* See YbProgressType below */
#define PROGRESS_COPY_STATUS 6

enum YbProgressType
{
	CP_IN_PROG,
	CP_ERROR,
	CP_SUCCESS
};

/* Commands of COPY (as advertised via PROGRESS_COPY_COMMAND) */
#define PROGRESS_COPY_COMMAND_FROM 1
#define PROGRESS_COPY_COMMAND_TO 2

/* Types of COPY commands (as advertised via PROGRESS_COPY_TYPE) */
#define PROGRESS_COPY_TYPE_FILE 1
#define PROGRESS_COPY_TYPE_PROGRAM 2
#define PROGRESS_COPY_TYPE_PIPE 3
#define PROGRESS_COPY_TYPE_CALLBACK 4

/*
 * TODO: remove this comment after PG 13 merge
 * The following macros and their values are imported from upstream PG.
 * The relevant PG commit (ab0dfc961b6a821f23d9c40c723d11380ce195a6)
 * was only partially imported to YB - so not all the commit's newly added
 * macros are imported (only the ones used in YB are).
 */

/* Progress parameters for CREATE INDEX */
#define PROGRESS_CREATEIDX_COMMAND				0
#define PROGRESS_CREATEIDX_INDEX_OID			6
#define PROGRESS_CREATEIDX_PHASE				9
#define PROGRESS_CREATEIDX_TUPLES_TOTAL			11
#define PROGRESS_CREATEIDX_TUPLES_DONE			12
#define PROGRESS_CREATEIDX_PARTITIONS_TOTAL		13
#define PROGRESS_CREATEIDX_PARTITIONS_DONE		14

/* Commands of PROGRESS_CREATEIDX */
#define PROGRESS_CREATEIDX_COMMAND_CREATE			1
#define PROGRESS_CREATEIDX_COMMAND_CREATE_CONCURRENTLY	2

/* YB phases for CREATE INDEX */
#define YB_PROGRESS_CREATEIDX_INITIALIZING 0
#define YB_PROGRESS_CREATEIDX_BACKFILLING 1

/*
 * YB constant used to indicate that the progress parameter for CREATE INDEX
 * is not computed.
 */
#define YB_PROGRESS_CREATEIDX_INVALID -10

#endif
