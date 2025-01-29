/*-------------------------------------------------------------------------
 *
 * relscan.h
 *	  POSTGRES relation scan descriptor definitions.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/relscan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef RELSCAN_H
#define RELSCAN_H

#include "access/htup_details.h"
#include "access/itup.h"
#include "port/atomics.h"
#include "storage/buf.h"
#include "storage/spin.h"
#include "utils/relcache.h"

/* Yugabyte includes */
#include "access/yb_sys_scan_base.h"
#include "executor/ybExpr.h"
#include "pg_yb_utils.h"
#include "yb/yql/pggate/ybc_pggate.h"

typedef struct YbScanDescData *YbScanDesc;

struct ParallelTableScanDescData;

/*
 * Generic descriptor for table scans. This is the base-class for table scans,
 * which needs to be embedded in the scans of individual AMs.
 */
typedef struct TableScanDescData
{
	/* scan parameters */
	Relation	rs_rd;			/* heap relation descriptor */
	struct SnapshotData *rs_snapshot;	/* snapshot to see */
	int			rs_nkeys;		/* number of scan keys */
	struct ScanKeyData *rs_key; /* array of scan key descriptors */

	/* Range of ItemPointers for table_scan_getnextslot_tidrange() to scan. */
	ItemPointerData rs_mintid;
	ItemPointerData rs_maxtid;

	/*
	 * Information about type and behaviour of the scan, a bitmask of members
	 * of the ScanOptions enum (see tableam.h).
	 */
	uint32		rs_flags;

	struct ParallelTableScanDescData *rs_parallel;	/* parallel scan
													 * information */
} TableScanDescData;
typedef struct TableScanDescData *TableScanDesc;

/*
 * Shared state for parallel table scan.
 *
 * Each backend participating in a parallel table scan has its own
 * TableScanDesc in backend-private memory, and those objects all contain a
 * pointer to this structure.  The information here must be sufficient to
 * properly initialize each new TableScanDesc as workers join the scan, and it
 * must act as a information what to scan for those workers.
 */
typedef struct ParallelTableScanDescData
{
	Oid			phs_relid;		/* OID of relation to scan */
	bool		phs_syncscan;	/* report location to syncscan logic? */
	bool		phs_snapshot_any;	/* SnapshotAny, not phs_snapshot_data? */
	Size		phs_snapshot_off;	/* data for snapshot */
} ParallelTableScanDescData;
typedef struct ParallelTableScanDescData *ParallelTableScanDesc;

/*
 * Shared state for parallel table scans, for block oriented storage.
 */
typedef struct ParallelBlockTableScanDescData
{
	ParallelTableScanDescData base;

	BlockNumber phs_nblocks;	/* # blocks in relation at start of scan */
	slock_t		phs_mutex;		/* mutual exclusion for setting startblock */
	BlockNumber phs_startblock; /* starting block number */
	pg_atomic_uint64 phs_nallocated;	/* number of blocks allocated to
										 * workers so far. */
}			ParallelBlockTableScanDescData;
typedef struct ParallelBlockTableScanDescData *ParallelBlockTableScanDesc;

/*
 * Per backend state for parallel table scan, for block-oriented storage.
 */
typedef struct ParallelBlockTableScanWorkerData
{
	uint64		phsw_nallocated;	/* Current # of blocks into the scan */
	uint32		phsw_chunk_remaining;	/* # blocks left in this chunk */
	uint32		phsw_chunk_size;	/* The number of blocks to allocate in
									 * each I/O chunk for the scan */
} ParallelBlockTableScanWorkerData;
typedef struct ParallelBlockTableScanWorkerData *ParallelBlockTableScanWorker;

/*
 * Base class for fetches from a table via an index. This is the base-class
 * for such scans, which needs to be embedded in the respective struct for
 * individual AMs.
 */
typedef struct IndexFetchTableData
{
	Relation	rel;
} IndexFetchTableData;

/*
 * We use the same IndexScanDescData structure for both amgettuple-based
 * and amgetbitmap-based index scans.  Some fields are only relevant in
 * amgettuple-based scans.
 */
typedef struct IndexScanDescData
{
	/* scan parameters */
	Relation	heapRelation;	/* heap relation descriptor, or NULL */
	Relation	indexRelation;	/* index relation descriptor */
	struct SnapshotData *xs_snapshot;	/* snapshot to see */
	int			numberOfKeys;	/* number of index qualifier conditions */
	int			numberOfOrderBys;	/* number of ordering operators */
	struct ScanKeyData *keyData;	/* array of index qualifier descriptors */
	struct ScanKeyData *orderByData;	/* array of ordering op descriptors */
	bool		xs_want_itup;	/* caller requests index tuples */
	bool		xs_temp_snap;	/* unregister snapshot at scan end? */

	/* signaling to index AM about killing index tuples */
	bool		kill_prior_tuple;	/* last-returned tuple is dead */
	bool		ignore_killed_tuples;	/* do not return killed entries */
	bool		xactStartedInRecovery;	/* prevents killing/seeing killed
										 * tuples */

	/* index access method's private state */
	void	   *opaque;			/* access-method-specific info */

	/*
	 * In an index-only scan, a successful amgettuple call must fill either
	 * xs_itup (and xs_itupdesc) or xs_hitup (and xs_hitupdesc) to provide the
	 * data returned by the scan.  It can fill both, in which case the heap
	 * format will be used.
	 */
	IndexTuple	xs_itup;		/* index tuple returned by AM */
	struct TupleDescData *xs_itupdesc;	/* rowtype descriptor of xs_itup */
	HeapTuple	xs_hitup;		/* index data returned by AM, as HeapTuple */
	struct TupleDescData *xs_hitupdesc; /* rowtype descriptor of xs_hitup */

	ItemPointerData xs_heaptid; /* result */
	bool		xs_heap_continue;	/* T if must keep walking, potential
									 * further results */
	IndexFetchTableData *xs_heapfetch;

	bool		xs_recheck;		/* T means scan keys must be rechecked */

	/*
	 * When fetching with an ordering operator, the values of the ORDER BY
	 * expressions of the last returned tuple, according to the index.  If
	 * xs_recheckorderby is true, these need to be rechecked just like the
	 * scan keys, and the values returned here are a lower-bound on the actual
	 * values.
	 */
	Datum	   *xs_orderbyvals;
	bool	   *xs_orderbynulls;
	bool		xs_recheckorderby;

	/* parallel index scan information, in shared memory */
	struct ParallelIndexScanDescData *parallel_scan;

	/*
	 * During execution, Postgres will push down hints to YugaByte for
	 * performance purpose. (currently, only LIMIT values are being pushed
	 * down). All these execution information will kept in "yb_exec_params".
	 *
	 * - Generally, "yb_exec_params" is kept in execution-state. As Postgres
	 *   executor traverses and excutes the nodes, it passes along the
	 *   execution state. Necessary information (such as LIMIT values) will be
	 *   collected and written to "yb_exec_params" in EState.
	 *
	 * - However, IndexScan execution doesn't use Postgres's node execution
	 *   infrastructure. Neither execution plan nor execution state is passed
	 *   to IndexScan operators. As a result, "yb_exec_params" is kept in
	 *   "IndexScanDescData" to avoid passing EState to a lot of IndexScan
	 *   functions.
	 *
	 * - Postgres IndexScan function will call and pass "yb_exec_params" to
	 *   PgGate to control the index-scan execution in YugaByte.
	 */
	YbcPgExecParameters *yb_exec_params;

	/*
	 * yb_scan_plan stores postgres scan plan for current index scan.
	 * This information is used to determine target columns that must be read from DocDB
	 * and columns which can be omitted.
	 * TODO: Calculate set of required YB targets on plan stage and use it here
	 *       instead of scan plan. In addition to code speedup this approach will allow to
	 *       remove scan plan from IndexScanDescData structure. Native postgres code doesn't
	 *       have plan information in scan state structures.
	 */
	Scan	   *yb_scan_plan;
	YbPushdownExprs *yb_rel_pushdown;
	YbPushdownExprs *yb_idx_pushdown;
	List	   *yb_aggrefs;		/* aggregate information for aggregate
								 * pushdown */
	TupleTableSlot *yb_agg_slot;	/* scan slot used by aggregate pushdown */
	int			yb_distinct_prefixlen;	/* prefix length, in columns, of a
										 * distinct index scan */
	bool		fetch_ybctids_only;
}			IndexScanDescData;

/* Generic structure for parallel scans */
typedef struct ParallelIndexScanDescData
{
	Oid			ps_relid;
	Oid			ps_indexid;
	Size		ps_offset;		/* Offset in bytes of am specific structure */
	char		ps_snapshot_data[FLEXIBLE_ARRAY_MEMBER];
}			ParallelIndexScanDescData;

struct TupleTableSlot;

/* Struct for storage-or-index scans of system tables */
typedef struct SysScanDescData
{
	Relation	heap_rel;		/* catalog being scanned */
	Relation	irel;			/* NULL if doing heap or yb scan */
	struct TableScanDescData *scan; /* only valid in storage-scan case */
	struct IndexScanDescData *iscan;	/* only valid in index-scan case */
	struct SnapshotData *snapshot;	/* snapshot to unregister at end of scan */
	struct TupleTableSlot *slot;
	YbSysScanBase ybscan;		/* only valid in yb-scan case */
}			SysScanDescData;

#endif							/* RELSCAN_H */
