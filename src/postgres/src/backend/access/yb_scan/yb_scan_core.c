/*-------------------------------------------------------------------------
 *
 * yb_scan_core.c
 *	  Internal YB scan implementation.
 *	  Contains scan creation, key binding, and fetch logic
 *	  that is private to the AM layer.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/backend/access/yb_scan/yb_scan_core.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/nbtree.h"
#include "access/sysattr.h"
#include "access/yb_cost.h"
#include "access/yb_parallel_scan.h"
#include "access/yb_scan_core.h"
#include "access/yb_table_scan_options.h"
#include "access/yb_target.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/pg_type.h"
#include "lib/qunique.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "optimizer/optimizer.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/typcache.h"

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

/*
 * Checks if an attribute is a hash or key column and note it in the scan plan.
 */
static void
ybcCheckKeyAttribute(YbScanPlan scan_plan,
					 YbcPgTableDesc ybc_table_desc,
					 AttrNumber attnum)
{
	YbcPgColumnInfo column_info;

	HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
											   attnum,
											   &column_info), ybc_table_desc);

	int			idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

	if (column_info.is_hash)
		scan_plan->hash_key_cols = bms_add_member(scan_plan->hash_key_cols, idx);
	if (column_info.is_key)
		scan_plan->key_cols = bms_add_member(scan_plan->key_cols, idx);
}

/*
 * Get Yugabyte-specific table metadata and load it into the scan_plan.
 * Currently only key info.
 */
static void
ybcLoadTableInfo(Relation relation, YbScanPlan scan_plan)
{
	Oid			dboid = YBCGetDatabaseOid(relation);
	YbcPgTableDesc ybc_table_desc = NULL;

	HandleYBStatus(YBCPgGetTableDesc(dboid, YbGetRelfileNodeId(relation),
									 &ybc_table_desc));

	for (AttrNumber attnum = 1; attnum <= relation->rd_att->natts; attnum++)
		ybcCheckKeyAttribute(scan_plan, ybc_table_desc, attnum);
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
YbBindColumn(YbOpaque ybScan, TupleDesc bind_desc,
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
YbBindColumnCondBetween(YbOpaque ybScan,
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
YbBindColumnNotNull(YbOpaque ybScan, TupleDesc bind_desc, AttrNumber attnum)
{
	HandleYBStatus(YBCPgDmlBindColumnCondIsNotNull(ybScan->handle, attnum));
}

/*
 * Bind an array of scan keys for a column.
 */
static void
ybcBindColumnCondIn(YbOpaque ybScan, TupleDesc bind_desc, AttrNumber attnum,
					int nvalues, Datum *values, bool bind_to_null)
{
	Oid			atttypid = ybc_get_atttypid(bind_desc, attnum);
	Oid			attcollation = YBEncodingCollation(ybScan->handle, attnum,
												   ybc_get_attcollation(bind_desc,
																		attnum));

	YbcPgExpr	colref = YBCNewColumnRef(ybScan->handle, attnum, atttypid,
										 attcollation, NULL);

	int			total_num_values = nvalues + (bind_to_null ? 1 : 0);
	Assert(total_num_values > 0);
	YbcPgExpr  *ybc_exprs = palloc(sizeof(YbcPgExpr) * total_num_values);

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
	pfree(ybc_exprs);
}

/*
 * Bind an array of scan keys for a tuple of columns.
 */
static void
ybcBindTupleExprCondIn(YbOpaque ybScan,
					   TupleDesc bind_desc,
					   int n_attnum_values,
					   AttrNumber *attnum,
					   int nvalues,
					   Datum *values)
{
	Assert(nvalues > 0);
	YbcPgExpr  *ybc_rhs_exprs = palloc(sizeof(YbcPgExpr) * nvalues);
	YbcPgExpr	ybc_elems_exprs[n_attnum_values];	/* VLA - scratch space */
	Oid			tupType =
		HeapTupleHeaderGetTypeId(DatumGetHeapTupleHeader(values[0]));
	Oid			tupTypmod =
		HeapTupleHeaderGetTypMod(DatumGetHeapTupleHeader(values[0]));
	YbcPgTypeAttrs type_attrs = {tupTypmod};
	TupleDesc	tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);
	Datum		datum_values[tupdesc->natts];
	bool		is_null[tupdesc->natts];

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
	pfree(ybc_rhs_exprs);

	ReleaseTupleDesc(tupdesc);
}

static void
ybcUpdateFKCache(YbOpaque ybScan, Datum ybctid)
{
	if (!ybScan->exec_params)
		return;

	switch (ybScan->exec_params->rowmark)
	{
		case ROW_MARK_EXCLUSIVE:
		case ROW_MARK_NOKEYEXCLUSIVE:
		case ROW_MARK_SHARE:
		case ROW_MARK_KEYSHARE:
			YBCPgAddIntoForeignKeyReferenceCache(YbGetRelfileNodeId(ybScan->table),
												 ybctid);
			break;
		case ROW_MARK_REFERENCE:
		case ROW_MARK_COPY:
			break;
	}
}

/*
 * Obtain the next parallel range from pscan and apply it to handle.
 * Also sets exec_params for unlimited fetch (parallel ranges are
 * already bounded).
 *
 * Returns true if a range was applied; false means no more ranges
 * and the worker should stop scanning.
 */
bool
yb_scan_apply_next_parallel_range(YbcPgStatement handle,
								  YbcPgExecParameters *exec_params,
								  struct YBParallelPartitionKeysData *pscan)
{
	const char *low_bound;
	size_t		low_bound_size;
	const char *high_bound;
	size_t		high_bound_size;

	if (!ybParallelNextRange(pscan,
							 &low_bound, &low_bound_size,
							 &high_bound, &high_bound_size))
		return false;

	HandleYBStatus(YBCPgDmlApplyParallelRange(handle,
											  low_bound,
											  low_bound_size,
											  high_bound,
											  high_bound_size));
	if (low_bound)
		pfree((void *) low_bound);
	if (high_bound)
		pfree((void *) high_bound);

	/*
	 * Use unlimited fetch.
	 * Parallel scan range is already of limited size, it is
	 * unlikely to exceed the message size, but may save some RPCs.
	 */
	exec_params->limit_use_default = true;
	exec_params->yb_fetch_row_limit = 0;
	exec_params->yb_fetch_size_limit = 0;
	return true;
}

static HeapTuple
ybcFetchNextHeapTuple(YbOpaque ybScan, ScanDirection dir)
{
	HeapTuple	tuple = NULL;
	bool		has_data = false;
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
			/*
			 * The caller may run this fetch under a short-lived (e.g.
			 * per-tuple) memory context.  Request setup happens once per
			 * scan (or parallel range), not once per tuple, so run it in
			 * the scan's own context.
			 */
			MemoryContext oldcxt =
				MemoryContextSwitchTo(GetMemoryChunkContext(ybScan));

			if (ybScan->pscan != NULL &&
				!yb_scan_apply_next_parallel_range(ybScan->handle,
												   ybScan->exec_params,
												   ybScan->pscan))
			{
				MemoryContextSwitchTo(oldcxt);
				return NULL;
			}

			/* Set scan direction, if matters */
			if (ScanDirectionIsForward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, true));
			else if (ScanDirectionIsBackward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, false));

			HandleYBStatus(YBCPgExecSelect(ybScan->handle,
										   ybScan->exec_params));
			ybScan->is_exec_done = true;
			MemoryContextSwitchTo(oldcxt);
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
									RelationGetRelationName(ybScan->table))));
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
			tuple->t_tableOid = RelationGetRelid(ybScan->table);
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
ybcFetchNextIndexTuple(YbOpaque ybScan, ScanDirection dir)
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
			/*
			 * The caller may run this fetch under a short-lived (e.g.
			 * per-tuple) memory context.  Request setup happens once per
			 * scan (or parallel range), not once per tuple, so run it in
			 * the scan's own context.
			 */
			MemoryContext oldcxt =
				MemoryContextSwitchTo(GetMemoryChunkContext(ybScan));

			if (ybScan->pscan != NULL &&
				!yb_scan_apply_next_parallel_range(ybScan->handle,
												   ybScan->exec_params,
												   ybScan->pscan))
			{
				MemoryContextSwitchTo(oldcxt);
				return NULL;
			}

			/* Set scan direction, if matters */
			if (ScanDirectionIsForward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, true));
			else if (ScanDirectionIsBackward(dir))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, false));

			HandleYBStatus(YBCPgExecSelect(ybScan->handle,
										   ybScan->exec_params));
			ybScan->is_exec_done = true;
			MemoryContextSwitchTo(oldcxt);
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
ybcSetupScanPlan(bool xs_want_itup, YbOpaque ybScan, YbScanPlan scan_plan)
{
	Relation	relation = ybScan->table;
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

		/*
		 * yb_hash_code scan keys refer to DocDB's virtual hash-code column,
		 * not a real index attribute.  Top-level yb_hash_code keys are kept
		 * in ybScan->hash_code_keys; ROW-comparison subkeys stay in
		 * ybScan->keys and need this mapping skipped.  A ROW header also has
		 * InvalidAttrNumber when its first subkey is yb_hash_code.
		 */
		if (key->sk_flags & YB_SK_SEARCHHASHCODE ||
			((key->sk_flags & SK_ROW_HEADER) &&
			 key->sk_attno == InvalidAttrNumber))
		{
			ybScan->target_key_attnums[i] = InvalidAttrNumber;
			scan_plan->bind_key_attnums[i] = InvalidAttrNumber;
		}
		else if (key->sk_attno == InvalidAttrNumber)
		{
			elog(ERROR, "unexpected scan key without attribute number");
		}
		else if (!index)
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
			return bms_is_member(idx, scan_plan->key_cols);

		case BTLessStrategyNumber:
		case BTLessEqualStrategyNumber:
		case BTGreaterEqualStrategyNumber:
		case BTGreaterStrategyNumber:
			/* range key */
			return (!bms_is_member(idx, scan_plan->hash_key_cols) &&
					bms_is_member(idx, scan_plan->key_cols));

		default:
			/* TODO: support other logical operators */
			return false;
	}
}

static bool
YbIsHashCodeSearch(ScanKey key)
{
	return (key->sk_flags & YB_SK_SEARCHHASHCODE) != 0;
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
ybAddOrdinaryScanKey(ScanKey key, YbOpaque ybScan)
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
ybExtractScanKeys(ScanKey keys, int nkeys, YbOpaque ybScan)
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
YbShouldPushdownScanKey(YbScanPlan scan_plan, AttrNumber attnum,
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

	if (YbIsSearchNotNull(key))
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
ybcSetupScanKeys(YbOpaque ybScan, YbScanPlan scan_plan)
{
	bool		qualified_scan_key_cols_has_ybctid = false;

	for (int i = 0; i < ybScan->nkeys; i++)
	{
		const AttrNumber attnum = scan_plan->bind_key_attnums[i];

		if (attnum == InvalidAttrNumber)
			continue;

		int			idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

		if (attnum == YBTupleIdAttributeNumber)
		{
			qualified_scan_key_cols_has_ybctid = true;
			scan_plan->qualified_scan_key_cols =
				bms_add_member(scan_plan->qualified_scan_key_cols, idx);
		}

		/* SeqScan may give scan keys that are not key columns. */
		bool		is_key_column = bms_is_member(idx, scan_plan->key_cols);

		if (is_key_column &&
			YbShouldPushdownScanKey(scan_plan, attnum, ybScan->keys[i]))
		{
			scan_plan->qualified_scan_key_cols =
				bms_add_member(scan_plan->qualified_scan_key_cols, idx);
		}
	}

	/*
	 * TODO(#30756): currently, conditions on hash keys are all or nothing:
	 * either all hash keys have a condition bound or none of them.
	 */
	if (!bms_is_subset(scan_plan->hash_key_cols,
					   scan_plan->qualified_scan_key_cols))
	{
		/* TODO(#11881): delete only hash key cols in all cases. */
		if (ybScan->hash_code_keys != NIL ||
			qualified_scan_key_cols_has_ybctid)
		{
			scan_plan->qualified_scan_key_cols =
				bms_del_members(scan_plan->qualified_scan_key_cols,
								scan_plan->hash_key_cols);
		}
		else
		{
			bms_free(scan_plan->qualified_scan_key_cols);
			scan_plan->qualified_scan_key_cols = NULL;
		}
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
	Datum		datum_values[val_tupdesc->natts];
	bool		datum_nulls[val_tupdesc->natts];

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
YbCheckScanTypes(YbOpaque ybScan, YbScanPlan scan_plan, int i)
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
	 * by leaving start_key[idx] and end_key[idx] as NULL. For the following cases
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

/*
 * Per-column fold state used by ybBindOrdinaryScanKeys to merge all
 * same-column conditions (equality, inequality, SAOP, IS (NOT) NULL)
 * into a single final type for that column.
 *
 * Transitions occur as conditions are folded in: for example, encountering
 * an equality when the state is YB_FOLD_SAOP promotes to YB_FOLD_EQUALITY
 * (after verifying the equality value is in the array).  Contradictions
 * (e.g. equality outside an inequality range) cause early return.
 *
 * Which fields are applied depends on the final resolved type:
 *
 *   YB_FOLD_NONE         - none
 *   YB_FOLD_EQUALITY     - eq_value
 *   YB_FOLD_IS_NULL      - none
 *   YB_FOLD_IS_NOT_NULL  - none
 *   YB_FOLD_RANGE        - start_key / end_key
 *   YB_FOLD_SAOP         - saop_base_idx, saop_extra_idxs, saop_count, and
 *                          (optionally) start_key / end_key when inequality
 *                          bounds were accumulated before the state became
 *                          SAOP.  Those bounds are then used to cull array
 *                          elements at bind time.
 *
 * Non-applied fields may remain populated as a side effect of an earlier
 * transition.  For example, if the state was YB_FOLD_RANGE and then got
 * promoted to YB_FOLD_EQUALITY, start_key / end_key will still hold the
 * old bounds but are no longer read.  Callers must consult `type` to
 * decide which fields to use.
 */
typedef enum
{
	YB_FOLD_NONE = 0,
	YB_FOLD_EQUALITY,
	YB_FOLD_IS_NULL,
	YB_FOLD_SAOP,
	YB_FOLD_RANGE,
	YB_FOLD_IS_NOT_NULL
} YbColFoldType;

typedef struct
{
	/* Final fold type after merging all conditions */
	YbColFoldType type;

	/* Equality-bound value */
	Datum		eq_value;

	/*
	 * Inequality bounds. NULL when no lower / upper bound exists.
	 * Bound value, inclusivity, comparison function, and collation
	 * are all derived from the scan key.
	 */
	ScanKey		start_key;
	ScanKey		end_key;

	/* Index into ybScan->keys[] of the first same-column SAOP. */
	int			saop_base_idx;
	/*
	 * Indices of additional same-column SAOPs to intersect with the base.
	 * Palloc'd of size nkeys - 1 when second SAOP is encountered.
	 */
	int		   *saop_extra_idxs;
	/* Total number of SAOPs recorded */
	int			saop_count;
} YbColFoldState;

static bool
YbBindRowComparisonKeys(YbOpaque ybScan, YbScanPlan scan_plan,
						int skey_index, YbColFoldState fold[],
						bool is_for_precheck)
{
	Relation	index = ybScan->index;
	int			length_of_key = YbGetLengthOfKey(&ybScan->keys[skey_index]);

	ScanKey		header_key = ybScan->keys[skey_index];

	ScanKey    *subkeys = &ybScan->keys[skey_index + 1];

	/*
	 * We can only push down a contiguous index-key prefix using the same
	 * comparison operation.
	 *
	 * A leading yb_hash_code subkey (sk_attno = InvalidAttrNumber,
	 * YB_SK_SEARCHHASHCODE flag) is encoded as the first PgGate row-bound
	 * value, followed by the matching hash/range key column prefix.
	 */
	bool		is_hash_index = (index->rd_indoption[0] & INDOPTION_HASH) != 0;

	int			strategy = header_key->sk_strategy;
	int			subkey_count = length_of_key - 1;
	int			first_key_subkey = 0;

	if (is_hash_index)
	{
		Assert(subkey_count > 0);

		if (!YbIsHashCodeSearch(subkeys[0]))
			return true;

		/*
		 * Hash row bounds are encoded as DocKey bounds, which can be
		 * disabled by the AutoFlag-backed GUC.
		 */
		if (!yb_allow_dockey_bounds)
			return true;

		first_key_subkey = 1;
	}

	bool		is_direction_asc =
		is_hash_index ||
		!(index->rd_indoption[subkeys[0]->sk_attno - 1] &
		  INDOPTION_DESC);

	int			max_pushdown_subkey_count =
		Min(subkey_count, index->rd_index->indnkeyatts + first_key_subkey);

	Assert(max_pushdown_subkey_count >= first_key_subkey);

	int			pushdown_subkey_count = first_key_subkey;

	for (; pushdown_subkey_count != max_pushdown_subkey_count;
		 ++pushdown_subkey_count)
	{
		ScanKey		key = subkeys[pushdown_subkey_count];

		/*
		 * Make sure that the same comparator is applied to
		 * all subkeys.
		 */
		if (strategy != key->sk_strategy)
			break;

		/* Make sure that the specified keys are contiguous. */
		if (key->sk_attno != pushdown_subkey_count + 1 - first_key_subkey)
			break;

		if (YbIsHashCodeSearch(key))
			break;

		bool		asc =
			(index->rd_indoption[key->sk_attno - 1] & INDOPTION_DESC) == 0;

		if (strategy != BTEqualStrategyNumber && asc != is_direction_asc)
			break;

		/*
		 * The pushed-down subkey bound constant is re-encoded using the index
		 * column's type (see YBCNewConstant below).  A cross-type comparison
		 * whose argument does not fit that column type (e.g. an out-of-int4
		 * range int8 constant against an int4 column) would be silently
		 * truncated, producing a wrong bound for the DocDB scan.
		 * This mirrors the YbCheckScanTypes guard applied to scalar keys.
		 */
		{
			AttrNumber	attnum =
				scan_plan->bind_key_attnums[skey_index + 1 + pushdown_subkey_count];
			Oid			col_typid =
				ybc_get_atttypid(scan_plan->bind_desc, attnum);

			if (OidIsValid(key->sk_subtype) &&
				!YbIsScanCompatible(col_typid, key->sk_subtype,
									true /* is_value_scalar */ ,
									key->sk_argument))
				break;
		}
	}

	bool		needs_recheck = true;

	if (pushdown_subkey_count <= first_key_subkey)
		return needs_recheck;

	if (is_for_precheck)
		return needs_recheck;

	YbcPgExpr  *col_values = palloc(sizeof(YbcPgExpr) *
									pushdown_subkey_count);

	if (is_hash_index)
	{
		ScanKey		hash_key = subkeys[0];

		col_values[0] = YBCNewConstant(ybScan->handle,
									   INT4OID,
									   hash_key->sk_collation,
									   hash_key->sk_argument,
									   false);
	}

	/*
	 * Prepare upper/lower bound tuples determined from this
	 * clause for bind. Care must be taken in the case
	 * that key columns in the index are ordered
	 * differently from each other. For example, consider
	 * if the underlying index has key
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

	bool		gt = (strategy == BTGreaterEqualStrategyNumber ||
					  strategy == BTGreaterStrategyNumber);

	bool		is_inclusive = (strategy != BTGreaterStrategyNumber &&
								strategy != BTLessStrategyNumber);

	/* Whether or not the RHS values make up a DocDB upper bound */
	bool		is_upper_bound = gt ^ is_direction_asc;

	for (int subkey_index = first_key_subkey;
		 subkey_index < pushdown_subkey_count;
		 ++subkey_index)
	{
		ScanKey		current = subkeys[subkey_index];

		AttrNumber	attnum =
			scan_plan->bind_key_attnums[skey_index + 1 + subkey_index];

		col_values[subkey_index] =
			YBCNewConstant(ybScan->handle,
						   ybc_get_atttypid(scan_plan->bind_desc,
											attnum),
						   current->sk_collation,
						   current->sk_argument,
						   false);

		/*
		 * PgGate rejects IS NOT NULL binds on partition columns, and
		 * the full row comparison remains rechecked by Postgres.
		 */
		if (subkey_index == 0)
		{
			AttrNumber	attno = current->sk_attno;
			int			att_idx = YBAttnumToBmsIndex(ybScan->table,
													attno);

			if (fold[att_idx].type == YB_FOLD_NONE)
				fold[att_idx].type = YB_FOLD_IS_NOT_NULL;
		}
	}

	if (is_upper_bound || strategy == BTEqualStrategyNumber)
	{
		HandleYBStatus(YBCPgDmlAddRowUpperBound(ybScan->handle,
												pushdown_subkey_count,
												col_values,
												is_inclusive));
	}

	if (!is_upper_bound || strategy == BTEqualStrategyNumber)
	{
		HandleYBStatus(YBCPgDmlAddRowLowerBound(ybScan->handle,
												pushdown_subkey_count,
												col_values,
												is_inclusive));
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
 * Fold two unique sorted Datum arrays by intersecting.
 *
 * The intersection is written in place at the front of dst. Underlying memory
 * allocation and pointer are preserved. The new element count is returned.
 */
static int
ybIntersectSortedArrays(Datum *dst, int dst_len, Datum *src, int src_len,
						FmgrInfo *cmp_fn, Oid collation)
{
	int			dst_iter = 0,
				src_iter = 0,
				new_elem_count = 0;

	while (dst_iter < dst_len && src_iter < src_len)
	{
		int32		cmp = DatumGetInt32(FunctionCall2Coll(cmp_fn,
														 collation,
														 dst[dst_iter],
														 src[src_iter]));

		if (cmp == 0)
		{
			dst[new_elem_count++] = dst[dst_iter];
			dst_iter++;
			src_iter++;
		}
		else if (cmp < 0)
			dst_iter++;
		else
			src_iter++;
	}
	return new_elem_count;
}

/*
 * Check whether a scalar Datum is present in a SAOP scan key's array.
 */
static bool
ybIsValueInArray(Datum value, ScanKey saop_key, Datum array_const)
{
	ArrayType  *arrayval = DatumGetArrayTypeP(array_const);
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	Datum	   *elem_values;
	bool	   *elem_nulls;
	int			num_elems;
	bool		found = false;

	get_typlenbyvalalign(ARR_ELEMTYPE(arrayval),
						 &elmlen, &elmbyval, &elmalign);
	deconstruct_array(arrayval, ARR_ELEMTYPE(arrayval),
					  elmlen, elmbyval, elmalign,
					  &elem_values, &elem_nulls, &num_elems);

	for (int i = 0; i < num_elems; i++)
	{
		if (elem_nulls[i])
			continue;
		if (DatumGetBool(FunctionCall2Coll(&saop_key->sk_func,
										   saop_key->sk_collation,
										   value, elem_values[i])))
		{
			found = true;
			break;
		}
	}

	pfree(elem_values);
	pfree(elem_nulls);
	return found;
}

typedef struct
{
	FmgrInfo   *cmp_fn;
	Oid			collation;
} YbSortArrayContext;

/* qsort_arg comparator for ybSortAndUniqArrayElements */
static int
ybCompareArrayElements(const void *a, const void *b, void *arg)
{
	Datum		da = *((const Datum *) a);
	Datum		db = *((const Datum *) b);
	YbSortArrayContext *cxt = (YbSortArrayContext *) arg;

	return DatumGetInt32(FunctionCall2Coll(cxt->cmp_fn,
										   cxt->collation,
										   da, db));
}

/*
 * Sort array elements ascending and eliminate duplicates.  Returns the
 * resulting number of elements.  Based on _bt_sort_array_elements.
 */
static int
ybSortAndUniqArrayElements(Datum *elems, int nelems,
						   FmgrInfo *cmp_fn, Oid collation)
{
	YbSortArrayContext cxt;

	if (nelems <= 1)
		return nelems;			/* no work to do */

	cxt.cmp_fn = cmp_fn;
	cxt.collation = collation;
	qsort_arg(elems, nelems, sizeof(Datum), ybCompareArrayElements, &cxt);
	return qunique_arg(elems, nelems, sizeof(Datum),
					   ybCompareArrayElements, &cxt);
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
 * Notable params:
 * - cmp_fn: element comparator.  Must be valid.
 * - fold_state: accumulated bounds and saop indices used to truncate and merge
 *   SAOP element arrays.  NULL when the key is a row array, or if there were
 *   no accumulated fold information.
 *
 * Returns false if the array is culled to zero elements and NULL is not bound.
 * Caller is still expected to pfree culled_elem_values in this case.
 */
static bool
YbCullArray(ArrayType *arrayval,
			ScanKey key,
			TupleDesc bind_desc,
			FmgrInfo *cmp_fn,
			bool is_row,
			int row_nkeys, AttrNumber *row_attnums,
			Oid scalar_col_typid, Oid scalar_val_typid,
			YbColFoldState *fold_state,
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

			if (fold_state)
			{
				/* Drop array elements that are below the lower range bound. */
				if (fold_state->start_key &&
					!DatumGetBool(FunctionCall2Coll(&fold_state->start_key->sk_func,
													fold_state->start_key->sk_collation,
													elem_values[i],
													fold_state->start_key->sk_argument)))
					continue;

				/* Drop array elements that are above the upper range bound. */
				if (fold_state->end_key &&
					!DatumGetBool(FunctionCall2Coll(&fold_state->end_key->sk_func,
													fold_state->end_key->sk_collation,
													elem_values[i],
													fold_state->end_key->sk_argument)))
					continue;
			}

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

	/*
	 * Sort the non-null elements and eliminate any duplicates.  A scalar IN
	 * bind reaches DocDB's scan options as given, and DocDB requires those
	 * sorted and duplicate-free.  The caller's intersection also requires all
	 * arrays in the same order.  Sort ascending whatever the column's ASC or
	 * DESC sorting type is, since DocDB derives the physical option order
	 * itself from that sorting type and the scan direction.
	 */
	*culled_num_elems = ybSortAndUniqArrayElements(elem_values, num_valid,
												   cmp_fn,
												   key->sk_collation);

	return true;
}

/*
 * Bind scalar array ops and row array ops.
 *
 * In/out params:
 * - is_column_bound: tracks binding state for each column
 *
 * Out params:
 * - bail_out: set true when an empty array or contradiction makes the scan
 *   unsatisfiable, results in early return
 *
 * Notable params:
 * - skey_index: the scan key index we are focusing on
 * - fold_state: accumulated bounds and saop indices used to truncate and merge
 *   SAOP element arrays.  NULL when the key is a row array, or if there were
 *   no accumulated fold information.
 */
static void
YbBindSearchArray(YbOpaque ybScan, YbScanPlan scan_plan,
				  int skey_index, bool is_for_precheck,
				  YbColFoldState *fold_state,
				  bool is_column_bound[],
				  bool *bail_out)
{
	/* based on _bt_preprocess_array_keys() */
	ArrayType  *arrayval;
	int			num_elems;
	Datum	   *elem_values;
	ScanKey		key = ybScan->keys[skey_index];
	AttrNumber	scalar_attnum = scan_plan->bind_key_attnums[skey_index];
	Relation	relation = ybScan->table;

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
				ybScan->needs_recheck = true;
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

	/*
	 * Resolve the element comparator once for this key: the per-array sort in
	 * YbCullArray and the SAOP intersection below must use the same ordering.
	 * Key it off the element type (sk_subtype) instead of the index opfamily
	 * because DocDB needs the array in the order its own key encoding induces,
	 * and that encoding follows the type, the column collation (which index
	 * collation matching keeps equal to sk_collation), and the column's
	 * sorting type, with no notion of opclasses.
	 */
	TypeCacheEntry *typentry = lookup_type_cache(key->sk_subtype,
												 TYPECACHE_CMP_PROC_FINFO);
	FmgrInfo   *cmp_fn = &typentry->cmp_proc_finfo;

	/*
	 * A type with no default comparator leaves cmp_fn invalid.  Such a key is
	 * recheck-only: its arrays cannot be sorted for binding nor intersected,
	 * so bind nothing and let recheck enforce the whole condition.  This is
	 * decided before the precheck return, so precheck and execution agree.
	 */
	if (!OidIsValid(cmp_fn->fn_oid))
	{
		ybScan->needs_recheck = true;
		return;
	}

	if (is_for_precheck)
		return;

	arrayval = DatumGetArrayTypeP(YbGetArrayConst(&ybScan->keys[skey_index]));
	Assert(key->sk_subtype == ARR_ELEMTYPE(arrayval));
	if (!YbCullArray(arrayval,
					 key,
					 scan_plan->bind_desc,
					 cmp_fn,
					 is_row,
					 row_nkeys, row_attnums,
					 scalar_col_typid, scalar_val_typid,
					 is_row ? NULL : fold_state,
					 &scalar_null_bound,
					 &elem_values,
					 &num_elems))
	{
		*bail_out = true;
		pfree(elem_values);
		return;
	}

	/*
	 * SAOP intersection: when the fold state records additional
	 * same-column SAOPs of the same subtype, cull each extra array
	 * against the fold bounds and intersect it into the primary array's
	 * element list.
	 */
	if (!is_row && fold_state != NULL && fold_state->saop_count > 1)
	{
		for (int j = 0; j < fold_state->saop_count - 1 && num_elems > 0; j++)
		{
			ScanKey		fold_key = ybScan->keys[fold_state->saop_extra_idxs[j]];
			ArrayType  *fold_arrayval;
			int			fold_nelems;
			Datum	   *fold_elems;
			bool		fold_null = false;

			fold_arrayval = DatumGetArrayTypeP(YbGetArrayConst(&fold_key));

			if (!YbCullArray(fold_arrayval,
							 fold_key,
							 scan_plan->bind_desc,
							 cmp_fn,
							 false,
							 0, NULL,
							 scalar_col_typid, scalar_val_typid,
							 fold_state,
							 &fold_null,
							 &fold_elems,
							 &fold_nelems))
			{
				*bail_out = true;
				pfree(elem_values);
				pfree(fold_elems);
				return;
			}

			num_elems = ybIntersectSortedArrays(elem_values,
												num_elems,
												fold_elems,
												fold_nelems,
												cmp_fn,
												key->sk_collation);

			pfree(fold_elems);
		}

		if (num_elems == 0)
		{
			*bail_out = true;
			pfree(elem_values);
			return;
		}
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
 * Fold a new inequality scankey into the accumulated bound on one side,
 * then check for contradiction with the bound on the other side.
 *
 * is_lower_bound selects which side to update: true for the lower (start_key)
 * bound, false for the upper (end_key) bound.
 */
static bool
YbFoldInequalityBound(YbColFoldState *fs, ScanKey key, bool is_lower_bound)
{
	ScanKey    *bound = is_lower_bound ? &fs->start_key : &fs->end_key;

	/* All scan keys on the same column must share the same collation. */
	Assert(!fs->start_key || key->sk_collation == fs->start_key->sk_collation);
	Assert(!fs->end_key || key->sk_collation == fs->end_key->sk_collation);

	if (*bound)
	{
		bool		existing_bound_tighter =
			DatumGetBool(FunctionCall2Coll(&key->sk_func,
										   key->sk_collation,
										   (*bound)->sk_argument,
										   key->sk_argument));

		if (!existing_bound_tighter)
			*bound = key;
	}
	else
		*bound = key;

	/* Contradiction check once both sides exist. */
	if (fs->start_key && fs->end_key)
	{
		/*
		 * Each check runs one side's value through the other side's
		 * comparator (which may be strict or non-strict) to verify the
		 * bounds don't cross.
		 */
		bool		start_satisfies_end =
			DatumGetBool(FunctionCall2Coll(&fs->end_key->sk_func,
										   key->sk_collation,
										   fs->start_key->sk_argument,
										   fs->end_key->sk_argument));
		bool		end_satisfies_start =
			DatumGetBool(FunctionCall2Coll(&fs->start_key->sk_func,
										   key->sk_collation,
										   fs->end_key->sk_argument,
										   fs->start_key->sk_argument));

		if (!start_satisfies_end || !end_satisfies_start)
			return false;
	}
	return true;
}

/*
 * Use the scan-descriptor and scan-plan to setup binds for the queryplan.
 */
static bool
ybBindOrdinaryScanKeys(YbOpaque ybScan, YbScanPlan scan_plan, Scan *scan,
					   bool is_for_precheck)
{
	Relation	relation = scan_plan->target_relation;

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

		if (max_idx < idx && bms_is_member(idx, scan_plan->qualified_scan_key_cols))
			max_idx = idx;
	}
	max_idx++;

	/* Find intervals for columns */

	bool		is_column_bound[max_idx];	/* VLA */

	memset(is_column_bound, 0, sizeof(bool) * max_idx);

	YbColFoldState fold[max_idx];	/* VLA - per-column fold state */

	memset(fold, 0, sizeof(YbColFoldState) * max_idx);

	bool		key_folded[ybScan->nkeys + 1]; /* VLA - per-key fold flag */

	memset(key_folded, 0, sizeof(bool) * (ybScan->nkeys + 1));

	if (yb_enable_advanced_index_cond_fold && !is_for_precheck)
	{
		/*
		 * Key pass: iterate over all scan keys and fold same-column conditions
		 * into per-column YbColFoldState entries.  Row comparisons and Row IN
		 * are bound immediately.  Contradictions cause early return.
		 */
		for (int i = 0; i < ybScan->nkeys; i += YbGetLengthOfKey(&ybScan->keys[i]))
		{
			ScanKey		key = ybScan->keys[i];

			/* Row comparison binds directly without conflicting with other bindings */
			if (YbIsRowHeader(key) && !YbIsSearchArray(key))
			{
				bool		rc = YbBindRowComparisonKeys(ybScan, scan_plan,
														i, fold,
														is_for_precheck);

				ybScan->needs_recheck |= rc;
				continue;
			}

			int			idx = YBAttnumToBmsIndex(relation,
												 scan_plan->bind_key_attnums[i]);

			if (!bms_is_member(idx, scan_plan->qualified_scan_key_cols))
			{
				ybScan->needs_recheck = true;
				continue;
			}

			if (!YbCheckScanTypes(ybScan, scan_plan, i))
			{
				ybScan->needs_recheck = true;
				continue;
			}

			/*
			 * Skip columns already bound.  In current practice Row IN comes
			 * only from internal scans, so we never see a second Row IN on
			 * the same column, but enforce it here to stay safe against
			 * future callers.
			 */
			if (is_column_bound[idx])
			{
				ybScan->needs_recheck = true;
				continue;
			}

			/* Row IN binds directly */
			if (YbIsRowHeader(key) && YbIsSearchArray(key))
			{
				bool		bail_out = false;

				YbBindSearchArray(ybScan, scan_plan, i,
								  false,
								  NULL,
								  is_column_bound,
								  &bail_out);

				if (bail_out)
					return false;
				continue;
			}

			/* Begin fold state construction */
			YbColFoldState *fs = &fold[idx];

			/* IS NOT NULL */
			if (key->sk_strategy == InvalidStrategy &&
				YbIsSearchNotNull(key))
			{
				/* Contradicts IS NULL. */
				if (fs->type == YB_FOLD_IS_NULL)
					return false;
				if (fs->type == YB_FOLD_NONE)
					fs->type = YB_FOLD_IS_NOT_NULL;
			}
			/* IS NULL */
			else if (key->sk_strategy == InvalidStrategy &&
					 YbIsSearchNull(key))
			{
				/* Contradicts every state except NONE and IS_NULL. */
				if (fs->type != YB_FOLD_NONE &&
					fs->type != YB_FOLD_IS_NULL)
					return false;
				fs->type = YB_FOLD_IS_NULL;
			}
			/* EQUALITY */
			else if (key->sk_strategy == BTEqualStrategyNumber &&
					 YbIsBasicOpSearch(key))
			{
				switch (fs->type)
				{
					case YB_FOLD_NONE:
					case YB_FOLD_IS_NOT_NULL:
						/* State sets to EQUALITY */
						fs->type = YB_FOLD_EQUALITY;
						fs->eq_value = key->sk_argument;
						break;

					case YB_FOLD_EQUALITY:
						/* Check contradiction with current EQUALITY */
						if (!DatumGetBool(FunctionCall2Coll(&key->sk_func,
															key->sk_collation,
															fs->eq_value,
															key->sk_argument)))
							return false;
						break;

					case YB_FOLD_RANGE:
						/* State sets to EQUALITY if range is satisfied */
						if (fs->start_key &&
							!DatumGetBool(FunctionCall2Coll(&fs->start_key->sk_func,
															fs->start_key->sk_collation,
															key->sk_argument,
															fs->start_key->sk_argument)))
							return false;
						if (fs->end_key &&
							!DatumGetBool(FunctionCall2Coll(&fs->end_key->sk_func,
															fs->end_key->sk_collation,
															key->sk_argument,
															fs->end_key->sk_argument)))
							return false;
						fs->type = YB_FOLD_EQUALITY;
						fs->eq_value = key->sk_argument;
						break;

					case YB_FOLD_SAOP:
					{
						/* State sets to EQUALITY if value is in every array */
						for (int s = 0; s < fs->saop_count; s++)
						{
							int			sidx = (s == 0) ?
								fs->saop_base_idx : fs->saop_extra_idxs[s - 1];
							ScanKey		saop = ybScan->keys[sidx];
							Datum		arr = YbGetArrayConst(&ybScan->keys[sidx]);

							if (!ybIsValueInArray(key->sk_argument, saop, arr))
								return false;
						}
						fs->type = YB_FOLD_EQUALITY;
						fs->eq_value = key->sk_argument;
						break;
					}

					case YB_FOLD_IS_NULL:
						/* Contradicts IS NULL. */
						return false;
				}
			}
			/* SAOP */
			else if (key->sk_strategy == BTEqualStrategyNumber &&
					 YbIsSearchArray(key))
			{
				switch (fs->type)
				{
					case YB_FOLD_NONE:
					case YB_FOLD_IS_NOT_NULL:
					case YB_FOLD_RANGE:
						/* State sets to SAOP, record this as the base SAOP key */
						fs->type = YB_FOLD_SAOP;
						fs->saop_base_idx = i;
						fs->saop_count = 1;
						break;

					case YB_FOLD_EQUALITY:
					{
						/* Check contradiction with current EQUALITY */
						Datum		arr = YbGetArrayConst(&ybScan->keys[i]);

						if (!ybIsValueInArray(fs->eq_value, key, arr))
							return false;
						break;
					}

					case YB_FOLD_SAOP:
						if (key->sk_subtype ==
							ybScan->keys[fs->saop_base_idx]->sk_subtype)
						{
							/* Record additional SAOP key for folding */
							if (fs->saop_count == 1)
								fs->saop_extra_idxs = palloc(sizeof(int) *
															 (ybScan->nkeys - 1));
							fs->saop_extra_idxs[fs->saop_count - 1] = i;
							fs->saop_count++;
						}
						else
						{
							/* Cross-type SAOP cannot be intersected. */
							ybScan->needs_recheck = true;
							continue;
						}
						break;

					case YB_FOLD_IS_NULL:
						/* Contradicts IS NULL. */
						return false;
				}
			}
			/* INEQUALITY */
			else if (key->sk_strategy == BTGreaterEqualStrategyNumber ||
					 key->sk_strategy == BTGreaterStrategyNumber ||
					 key->sk_strategy == BTLessEqualStrategyNumber ||
					 key->sk_strategy == BTLessStrategyNumber)
			{
				/* Contradicts IS NULL. */
				if (fs->type == YB_FOLD_IS_NULL)
					return false;

				/* Check contradiction with current EQUALITY */
				if (fs->type == YB_FOLD_EQUALITY)
				{
					bool		satisfies =
						DatumGetBool(FunctionCall2Coll(&key->sk_func,
													   key->sk_collation,
													   fs->eq_value,
													   key->sk_argument));

					if (!satisfies)
						return false;
				}
				else
				{
					/* For all other states, accumulate tightest bounds */
					if (fs->type == YB_FOLD_NONE ||
						fs->type == YB_FOLD_IS_NOT_NULL)
						fs->type = YB_FOLD_RANGE;

					switch (key->sk_strategy)
					{
						case BTGreaterEqualStrategyNumber:
						case BTGreaterStrategyNumber:
							if (!YbFoldInequalityBound(fs, key, true))
								return false;
							break;

						case BTLessEqualStrategyNumber:
						case BTLessStrategyNumber:
							if (!YbFoldInequalityBound(fs, key, false))
								return false;
							break;

						case InvalidStrategy:
						case BTEqualStrategyNumber:
							pg_unreachable();
					}
				}
			}
			else
			{
				continue;
			}

			key_folded[i] = true;
		}

		/*
		 * Column pass: bind directly from resolved fold state.
		 * Columns already bound by Row IN are skipped. Any additional
		 * conditions on those columns go to recheck.
		 */
		for (int idx = 0; idx < max_idx; idx++)
		{
			YbColFoldState *fs = &fold[idx];

			/* Skip columns already bound */
			if (is_column_bound[idx])
			{
				if (fs->type != YB_FOLD_NONE)
					ybScan->needs_recheck = true;
				continue;
			}

			AttrNumber	attnum = YBBmsIndexToAttnum(relation, idx);

			switch (fs->type)
			{
				case YB_FOLD_EQUALITY:
					YbBindColumn(ybScan, scan_plan->bind_desc,
								 attnum, fs->eq_value, false);
					break;

				case YB_FOLD_IS_NULL:
					YbBindColumn(ybScan, scan_plan->bind_desc,
								 attnum, (Datum) 0, true);
					break;

				case YB_FOLD_SAOP:
				{
					/*
					 * The first SAOP key drives the bind. If any inequality
					 * bounds or additional SAOP indices were accumulated for
					 * this column, pass the fold state so YbBindSearchArray
					 * can cull and merge accordingly.
					 */
					bool		bail_out = false;

					YbBindSearchArray(ybScan, scan_plan, fs->saop_base_idx,
									  false,
									  fs,
									  is_column_bound,
									  &bail_out);

					if (bail_out)
						return false;
					break;
				}

				case YB_FOLD_RANGE:
					YbBindColumnCondBetween(ybScan, scan_plan->bind_desc,
											attnum,
											fs->start_key != NULL,
											fs->start_key &&
											fs->start_key->sk_strategy == BTGreaterEqualStrategyNumber,
											fs->start_key ? fs->start_key->sk_argument : (Datum) 0,
											fs->end_key != NULL,
											fs->end_key &&
											fs->end_key->sk_strategy == BTLessEqualStrategyNumber,
											fs->end_key ? fs->end_key->sk_argument : (Datum) 0);
					break;

				case YB_FOLD_IS_NOT_NULL:
					YbBindColumnNotNull(ybScan, scan_plan->bind_desc,
										attnum);
					break;

				case YB_FOLD_NONE:
					break;
			}
		}

		return true;
	}

	/*
	 * Non-fold path: priority-based binding. To be cleaned up.
	 *
	 * Bind the merge-scan pinned SAOPs first so that the priority-based
	 * binding below finds their columns already bound and leaves them to drive
	 * the merge streams.
	 */
	for (int i = 0; i < ybScan->nkeys; i += YbGetLengthOfKey(&ybScan->keys[i]))
	{
		ScanKey		key = ybScan->keys[i];

		if (YbIsSearchArray(key))
		{
			Datum		this_array_const;
			ListCell   *lc;
			YbMergeScanInfo *yb_merge_scan_info = NULL;

			this_array_const = YbGetArrayConst(&ybScan->keys[i]);

			if (scan)
			{
				if (IsA(scan, IndexScan))
					yb_merge_scan_info =
						((IndexScan *) scan)->yb_merge_scan_info;
				else if (IsA(scan, IndexOnlyScan))
					yb_merge_scan_info =
						((IndexOnlyScan *) scan)->yb_merge_scan_info;
			}

			if (yb_merge_scan_info)
			{
				foreach(lc, yb_merge_scan_info->saop_cols)
				{
					ScalarArrayOpExpr *pinned_saop =
						((YbMergeScanSaopColInfo *) lfirst(lc))->saop;
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
										  NULL,
										  is_column_bound,
										  &bail_out);

						if (bail_out)
							return false;

						break;
					}
				}
			}
		}
	}

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

		/* Check if this is full key row comparison expression */
		if (YbIsRowHeader(key) &&
			!YbIsSearchArray(key))
		{
			bool		needs_recheck = YbBindRowComparisonKeys(ybScan, scan_plan, i,
																fold,
																is_for_precheck);

			ybScan->needs_recheck |= needs_recheck;
			/*
			 * Full key RowComparison bindings don't interact
			 * or interfere too much with other bindings to the same columns.
			 * They set the upper/lower bounds of the requested scan and also
			 * apply IS NOT NULL filters on the bound LHS columns.
			 */
			continue;
		}

		int			bind_key_attnum = scan_plan->bind_key_attnums[i];
		int			idx = YBAttnumToBmsIndex(relation, bind_key_attnum);

		/* Check if this is a qualified key column. */
		if (!bms_is_member(idx, scan_plan->qualified_scan_key_cols))
		{
			ybScan->needs_recheck = true;
			continue;
		}

		/*
		 * Assign key offsets. Where n is the number of keys, and i is the
		 * clause's index in the list (i < n):
		 *  Clause Type          |    Value
		 * ----------------------+--------------
		 *  ROW IN               | -(n * 2 + i)
		 *  EQUAL, IS NULL       | -(n + i)
		 *  IN                   |     -i
		 *  BETWEEN, IS NOT NULL |      i
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
			ybScan->needs_recheck = true;
			continue;
		}

		switch (key->sk_strategy)
		{
			case InvalidStrategy:
				if (YbIsSearchNotNull(key))
				{
					if (fold[idx].type == YB_FOLD_NONE)
						fold[idx].type = YB_FOLD_IS_NOT_NULL;
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
									  NULL,
									  is_column_bound,
									  &bail_out);

					if (bail_out)
						return false;
				}
				break;

			case BTGreaterEqualStrategyNumber:
			case BTGreaterStrategyNumber:
			case BTLessEqualStrategyNumber:
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

				{
					bool		is_lower_bound =
						(key->sk_strategy == BTGreaterEqualStrategyNumber ||
						 key->sk_strategy == BTGreaterStrategyNumber);

					if (!YbFoldInequalityBound(&fold[idx], key, is_lower_bound))
						return false;
				}
				break;

			default:
				break;			/* unreachable */
		}
	}

	/* Bind keys for BETWEEN and IS NOT NULL */
	int			min_idx = bms_first_member(scan_plan->qualified_scan_key_cols);

	min_idx = min_idx < 0 ? 0 : min_idx;
	for (int idx = min_idx; idx < max_idx; idx++)
	{
		YbColFoldState *fs = &fold[idx];

		/* There's no range key or IS NOT NULL for this query */
		if (!fs->start_key && !fs->end_key &&
			fs->type != YB_FOLD_IS_NOT_NULL)
			continue;

		/* Do not bind more than one condition to a column */
		if (is_column_bound[idx])
		{
			ybScan->needs_recheck = true;
			continue;
		}

		if (!is_for_precheck)
		{
			if (fs->start_key || fs->end_key)
			{
				YbBindColumnCondBetween(ybScan, scan_plan->bind_desc,
										YBBmsIndexToAttnum(relation, idx),
										fs->start_key != NULL,
										fs->start_key &&
										fs->start_key->sk_strategy == BTGreaterEqualStrategyNumber,
										fs->start_key ? fs->start_key->sk_argument : (Datum) 0,
										fs->end_key != NULL,
										fs->end_key &&
										fs->end_key->sk_strategy == BTLessEqualStrategyNumber,
										fs->end_key ? fs->end_key->sk_argument : (Datum) 0);
			}
			else
			{
				Assert(fs->type == YB_FOLD_IS_NOT_NULL);
				YbBindColumnNotNull(ybScan, scan_plan->bind_desc,
									YBBmsIndexToAttnum(relation, idx));
			}
		}
	}
	return true; /* end of non-fold path. */
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
ybBindSearchHashCodeScanKeys(YbOpaque ybScan, bool is_for_precheck)
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

	if (is_for_precheck)
		return true;

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

/*
 * Two modes:
 * - is_for_precheck=true: return whether unsatisfiable conditions were found
 *   and set field needs_recheck in the given ybScan.  Both results are not
 *   necessarily exact and err on the side of caution:
 *   - may return false (signifying no unsatisfiable conditions) even if there
 *     are unsatisfiable conditions.
 *   - may set needs_recheck true even if recheck is not actually needed.
 * - is_for_precheck=false: same as above case but also actually performs binds
 *   to the ybScan->handle.
 *
 * TODO(jason): needs_recheck initialization should be made clearer by only
 * setting it once such as by passing it as an out-param to this function.
 * This only makes more sense if the constant params are eliminated from
 * YbOpaque and put in some different structure.  In general, this function
 * deserves three cases of return information: bail-out, no-bail-out+recheck,
 * no-bail-out+no-recheck.
 */
static bool
ybBindScanKeys(YbOpaque ybScan, YbScanPlan scan_plan, Scan *scan,
			   bool is_for_precheck)
{
	/*
	 * Best-effort try to determine if all keys are bound.
	 * - YBCIsSysTablePrefetchingStarted: if this scan is for system table
	 *   prefetching, it is a special case that doesn't push down conditions,
	 *   so assume the worst.
	 */
	ybScan->needs_recheck = YBCIsSysTablePrefetchingStarted();

	/*
	 * For testing, skip doing any scan key binds if
	 * yb_test_skip_binding_scan_keys=true.  Internal scans (signified by
	 * "scan" variable being NULL) do not have additional recheck mechanisms
	 * besides YB recheck, so avoid skipping binds for those as that may
	 * possibly lead to incorrect results.
	 */
	if (yb_test_skip_binding_scan_keys && scan)
	{
		/*
		 * NOTE: We set needs_recheck to true even in case there are zero scan
		 * keys.
		 */
		ybScan->needs_recheck = true;
		return true;
	}

	if (!ybBindOrdinaryScanKeys(ybScan, scan_plan, scan, is_for_precheck))
	{
		/* In case of unsatisfiable scan, recheck is trivially not needed. */
		ybScan->needs_recheck = false;
		return false;
	}

	if (!ybBindSearchHashCodeScanKeys(ybScan, is_for_precheck))
	{
		/* In case of unsatisfiable scan, recheck is trivially not needed. */
		ybScan->needs_recheck = false;
		return false;
	}

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
 * Replace decoded PK column requests with a request for ybidxbasectid.
 */
static void
ybcReplacePkReqWithBasectid(IndexOnlyScan *ios_plan,
							YbAttnumBmsState *result)
{
	if (ios_plan->yb_num_decoded_pk_cols > 0)
	{
		int			phys_natts = list_length(ios_plan->indextlist) -
								 ios_plan->yb_num_decoded_pk_cols;
		bool		removed = false;
		int			bms_idx = ybcAttnumBmsIndex(result, phys_natts);

		while ((bms_idx = bms_next_member(result->bms, bms_idx)) >= 0)
		{
			bms_del_member(result->bms, bms_idx);
			removed = true;
		}
		if (removed)
			ybcAttnumBmsAdd(result, YBIdxBaseTupleIdAttributeNumber);
	}
}

/*
 * Returns list of target columns required by scan plan.
 */
static YbAttnumBmsState
ybcBuildRequiredAttrs(YbOpaque yb_scan, YbScanPlan scan_plan,
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
		 * Collect local YB/PG recheck attributes
		 *
		 * TODO(jason): only do this if YB/PG recheck is needed.
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

	/*
	 * For index-only scans with decoded primary key columns, do not request
	 * their values from DocDB. Instead, request ybidxbasectid for decoding.
	 */
	if (pg_scan_plan && IsA(pg_scan_plan, IndexOnlyScan))
		ybcReplacePkReqWithBasectid((IndexOnlyScan *) pg_scan_plan, &result);

	return result;
}

static void
ybcSetupTargets(YbOpaque yb_scan, YbScanPlan scan_plan, Scan *pg_scan_plan)
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
YbApplyMergeSortKeys(YbOpaque ybScan, Scan *pg_scan_plan)
{
	YbSortInfo *sort_info = NULL;
	bool		reverse = false;
	bool		yb_add_sort_targets = false;
	int16	   *indkey_values = NULL;

	if (IsA(pg_scan_plan, IndexScan))
	{
		IndexScan  *plan = (IndexScan *) pg_scan_plan;

		if (plan->yb_merge_scan_info)
		{
			sort_info = plan->yb_merge_scan_info->sort_cols;
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

		if (plan->yb_merge_scan_info)
		{
			sort_info = plan->yb_merge_scan_info->sort_cols;
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
 * Before beginning execution, determine whether any kind of recheck is needed:
 * - YB recheck
 * - PG recheck
 * There is only one condition to avoid both of those: needs_recheck.  Use as
 * little resources as possible to make this determination.  This is largely a
 * dup of YbBeginScan minus the unessential parts.
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
	YbOpaqueData ybscan = {0};

	ybExtractScanKeys(keys, nkeys, &ybscan);

	if (YbIsUnsatisfiableCondition(ybscan.nkeys, ybscan.keys))
		return false;

	ybscan.table = relation;
	ybscan.index = index;

	/* Set up the scan plan */
	YbScanPlanData scan_plan;

	ybcSetupScanPlan(xs_want_itup, &ybscan, &scan_plan);
	ybcSetupScanKeys(&ybscan, &scan_plan);

	/* Determine needs_recheck. */
	(void) ybBindScanKeys(&ybscan, &scan_plan, scan,
						  true);	/* is_for_precheck */

	bms_free(scan_plan.hash_key_cols);
	bms_free(scan_plan.key_cols);
	bms_free(scan_plan.qualified_scan_key_cols);
	return ybscan.needs_recheck;
}

/*
 * Begin a scan for
 *   SELECT <Targets> FROM <relation> USING <index> WHERE <Binds>
 * NOTES:
 * - "table" is the table (not index).  Must always be specified.
 * - "index" is the index, if applicable.  NULL otherwise.
 * - "nkeys" and "keys" identify which key columns are provided in the SELECT
 *   WHERE clause.  Can be 0/NULL.
 *   - nkeys = Number of keys.
 *   - keys[].sk_attno = the column's attribute number with respect to
 *     - "table" if sequential scan
 *     - "index" if index (only) scan
 *     Easy way to tell between the two cases is whether index is NULL.
 *     Note: ybc_systable_beginscan can call for either case.
 * - If "xs_want_itup" is true, Postgres layer is expecting an IndexTuple.
 * - "rel_pushdown" defines expressions to push down to the targeted relation.
 *   - sequential scan: table.
 *   - index scan: table (not index).
 *   - index only scan: index.
 * - "idx_pushdown" defines expressions to push down to the index in case of an
 *   index scan.
 */
YbOpaque
YbBeginScan(Relation table,
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
	YbOpaque	ybScan = (YbOpaque) palloc0(sizeof(YbOpaqueData));

	/* Flatten keys and store the results in ybScan. */
	ybExtractScanKeys(keys, nkeys, ybScan);

	if (YbIsUnsatisfiableCondition(ybScan->nkeys, ybScan->keys))
	{
		ybScan->quit_scan = true;
		return ybScan;
	}
	ybScan->exec_params = exec_params;
	ybScan->table = table;
	ybScan->index = index;
	ybScan->quit_scan = false;
	ybScan->prepare_params.fetch_ybctids_only = fetch_ybctids_only;

	/* Set up the scan plan */
	YbScanPlanData scan_plan;

	ybcSetupScanPlan(xs_want_itup, ybScan, &scan_plan);
	ybcSetupScanKeys(ybScan, &scan_plan);

	ybScan->handle = YbNewSelect(table, &ybScan->prepare_params);

	/* Set up binds */
	if (!ybBindScanKeys(ybScan, &scan_plan, pg_scan_plan,
						false /* is_for_precheck */ ))
	{
		ybScan->quit_scan = true;
		bms_free(scan_plan.hash_key_cols);
		bms_free(scan_plan.key_cols);
		bms_free(scan_plan.qualified_scan_key_cols);
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
	 * TODO(jason): revisit this for #15080.
	 *
	 * Initdb and walsender don't have local catalog version, so ignore for
	 * those cases as well.
	 */
	if (!(is_internal_scan && IsSystemRelation(table)) &&
		!IsBootstrapProcessingMode() &&
		MyBackendType != B_WAL_SENDER &&
		MyBackendType != YB_YSQL_CONN_MGR_WAL_SENDER)
		YbSetCatalogCacheVersion(ybScan->handle,
								 YbGetCatalogCacheVersion());

	/* Set distinct prefix length. */
	if (distinct_prefixlen > 0)
		YBCPgSetDistinctPrefixLength(ybScan->handle, distinct_prefixlen);

	if (pg_scan_plan)
		YbApplyMergeSortKeys(ybScan, pg_scan_plan);

	bms_free(scan_plan.hash_key_cols);
	bms_free(scan_plan.key_cols);
	bms_free(scan_plan.qualified_scan_key_cols);
	return ybScan;
}

/*
 * There are two levels of recheck:
 * - YB recheck
 * - PG recheck
 * If YB recheck can make perfect yes/no decisions, then PG recheck is not
 * needed.
 *
 * Since the logic for determining whether YB recheck can make perfect
 * decisions and for actually performing the YB recheck are tightly coupled,
 * this function shares the code for both modes:
 * - For determining whether PG recheck may be needed: tup is NULL, and
 *   return...
 *   - false: tuples definitely will not need PG recheck
 *   - true: tuples might perform PG recheck
 * - For actually executing YB recheck: tup is not NULL, and return...
 *   - false: in case PG recheck is...
 *     - needed: tup needs PG recheck
 *     - not needed: tup is a valid match
 *   - true: tup definitely mismatches
 */
static bool
ybRecheck(HeapTuple tup, YbOpaque ybScan)
{
	ScanKey    *keys = ybScan->keys;
	AttrNumber *sk_attno = ybScan->target_key_attnums;
	bool		is_determining_pg_recheck_mode = !tup;

	/*
	 * Neither YB recheck nor PG recheck is needed if all scan keys are bound.
	 * The caller is expected to avoid calling this function in that case.
	 */
	Assert(ybScan->needs_recheck);

	/*
	 * Index Only Scan never goes through YB recheck, so it makes no sense to
	 * call this function in that case.
	 */
	Assert(!ybScan->prepare_params.index_only_scan);

	for (int i = 0; i < ybScan->nkeys; i += YbGetLengthOfKey(&keys[i]))
	{
		/* TODO: support expressions */
		if (sk_attno[i] == InvalidAttrNumber)
		{
			if (is_determining_pg_recheck_mode)
				return true;
			continue;
		}

		/*
		 * res_datum is ill-defined when length of key is not 1, but such cases
		 * would continue/return before we get to read res_datum.
		 *
		 * Both res_datum and is_null are ill-defined when
		 * is_determining_pg_recheck_mode, but we don't read those values in
		 * that case.
		 */
		ScanKey		key = keys[i];
		bool		is_null = false;
		Datum		res_datum = (tup ?
								 heap_getattr(tup, sk_attno[i],
											  ybScan->target_desc, &is_null) :
								 PointerGetDatum(NULL));

		if (key->sk_flags & SK_SEARCHNULL)
		{
			if (is_determining_pg_recheck_mode || is_null)
				continue;
			else
				return true;
		}

		if (key->sk_flags & SK_SEARCHNOTNULL)
		{
			if (is_determining_pg_recheck_mode || !is_null)
				continue;
			else
				return true;
		}

		if (key->sk_flags == 0)
		{
			bool		matches;

			if (is_determining_pg_recheck_mode)
				continue;
			if (is_null)
				return true;

			matches = DatumGetBool(FunctionCall2Coll(&key->sk_func,
													 key->sk_collation,
													 res_datum,
													 key->sk_argument));

			if (!matches)
				return true;
		}

		/* TODO: support the different search options like SK_SEARCHARRAY. */
		if (is_determining_pg_recheck_mode)
			return true;
	}

	return false;
}

/*
 * Whether rows returned by DocDB may need to go through PG recheck.  This
 * function is ready to be called after calling ybBindScanKeys, which sets some
 * variables that are read here.  There is an implicit assumption that this
 * returns false for heap/system scans.
 */
inline bool
YbNeedsPgRecheck(YbOpaque yb_scan)
{
	if (!yb_scan->needs_recheck)
		return false;

	/*
	 * Index Only Scan does not go through YB recheck like the other scans.  So
	 * needs_recheck means we need PG recheck.
	 */
	if (yb_scan->prepare_params.index_only_scan)
		return true;

	/*
	 * If YB recheck cannot always make a clear decision, we need to fall back
	 * to PG recheck.
	 */
	return ybRecheck(NULL /* tup */ , yb_scan);
}

HeapTuple
ybc_getnext_heaptuple(YbOpaque ybScan, ScanDirection dir)
{
	HeapTuple	tup = NULL;

	if (ybScan->quit_scan)
		return NULL;

	/* Loop over rows from pggate. */
	while (HeapTupleIsValid(tup = ybcFetchNextHeapTuple(ybScan, dir)))
	{
		if (!ybScan->needs_recheck)
			return tup;

		/*
		 * Do a YB recheck first before deferring to a PG recheck if needed.
		 * In case of heap/system scans, this is the main check, and it is
		 * unexpected to need a PG recheck as there is no such code to do that.
		 */
		if (ybRecheck(tup, ybScan))
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
ybc_getnext_indextuple(YbOpaque ybScan, ScanDirection dir)
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
ybc_free_ybscan(YbOpaque ybscan)
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
	 * even though it's YbOpaque was not set. We need to cleanup after the
	 * biss_ScanDesc but not the YbOpaque.
	 */
	if (PointerIsValid(ybscan))
	{
		YBCPgDeleteStatement(ybscan->handle);
		pfree(ybscan);
	}
}

/*
 * Build a Scan node with a targetlist containing the columns required for
 * index backfill. This creates Var nodes for each column needed by the index,
 * allowing YbBeginScan to use its standard column projection logic.
 *
 * This optimization significantly reduces the amount of data read from
 * DocDB during index backfill, especially for tables with many columns
 * where a given index references only a few.
 */
static Scan *
ybcBuildScanPlanForIndexBuild(Relation relation, IndexInfo *indexInfo)
{
	Scan	   *scan_plan;
	TupleDesc	tupdesc = RelationGetDescr(relation);
	List	   *targetlist = NIL;
	Var		   *ybctid_var;
	TargetEntry *ybctid_tle;
	int			i;
	int			idx;
	int			resno = 1;
	/*
	 * Use varno=1 since this is always scanning the base relation.
	 * Concurrent index creation in postgres is restricted to one index
	 * per table/statement (unlike the non-concurrent index creation process).
	 * As a result, we're guaranteed that only one table is involved in the process,
	 * and said table is opened for inspection first, leading to it being varno=1.
	 */
	const Index varno = 1;

	/* This function is only for Yugabyte relations */
	Assert(IsYBRelation(relation));

	/*
	 * Use YbAttnumBmsState for bitmapset operations. This handles the
	 * min_attr offset correctly for Yugabyte relations which may have
	 * different system columns than standard Postgres relations.
	 * Initialize at declaration since min_attr is const.
	 */
	YbAttnumBmsState required_attrs = ybcAttnumBmsConstruct();

	/*
	 * Always need ybctid for index entry construction. Add it directly to
	 * targetlist since it's the only system column allowed in indexes.
	 */
	ybctid_var = makeVar(varno,
						 YBTupleIdAttributeNumber,
						 BYTEAOID,
						 -1,		/* typmod */
						 InvalidOid,	/* collation */
						 0);		/* varlevelsup */
	ybctid_tle = makeTargetEntry((Expr *) ybctid_var,
								 resno++,
								 NULL,		/* resname */
								 false);	/* resjunk */
	targetlist = lappend(targetlist, ybctid_tle);

	/* Add columns directly referenced in the index */
	for (i = 0; i < indexInfo->ii_NumIndexAttrs; i++)
	{
		AttrNumber	attnum = indexInfo->ii_IndexAttrNumbers[i];

		/*
		 * attnum == 0 means this is an expression index column,
		 * which will be handled by extracting vars from ii_Expressions.
		 */
		if (attnum > 0)
			ybcAttnumBmsAdd(&required_attrs, attnum);
	}

	/*
	 * Add columns referenced in index expressions.
	 * Use ybcPullVarattnosIntoAttnumBms which handles the min_attr offset.
	 */
	ybcPullVarattnosIntoAttnumBms(indexInfo->ii_Expressions, varno,
								  &required_attrs);

	/*
	 * Add columns referenced in partial index predicate.
	 */
	ybcPullVarattnosIntoAttnumBms(indexInfo->ii_Predicate, varno,
								  &required_attrs);

	/*
	 * Build targetlist with Var nodes for each required column.
	 */
	idx = -1;
	while ((idx = bms_next_member(required_attrs.bms, idx)) >= 0)
	{
		AttrNumber	attnum = ybcAttnumBmsAttnum(&required_attrs, idx);
		Var		   *var;
		TargetEntry *tle;
		Form_pg_attribute attr;

		/*
		 * Only non-system columns should be in required_attrs. ybctid is
		 * handled separately above, and other system columns cannot be indexed.
		 */
		Assert(attnum > 0);

		/*
		 * Verify the column exists and is not dropped. The parser should have
		 * already rejected any attempt to create an index on a dropped or
		 * non-existent column, so this is just a sanity check.
		 */
		Assert(attnum <= tupdesc->natts);
		attr = TupleDescAttr(tupdesc, attnum - 1);
		Assert(!attr->attisdropped);

		var = makeVar(varno,
					  attnum,
					  attr->atttypid,
					  attr->atttypmod,
					  attr->attcollation,
					  0);	/* varlevelsup */

		tle = makeTargetEntry((Expr *) var,
							  resno++,
							  NULL,		/* resname */
							  false);	/* resjunk */
		targetlist = lappend(targetlist, tle);
	}

	ybcAttnumBmsDestroy(&required_attrs);

	/* Create the Scan node */
	scan_plan = makeNode(Scan);
	scan_plan->scanrelid = varno;
	scan_plan->plan.targetlist = targetlist;
	scan_plan->plan.qual = NIL;	/* No quals for backfill scan */

	return scan_plan;
}

/*
 * TODO: move ybc_heap_beginscan into heapam.c so that yb_scan_core.c
 * does not need to depend on yb_table_scan_options.h.  Requires
 * reworking several references to scan-core internals first.
 */
TableScanDesc
ybc_heap_beginscan(Relation relation,
				   Snapshot snapshot,
				   int nkeys,
				   ScanKey keys,
				   uint32 flags,
				   YbTableScanOptions *yb_options)
{
	YbTableScanDesc ybDesc =
		(YbTableScanDesc) palloc0(sizeof(YbTableScanDescData));
	TableScanDesc tsdesc = (TableScanDesc) ybDesc;

	/*
	 * Neutral defaults standing in for an omitted yb_options so that the
	 * rest of the function can read fields unconditionally.  All zeroes
	 * except rowmark, whose "not set" value is -1.
	 */
	YbTableScanOptions no_options = {.rowmark = YBC_NO_ROW_MARK};

	if (yb_options == NULL)
		yb_options = &no_options;

	tsdesc->rs_rd = relation;
	tsdesc->rs_snapshot = snapshot;
	tsdesc->rs_nkeys = nkeys;
	tsdesc->rs_key = keys;
	tsdesc->rs_flags = flags;
	tsdesc->rs_parallel = NULL;

	Scan	   *pg_scan_plan = yb_options->pg_scan_plan;

	/*
	 * When index_info is provided and the column-projection GUC is
	 * enabled, build a projected scan plan that only fetches the
	 * columns needed for the index.  System catalogs are excluded
	 * because their index builds use a different path.
	 */
	if (yb_options->index_info &&
		yb_enable_index_backfill_scan_optimization &&
		!IsSystemRelation(relation))
	{
		Assert(pg_scan_plan == NULL);
		pg_scan_plan = ybcBuildScanPlanForIndexBuild(relation,
													 yb_options->index_info);
	}

	ybDesc->ybscan = YbBeginScan(relation,
								 NULL,	/* index */
								 false, /* xs_want_itup */
								 nkeys,
								 keys,
								 pg_scan_plan,
								 yb_options->rel_pushdown,
								 NULL,	/* idx_pushdown */
								 yb_options->aggrefs,
								 yb_options->distinct_prefixlen,
								 yb_options->exec_params,
								 yb_options->is_internal_scan,
								 yb_options->fetch_ybctids_only);

	Assert(!YbNeedsPgRecheck(ybDesc->ybscan));

	if (yb_options->rowmark != YBC_NO_ROW_MARK)
	{
		YbOpaque	opaque = ybDesc->ybscan;

		opaque->exec_params->rowmark = yb_options->rowmark;
		opaque->exec_params->pg_wait_policy = yb_options->wait_policy;
		opaque->exec_params->docdb_wait_policy =
			YBGetDocDBWaitPolicy(yb_options->wait_policy);
	}

	if (yb_options->pscan)
		ybDesc->ybscan->pscan = yb_options->pscan;

	return tsdesc;
}

HeapTuple
ybc_heap_getnext(TableScanDesc tsdesc)
{
	YbTableScanDesc ybDesc = (YbTableScanDesc) tsdesc;
	HeapTuple	tuple;

	Assert(PointerIsValid(ybDesc->ybscan));
	tuple = ybc_getnext_heaptuple(ybDesc->ybscan, ForwardScanDirection);

	return tuple;
}

bool
ybc_heap_getnextslot(TableScanDesc tsdesc, ScanDirection direction,
					 TupleTableSlot *slot)
{
	YbTableScanDesc ybDesc = (YbTableScanDesc) tsdesc;
	YbOpaque	ybScan = ybDesc->ybscan;

	Assert(PointerIsValid(ybScan));
	if (ybScan->quit_scan)
	{
		ExecClearTuple(slot);
		return false;
	}

	/*
	 * Non-virtual slots (e.g. TTSOpsHeapTuple from table_slot_create)
	 * need the HeapTuple materialization path.  This covers systable
	 * scans, COPY, DDL validation, index builds, etc.
	 */
	if (!TTS_IS_VIRTUAL(slot))
	{
		HeapTuple	tuple = ybc_getnext_heaptuple(ybScan, direction);

		if (tuple)
		{
			ExecStoreHeapTuple(tuple, slot, false);
			return true;
		}
		ExecClearTuple(slot);
		return false;
	}

	/*
	 * Virtual-slot fast path: fill the slot directly from the DocDB
	 * response without materializing a HeapTuple.
	 *
	 * In the case of parallel scan we need to obtain boundaries from the
	 * pscan before the scan is executed. Also empty row from parallel range
	 * scan does not mean scan is done, it means the range is done and we need
	 * to pick up next. No rows from parallel range is possible, hence the
	 * loop.
	 */
	while (true)
	{
		/* Need to execute the request */
		if (!ybScan->is_exec_done)
		{
			/*
			 * Callers (e.g. YbSeqNext) may invoke this callback under a
			 * per-tuple context so that fetched datums are reset every tuple.
			 * Request setup must not allocate there: it happens once per scan
			 * (or parallel range), not once per tuple, so run it in the scan
			 * descriptor's own context, whose lifetime matches the scan by
			 * construction.
			 */
			MemoryContext oldcxt =
				MemoryContextSwitchTo(GetMemoryChunkContext(ybDesc));

			/* Parallel mode: pick up parallel block first */
			if (ybScan->pscan != NULL &&
				!yb_scan_apply_next_parallel_range(ybScan->handle,
												   ybScan->exec_params,
												   ybScan->pscan))
			{
				MemoryContextSwitchTo(oldcxt);
				ExecClearTuple(slot);
				return false;
			}

			/* Set scan direction, if matters */
			if (ScanDirectionIsForward(direction))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, true));
			else if (ScanDirectionIsBackward(direction))
				HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, false));

			HandleYBStatus(YBCPgExecSelect(ybScan->handle,
										   ybScan->exec_params));
			ybScan->is_exec_done = true;
			MemoryContextSwitchTo(oldcxt);
		}

		ybFetchNext(ybScan->handle, slot,
					RelationGetRelid(tsdesc->rs_rd));

		if (!TupIsNull(slot))
			return true;

		/*
		 * No more rows in parallel mode: repeat for next range, else break to
		 * return the result.
		 */
		if (ybScan->pscan != NULL)
		{
			ybScan->is_exec_done = false;
			continue;
		}

		return false;
	}
}

void
ybc_heap_endscan(TableScanDesc tsdesc)
{
	YbTableScanDesc ybDesc = (YbTableScanDesc) tsdesc;

	if (tsdesc->rs_flags & SO_TEMP_SNAPSHOT)
		UnregisterSnapshot(tsdesc->rs_snapshot);
	ybc_free_ybscan(ybDesc->ybscan);
	pfree(ybDesc);
}

/* --------------------------------------------------------------------------------------------- */

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
