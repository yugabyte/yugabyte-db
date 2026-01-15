/*--------------------------------------------------------------------------------------------------
 *
 * yb_scan.c
 *	  YugaByte catalog scan API.
 *	  This is used to access data from YugaByte's system catalog tables.
 *
 * Copyright (c) YugabyteDB, Inc.
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
 * src/backend/access/yb_access/yb_scan.c
 *
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/nbtree.h"
#include "access/relation.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "access/yb_pg_inherits_scan.h"
#include "access/yb_scan.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/pg_database.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/yb_tablegroup.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "nodes/pathnodes.h"
#include "optimizer/cost.h"
#include "optimizer/optimizer.h"
#include "optimizer/paths.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "pgstat.h"
#include "postmaster/bgworker_internals.h"	/* for MAX_PARALLEL_WORKER_LIMIT */
#include "utils/datum.h"
#include "utils/elog.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/resowner_private.h"
#include "utils/selfuncs.h"
#include "utils/snapmgr.h"
#include "utils/spccache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"
#include "yb/yql/pggate/util/ybc_guc.h"
#include "yb/yql/pggate/ybc_gflags.h"
#include "ybgate/ybgate_api.h"

typedef struct YbAttnumBmsState
{
	Bitmapset  *bms;
	const AttrNumber min_attr;
} YbAttnumBmsState;

static inline YbAttnumBmsState
ybcAttnumBmsConstruct()
{
	return (YbAttnumBmsState)
	{
		.bms = NULL,
			.min_attr = YBSystemFirstLowInvalidAttributeNumber,
	};
}

static inline void
ybcAttnumBmsDestroy(YbAttnumBmsState *state)
{
	bms_free(state->bms);
	state->bms = NULL;
}

static inline int
ybcAttnumBmsIndex(const YbAttnumBmsState *state, AttrNumber attnum)
{
	return YBAttnumToBmsIndexWithMinAttr(state->min_attr, attnum);
}

static inline AttrNumber
ybcAttnumBmsAttnum(const YbAttnumBmsState *state, int idx)
{
	return YBBmsIndexToAttnumWithMinAttr(state->min_attr, idx);
}

static inline void
ybcAttnumBmsAdd(YbAttnumBmsState *state, AttrNumber attnum)
{
	state->bms = bms_add_member(state->bms, ybcAttnumBmsIndex(state, attnum));
}

static inline void
ybcAttnumBmsDel(YbAttnumBmsState *state, AttrNumber attnum)
{
	state->bms = bms_del_member(state->bms, ybcAttnumBmsIndex(state, attnum));
}

static inline bool
ybcAttnumBmsExists(const YbAttnumBmsState *state, AttrNumber attnum)
{
	return bms_is_member(ybcAttnumBmsIndex(state, attnum), state->bms);
}

static inline bool
ybcAttnumBmsDelIfExists(YbAttnumBmsState *state, AttrNumber attnum)
{
	if (!ybcAttnumBmsExists(state, attnum))
		return false;

	ybcAttnumBmsDel(state, attnum);
	return true;
}

static inline bool
ybcAttnumBmsIsEmpty(const YbAttnumBmsState *state)
{
	return bms_is_empty(state->bms);
}

typedef struct YbScanPlanData
{
	/* The relation where to read data from */
	Relation	target_relation;

	/* Primary and hash key columns of the referenced table/relation. */
	Bitmapset  *primary_key;
	Bitmapset  *hash_key;

	/* Set of key columns whose values will be used for scanning. */
	Bitmapset  *sk_cols;

	/* Description and attnums of the columns to bind */
	TupleDesc	bind_desc;
	AttrNumber	bind_key_attnums[YB_MAX_SCAN_KEYS];
} YbScanPlanData;

typedef YbScanPlanData *YbScanPlan;

typedef struct YbDefaultSysScanData
{
	YbSysScanBaseData base;
	YbScanDesc	ybscan;
} YbDefaultSysScanData;

typedef struct YbDefaultSysScanData *YbDefaultSysScan;

static void
ybcAddAttributeColumn(YbScanPlan scan_plan, AttrNumber attnum)
{
	const int	idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

	if (bms_is_member(idx, scan_plan->primary_key))
		scan_plan->sk_cols = bms_add_member(scan_plan->sk_cols, idx);
}

/*
 * Checks if an attribute is a hash or primary key column and note it in
 * the scan plan.
 */
static void
ybcCheckPrimaryKeyAttribute(YbScanPlan scan_plan,
							YbcPgTableDesc ybc_table_desc,
							AttrNumber attnum)
{
	YbcPgColumnInfo column_info = {0};

	/*
	 * TODO(neil) We shouldn't need to upload YugaByte table descriptor here because the structure
	 * Postgres::Relation already has all information.
	 * - Primary key indicator: IndexRelation->rd_index->indisprimary
	 * - Number of key columns: IndexRelation->rd_index->indnkeyatts
	 * - Number of all columns: IndexRelation->rd_index->indnatts
	 * - Hash, range, etc: IndexRelation->rd_indoption (Bits INDOPTION_HASH, RANGE, etc)
	 */
	HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
											   attnum,
											   &column_info), ybc_table_desc);

	int			idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

	if (column_info.is_hash)
		scan_plan->hash_key = bms_add_member(scan_plan->hash_key, idx);
	if (column_info.is_primary)
		scan_plan->primary_key = bms_add_member(scan_plan->primary_key, idx);
}

/*
 * Get YugaByte-specific table metadata and load it into the scan_plan.
 * Currently only the hash and primary key info.
 */
static void
ybcLoadTableInfo(Relation relation, YbScanPlan scan_plan)
{
	Oid			dboid = YBCGetDatabaseOid(relation);
	YbcPgTableDesc ybc_table_desc = NULL;

	HandleYBStatus(YBCPgGetTableDesc(dboid, YbGetRelfileNodeId(relation),
									 &ybc_table_desc));

	for (AttrNumber attnum = 1; attnum <= relation->rd_att->natts; attnum++)
		ybcCheckPrimaryKeyAttribute(scan_plan, ybc_table_desc, attnum);
}

static Oid
ybc_get_atttypid(TupleDesc bind_desc, AttrNumber attnum)
{
	return attnum > 0 ? TupleDescAttr(bind_desc, attnum - 1)->atttypid :
		SystemAttributeDefinition(attnum)->atttypid;
}

/*
 * Bind a scan key.
 */
static void
YbBindColumn(YbScanDesc ybScan, TupleDesc bind_desc,
			 AttrNumber attnum, Datum value, bool is_null)
{
	Oid			atttypid = ybc_get_atttypid(bind_desc, attnum);
	Oid			attcollation = YBEncodingCollation(ybScan->handle, attnum,
												   ybc_get_attcollation(bind_desc,
																		attnum));

	YbcPgExpr	ybc_expr = YBCNewConstant(ybScan->handle, atttypid, attcollation,
										  value, is_null);

	HandleYBStatus(YBCPgDmlBindColumn(ybScan->handle, attnum, ybc_expr));
}

static void
YbBindColumnCondBetween(YbScanDesc ybScan,
						TupleDesc bind_desc, AttrNumber attnum,
						bool start_valid, bool start_inclusive, Datum value,
						bool end_valid, bool end_inclusive, Datum value_end)
{
	/* Special handling of quals on ybctid column. */
	if (attnum == YBTupleIdAttributeNumber)
	{
		HandleYBStatus(YBCPgDmlBindBounds(ybScan->handle,
										  start_valid ? value : 0,
										  start_inclusive,
										  end_valid ? value_end : 0,
										  end_inclusive));
		return;
	}

	Oid			atttypid = ybc_get_atttypid(bind_desc, attnum);
	Oid			attcollation = YBEncodingCollation(ybScan->handle, attnum,
												   ybc_get_attcollation(bind_desc,
																		attnum));

	YbcPgExpr	ybc_expr = (start_valid ?
							YBCNewConstant(ybScan->handle,
										   atttypid,
										   attcollation,
										   value,
										   false /* isnull */ ) :
							NULL);
	YbcPgExpr	ybc_expr_end = (end_valid ?
								YBCNewConstant(ybScan->handle,
											   atttypid,
											   attcollation,
											   value_end,
											   false /* isnull */ ) :
								NULL);

	HandleYBStatus(YBCPgDmlBindColumnCondBetween(ybScan->handle, attnum,
												 ybc_expr, start_inclusive,
												 ybc_expr_end, end_inclusive));
}

static void
YbBindColumnNotNull(YbScanDesc ybScan, TupleDesc bind_desc, AttrNumber attnum)
{
	HandleYBStatus(YBCPgDmlBindColumnCondIsNotNull(ybScan->handle, attnum));
}

/*
 * Bind an array of scan keys for a column.
 */
static void
ybcBindColumnCondIn(YbScanDesc ybScan, TupleDesc bind_desc, AttrNumber attnum,
					int nvalues, Datum *values, bool bind_to_null)
{
	Oid			atttypid = ybc_get_atttypid(bind_desc, attnum);
	Oid			attcollation = YBEncodingCollation(ybScan->handle, attnum,
												   ybc_get_attcollation(bind_desc,
																		attnum));

	YbcPgExpr	colref = YBCNewColumnRef(ybScan->handle, attnum, atttypid,
										 attcollation, NULL);

	int			total_num_values = nvalues + (bind_to_null ? 1 : 0);
	YbcPgExpr	ybc_exprs[total_num_values];	/* VLA - scratch space */

	/* First, create expr for non-null values. */
	for (int i = 0; i < nvalues; i++)
		ybc_exprs[i] = YBCNewConstant(ybScan->handle, atttypid, attcollation,
									  values[i], false /* is_null */ );

	/* Create expr for NULL if bind_to_null is set. */
	if (bind_to_null)
		ybc_exprs[nvalues] = YBCNewConstant(ybScan->handle, atttypid,
											attcollation, (Datum) 0,
											true /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumnCondIn(ybScan->handle, colref,
											total_num_values, ybc_exprs));
}

/*
 * Bind an array of scan keys for a tuple of columns.
 */
static void
ybcBindTupleExprCondIn(YbScanDesc ybScan,
					   TupleDesc bind_desc,
					   int n_attnum_values,
					   AttrNumber *attnum,
					   int nvalues,
					   Datum *values)
{
	Assert(nvalues > 0);
	YbcPgExpr	ybc_rhs_exprs[nvalues];

	YbcPgExpr	ybc_elems_exprs[n_attnum_values];	/* VLA - scratch space */
	Datum		datum_values[n_attnum_values];
	bool		is_null[n_attnum_values];

	Oid			tupType =
		HeapTupleHeaderGetTypeId(DatumGetHeapTupleHeader(values[0]));
	Oid			tupTypmod =
		HeapTupleHeaderGetTypMod(DatumGetHeapTupleHeader(values[0]));
	YbcPgTypeAttrs type_attrs = {tupTypmod};

	/* Form the lhs tuple. */
	for (int i = 0; i < n_attnum_values; i++)
	{
		Oid			atttypid = ybc_get_atttypid(bind_desc, attnum[i]);
		Oid			attcollation = YBEncodingCollation(ybScan->handle, attnum[i],
													   ybc_get_attcollation(bind_desc,
																			attnum[i]));

		ybc_elems_exprs[i] = YBCNewColumnRef(ybScan->handle, attnum[i],
											 atttypid, attcollation, NULL);
	}

	YbcPgExpr	lhs = YBCNewTupleExpr(ybScan->handle, &type_attrs,
									  n_attnum_values, ybc_elems_exprs);

	TupleDesc	tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);

	HeapTupleData tuple;

	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;

	/* Form the list of tuples for the RHS. */
	for (int i = 0; i < nvalues; i++)
	{
		tuple.t_len = HeapTupleHeaderGetDatumLength(values[i]);
		tuple.t_data = DatumGetHeapTupleHeader(values[i]);
		heap_deform_tuple(&tuple, tupdesc, datum_values, is_null);
		for (int j = 0; j < n_attnum_values; j++)
		{
			Oid			atttypid = ybc_get_atttypid(bind_desc, attnum[j]);
			Oid			attcollation = YBEncodingCollation(ybScan->handle, attnum[j],
														   ybc_get_attcollation(bind_desc,
																				attnum[j]));

			ybc_elems_exprs[j] = YBCNewConstant(ybScan->handle, atttypid,
												attcollation, datum_values[j],
												is_null[j]);
		}

		ybc_rhs_exprs[i] = YBCNewTupleExpr(ybScan->handle, &type_attrs,
										   n_attnum_values, ybc_elems_exprs);
	}

	HandleYBStatus(YBCPgDmlBindColumnCondIn(ybScan->handle, lhs, nvalues,
											ybc_rhs_exprs));

	ReleaseTupleDesc(tupdesc);
}

/*
 * Utility method to bind const to column.
 */
void
YbBindDatumToColumn(YbcPgStatement stmt,
					int attr_num,
					Oid type_id,
					Oid collation_id,
					Datum datum,
					bool is_null,
					const YbcPgTypeEntity *null_type_entity)
{
	YbcPgExpr	expr;
	const YbcPgTypeEntity *type_entity;

	if (is_null && null_type_entity)
		type_entity = null_type_entity;
	else
		type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);

	YbcPgCollationInfo collation_info;

	YBGetCollationInfo(collation_id, type_entity, datum, is_null,
					   &collation_info);

	HandleYBStatus(YBCPgNewConstant(stmt, type_entity,
									collation_info.collate_is_valid_non_c,
									collation_info.sortkey,
									datum, is_null, &expr));

	HandleYBStatus(YBCPgDmlBindColumn(stmt, attr_num, expr));
}

static void
YbDmlAppendTargetImpl(YbcPgStatement handle, AttrNumber attnum, Oid typid, Oid collation, Oid typmod)
{
	const YbcPgTypeAttrs type_attrs = {.typmod = typmod};

	HandleYBStatus(YBCPgDmlAppendTarget(handle,
										YBCNewColumnRef(handle, attnum, typid,
														collation, &type_attrs),
										false /* is_for_secondary_index */ ));
}

/*
 * Add a system column as target to the given statement handle.
 */
void
YbDmlAppendTargetSystem(AttrNumber attnum, YbcPgStatement handle)
{
	Assert(attnum < 0);
	YbDmlAppendTargetImpl(handle, attnum, InvalidOid /* typid */ ,
						  InvalidOid /* collation */ , -1 /* typmod */ );
}

void
YbDmlAppendTargetRegularAttr(const FormData_pg_attribute *attr,
							 YbcPgStatement handle)
{
	Assert(attr->attnum > 0);
	Assert(!attr->attisdropped);

	YbDmlAppendTargetImpl(handle, attr->attnum, attr->atttypid,
						  attr->attcollation, attr->atttypmod);

}

/*
 * Add a regular column as target to the given statement handle.
 * Assume tupdesc's relation is the same as handle's target relation.
 */
void
YbDmlAppendTargetRegular(TupleDesc tupdesc, AttrNumber attnum,
						 YbcPgStatement handle)
{
	Assert(attnum > 0);
	YbDmlAppendTargetRegularAttr(TupleDescAttr(tupdesc, attnum - 1), handle);
}

static void
ybcUpdateFKCache(YbScanDesc ybScan, Datum ybctid)
{
	if (!ybScan->exec_params)
		return;

	switch (ybScan->exec_params->rowmark)
	{
		case ROW_MARK_EXCLUSIVE:
		case ROW_MARK_NOKEYEXCLUSIVE:
		case ROW_MARK_SHARE:
		case ROW_MARK_KEYSHARE:
			YBCPgAddIntoForeignKeyReferenceCache(YbGetRelfileNodeId(ybScan->rs_base.rs_rd),
												 ybctid);
			break;
		case ROW_MARK_REFERENCE:
		case ROW_MARK_COPY:
			break;
	}
}

static HeapTuple
ybcFetchNextHeapTuple(YbScanDesc ybScan, ScanDirection dir)
{
	HeapTuple	tuple = NULL;
	bool		has_data = false;
	TupleDesc	tupdesc = ybScan->target_desc;
	TableScanDesc tsdesc = (TableScanDesc) ybScan;
	Datum	   *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool	   *nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YbcPgSysColumns syscols;

	/*
	 * In the case of parallel scan we need to obtain boundaries from the pscan
	 * before the scan is executed. Also empty row from parallel range scan does
	 * not mean scan is done, it means the range is done and we need to pick up
	 * next. No rows from parallel range is possible, hence the loop.
	 */
	while (true)
	{
		/* Need to execute the request */
		if (!ybScan->is_exec_done)
		{
			/* Parallel mode: pick up parallel block first */
			if (ybScan->pscan != NULL)
			{
				YBParallelPartitionKeys parallel_scan = ybScan->pscan;
				const char *low_bound;
				size_t		low_bound_size;
				const char *high_bound;
				size_t		high_bound_size;

				/*
				 * If range is found, apply the boundaries, false means the scan
				 * is done for that worker.
				 */
				if (ybParallelNextRange(parallel_scan,
										&low_bound, &low_bound_size,
										&high_bound, &high_bound_size))
				{
					HandleYBStatus(YBCPgDmlBindRange(ybScan->handle,
													 low_bound, low_bound_size,
													 high_bound,
													 high_bound_size));
					if (low_bound)
						pfree((void *) low_bound);
					if (high_bound)
						pfree((void *) high_bound);
				}
				else
					return NULL;
				/*
				 * Use unlimited fetch.
				 * Parallel scan range is already of limited size, it is
				 * unlikely to exceed the message size, but may save some RPCs.
				 */
				ybScan->exec_params->limit_use_default = true;
				ybScan->exec_params->yb_fetch_row_limit = 0;
				ybScan->exec_params->yb_fetch_size_limit = 0;
			}
			/* Set scan direction, if matters */
			if (ScanDirectionIsForward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, true));
			else if (ScanDirectionIsBackward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, false));

			HandleYBStatus(YBCPgExecSelect(ybScan->handle,
										   ybScan->exec_params));
			ybScan->is_exec_done = true;
		}

		/* Fetch one row. */
		YbcStatus	status = YBCPgDmlFetch(ybScan->handle,
										   tupdesc->natts,
										   (uint64_t *) values,
										   nulls,
										   &syscols,
										   &has_data);

		if (status && !IsolationIsSerializable())
		{
			const uint32_t err_code = YBCStatusPgsqlError(status);

			if (ybScan->exec_params != NULL && err_code == ERRCODE_YB_TXN_CONFLICT)
			{
				elog(DEBUG2, "Error when trying to lock row. "
					 "pg_wait_policy=%d docdb_wait_policy=%d message=%s",
					 ybScan->exec_params->pg_wait_policy,
					 ybScan->exec_params->docdb_wait_policy,
					 YBCStatusMessageBegin(status));
				YBCFreeStatus(status);
				status = NULL;
				if (ybScan->exec_params->pg_wait_policy == LockWaitError)
					ereport(ERROR,
							(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
							 errmsg("could not obtain lock on row in relation \"%s\"",
									RelationGetRelationName(tsdesc->rs_rd))));
				else
					ereport(ERROR,
							(errcode(ERRCODE_YB_TXN_CONFLICT),
							 errmsg("could not serialize access due to concurrent update")));
			}
			else if (err_code == ERRCODE_YB_TXN_SKIP_LOCKING)
			{
				/* For skip locking, it's correct to simply return no results. */
				has_data = false;
				YBCFreeStatus(status);
				status = NULL;
			}
		}

		HandleYBStatus(status);

		if (has_data)
		{
			tuple = heap_form_tuple(tupdesc, values, nulls);

			if (syscols.ybctid != NULL)
			{
				HEAPTUPLE_YBCTID(tuple) = PointerGetDatum(syscols.ybctid);
				ybcUpdateFKCache(ybScan, HEAPTUPLE_YBCTID(tuple));
			}
			tuple->t_tableOid = RelationGetRelid(tsdesc->rs_rd);
			break;
		}
		else if (ybScan->pscan != NULL)
			ybScan->is_exec_done = false;
		else
			break;
	}
	pfree(values);
	pfree(nulls);

	return tuple;
}

static IndexTuple
ybcFetchNextIndexTuple(YbScanDesc ybScan, ScanDirection dir)
{
	IndexTuple	tuple = NULL;
	bool		has_data = false;
	Relation	index = ybScan->index;
	TupleDesc	tupdesc = ybScan->target_desc;
	Datum	   *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool	   *nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YbcPgSysColumns syscols;

	/*
	 * In the case of parallel scan we need to obtain boundaries from the pscan
	 * before the scan is executed. Also empty row from parallel range scan does
	 * not mean scan is done, it means the range is done and we need to pick up
	 * next. No rows from parallel range is possible, hence the loop.
	 */
	while (true)
	{
		/* Need to execute the request */
		if (!ybScan->is_exec_done)
		{
			/* Parallel mode: pick up parallel block first */
			if (ybScan->pscan != NULL)
			{
				YBParallelPartitionKeys parallel_scan = ybScan->pscan;
				const char *low_bound;
				size_t		low_bound_size;
				const char *high_bound;
				size_t		high_bound_size;

				/*
				 * If range is found, apply the boundaries, false means the scan
				 * is done for that worker.
				 */
				if (ybParallelNextRange(parallel_scan,
										&low_bound, &low_bound_size,
										&high_bound, &high_bound_size))
				{
					HandleYBStatus(YBCPgDmlBindRange(ybScan->handle, low_bound,
													 low_bound_size, high_bound,
													 high_bound_size));
					if (low_bound)
						pfree((void *) low_bound);
					if (high_bound)
						pfree((void *) high_bound);
				}
				else
					return NULL;
				/*
				 * Use unlimited fetch.
				 * Parallel scan range is already of limited size, it is
				 * unlikely to exceed the message size, but may save some RPCs.
				 */
				ybScan->exec_params->limit_use_default = true;
				ybScan->exec_params->yb_fetch_row_limit = 0;
				ybScan->exec_params->yb_fetch_size_limit = 0;
			}
			/* Set scan direction, if matters */
			if (ScanDirectionIsForward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, true));
			else if (ScanDirectionIsBackward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, false));

			HandleYBStatus(YBCPgExecSelect(ybScan->handle,
										   ybScan->exec_params));
			ybScan->is_exec_done = true;
		}

		/* Fetch one row. */
		HandleYBStatus(YBCPgDmlFetch(ybScan->handle,
									 tupdesc->natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data));

		if (has_data)
		{
			/*
			 * Return the IndexTuple. If this is a primary key, reorder the
			 * values first as expected in the index's column order first.
			 */
			if (index->rd_index->indisprimary)
			{
				Assert(index->rd_index->indnatts <= INDEX_MAX_KEYS);

				Datum		ivalues[INDEX_MAX_KEYS];
				bool		inulls[INDEX_MAX_KEYS];

				for (int i = 0; i < index->rd_index->indnatts; i++)
				{
					AttrNumber	attno = index->rd_index->indkey.values[i];

					ivalues[i] = values[attno - 1];
					inulls[i] = nulls[attno - 1];
				}

				tuple = index_form_tuple(RelationGetDescr(index), ivalues, inulls);
				if (syscols.ybctid != NULL)
				{
					INDEXTUPLE_BASECTID(tuple) = PointerGetDatum(syscols.ybctid);
					ybcUpdateFKCache(ybScan, INDEXTUPLE_BASECTID(tuple));
				}
			}
			else
			{
				tuple = index_form_tuple(tupdesc, values, nulls);
				if (syscols.ybbasectid != NULL)
				{
					INDEXTUPLE_BASECTID(tuple) = PointerGetDatum(syscols.ybbasectid);
					ybcUpdateFKCache(ybScan, INDEXTUPLE_BASECTID(tuple));
				}

				/* Fields used by yb_index_check() */
				if (syscols.ybuniqueidxkeysuffix != NULL)
					tuple->t_ybuniqueidxkeysuffix =
						PointerGetDatum(syscols.ybuniqueidxkeysuffix);
				if (syscols.ybctid != NULL)
					tuple->t_ybindexrowybctid = PointerGetDatum(syscols.ybctid);
			}
			break;
		}
		else if (ybScan->pscan != NULL)
			ybScan->is_exec_done = false;
		else
			break;
	}
	pfree(values);
	pfree(nulls);

	return tuple;
}

static Oid
ybcCalculateIndexRelfileNodeId(Relation rel, Relation index,
							   const YbcPgPrepareParameters *params)
{
	Assert(index);
	if (!index->rd_index->indisprimary)
		return YbGetRelfileNodeId(index);
	else if (params->index_only_scan)
		return YbGetRelfileNodeId(rel);
	return InvalidOid;
}

/*
 * True if the scan involving table and index is such that the index data is
 * sharded together with the table.
 * TODO(#25940): this logic is not completely clean, as indicated by the
 * todos below.
 */
bool
YbIsScanningEmbeddedIdx(Relation table, Relation index)
{
	YbcTableProperties yb_table_properties_table;
	bool		is_embedded;

	yb_table_properties_table = YbGetTableProperties(table);

	/*
	 * There are a few cases where embedding happens.
	 * 1. System table: all system tables and indexes are specially colocated
	 *    to the sys catalog tablet.
	 * 2. Copartitioning: some indexes may use copartitioning, which shards the
	 *    table and index together.
	 */
	is_embedded = (IsSystemRelation(table) ||
				   (index && index->rd_indam->yb_amiscopartitioned));

	/*
	 * 3. Colocation: the table and index may be colocated to the same tablet:
	 *    - If ysql_enable_colocated_tables_with_tablespaces, check that the
	 *      table and index are in the same colocation tablet using
	 *      tablegroup_oid.
	 *      - TODO(#25940): index->rd_index->indisprimary seems irrelevant and
	 *        should not be a pass condition.
	 *    - Else, simply check for colocation of the table because the index
	 *      should follow the table.
	 *      - TODO(#25940): index being NULL or pk index should not be a pass
	 *        condition.
	 *    - TODO(#25940): the gflag could be turned on/off in the lifetime of
	 *      a cluster, so it shouldn't even be involved in this logic.
	 *      Everything should be validated, likely using the tablegroup_oid
	 *      check, assuming that holds even when indexes are created when the
	 *      flag is false.
	 */
	if (*YBCGetGFlags()->ysql_enable_colocated_tables_with_tablespaces)
		is_embedded |= (yb_table_properties_table->is_colocated &&
						((index && index->rd_index->indisprimary) ||
						 (index &&
						  (YbGetTableProperties(index)->tablegroup_oid ==
						   yb_table_properties_table->tablegroup_oid))));
	else
		is_embedded |= yb_table_properties_table->is_colocated;

	return is_embedded;
}

/*
 * Set up scan plan.
 * This function sets up target and bind columns for each type of scans.
 *    SELECT <Target_columns> FROM <Table> WHERE <Binds>
 *
 * 1. SequentialScan(Table) and PrimaryIndexScan(Table): index = 0
 *    - Table can be systable or usertable.
 *    - YugaByte doesn't have a separate PrimaryIndexTable. It's a special case.
 *    - Both target and bind descriptors are specified by the <Table>
 *
 * 2. IndexScan(SysTable, Index).
 *    - Target descriptor is specifed by the SysTable.
 *    - Bind descriptor is specified by the IndexTable.
 *    - For this scan, YugaByte returns a heap-tuple, which has all user's requested data.
 *
 * 3. IndexScan(UserTable, Index)
 *    - Both target and bind descriptors are specifed by the IndexTable.
 *    - For this scan, YugaByte returns an index-tuple, which has a ybctid (ROWID) to be used for
 *      querying data from the UserTable.
 *    - TODO(neil) By batching ybctid and processing it on YugaByte for all index-scans, the target
 *      for index-scan on regular table should also be the table itself (relation).
 *
 * 4. IndexOnlyScan(Table, Index)
 *    - Table can be systable or usertable.
 *    - Both target and bind descriptors are specifed by the IndexTable.
 *    - For this scan, YugaByte ALWAYS return index-tuple, which is expected by Postgres layer.
 * 5. BitmapIndexScan(Index)
 *    - Table is null because we are only interested in getting ybctids from the index.
 */
static void
ybcSetupScanPlan(bool xs_want_itup, YbScanDesc ybScan, YbScanPlan scan_plan)
{
	TableScanDesc tsdesc = (TableScanDesc) ybScan;
	Relation	relation = tsdesc->rs_rd;
	Relation	index = ybScan->index;
	int			i;

	memset(scan_plan, 0, sizeof(*scan_plan));

	ybScan->prepare_params.embedded_idx = YbIsScanningEmbeddedIdx(relation,
																  index);

	if (index)
	{
		YbcPgPrepareParameters *params = &ybScan->prepare_params;

		params->index_only_scan = xs_want_itup;
		params->index_relfilenode_oid =
			ybcCalculateIndexRelfileNodeId(relation, index, params);
	}

	/* Setup descriptors for target and bind. */
	if (!index || index->rd_index->indisprimary)
	{
		/*
		 * SequentialScan or PrimaryIndexScan or BitmapIndexScan on the primary index
		 * - YugaByte does not have a separate table for PrimaryIndex.
		 * - The target table descriptor, where data is read and returned, is the main table.
		 * - The binding table descriptor, whose column is bound to values, is also the main table.
		 */
		scan_plan->target_relation = relation;
		ybcLoadTableInfo(relation, scan_plan);
		ybScan->target_desc = RelationGetDescr(relation);
		scan_plan->bind_desc = RelationGetDescr(relation);
	}
	else
	{
		/*
		 * Index-Scan: SELECT data FROM UserTable WHERE rowid IN (SELECT ybctid FROM indexTable)
		 *
		 */

		if (ybScan->prepare_params.fetch_ybctids_only)
		{
			/*
			 * BitmapIndexScan
			 * - A BitmapIndexScan accesses only the index, not the main table.
			 */
			scan_plan->target_relation = index;
			ybScan->target_desc = RelationGetDescr(index);
		}
		else if (ybScan->prepare_params.index_only_scan || relation == NULL)
		{
			/*
			 * IndexOnlyScan
			 * - This special case is optimized where data is read from index table.
			 * - The target table descriptor, where data is read and returned, is the index table.
			 * - The binding table descriptor, whose column is bound to values, is also the index table.
			 */
			scan_plan->target_relation = index;
			ybScan->target_desc = RelationGetDescr(index);
		}
		else
		{
			/*
			 * IndexScan ( SysTable / UserTable)
			 * - YugaByte will use the binds to query base-ybctid in the index table, which is then used
			 *   to query data from the main table.
			 * - The target table descriptor, where data is read and returned, is the main table.
			 * - The binding table descriptor, whose column is bound to values, is the index table.
			 */
			scan_plan->target_relation = relation;
			ybScan->target_desc = RelationGetDescr(relation);
		}

		ybcLoadTableInfo(index, scan_plan);
		scan_plan->bind_desc = RelationGetDescr(index);
	}

	/*
	 * Setup bind and target attnum of ScanKey.
	 * - The target-attnum comes from the table that is being read by the scan
	 * - The bind-attnum comes from the table that is being scan by the scan.
	 *
	 * Examples:
	 * - For IndexScan(Table, Index), Table is used for targets, but Index is for binds.
	 * - For IndexOnlyScan(Table, Index), only Index is used to setup both target and bind.
	 */
	for (i = 0; i < ybScan->nkeys; i++)
	{
		ScanKey		key = ybScan->keys[i];

		if (!index)
		{
			/* Sequential scan */
			ybScan->target_key_attnums[i] = key->sk_attno;
			scan_plan->bind_key_attnums[i] = key->sk_attno;
		}
		else if (index->rd_index->indisprimary)
		{
			/*
			 * PrimaryIndex scan: This is a special case in YugaByte. There is no PrimaryIndexTable.
			 * The table itself will be scanned.
			 */
			ybScan->target_key_attnums[i] = scan_plan->bind_key_attnums[i] =
				index->rd_index->indkey.values[key->sk_attno - 1];
		}
		else if (ybScan->prepare_params.index_only_scan)
		{
			/*
			 * IndexOnlyScan(Table, Index) returns IndexTuple.
			 * Use the index attnum for both targets and binds.
			 */
			scan_plan->bind_key_attnums[i] = key->sk_attno;
			ybScan->target_key_attnums[i] = key->sk_attno;
		}
		else
		{
			/*
			 * IndexScan(Table, Index) returns HeapTuple.
			 * Use Table attnum for targets. Use its Index attnum for binds.
			 */
			scan_plan->bind_key_attnums[i] = key->sk_attno;
			ybScan->target_key_attnums[i] =
				index->rd_index->indkey.values[key->sk_attno - 1];
		}
	}
}

static bool
ybc_should_pushdown_op(YbScanPlan scan_plan, AttrNumber attnum, int op_strategy)
{
	const int	idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

	switch (op_strategy)
	{
		case BTEqualStrategyNumber:
			return bms_is_member(idx, scan_plan->primary_key);

		case BTLessStrategyNumber:
		case BTLessEqualStrategyNumber:
		case BTGreaterEqualStrategyNumber:
		case BTGreaterStrategyNumber:
			/* range key */
			return (!bms_is_member(idx, scan_plan->hash_key) &&
					bms_is_member(idx, scan_plan->primary_key));

		default:
			/* TODO: support other logical operators */
			return false;
	}
}

static bool
YbIsHashCodeSearch(ScanKey key)
{
	bool		is_hash_search = (key->sk_flags & YB_SK_IS_HASHED) != 0;

	/* We currently don't support hash code search with any other flags */
	Assert(!is_hash_search || key->sk_flags == YB_SK_IS_HASHED);
	return is_hash_search;
}

/*
 * Is this a basic (c =/</<=/>=/> value) (in)equality condition possibly on
 * hashed values?
 * TODO: The null value case (SK_ISNULL) should always evaluate to false
 *       per SQL semantics but in DocDB it will be true. So this case
 *       will require PG filtering (for null values only).
 */
static bool
YbIsBasicOpSearch(ScanKey key)
{
	return (key->sk_flags == 0 ||
			key->sk_flags == SK_ISNULL ||
			YbIsHashCodeSearch(key));
}

/*
 * Is this a null search (c IS NULL) -- same as equality cond for DocDB.
 */
static bool
YbIsSearchNull(ScanKey key)
{
	return key->sk_flags == (SK_ISNULL | SK_SEARCHNULL);
}

/*
 * Is this a not-null search (c IS NOT NULL).
 */
static bool
YbIsSearchNotNull(ScanKey key)
{
	return key->sk_flags == (SK_ISNULL | SK_SEARCHNOTNULL);
}

/*
 * Is this an array search (c = ANY(..) or c IN ..).
 */
static bool
YbIsSearchArray(ScanKey key)
{
	return key->sk_flags & SK_SEARCHARRAY;
}

static bool
YbIsRowHeader(ScanKey key)
{
	return key->sk_flags & SK_ROW_HEADER;
}

static bool
YbSearchArrayRetainNulls(ScanKey key)
{
	return key->sk_flags & YB_SK_SEARCHARRAY_RETAIN_NULLS;
}

/*
 * Is the condition never TRUE because of c {=|<|<=|>=|>} NULL, etc.?
 */
static bool
YbIsNeverTrueNullCond(ScanKey key)
{
	return ((key->sk_flags & SK_ISNULL) != 0 &&
			(key->sk_flags & (SK_SEARCHNULL | SK_SEARCHNOTNULL)) == 0);
}

static int
YbGetLengthOfKey(const ScanKey *key_ptr)
{
	if (!YbIsRowHeader(key_ptr[0]))
		return 1;

	int			length_of_key = 0;

	while (!(key_ptr[length_of_key]->sk_flags & SK_ROW_END))
		length_of_key++;

	/* We also want to include the last element. */
	length_of_key++;
	return length_of_key;
}

/*
 * Add ordinary key to ybScan.
 */
static void
ybAddOrdinaryScanKey(ScanKey key, YbScanDesc ybScan)
{
	if (ybScan->nkeys >= YB_MAX_SCAN_KEYS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_COLUMNS),
				 errmsg("cannot use more than %d predicates in a table or index scan",
						YB_MAX_SCAN_KEYS)));
	ybScan->keys[ybScan->nkeys++] = key;
}

/*
 * Extract keys and store to ybScan.
 */
static void
ybExtractScanKeys(ScanKey keys, int nkeys, YbScanDesc ybScan)
{
	for (int i = 0; i < nkeys; ++i)
	{
		ScanKey		key = &keys[i];

		if (YbIsHashCodeSearch(key))
		{
			Assert(!YbIsRowHeader(key));
			ybScan->hash_code_keys = lappend(ybScan->hash_code_keys, key);
		}
		else
		{
			ybAddOrdinaryScanKey(key, ybScan);

			/* Extract subkeys in case of row comparison. */
			if (YbIsRowHeader(key))
			{
				ScanKey		subkey = (ScanKey) key->sk_argument;

				do
				{
					ybAddOrdinaryScanKey(subkey, ybScan);
				}
				while (((subkey++)->sk_flags & SK_ROW_END) == 0);
			}
		}
	}
}

/*
 * Return whether the given conditions are unsatisfiable regardless of the
 * values in the index because of always FALSE or UNKNOWN conditions.
 */
static bool
YbIsUnsatisfiableCondition(int nkeys, ScanKey keys[])
{
	for (int i = 0; i < nkeys; ++i)
	{
		ScanKey		key = keys[i];

		/*
		 * Look for two cases:
		 * - op null
		 * - row(a, b, c) op row(null, e, f)
		 */
		if (((key->sk_strategy != InvalidStrategy &&
			  (key->sk_flags & SK_ROW_MEMBER) == 0) ||
			 (i > 0 && YbIsRowHeader(keys[i - 1]) &&
			  key->sk_flags & SK_ROW_MEMBER)) &&
			YbIsNeverTrueNullCond(key))
		{
			elog(DEBUG1, "skipping a scan due to unsatisfiable condition");
			return true;
		}
	}
	return false;
}

static bool
YbShouldPushdownScanPrimaryKey(YbScanPlan scan_plan, AttrNumber attnum,
							   ScanKey key)
{
	if (YbIsHashCodeSearch(key))
		return true;

	if (YbIsBasicOpSearch(key))
	{
		/* Eq strategy for hash key, eq + ineq for range key. */
		return ybc_should_pushdown_op(scan_plan, attnum, key->sk_strategy);
	}

	if (YbIsSearchNull(key))
	{
		/* Always expect InvalidStrategy for NULL search. */
		Assert(key->sk_strategy == InvalidStrategy);
		return true;
	}

	if (yb_pushdown_is_not_null && YbIsSearchNotNull(key))
	{
		/* Always expect InvalidStrategy for IS NOT NULL search. */
		Assert(key->sk_strategy == InvalidStrategy);
		return true;
	}

	if (YbIsSearchArray(key))
	{
		/*
		 * Only allow equal strategy here (i.e. IN .. or = ANY(..) conditions,
		 * NOT IN will generate <> which is not a supported LSM/BTREE
		 * operator, so it should not get to this point.
		 */
		return key->sk_strategy == BTEqualStrategyNumber;
	}

	if (key->sk_flags & SK_ROW_MEMBER)
	{
		/* We'll recheck if this is a valid row comparison key later. */
		return true;
	}

	/* No other operators are supported. */
	return false;
}

/* int comparator for qsort() */
static int
int_compar_cb(const void *v1, const void *v2)
{
	const int  *k1 = v1;
	const int  *k2 = v2;

	if (*k1 < *k2)
		return -1;

	if (*k1 > *k2)
		return 1;

	return 0;
}

/* Use the scan-descriptor and scan-plan to setup scan key for filtering */
static void
ybcSetupScanKeys(YbScanDesc ybScan, YbScanPlan scan_plan)
{
	/*
	 * Find the scan keys that are the primary key.
	 */
	bool		sk_cols_has_ybctid = false;

	for (int i = 0; i < ybScan->nkeys; i++)
	{
		const AttrNumber attnum = scan_plan->bind_key_attnums[i];

		if (attnum == InvalidAttrNumber)
			break;

		int			idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

		if (attnum == YBTupleIdAttributeNumber)
		{
			sk_cols_has_ybctid = true;
			scan_plan->sk_cols = bms_add_member(scan_plan->sk_cols, idx);
		}
		/*
		 * TODO: Can we have bound keys on non-pkey columns here?
		 *       If not we do not need the is_primary_key below.
		 */
		bool		is_primary_key = bms_is_member(idx, scan_plan->primary_key);

		if (is_primary_key &&
			YbShouldPushdownScanPrimaryKey(scan_plan, attnum, ybScan->keys[i]))
		{
			scan_plan->sk_cols = bms_add_member(scan_plan->sk_cols, idx);
		}
	}

	/*
	 * If hash key is not fully set and ybctid is not set either, we must do a
	 * full-table scan so clear all the scan keys if the hash code was
	 * explicitly specified as a scan key then we also shouldn't be clearing the
	 * scan keys.
	 */
	if (ybScan->hash_code_keys == NIL &&
		!bms_is_subset(scan_plan->hash_key, scan_plan->sk_cols) &&
		!sk_cols_has_ybctid)
	{
		bms_free(scan_plan->sk_cols);
		scan_plan->sk_cols = NULL;
	}
}

/* Return true if typid is one of the Object Identifier Types */
static bool
YbIsOidType(Oid typid)
{
	switch (typid)
	{
		case OIDOID:
		case REGPROCOID:
		case REGPROCEDUREOID:
		case REGOPEROID:
		case REGOPERATOROID:
		case REGCLASSOID:
		case REGTYPEOID:
		case REGROLEOID:
		case REGNAMESPACEOID:
		case REGCONFIGOID:
		case REGDICTIONARYOID:
			return true;
		default:
			return false;
	}
}

static int64
YbDatumGetInt64(Datum value, Oid value_typid)
{
	switch (value_typid)
	{
		case INT2OID:
			return DatumGetInt16(value);
		case INT4OID:
			return DatumGetInt32(value);
		case INT8OID:
			return DatumGetInt64(value);
		default:
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("not an integer type")));
	}
}

static bool
YbIsIntegerInRange(Datum value, Oid value_typid, int min, int max)
{
	int64		val = YbDatumGetInt64(value, value_typid);

	return val >= min && val <= max;
}

/*
 * Return true if a scan key column type is compatible with value type.
 */
static bool
YbIsScanCompatible(Oid column_typid,
				   Oid value_typid,
				   bool is_value_scalar,
				   Datum value)
{
	if (column_typid == value_typid)
		return true;

	switch (column_typid)
	{
		case INT2OID:

			/*
			 * If column c0 has INT2OID type and value type is INT4OID, the
			 * value may overflow INT2OID. For example, where clause condition
			 * "c0 = 65539" would become "c0 = 3" and will unnecessarily fetch
			 * a row with key of 3. This will not affect correctness
			 * because at upper Postgres layer filtering will be subsequently
			 * applied for equality/inequality conditions. For example, "c0 =
			 * 65539" will be applied again to filter out this row.
			 * We prefer to bind scan key c0 to account for the
			 * common case where INT4OID value does not overflow INT2OID,
			 * which happens in some system relation scan queries.
			 *
			 * For this purpose, specifically for when the value is scalar,
			 * we return true when we are sure that
			 * there isn't a data overflow. For instance, if column c0 has
			 * INT2OID and value type is INT4OID, and its an inequality
			 * strategy, we check if the actual value is within the
			 * bounds of INT2OID. If yes, then we return true, otherwise false.
			 */
			return (!is_value_scalar ?
					(value_typid == INT4OID || value_typid == INT8OID) :
					YbIsIntegerInRange(value, value_typid, SHRT_MIN, SHRT_MAX));
		case INT4OID:
			return (!is_value_scalar ?
					(value_typid == INT2OID || value_typid == INT8OID) :
					YbIsIntegerInRange(value, value_typid, INT_MIN, INT_MAX));
		case INT8OID:
			return value_typid == INT2OID || value_typid == INT4OID;

		case TEXTOID:
		case BPCHAROID:
		case VARCHAROID:
			return (value_typid == TEXTOID || value_typid == BPCHAROID ||
					value_typid == VARCHAROID);

		default:
			if (YbIsOidType(column_typid) && YbIsOidType(value_typid))
				return true;
			/* Conservatively return false. */
			return false;
	}
}

/*
 * Determine whether an equality between two types needs further recheck.  For
 * now, this only flags cases where the storage column type is smaller than the
 * value type.
 *
 * TODO(jason): check if any other type combos need checking.  float4 and
 * float8 look suspicious.
 *
 * SELECT a.typname, b.typname
 *   FROM pg_operator
 *   JOIN pg_type a ON oprleft = a.oid
 *   JOIN pg_type b ON oprright = b.oid
 *  WHERE oprname = '=' AND oprleft != oprright;
 *    typname   |   typname
 * -------------+-------------
 *  name        | text
 *  int8        | int2
 *  int8        | int4
 *  int2        | int8
 *  int2        | int4
 *  int4        | int8
 *  int4        | int2
 *  text        | name
 *  xid         | int4
 *  float4      | float8
 *  float8      | float4
 *  date        | timestamp
 *  date        | timestamptz
 *  timestamp   | date
 *  timestamp   | timestamptz
 *  timestamptz | date
 *  timestamptz | timestamp
 * (17 rows)
 */
static bool
YbShouldRecheckEquality(Oid column_typid, Oid value_typid)
{
	switch (column_typid)
	{
		case INT2OID:
			if (value_typid == INT4OID)
				return true;
			yb_switch_fallthrough();
		case INT4OID:
			if (value_typid == INT8OID)
				return true;
			break;
		default:
			break;
	}

	return false;
}

static bool
YbIsValueOutOfRange(Oid col_typid, Oid val_typid, Datum val)
{
	return (YbShouldRecheckEquality(col_typid, val_typid) &&
			!YbIsIntegerInRange(val, val_typid,
								col_typid == INT2OID ? SHRT_MIN : INT_MIN,
								col_typid == INT2OID ? SHRT_MAX : INT_MAX));
}

static bool
YbNeedTupleRangeCheck(Datum value, TupleDesc bind_desc,
					  int key_length, AttrNumber bind_key_attnums[])
{
	Oid			tupType = HeapTupleHeaderGetTypeId(DatumGetHeapTupleHeader(value));
	Oid			tupTypmod = HeapTupleHeaderGetTypMod(DatumGetHeapTupleHeader(value));
	TupleDesc	val_tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);
	bool		need_check = false;

	for (int i = 0; i < key_length; i++)
	{
		Oid			val_type = ybc_get_atttypid(val_tupdesc, i + 1);
		Oid			column_type = ybc_get_atttypid(bind_desc,
												   bind_key_attnums[i]);

		if (YbShouldRecheckEquality(column_type, val_type))
		{
			need_check = true;
			break;
		}
	}
	ReleaseTupleDesc(val_tupdesc);
	return need_check;
}

static bool
YbIsTupleInRange(Datum value, TupleDesc bind_desc,
				 int key_length, AttrNumber bind_key_attnums[])
{
	Oid			tupType = HeapTupleHeaderGetTypeId(DatumGetHeapTupleHeader(value));
	Oid			tupTypmod = HeapTupleHeaderGetTypMod(DatumGetHeapTupleHeader(value));
	TupleDesc	val_tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);

	Datum		datum_values[key_length];
	bool		datum_nulls[key_length];

	HeapTupleData tuple;

	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;
	tuple.t_len = HeapTupleHeaderGetDatumLength(value);
	tuple.t_data = DatumGetHeapTupleHeader(value);
	heap_deform_tuple(&tuple, val_tupdesc,
					  datum_values, datum_nulls);
	bool		is_in_range = true;

	for (int i = 0; i < key_length; i++)
	{
		Datum		val = datum_values[i];
		Oid			val_type = ybc_get_atttypid(val_tupdesc, i + 1);
		Oid			column_type = ybc_get_atttypid(bind_desc,
												   bind_key_attnums[i]);

		if (YbIsValueOutOfRange(column_type, val_type, val))
		{
			is_in_range = false;
			break;
		}
	}
	ReleaseTupleDesc(val_tupdesc);
	return is_in_range;
}

/*
 * We require compatible column type and value type to avoid misinterpreting the value Datum
 * using a column type that can cause wrong scan results. Returns true if the column type
 * and value type are compatible.
 */
static bool
YbCheckScanTypes(YbScanDesc ybScan, YbScanPlan scan_plan, int i)
{
	ScanKey		key = ybScan->keys[i];
	Oid			valtypid = key->sk_subtype;

	/*
	 * TODO(jason): this RECORDOID logic is hacky.  It essentially skips the
	 * whole check.  This should only arise from the (SK_ROW_HEADER |
	 * SK_SEARCHARRAY) case, and in that case, this call should be avoided to
	 * begin with since scan_plan->bind_key_attnums is irrelevant.  Or rather,
	 * bind_key_attnums usage should be completely reworked to not be used in
	 * all cases where SK_ROW_HEADER is involved.
	 */
	Oid			atttypid = (valtypid == RECORDOID ?
							RECORDOID :
							ybc_get_atttypid(scan_plan->bind_desc,
											 scan_plan->bind_key_attnums[i]));

	Assert(OidIsValid(atttypid));

	/*
	 * Example: CREATE TABLE t1(c0 REAL, c1 TEXT, PRIMARY KEY(c0 asc));
	 *          INSERT INTO t1(c0, c1) VALUES(0.4, 'SHOULD BE IN RESULT');
	 *          SELECT ALL t1.c1 FROM t1 WHERE ((0.6)>(t1.c0));
	 * Internally, c0 has float4 type, 0.6 has float8 type. If we bind 0.6 directly with
	 * column c0, float8 0.6 will be misinterpreted as float4. However, casting to float4
	 * may lose precision. Here we simply do not bind a key when there is a type mismatch
	 * by leaving start_valid[idx] and end_valid[idx] as false. For the following cases
	 * we assume that Postgres ensures there is no concern for type mismatch.
	 * (1) value type is not a valid type id
	 * (2) InvalidStrategy (for IS NULL)
	 * (3) value type is a polymorphic pseudotype
	 */
	return (!OidIsValid(valtypid) ||
			key->sk_strategy == InvalidStrategy ||
			YbIsScanCompatible(atttypid, valtypid,
							   !YbIsRowHeader(key) && !YbIsSearchArray(key),
							   key->sk_argument) ||
			IsPolymorphicType(valtypid));
}

static bool
YbBindRowComparisonKeys(YbScanDesc ybScan, YbScanPlan scan_plan,
						int skey_index, bool is_not_null[],
						bool is_for_precheck)
{
	int			last_att_no = YBFirstLowInvalidAttributeNumber;
	Relation	index = ybScan->index;
	int			length_of_key = YbGetLengthOfKey(&ybScan->keys[skey_index]);

	ScanKey		header_key = ybScan->keys[skey_index];

	ScanKey    *subkeys = &ybScan->keys[skey_index + 1];

	/*
	 * We can only push down right now if the primary key columns
	 * are specified in the correct order and the primary key
	 * has no hashed columns. We also need to ensure that
	 * the same comparison operation is done to all subkeys.
	 */
	bool		can_pushdown = true;

	int			strategy = header_key->sk_strategy;
	int			subkey_count = length_of_key - 1;

	for (int j = 0; j < subkey_count; j++)
	{
		ScanKey		key = subkeys[j];

		/* Make sure that the specified keys are in the right order. */
		if (key->sk_attno <= last_att_no)
		{
			can_pushdown = false;
			break;
		}

		/*
			* Make sure that the same comparator is applied to
			* all subkeys.
			*/
		if (strategy != key->sk_strategy)
		{
			can_pushdown = false;
			break;
		}
		last_att_no = key->sk_attno;

		/* Make sure that there are no hash key columns. */
		if (index->rd_indoption[key->sk_attno - 1]
			& INDOPTION_HASH)
		{
			can_pushdown = false;
			break;
		}
	}

	/*
	 * Make sure that the primary key has no hash columns in order
	 * to push down.
	 */

	for (int i = 0; (i < index->rd_index->indnkeyatts) && can_pushdown; i++)
	{
		if (index->rd_indoption[i] & INDOPTION_HASH)
			can_pushdown = false;
	}

	bool		needs_recheck = true;

	if (can_pushdown)
	{

		YbcPgExpr  *col_values = palloc(sizeof(YbcPgExpr) *
										index->rd_index->indnkeyatts);

		/*
		 * Prepare upper/lower bound tuples determined from this
		 * clause for bind. Care must be taken in the case
		 * that primary key columns in the index are ordered
		 * differently from each other. For example, consider
		 * if the underlying index has primary key
		 * (r1 ASC, r2 DESC, r3 ASC) and we are dealing with
		 * a clause like (r1, r2, r3) <= (40, 35, 12).
		 * We cannot simply bind (40, 35, 12) as an upper bound
		 * as that will miss tuples such as (40, 32, 0).
		 * Instead we must push down (40, Inf, 12) in this case
		 * for correctness. (Note that +Inf in this context
		 * is higher in STORAGE order than all other values not
		 * necessarily logical order, similar to the role of
		 * docdb::ValueType::kHighest.
		 */

		/*
		 * Is the first column in ascending order in the index?
		 * This is important because whether or not the RHS of a
		 * (row key) >= (row key values) expression is
		 * considered an upper bound is dependent on the answer
		 * to this question. The RHS of such an expression will
		 * be the scan upper bound if the first column is in
		 * descending order and lower if else. Similar logic
		 * applies to the RHS of (row key) <= (row key values)
		 * expressions.
		 */
		bool		is_direction_asc = !(index->rd_indoption[subkeys[0]->sk_attno - 1] &
										 INDOPTION_DESC);

		bool		gt = (strategy == BTGreaterEqualStrategyNumber ||
						  strategy == BTGreaterStrategyNumber);

		bool		is_inclusive = (strategy != BTGreaterStrategyNumber &&
									strategy != BTLessStrategyNumber);

		bool		is_point_scan = ((subkey_count == index->rd_index->indnatts) &&
									 (strategy == BTEqualStrategyNumber));

		/* Whether or not the RHS values make up a DocDB upper bound */
		bool		is_upper_bound = gt ^ is_direction_asc;
		size_t		subkey_index = 0;

		for (int j = 0; j < index->rd_index->indnkeyatts; j++)
		{
			bool		is_column_specified = (subkey_index < subkey_count &&
											   (subkeys[subkey_index]->sk_attno - 1) == j);

			/*
			 * Is the current column stored in ascending order in the
			 * underlying index?
			 */
			bool		asc = (index->rd_indoption[j] & INDOPTION_DESC) == 0;

			/*
			 * If this column has different directionality than the
			 * first column then we have to adjust the bounds on this
			 * column.
			 */
			if (!is_column_specified ||
				(asc != is_direction_asc && !is_point_scan))
			{
				col_values[j] = NULL;
				needs_recheck = true;

				/*
				 * If this is just for precheck, we can return that recheck
				 * is needed.
				 */
				if (is_for_precheck)
					return true;
			}
			else if (!is_for_precheck)
			{
				ScanKey		current = subkeys[subkey_index];
				AttrNumber	attnum =
					scan_plan->bind_key_attnums[skey_index + 1 + subkey_index];

				col_values[j] = YBCNewConstant(ybScan->handle,
											   ybc_get_atttypid(scan_plan->bind_desc,
																attnum),
											   current->sk_collation,
											   current->sk_argument,
											   false);
			}

			if (is_column_specified)
			{
				int			att_idx = YBAttnumToBmsIndex(((TableScanDesc) ybScan)->rs_rd,
														 subkeys[subkey_index]->sk_attno);

				/* Set the first column in this RC to not null. */
				is_not_null[att_idx] |= subkey_index == 0;

				subkey_index++;
			}
		}

		if (is_for_precheck)
			return needs_recheck;

		if (is_upper_bound || strategy == BTEqualStrategyNumber)
		{
			HandleYBStatus(YBCPgDmlAddRowUpperBound(ybScan->handle,
													index->rd_index->indnkeyatts,
													col_values,
													is_inclusive));
		}

		if (!is_upper_bound || strategy == BTEqualStrategyNumber)
		{
			HandleYBStatus(YBCPgDmlAddRowLowerBound(ybScan->handle,
													index->rd_index->indnkeyatts,
													col_values,
													is_inclusive));
		}
	}

	return needs_recheck;
}

static Datum
YbGetArrayConst(ScanKey *keys)
{
	/*
	 * Get array from keys.  See skey.h and ybExtractScanKeys for layout
	 * details.
	 */
	if (YbIsRowHeader(*keys))
		return (*(++keys))->sk_argument;
	return (*keys)->sk_argument;
}

/*
 * Given an array, cull it by removing unsatisfiable and duplicate elements.
 *
 * Out params:
 * - scalar_null_bound: for scalar (non-row) arrays, whether a null element was
 *   found and NULLs are to be retained
 * - culled_elem_values: palloc'd culled array as a C-array of Datums
 * - culled_num_elems: culled array size
 *
 * Returns false if the array is culled to zero elements and NULL is not bound.
 * Caller is still expected to pfree culled_elem_values in this case.
 */
static bool
YbCullArray(ArrayType *arrayval,
			ScanKey key,
			TupleDesc bind_desc,
			Relation index,
			bool is_row,
			int row_nkeys, AttrNumber *row_attnums,
			Oid scalar_col_typid, Oid scalar_val_typid,
			bool *scalar_null_bound,
			Datum **culled_elem_values,
			int *culled_num_elems)
{
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	Datum	   *elem_values;
	bool	   *elem_nulls;
	int			num_elems;

	get_typlenbyvalalign(ARR_ELEMTYPE(arrayval),
						 &elmlen, &elmbyval, &elmalign);

	deconstruct_array(arrayval,
					  ARR_ELEMTYPE(arrayval),
					  elmlen, elmbyval, elmalign,
					  &elem_values, &elem_nulls, &num_elems);
	*culled_elem_values = elem_values;

	int			num_valid = 0;
	bool		retain_nulls = YbSearchArrayRetainNulls(key);

	/*
	 * Filter out nulls and out-of-range elements since they'll never match.
	 *
	 * Four cases for nulls:
	 * - is_row=t, retain_nulls=t: save the value (which is a row that contains
	 *   null(s))
	 * - is_row=t, retain_nulls=f: ignore (this value cannot match)
	 * - is_row=f, retain_nulls=t: save the fact that a null was encountered
	 *   (as any further nulls encoutered are just duplicates)
	 * - is_row=f, retain_nulls=f: ignore (this value cannot match)
	 */
	if (is_row)
	{
		/*
		 * To speed up the common case, cache whether we need to do any value
		 * out-of-range checks on the elements.  Each value row has the same
		 * types as the first value row, so looking at the first value row is
		 * sufficient.
		 */
		bool row_should_check_range = (num_elems > 0 &&
									   YbNeedTupleRangeCheck(elem_values[0],
															 bind_desc,
															 row_nkeys,
															 row_attnums));

		for (int i = 0; i < num_elems; i++)
		{
			bool		row_has_nulls =
				HeapTupleHeaderHasNulls(DatumGetHeapTupleHeader(elem_values[i]));

			/*
			 * For rows, we use row_has_nulls instead of elem_nulls.
			 */
			Assert(!elem_nulls[i]);

			if (!retain_nulls && row_has_nulls)
				continue;

			if (row_should_check_range)
			{
				/*
				 * is_row has two cases:
				 * - BNL: never sets YB_SK_SEARCHARRAY_RETAIN_NULLS
				 * - INSERT ON CONFLICT batching: never needs tuple in range
				 *   check
				 * Hence, the following assert.
				 */
				Assert(!row_has_nulls);

				if (!YbIsTupleInRange(elem_values[i],
									  bind_desc,
									  row_nkeys,
									  row_attnums))
					continue;
			}

			elem_values[num_valid++] = elem_values[i];
		}
	}
	else
	{
		for (int i = 0; i < num_elems; i++)
		{
			if (elem_nulls[i])
			{
				if (retain_nulls)
					*scalar_null_bound = true;
				continue;
			}

			if (YbIsValueOutOfRange(scalar_col_typid, scalar_val_typid,
									elem_values[i]))
				continue;

			elem_values[num_valid++] = elem_values[i];
		}
	}

	pfree(elem_nulls);

	/*
	 * If there are no non-nulls, and binding to NULL is not required, the scan
	 * qual is unsatisfiable.
	 */
	if (num_valid == 0 && !(!is_row && *scalar_null_bound))
		return false;

	/* Build temporary vars */
	IndexScanDescData tmp_scan_desc;

	memset(&tmp_scan_desc, 0, sizeof(IndexScanDescData));
	tmp_scan_desc.indexRelation = index;

	/*
	 * Sort the non-null elements and eliminate any duplicates.  We must
	 * sort in the same ordering used by the index column, so that the
	 * successive primitive indexscans produce data in index order.
	 */
	*culled_num_elems = _bt_sort_array_elements(&tmp_scan_desc, key,
												false,	/* reverse */
												elem_values, num_valid);

	return true;
}

/*
 * Bind scalar array ops and row array ops.
 *
 * skey_index represents the scan key index we are focusing on.
 *
 * is_for_precheck signifies that the caller only wants to use this for
 * predetermine-recheck purposes, so don't actually do binds but still
 * calculate all_ordinary_keys_bound.
 * TODO(jason): do a proper cleanup.
 */
static void
YbBindSearchArray(YbScanDesc ybScan, YbScanPlan scan_plan,
				  int skey_index, bool is_for_precheck, bool is_column_bound[],
				  bool *bail_out)
{
	/* based on _bt_preprocess_array_keys() */
	ArrayType  *arrayval;
	int			num_elems;
	Datum	   *elem_values;
	ScanKey		key = ybScan->keys[skey_index];
	AttrNumber	scalar_attnum = scan_plan->bind_key_attnums[skey_index];
	Relation	relation = ((TableScanDesc) ybScan)->rs_rd;

	*bail_out = false;

	bool		is_row = false;
	int			row_nkeys;
	AttrNumber *row_attnums;
	Oid			scalar_col_typid;
	Oid			scalar_val_typid;
	bool		scalar_null_bound;

	if (YbIsRowHeader(key))
	{
		Bitmapset  *newly_bound_idxs = NULL;
		int			bound_idx;

		/*
		 * Get num subkeys and their attnums in this rowkey (exclude header).
		 * See skey.h and ybExtractScanKeys for layout details.
		 */
		is_row = true;
		row_nkeys = YbGetLengthOfKey(&ybScan->keys[skey_index]) - 1;
		row_attnums = &scan_plan->bind_key_attnums[skey_index + 1];

		/* If any column is already bound, give up. */
		for (int row_idx = 0; row_idx < row_nkeys; row_idx++)
		{
			bound_idx = YBAttnumToBmsIndex(relation, row_attnums[row_idx]);

			if (is_column_bound[bound_idx])
			{
				ybScan->all_ordinary_keys_bound = false;
				return;
			}

			newly_bound_idxs = bms_add_member(newly_bound_idxs, bound_idx);
		}

		/*
		 * All columns are bindable: mark them as bound before proceeding to
		 * bind them.
		 */
		while ((bound_idx = bms_first_member(newly_bound_idxs)) >= 0)
		{
			is_column_bound[bound_idx] = true;
		}

		bms_free(newly_bound_idxs);
	}
	else
	{
		scalar_col_typid = ybc_get_atttypid(scan_plan->bind_desc,
											scalar_attnum);
		scalar_val_typid = key->sk_subtype;
		scalar_null_bound = false;
		Assert(!is_column_bound[YBAttnumToBmsIndex(relation, scalar_attnum)]);
		is_column_bound[YBAttnumToBmsIndex(relation, scalar_attnum)] = true;
	}

	if (is_for_precheck)
		return;

	arrayval = DatumGetArrayTypeP(YbGetArrayConst(&ybScan->keys[skey_index]));
	Assert(key->sk_subtype == ARR_ELEMTYPE(arrayval));
	if (!YbCullArray(arrayval,
					 key,
					 scan_plan->bind_desc,
					 ybScan->index,
					 is_row,
					 row_nkeys, row_attnums,
					 scalar_col_typid, scalar_val_typid, &scalar_null_bound,
					 &elem_values,
					 &num_elems))
	{
		*bail_out = true;
		pfree(elem_values);
		return;
	}

	if (is_row)
	{
		ybcBindTupleExprCondIn(ybScan, scan_plan->bind_desc,
							   row_nkeys, row_attnums,
							   num_elems, elem_values);
	}
	else if (scalar_attnum == YBTupleIdAttributeNumber)
		YBCPgBindYbctids(ybScan->handle, num_elems, elem_values);
	else
		ybcBindColumnCondIn(ybScan, scan_plan->bind_desc,
							scalar_attnum, num_elems,
							elem_values, scalar_null_bound);

	pfree(elem_values);
}

/*
 * Use the scan-descriptor and scan-plan to setup binds for the queryplan.
 * is_for_precheck signifies that the caller only wants to use this for
 * predetermine-recheck purposes, so don't actually do binds but still
 * calculate all_ordinary_keys_bound.
 * TODO(jason): do a proper cleanup.
 */
static bool
YbBindScanKeys(YbScanDesc ybScan, YbScanPlan scan_plan, Scan *scan,
			   bool is_for_precheck)
{
	Relation	relation = scan_plan->target_relation;

	/*
	 * Best-effort try to determine if all non-yb_hash_code keys are bound.
	 * - GUCs: these are AUTO_PG_FLAGs for rolling-upgrade purposes.  Until
	 *   upgrade is complete, it is possible that a bind that is formed here is
	 *   not properly interpreted by the tserver, so returned rows ought to be
	 *   rechecked (assuming error is not returned).  Since it shouldn't be
	 *   common for these GUCs to be false, don't bother with a more detailed
	 *   inspection (e.g. conditions may all be unrelated to strict inequality
	 *   and all be bound, yet if yb_pushdown_strict_inequality is false, this
	 *   logic lazily thinks everything isn't bound).
	 * - YBCIsSysTablePrefetchingStarted: if this scan is for system table
	 *   prefetching, it is a special case that doesn't push down conditions,
	 *   so assume the worst.
	 */
	ybScan->all_ordinary_keys_bound = (yb_bypass_cond_recheck &&
									   yb_pushdown_strict_inequality &&
									   yb_pushdown_is_not_null &&
									   !YBCIsSysTablePrefetchingStarted());

	/*
	 * Set up the arrays to store the search intervals for each PG/YSQL
	 * attribute (i.e. DocDB column).
	 * The size of the arrays will be based on the max attribute
	 * number used in the query but, as usual, offset to account for the
	 * negative attribute numbers of system attributes.
	 */
	int			max_idx = 0;

	for (int i = 0; i < ybScan->nkeys; i++)
	{
		int			idx = YBAttnumToBmsIndex(relation, scan_plan->bind_key_attnums[i]);

		if (max_idx < idx && bms_is_member(idx, scan_plan->sk_cols))
			max_idx = idx;
	}
	max_idx++;

	/* Find intervals for columns */

	bool		is_column_bound[max_idx];	/* VLA - scratch space */

	memset(is_column_bound, 0, sizeof(bool) * max_idx);

	bool		start_valid[max_idx];	/* VLA - scratch space */

	memset(start_valid, 0, sizeof(bool) * max_idx);

	bool		end_valid[max_idx]; /* VLA - scratch space */

	memset(end_valid, 0, sizeof(bool) * max_idx);

	bool		is_not_null[max_idx];	/* VLA - scratch space */

	memset(is_not_null, 0, sizeof(bool) * max_idx);

	Datum		start[max_idx]; /* VLA - scratch space */
	Datum		end[max_idx];	/* VLA - scratch space */

	bool		start_inclusive[max_idx];	/* VLA - scratch space */

	memset(start_inclusive, 0, sizeof(bool) * max_idx);

	bool		end_inclusive[max_idx]; /* VLA - scratch space */

	memset(end_inclusive, 0, sizeof(bool) * max_idx);

	FmgrInfo   *start_cmp_fn[max_idx];
	FmgrInfo   *end_cmp_fn[max_idx];

	/*
	 * Find an order of relevant keys such that for the same column, an EQUAL
	 * condition is encountered before IN or BETWEEN. is_column_bound is then
	 * used to establish priority order ROW IN > EQUAL > IN > BETWEEN.
	 * IS NOT NULL is treated as a special case of BETWEEN.
	 */
	int			noffsets = 0;
	int			offsets[ybScan->nkeys + 1]; /* VLA - scratch space: +1 to
											 * avoid zero elements */
	int			length_of_key = 0;

	for (int i = 0; i < ybScan->nkeys; i += length_of_key)
	{
		length_of_key = YbGetLengthOfKey(&ybScan->keys[i]);
		ScanKey		key = ybScan->keys[i];

		/* Prioritize binding SAOPs that are pinned by SAOP merge. */
		if (YbIsSearchArray(key))
		{
			Datum		this_array_const;
			ListCell   *lc;
			YbSaopMergeInfo *yb_saop_merge_info = NULL;

			this_array_const = YbGetArrayConst(&ybScan->keys[i]);

			if (scan)
			{
				if (IsA(scan, IndexScan))
					yb_saop_merge_info =
						((IndexScan *) scan)->yb_saop_merge_info;
				else if (IsA(scan, IndexOnlyScan))
					yb_saop_merge_info =
						((IndexOnlyScan *) scan)->yb_saop_merge_info;
			}

			if (yb_saop_merge_info)
			{
				foreach(lc, yb_saop_merge_info->saop_cols)
				{
					ScalarArrayOpExpr *pinned_saop =
						((YbSaopMergeSaopColInfo *) lfirst(lc))->saop;
					Datum		pinned_array_const =
						((Const *) lsecond(pinned_saop->args))->constvalue;

					/*
					 * Direct datum comparison (compared to datumIsEqual) is
					 * safe because yb_match_in_index_clause and
					 * ExecIndexBuildScanKeys set pinned_array_const and
					 * this_array_const, respectively, to the same field in
					 * memory.
					 */
					if (this_array_const == pinned_array_const)
					{
						bool		bail_out = false;

						/* YbBindSearchArray updates is_column_bound. */
						YbBindSearchArray(ybScan, scan_plan, i,
										  is_for_precheck,
										  is_column_bound,
										  &bail_out);

						if (bail_out)
							return false;

						continue;
					}
				}
			}
		}

		/* Check if this is full key row comparison expression */
		if (YbIsRowHeader(key) &&
			!YbIsSearchArray(key))
		{
			bool		needs_recheck = YbBindRowComparisonKeys(ybScan, scan_plan, i,
																is_not_null,
																is_for_precheck);

			ybScan->all_ordinary_keys_bound &= !needs_recheck;
			/*
			 * Full primary-key RowComparison bindings don't interact
			 * or interfere too much with other bindings to the same columns.
			 * They set the upper/lower bounds of the requested scan and also
			 * apply IS NOT NULL filters on the bound LHS columns.
			 */
			continue;
		}

		/* Check if this is primary columns */
		int			bind_key_attnum = scan_plan->bind_key_attnums[i];
		int			idx = YBAttnumToBmsIndex(relation, bind_key_attnum);

		if (!bms_is_member(idx, scan_plan->sk_cols))
		{
			ybScan->all_ordinary_keys_bound = false;
			continue;
		}

		/*
		 * Assign key offsets. Where n is the number of keys, and i is the
		 * clause's index in the list (i < n):
		 *  Clause Type |    Value
		 * -------------+--------------
		 *  ROW IN      | -(n * 2 + i)
		 *  EQUAL       |  -(n + i)
		 *  IN          |     -i
		 *  BETWEEN     |      i
		 *
		 * qsort will place the larger negative values first, and a modulo
		 * operation will return the clause's original index.
		 */
		switch (key->sk_strategy)
		{
			case InvalidStrategy:
				if (YbIsSearchNotNull(key))
				{
					offsets[noffsets++] = i;
					break;
				}
				/* Should be ensured during planning. */
				Assert(YbIsSearchNull(key));
				/* fallthrough  -- treating IS NULL as (DocDB) = (null) */
				yb_switch_fallthrough();
			case BTEqualStrategyNumber:
				if (YbIsBasicOpSearch(key) || YbIsSearchNull(key))
				{
					/*
					 * Use a -ve value so that qsort places EQUAL before
					 * others
					 */
					offsets[noffsets++] = -(ybScan->nkeys + i);
				}
				else if (YbIsSearchArray(key))
				{
					/* Row IN expressions take priority over all. */
					offsets[noffsets++] = (length_of_key > 1 ?
										   -(ybScan->nkeys * 2 + i) :
										   -i);
				}
				break;
			case BTGreaterEqualStrategyNumber:
			case BTGreaterStrategyNumber:
			case BTLessStrategyNumber:
			case BTLessEqualStrategyNumber:
				offsets[noffsets++] = i;
				yb_switch_fallthrough();

			default:
				break;			/* unreachable */
		}
	}

	qsort(offsets, noffsets, sizeof(int), int_compar_cb);
	/* restore -ve offsets to +ve */
	for (int i = 0; i < noffsets; i++)
		if (offsets[i] < 0)
			offsets[i] = (-offsets[i]) % (ybScan->nkeys);
		else
			break;

	/*
	 * Bind keys for EQUALS and IN, collecting info for ranges and IS NOT NULL
	 */
	for (int k = 0; k < noffsets; k++)
	{
		int			i = offsets[k];
		ScanKey		key = ybScan->keys[i];
		int			idx = YBAttnumToBmsIndex(relation, scan_plan->bind_key_attnums[i]);

		/*
		 * YBAttnumToBmsIndex should guarantee that index is positive
		 * -- needed for hash code search below.
		 */
		Assert(idx > 0);

		/* Do not bind more than one condition to a column */
		if (is_column_bound[idx] ||
			!YbCheckScanTypes(ybScan, scan_plan, i))
		{
			ybScan->all_ordinary_keys_bound = false;
			continue;
		}

		bool		bound_inclusive = false;

		switch (key->sk_strategy)
		{
			case InvalidStrategy:
				if (YbIsSearchNotNull(key))
				{
					is_not_null[idx] = true;
					break;
				}

				/*
				 * Otherwise this is an IS NULL search. c IS NULL -> c = NULL
				 * (checked above)
				 */
				yb_switch_fallthrough();
			case BTEqualStrategyNumber:
				/* Bind the scan keys */
				if (YbIsBasicOpSearch(key) || YbIsSearchNull(key))
				{
					/* Either c = NULL or c IS NULL. */
					if (!is_for_precheck)
					{
						bool		is_null = (key->sk_flags & SK_ISNULL) == SK_ISNULL;

						YbBindColumn(ybScan, scan_plan->bind_desc,
									 scan_plan->bind_key_attnums[i],
									 key->sk_argument, is_null);
					}
					is_column_bound[idx] = true;
				}
				else if (YbIsSearchArray(key))
				{
					bool		bail_out = false;

					/* YbBindSearchArray updates is_column_bound. */
					YbBindSearchArray(ybScan, scan_plan, i,
									  is_for_precheck,
									  is_column_bound,
									  &bail_out);

					if (bail_out)
						return false;
				}
				break;

			case BTGreaterEqualStrategyNumber:
				bound_inclusive = true;
				yb_switch_fallthrough();
			case BTGreaterStrategyNumber:
				/*
				 * For prechecks, we skip computation of the range bounds as we
				 * are interested in only knowing if the keys can be bound, and
				 * not what they bind to. Further, in some cases such as nested
				 * subqueries, the value datums may not yet be available during
				 * the precheck.
				 */
				if (is_for_precheck)
					break;

				if (start_valid[idx])
				{
					/* take max of old value and new value */
					bool		is_gt = DatumGetBool(FunctionCall2Coll(&key->sk_func,
																	   key->sk_collation,
																	   start[idx],
																	   key->sk_argument));

					if (!is_gt)
					{
						start[idx] = key->sk_argument;
						start_inclusive[idx] = bound_inclusive;
						start_cmp_fn[idx] = &key->sk_func;
					}
				}
				else
				{
					start[idx] = key->sk_argument;
					start_inclusive[idx] = bound_inclusive;
					start_valid[idx] = true;
					start_cmp_fn[idx] = &key->sk_func;
				}

				if (end_valid[idx])
				{
					bool		is_lt = DatumGetBool(FunctionCall2Coll(end_cmp_fn[idx],
																	   key->sk_collation,
																	   start[idx],
																	   end[idx]));

					if (!is_lt)
						return false;

					bool		is_gt = DatumGetBool(FunctionCall2Coll(start_cmp_fn[idx],
																	   key->sk_collation,
																	   end[idx],
																	   start[idx]));

					if (!is_gt)
						return false;
				}
				break;

			case BTLessEqualStrategyNumber:
				bound_inclusive = true;
				yb_switch_fallthrough();
			case BTLessStrategyNumber:
				/*
				 * For prechecks, we skip computation of the range bounds as we
				 * are interested in only knowing if the keys can be bound, and
				 * not what they bind to. Further, in some cases such as nested
				 * subqueries, the value datums may not yet be available during
				 * the precheck.
				 */
				if (is_for_precheck)
					break;

				if (end_valid[idx])
				{
					/* take min of old value and new value */
					bool		is_lt = DatumGetBool(FunctionCall2Coll(&key->sk_func,
																	   key->sk_collation,
																	   end[idx],
																	   key->sk_argument));

					if (!is_lt)
					{
						end[idx] = key->sk_argument;
						end_inclusive[idx] = bound_inclusive;
						end_cmp_fn[idx] = &key->sk_func;
					}
				}
				else
				{
					end[idx] = key->sk_argument;
					end_inclusive[idx] = bound_inclusive;
					end_valid[idx] = true;
					end_cmp_fn[idx] = &key->sk_func;
				}

				if (start_valid[idx])
				{
					bool		is_gt = DatumGetBool(FunctionCall2Coll(start_cmp_fn[idx],
																	   key->sk_collation,
																	   end[idx],
																	   start[idx]));

					if (!is_gt)
						return false;

					bool		is_lt = DatumGetBool(FunctionCall2Coll(end_cmp_fn[idx],
																	   key->sk_collation,
																	   start[idx],
																	   end[idx]));

					if (!is_lt)
						return false;
				}
				break;

			default:
				break;			/* unreachable */
		}
	}

	/* Bind keys for BETWEEN and IS NOT NULL */
	int			min_idx = bms_first_member(scan_plan->sk_cols);

	min_idx = min_idx < 0 ? 0 : min_idx;
	for (int idx = min_idx; idx < max_idx; idx++)
	{
		/* There's no range key or IS NOT NULL for this query */
		if (!start_valid[idx] && !end_valid[idx] && !is_not_null[idx])
			continue;

		/* Do not bind more than one condition to a column */
		if (is_column_bound[idx])
		{
			ybScan->all_ordinary_keys_bound = false;
			continue;
		}

		if (!is_for_precheck)
		{
			if (start_valid[idx] || end_valid[idx])
			{
				YbBindColumnCondBetween(ybScan, scan_plan->bind_desc,
										YBBmsIndexToAttnum(relation, idx),
										start_valid[idx], start_inclusive[idx],
										start[idx], end_valid[idx],
										end_inclusive[idx], end[idx]);
			}
			else if (yb_pushdown_is_not_null)
			{
				/* is_not_null[idx] must be true */
				YbBindColumnNotNull(ybScan, scan_plan->bind_desc,
									YBBmsIndexToAttnum(relation, idx));
			}
		}
	}
	return true;
}

/*
 * Whether any columns may find mismatch during preliminary check.
 */
static bool
YbMayFailPreliminaryCheck(YbScanDesc ybScan)
{
	if (ybScan->all_ordinary_keys_bound)
		return false;

	ScanKey    *keys = ybScan->keys;

	for (int i = 0; i < ybScan->nkeys; i += YbGetLengthOfKey(&keys[i]))
	{
		if (ybScan->target_key_attnums[i] != InvalidAttrNumber &&
			(!keys[i]->sk_flags ||
			 (keys[i]->sk_flags & (SK_SEARCHNULL | SK_SEARCHNOTNULL))))
			return true;
	}
	return false;
}

/*
 * Before beginning execution, determine whether any kind of recheck is needed:
 * - YB preliminary check
 * - PG recheck
 * Use as little resources as possible to make this determination.  This is
 * largely a dup of ybcBeginScan minus the unessential parts.
 * TODO(jason): there may be room for further cleanup/optimization.
 */
bool
YbPredetermineNeedsRecheck(Scan *scan,
						   Relation relation,
						   Relation index,
						   bool xs_want_itup,
						   ScanKey keys,
						   int nkeys)
{
	YbScanDescData ybscan;

	memset(&ybscan, 0, sizeof(YbScanDescData));
	TableScanDesc tsdesc = (TableScanDesc) &ybscan;

	tsdesc->rs_rd = relation;
	tsdesc->rs_key = keys;
	tsdesc->rs_nkeys = nkeys;

	ybExtractScanKeys(keys, nkeys, &ybscan);

	if (YbIsUnsatisfiableCondition(ybscan.nkeys, ybscan.keys))
		return false;

	ybscan.index = index;

	/* Set up the scan plan */
	YbScanPlanData scan_plan;

	ybcSetupScanPlan(xs_want_itup, &ybscan, &scan_plan);
	ybcSetupScanKeys(&ybscan, &scan_plan);

	YbBindScanKeys(&ybscan, &scan_plan, scan, true /* is_for_precheck */ );

	/*
	 * Finally, ybscan has everything needed to determine recheck.  Do it now.
	 */
	bool		needs_recheck = (YbNeedsPgRecheck(&ybscan) ||
								 YbMayFailPreliminaryCheck(&ybscan));

	bms_free(scan_plan.hash_key);
	bms_free(scan_plan.primary_key);
	bms_free(scan_plan.sk_cols);

	return needs_recheck;
}

typedef struct
{
	YbcPgBoundType type;
	int64_t		value;
} YbBound;

static inline bool
YbBoundEqual(const YbBound *lhs, const YbBound *rhs)
{
	return lhs->type == rhs->type && lhs->value == rhs->value;
}

typedef struct
{
	YbBound		start;
	YbBound		end;
} YbRange;

static inline bool
YbBoundValid(const YbBound *bound)
{
	return bound->type != YB_YQL_BOUND_INVALID;
}

static inline bool
YbBoundInclusive(const YbBound *bound)
{
	return bound->type == YB_YQL_BOUND_VALID_INCLUSIVE;
}

static bool
YbIsValidRange(const YbBound *start, const YbBound *end)
{
	Assert(YbBoundValid(start) && YbBoundValid(end));
	return (start->value < end->value ||
			(start->value == end->value &&
			 YbBoundInclusive(start) &&
			 YbBoundInclusive(end)));
}

static bool
YbApplyStartBound(YbRange *range, const YbBound *start)
{
	Assert(YbIsValidRange(&range->start, &range->end));
	Assert(YbBoundValid(start));

	if (!YbIsValidRange(start, &range->end))
		return false;

	if ((range->start.value < start->value) ||
		(range->start.value == start->value && !YbBoundInclusive(start)))
	{
		range->start = *start;
	}
	return true;
}

static bool
YbApplyEndBound(YbRange *range, const YbBound *end)
{
	Assert(YbIsValidRange(&range->start, &range->end));
	Assert(YbBoundValid(end));

	if (!YbIsValidRange(&range->start, end))
		return false;

	if ((range->end.value > end->value) ||
		(range->end.value == end->value && !YbBoundInclusive(end)))
	{
		range->end = *end;
	}
	return true;
}

static inline uint16_t
YbBoundUint16Value(const YbBound *bound)
{
	Assert(bound->type == YB_YQL_BOUND_INVALID ||
		   (bound->value >= 0 && bound->value <= UINT16_MAX));
	return bound->value;
}

static bool
YbBindHashKeys(YbScanDesc ybScan)
{
	static const YbBound YB_MIN_HASH_BOUND = {
		.type = YB_YQL_BOUND_VALID_INCLUSIVE,
		.value = 0,
	};
	static const YbBound YB_MAX_HASH_BOUND = {
		.type = YB_YQL_BOUND_VALID_INCLUSIVE,
		.value = UINT16_MAX,
	};
	YbRange		range = {
		.start = YB_MIN_HASH_BOUND,
		.end = YB_MAX_HASH_BOUND,
	};
	ListCell   *lc;

	foreach(lc, ybScan->hash_code_keys)
	{
		ScanKey		key = (ScanKey) lfirst(lc);

		Assert(YbIsHashCodeSearch(key));
		YbBound		bound = {
			.type = YB_YQL_BOUND_VALID,
			.value = YbDatumGetInt64(key->sk_argument, key->sk_subtype)
		};

		switch (key->sk_strategy)
		{
			case BTEqualStrategyNumber:
				bound.type = YB_YQL_BOUND_VALID_INCLUSIVE;
				if (!YbApplyStartBound(&range, &bound) ||
					!YbApplyEndBound(&range, &bound))
					return false;
				break;

			case BTGreaterEqualStrategyNumber:
				bound.type = YB_YQL_BOUND_VALID_INCLUSIVE;
				yb_switch_fallthrough();
			case BTGreaterStrategyNumber:
				if (!YbApplyStartBound(&range, &bound))
					return false;
				break;

			case BTLessEqualStrategyNumber:
				bound.type = YB_YQL_BOUND_VALID_INCLUSIVE;
				yb_switch_fallthrough();
			case BTLessStrategyNumber:
				if (!YbApplyEndBound(&range, &bound))
					return false;
				break;

			default:
				break;			/* unreachable */
		}
	}

	if (YbBoundEqual(&range.start, &YB_MIN_HASH_BOUND))
		range.start.type = YB_YQL_BOUND_INVALID;
	if (YbBoundEqual(&range.end, &YB_MAX_HASH_BOUND))
		range.end.type = YB_YQL_BOUND_INVALID;
	if (YbBoundValid(&range.start) || YbBoundValid(&range.end))
		YBCPgDmlBindHashCodes(ybScan->handle, range.start.type,
							  YbBoundUint16Value(&range.start), range.end.type,
							  YbBoundUint16Value(&range.end));

	return true;
}

static inline void
ybcPullVarattnosIntoAttnumBms(List *list, Index varno, YbAttnumBmsState *state)
{
	if (list)
		pull_varattnos_min_attr((Node *) list, varno, &state->bms,
								state->min_attr);
}

/*
 * Adds any columns referenced by the bitmap scan local quals to the
 * required_attrs bitmap.
 *
 * If the local quals will not be used, the caller is responsible for ensuring
 * that they are removed from the YbBitmapTableScan node before calling this.
 */
static void
YbAddBitmapScanRecheckColumns(YbBitmapTableScan *plan, Index target_relid,
							  YbAttnumBmsState *required_attrs)
{
	ybcPullVarattnosIntoAttnumBms(plan->fallback_local_quals, target_relid,
								  required_attrs);
	ybcPullVarattnosIntoAttnumBms(plan->recheck_local_quals, target_relid,
								  required_attrs);
}

static void
ybcAddNonDroppedAttr(const TupleDesc tup_desc,
					 AttrNumber attnum,
					 YbAttnumBmsState *attnums)
{
	if (!TupleDescAttr(tup_desc, attnum - 1)->attisdropped)
		ybcAttnumBmsAdd(attnums, attnum);
}

/*
 * Returns list of target columns required by scan plan.
 */
static YbAttnumBmsState
ybcBuildRequiredAttrs(YbScanDesc yb_scan, YbScanPlan scan_plan,
					  Scan *pg_scan_plan)
{
	const YbcPgPrepareParameters *params = &yb_scan->prepare_params;
	const bool	is_index_only_scan = params->index_only_scan;
	bool		all_attrs_required = !params->fetch_ybctids_only;
	Relation	index = yb_scan->index;

	Assert(!is_index_only_scan || index);

	YbAttnumBmsState result = ybcAttnumBmsConstruct();

	if (params->fetch_ybctids_only)
	{
		Assert(index);
		ybcAttnumBmsAdd(&result,
						index->rd_index->indisprimary ?
						YBTupleIdAttributeNumber :
						YBIdxBaseTupleIdAttributeNumber);
		return result;
	}

	/* Catalog requests do not have a pg_scan_plan and require ybctid */
	if (!pg_scan_plan)
		ybcAttnumBmsAdd(&result, YBTupleIdAttributeNumber);
	else
	{
		Index		target_relid = (is_index_only_scan ?
									INDEX_VAR :
									pg_scan_plan->scanrelid);

		/* Collect target attributes */
		ybcPullVarattnosIntoAttnumBms(pg_scan_plan->plan.targetlist,
									  target_relid, &result);

		/* Collect local table filter attributes */
		ybcPullVarattnosIntoAttnumBms(pg_scan_plan->plan.qual, target_relid,
									  &result);

		/*
		 * Collect local recheck/precheck attributes
		 *
		 * TODO(jason): only do this if recheck/precheck is needed.
		 */
		if (IsA(pg_scan_plan, YbBitmapTableScan))
			YbAddBitmapScanRecheckColumns((YbBitmapTableScan *) pg_scan_plan,
										  target_relid,
										  &result);
		else if (IsA(pg_scan_plan, IndexOnlyScan))
			ybcPullVarattnosIntoAttnumBms(((IndexOnlyScan *) pg_scan_plan)->recheckqual,
										  target_relid,
										  &result);
		else if (IsA(pg_scan_plan, IndexScan))
			ybcPullVarattnosIntoAttnumBms(((IndexScan *) pg_scan_plan)->indexqualorig,
										  target_relid,
										  &result);

		/* TableOidAttrNumber is a virtual column, do not send it */
		if (ybcAttnumBmsDelIfExists(&result, TableOidAttributeNumber))
		{
			/*
			 * TODO(#18870): A HeapTuple is required to store
			 * TableOidAttrNumber. Force its creation by including ybctid.
			 */
			ybcAttnumBmsAdd(&result, YBTupleIdAttributeNumber);
		}

		/*
		 * TODO(#16717): Such placeholder target can be removed once the pg_dml
		 * fetcher can recognize empty rows in a response with no explict
		 * targets.
		 *
		 * TODO(#18870): ybctid can be large. Can we use a smaller placeholder
		 * than this? (e.g. NULL)
		 */
		if (ybcAttnumBmsIsEmpty(&result))
			ybcAttnumBmsAdd(&result, YBTupleIdAttributeNumber);

		/*
		 * Postgres uses InvalidAttrNumber as a marker that all columns are
		 * required. It must not be set as a target.
		 */
		all_attrs_required = ybcAttnumBmsDelIfExists(&result,
													 InvalidAttrNumber);
	}

	if (all_attrs_required)
	{
		TupleDesc	target_desc = yb_scan->target_desc;

		if (is_index_only_scan && index->rd_index->indisprimary)
		{
			/*
			 * Special case: For Primary-Key-ONLY-Scan, we select ONLY the
			 * primary key from the target table instead of the whole target
			 * table.
			 */
			for (int i = 0; i < index->rd_index->indnatts; ++i)
				ybcAddNonDroppedAttr(target_desc,
									 index->rd_index->indkey.values[i],
									 &result);
		}
		else
			for (AttrNumber attnum = 1; attnum <= target_desc->natts; ++attnum)
				ybcAddNonDroppedAttr(target_desc, attnum, &result);
	}

	return result;
}

static void
ybcSetupTargets(YbScanDesc yb_scan, YbScanPlan scan_plan, Scan *pg_scan_plan)
{
	YbAttnumBmsState required_attrs = ybcBuildRequiredAttrs(yb_scan,
															scan_plan,
															pg_scan_plan);

	Assert(!ybcAttnumBmsIsEmpty(&required_attrs));
	int			idx = -1;

	while ((idx = bms_next_member(required_attrs.bms, idx)) >= 0)
	{
		const AttrNumber attnum = ybcAttnumBmsAttnum(&required_attrs, idx);

		Assert(attnum != InvalidAttrNumber);
		if (attnum > 0)
			YbDmlAppendTargetRegular(yb_scan->target_desc, attnum,
									 yb_scan->handle);
		else
			YbDmlAppendTargetSystem(attnum, yb_scan->handle);
	}
	ybcAttnumBmsDestroy(&required_attrs);
}

/*
 * Set aggregate targets into handle.  If index is not null, convert column
 * attribute numbers from table-based numbers to index-based ones.
 */
void
YbDmlAppendTargetsAggregate(List *aggrefs, Scan *outer_plan,
							TupleDesc tupdesc, Relation index,
							bool xs_want_itup, YbcPgStatement handle)
{
	ListCell   *lc;

	/* Set aggregate scan targets. */
	foreach(lc, aggrefs)
	{
		Aggref	   *aggref = lfirst_node(Aggref, lc);
		char	   *func_name = get_func_name(aggref->aggfnoid);
		ListCell   *lc_arg;
		YbcPgExpr	op_handle;
		const YbcPgTypeEntity *type_entity;

		/* Get type entity for the operator from the aggref. */
		type_entity = YbDataTypeFromOidMod(InvalidAttrNumber,
										   aggref->aggtranstype);

		/* Create operator. */
		HandleYBStatus(YBCPgNewOperator(handle, func_name, type_entity,
										aggref->aggcollid, &op_handle));

		/* Handle arguments. */
		if (aggref->aggstar)
		{
			/*
			 * Add dummy argument for COUNT(*) case, turning it into COUNT(0).
			 * We don't use a column reference as we want to count rows
			 * even if all column values are NULL.
			 */
			YbcPgExpr	const_handle;

			HandleYBStatus(YBCPgNewConstant(handle,
											type_entity,
											false /* collate_is_valid_non_c */ ,
											NULL /* collation_sortkey */ ,
											0 /* datum */ ,
											false /* is_null */ ,
											&const_handle));
			HandleYBStatus(YBCPgOperatorAppendArg(op_handle, const_handle));
		}
		else
		{
			/* Add aggregate arguments to operator. */
			foreach(lc_arg, aggref->args)
			{
				TargetEntry *tle = lfirst_node(TargetEntry, lc_arg);

				if (IsA(tle->expr, Const))
				{
					Const	   *const_node = castNode(Const, tle->expr);

					/* Already checked by yb_agg_pushdown_supported */
					Assert(const_node->constisnull || const_node->constbyval);

					YbcPgExpr	const_handle;

					HandleYBStatus(YBCPgNewConstant(handle,
													type_entity,
													false /* collate_is_valid_non_c */ ,
													NULL /* collation_sortkey */ ,
													const_node->constvalue,
													const_node->constisnull,
													&const_handle));
					HandleYBStatus(YBCPgOperatorAppendArg(op_handle,
														  const_handle));
				}
				else if (IsA(tle->expr, Var))
				{
					Var		   *var = castNode(Var, tle->expr);
					int			attno = var->varattno;

					/*
					 * Change column reference in an aggregate to attribute
					 * number. Given limited number of cases we support, we
					 * take a number of assumptions here: the outer plan is a
					 * plain Scan, and the scan's target list contains only
					 * simple Vars.
					 * Support for more generic plan shapes would require
					 * deep rework of Postgres/PgGate interactions.
					 */
					if (outer_plan)
					{
						List	   *tlist = outer_plan->plan.targetlist;

						Assert(var->varno == OUTER_VAR);
						Assert(attno > 0);
						Assert(attno <= list_length(tlist));
						TargetEntry *scan_tle = list_nth_node(TargetEntry, tlist, attno - 1);

						Assert(IsA(scan_tle->expr, Var));
						attno = castNode(Var, scan_tle->expr)->varattno;
					}
					Form_pg_attribute attr = TupleDescAttr(tupdesc, attno - 1);
					YbcPgTypeAttrs type_attrs = {attr->atttypmod};

					YbcPgExpr	arg = YBCNewColumnRef(handle,
													  attno,
													  attr->atttypid,
													  attr->attcollation,
													  &type_attrs);

					HandleYBStatus(YBCPgOperatorAppendArg(op_handle, arg));
				}
				else
				{
					/* Should never happen. */
					ereport(ERROR,
							(errcode(ERRCODE_INTERNAL_ERROR),
							 errmsg("unsupported aggregate function argument type")));
				}
			}
		}

		/* Add aggregate operator as scan target. */
		HandleYBStatus(YBCPgDmlAppendTarget(handle, op_handle,
											false /* is_for_secondary_index */ ));
	}
}

/*
 * YbDmlAppendTargets
 *
 * Add targets to the statement.  The colref list is expected to be made up of
 * YbExprColrefDesc nodes.  Unlike YbDmlAppendTargetRegular, it does not do any
 * dropped-columns checking.
 */
void
YbDmlAppendTargets(List *colrefs, YbcPgStatement handle)
{
	ListCell   *lc;
	YbcPgExpr	expr;
	YbcPgTypeAttrs type_attrs;
	YbExprColrefDesc *colref;

	foreach(lc, colrefs)
	{
		colref = lfirst_node(YbExprColrefDesc, lc);
		type_attrs.typmod = colref->typmod;
		expr = YBCNewColumnRef(handle,
							   colref->attno,
							   colref->typid,
							   colref->collid,
							   &type_attrs);
		HandleYBStatus(YBCPgDmlAppendTarget(handle, expr, false /* is_for_secondary_index */ ));
	}
}

void
YbAppendPrimaryColumnRef(YbcPgStatement dml, YbcPgExpr colref)
{
	HandleYBStatus(YbPgDmlAppendColumnRef(dml, colref,
										  false /* is_for_secondary_index */ ));
}

/*
 * YbDmlAppendColumnRefsImpl
 *
 * Add the list of column references used by pushed down expressions to the
 * statement.
 * The colref list is expected to be the list of YbExprColrefDesc nodes.
 */
static void
YbAppendColumnRefsImpl(YbcPgStatement dml, List *colrefs,
					   bool is_for_secondary_index)
{
	ListCell   *lc;

	foreach(lc, colrefs)
	{
		YbExprColrefDesc *param = lfirst_node(YbExprColrefDesc, lc);
		YbcPgTypeAttrs type_attrs = {param->typmod};

		HandleYBStatus(YbPgDmlAppendColumnRef(dml,
											  YBCNewColumnRef(dml,
															  param->attno,
															  param->typid,
															  param->collid,
															  &type_attrs),
											  is_for_secondary_index));
	}
}

void
YbAppendPrimaryColumnRefs(YbcPgStatement dml, List *colrefs)
{
	YbAppendColumnRefsImpl(dml, colrefs, false /* is_for_secondary_index */ );
}

static void
YbApplyPushdownImpl(YbcPgStatement dml, const YbPushdownExprs *pushdown,
					bool is_for_secondary_index)
{
	if (!pushdown)
		return;

	YbAppendColumnRefsImpl(dml, pushdown->colrefs, is_for_secondary_index);
	const uint32_t serialization_version = yb_major_version_upgrade_compatibility > 0
		? yb_major_version_upgrade_compatibility : YbgGetPgVersion();

	ListCell   *lc;

	foreach(lc, pushdown->quals)
	{
		Expr	   *expr = lfirst(lc);

		HandleYBStatus(YbPgDmlAppendQual(dml, YBCNewEvalExprCall(dml, expr), serialization_version,
										 is_for_secondary_index));
	}
}

void
YbApplyPrimaryPushdown(YbcPgStatement dml, const YbPushdownExprs *pushdown)
{
	YbApplyPushdownImpl(dml, pushdown, false /* is_for_secondary_index */ );
}

void
YbApplySecondaryIndexPushdown(YbcPgStatement dml, const YbPushdownExprs *pushdown)
{
	YbApplyPushdownImpl(dml, pushdown, true /* is_for_secondary_index */ );
}

/*
 * Allows to call ApplySortComparator from the PgGate, which does not know
 * Datum, SortSupport data types.
 */
static inline int
yb_sort_comparator_adapter(uint64_t datum1, bool isnull1,
						   uint64_t datum2, bool isnull2, void *state)
{
	return ApplySortComparator((Datum) datum1, isnull1,
							   (Datum) datum2, isnull2, (SortSupport) state);
}

/*
 * YbAddSortTarget - add specified attribute to the secondary index scan as a target
 *
 * Typically the only target on the secondary index scan is the base table ybctid.
 * However, if the secondary index scan performs merge sort of multiple streams,
 * it also needs to fetch and parse values of the sort keys, hence they are added
 * as the targets.
 */
static void
YbAddSortTarget(YbcPgStatement stmt, TupleDesc tupdesc, AttrNumber attno)
{
	Form_pg_attribute attr = TupleDescAttr(tupdesc, attno - 1);
	YbcPgTypeAttrs type_attrs = {attr->atttypmod};
	Oid			attcollation = YBEncodingCollation(stmt, attno,
												   ybc_get_attcollation(tupdesc, attno));
	YbcPgExpr	colref = YBCNewColumnRef(stmt, attno, attr->atttypid, attcollation, &type_attrs);

	HandleYBStatus(YBCPgDmlAppendTarget(stmt, colref, true /* is_for_secondary_index */ ));
}

/*
 * YbApplyMergeSortKeys - set up planned merge sort in PgGate
 *
 * Apply merge sort info to the scan. merge stream conditions are expected to be applied separately.
 * Merge sort assumes ordered data, so it is applicable to Index and IndexOnly scan with defined
 * scan order.
 */
static void
YbApplyMergeSortKeys(YbScanDesc ybScan, Scan *pg_scan_plan)
{
	YbSortInfo *sort_info = NULL;
	bool		reverse = false;
	bool		yb_add_sort_targets = false;
	int16	   *indkey_values = NULL;

	if (IsA(pg_scan_plan, IndexScan))
	{
		IndexScan  *plan = (IndexScan *) pg_scan_plan;

		if (plan->yb_saop_merge_info)
		{
			sort_info = plan->yb_saop_merge_info->sort_cols;
			Assert(!ScanDirectionIsNoMovement(plan->indexorderdir));
			reverse = ScanDirectionIsBackward(plan->indexorderdir);
			/*
			 * Key columns of a primary or embedded index may have different attribute numbers than
			 * the respective columns of the base table. So we need to provide their positions in
			 * the DocDB tuple. On the other hand, when we make separate request to the secondary
			 * index, we need to set up key column data retrieval, in addition to the ybctid.
			 */
			if (ybScan->index->rd_index->indisprimary || ybScan->prepare_params.embedded_idx)
			{
				indkey_values = ybScan->index->rd_index->indkey.values;
			}
			else
			{
				yb_add_sort_targets = true;
			}
		}
	}
	else if (IsA(pg_scan_plan, IndexOnlyScan))
	{
		IndexOnlyScan *plan = (IndexOnlyScan *) pg_scan_plan;

		if (plan->yb_saop_merge_info)
		{
			sort_info = plan->yb_saop_merge_info->sort_cols;
			Assert(!ScanDirectionIsNoMovement(plan->indexorderdir));
			reverse = ScanDirectionIsBackward(plan->indexorderdir);
		}
	}
	if (!sort_info)
		return;

	/* Create and apply sort keys */
	YbcPgStatement stmt = ybScan->handle;
	YbcSortKey *yb_sort_keys = (YbcSortKey *) palloc(sort_info->numCols * sizeof(YbcSortKey));

	for (int i = 0; i < sort_info->numCols; ++i)
	{
		YbcSortKey *key = &yb_sort_keys[i];

		key->att_idx = sort_info->sortColIdx[i] - 1;
		key->value_idx = indkey_values ? indkey_values[key->att_idx] - 1 : key->att_idx;
		key->comparator = yb_sort_comparator_adapter;
		key->sortstate = palloc0(sizeof(SortSupportData));
		SortSupport sort_support = (SortSupport) key->sortstate;

		sort_support->ssup_cxt = CurrentMemoryContext;
		sort_support->ssup_collation = sort_info->collations[i];
		sort_support->ssup_nulls_first = sort_info->nullsFirst[i];
		sort_support->ssup_reverse = reverse;
		sort_support->abbreviate = false;
		PrepareSortSupportFromOrderingOp(sort_info->sortOperators[i], sort_support);
		if (yb_add_sort_targets)
			YbAddSortTarget(stmt, RelationGetDescr(ybScan->index), sort_info->sortColIdx[i]);
	}
	HandleYBStatus(YBCPgDmlSetMergeSortKeys(stmt, sort_info->numCols, yb_sort_keys));
}

/*
 * Begin a scan for
 *   SELECT <Targets> FROM <relation> USING <index> WHERE <Binds>
 * NOTES:
 * - "relation" is the non-index table.
 * - "index" is the index table, if applicable.
 * - "nkeys" and "key" identify which key columns are provided in the SELECT
 *   WHERE clause.
 *   - nkeys = Number of keys.
 *   - keys[].sk_attno = the column's attribute number with respect to
 *     - "relation" if sequential scan
 *     - "index" if index (only) scan
 *     Easy way to tell between the two cases is whether index is NULL.
 *     Note: ybc_systable_beginscan can call for either case.
 * - If "xs_want_itup" is true, Postgres layer is expecting an IndexTuple that
 *   has ybctid to identify the desired row.
 * - "rel_pushdown" defines expressions to push down to the targeted relation.
 *   - sequential scan: non-index table.
 *   - index scan: non-index table.
 *   - index only scan: index table.
 * - "idx_pushdown" defines expressions to push down to the index in case of an
 *   index scan.
 */
YbScanDesc
ybcBeginScan(Relation relation,
			 Relation index,
			 bool xs_want_itup,
			 int nkeys,
			 ScanKey keys,
			 Scan *pg_scan_plan,
			 YbPushdownExprs *rel_pushdown,
			 YbPushdownExprs *idx_pushdown,
			 List *aggrefs,
			 int distinct_prefixlen,
			 YbcPgExecParameters *exec_params,
			 bool is_internal_scan,
			 bool fetch_ybctids_only)
{
	/* Set up Yugabyte scan description */
	YbScanDesc	ybScan = (YbScanDesc) palloc0(sizeof(YbScanDescData));
	TableScanDesc tsdesc = (TableScanDesc) ybScan;

	tsdesc->rs_rd = relation;
	tsdesc->rs_key = keys;
	tsdesc->rs_nkeys = nkeys;

	/* Flatten keys and store the results in ybScan. */
	ybExtractScanKeys(keys, nkeys, ybScan);

	if (YbIsUnsatisfiableCondition(ybScan->nkeys, ybScan->keys))
	{
		ybScan->quit_scan = true;
		return ybScan;
	}
	ybScan->exec_params = exec_params;
	ybScan->index = index;
	ybScan->quit_scan = false;
	ybScan->prepare_params.fetch_ybctids_only = fetch_ybctids_only;

	/* Set up the scan plan */
	YbScanPlanData scan_plan;

	ybcSetupScanPlan(xs_want_itup, ybScan, &scan_plan);
	ybcSetupScanKeys(ybScan, &scan_plan);

	ybScan->handle = YbNewSelect(relation, &ybScan->prepare_params);

	/* Set up binds */
	if (!YbBindScanKeys(ybScan, &scan_plan, pg_scan_plan,
						false /* is_for_precheck */ ) ||
		!YbBindHashKeys(ybScan))
	{
		ybScan->quit_scan = true;
		bms_free(scan_plan.hash_key);
		bms_free(scan_plan.primary_key);
		bms_free(scan_plan.sk_cols);
		return ybScan;
	}

	/*
	 * Set up targets.  There are two separate cases:
	 * - aggregate pushdown
	 * - not aggregate pushdown
	 * This ought to be reworked once aggregate pushdown supports a mix of
	 * non-aggregate and aggregate targets.
	 */
	if (aggrefs != NIL)
		YbDmlAppendTargetsAggregate(aggrefs, pg_scan_plan, ybScan->target_desc,
									index, xs_want_itup, ybScan->handle);
	else
		ybcSetupTargets(ybScan, &scan_plan, pg_scan_plan);

	YbApplyPrimaryPushdown(ybScan->handle, rel_pushdown);
	YbApplySecondaryIndexPushdown(ybScan->handle, idx_pushdown);

	/*
	 * Set the current syscatalog version (will check that we are up to
	 * date). Avoid it for internal syscatalog requests because that is the way
	 * it has been since the early days of YSQL. For tighter correctness, it
	 * should be sent for syscatalog requests, but this will result in more
	 * cases of catalog version mismatch.
	 * TODO(jason): revisit this for #15080. Condition could instead be
	 * (!IsBootstrapProcessingMode()) which skips initdb and catalog
	 * prefetching.
	 */
	if (!(is_internal_scan && IsSystemRelation(relation)))
		YbSetCatalogCacheVersion(ybScan->handle,
								 YbGetCatalogCacheVersion());

	/* Set distinct prefix length. */
	if (distinct_prefixlen > 0)
		YBCPgSetDistinctPrefixLength(ybScan->handle, distinct_prefixlen);

	if (pg_scan_plan)
		YbApplyMergeSortKeys(ybScan, pg_scan_plan);

	bms_free(scan_plan.hash_key);
	bms_free(scan_plan.primary_key);
	bms_free(scan_plan.sk_cols);
	return ybScan;
}

/*
 * Also known as "preliminary check".
 *
 * Return true if the given tuple does not match the ordinary
 * (non-yb_hash_code) scan keys.  Returning false is not a guarantee for match.
 *
 * Any modifications here may need to be reflected in YbNeedsPgRecheck as well.
 */
static bool
ybIsTupMismatch(HeapTuple tup, YbScanDesc ybScan)
{
	ScanKey    *keys = ybScan->keys;
	AttrNumber *sk_attno = ybScan->target_key_attnums;

	/*
	 * This function tries to find mismatches on ordinary keys.  If ordinary
	 * keys are already known to be pushed down, it is futile to try to find a
	 * mismatch.
	 */
	if (ybScan->all_ordinary_keys_bound)
		return false;

	for (int i = 0; i < ybScan->nkeys; i += YbGetLengthOfKey(&keys[i]))
	{
		if (sk_attno[i] == InvalidAttrNumber)
			continue;

		ScanKey		key = keys[i];
		bool		is_null = false;
		Datum		res_datum = heap_getattr(tup, sk_attno[i], ybScan->target_desc,
											 &is_null);

		if (key->sk_flags & SK_SEARCHNULL)
		{
			if (is_null)
				continue;
			else
				return true;
		}

		if (key->sk_flags & SK_SEARCHNOTNULL)
		{
			if (!is_null)
				continue;
			else
				return true;
		}

		/* TODO: support the different search options like SK_SEARCHARRAY. */
		if (key->sk_flags != 0)
			continue;

		if (is_null)
			return true;

		bool		matches = DatumGetBool(FunctionCall2Coll(&key->sk_func,
															 key->sk_collation,
															 res_datum,
															 key->sk_argument));

		if (!matches)
			return true;
	}

	return false;
}

/*
 * Whether rows returned by DocDB need to be rechecked.  Currently, it can be
 * predetermined for the entire scan before tuples are fetched.  This function
 * is ready to be called after calling YbBindScanKeys, which sets some
 * variables that are read here.
 */
inline bool
YbNeedsPgRecheck(YbScanDesc yb_scan)
{
	/*
	 * Due to historical reasons, yb_hash_code pushdown always requires
	 * recheck.
	 */
	if (yb_scan->hash_code_keys)
		return true;

	/* If all keys are bound, there is no need to recheck. */
	if (yb_scan->all_ordinary_keys_bound)
		return false;

	/*
	 * Precheck takes care of SK_SEARCHNULL and SK_SEARCHNOTNULL cases.  All
	 * other cases need recheck.  Due to historical reasons, index only scan
	 * and expressions (corresponding to InvalidAttrNumber) always require
	 * recheck.
	 */
	const ScanKey *keys = yb_scan->keys;
	const bool	is_index_only_scan = yb_scan->prepare_params.index_only_scan;

	for (int i = 0; i < yb_scan->nkeys; i += YbGetLengthOfKey(&keys[i]))
	{
		const AttrNumber attnum = yb_scan->target_key_attnums[i];

		if (is_index_only_scan ||
			attnum == InvalidAttrNumber ||
			keys[i]->sk_flags & ~(SK_SEARCHNULL | SK_SEARCHNOTNULL))
		{
			return true;
		}
	}
	return false;
}

HeapTuple
ybc_getnext_heaptuple(YbScanDesc ybScan, ScanDirection dir)
{
	HeapTuple	tup = NULL;

	if (ybScan->quit_scan)
		return NULL;

	/* Loop over rows from pggate. */
	while (HeapTupleIsValid(tup = ybcFetchNextHeapTuple(ybScan, dir)))
	{
		/* Do a preliminary check to skip rows we can guarantee don't match. */
		if (ybIsTupMismatch(tup, ybScan))
		{
			YBCPgIncrementIndexRecheckCount();
			heap_freetuple(tup);
			continue;
		}
		break;
	}
	return tup;
}

IndexTuple
ybc_getnext_indextuple(YbScanDesc ybScan, ScanDirection dir)
{
	if (ybScan->quit_scan)
		return NULL;
	return ybcFetchNextIndexTuple(ybScan, dir);
}

bool
ybc_getnext_aggslot(IndexScanDesc scan, YbcPgStatement handle,
					bool index_only_scan)
{
	Assert(scan->yb_agg_slot);

	/*
	 * As of 2023-08-10, the relid passed into ybFetchNext is not going to
	 * be used as it is only used when there are system targets, not
	 * counting the internal ybbasectid lookup to the index.
	 * YbDmlAppendTargetsAggregate only adds that ybbasectid plus operator
	 * targets.
	 * TODO(jason): this may need to be revisited when supporting GROUP BY
	 * aggregate pushdown where system columns are directly targeted.
	 */
	ybFetchNext(handle, scan->yb_agg_slot, InvalidOid /* relid */ );
	/* For IndexScan, hack to make index_getnext think there are tuples. */
	if (!index_only_scan)
		scan->xs_hitup = (HeapTuple) 1;
	return !TTS_EMPTY(scan->yb_agg_slot);
}

void
ybc_free_ybscan(YbScanDesc ybscan)
{
	/*
	 * YB Bitmap Table Scans instantiate the biss_ScanDesc of their children
	 * Bitmap Index Scans, even if it will not be executed. Other nodes
	 * (like Index Scan) have their ScanDesc set when ExecInitIndexScan is
	 * called.
	 *
	 * If the index scan is never executed, then we never even reach this point.
	 *
	 * If the bitmap scan is never executed, it still has a valid biss_ScanDesc,
	 * even though it's YbScanDesc was not set. We need to cleanup after the
	 * biss_ScanDesc but not the YbScanDesc.
	 */
	if (PointerIsValid(ybscan))
	{
		YBCPgDeleteStatement(ybscan->handle);
		pfree(ybscan);
	}
}

static SysScanDesc
YbBuildSysScanDesc(Relation relation, Snapshot snapshot, YbSysScanBase ybscan)
{
	SysScanDesc scan_desc = palloc0(sizeof(SysScanDescData));

	scan_desc->heap_rel = relation;
	scan_desc->snapshot = snapshot;
	scan_desc->ybscan = ybscan;
	return scan_desc;
}

static SysScanDesc
YbBuildOptimizedSysTableScan(Relation relation,
							 Oid indexId,
							 bool indexOK,
							 Snapshot snapshot,
							 int nkeys,
							 ScanKey key)
{
	if (relation->rd_id == InheritsRelationId)
		return YbBuildSysScanDesc(relation,
								  snapshot,
								  yb_pg_inherits_beginscan(relation, key,
														   nkeys, indexId));
	return NULL;
}

SysScanDesc
ybc_systable_beginscan(Relation relation,
					   Oid indexId,
					   bool indexOK,
					   Snapshot snapshot,
					   int nkeys,
					   ScanKey key)
{
	SysScanDesc scan = (IsBootstrapProcessingMode() ?
						NULL :
						YbBuildOptimizedSysTableScan(relation, indexId,
													 indexOK, snapshot, nkeys,
													 key));

	return (scan ?
			scan :
			ybc_systable_begin_default_scan(relation, indexId, indexOK,
											snapshot, nkeys, key));
}

static HeapTuple
ybc_systable_getnext(YbSysScanBase default_scan)
{
	YbDefaultSysScan scan = (void *) default_scan;

	Assert(PointerIsValid(scan->ybscan));

	HeapTuple	tuple = ybc_getnext_heaptuple(scan->ybscan,
											  true);	/* is_forward_scan */

	return tuple;
}

static void
ybc_systable_endscan(YbSysScanBaseData *default_scan)
{
	YbDefaultSysScan scan = (void *) default_scan;

	ybc_free_ybscan(scan->ybscan);
	pfree(scan);
}

static YbSysScanVirtualTable yb_default_scan = {
	.next = &ybc_systable_getnext,
	.end = &ybc_systable_endscan
};

SysScanDesc
ybc_systable_begin_default_scan(Relation relation,
								Oid indexId,
								bool indexOK,
								Snapshot snapshot,
								int nkeys,
								ScanKey key)
{
	Relation	index = NULL;

	/*
	 * Look up the index to scan with if we can. If the index is the primary key which is part
	 * of the table in YugaByte, we should scan the table directly.
	 */
	if (indexOK && !IgnoreSystemIndexes && !ReindexIsProcessingIndex(indexId))
	{
		index = RelationIdGetRelation(indexId);
		if (index->rd_index->indisprimary)
		{
			RelationClose(index);
			index = NULL;
		}

		if (index)
		{
			/*
			 * Change attribute numbers to be index column numbers.
			 * - This conversion is the same as function systable_beginscan() in file "genam.c". If we
			 *   ever reuse Postgres index code, this conversion is a must because the key entries must
			 *   match what Postgres code expects.
			 *
			 * - When selecting using INDEX, the key values are bound to the IndexTable, so index attnum
			 *   must be used for bindings.
			 */
			for (int i = 0; i < nkeys; ++i)
				key[i].sk_attno = YbGetIndexAttnum(index, key[i].sk_attno);
		}
	}

	YbDefaultSysScan scan = palloc0(sizeof(YbDefaultSysScanData));

	scan->ybscan = ybcBeginScan(relation,
								index,
								false /* xs_want_itup */ ,
								nkeys,
								key,
								NULL /* pg_scan_plan */ ,
								NULL /* rel_pushdown */ ,
								NULL /* idx_pushdown */ ,
								NULL /* aggrefs */ ,
								0 /* distinct_prefixlen */ ,
								NULL /* exec_params */ ,
								true /* is_internal_scan */ ,
								false /* fetch_ybctids_only */ );
	Assert(!YbNeedsPgRecheck(scan->ybscan));

	scan->base.vtable = &yb_default_scan;

	if (index)
		RelationClose(index);

	return YbBuildSysScanDesc(relation, snapshot, &scan->base);
}

TableScanDesc
ybc_heap_beginscan(Relation relation,
				   Snapshot snapshot,
				   int nkeys,
				   ScanKey key,
				   uint32 flags)
{
	/*
	 * Restart should not be prevented if operation caused by system read of
	 * system table.
	 */
	Scan	   *pg_scan_plan = NULL;	/* In current context scan plan is not
										 * available */
	YbScanDesc	ybScan = ybcBeginScan(relation,
									  NULL /* index */ ,
									  false /* xs_want_itup */ ,
									  nkeys,
									  key,
									  pg_scan_plan,
									  NULL /* rel_pushdown */ ,
									  NULL /* idx_pushdown */ ,
									  NULL /* aggrefs */ ,
									  0 /* distinct_prefixlen */ ,
									  NULL /* exec_params */ ,
									  true /* is_internal_scan */ ,
									  false /* fetch_ybctids_only */ );
	Assert(!YbNeedsPgRecheck(ybScan));

	/* Set up Postgres sys table scan description */
	TableScanDesc tsdesc = (TableScanDesc) ybScan;

	tsdesc->rs_snapshot = snapshot;
	tsdesc->rs_flags = flags;

	return tsdesc;
}

HeapTuple
ybc_heap_getnext(TableScanDesc tsdesc)
{
	YbScanDesc	ybdesc = (YbScanDesc) tsdesc;
	HeapTuple	tuple;

	Assert(PointerIsValid(tsdesc));
	tuple = ybc_getnext_heaptuple(ybdesc, true /* is_forward_scan */ );

	return tuple;
}

void
ybc_heap_endscan(TableScanDesc tsdesc)
{
	YbScanDesc	ybdesc = (YbScanDesc) tsdesc;

	if (tsdesc->rs_flags & SO_TEMP_SNAPSHOT)
		UnregisterSnapshot(tsdesc->rs_snapshot);
	ybc_free_ybscan(ybdesc);
}

/* --------------------------------------------------------------------------------------------- */

/*
 * ybcGetForeignRelSize
 *		Obtain relation size estimates for a foreign table
 */
void
ybcGetForeignRelSize(PlannerInfo *root,
					 RelOptInfo *baserel,
					 Oid foreigntableid)
{
	if (baserel->tuples < 0)
		baserel->tuples = YBC_DEFAULT_NUM_ROWS;

	/* Set the estimate for the total number of rows (tuples) in this table. */
	if (yb_enable_base_scans_cost_model ||
		yb_enable_optimizer_statistics)
	{
		set_baserel_size_estimates(root, baserel);
	}
	else
	{
		/*
		 * Initialize the estimate for the number of rows returned by this
		 * query.  This does not yet take into account the restriction clauses,
		 * but it will be updated later by ybcIndexCostEstimate once it
		 * inspects the clauses.
		 */
		baserel->rows = baserel->tuples;
	}

	/*
	 * Test any indexes of rel for applicability also.
	 */
	check_index_predicates(root, baserel);
}

void
ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
				bool is_backwards_scan, bool is_seq_scan,
				bool is_uncovered_idx_scan,
				Cost *startup_cost, Cost *total_cost,
				Oid index_tablespace_oid)
{
	if (is_seq_scan && !enable_seqscan)
		*startup_cost = disable_cost;
	else
		*startup_cost = (yb_enable_optimizer_statistics ?
						 yb_network_fetch_cost :
						 0);

	/*
	 * Yugabyte-specific per-tuple cost considerations:
	 *   - 10x the regular CPU cost to account for network/RPC + DocDB overhead.
	 *   - backwards scan scale factor as it will need that many more fetches
	 *     to get all rows/tuples.
	 *   - uncovered index scan is more costly than index-only or seq scan because
	 *     it requires extra request to the main table.
	 */
	double		tsp_cost = 0.0;
	bool		is_valid_tsp_cost = (!is_uncovered_idx_scan &&
									 get_yb_tablespace_cost(index_tablespace_oid,
															&tsp_cost));
	Cost		yb_per_tuple_cost_factor = YB_DEFAULT_PER_TUPLE_COST;

	if (is_valid_tsp_cost && yb_per_tuple_cost_factor > tsp_cost)
		yb_per_tuple_cost_factor = tsp_cost;

	Assert(!is_valid_tsp_cost || tsp_cost != 0);
	if (is_backwards_scan)
		yb_per_tuple_cost_factor *= YBC_BACKWARDS_SCAN_COST_FACTOR;
	if (is_uncovered_idx_scan)
		yb_per_tuple_cost_factor *= YBC_UNCOVERED_INDEX_COST_FACTOR;

	Cost		cost_per_tuple = (cpu_tuple_cost * yb_per_tuple_cost_factor +
								  baserel->baserestrictcost.per_tuple);

	*startup_cost += baserel->baserestrictcost.startup;

	*total_cost = (*startup_cost + cost_per_tuple * baserel->tuples *
				   selectivity);
}

/*
 * Evaluate the selectivity for yb_hash_code qualifiers.
 * Returns 1.0 if there are no yb_hash_code comparison expressions for this
 * index.
 */
static double
ybcEvalHashSelectivity(List *hashed_rinfos)
{
	bool		greatest_set = false;
	int			greatest = 0;

	bool		lowest_set = false;
	int			lowest = USHRT_MAX;
	double		selectivity;
	ListCell   *lc;

	foreach(lc, hashed_rinfos)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
		Expr	   *clause = rinfo->clause;

		Assert(IsA(clause, OpExpr));
		OpExpr	   *op = (OpExpr *) clause;
		Node	   *other_operand = (Node *) lsecond(op->args);

		if (!IsA(other_operand, Const))
			continue;

		int			strategy;
		Oid			lefttype;
		Oid			righttype;

		get_op_opfamily_properties(((OpExpr *) clause)->opno,
								   INTEGER_LSM_FAM_OID,
								   false,
								   &strategy,
								   &lefttype,
								   &righttype);

		int			signed_val = ((Const *) other_operand)->constvalue;

		signed_val = signed_val < 0 ? 0 : signed_val;
		uint32_t	val = signed_val > USHRT_MAX ? USHRT_MAX : signed_val;

		/*
		 * The goal here is to calculate selectivity based on qualifiers.
		 *
		 * 1. yb_hash_code(hash_col) -- Single Key selectivity
		 * 2. yb_hash_code(hash_col) >= ABC and yb_hash_code(hash_col) <= XYZ
		 *    This specifically means that we return all the hash codes between
		 *    ABC and XYZ. YBCEvalHashValueSelectivity takes in ABC and XYZ as
		 *    arguments and finds the number of buckets to search to return what
		 *    is required. If it needs to search 16 buckets out of 48 buckets
		 *    then the selectivity is 0.33 which YBCEvalHashValueSelectivity
		 *    returns.
		 */
		switch (strategy)
		{
			case BTLessStrategyNumber:
				yb_switch_fallthrough();
			case BTLessEqualStrategyNumber:
				greatest_set = true;
				greatest = val > greatest ? val : greatest;
				break;
			case BTGreaterEqualStrategyNumber:
				yb_switch_fallthrough();
			case BTGreaterStrategyNumber:
				lowest_set = true;
				lowest = val < lowest ? val : lowest;
				break;
			case BTEqualStrategyNumber:
				return YBC_SINGLE_KEY_SELECTIVITY;
			default:
				break;
		}

		if (greatest == lowest && greatest_set && lowest_set)
			break;
	}

	if (!greatest_set && !lowest_set)
		return 1.0;

	greatest = greatest_set ? greatest : INT32_MAX;
	lowest = lowest_set ? lowest : INT32_MIN;

	selectivity = YBCEvalHashValueSelectivity(lowest, greatest);
#ifdef SELECTIVITY_DEBUG
	elog(DEBUG4, "yb_hash_code selectivity is %f", selectivity);
#endif
	return selectivity;
}

/*
 * Evaluate the selectivity for some qualified cols given the hash and primary key cols.
 */
static double
ybcIndexEvalClauseSelectivity(double reltuples,
							  Bitmapset *qual_cols,
							  bool is_unique_idx,
							  Bitmapset *hash_key,
							  Bitmapset *primary_key)
{
	/*
	 * If there is no search condition, or not all of the hash columns have
	 * search conditions, it will be a full-table scan.
	 */
	if (bms_is_empty(qual_cols) || !bms_is_subset(hash_key, qual_cols))
		return YBC_FULL_SCAN_SELECTIVITY;

	/*
	 * Otherwise, it will be either a primary key lookup or range scan
	 * on a hash key.
	 */
	if (bms_is_subset(primary_key, qual_cols))
	{
		/* For unique indexes full key guarantees single row. */
		if (is_unique_idx)
			return ((reltuples == 0) ?
					YBC_SINGLE_ROW_SELECTIVITY :
					(double) (1.0 / reltuples));
		else
			return YBC_SINGLE_KEY_SELECTIVITY;
	}

	return YBC_HASH_SCAN_SELECTIVITY;
}

Oid
ybc_get_attcollation(TupleDesc desc, AttrNumber attnum)
{
	return (attnum > 0 ?
			TupleDescAttr(desc, attnum - 1)->attcollation :
			InvalidOid);
}

bool
yb_is_hashed(Expr *clause, IndexOptInfo *index)
{
	bool		is_hashed = false;
	Node	   *leftop;

	if (IsA(clause, OpExpr))
	{
		leftop = get_leftop(clause);
		if (IsA(leftop, FuncExpr))
		{
			is_hashed = (((FuncExpr *) leftop)->funcid == F_YB_HASH_CODE);
			ListCell   *ls;

			if (is_hashed)
			{
				/*
				 * YB: We aren't going to push down a yb_hash_code call
				 * if we matched the call against an expression
				 */
				foreach(ls, index->indexprs)
				{
					Node	   *indexpr = (Node *) lfirst(ls);

					if (indexpr && IsA(indexpr, RelabelType))
						indexpr = (Node *) ((RelabelType *) indexpr)->arg;
					if (equal(indexpr, leftop))
					{
						is_hashed = false;
						break;
					}
				}
			}
		}
	}
	return is_hashed;
}

/*
 * Compute index access portion of IndexScan/IndexOnlyScan node.
 *   - Table row fetch costs are added by cost_index().
 *   - When yb_enable_optimizer_statistics is false, this function also updates
 *     baserel->rows if the current index qual is more selective than the ones
 *     seen so far.  i.e.: the table cardinality is determined by the most
 *     selective index qual regardless of the access path that is eventually
 *     chosen.
 */
void
ybcIndexCostEstimate(struct PlannerInfo *root, IndexPath *path,
					 Selectivity *selectivity, Cost *startup_cost,
					 Cost *total_cost)
{
	IndexOptInfo *indexinfo = path->indexinfo;
	bool		is_primary = false;
	RelOptInfo *baserel = path->path.parent;
	ListCell   *lc;
	bool		is_backwards_scan = path->indexscandir == BackwardScanDirection;
	bool		is_unique = indexinfo->unique;
	bool		is_partial_idx = (indexinfo->indpred != NIL &&
								  indexinfo->predOK);
	Bitmapset  *const_quals = NULL;
	List	   *hashed_rinfos = NIL;
	List	   *clauses = NIL;
	double		baserel_rows_estimate;


	if (!indexinfo->hypothetical)
	{
		/* Hypothetical index cannot be primary index */
		Relation	index = RelationIdGetRelation(indexinfo->indexoid);

		is_primary = index->rd_index->indisprimary;
		RelationClose(index);
	}

	/* Primary-index scans are always covered in Yugabyte (internally) */
	bool		is_uncovered_idx_scan = (!is_primary &&
										 path->path.pathtype != T_IndexOnlyScan);

	YbScanPlanData scan_plan;

	memset(&scan_plan, 0, sizeof(scan_plan));

	if (is_primary || indexinfo->hypothetical)
	{
		RangeTblEntry *rte = planner_rt_fetch(indexinfo->rel->relid, root);

		Assert(rte->rtekind == RTE_RELATION);
		Oid			baserel_oid = rte->relid;

		scan_plan.target_relation = RelationIdGetRelation(baserel_oid);
	}
	else
	{
		scan_plan.target_relation = RelationIdGetRelation(indexinfo->indexoid);
	}

	for (int i = 0; i < indexinfo->nkeycolumns; i++)
	{
		int			bms_idx;

		if (indexinfo->hypothetical)
			bms_idx = YBAttnumToBmsIndexWithMinAttr(YBFirstLowInvalidAttributeNumber,
													i + 1);
		else
		{
			if (is_primary)
				bms_idx = YBAttnumToBmsIndex(scan_plan.target_relation, indexinfo->indexkeys[i]);
			else
				bms_idx = YBAttnumToBmsIndex(scan_plan.target_relation, i + 1);
		}

		if (i < indexinfo->nhashcolumns)
		{
			scan_plan.hash_key = bms_add_member(scan_plan.hash_key, bms_idx);
		}
		scan_plan.primary_key = bms_add_member(scan_plan.primary_key, bms_idx);
	}

	/* Find out the search conditions on the primary key columns */
	foreach(lc, path->indexclauses)
	{
		IndexClause *iclause = lfirst_node(IndexClause, lc);
		int			indexcol = iclause->indexcol;
		ListCell   *lc2;

		foreach(lc2, iclause->indexquals)
		{
			RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc2);
			AttrNumber	attnum = (is_primary ?
								  path->indexinfo->indexkeys[indexcol] :
								  (indexcol + 1));
			Expr	   *clause = rinfo->clause;
			int			bms_idx = YBAttnumToBmsIndex(scan_plan.target_relation, attnum);

			if (IsA(clause, NullTest))
			{
				const_quals = bms_add_member(const_quals, bms_idx);
				ybcAddAttributeColumn(&scan_plan, attnum);
			}
			else
			{
				OpExpr	   *op = (OpExpr *) clause;
				Oid			clause_op = op->opno;
				Node	   *other_operand = (Node *) lsecond(op->args);
				Oid			opfamily = path->indexinfo->opfamily[indexcol];

				/*
				 * If specified, skip boolean index qual to avoid the row count
				 * estimate change, a side effect introduced by the fix for
				 * https://github.com/yugabyte/yugabyte-db/issues/26266
				 * for backward compatibility.  See the function header
				 * comment and around the lines updating baserel->rows, too.
				 */
				if (OidIsValid(clause_op) &&
					(!yb_ignore_bool_cond_for_legacy_estimate ||
					 !IsBooleanOpfamily(opfamily)))
				{
					ybcAddAttributeColumn(&scan_plan, attnum);
					if (other_operand && IsA(other_operand, Const))
						const_quals = bms_add_member(const_quals, bms_idx);
				}
			}

			if (yb_is_hashed(clause, path->indexinfo))
				hashed_rinfos = lappend(hashed_rinfos, rinfo);
			else
				clauses = lappend(clauses, rinfo);
		}
	}
	if (hashed_rinfos != NIL)
	{
		*selectivity = ybcEvalHashSelectivity(hashed_rinfos);
		baserel_rows_estimate = baserel->tuples * (*selectivity);
	}
	else
	{
		if (yb_enable_optimizer_statistics)
		{
			*selectivity = clauselist_selectivity(root /* PlannerInfo */ ,
												  clauses,
												  path->indexinfo->rel->relid /* varrelid */ ,
												  JOIN_INNER,
												  NULL /* SpecialJoinInfo */ );
			baserel_rows_estimate = (baserel->tuples * (*selectivity) >= 1 ?
									 baserel->tuples * (*selectivity) :
									 1);
		}
		else
		{
			*selectivity = ybcIndexEvalClauseSelectivity(baserel->tuples,
														 scan_plan.sk_cols,
														 is_unique,
														 scan_plan.hash_key,
														 scan_plan.primary_key);
			baserel_rows_estimate = baserel->tuples * (*selectivity);
		}
	}

	path->path.rows = baserel_rows_estimate;

	/*
	 * For partial indexes, scale down the rows to account for the predicate.
	 * Do this after setting the baserel rows since this does not apply to base rel.
	 */
	if (!yb_enable_optimizer_statistics && is_partial_idx)
		*selectivity *= YBC_PARTIAL_IDX_PRED_SELECTIVITY;

	ybcCostEstimate(baserel, *selectivity, is_backwards_scan,
					false /* is_seq_scan */ , is_uncovered_idx_scan,
					startup_cost, total_cost,
					path->indexinfo->reltablespace);

	/* SAOP merge index scans should not be possible in non-CBO mode. */
	Assert(!path->yb_index_path_info.saop_merge_saop_cols);

	if (!yb_enable_optimizer_statistics)
	{
		/*
		 * Try to evaluate the number of rows this baserel might return.
		 * We cannot rely on the join conditions here (e.g. t1.c1 = t2.c2) because
		 * they may not be applied if another join path is chosen.
		 * So only use the t1.c1 = <const_value> quals (filtered above) for this.
		 */
		double		const_qual_selectivity = ybcIndexEvalClauseSelectivity(baserel->tuples,
																		   const_quals,
																		   is_unique,
																		   scan_plan.hash_key,
																		   scan_plan.primary_key);

		baserel_rows_estimate = const_qual_selectivity * baserel->tuples;

		if (baserel_rows_estimate < baserel->rows)
			baserel->rows = baserel_rows_estimate;
	}

	RelationClose(scan_plan.target_relation);
}

static bool
YbFetchRowData(YbcPgStatement ybc_stmt, Relation relation, Datum ybctid,
			   Datum *values, bool *nulls, YbcPgSysColumns *syscols)
{
	bool		has_data = false;
	TupleDesc	tupdesc = RelationGetDescr(relation);

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(ybc_stmt,
											 BYTEAOID,
											 InvalidOid,
											 ybctid,
											 false);

	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, YBTupleIdAttributeNumber,
									  ybctid_expr));

	/*
	 * Set up the scan targets. For index-based scan we need to return all "real" columns.
	 */
	for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		if (!TupleDescAttr(tupdesc, attnum - 1)->attisdropped)
			YbDmlAppendTargetRegular(tupdesc, attnum, ybc_stmt);
	}
	YbDmlAppendTargetSystem(YBTupleIdAttributeNumber, ybc_stmt);

	/*
	 * Execute the select statement.
	 * This select statement fetch the row for a specific YBCTID, LIMIT setting is not needed.
	 */
	HandleYBStatus(YBCPgExecSelect(ybc_stmt, NULL /* exec_params */ ));

	/* Fetch one row. */
	HandleYBStatus(YBCPgDmlFetch(ybc_stmt,
								 tupdesc->natts,
								 (uint64_t *) values,
								 nulls,
								 syscols,
								 &has_data));

	return has_data;
}

bool
YbFetchHeapTuple(Relation relation, Datum ybctid, HeapTuple *tuple)
{
	TupleDesc	tupdesc = RelationGetDescr(relation);
	Datum	   *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool	   *nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YbcPgSysColumns syscols;

	/* Read data */
	YbcPgStatement ybc_stmt = YbNewSelect(relation, NULL /* prepare_params */ );

	const bool has_data = YbFetchRowData(ybc_stmt, relation, ybctid, values, nulls, &syscols);

	/* Write into the given tuple */
	if (has_data)
	{
		*tuple = heap_form_tuple(tupdesc, values, nulls);
		(*tuple)->t_tableOid = RelationGetRelid(relation);
		if (syscols.ybctid != NULL)
			HEAPTUPLE_YBCTID(*tuple) = PointerGetDatum(syscols.ybctid);
	}


	/* Free up memory and return data */
	pfree(values);
	pfree(nulls);
	YBCPgDeleteStatement(ybc_stmt);
	return has_data;
}

void
YBCHandleConflictError(Relation rel, LockWaitPolicy wait_policy)
{
	if (wait_policy == LockWaitError)
	{
		/*
		 * In case the user has specified NOWAIT, the intention is to error out
		 * immediately. If we raise ERRCODE_YB_TXN_CONFLICT, the statement might
		 * be retried by our retry logic in yb_attempt_to_restart_on_error().
		 */

		if (rel)
			ereport(ERROR,
					(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
					 errmsg("could not obtain lock on row in relation \"%s\"",
							RelationGetRelationName(rel))));
		else
		{
			/*
			 * It is not expected that relation is null. Raise an error wihout
			 * relation name in release mode.
			 */
			Assert(false);
			ereport(ERROR,
					(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
					 errmsg("could not obtain lock on row")));
		}
	}

	ereport(ERROR,
			(errcode(ERRCODE_YB_TXN_CONFLICT),
			 errmsg("could not serialize access due to concurrent update")));
}

static bool
YBCIsExplicitRowLockConflictStatus(YbcStatus status)
{
	Assert(status);
	const uint32_t err_code = YBCStatusPgsqlError(status);

	return err_code == ERRCODE_YB_TXN_CONFLICT || err_code == ERRCODE_YB_TXN_ABORTED;
}

static void
HandleExplicitRowLockStatus(YbcPgExplicitRowLockStatus status)
{
	if (status.error_info.is_initialized &&
		YBCIsExplicitRowLockConflictStatus(status.ybc_status))
	{
		YBCFreeStatus(status.ybc_status);
		YBCHandleConflictError((OidIsValid(status.error_info.conflicting_table_id) ?
								RelationIdGetRelation(status.error_info.conflicting_table_id) :
								NULL),
							   status.error_info.pg_wait_policy);
	}
	else
	{
		HandleYBStatus(status.ybc_status);
	}
}

/*
 * The return value of this function depends on whether we are batching or not.
 * Currently, batching is enabled if the GUC yb_explicit_row_locking_batch_size > 1
 * and the wait policy is not "SKIP LOCKED".
 * If we are batching, then the return value is just a placeholder, as we are not
 * acquiring the lock on the row before returning.
 * Otherwise, the returned TM_Result is adjusted in case of an error in acquiring the lock.
 */
TM_Result
YBCLockTuple(Relation relation, Datum ybctid, RowMarkType mode,
			 LockWaitPolicy pg_wait_policy, EState *estate)
{
	const YbcPgExplicitRowLockParams lock_params = {
		.rowmark = mode,
		.pg_wait_policy = pg_wait_policy,
		.docdb_wait_policy = YBGetDocDBWaitPolicy(pg_wait_policy)
	};

	const Oid	relfile_oid = YbGetRelfileNodeId(relation);
	const Oid	db_oid = YBCGetDatabaseOid(relation);

	if (yb_explicit_row_locking_batch_size > 1 &&
		lock_params.pg_wait_policy != LockWaitSkip)
	{
		HandleExplicitRowLockStatus(YBCAddExplicitRowLockIntent(relfile_oid,
																ybctid, db_oid,
																&lock_params,
																YbBuildTableLocalityInfo(relation)));
		YBCPgAddIntoForeignKeyReferenceCache(relfile_oid, ybctid);
		return TM_Ok;
	}

	YbcPgStatement ybc_stmt = YbNewSelect(relation, NULL /* prepare_params */ );

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(ybc_stmt, BYTEAOID, InvalidOid, ybctid, false);

	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	YbcPgExecParameters exec_params = {0};

	exec_params.limit_count = 1;
	exec_params.rowmark = lock_params.rowmark;
	exec_params.pg_wait_policy = lock_params.pg_wait_policy;
	exec_params.docdb_wait_policy = lock_params.docdb_wait_policy;
	exec_params.stmt_in_txn_limit_ht_for_reads =
		estate->yb_exec_params.stmt_in_txn_limit_ht_for_reads;

	TM_Result	res = TM_Ok;
	MemoryContext exec_context = CurrentMemoryContext;

	PG_TRY();
	{
		/*
		 * Execute the select statement to lock the tuple with given ybctid.
		 */
		HandleYBStatus(YBCPgExecSelect(ybc_stmt, &exec_params));

		bool		has_data = false;
		Datum	   *values = NULL;
		bool	   *nulls = NULL;
		YbcPgSysColumns syscols;

		/*
		 * Below is done to ensure the read request is flushed to tserver.
		 */
		HandleYBStatus(YBCPgDmlFetch(ybc_stmt, 0, (uint64_t *) values, nulls,
									 &syscols, &has_data));
		YBCPgAddIntoForeignKeyReferenceCache(relfile_oid, ybctid);
	}
	PG_CATCH();
	{
		MemoryContext error_context = MemoryContextSwitchTo(exec_context);
		ErrorData  *edata = CopyErrorData();

		elog(DEBUG2, "Error when trying to lock row. "
			 "pg_wait_policy=%d docdb_wait_policy=%d message=%s",
			 lock_params.pg_wait_policy, lock_params.docdb_wait_policy,
			 edata->message);

		if (edata->sqlerrcode == ERRCODE_YB_TXN_CONFLICT)
			res = TM_Updated;
		else if (edata->sqlerrcode == ERRCODE_YB_TXN_SKIP_LOCKING)
			res = TM_WouldBlock;
		else
		{
			YBCPgDeleteStatement(ybc_stmt);
			MemoryContextSwitchTo(error_context);
			PG_RE_THROW();
		}

		/* Discard the error if not rethrown */
		FlushErrorState();
	}
	PG_END_TRY();

	YBCPgDeleteStatement(ybc_stmt);
	return res;
}

void
YBCFlushTupleLocks()
{
	HandleExplicitRowLockStatus(YBCFlushExplicitRowLockIntents());
}

/*
 * ANALYZE support: take random sample of a YB table data
 */

YbSample
ybBeginSample(Relation rel, int targrows)
{
	ReservoirStateData rstate;
	TupleDesc	tupdesc = RelationGetDescr(rel);
	YbSample	ybSample = (YbSample) palloc0(sizeof(YbSampleData));

	ybSample->relation = rel;
	ybSample->targrows = targrows;
	ybSample->liverows = 0;
	ybSample->deadrows = 0;
	elog(DEBUG1, "Sampling %d rows from table %s",
		 targrows, RelationGetRelationName(rel));

	reservoir_init_selection_state(&rstate, targrows);
	/*
	 * Create new sampler command
	 */
	ybSample->handle = YbNewSample(rel, targrows, rstate.W, rstate.randstate.s0, rstate.randstate.s1);
	for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		if (!TupleDescAttr(tupdesc, attnum - 1)->attisdropped)
			YbDmlAppendTargetRegular(tupdesc, attnum, ybSample->handle);
	}

	ybSample->exec_params.yb_fetch_row_limit = yb_fetch_row_limit;
	ybSample->exec_params.yb_fetch_size_limit = yb_fetch_size_limit;
	ybSample->exec_params.rowmark = -1;

	return ybSample;
}

/*
 * Sequentially scan next block of YB table and select rows for the sample.
 * Block is a sequence of rows from one partition, up to specific number of
 * rows or the end of the partition.
 * Algorithm selects every scanned row until targrows are selected, then it
 * select random rows, with decreasing probability, to replace one of the
 * previously selected rows.
 * The IDs of selected rows are stored in the internal buffer (reservoir).
 * Scan ends and function returns false if one of two is true:
 *  - end of the table is reached
 *  or
 *  - targrows are selected and end of a table partition is reached.
 */
bool
ybSampleNextBlock(YbSample ybSample)
{
	bool		has_more;

	HandleYBStatus(YBCPgSampleNextBlock(ybSample->handle, &has_more));
	return has_more;
}

/*
 * Fetch the rows selected for the sample into pre-allocated buffer.
 * Return number of rows fetched.
 */
int
ybFetchSample(YbSample ybSample, HeapTuple *rows)
{
	Oid			relid = RelationGetRelid(ybSample->relation);
	TupleDesc	tupdesc = RelationGetDescr(ybSample->relation);
	Datum	   *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool	   *nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	int			numrows = 0;
	int			sampledrows;
	bool		has_data = false;

	/*
	 * Retrieve liverows and deadrows counters.
	 * TODO: count deadrows
	 */
	HandleYBStatus(YBCPgGetEstimatedRowCount(ybSample->handle,
											 &sampledrows,
											 &ybSample->liverows,
											 &ybSample->deadrows));
	while (numrows < sampledrows)
	{
		/*
		 * Execute equivalent of
		 *   SELECT * FROM table WHERE ybctid IN [yctid0, ybctid1, ...];
		 */
		if (!has_data)
		{
			HandleYBStatus(YBCPgExecSample(ybSample->handle,
										   &ybSample->exec_params));
		}
		YbcPgSysColumns syscols;
		/* Fetch one row. */
		HandleYBStatus(YBCPgDmlFetch(ybSample->handle,
									 tupdesc->natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data));

		if (has_data)
		{
			/* Make a heap tuple in current memory context */
			rows[numrows] = heap_form_tuple(tupdesc, values, nulls);
			if (syscols.ybctid != NULL)
				HEAPTUPLE_YBCTID(rows[numrows]) = PointerGetDatum(syscols.ybctid);
			rows[numrows]->t_tableOid = relid;
			++numrows;
		}
	}

	if (*YBCGetGFlags()->TEST_delay_after_table_analyze_ms > 0)
	{
		pg_usleep(*YBCGetGFlags()->TEST_delay_after_table_analyze_ms * 1000L);
	}

	pfree(values);
	pfree(nulls);
	/* Close the DocDB statement */
	YBCPgDeleteStatement(ybSample->handle);
	return numrows;
}

/*
 * ybFetchNext
 *
 *  Fetch next row from the provided YbcPgStatement and load it into the slot.
 *
 * The statement must be ready to be fetched from, in other words it should be
 * executed, that means request is sent to the DocDB.
 *
 * Fetched values are copied from the DocDB response and memory for by-reference
 * data types is allocated from the current memory context, so be sure that
 * lifetime of that context is appropriate.
 *
 * slot is expected to be a VirtualTupleTableSlot. Its t_tableOid field is
 * updated with provided relid and ybctid field is set to returned ybctid
 * value.
 */
void
ybFetchNext(YbcPgStatement handle, TupleTableSlot *slot, Oid relid)
{
	Assert(slot != NULL);
	Assert(TTS_IS_VIRTUAL(slot));
	TupleDesc	tupdesc = slot->tts_tupleDescriptor;
	Datum	   *values = slot->tts_values;
	bool	   *nulls = slot->tts_isnull;
	YbcPgSysColumns syscols;
	bool		has_data;

	ExecClearTuple(slot);
	/* Fetch one row. */
	HandleYBStatus(YBCPgDmlFetch(handle,
								 tupdesc->natts,
								 (uint64_t *) values,
								 nulls,
								 &syscols,
								 &has_data));
	if (has_data)
	{
		slot->tts_nvalid = tupdesc->natts;
		slot->tts_flags &= ~TTS_FLAG_EMPTY; /* Not empty */
		TABLETUPLE_YBCTID(slot) = PointerGetDatum(syscols.ybctid);
		slot->tts_tableOid = relid;
	}
}

/***************************************************************************
 * Parallel scan on hash partitioned table
 *
 * Currently not in use, but may be salvaged
 ***************************************************************************/

/*
 * Number of rows per planned parallel range.
 */
int			yb_parallel_range_rows = 0;

/*
 * ybParallelWorkers
 *
 * Estimate how many parallel workers are needed to scan a relation having
 * specified number of rows.
 */
int
ybParallelWorkers(double numrows)
{
	/* yb_parallel_range_rows set to 0 disables parallelizm */
	if (yb_parallel_range_rows <= 0)
		return 0;

	/* Estimate number of parallel workers */
	double		result = ceil(numrows / (double) yb_parallel_range_rows) - 1;

	/*
	 * Cap it at compile time limit for sanity, later on the value will be
	 * further capped accoding to the configuration.
	 */
	return (result > MAX_PARALLEL_WORKER_LIMIT ?
			MAX_PARALLEL_WORKER_LIMIT :
			(int) result);
}

/******************************************************************************
 * Parallel scan on YB tables regardless of partitioning
 *
 * Based on the sequnce of the keys retrieved from the DocDB.
 * The keys are fetched and put into a cyclic buffer by any worker that finds
 * the number of keys in the buffer is too low. If not fetching keys, the
 * workers are continuously scanning ranges between the keys. Worker takes one
 * key from the buffer as the low range bound, and copies the next key as the
 * high range bound. A mutex is used to ensure that no more than one worker is
 * putting or taking rows. The very last key remaining in the buffer can not be
 * taken until the fetch is complete, because it is used as a starting key when
 * keys are requested from DocDB.
 ******************************************************************************/

/*
 * yb_estimate_parallel_size
 *
 * Calculate the size of the shared memory block to exchange information
 * between the workers.
 * TODO(#19467) The variable part of the block is a cyclic buffer to store keys.
 * It is a constant currently, but its size should be estimated before the
 * parallel scan starts. For normal operation it should hold several keys
 * (optimal number is TBD and may depend on the number of workers). However, in
 * the worst case scenario, it is safe to allow keys no longer than 1/3 of
 * the buffer. The buffer must keep the very last key, and have room before or
 * after to add one more key. In the worst case the very last key sits in the
 * middle, and the buffer 3 times bigger than the key ensures that one would
 * fit.
 */
Size
yb_estimate_parallel_size(void)
{
	Size		size = sizeof(YBParallelPartitionKeysData);

	return add_size(size, YB_PARTITION_KEY_DATA_CAPACITY);
}

/*
 * yb_init_partition_key_data
 *
 * Initialize the YBParallelPartitionKeys structure
 */
void
yb_init_partition_key_data(void *data)
{
	YBParallelPartitionKeys ppk = (YBParallelPartitionKeys) data;

	SpinLockInit(&ppk->mutex);
	ConditionVariableInit(&ppk->cv_empty);
	ppk->database_oid = InvalidOid;
	ppk->table_relfilenode_oid = InvalidOid;
	ppk->fetch_status = FETCH_STATUS_IDLE;
	ppk->low_offset = 0;
	ppk->high_offset = 0;
	ppk->key_count = 0;
	ppk->total_key_size = 0.0;
	ppk->total_key_count = 0.0;
	ppk->key_data_size = 0;
	ppk->key_data_capacity = YB_PARTITION_KEY_DATA_CAPACITY;
}

typedef int yb_keylen_t;
#define KEY_LEN(ppk, key_offset) \
	(ppk)->key_data + (key_offset)
#define KEY_DATA(ppk, key_offset) \
	(ppk)->key_data + (key_offset) + sizeof(yb_keylen_t)

/*
 * yb_add_key_unsynchronized
 *
 * Copy next key into the cyclic buffer. Caller must assure exclusive access to
 * the YBParallelPartitionKeys structure.
 * Function checks for the available space in the buffer and returns false if it
 * is insufficient. Otherwise it appends the key into the buffer.
 */
static bool
yb_add_key_unsynchronized(YBParallelPartitionKeys ppk,
						  const char *key, yb_keylen_t key_len)
{
	/* Only the first key is allowed to be empty */
	Assert(key_len > 0 || ppk->key_count == 0);
	/* Special case: initially empty buffer */
	if (ppk->key_count == 0)
	{
		Assert(sizeof(key_len) + key_len <= ppk->key_data_capacity);
		memcpy(KEY_LEN(ppk, 0), &key_len, sizeof(yb_keylen_t));
		/* Update counters, etc */
		if (key_len > 0)
		{
			memcpy(KEY_DATA(ppk, 0), key, key_len);
			ppk->total_key_size += key_len;
			ppk->total_key_count += 1;
		}
		++ppk->key_count;
		ppk->key_data_size += sizeof(key_len) + key_len;
	}
	/* need to check empty space */
	else if (ppk->high_offset < ppk->low_offset)
	{
		/*
		 * Wrapped around buffer, the available space lays between the end of
		 * the high key and the beginning of the low key.
		 */
		yb_keylen_t high_key_len;

		memcpy(&high_key_len, KEY_LEN(ppk, ppk->high_offset), sizeof(yb_keylen_t));
		int			free_offset = ppk->high_offset + sizeof(int) + high_key_len;

		/* Check the room in the buffer */
		Assert(free_offset <= ppk->low_offset);
		if (ppk->low_offset - free_offset < sizeof(yb_keylen_t) + key_len)
			return false;
		memcpy(KEY_LEN(ppk, free_offset), &key_len, sizeof(yb_keylen_t));
		memcpy(KEY_DATA(ppk, free_offset), key, key_len);
		/* Update counters, etc */
		++ppk->key_count;
		ppk->high_offset = free_offset;
		ppk->total_key_size += key_len;
		ppk->total_key_count += 1;
	}
	else						/* The low_offset == high_offset iif key_count
								 * == 1 */
	{
		/*
		 * In not wrapped around buffer we maintain ppk->key_data_size
		 * pointing at the beginning of the free space.
		 */
		int			free_offset = ppk->key_data_size;

		/* Check for the trailing space capacity */
		if (ppk->key_data_capacity - free_offset >= sizeof(key_len) + key_len)
		{
			memcpy(KEY_LEN(ppk, free_offset), &key_len, sizeof(yb_keylen_t));
			memcpy(KEY_DATA(ppk, free_offset), key, key_len);
			/* Update counters, etc */
			++ppk->key_count;
			ppk->high_offset = free_offset;
			ppk->key_data_size += sizeof(key_len) + key_len;
			ppk->total_key_size += key_len;
			ppk->total_key_count += 1;
		}
		/*
		 * The key does not fit into remaining space at the end of the buffer,
		 * but there may be free space at the beginning, so we can wraparoud.
		 */
		else if (ppk->low_offset >= sizeof(key_len) + key_len)
		{
			memcpy(KEY_LEN(ppk, 0), &key_len, sizeof(yb_keylen_t));
			memcpy(KEY_DATA(ppk, 0), key, key_len);
			/* Update counters, etc */
			++ppk->key_count;
			ppk->high_offset = 0;
			ppk->total_key_size += key_len;
			ppk->total_key_count += 1;
		}
		/* No luck, let caller know */
		else
			return false;
	}
	return true;
}

/*
 * yb_remove_key_unsynchronized
 *
 * Remove the lowest key from the buffer. Caller must assure exclusive access to
 * the YBParallelPartitionKeys structure
 */
static void
yb_remove_key_unsynchronized(YBParallelPartitionKeys ppk)
{
	Assert(ppk->key_count > 0);
	yb_keylen_t key_len;

	--ppk->key_count;
	memcpy(&key_len, KEY_LEN(ppk, ppk->low_offset), sizeof(yb_keylen_t));
	/* Find offset of the next element */
	int			next = ppk->low_offset + sizeof(yb_keylen_t) + key_len;

	if (next == ppk->key_data_size)
	{
		/*
		 * The lowest key is actually the last one in the wrapped around cyclic
		 * buffer, so the next one starts from the beginning of the buffer data.
		 */
		next = 0;
		/*
		 * Also we need to update the key_data_size to point to the free space
		 * after the higest key, which will be the last after the removal,  as
		 * the buffer will no longer be wrapped around.
		 * Special case is if the lowest key is the only key in the buffer.
		 * Empty buffer is not suposed to be used, but it would make no harm
		 * to reset.
		 */
		if (ppk->key_count == 0)
		{
			ppk->high_offset = 0;
			ppk->key_data_size = 0;
		}
		else
		{
			memcpy(&key_len, KEY_LEN(ppk, ppk->high_offset), sizeof(yb_keylen_t));
			ppk->key_data_size = ppk->high_offset + sizeof(yb_keylen_t) + key_len;
		}
	}
	ppk->low_offset = next;
}

/*
 * Structure to encapsulate ppk_buffer_fetch_callback's state.
 * When worker fetches next portion of keys the buffer may become full. In such
 * a case rest of the keys have to be discarded to avoid skipping keys.
 * There's no way to let caller know about discarded keys, so callback should
 * remember the fact in its state and ignore the rest of the keys.
 * The ppk_buffer_initialize_callback changes the fetch status back to IDLE for
 * the same purpose because it uses the YBParallelPartitionKeys structure
 * exclusively, but ppk_buffer_fetch_callback may work concurrently with other
 * workers taking keys from the buffer, and at some point other worker may need
 * to fetch more and change the fetch state to WORKING. Hence the separate
 * field, a counter, to be able to report inefficient fetch.
 */
typedef struct YbFetchKeysParam
{
	int			discarded;
	YBParallelPartitionKeys ppk;
} YbFetchKeysParam;

static void
ppk_buffer_fetch_callback(void *param, const char *key, size_t key_size)
{
	YbFetchKeysParam *fkp = (YbFetchKeysParam *) param;
	YBParallelPartitionKeys ppk = fkp->ppk;

	/* Once discarded, discard all the keys, just count them */
	if (fkp->discarded)
	{
		++fkp->discarded;
		return;
	}
	if (key_size)
	{
		bool		added;

		SpinLockAcquire(&ppk->mutex);
		/*
		 * Function is supposed to be called by the worker actively performing
		 * fetch.
		 */
		Assert(ppk->fetch_status == FETCH_STATUS_WORKING);
		added = yb_add_key_unsynchronized(ppk, key, key_size);
		SpinLockRelease(&ppk->mutex);
		/*
		 * If a value has been successfully added, notify other workers that
		 * may be waiting for available key. Key may fail to be added because
		 * the buffer has no room for it. That means the key and all subsequent
		 * messages of the block have to be discarded.
		 * Since this fetch cycle is, in fact, done, allow other workers to
		 * start another fetch, while this worker will be busy for some time
		 * throwing away remaining keys.
		 */
		if (added)
			ConditionVariableSignal(&ppk->cv_empty);
		else
		{
			ppk->fetch_status = FETCH_STATUS_IDLE;
			++fkp->discarded;
		}
	}
	else
	{
		/* The last key from DocDB */
		SpinLockAcquire(&ppk->mutex);
		/* Update fetch status */
		Assert(ppk->fetch_status == FETCH_STATUS_WORKING);
		ppk->fetch_status = FETCH_STATUS_DONE;
		SpinLockRelease(&ppk->mutex);
		/*
		 * The fact that fetch is done makes very last key in the buffer
		 * available, so if there are workers waiting, let them know. One
		 * worker will be able to grab the last working range, other will be
		 * able to tell that their work is done.
		 */
		ConditionVariableBroadcast(&ppk->cv_empty);
	}
}

/*
 * yb_fetch_partition_keys
 *
 * Fetch some keys from the DocDB and put them into the parallel state
 * buffer. Function estimates how many keys to request, but if there are too
 * many keys to fit into the buffer, the remaining keys are discarded.
 */
static void
yb_fetch_partition_keys(YBParallelPartitionKeys ppk)
{
	const char *latest_key;
	size_t		latest_key_size;
	uint64_t	max_num_ranges;
	YbFetchKeysParam fkp = {0, ppk};

	/* Estimate fetch parameter values */
	SpinLockAcquire(&ppk->mutex);
	/* Until fetch is done at least one key must remain in the buffer */
	Assert(ppk->key_count > 0);
	yb_keylen_t key_len;

	memcpy(&key_len, KEY_LEN(ppk, ppk->high_offset), sizeof(yb_keylen_t));
	latest_key_size = key_len;
	/* Empty key indicates the end of the keys, fetch shouldn't be possible. */
	Assert(latest_key_size);
	/*
	 * It is safe to refer the key data in place, since the highest key can not
	 * be removed from the buffer until fetch is completed.
	 */
	latest_key = KEY_DATA(ppk, ppk->high_offset);

	/*
	 * Find average key size so far. We expect reasonable number have already
	 * been received during initialization.
	 */
	double		average_key_size = ppk->total_key_size / ppk->total_key_count;

	/* Account for the key length stored in the buffer */
	average_key_size += sizeof(yb_keylen_t);
	max_num_ranges =
		floor(ppk->key_data_capacity / average_key_size) - ppk->key_count;
	if (max_num_ranges < 16)
		max_num_ranges = 16;
	else if (max_num_ranges > 1024)
		max_num_ranges = 1024;
	SpinLockRelease(&ppk->mutex);

	/*
	 * We don't bother to take the lock to read ppk->key_data_capacity because
	 * it remains constant since its initialization. However, later on we will
	 * calculate fetch sizes and will take the lock, and capture
	 * ppk->key_data_capacity under that lock.
	 */
	HandleYBStatus(YBCGetTableKeyRanges(ppk->database_oid,
										ppk->table_relfilenode_oid,
										ppk->is_forward ? latest_key : NULL /* lower_bound_key */ ,
										ppk->is_forward ? latest_key_size : 0 /* lower_bound_key_size */ ,
										ppk->is_forward ? NULL : latest_key /* upper_bound_key */ ,
										ppk->is_forward ? 0 : latest_key_size /* upper_bound_key_size */ ,
										max_num_ranges, yb_parallel_range_size, ppk->is_forward,
										(ppk->key_data_capacity / 3) - sizeof(yb_keylen_t) /* max_key_length */ ,
										ppk_buffer_fetch_callback, &fkp));
	SpinLockAcquire(&ppk->mutex);
	/* Update fetch status */
	if (ppk->fetch_status == FETCH_STATUS_WORKING)
		ppk->fetch_status = FETCH_STATUS_IDLE;
	else
		Assert(ppk->fetch_status == FETCH_STATUS_DONE || fkp.discarded);
	SpinLockRelease(&ppk->mutex);
	/* Log results for debugging and fine tuning */
	if (fkp.discarded)
		elog(LOG, "Had to discard %d keys out of requested %d. Plan better!",
			 fkp.discarded, (int) max_num_ranges);
	else
		elog(LOG, "Fetch of up to %d keys is completed", (int) max_num_ranges);
	/* All keys are accounted for, log stats */
	if (ppk->fetch_status == FETCH_STATUS_DONE)
		elog(LOG, "Fetch is done, received %.0f keys (%.0f bytes)",
			 ppk->total_key_count, ppk->total_key_size);
}

static void
ppk_buffer_initialize_callback(void *param, const char *key, size_t key_size)
{
	YBParallelPartitionKeys ppk = (YBParallelPartitionKeys) param;

	if (ppk->fetch_status != FETCH_STATUS_WORKING)
	{
		/*
		 * Status changes from WORKING to IDLE when buffer is full, in that
		 * case the remaining keys are ignored.
		 * Status changes to DONE if the key is empty, indicating the end of
		 * the keys, callback must not be called after that.
		 */
		Assert(ppk->fetch_status == FETCH_STATUS_IDLE);
		return;
	}
	if (key_size == 0)
	{
		elog(LOG,
			 "All ranges are fetched at once, received %.0f keys (%.0f bytes)",
			 ppk->total_key_count, ppk->total_key_size);
		ppk->fetch_status = FETCH_STATUS_DONE;
	}
	else if (!yb_add_key_unsynchronized(ppk, key, key_size))
	{
		elog(LOG, "Buffer is full after %d initial keys are loaded, "
			 "discard the rest", ppk->key_count);
		ppk->fetch_status = FETCH_STATUS_IDLE;
	}
}

/*
 * ybParallelPrepare
 *
 * Load initial data into the parallel state structure.
 * When this function is working, no parallel worker is started yet, so
 * the parallel state is owned exclusively, no locking is needed.
 */
void
ybParallelPrepare(YBParallelPartitionKeys ppk, Relation relation,
				  YbcPgExecParameters *exec_params, bool is_forward)
{
	/*
	 * The index scan access method's DSM initialization routines do not
	 * disclose if DSM is initialized for main process or for the background
	 * worker. However, it is still guaranteed that background workers do not
	 * start until main worker DSM initialization is completed.
	 * Hence we always call ybParallelPrepare and use table_relfilenode_oid as
	 * an indicator: if table_relfilenode_oid is valid, it is a background
	 * worker and no initialization is needed.
	 * The table_relfilenode_oid is never changed once initialized,
	 * so spinlock is not required to check it. The rest of the code still
	 * has the YBParallelPartitionKeys structure exclusively.
	 */
	if (OidIsValid(ppk->table_relfilenode_oid))
		return;

	/* We expect freshly initialized parallel state */
	Assert(ppk->fetch_status == FETCH_STATUS_IDLE);
	Assert(ppk->low_offset == 0);
	Assert(ppk->high_offset == 0);
	Assert(ppk->key_count == 0);
	ppk->database_oid = YBCGetDatabaseOid(relation);
	ppk->table_relfilenode_oid = YbGetRelfileNodeId(relation);
	ppk->is_forward = is_forward;
	/*
	 * Put empty key as the first to be taken.
	 * Empty key means lower bound unchanged, so if original request has
	 * lower bound, it will be used.
	 * TODO(#19465) Scan conditions may allow to determine boundaries, and we
	 * have algorithms to do so, however, in practice it happens much later to
	 * be useful here. We need to move this logic.
	 */
	yb_add_key_unsynchronized(ppk, NULL, 0);
	/* Fetch the first set of keys */
	ppk->fetch_status = FETCH_STATUS_WORKING;
	HandleYBStatus(YBCGetTableKeyRanges(ppk->database_oid,
										ppk->table_relfilenode_oid,
										NULL /* lower_bound_key */ , 0 /* lower_bound_key_size */ ,
										NULL /* upper_bound_key */ , 0 /* upper_bound_key_size */ ,
										YB_PARTITION_KEYS_DEFAULT_FETCH_SIZE,
										yb_parallel_range_size, is_forward,
										(ppk->key_data_capacity / 3) - sizeof(yb_keylen_t),
										ppk_buffer_initialize_callback, ppk));
	/* Update fetch status, unless updated by the callback */
	if (ppk->fetch_status == FETCH_STATUS_WORKING)
		ppk->fetch_status = FETCH_STATUS_IDLE;
}

typedef enum YbNextRangeResult
{
	NEXT_RANGE_WAIT,
	NEXT_RANGE_SUCCESS,
	NEXT_RANGE_FETCH,
	NEXT_RANGE_DONE
} YbNextRangeResult;

/*
 * yb_copy_key_unsynchronized
 *
 * Copy the lowest key from the buffer into newly palloc'ed space and return
 * pointer to the space in bound parameter.
 * Return NULL if the key is empty.
 */
static void
yb_copy_key_unsynchronized(YBParallelPartitionKeys ppk,
						   const char **bound,
						   size_t *bound_size)
{
	yb_keylen_t key_len;

	memcpy(&key_len, KEY_LEN(ppk, ppk->low_offset), sizeof(yb_keylen_t));
	*bound_size = key_len;
	if (key_len > 0)
	{
		*bound = (const char *) palloc(key_len);
		memcpy((void *) *bound, KEY_DATA(ppk, ppk->low_offset), key_len);
	}
	else
		*bound = NULL;
}

/*
 * ybParallelNextRange
 *
 * Take another range to work on from the parallel state.
 * If there are too few ybctids in the buffer this function may fetch some
 * first. Function may block if there are no ybctids available.
 * Return values low_bound and high_bound are the boundaries for the range.
 * If they are not NULLs, they are palloc'ed, caller must free them.
 * Function returns true if next range exists and valid bounds are returned.
 * If false is returned it means no more ranges and the worker should stop.
 */
bool
ybParallelNextRange(YBParallelPartitionKeys ppk,
					const char **low_bound,
					size_t *low_bound_size,
					const char **high_bound,
					size_t *high_bound_size)
{
	YbNextRangeResult result = NEXT_RANGE_WAIT;

	while (true)
	{
		SpinLockAcquire(&ppk->mutex);
		/*
		 * Check if we should fetch key
		 * TODO(#19469) create config variable for key count triggering the
		 * fetch or find logic better than the magic number
		 */
		if (ppk->fetch_status == FETCH_STATUS_IDLE && ppk->key_count < 4)
		{
			/*
			 * We will fetch more ranges after mutex is released, for now,
			 * prevent other workers from attempting to fetch.
			 */
			ppk->fetch_status = FETCH_STATUS_WORKING;
			result = NEXT_RANGE_FETCH;
		}
		else
		{
			/*
			 * When performing forward scan, keys in the buffer are in the
			 * ascending order, so first one is going to be the lower bound,
			 * and second, if exists, the higher bound.
			 * When performing backward scan, keys in the buffer are in the
			 * descending order, so destination bouns are opposite.
			 */
			const char **first_key_dest_ptr = ppk->is_forward ? low_bound : high_bound;
			size_t	   *first_key_size_ptr = ppk->is_forward ? low_bound_size : high_bound_size;
			const char **second_key_dest_ptr = ppk->is_forward ? high_bound : low_bound;
			size_t	   *second_key_size_ptr = ppk->is_forward ? high_bound_size : low_bound_size;

			/* Have multiple keys, can take one. */
			if (ppk->key_count > 1)
			{
				yb_copy_key_unsynchronized(ppk, first_key_dest_ptr,
										   first_key_size_ptr);
				yb_remove_key_unsynchronized(ppk);
				yb_copy_key_unsynchronized(ppk, second_key_dest_ptr,
										   second_key_size_ptr);
				result = NEXT_RANGE_SUCCESS;
			}
			/* If the fetch is completed it is OK to take the last key. */
			else if (ppk->fetch_status == FETCH_STATUS_DONE)
			{
				if (ppk->key_count == 1)
				{
					yb_copy_key_unsynchronized(ppk, first_key_dest_ptr,
											   first_key_size_ptr);
					yb_remove_key_unsynchronized(ppk);
					*second_key_dest_ptr = NULL;
					*second_key_size_ptr = 0;
					result = NEXT_RANGE_SUCCESS;
				}
				else
				{
					/* No more data. */
					result = NEXT_RANGE_DONE;
				}
				/* The buffer should be empty now. */
				Assert(ppk->key_count == 0);
			}
			/* Wait otherwise. */
		}
		SpinLockRelease(&ppk->mutex);
		if (result == NEXT_RANGE_SUCCESS || result == NEXT_RANGE_DONE)
			/* All is done. */
			break;
		else if (result == NEXT_RANGE_FETCH)
			/* Fetch more keys and try again. */
			yb_fetch_partition_keys(ppk);
		else					/* result == NEXT_RANGE_WAIT */
		{
			elog(LOG, "ybParallelNextRange: waiting on empty queue");
			ConditionVariableSleep(&ppk->cv_empty,
								   WAIT_EVENT_YB_PARALLEL_SCAN_EMPTY);
		}
	}
	ConditionVariableCancelSleep();
	/*
	 * One value has been taken from the buffer, if there is a worker attempting
	 * to put fetched data it may be able to proceed now.
	 */
	Assert(result == NEXT_RANGE_SUCCESS || result == NEXT_RANGE_DONE);
	return result == NEXT_RANGE_SUCCESS;
}
