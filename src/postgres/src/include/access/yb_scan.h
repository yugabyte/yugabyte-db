/*--------------------------------------------------------------------------------------------------
 *
 * yb_scan.h
 *	  prototypes for yb_access/yb_scan.c
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/access/yb_scan.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "skey.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/itup.h"
#include "access/relation.h"
#include "nodes/pathnodes.h"
#include "utils/catcache.h"
#include "utils/resowner.h"
#include "utils/sampling.h"
#include "utils/snapshot.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"

extern PGDLLIMPORT int yb_parallel_range_rows;

/*
 * In fact, only initial fetch uses the default size, we estimate the number
 * based on average key length so far. In future we may be able to estimate
 * key length before fetch starts and always use estimated value.
 */
#define YB_PARTITION_KEYS_DEFAULT_FETCH_SIZE 100

/*
 * Size of the buffer (in bytes) to store the parallel keys.
 * Ideally it should be adjustable to specific relation, since different
 * relations has different key sizes. Moreover, key sizes may vary within a
 * relation.
 */
#define YB_PARTITION_KEY_DATA_CAPACITY 16384

/*
 * State of the parallel key fetch. We only allow one fetching backend at a
 * time, so FETCH_STATUS_WORKING indicates that one of the backends is actively
 * performing the fetch.
 * FETCH_STATUS_IDLE means that DocDB has more keys to return, a worker should
 * request them.
 * FETCH_STATUS_DONE means that all the keys has been fetched, no more fetch is
 * needed.
 */
typedef enum
{
	FETCH_STATUS_IDLE,
	FETCH_STATUS_WORKING,
	FETCH_STATUS_DONE
} FetchStatus;

/*
 * The parallel scan state structure
 * Most important data stored in the structure is the buffer of parallel key,
 * representing bounds of the blocks to scan by the parallel workers.
 * The keys are ordered, and parallel workers take one at a time in their
 * order as the lower bound of the scan range, and peek at the next key to use
 * as the higher bound. The key is essentially a byte array of variable length,
 * its actual length is stored as the prefixes to the actual data. The keys are
 * stored without gaps, so the end of the key is the beginning of the next. Keys
 * are stored continuously, so if the next key does not fit into the space
 * between the end of the last key and the end of the buffer, it is written from
 * the beginning of the buffer (referred as buffer wraparound).
 *
 * The low_offset and high_offset indicate two "active" keys in the buffer. The
 * low_offset is the key to be used and removed by the worker, grabbing next
 * scan range, and the high_offset points to key used by fetching worker: it is
 * the lower bound when making a request, and the point to append keys from the
 * response. In fact, the keys in the buffer form a FIFO queue. As it was
 * mentioned above, there may be unused space at the end of the buffer, because
 * of the key continuity. The key_data_size variable points to the beginning of
 * that space.
 *
 * The keys are fetched from the DocDB by a random worker when number of the
 * keys available in the buffer goes low. The fetch_status indicates current
 * fetch state. The last key in the buffer (highest key) is used as a fetch
 * starting point. For that reason we do not allow to take the last remaining
 * key from the buffer, until DocDB fetch is done.
 *
 * Purpose of other fields of the structure:
 *  - mutex to synchronize access to other fields
 *  - cv_empty is the conditional variable to wait while buffer has keys
 *    available to take
 *  - total_key_size, total_key_count key stats, also used to estimate number
 *    of keys to fetch (provides average key length).
 */
typedef struct YBParallelPartitionKeysData
{
	slock_t		mutex;			/* to synchronize access from the workers */
	ConditionVariable cv_empty;	/* to wait until buffer has more entries */
	Oid			database_oid;	/* database of the target relation */
	Oid			table_relfilenode_oid; /* relfilenode_oid of the target
										  relation */
	bool		is_forward;		/* scan direction */
	FetchStatus fetch_status;	/* if fetch is in progress or completed */
	int			low_offset;		/* offset of the lowest key in the buffer */
	int			high_offset;	/* offset of the highest key in the buffer */
	int			key_count;		/* number of keys in the buffer */
	double		total_key_size;	/* combined length of the keys fetched so far */
	double		total_key_count;	/* number of keys fetched so far */
	int			key_data_size;	/* end of the last entry in the wraparound key_data */
	int			key_data_capacity;	/* YB_PARTITION_KEY_DATA_CAPACITY now, but may change */
	char		key_data[FLEXIBLE_ARRAY_MEMBER];	/* the buffer */
} YBParallelPartitionKeysData;
typedef YBParallelPartitionKeysData *YBParallelPartitionKeys;

/*
 * SCAN PLAN - Two structures.
 * - "struct YbScanPlanData" contains variables that are used during preparing statement.
 * - "struct YbScanDescData" contains variables that are used thru out the life of the statement.
 *
 * YugaByte ScanPlan has two different lists.
 *   Binding list:
 *   - This list is used to receive the user-given data for key columns (WHERE clause).
 *   - "sk_attno" is the INDEX's attnum for the key columns that are used to query data.
 *     In YugaByte, primary index attnum is the same as column attnum in the UserTable.
 *   - "bind_desc" is the description of the index columns (count, types, ...).
 *   - The bind lists don't need to be included in the description as they are only needed
 *     during setup.
 *   Target list:
 *   - This list identifies the user-wanted data to be fetched from the database.
 *   - "target_desc" contains the description of the target columns (count, types, ...).
 *   - The target fields are used for double-checking key values after selecting data
 *     from database. It should be removed once we no longer need to double-check the key values.
 */
typedef struct YbScanDescData
{
#define YB_MAX_SCAN_KEYS (INDEX_MAX_KEYS * 2) /* A pair of lower/upper bounds per column max */

	/* Base of a scan descriptor - Currently it is used either by postgres::heap or Yugabyte.
	 * It contains basic information that defines a scan.
	 * - Relation: Which table to scan.
	 * - Keys: Scan conditions.
	 *   In YB ScanKey could be one of two types:
	 *   o key for regular column
	 *   o key which represents the yb_hash_code function.
	 *   The keys array holds keys of both types.
	 *   All regular keys go before keys for yb_hash_code.
	 *   Keys in range [0, nkeys) are regular keys.
	 *   Keys in range [nkeys, nkeys + nhash_keys) are keys for yb_hash_code
	 *   Such separation allows to process regular and non-regular keys independently.
	 */
	TableScanDescData rs_base;

	/* The handle for the internal YB Select statement. */
	YBCPgStatement handle;
	bool is_exec_done;

	/* Secondary index used in this scan. */
	Relation index;

	/*
	 * ScanKey could be one of two types:
	 *  - key which represents the yb_hash_code function.
	 *  - otherwise
	 * hash_code_keys holds the first type; keys holds the second.
	 */
	ScanKey keys[YB_MAX_SCAN_KEYS];
	/* Number of elements in the above array. */
	int nkeys;
	/*
	 * List of ScanKey for keys which represent the yb_hash_code function.
	 * Prefer List over array because this is likely to have zero or a few
	 * elements in most cases.
	 */
	List *hash_code_keys;

	/*
	 * True if all ordinary (non-yb_hash_code) keys are bound to pggate.  There
	 * could be false negatives: it could say false when they are in fact all
	 * bound.
	 */
	bool all_ordinary_keys_bound;

	/* Destination for queried data from Yugabyte database */
	TupleDesc target_desc;
	AttrNumber target_key_attnums[YB_MAX_SCAN_KEYS];

	/* Kept query-plan control to pass it to PgGate during preparation */
	YBCPgPrepareParameters prepare_params;

	/*
	 * Kept execution control to pass it to PgGate.
	 * - When YBC-index-scan layer is called by Postgres IndexScan functions, it will read the
	 *   "yb_exec_params" from Postgres IndexScan and kept the info in this attribute.
	 *
	 * - YBC-index-scan in-turn will passes this attribute to PgGate to control the index-scan
	 *   execution in YB tablet server.
	 */
	YBCPgExecParameters *exec_params;

	/*
	 * Flag used for bailing out from scan early. Currently used to bail out
	 * from scans where one of the bind conditions is:
	 *   - A comparison operator with null, e.g.: c = null, etc.
	 *   - A search array and is empty.
	 *     Consider an example query,
	 *       select c1,c2 from test
	 *       where c1 = XYZ AND c2 = ANY(ARRAY[]::integer[]);
	 *     The second bind condition c2 = ANY(ARRAY[]::integer[]) will never be
	 *     satisfied.
	 * Hence when, such condition is detected, we bail out from creating and
	 * sending a request to docDB.
	 */
	bool quit_scan;

	YBParallelPartitionKeys pscan;
} YbScanDescData;

extern void ybc_free_ybscan(YbScanDesc ybscan);

/*
 * Access to YB-stored system catalogs (mirroring API from genam.c)
 * We ignore the index id and always do a regular YugaByte scan (Postgres
 * would do either heap scan or index scan depending on the params).
 */
extern SysScanDesc ybc_systable_beginscan(Relation relation,
										  Oid indexId,
										  bool indexOK,
										  Snapshot snapshot,
										  int nkeys,
										  ScanKey key);
extern SysScanDesc ybc_systable_begin_default_scan(Relation relation,
												   Oid indexId,
												   bool indexOK,
												   Snapshot snapshot,
												   int nkeys,
												   ScanKey key);

/*
 * Access to YB-stored system catalogs (mirroring API from heapam.c)
 * We will do a YugaByte scan instead of a heap scan.
 */
extern TableScanDesc ybc_heap_beginscan(Relation relation,
										Snapshot snapshot,
										int nkeys,
										ScanKey key,
										uint32 flags);
extern HeapTuple ybc_heap_getnext(TableScanDesc scanDesc);
extern void ybc_heap_endscan(TableScanDesc scanDesc);

extern void
YbBindDatumToColumn(YBCPgStatement stmt,
					int attr_num,
					Oid type_id,
					Oid collation_id,
					Datum datum,
					bool is_null,
					const YBCPgTypeEntity *null_type_entity);

/* Add targets to the given statement. */
extern void YbDmlAppendTargetSystem(AttrNumber attnum, YBCPgStatement handle);
extern void YbDmlAppendTargetRegular(TupleDesc tupdesc, AttrNumber attnum,
									 YBCPgStatement handle);
extern void YbDmlAppendTargetsAggregate(List *aggrefs, TupleDesc tupdesc,
										Relation index, bool xs_want_itup,
										YBCPgStatement handle);
extern void YbDmlAppendTargets(List *colrefs, YBCPgStatement handle);
/* Add quals to the given statement. */
extern void YbDmlAppendQuals(List *quals, bool is_primary,
							 YBCPgStatement handle);
/* Add column references to the given statement. */
extern void YbDmlAppendColumnRefs(List *colrefs, bool is_primary,
								  YBCPgStatement handle);

/*
 * The ybc_idx API is used to process the following SELECT.
 *   SELECT data FROM heapRelation WHERE rowid IN
 *     ( SELECT rowid FROM indexRelation WHERE key = given_value )
 */
extern YbScanDesc ybcBeginScan(Relation relation,
							   Relation index,
							   bool xs_want_itup,
							   int nkeys,
							   ScanKey key,
							   Scan *pg_scan_plan,
							   PushdownExprs *rel_pushdown,
							   PushdownExprs *idx_pushdown,
							   List *aggrefs,
							   int distinct_prefixlen,
							   YBCPgExecParameters *exec_params,
							   bool is_internal_scan,
							   bool fetch_ybctids_only);

/* Returns whether the given populated ybScan needs PG recheck. */
extern bool YbNeedsPgRecheck(YbScanDesc ybScan);
/*
 * Used in Agg node init phase to determine whether YB preliminary check or PG
 * recheck may be needed.
 */
extern bool YbPredetermineNeedsRecheck(Relation relation,
									   Relation index,
									   bool xs_want_itup,
									   ScanKey keys,
									   int nkeys);

extern HeapTuple ybc_getnext_heaptuple(YbScanDesc ybScan, ScanDirection dir,
									   bool *recheck);
extern IndexTuple ybc_getnext_indextuple(YbScanDesc ybScan, ScanDirection dir,
										 bool *recheck);
extern bool ybc_getnext_aggslot(IndexScanDesc scan, YBCPgStatement handle,
								bool index_only_scan);

extern Oid ybc_get_attcollation(TupleDesc bind_desc, AttrNumber attnum);

/* Number of rows assumed for a YB table if no size estimates exist */
#define YBC_DEFAULT_NUM_ROWS  1000

#define YBC_SINGLE_ROW_SELECTIVITY	(1.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_SINGLE_KEY_SELECTIVITY	(10.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_HASH_SCAN_SELECTIVITY	(100.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_FULL_SCAN_SELECTIVITY	1.0

/*
 * For a partial index the index predicate will filter away some rows.
 * TODO: Evaluate this based on the predicate itself and table stats.
 */
#define YBC_PARTIAL_IDX_PRED_SELECTIVITY 0.8

/*
 * Backwards scans are more expensive in DocDB.
 */
#define YBC_BACKWARDS_SCAN_COST_FACTOR 1.1

/*
 * Uncovered indexes will require extra RPCs to the main table to retrieve the
 * values for all required columns. These requests are now batched in PgGate
 * so the extra cost should be relatively low in general.
 */
#define YBC_UNCOVERED_INDEX_COST_FACTOR 1.1

/* OID for function "yb_hash_code" */
#define YB_HASH_CODE_OID 8020

extern void ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
							bool is_backwards_scan, bool is_seq_scan, bool is_uncovered_idx_scan,
							Cost *startup_cost, Cost *total_cost, Oid index_tablespace_oid);
extern void ybcIndexCostEstimate(struct PlannerInfo *root, IndexPath *path,
								 			Selectivity *selectivity, Cost *startup_cost,
											Cost *total_cost);

/*
 * Fetch a single row for given ybctid into a slot.
 * This API is needed for reading data via index.
 */
extern TM_Result YBCLockTuple(Relation relation, Datum ybctid, RowMarkType mode,
								LockWaitPolicy wait_policy, EState* estate);
/*
 * Fetch a single row for given ybctid into a heap-tuple.
 * This API is needed for reading data from a catalog (system table).
 */
extern bool YbFetchHeapTuple(Relation relation, Datum ybctid, HeapTuple* tuple);
extern void YBCFlushTupleLocks();

/*
 * ANALYZE support: sampling of table data
 */
typedef struct YbSampleData
{
	/* The handle for the internal YB Sample statement. */
	YBCPgStatement handle;

	Relation	relation;
	int			targrows;	/* # of rows to collect */
	double		liverows;	/* # live rows seen */
	double		deadrows;	/* # dead rows seen */
} YbSampleData;

typedef struct YbSampleData *YbSample;

extern YbSample ybBeginSample(Relation rel, int targrows);
extern bool ybSampleNextBlock(YbSample ybSample);
extern int ybFetchSample(YbSample ybSample, HeapTuple *rows);
extern void ybFetchNext(YBCPgStatement handle, TupleTableSlot *slot, Oid relid);

extern int ybParallelWorkers(double numrows);

extern Size yb_estimate_parallel_size(void);
extern void yb_init_partition_key_data(void *data);
extern void ybParallelPrepare(YBParallelPartitionKeys ppk, Relation relation,
							  YBCPgExecParameters *exec_params,
							  bool is_forward);
extern bool ybParallelNextRange(YBParallelPartitionKeys ppk,
								const char **low_bound, size_t *low_bound_size,
								const char **high_bound,
								size_t *high_bound_size);
