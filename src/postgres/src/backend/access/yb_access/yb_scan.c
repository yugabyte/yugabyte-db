/*--------------------------------------------------------------------------------------------------
 *
 * yb_scan.c
 *	  YugaByte catalog scan API.
 *	  This is used to access data from YugaByte's system catalog tables.
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
 * src/backend/access/yb_access/yb_scan.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include <limits.h>
#include <string.h>
#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "commands/dbcommands.h"
#include "commands/tablegroup.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/catalog.h"
#include "catalog/pg_database.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "nodes/relation.h"
#include "optimizer/cost.h"
#include "optimizer/var.h"
#include "utils/datum.h"
#include "utils/elog.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "utils/resowner_private.h"
#include "utils/syscache.h"
#include "utils/selfuncs.h"
#include "utils/snapmgr.h"
#include "utils/spccache.h"
#include "utils/typcache.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "access/nbtree.h"
#include "catalog/yb_type.h"

typedef struct YbScanPlanData
{
	/* The relation where to read data from */
	Relation target_relation;

	/* Primary and hash key columns of the referenced table/relation. */
	Bitmapset *primary_key;
	Bitmapset *hash_key;

	/* Set of key columns whose values will be used for scanning. */
	Bitmapset *sk_cols;

	/* Description and attnums of the columns to bind */
	TupleDesc bind_desc;
	AttrNumber bind_key_attnums[YB_MAX_SCAN_KEYS];
} YbScanPlanData;

typedef YbScanPlanData *YbScanPlan;

static void ybcAddAttributeColumn(YbScanPlan scan_plan, AttrNumber attnum)
{
	const int idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

	if (bms_is_member(idx, scan_plan->primary_key))
		scan_plan->sk_cols = bms_add_member(scan_plan->sk_cols, idx);
}

/*
 * Checks if an attribute is a hash or primary key column and note it in
 * the scan plan.
 */
static void ybcCheckPrimaryKeyAttribute(YbScanPlan      scan_plan,
										YBCPgTableDesc  ybc_table_desc,
										AttrNumber      attnum)
{
	YBCPgColumnInfo column_info = {0};

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

	int idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

	if (column_info.is_hash)
	{
		scan_plan->hash_key = bms_add_member(scan_plan->hash_key, idx);
	}
	if (column_info.is_primary)
	{
		scan_plan->primary_key = bms_add_member(scan_plan->primary_key, idx);
	}
}

/*
 * Get YugaByte-specific table metadata and load it into the scan_plan.
 * Currently only the hash and primary key info.
 */
static void ybcLoadTableInfo(Relation relation, YbScanPlan scan_plan)
{
	Oid            dboid          = YBCGetDatabaseOid(relation);
	YBCPgTableDesc ybc_table_desc = NULL;

	HandleYBStatus(YBCPgGetTableDesc(dboid, YbGetStorageRelid(relation), &ybc_table_desc));

	for (AttrNumber attnum = 1; attnum <= relation->rd_att->natts; attnum++)
	{
		ybcCheckPrimaryKeyAttribute(scan_plan, ybc_table_desc, attnum);
	}
	if (relation->rd_rel->relhasoids)
	{
		ybcCheckPrimaryKeyAttribute(scan_plan, ybc_table_desc, ObjectIdAttributeNumber);
	}
}

static Oid ybc_get_atttypid(TupleDesc bind_desc, AttrNumber attnum)
{
	Oid	atttypid;

	if (attnum > 0)
	{
		/* Get the type from the description */
		atttypid = TupleDescAttr(bind_desc, attnum - 1)->atttypid;
	}
	else
	{
		/* This must be an OID column. */
		atttypid = OIDOID;
	}

  return atttypid;
}

/*
 * Bind a scan key.
 */
static void
YbBindColumn(YbScanDesc ybScan, TupleDesc bind_desc,
             AttrNumber attnum, Datum value, bool is_null)
{
	Oid	atttypid = ybc_get_atttypid(bind_desc, attnum);
	Oid	attcollation = YBEncodingCollation(ybScan->handle, attnum,
										   ybc_get_attcollation(bind_desc, attnum));

	YBCPgExpr ybc_expr = YBCNewConstant(ybScan->handle, atttypid, attcollation, value, is_null);

	HandleYBStatus(YBCPgDmlBindColumn(ybScan->handle, attnum, ybc_expr));
}

static void
YbBindColumnCondBetween(YbScanDesc ybScan,
                        TupleDesc bind_desc, AttrNumber attnum,
                        bool start_valid, bool start_inclusive, Datum value,
                        bool end_valid, bool end_inclusive, Datum value_end)
{
	Oid	atttypid = ybc_get_atttypid(bind_desc, attnum);
	Oid	attcollation = YBEncodingCollation(ybScan->handle, attnum,
										   ybc_get_attcollation(bind_desc, attnum));

	YBCPgExpr ybc_expr = start_valid ? YBCNewConstant(ybScan->handle,
													  atttypid,
													  attcollation,
													  value,
													  false /* isnull */)
									 : NULL;
	YBCPgExpr ybc_expr_end = end_valid ? YBCNewConstant(ybScan->handle,
														atttypid,
														attcollation,
														value_end,
														false /* isnull */)
									   : NULL;

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
static void ybcBindColumnCondIn(YbScanDesc ybScan, TupleDesc bind_desc, AttrNumber attnum,
                                int nvalues, Datum *values)
{
	Oid	atttypid = ybc_get_atttypid(bind_desc, attnum);
	Oid	attcollation = YBEncodingCollation(ybScan->handle, attnum,
										   ybc_get_attcollation(bind_desc, attnum));

	YBCPgExpr colref =
		YBCNewColumnRef(ybScan->handle, attnum, atttypid, attcollation, NULL);

	YBCPgExpr ybc_exprs[nvalues]; /* VLA - scratch space */
	for (int i = 0; i < nvalues; i++) {
		/*
		 * For IN we are removing all null values in ybcBindScanKeys before
		 * getting here (relying on btree/lsm operators being strict).
		 * So we can safely set is_null to false for all options left here.
		 */
		ybc_exprs[i] = YBCNewConstant(ybScan->handle, atttypid, attcollation,
									  values[i], false /* is_null */);
	}

	HandleYBStatus(
		YBCPgDmlBindColumnCondIn(ybScan->handle, colref, nvalues, ybc_exprs));
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
	YBCPgExpr ybc_rhs_exprs[nvalues];

	YBCPgExpr ybc_elems_exprs[n_attnum_values];	/* VLA - scratch space */
	Datum datum_values[n_attnum_values];
	bool is_null[n_attnum_values];

	Oid tupType =
		HeapTupleHeaderGetTypeId(DatumGetHeapTupleHeader(values[0]));
	Oid tupTypmod =
		HeapTupleHeaderGetTypMod(DatumGetHeapTupleHeader(values[0]));
	YBCPgTypeAttrs type_attrs = { tupTypmod };

	/* Form the lhs tuple. */
	for (int i = 0; i < n_attnum_values; i++)
	{
		Oid	atttypid = ybc_get_atttypid(bind_desc, attnum[i]);
		Oid	attcollation = YBEncodingCollation(ybScan->handle, attnum[i],
										   	   ybc_get_attcollation(bind_desc, attnum[i]));
		ybc_elems_exprs[i] =
			YBCNewColumnRef(ybScan->handle, attnum[i], atttypid,
							attcollation, NULL);
	}

	YBCPgExpr lhs =
		YBCNewTupleExpr(ybScan->handle, &type_attrs, n_attnum_values,
						ybc_elems_exprs);

	TupleDesc tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);

	HeapTupleData tuple;
	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;

	/* Form the list of tuples for the RHS. */
	for (int i = 0; i < nvalues; i++)
	{
		tuple.t_len = HeapTupleHeaderGetDatumLength(values[i]);
		tuple.t_data = DatumGetHeapTupleHeader(values[i]);
		heap_deform_tuple(&tuple, tupdesc,
						  datum_values, is_null);
		for (int j = 0; j < n_attnum_values; j++)
		{
			Oid atttypid = ybc_get_atttypid(bind_desc, attnum[j]);
			Oid	attcollation =
				YBEncodingCollation(ybScan->handle, attnum[j],
									ybc_get_attcollation(bind_desc, attnum[j]));
			ybc_elems_exprs[j] =
				YBCNewConstant(ybScan->handle, atttypid, attcollation,
							   datum_values[j], is_null[j]);
		}

		ybc_rhs_exprs[i] =
			YBCNewTupleExpr(ybScan->handle, &type_attrs, n_attnum_values,
							ybc_elems_exprs);
	}

	HandleYBStatus(
		YBCPgDmlBindColumnCondIn(ybScan->handle, lhs, nvalues,
								 ybc_rhs_exprs));

	ReleaseTupleDesc(tupdesc);
}

/*
 * Add a system column as target to the given statement handle.
 */
void
YbDmlAppendTargetSystem(AttrNumber attnum, YBCPgStatement handle)
{
	Assert(attnum < 0);

	YBCPgExpr	expr;
	YBCPgTypeAttrs type_attrs;

	/* System columns don't use typmod. */
	type_attrs.typmod = -1;

	expr = YBCNewColumnRef(handle,
						   attnum,
						   InvalidOid /* attr_typid */,
						   InvalidOid /* attr_collation */,
						   &type_attrs);
	HandleYBStatus(YBCPgDmlAppendTarget(handle, expr));
}

/*
 * Add a regular column as target to the given statement handle.  Assume
 * tupdesc's relation is the same as handle's target relation.
 */
void
YbDmlAppendTargetRegular(TupleDesc tupdesc, AttrNumber attnum, YBCPgStatement handle)
{
	Assert(attnum > 0);

	Form_pg_attribute att;
	YBCPgExpr	expr;
	YBCPgTypeAttrs type_attrs;

	att = TupleDescAttr(tupdesc, attnum - 1);
	/* Should not be given dropped attributes. */
	Assert(!att->attisdropped);
	type_attrs.typmod = att->atttypmod;
	expr = YBCNewColumnRef(handle,
						   attnum,
						   att->atttypid,
						   att->attcollation,
						   &type_attrs);
	HandleYBStatus(YBCPgDmlAppendTarget(handle, expr));
}

static void ybcUpdateFKCache(YbScanDesc ybScan, Datum ybctid)
{
	if (!ybScan->exec_params)
		return;

	switch (ybScan->exec_params->rowmark) {
	case ROW_MARK_EXCLUSIVE:
	case ROW_MARK_NOKEYEXCLUSIVE:
	case ROW_MARK_SHARE:
	case ROW_MARK_KEYSHARE:
		YBCPgAddIntoForeignKeyReferenceCache(RelationGetRelid(ybScan->relation), ybctid);
		break;
	case ROW_MARK_REFERENCE:
	case ROW_MARK_COPY:
		break;
	}
}

static HeapTuple ybcFetchNextHeapTuple(YbScanDesc ybScan, bool is_forward_scan)
{
	HeapTuple tuple    = NULL;
	bool      has_data = false;
	TupleDesc tupdesc  = ybScan->target_desc;

	Datum           *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool            *nulls  = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Execute the select statement. */
	if (!ybScan->is_exec_done)
	{
		HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, is_forward_scan));
		HandleYBStatus(YBCPgExecSelect(ybScan->handle, ybScan->exec_params));
		ybScan->is_exec_done = true;
	}

	/* Fetch one row. */
	YBCStatus status = YBCPgDmlFetch(ybScan->handle,
									 tupdesc->natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data);

	if (IsolationIsSerializable())
		HandleYBStatus(status);
	else if (status)
	{
		if (ybScan->exec_params != NULL && YBCIsTxnConflictError(YBCStatusTransactionError(status)))
		{
			elog(DEBUG2, "Error when trying to lock row. "
				 "pg_wait_policy=%d docdb_wait_policy=%d txn_errcode=%d message=%s",
				 ybScan->exec_params->pg_wait_policy,
				 ybScan->exec_params->docdb_wait_policy,
				 YBCStatusTransactionError(status),
				 YBCStatusMessageBegin(status));
			if (ybScan->exec_params->pg_wait_policy == LockWaitError)
				ereport(ERROR, (errcode(ERRCODE_LOCK_NOT_AVAILABLE),
								errmsg("could not obtain lock on row in relation \"%s\"",
									   RelationGetRelationName(ybScan->relation))));
			else
				ereport(ERROR,
						(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
						 errmsg("could not serialize access due to concurrent update"),
						 yb_txn_errcode(YBCGetTxnConflictErrorCode())));
		}
		else if (YBCIsTxnSkipLockingError(YBCStatusTransactionError(status)))
			/* For skip locking, it's correct to simply return no results. */
			has_data = false;
		else
			HandleYBStatus(status);
	}

	if (has_data)
	{
		tuple = heap_form_tuple(tupdesc, values, nulls);

		if (syscols.oid != InvalidOid)
		{
			HeapTupleSetOid(tuple, syscols.oid);
		}
		if (syscols.ybctid != NULL)
		{
			tuple->t_ybctid = PointerGetDatum(syscols.ybctid);
			ybcUpdateFKCache(ybScan, tuple->t_ybctid);
		}
		tuple->t_tableOid = RelationGetRelid(ybScan->relation);
	}
	pfree(values);
	pfree(nulls);

	return tuple;
}

static IndexTuple
ybcFetchNextIndexTuple(YbScanDesc ybScan, bool is_forward_scan)
{
	IndexTuple tuple    = NULL;
	bool       has_data = false;
	Relation   index    = ybScan->index;
	TupleDesc  tupdesc  = ybScan->target_desc;

	Datum           *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool            *nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Execute the select statement. */
	if (!ybScan->is_exec_done)
	{
		HandleYBStatus(YBCPgSetForwardScan(ybScan->handle, is_forward_scan));
		HandleYBStatus(YBCPgExecSelect(ybScan->handle, ybScan->exec_params));
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
		 * Return the IndexTuple. If this is a primary key, reorder the values first as expected
		 * in the index's column order first.
		 */
		if (index->rd_index->indisprimary)
		{
			Assert(index->rd_index->indnatts <= INDEX_MAX_KEYS);

			Datum ivalues[INDEX_MAX_KEYS];
			bool  inulls[INDEX_MAX_KEYS];

			for (int i = 0; i < index->rd_index->indnatts; i++)
			{
				AttrNumber attno = index->rd_index->indkey.values[i];
				ivalues[i] = values[attno - 1];
				inulls[i]  = nulls[attno - 1];
			}

			tuple = index_form_tuple(RelationGetDescr(index), ivalues, inulls);
			if (syscols.ybctid != NULL)
			{
				tuple->t_ybctid = PointerGetDatum(syscols.ybctid);
				ybcUpdateFKCache(ybScan, tuple->t_ybctid);
			}
		}
		else
		{
			tuple = index_form_tuple(tupdesc, values, nulls);
			if (syscols.ybbasectid != NULL)
			{
				tuple->t_ybctid = PointerGetDatum(syscols.ybbasectid);
				ybcUpdateFKCache(ybScan, tuple->t_ybctid);
			}
		}

	}
	pfree(values);
	pfree(nulls);

	return tuple;
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
 */
static void
ybcSetupScanPlan(bool xs_want_itup, YbScanDesc ybScan, YbScanPlan scan_plan)
{
	Relation relation = ybScan->relation;
	Relation index = ybScan->index;
	int i;
	memset(scan_plan, 0, sizeof(*scan_plan));

	/*
	 * Setup control-parameters for Yugabyte preparing statements for different
	 * types of scan.
	 * - "querying_colocated_table": Support optimizations for (system,
	 *   user database and tablegroup) colocated tables
	 * - "index_oid, index_only_scan, use_secondary_index": Different index
	 *   scans.
	 * NOTE: Primary index is a special case as there isn't a primary index
	 * table in YugaByte.
	 */

	ybScan->prepare_params.querying_colocated_table =
		IsSystemRelation(relation) ||
		YbGetTableProperties(relation)->is_colocated;

	if (index)
	{
		ybScan->prepare_params.index_oid = RelationGetRelid(index);
		ybScan->prepare_params.index_only_scan = xs_want_itup;
		ybScan->prepare_params.use_secondary_index = !index->rd_index->indisprimary;
	}

	/* Setup descriptors for target and bind. */
	if (!index || index->rd_index->indisprimary)
	{
		/*
		 * SequentialScan or PrimaryIndexScan
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

		if (ybScan->prepare_params.index_only_scan)
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
		ScanKey key = ybScan->keys[i];
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
			ybScan->target_key_attnums[i] =	scan_plan->bind_key_attnums[i] =
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

static bool ybc_should_pushdown_op(YbScanPlan scan_plan, AttrNumber attnum, int op_strategy)
{
	const int idx =  YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

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
	bool is_hash_search = (key->sk_flags & YB_SK_IS_HASHED) != 0;

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
	return key->sk_flags == 0 ||
	       key->sk_flags == SK_ISNULL ||
	       YbIsHashCodeSearch(key);
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

/*
 * Is the condition never TRUE because of c {=|<|<=|>=|>} NULL, etc.?
 */
static bool
YbIsNeverTrueNullCond(ScanKey key)
{
	return (key->sk_flags & SK_ISNULL) != 0 &&
	       (key->sk_flags & (SK_SEARCHNULL | SK_SEARCHNOTNULL)) == 0;
}

static int
YbGetLengthOfKey(ScanKey *key_ptr)
{
	if (!YbIsRowHeader(key_ptr[0]))
		return 1;

	int length_of_key = 0;
	while(!(key_ptr[length_of_key]->sk_flags & SK_ROW_END))
	{
		length_of_key++;
	}

	/* We also want to include the last element. */
	length_of_key++;
	return length_of_key;
}

/*
 * Given a table attribute number, get a corresponding index attribute number.
 * Throw an error if it is not found.
 */
static AttrNumber
YbGetIndexAttnum(AttrNumber table_attno, Relation index)
{
	for (int i = 0; i < IndexRelationGetNumberOfAttributes(index); ++i)
	{
		if (table_attno == index->rd_index->indkey.values[i])
			return i + 1;
	}
	elog(ERROR, "column is not in index");
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
		ScanKey key = &keys[i];

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
				ScanKey subkey = (ScanKey) key->sk_argument;
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
		ScanKey key = keys[i];

		/*
		 * Look for two cases:
		 * - op null
		 * - row(a, b, c) op row(null, e, f)
		 */
		if (((key->sk_strategy != InvalidStrategy &&
			  (key->sk_flags & SK_ROW_MEMBER) == 0)||
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
	{
		return true;
	}

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
	/* No other operators are supported. */
	return false;
}

/* int comparator for qsort() */
static int int_compar_cb(const void *v1, const void *v2)
{
  const int *k1 = v1;
  const int *k2 = v2;

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
	for (int i = 0; i < ybScan->nkeys; i++)
	{
		const AttrNumber attnum = scan_plan->bind_key_attnums[i];
		if (attnum == InvalidAttrNumber)
			break;

		int idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);
		/*
		 * TODO: Can we have bound keys on non-pkey columns here?
		 *       If not we do not need the is_primary_key below.
		 */
		bool is_primary_key = bms_is_member(idx, scan_plan->primary_key);

		if (is_primary_key &&
			YbShouldPushdownScanPrimaryKey(scan_plan, attnum, ybScan->keys[i]))
		{
			scan_plan->sk_cols = bms_add_member(scan_plan->sk_cols, idx);
		}
	}

	/*
	 * If hash key is not fully set, we must do a full-table scan so clear all
	 * the scan keys if the hash code was explicitly specified as a
	 * scan key then we also shouldn't be clearing the scan keys
	 */
	if (ybScan->hash_code_keys == NIL &&
		!bms_is_subset(scan_plan->hash_key, scan_plan->sk_cols))
	{
		bms_free(scan_plan->sk_cols);
		scan_plan->sk_cols = NULL;
	}
}

/* Return true if typid is one of the Object Identifier Types */
static bool YbIsOidType(Oid typid) {
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

static bool YbIsIntegerInRange(Datum value, Oid value_typid, int min, int max) {
	int64 val;
	switch (value_typid)
	{
		case INT2OID:
			val = (int64) DatumGetInt16(value);
			break;
		case INT4OID:
			val = (int64) DatumGetInt32(value);
			break;
		case INT8OID:
			val = DatumGetInt64(value);
			break;
		default:
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("not an integer type")));
	}
	return val >= min && val <= max;
}

/*
 * Return true if a scan key column type is compatible with value type. 'equal_strategy' is true
 * for BTEqualStrategyNumber.
 */
static bool YbIsScanCompatible(Oid column_typid,
							   Oid value_typid,
							   bool equal_strategy,
							   Datum value) {
	if (column_typid == value_typid)
		return true;

	switch (column_typid)
	{
		case INT2OID:

			/*
			 * If column c0 has INT2OID type and value type is INT4OID, the value may overflow
			 * INT2OID. For example, where clause condition "c0 = 65539" would become "c0 = 3"
			 * and will unnecessarily fetch a row with key of 3. This will not affect correctness
			 * because at upper Postgres layer filtering will be subsequently applied for equality/
			 * inequality conditions. For example, "c0 = 65539" will be applied again to filter out
			 * this row. We prefer to bind scan key c0 to account for the common case where INT4OID
			 * value does not overflow INT2OID, which happens in some system relation scan queries.
			 *
			 * For this purpose, specifically for inequalities, we return true when we are sure that
			 * there isn't a data overflow. For instance, if column c0 has INT2OID and value type is
			 * INT4OID, and its an inequality strategy, we check if the actual value is within the
			 * bounds of INT2OID. If yes, then we return true, otherwise false.
			 */
			return equal_strategy ? (value_typid == INT4OID || value_typid == INT8OID) :
				   YbIsIntegerInRange(value, value_typid, SHRT_MIN, SHRT_MAX);
		case INT4OID:
			return equal_strategy ? (value_typid == INT2OID || value_typid == INT8OID) :
				   YbIsIntegerInRange(value, value_typid, INT_MIN, INT_MAX);
		case INT8OID:
			return value_typid == INT2OID || value_typid == INT4OID;

		case TEXTOID:
		case BPCHAROID:
		case VARCHAROID:
			return value_typid == TEXTOID || value_typid == BPCHAROID || value_typid == VARCHAROID;

		default:
			if (YbIsOidType(column_typid) && YbIsOidType(value_typid))
				return true;
			/* Conservatively return false. */
			return false;
	}
}

/*
 * We require compatible column type and value type to avoid misinterpreting the value Datum
 * using a column type that can cause wrong scan results. Returns true if the column type
 * and value type are compatible.
 */
static bool
YbCheckScanTypes(YbScanDesc ybScan, YbScanPlan scan_plan, int i)
{
	ScanKey key = ybScan->keys[i];
	Oid valtypid = key->sk_subtype;
	Oid atttypid = valtypid == RECORDOID ? RECORDOID :
		ybc_get_atttypid(scan_plan->bind_desc, scan_plan->bind_key_attnums[i]);
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
	return !OidIsValid(valtypid) ||
	       key->sk_strategy == InvalidStrategy ||
	       YbIsScanCompatible(atttypid, valtypid,
	                          key->sk_strategy == BTEqualStrategyNumber,
	                          key->sk_argument) ||
	       IsPolymorphicType(valtypid);
}

static bool
YbBindRowComparisonKeys(YbScanDesc ybScan, YbScanPlan scan_plan,
								int skey_index, bool is_for_precheck)
{
	int last_att_no = YBFirstLowInvalidAttributeNumber;
	Relation index = ybScan->index;
	int length_of_key = YbGetLengthOfKey(&ybScan->keys[skey_index]);

	ScanKey header_key = ybScan->keys[skey_index];

	ScanKey *subkeys = &ybScan->keys[skey_index + 1];

	/*
	 * We can only push down right now if the primary key columns
	 * are specified in the correct order and the primary key
	 * has no hashed columns. We also need to ensure that
	 * the same comparison operation is done to all subkeys.
	 */
	bool can_pushdown = true;

	int strategy = header_key->sk_strategy;
	int subkey_count = length_of_key - 1;
	for (int j = 0; j < subkey_count; j++)
	{
		ScanKey key = subkeys[j];

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
		{
			can_pushdown = false;
		}
	}

	bool needs_recheck = !can_pushdown;

	if (can_pushdown)
	{

		YBCPgExpr *col_values =
			palloc(sizeof(YBCPgExpr) * index->rd_index->indnkeyatts);
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
		bool is_direction_asc =
			!(index->rd_indoption[subkeys[0]->sk_attno - 1] & INDOPTION_DESC);

		bool gt =
			strategy == BTGreaterEqualStrategyNumber ||
			strategy == BTGreaterStrategyNumber;

		bool is_inclusive =
			strategy != BTGreaterStrategyNumber &&
			strategy != BTLessStrategyNumber;

		bool is_point_scan =
			(subkey_count == index->rd_index->indnatts) &&
			(strategy == BTEqualStrategyNumber);

		/* Whether or not the RHS values make up a DocDB upper bound */
		bool is_upper_bound = gt ^ is_direction_asc;
		size_t subkey_index = 0;

		for (int j = 0; j < index->rd_index->indnkeyatts; j++)
		{
			bool is_column_specified =
				subkey_index < subkey_count &&
				(subkeys[subkey_index]->sk_attno - 1) == j;
			/*
			 * Is the current column stored in ascending order in the
			 * underlying index?
			 */
			bool asc = (index->rd_indoption[j] & INDOPTION_DESC) == 0;

			/*
			 * If this column has different directionality than the
			 * first column then we have to adjust the bounds on this
			 * column.
			 */
			if(!is_column_specified ||
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
				ScanKey current = subkeys[subkey_index];
				col_values[j] =
					YBCNewConstant(
						ybScan->handle,
						ybc_get_atttypid(scan_plan->bind_desc,
										 current->sk_attno),
						current->sk_collation,
						current->sk_argument,
						false);
			}

			if (is_column_specified)
			{
				subkey_index++;
			}
		}

		if (is_for_precheck)
			return needs_recheck;

		if (is_upper_bound || strategy == BTEqualStrategyNumber)
		{
			HandleYBStatus(
				YBCPgDmlAddRowUpperBound(ybScan->handle,
										 index->rd_index->indnkeyatts,
										 col_values,
										 is_inclusive));
		}

		if (!is_upper_bound || strategy == BTEqualStrategyNumber)
		{
			HandleYBStatus(
				YBCPgDmlAddRowLowerBound(ybScan->handle,
										 index->rd_index->indnkeyatts,
										 col_values,
										 is_inclusive));
		}
	}

	return needs_recheck;
}

/*
 * is_for_precheck signifies that the caller only wants to use this for
 * predetermine-recheck purposes, so don't actually do binds but still
 * calculate all_ordinary_keys_bound.
 * TODO(jason): do a proper cleanup.
 */
static bool
YbBindSearchArray(YbScanDesc ybScan, YbScanPlan scan_plan,
				  int skey_index, bool is_for_precheck, bool is_column_bound[],
				  bool *bail_out)
{
	/* based on _bt_preprocess_array_keys() */
	ArrayType  *arrayval;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	int			num_elems;
	Datum	   *elem_values;
	bool	   *elem_nulls;
	int			num_valid;
	int			j;
	bool is_row = false;
	int length_of_key = YbGetLengthOfKey(&ybScan->keys[skey_index]);
	Relation relation = ybScan->relation;
	Relation index = ybScan->index;

	ScanKey key = ybScan->keys[skey_index];
	int i = skey_index;
	*bail_out = false;

	/*
	 * First, deconstruct the array into elements.
	 * Anything allocated here (including a possibly detoasted
	 * array value) is in the workspace context.
	 */
	if (YbIsRowHeader(key))
	{
		is_row = true;
		for(int row_ind = 1; row_ind < length_of_key; row_ind++)
		{
			int bound_idx = YBAttnumToBmsIndex(
				relation, scan_plan->bind_key_attnums[i + row_ind]);
			if (is_column_bound[bound_idx])
				return false;
			else
				is_column_bound[bound_idx] = true;
		}
	}

	if (is_for_precheck)
		return true;

	if (is_row)
		arrayval = DatumGetArrayTypeP((ybScan->keys[i+1])->sk_argument);
	else
		arrayval = DatumGetArrayTypeP(key->sk_argument);

	Assert(key->sk_subtype == ARR_ELEMTYPE(arrayval));
	/* We could cache this data, but not clear it's worth it */
	get_typlenbyvalalign(ARR_ELEMTYPE(arrayval), &elmlen,
							&elmbyval, &elmalign);

	deconstruct_array(arrayval,
					  ARR_ELEMTYPE(arrayval),
					  elmlen, elmbyval, elmalign,
					  &elem_values, &elem_nulls, &num_elems);
	Assert(ARR_NDIM(arrayval) <= 2);

	/*
	 * Compress out any null elements.  We can ignore them since we assume
	 * all btree operators are strict.
	 * Also remove elements that are too large or too small.
	 * eg. WHERE element = INT_MAX + k, where k is positive and element
	 * is of integer type.
	 */
	Oid atttype = ybc_get_atttypid(scan_plan->bind_desc,
								   scan_plan->bind_key_attnums[i]);

	num_valid = 0;
	for (j = 0; j < num_elems; j++)
	{
		if (elem_nulls[j])
			continue;

		/* Skip integer element where the value overflows the column type */
		if (!is_row && (atttype == INT2OID || atttype == INT4OID) &&
			!YbIsIntegerInRange(elem_values[j], ybScan->keys[i]->sk_subtype,
								atttype == INT2OID ? SHRT_MIN : INT_MIN,
								atttype == INT2OID ? SHRT_MAX : INT_MAX))
			continue;

		/* Skip any rows that have NULLs in them. */
		/*
		 * TODO: record_eq considers NULL record elements to
		 * be equal. However, the only way we receive IN filters
		 * with tuples is through
		 * compound batched nested loop joins where NULL
		 * elements of batched record are not considered equal.
		 * This needs to be rechecked when row IN filters can
		 * arise through other means.
		 */
		if ((!is_row && !elem_nulls[j])
			|| (is_row &&
				!HeapTupleHeaderHasNulls(
					DatumGetHeapTupleHeader(elem_values[j]))))
			elem_values[num_valid++] = elem_values[j];
	}

	pfree(elem_nulls);

	/*
	 * If there's no non-nulls, the scan qual is unsatisfiable
	 * Example: SELECT ... FROM ... WHERE h = ... AND r IN (NULL,NULL);
	 */
	if (num_valid == 0)
	{
		*bail_out = true;
		pfree(elem_values);
		return false;
	}

	/* Build temporary vars */
	IndexScanDescData tmp_scan_desc;
	memset(&tmp_scan_desc, 0, sizeof(IndexScanDescData));
	tmp_scan_desc.indexRelation = index;

	/*
	 * Sort the non-null elements and eliminate any duplicates.  We must
	 * sort in the same ordering used by the index column, so that the
	 * successive primitive indexscans produce data in index order.
	 */
	num_elems = _bt_sort_array_elements(&tmp_scan_desc, key,
										false /* reverse */,
										elem_values, num_valid);

	/*
	 * And set up the BTArrayKeyInfo data.
	 */

	if (is_row)
	{
		AttrNumber attnums[length_of_key];
		/* Subkeys for this rowkey start at i+1. */
		for (int j = 1; j <= length_of_key; j++)
		{
			attnums[j - 1] = scan_plan->bind_key_attnums[i + j];
		}

		ybcBindTupleExprCondIn(ybScan, scan_plan->bind_desc,
								length_of_key - 1, attnums,
								num_elems, elem_values);
	}
	else
	{
		ybcBindColumnCondIn(ybScan, scan_plan->bind_desc,
							scan_plan->bind_key_attnums[i],
							num_elems, elem_values);
	}

	pfree(elem_values);

	return true;
}

/*
 * Use the scan-descriptor and scan-plan to setup binds for the queryplan.
 * is_for_precheck signifies that the caller only wants to use this for
 * predetermine-recheck purposes, so don't actually do binds but still
 * calculate all_ordinary_keys_bound.
 * TODO(jason): do a proper cleanup.
 */
static bool
YbBindScanKeys(YbScanDesc ybScan, YbScanPlan scan_plan, bool is_for_precheck)
{
	Relation relation = ybScan->relation;
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
	int max_idx = 0;
	for (int i = 0; i < ybScan->nkeys; i++)
	{
		int idx = YBAttnumToBmsIndex(relation, scan_plan->bind_key_attnums[i]);
		if (max_idx < idx && bms_is_member(idx, scan_plan->sk_cols))
			max_idx = idx;
	}
	max_idx++;

	/* Find intervals for columns */

	bool is_column_bound[max_idx]; /* VLA - scratch space */
	memset(is_column_bound, 0, sizeof(bool) * max_idx);

	bool start_valid[max_idx]; /* VLA - scratch space */
	memset(start_valid, 0, sizeof(bool) * max_idx);

	bool end_valid[max_idx]; /* VLA - scratch space */
	memset(end_valid, 0, sizeof(bool) * max_idx);

	bool is_not_null[max_idx]; /* VLA - scratch space */
	memset(is_not_null, 0, sizeof(bool) * max_idx);

	Datum start[max_idx]; /* VLA - scratch space */
	Datum end[max_idx]; /* VLA - scratch space */

	bool start_inclusive[max_idx];	/* VLA - scratch space */
	memset(start_inclusive, 0, sizeof(bool) * max_idx);

	bool end_inclusive[max_idx];	/* VLA - scratch space */
	memset(end_inclusive, 0, sizeof(bool) * max_idx);

	/*
	 * find an order of relevant keys such that for the same column, an EQUAL
	 * condition is encountered before IN or BETWEEN. is_column_bound is then
	 * used to establish priority order EQUAL > IN > BETWEEN. IS NOT NULL is
	 * treated as a special case of BETWEEN.
	 */
	int noffsets = 0;
	int offsets[ybScan->nkeys + 1]; /* VLA - scratch space: +1 to avoid zero elements */
	int length_of_key = 0;

	for (int i = 0; i < ybScan->nkeys; i += length_of_key)
	{
		length_of_key = YbGetLengthOfKey(&ybScan->keys[i]);
		ScanKey key = ybScan->keys[i];
		/* Check if this is full key row comparison expression */
		if (YbIsRowHeader(key) &&
			!YbIsSearchArray(key))
		{
			bool needs_recheck =
				YbBindRowComparisonKeys(ybScan, scan_plan, i, is_for_precheck);
			ybScan->all_ordinary_keys_bound &= !needs_recheck;
			/*
			 * Full primary-key RowComparison bindings don't interact
			 * or interfere with other bindings to the same columns. They
			 * just set the upper/lower bounds of the requested scan. We
			 * can just continue looking at the next keys without recording this
			 * key in the offsets array below.
			 */
			continue;
		}

		/* Check if this is primary columns */
		int bind_key_attnum = scan_plan->bind_key_attnums[i];
		int idx = YBAttnumToBmsIndex(relation, bind_key_attnum);
		if (!bms_is_member(idx, scan_plan->sk_cols))
		{
			ybScan->all_ordinary_keys_bound = false;
			continue;
		}

		/* Assign key offsets */
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
				switch_fallthrough();
			case BTEqualStrategyNumber:
				if (YbIsBasicOpSearch(key) || YbIsSearchNull(key))
				{
					/* Use a -ve value so that qsort places EQUAL before others */
					offsets[noffsets++] = -i;
				}
				else if (YbIsSearchArray(key))
				{
					/* Row IN expressions take priority over all. */
					offsets[noffsets++] =
						length_of_key > 1 ? - (i + ybScan->nkeys) : i;
				}
				break;
			case BTGreaterEqualStrategyNumber:
			case BTGreaterStrategyNumber:
			case BTLessStrategyNumber:
			case BTLessEqualStrategyNumber:
				offsets[noffsets++] = i;
				switch_fallthrough();

			default:
				break; /* unreachable */
		}
	}

	qsort(offsets, noffsets, sizeof(int), int_compar_cb);
	/* restore -ve offsets to +ve */
	for (int i = 0; i < noffsets; i++)
		if (offsets[i] < 0)
			offsets[i] = (-offsets[i]) % (ybScan->nkeys);
		else
			break;

	/* Bind keys for EQUALS and IN, collecting info for ranges and IS NOT NULL
	 */
	for (int k = 0; k < noffsets; k++)
	{
		int i = offsets[k];
		ScanKey key = ybScan->keys[i];
		int idx = YBAttnumToBmsIndex(relation, scan_plan->bind_key_attnums[i]);
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

		bool bound_inclusive = false;
		switch (key->sk_strategy)
		{
			case InvalidStrategy:
				if (YbIsSearchNotNull(key))
				{
					is_not_null[idx] = true;
					break;
				}
				/* Otherwise this is an IS NULL search. c IS NULL -> c = NULL
				 * (checked above) */
				switch_fallthrough();
			case BTEqualStrategyNumber:
				/* Bind the scan keys */
				if (YbIsBasicOpSearch(key) || YbIsSearchNull(key))
				{
					/* Either c = NULL or c IS NULL. */
					if (!is_for_precheck)
					{
						bool is_null = (key->sk_flags & SK_ISNULL) == SK_ISNULL;
						YbBindColumn(ybScan, scan_plan->bind_desc,
									 scan_plan->bind_key_attnums[i],
									 key->sk_argument, is_null);
					}
					is_column_bound[idx] = true;
				}
				else if (YbIsSearchArray(key))
				{
					bool bail_out = false;
					bool is_bound = YbBindSearchArray(ybScan, scan_plan, i,
													  is_for_precheck,
													  is_column_bound,
													  &bail_out);
					if (bail_out)
						return false;

					is_column_bound[idx] |= is_bound;
				}
				break;

			case BTGreaterEqualStrategyNumber:
				bound_inclusive = true;
				switch_fallthrough();
			case BTGreaterStrategyNumber:
				if (start_valid[idx])
				{
					/* take max of old value and new value */
					bool is_gt = DatumGetBool(FunctionCall2Coll(
						&key->sk_func, key->sk_collation, start[idx], key->sk_argument));
					if (!is_gt)
					{
						start[idx] = key->sk_argument;
						start_inclusive[idx] = bound_inclusive;
					}
				}
				else
				{
					start[idx] = key->sk_argument;
					start_inclusive[idx] = bound_inclusive;
					start_valid[idx] = true;
				}
				break;

			case BTLessEqualStrategyNumber:
				bound_inclusive = true;
				switch_fallthrough();
			case BTLessStrategyNumber:
				if (end_valid[idx])
				{
					/* take min of old value and new value */
					bool is_lt = DatumGetBool(FunctionCall2Coll(
						&key->sk_func, key->sk_collation, end[idx], key->sk_argument));
					if (!is_lt)
					{
						end[idx] = key->sk_argument;
						end_inclusive[idx] = bound_inclusive;
					}
				}
				else
				{
					end[idx] = key->sk_argument;
					end_inclusive[idx] = bound_inclusive;
					end_valid[idx] = true;
				}
				break;

			default:
				break; /* unreachable */
		}
	}

	/* Bind keys for BETWEEN and IS NOT NULL */
	int min_idx = bms_first_member(scan_plan->sk_cols);
	min_idx = min_idx < 0 ? 0 : min_idx;
	for (int idx = min_idx; idx < max_idx; idx++)
	{
		/* There's no range key or IS NOT NULL for this query */
		if (!start_valid[idx] && !end_valid[idx] && !is_not_null[idx])
		{
			continue;
		}

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
 * Before beginning execution, determine whether recheck is needed.  Use as
 * little resources as possible to make this determination.  This is largely a
 * dup of ybcBeginScan minus the unessential parts.
 * TODO(jason): there may be room for further cleanup/optimization.
 */
bool
YbPredetermineNeedsRecheck(Relation relation,
						   Relation index,
						   bool xs_want_itup,
						   ScanKey keys,
						   int nkeys)
{
	YbScanDescData ybscan;
	memset(&ybscan, 0, sizeof(YbScanDescData));

	ybExtractScanKeys(keys, nkeys, &ybscan);

	if (YbIsUnsatisfiableCondition(ybscan.nkeys, ybscan.keys))
		return false;

	ybscan.relation = relation;
	ybscan.index = index;

	/* Set up the scan plan */
	YbScanPlanData scan_plan;
	ybcSetupScanPlan(xs_want_itup, &ybscan, &scan_plan);
	ybcSetupScanKeys(&ybscan, &scan_plan);

	YbBindScanKeys(&ybscan, &scan_plan, true /* is_for_precheck */);

	/*
	 * Finally, ybscan has everything needed to determine recheck.  Do it now.
	 */
	return YbNeedsRecheck(&ybscan);
}

typedef struct {
	YBCPgBoundType type;
	uint64_t value;
} YbBound;

typedef struct {
	YbBound start;
	YbBound end;
} YbRange;

static inline bool
YbBoundValid(const YbBound* bound)
{
	return bound->type != YB_YQL_BOUND_INVALID;
}

static inline bool
YbBoundInclusive(const YbBound* bound)
{
	return bound->type == YB_YQL_BOUND_VALID_INCLUSIVE;
}

static bool
YbIsValidRange(const YbBound *start, const YbBound *end)
{
	Assert(YbBoundValid(start) && YbBoundValid(end));
	return start->value < end->value ||
	       (start->value == end->value &&
	        YbBoundInclusive(start) &&
	        YbBoundInclusive(end));
}

static bool
YbApplyStartBound(YbRange *range, const YbBound *start)
{
	Assert(YbBoundValid(start));
	if (YbBoundValid(&range->end) && !YbIsValidRange(start, &range->end))
		return false;

	if (!YbBoundValid(&range->start) ||
	    (range->start.value < start->value) ||
	    (range->start.value == start->value && !YbBoundInclusive(start)))
	{
		range->start = *start;
	}
	return true;
}

static bool
YbApplyEndBound(YbRange *range, const YbBound *end)
{
	Assert(YbBoundValid(end));
	if (YbBoundValid(&range->start) && !YbIsValidRange(&range->start, end))
		return false;

	if (!YbBoundValid(&range->end) ||
	    (range->end.value > end->value) ||
	    (range->end.value == end->value && !YbBoundInclusive(end)))
	{
		range->end = *end;
	}
	return true;
}

static bool
YbBindHashKeys(YbScanDesc ybScan)
{
	ListCell   *lc;
	YbRange		range = {0};

	foreach(lc, ybScan->hash_code_keys)
	{
		ScanKey key = (ScanKey) lfirst(lc);
		Assert(YbIsHashCodeSearch(key));
		YbBound bound = {
			.type = YB_YQL_BOUND_VALID,
			.value = key->sk_argument
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
				switch_fallthrough();
			case BTGreaterStrategyNumber:
				if (!YbApplyStartBound(&range, &bound))
					return false;
				break;

			case BTLessEqualStrategyNumber:
				bound.type = YB_YQL_BOUND_VALID_INCLUSIVE;
				switch_fallthrough();
			case BTLessStrategyNumber:
				if (!YbApplyEndBound(&range, &bound))
					return false;
				break;

			default:
				break; /* unreachable */
		}
	}

	if (YbBoundValid(&range.start) || YbBoundValid(&range.end))
		HandleYBStatus(YBCPgDmlBindHashCodes(
			ybScan->handle,
			range.start.type, range.start.value,
			range.end.type, range.end.value));

	return true;
}

static void
YbCollectHashKeyComponents(YbScanDesc ybScan, YbScanPlan scan_plan,
						   Bitmapset **required_attrs, bool is_index_only_scan)
{
	Relation index = ybScan->index;
	Relation secondary_index =
		index && !index->rd_index->indisprimary && !is_index_only_scan ? index :
																		 NULL;
	int idx = -1;
	while ((idx = bms_next_member(scan_plan->hash_key, idx)) >= 0)
	{
		if (secondary_index)
		{
			AttrNumber attnum =
				YBBmsIndexToAttnum(scan_plan->target_relation, idx);
			attnum = secondary_index->rd_index->indkey.values[attnum - 1];

			int hash_code_bms_idx = YBAttnumToBmsIndex(secondary_index, attnum);

			*required_attrs =
				bms_add_member(*required_attrs, hash_code_bms_idx);
		}
		else
		{
			*required_attrs = bms_add_member(*required_attrs, idx);
		}
	}
}

/*
 * Returns a bitmap of all non-hashcode columns that may require a recheck.
 */
static Bitmapset *
YbGetOrdinaryColumnsNeedingRecheck(YbScanDesc ybScan)
{
	Bitmapset *columns = NULL;

	if (ybScan->all_ordinary_keys_bound)
		return columns;

	ScanKey *keys = ybScan->keys;
	for (int i = 0; i < ybScan->nkeys; i += YbGetLengthOfKey(&keys[i]))
	{
		if (ybScan->prepare_params.index_only_scan ||
			(ybScan->target_key_attnums[i] != InvalidAttrNumber &&
			 keys[i]->sk_flags & ~(SK_SEARCHNULL | SK_SEARCHNOTNULL)))
		{
			int bms_idx = YBAttnumToBmsIndexWithMinAttr(
				YBFirstLowInvalidAttributeNumber, keys[i]->sk_attno);
			columns = bms_add_member(columns, bms_idx);
		}
	}
	return columns;
}

/* Setup the targets */
static void
ybcSetupTargets(YbScanDesc ybScan, YbScanPlan scan_plan, Scan *pg_scan_plan)
{
	Relation index = ybScan->index;
	bool is_index_only_scan = ybScan->prepare_params.index_only_scan;
	const int min_attr = YBFirstLowInvalidAttributeNumber;

	/*
	 * required_attrs struct stores list of target columns required by scan
	 * plan. All other non system columns may be excluded from reading from
	 * DocDB for optimization. Required columns are:
	 * - any bound key column that is required for recheck
	 * - all query targets columns (from index for Index Only Scan case and from
	 * table otherwise)
	 * - all qual columns (not bound to the scan keys), they are required for
	 * filtering results on the postgres side
	 */
	Bitmapset *required_attrs = NULL;
	bool all_attrs_required = true;

	/*
	 * Catalog requests do not have a pg_scan_plan, and require both ybctid and
	 * oid
	*/
	if (!pg_scan_plan)
	{
		if (scan_plan->target_relation->rd_rel->relhasoids)
			required_attrs = bms_add_member(
				required_attrs, YBAttnumToBmsIndexWithMinAttr(
									min_attr, ObjectIdAttributeNumber));

		required_attrs = bms_add_member(
			required_attrs,
			YBAttnumToBmsIndexWithMinAttr(min_attr, YBTupleIdAttributeNumber));
	}
	else
	{
		Index target_relid = ybScan->prepare_params.index_only_scan ?
								 INDEX_VAR :
								 pg_scan_plan->scanrelid;

		/* Collect target attributes */
		pull_varattnos_min_attr((Node *) pg_scan_plan->plan.targetlist,
								target_relid, &required_attrs, min_attr);

		/* Collect table filtering attributes */
		pull_varattnos_min_attr((Node *) pg_scan_plan->plan.qual, target_relid,
								&required_attrs, min_attr);

		if (ybScan->hash_code_keys != NIL)
			YbCollectHashKeyComponents(ybScan, scan_plan, &required_attrs,
									   is_index_only_scan);

		required_attrs = bms_join(required_attrs,
								  YbGetOrdinaryColumnsNeedingRecheck(ybScan));

		/* Add any explicitly listed target keys */
		AttrNumber *sk_attno = ybScan->target_key_attnums;
		AttrNumber *sk_attno_end = sk_attno + ybScan->nkeys;
		for (; sk_attno != sk_attno_end; ++sk_attno)
		{
			{
				int bms_idx =
					YBAttnumToBmsIndexWithMinAttr(min_attr, *sk_attno);
				required_attrs = bms_add_member(required_attrs, bms_idx);
			}
		}

		/* In case InvalidAttrNumber is set whole row columns are required */
		all_attrs_required = bms_is_member(
			YBAttnumToBmsIndexWithMinAttr(min_attr, InvalidAttrNumber),
			required_attrs);

		/* TableOidAttrNumber is a virtual column, do not send it. */
		int table_oid_bms_index =
			YBAttnumToBmsIndexWithMinAttr(min_attr, TableOidAttributeNumber);
		if (bms_is_member(table_oid_bms_index, required_attrs))
		{
			required_attrs =
				bms_del_member(required_attrs, table_oid_bms_index);
			/*
			 * TODO(#18870): A HeapTuple is required to store
			 * TableOidAttrNumber. Force its creation by including ybctid.
			 */
			required_attrs = bms_add_member(
				required_attrs, YBAttnumToBmsIndexWithMinAttr(
									min_attr, YBTupleIdAttributeNumber));
		}
	}

	/*
	 * TODO(#16717): Such placeholder target can be removed once the pg_dml
	 * fetcher can recognize empty rows in a response with no explict
	 * targets.
	 *
	 * TODO(#18870): ybctid can be large. Can we use a smaller placeholder than
	 * this? (e.g. NULL)
	 */
	if (bms_is_empty(required_attrs))
		required_attrs = bms_add_member(
			required_attrs,
			YBAttnumToBmsIndexWithMinAttr(min_attr, YBTupleIdAttributeNumber));

	/*
	 * If we need every attribute, go through each (normal) attribute and
	 * whatever is not in the bitmapset. Attributes in the bitmapset will be
	 * added next.
	 */
	if (all_attrs_required)
	{
		if (is_index_only_scan && index->rd_index->indisprimary)
		{
			/*
			 * Special case: For Primary-Key-ONLY-Scan, we select ONLY the
			 * primary key from the target table instead of the whole target
			 * table.
			 */
			for (int i = 0; i < index->rd_index->indnatts; i++)
			{
				AttrNumber attnum = index->rd_index->indkey.values[i];
				bool is_in_bms = bms_is_member(
					YBAttnumToBmsIndexWithMinAttr(min_attr, attnum),
					required_attrs);
				bool is_dropped =
					TupleDescAttr(ybScan->target_desc, attnum - 1)->attisdropped;
				if (!is_in_bms && !is_dropped)
					YbDmlAppendTargetRegular(ybScan->target_desc, attnum,
											 ybScan->handle);
			}
		}
		else
			for (AttrNumber attnum = 1; attnum <= ybScan->target_desc->natts;
				 attnum++)
			{
				bool is_in_bms = bms_is_member(
					YBAttnumToBmsIndexWithMinAttr(min_attr, attnum),
					required_attrs);
				bool is_dropped =
					TupleDescAttr(ybScan->target_desc, attnum - 1)->attisdropped;
				if (!is_in_bms && !is_dropped)
					YbDmlAppendTargetRegular(ybScan->target_desc, attnum,
											 ybScan->handle);
			}
	}

	/*
	 * Add everything from the required_attrs bitmapset. This must happen even
	 * if we already added everything through the bitmapset, because there may
	 * be special (system) columns in the bitmapset. We iterate backwards
	 * because that's the order that the prefetcher expects the columns.
	 */
	int idx = -1;
	while ((idx = bms_prev_member(required_attrs, idx)) >= 0)
	{
		AttrNumber attnum = YBBmsIndexToAttnumWithMinAttr(min_attr, idx);
		if (attnum > 0)
			YbDmlAppendTargetRegular(ybScan->target_desc, attnum,
									 ybScan->handle);
		else if (attnum < 0)
			YbDmlAppendTargetSystem(attnum, ybScan->handle);
	}

	if (!is_index_only_scan && index && !index->rd_index->indisprimary)
		YbDmlAppendTargetSystem(YBIdxBaseTupleIdAttributeNumber,
								ybScan->handle);

	bms_free(required_attrs);
}

/*
 * Set aggregate targets into handle.  If index is not null, convert column
 * attribute numbers from table-based numbers to index-based ones.
 */
void
YbDmlAppendTargetsAggregate(List *aggrefs, TupleDesc tupdesc, Relation index,
							bool xs_want_itup, YBCPgStatement handle)
{
	ListCell   *lc;

	/* Set aggregate scan targets. */
	foreach(lc, aggrefs)
	{
		Aggref *aggref = lfirst_node(Aggref, lc);
		char *func_name = get_func_name(aggref->aggfnoid);
		ListCell *lc_arg;
		YBCPgExpr op_handle;
		const YBCPgTypeEntity *type_entity;

		/* Get type entity for the operator from the aggref. */
		type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, aggref->aggtranstype);

		/* Create operator. */
		HandleYBStatus(YBCPgNewOperator(handle, func_name, type_entity, aggref->aggcollid, &op_handle));

		/* Handle arguments. */
		if (aggref->aggstar) {
			/*
			 * Add dummy argument for COUNT(*) case, turning it into COUNT(0).
			 * We don't use a column reference as we want to count rows
			 * even if all column values are NULL.
			 */
			YBCPgExpr const_handle;
			HandleYBStatus(YBCPgNewConstant(handle,
							 type_entity,
							 false /* collate_is_valid_non_c */,
							 NULL /* collation_sortkey */,
							 0 /* datum */,
							 false /* is_null */,
							 &const_handle));
			HandleYBStatus(YBCPgOperatorAppendArg(op_handle, const_handle));
		} else {
			/* Add aggregate arguments to operator. */
			foreach(lc_arg, aggref->args)
			{
				TargetEntry *tle = lfirst_node(TargetEntry, lc_arg);
				if (IsA(tle->expr, Const))
				{
					Const* const_node = castNode(Const, tle->expr);
					/* Already checked by yb_agg_pushdown_supported */
					Assert(const_node->constisnull || const_node->constbyval);

					YBCPgExpr const_handle;
					HandleYBStatus(YBCPgNewConstant(handle,
									 type_entity,
									 false /* collate_is_valid_non_c */,
									 NULL /* collation_sortkey */,
									 const_node->constvalue,
									 const_node->constisnull,
									 &const_handle));
					HandleYBStatus(YBCPgOperatorAppendArg(op_handle, const_handle));
				}
				else if (IsA(tle->expr, Var))
				{
					/*
					 * Use original attribute number (varoattno) instead of projected one (varattno)
					 * as projection is disabled for tuples produced by pushed down operators.
					 */
					int attno = castNode(Var, tle->expr)->varoattno;
					/*
					 * For index only scans, translate the table-based
					 * attribute number to an index-based one.
					 */
					if (index && xs_want_itup)
						attno = YbGetIndexAttnum(attno, index);
					Form_pg_attribute attr = TupleDescAttr(tupdesc, attno - 1);
					YBCPgTypeAttrs type_attrs = {attr->atttypmod};

					YBCPgExpr arg = YBCNewColumnRef(handle,
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
		HandleYBStatus(YBCPgDmlAppendTarget(handle, op_handle));
	}

	/* Set ybbasectid in case of non-primary secondary index scan. */
	if (index && !xs_want_itup && !index->rd_index->indisprimary)
		YbDmlAppendTargetSystem(YBIdxBaseTupleIdAttributeNumber,
								handle);
}

/*
 * YbDmlAppendTargets
 *
 * Add targets to the statement.  The colref list is expected to be made up of
 * YbExprColrefDesc nodes.  Unlike YbDmlAppendTargetRegular, it does not do any
 * dropped-columns checking.
 */
void
YbDmlAppendTargets(List *colrefs, YBCPgStatement handle)
{
	ListCell   *lc;
	YBCPgExpr	expr;
	YBCPgTypeAttrs type_attrs;
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
		HandleYBStatus(YBCPgDmlAppendTarget(handle, expr));
	}
}

/*
 * YbDmlAppendQuals
 *
 * Add remote filter expressions to the statement.
 * The expression are pushed down to DocDB and used to filter rows early to
 * avoid sending them across network.
 * Set is_primary to false if the filter expression is to apply to secondary
 * index. In this case Var nodes must be properly adjusted to refer the index
 * columns rather than main relation columns.
 * For primary key scan or sequential scan is_primary should be true.
 */
void
YbDmlAppendQuals(List *quals, bool is_primary, YBCPgStatement handle)
{
	ListCell   *lc;

	foreach(lc, quals)
	{
		Expr *expr = (Expr *) lfirst(lc);
		/* Create new PgExpr wrapper for the expression */
		YBCPgExpr yb_expr = YBCNewEvalExprCall(handle, expr);
		/* Add the PgExpr to the statement */
		HandleYBStatus(YbPgDmlAppendQual(handle, yb_expr, is_primary));
	}
}

/*
 * YbDmlAppendColumnRefs
 *
 * Add the list of column references used by pushed down expressions to the
 * statement.
 * The colref list is expected to be the list of YbExprColrefDesc nodes.
 * Set is_primary to false if the filter expression is to apply to secondary
 * index. In this case attno field values must be properly adjusted to refer
 * the index columns rather than main relation columns.
 * For primary key scan or sequential scan is_primary should be true.
 */
void
YbDmlAppendColumnRefs(List *colrefs, bool is_primary, YBCPgStatement handle)
{
	ListCell   *lc;

	foreach(lc, colrefs)
	{
		YbExprColrefDesc *param = lfirst_node(YbExprColrefDesc, lc);
		YBCPgTypeAttrs type_attrs = { param->typmod };
		/* Create new PgExpr wrapper for the column reference */
		YBCPgExpr yb_expr = YBCNewColumnRef(handle,
											param->attno,
											param->typid,
											param->collid,
											&type_attrs);
		/* Add the PgExpr to the statement */
		HandleYBStatus(YbPgDmlAppendColumnRef(handle, yb_expr, is_primary));
	}
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
			 int nkeys, ScanKey keys,
			 Scan *pg_scan_plan,
			 PushdownExprs *rel_pushdown,
			 PushdownExprs *idx_pushdown,
			 List *aggrefs,
			 int distinct_prefixlen,
			 YBCPgExecParameters *exec_params,
			 bool is_internal_scan)
{
	/* Set up Yugabyte scan description */
	YbScanDesc ybScan = (YbScanDesc) palloc0(sizeof(YbScanDescData));

	/* Flatten keys and store the results in ybScan. */
	ybExtractScanKeys(keys, nkeys, ybScan);

	if (YbIsUnsatisfiableCondition(ybScan->nkeys, ybScan->keys))
	{
		ybScan->quit_scan = true;
		return ybScan;
	}
	ybScan->exec_params = exec_params;
	ybScan->relation = relation;
	ybScan->index = index;
	ybScan->quit_scan = false;

	/* Set up the scan plan */
	YbScanPlanData scan_plan;
	ybcSetupScanPlan(xs_want_itup, ybScan, &scan_plan);
	ybcSetupScanKeys(ybScan, &scan_plan);

	/* Create handle */
	HandleYBStatus(YBCPgNewSelect(YBCGetDatabaseOid(relation),
								  YbGetStorageRelid(relation),
								  &ybScan->prepare_params,
								  YBCIsRegionLocal(relation),
								  &ybScan->handle));

	/* Set up binds */
	if (!YbBindScanKeys(ybScan, &scan_plan, false /* is_for_precheck */) ||
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
		YbDmlAppendTargetsAggregate(aggrefs, ybScan->target_desc, index,
									xs_want_itup, ybScan->handle);
	else
		ybcSetupTargets(ybScan, &scan_plan, pg_scan_plan);

	/*
	 * Set up pushdown expressions.
	 */
	if (rel_pushdown != NULL)
	{
		YbDmlAppendQuals(rel_pushdown->quals, true /* is_primary */,
						 ybScan->handle);
		YbDmlAppendColumnRefs(rel_pushdown->colrefs, true /* is_primary */,
							  ybScan->handle);
	}
	if (idx_pushdown != NULL)
	{
		YbDmlAppendQuals(idx_pushdown->quals, false /* is_primary */,
						 ybScan->handle);
		YbDmlAppendColumnRefs(idx_pushdown->colrefs, false /* is_primary */,
							  ybScan->handle);
	}

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

	bms_free(scan_plan.hash_key);
	bms_free(scan_plan.primary_key);
	bms_free(scan_plan.sk_cols);
	return ybScan;
}

/*
 * Return true if the given tuple does not match the ordinary
 * (non-yb_hash_code) scan keys.  Returning false is not a guarantee for match.
 *
 * Any modifications here may need to be reflected in YbNeedsRecheck as well.
 */
static bool
ybIsTupMismatch(HeapTuple tup, YbScanDesc ybScan)
{
	ScanKey	   *keys	 = ybScan->keys;
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

		ScanKey key = keys[i];
		bool  is_null = false;
		Datum res_datum = heap_getattr(
			tup, sk_attno[i], ybScan->target_desc, &is_null);

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

		bool matches = DatumGetBool(FunctionCall2Coll(
			&key->sk_func, key->sk_collation, res_datum, key->sk_argument));
		if (!matches)
			return true;
	}

	return false;
}

/*
 * Whether rows returned by DocDB need to be rechecked.  Currently, it can be
 * predetermined for the entire scan before tuples are fetched.
 */
inline bool
YbNeedsRecheck(YbScanDesc ybScan)
{
	if (ybScan->hash_code_keys != NIL)
		return true;

	Bitmapset *recheck_cols = YbGetOrdinaryColumnsNeedingRecheck(ybScan);
	bool any_cols_need_recheck = !bms_is_empty(recheck_cols);
	bms_free(recheck_cols);

	return any_cols_need_recheck;
}

HeapTuple
ybc_getnext_heaptuple(YbScanDesc ybScan, bool is_forward_scan, bool *recheck)
{
	HeapTuple   tup      = NULL;

	if (ybScan->quit_scan)
		return NULL;

	*recheck = YbNeedsRecheck(ybScan);

	/* Loop over rows from pggate. */
	while (HeapTupleIsValid(tup = ybcFetchNextHeapTuple(ybScan,
														is_forward_scan)))
	{
		/* Do a preliminary check to skip rows we can guarantee don't match. */
		if (ybIsTupMismatch(tup, ybScan))
		{
			heap_freetuple(tup);
			continue;
		}
		break;
	}
	return tup;
}

IndexTuple
ybc_getnext_indextuple(YbScanDesc ybScan, bool is_forward_scan, bool *recheck)
{
	if (ybScan->quit_scan)
		return NULL;
	*recheck = YbNeedsRecheck(ybScan);
	return ybcFetchNextIndexTuple(ybScan, is_forward_scan);
}

bool
ybc_getnext_aggslot(IndexScanDesc scan, YBCPgStatement handle,
					bool index_only_scan)
{
	/*
	 * As of 2023-08-10, the relid passed into ybFetchNext is not going to
	 * be used as it is only used when there are system targets, not
	 * counting the internal ybbasectid lookup to the index.
	 * YbDmlAppendTargetsAggregate only adds that ybbasectid plus operator
	 * targets.
	 * TODO(jason): this may need to be revisited when supporting GROUP BY
	 * aggregate pushdown where system columns are directly targeted.
	 */
	scan->yb_agg_slot = ybFetchNext(handle, scan->yb_agg_slot,
									InvalidOid /* relid */);
	/* For IndexScan, hack to make index_getnext think there are tuples. */
	if (!index_only_scan)
		scan->xs_hitup = (HeapTuple) 1;
	return !scan->yb_agg_slot->tts_isempty;
}

void ybc_free_ybscan(YbScanDesc ybscan)
{
	Assert(PointerIsValid(ybscan));
	YBCPgDeleteStatement(ybscan->handle);
	pfree(ybscan);
}

SysScanDesc ybc_systable_beginscan(Relation relation,
                                   Oid indexId,
                                   bool indexOK,
                                   Snapshot snapshot,
                                   int nkeys,
                                   ScanKey key)
{
	Relation index = NULL;

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
				key[i].sk_attno = YbGetIndexAttnum(key[i].sk_attno, index);
		}
	}

	Scan *pg_scan_plan = NULL; /* In current context scan plan is not available */
	YbScanDesc ybScan = ybcBeginScan(relation,
									 index,
									 false /* xs_want_itup */,
									 nkeys,
									 key,
									 pg_scan_plan,
									 NULL /* rel_pushdown */,
									 NULL /* idx_pushdown */,
									 NULL /* aggrefs */,
									 0 /* distinct_prefixlen */,
									 NULL /* exec_params */,
									 true /* is_internal_scan */);

	/* Set up Postgres sys table scan description */
	SysScanDesc scan_desc = (SysScanDesc) palloc0(sizeof(SysScanDescData));
	scan_desc->heap_rel   = relation;
	scan_desc->snapshot   = snapshot;
	scan_desc->ybscan     = ybScan;

	if (index)
	{
		RelationClose(index);
	}

	return scan_desc;
}

HeapTuple ybc_systable_getnext(SysScanDesc scan_desc)
{
	bool recheck = false;

	Assert(PointerIsValid(scan_desc->ybscan));

	HeapTuple tuple = ybc_getnext_heaptuple(scan_desc->ybscan, true /* is_forward_scan */,
											&recheck);

	Assert(!recheck);

	return tuple;
}

void ybc_systable_endscan(SysScanDesc scan_desc)
{
	ybc_free_ybscan(scan_desc->ybscan);
	pfree(scan_desc);
}

HeapScanDesc ybc_heap_beginscan(Relation relation,
                                Snapshot snapshot,
                                int nkeys,
                                ScanKey key,
                                bool temp_snap)
{
	/* Restart should not be prevented if operation caused by system read of system table. */
	Scan *pg_scan_plan = NULL; /* In current context scan plan is not available */
	YbScanDesc ybScan = ybcBeginScan(relation,
									 NULL /* index */,
									 false /* xs_want_itup */,
									 nkeys,
									 key,
									 pg_scan_plan,
									 NULL /* rel_pushdown */,
									 NULL /* idx_pushdown */,
									 NULL /* aggrefs */,
									 0 /* distinct_prefixlen */,
									 NULL /* exec_params */,
									 true /* is_internal_scan */);

	/* Set up Postgres sys table scan description */
	HeapScanDesc scan_desc = (HeapScanDesc) palloc0(sizeof(HeapScanDescData));
	scan_desc->rs_rd        = relation;
	scan_desc->rs_snapshot  = snapshot;
	scan_desc->rs_temp_snap = temp_snap;
	scan_desc->rs_cblock    = InvalidBlockNumber;
	scan_desc->ybscan       = ybScan;

	return scan_desc;
}

HeapTuple ybc_heap_getnext(HeapScanDesc scan_desc)
{
	bool recheck = false;

	Assert(PointerIsValid(scan_desc->ybscan));

	HeapTuple tuple = ybc_getnext_heaptuple(scan_desc->ybscan, true /* is_forward_scan */,
											&recheck);

	Assert(!recheck);

	return tuple;
}

void ybc_heap_endscan(HeapScanDesc scan_desc)
{
	ybc_free_ybscan(scan_desc->ybscan);
	if (scan_desc->rs_temp_snap)
		UnregisterSnapshot(scan_desc->rs_snapshot);
	pfree(scan_desc);
}

/*
 * ybc_remote_beginscan
 *   Begin sequential scan of a YB relation.
 * The YbSeqScan uses it directly, not via heap scan interception, so it has
 * more controls on what is passed over to the ybcBeginScan.
 * The HeapScanDesc structure is still being used, in future we may increase
 * the level of integration.
 * The structure is compatible with one the ybc_heap_beginscan returns, so
 * ybc_heap_getnext and ybc_heap_endscan are respectively used to fetch tuples
 * and finish the scan.
 */
HeapScanDesc
ybc_remote_beginscan(Relation relation,
					 Snapshot snapshot,
					 Scan *pg_scan_plan,
					 PushdownExprs *pushdown,
					 List *aggrefs,
					 YBCPgExecParameters *exec_params)
{
	YbScanDesc ybScan = ybcBeginScan(relation,
									 NULL /* index */,
									 false /* xs_want_itup */,
									 0 /* nkeys */,
									 NULL /* key */,
									 pg_scan_plan,
									 pushdown /* rel_pushdown */,
									 NULL /* idx_pushdown */,
									 aggrefs,
									 0 /* distinct_prefixlen */,
									 exec_params,
									 false /* is_internal_scan */);

	/* Set up Postgres sys table scan description */
	HeapScanDesc scan_desc = (HeapScanDesc) palloc0(sizeof(HeapScanDescData));
	scan_desc->rs_rd        = relation;
	scan_desc->rs_snapshot  = snapshot;
	scan_desc->rs_temp_snap = false;
	scan_desc->rs_cblock    = InvalidBlockNumber;
	scan_desc->ybscan       = ybScan;

	return scan_desc;
}

/* --------------------------------------------------------------------------------------------- */

void ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
					 bool is_backwards_scan, bool is_seq_scan,
					 bool is_uncovered_idx_scan,
					 Cost *startup_cost, Cost *total_cost,
					 Oid index_tablespace_oid)
{
	if (is_seq_scan && !enable_seqscan)
		*startup_cost = disable_cost;
	else
		*startup_cost =
			yb_enable_optimizer_statistics ? yb_network_fetch_cost : 0;

	/*
	 * Yugabyte-specific per-tuple cost considerations:
	 *   - 10x the regular CPU cost to account for network/RPC + DocDB overhead.
	 *   - backwards scan scale factor as it will need that many more fetches
	 *     to get all rows/tuples.
	 *   - uncovered index scan is more costly than index-only or seq scan because
	 *     it requires extra request to the main table.
	 */
	double tsp_cost = 0.0;
	bool is_valid_tsp_cost = (!is_uncovered_idx_scan
							  && get_yb_tablespace_cost(index_tablespace_oid,
														&tsp_cost));
	Cost yb_per_tuple_cost_factor = YB_DEFAULT_PER_TUPLE_COST;

	if (is_valid_tsp_cost && yb_per_tuple_cost_factor > tsp_cost)
	{
		yb_per_tuple_cost_factor = tsp_cost;
	}

	Assert(!is_valid_tsp_cost || tsp_cost != 0);
	if (is_backwards_scan)
	{
		yb_per_tuple_cost_factor *= YBC_BACKWARDS_SCAN_COST_FACTOR;
	}
	if (is_uncovered_idx_scan)
	{
		yb_per_tuple_cost_factor *= YBC_UNCOVERED_INDEX_COST_FACTOR;
	}

	Cost cost_per_tuple = cpu_tuple_cost * yb_per_tuple_cost_factor +
	                       baserel->baserestrictcost.per_tuple;

	*startup_cost += baserel->baserestrictcost.startup;

	*total_cost   = *startup_cost + cost_per_tuple * baserel->tuples * selectivity;
}

/*
 * Evaluate the selectivity for yb_hash_code qualifiers.
 * Returns 1.0 if there are no yb_hash_code comparison expressions for this
 * index.
 */
static double ybcEvalHashSelectivity(List *hashed_qinfos)
{
	bool greatest_set = false;
	int greatest = 0;

	bool lowest_set = false;
	int lowest = USHRT_MAX;
	double selectivity;
	ListCell * lc;

	foreach(lc, hashed_qinfos)
	{
		IndexQualInfo *qinfo = (IndexQualInfo *) lfirst(lc);
		RestrictInfo *rinfo = qinfo->rinfo;
		Expr	   *clause = rinfo->clause;

		if (!IsA(qinfo->other_operand, Const))
		{
			continue;
		}

		int strategy;
		Oid lefttype;
		Oid righttype;
		get_op_opfamily_properties(((OpExpr*) clause)->opno,
								   INTEGER_LSM_FAM_OID,
								   false,
								   &strategy,
								   &lefttype,
								   &righttype);

		int signed_val = ((Const*) qinfo->other_operand)->constvalue;
		signed_val = signed_val < 0 ? 0 : signed_val;
		uint32_t val = signed_val > USHRT_MAX ? USHRT_MAX : signed_val;

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
			case BTLessStrategyNumber: switch_fallthrough();
			case BTLessEqualStrategyNumber:
				greatest_set = true;
				greatest = val > greatest ? val : greatest;
				break;
			case BTGreaterEqualStrategyNumber: switch_fallthrough();
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
		{
			break;
		}
	}

	if (!greatest_set && !lowest_set)
	{
		return 1.0;
	}

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
static double ybcIndexEvalClauseSelectivity(Relation index,
											double reltuples,
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
	{
		return YBC_FULL_SCAN_SELECTIVITY;
	}

	/*
	 * Otherwise, it will be either a primary key lookup or range scan
	 * on a hash key.
	 */
	if (bms_is_subset(primary_key, qual_cols))
	{
		/* For unique indexes full key guarantees single row. */
		if (is_unique_idx)
			return (reltuples == 0) ? YBC_SINGLE_ROW_SELECTIVITY :
									 (double)(1.0 / reltuples);
		else
			return YBC_SINGLE_KEY_SELECTIVITY;
	}

	return YBC_HASH_SCAN_SELECTIVITY;
}

Oid ybc_get_attcollation(TupleDesc desc, AttrNumber attnum)
{
	return attnum > 0 ? TupleDescAttr(desc, attnum - 1)->attcollation : InvalidOid;
}

void ybcIndexCostEstimate(struct PlannerInfo *root, IndexPath *path,
							Selectivity *selectivity, Cost *startup_cost,
							Cost *total_cost)
{
	Relation	index = RelationIdGetRelation(path->indexinfo->indexoid);
	bool		isprimary = index->rd_index->indisprimary;
	Relation	relation = isprimary ? RelationIdGetRelation(index->rd_index->indrelid) : NULL;
	RelOptInfo *baserel = path->path.parent;
	List	   *qinfos = NIL;
	ListCell   *lc;
	bool        is_backwards_scan = path->indexscandir == BackwardScanDirection;
	bool        is_unique = index->rd_index->indisunique;
	bool        is_partial_idx = path->indexinfo->indpred != NIL && path->indexinfo->predOK;
	Bitmapset  *const_quals = NULL;
	List	   *hashed_qinfos = NIL;
	List	   *clauses = NIL;
	double 		baserel_rows_estimate;

	/* Primary-index scans are always covered in Yugabyte (internally) */
	bool       is_uncovered_idx_scan = !index->rd_index->indisprimary &&
	                                   path->path.pathtype != T_IndexOnlyScan;

	YbScanPlanData	scan_plan;
	memset(&scan_plan, 0, sizeof(scan_plan));
	scan_plan.target_relation = isprimary ? relation : index;
	ybcLoadTableInfo(scan_plan.target_relation, &scan_plan);

	/* Do preliminary analysis of indexquals */
	qinfos = deconstruct_indexquals(path);

	/* Find out the search conditions on the primary key columns */
	foreach(lc, qinfos)
	{
		IndexQualInfo *qinfo = (IndexQualInfo *) lfirst(lc);
		RestrictInfo *rinfo = qinfo->rinfo;
		AttrNumber	 attnum = isprimary ? index->rd_index->indkey.values[qinfo->indexcol]
										: (qinfo->indexcol + 1);
		Expr	   *clause = rinfo->clause;
		int			bms_idx = YBAttnumToBmsIndex(scan_plan.target_relation, attnum);

		if (IsA(clause, NullTest))
		{
			const_quals = bms_add_member(const_quals, bms_idx);
			ybcAddAttributeColumn(&scan_plan, attnum);
		}
		else
		{
			Oid	clause_op = qinfo->clause_op;

			if (OidIsValid(clause_op))
			{
				ybcAddAttributeColumn(&scan_plan, attnum);
				if (qinfo->other_operand && IsA(qinfo->other_operand, Const))
					const_quals = bms_add_member(const_quals, bms_idx);
			}
		}

		if (qinfo->is_hashed)
		{
			hashed_qinfos = lappend(hashed_qinfos, qinfo);
		}
		else
		{
			clauses = lappend(clauses, rinfo);
		}
	}
	if (hashed_qinfos != NIL)
	{
		*selectivity = ybcEvalHashSelectivity(hashed_qinfos);
		baserel_rows_estimate = baserel->tuples * (*selectivity);
	}
	else
	{
		if (yb_enable_optimizer_statistics)
		{
			*selectivity = clauselist_selectivity(root /* PlannerInfo */,
												clauses,
												path->indexinfo->rel->relid /* varrelid */,
												JOIN_INNER,
												NULL /* SpecialJoinInfo */);
			baserel_rows_estimate = baserel->tuples * (*selectivity) >= 1
				? baserel->tuples * (*selectivity)
				: 1;
		}
		else
		{
			*selectivity = ybcIndexEvalClauseSelectivity(index,
														baserel->tuples,
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
	{
		*selectivity *= YBC_PARTIAL_IDX_PRED_SELECTIVITY;
	}

	ybcCostEstimate(baserel, *selectivity, is_backwards_scan,
					false /* is_seq_scan */, is_uncovered_idx_scan,
					startup_cost, total_cost,
					path->indexinfo->reltablespace);

	if (!yb_enable_optimizer_statistics)
	{
		/*
		 * Try to evaluate the number of rows this baserel might return.
		 * We cannot rely on the join conditions here (e.g. t1.c1 = t2.c2) because
		 * they may not be applied if another join path is chosen.
		 * So only use the t1.c1 = <const_value> quals (filtered above) for this.
		 */
		double const_qual_selectivity = ybcIndexEvalClauseSelectivity(index,
																	  baserel->tuples,
																	  const_quals,
																	  is_unique,
																	  scan_plan.hash_key,
																	  scan_plan.primary_key);
		baserel_rows_estimate = const_qual_selectivity * baserel->tuples;

		if (baserel_rows_estimate < baserel->rows)
		{
			baserel->rows = baserel_rows_estimate;
		}
	}

	if (relation)
		RelationClose(relation);

	RelationClose(index);
}

HeapTuple YBCFetchTuple(Relation relation, Datum ybctid)
{
	YBCPgStatement ybc_stmt;
	TupleDesc      tupdesc = RelationGetDescr(relation);

	HandleYBStatus(YBCPgNewSelect(YBCGetDatabaseOid(relation),
								  YbGetStorageRelid(relation),
								  NULL /* prepare_params */,
								  YBCIsRegionLocal(relation),
								  &ybc_stmt));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(ybc_stmt,
										   BYTEAOID,
										   InvalidOid,
										   ybctid,
										   false);
	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	/*
	 * Set up the scan targets. For index-based scan we need to return all "real" columns.
	 */
	if (RelationGetForm(relation)->relhasoids)
		YbDmlAppendTargetSystem(ObjectIdAttributeNumber, ybc_stmt);
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
	HandleYBStatus(YBCPgExecSelect(ybc_stmt, NULL /* exec_params */));

	HeapTuple tuple    = NULL;
	bool      has_data = false;

	Datum           *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool            *nulls  = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Fetch one row. */
	HandleYBStatus(YBCPgDmlFetch(ybc_stmt,
								 tupdesc->natts,
								 (uint64_t *) values,
								 nulls,
								 &syscols,
								 &has_data));

	if (has_data)
	{
		tuple = heap_form_tuple(tupdesc, values, nulls);

		if (syscols.oid != InvalidOid)
		{
			HeapTupleSetOid(tuple, syscols.oid);
		}
		if (syscols.ybctid != NULL)
		{
			tuple->t_ybctid = PointerGetDatum(syscols.ybctid);
		}
		tuple->t_tableOid = RelationGetRelid(relation);
	}
	pfree(values);
	pfree(nulls);
	YBCPgDeleteStatement(ybc_stmt);
	return tuple;
}

HTSU_Result
YBCLockTuple(Relation relation, Datum ybctid, RowMarkType mode, LockWaitPolicy pg_wait_policy,
			 EState* estate)
{
	int docdb_wait_policy;

	YBSetRowLockPolicy(&docdb_wait_policy, pg_wait_policy);

	YBCPgStatement ybc_stmt;
	HandleYBStatus(YBCPgNewSelect(YBCGetDatabaseOid(relation),
								  RelationGetRelid(relation),
								  NULL /* prepare_params */,
								  YBCIsRegionLocal(relation),
								  &ybc_stmt));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(ybc_stmt, BYTEAOID, InvalidOid, ybctid, false);
	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	YBCPgExecParameters exec_params = {0};
	exec_params.limit_count = 1;
	exec_params.rowmark = mode;
	exec_params.pg_wait_policy = pg_wait_policy;
	exec_params.docdb_wait_policy = docdb_wait_policy;
	exec_params.stmt_in_txn_limit_ht_for_reads =
		estate->yb_exec_params.stmt_in_txn_limit_ht_for_reads;

	HTSU_Result res = HeapTupleMayBeUpdated;
	MemoryContext exec_context = GetCurrentMemoryContext();

	PG_TRY();
	{
		/*
		 * Execute the select statement to lock the tuple with given ybctid.
		 */
		HandleYBStatus(YBCPgExecSelect(ybc_stmt, &exec_params /* exec_params */));

		bool has_data = false;
		Datum *values = NULL;
		bool *nulls  = NULL;
		YBCPgSysColumns syscols;

		/*
		 * Below is done to ensure the read request is flushed to tserver.
		 */
		HandleYBStatus(
				YBCPgDmlFetch(
						ybc_stmt,
						0,
						(uint64_t *) values,
						nulls,
						&syscols,
						&has_data));
		YBCPgAddIntoForeignKeyReferenceCache(RelationGetRelid(relation), ybctid);
	}
	PG_CATCH();
	{
		MemoryContext error_context = MemoryContextSwitchTo(exec_context);
		ErrorData* edata = CopyErrorData();

		elog(DEBUG2, "Error when trying to lock row. "
			 "pg_wait_policy=%d docdb_wait_policy=%d txn_errcode=%d message=%s",
			 pg_wait_policy, docdb_wait_policy, edata->yb_txn_errcode, edata->message);

		if (YBCIsTxnConflictError(edata->yb_txn_errcode))
			res = HeapTupleUpdated;
		else if (YBCIsTxnSkipLockingError(edata->yb_txn_errcode))
			res = HeapTupleWouldBlock;
		else {
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

/*
 * ANALYZE support: take random sample of a YB table data
 */

YbSample
ybBeginSample(Relation rel, int targrows)
{
	ReservoirStateData rstate;
	Oid dboid = YBCGetDatabaseOid(rel);
	Oid relid = RelationGetRelid(rel);
	TupleDesc tupdesc = RelationGetDescr(rel);
	YbSample ybSample = (YbSample) palloc0(sizeof(YbSampleData));
	ybSample->relation = rel;
	ybSample->targrows = targrows;
	ybSample->liverows = 0;
	ybSample->deadrows = 0;
	elog(DEBUG1, "Sampling %d rows from table %s", targrows, RelationGetRelationName(rel));

	/*
	 * Create new sampler command
	 */
	HandleYBStatus(YBCPgNewSample(dboid,
								  relid,
								  targrows,
								  YBCIsRegionLocal(rel),
								  &ybSample->handle));

	/*
	 * Set up the scan targets. We need to return all "real" columns.
	 */
	if (RelationGetForm(ybSample->relation)->relhasoids)
		YbDmlAppendTargetSystem(ObjectIdAttributeNumber, ybSample->handle);
	for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		if (!TupleDescAttr(tupdesc, attnum - 1)->attisdropped)
			YbDmlAppendTargetRegular(tupdesc, attnum, ybSample->handle);
	}

	/*
	 * Initialize sampler random state
	 */
	reservoir_init_selection_state(&rstate, targrows);
	HandleYBStatus(YBCPgInitRandomState(ybSample->handle,
										rstate.W,
										SamplerRandomStateToUint64(rstate.randstate)));

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
	bool has_more;
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
	Oid relid = RelationGetRelid(ybSample->relation);
	TupleDesc	tupdesc = RelationGetDescr(ybSample->relation);
	Datum	   *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool	   *nulls  = (bool *) palloc(tupdesc->natts * sizeof(bool));
	int 		numrows;

	/*
	 * Execute equivalent of
	 *   SELECT * FROM table WHERE ybctid IN [yctid0, ybctid1, ...];
	 */
	HandleYBStatus(YBCPgExecSample(ybSample->handle));
	/*
	 * Retrieve liverows and deadrows counters.
	 * TODO: count deadrows
	 */
	HandleYBStatus(YBCPgGetEstimatedRowCount(ybSample->handle,
											 &ybSample->liverows,
											 &ybSample->deadrows));

	for (numrows = 0; numrows < ybSample->targrows; numrows++)
	{
		bool has_data = false;
		YBCPgSysColumns syscols;

		/* Fetch one row. */
		HandleYBStatus(YBCPgDmlFetch(ybSample->handle,
									 tupdesc->natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data));

		if (!has_data)
			break;

		/* Make a heap tuple in current memory context */
		rows[numrows] = heap_form_tuple(tupdesc, values, nulls);
		if (syscols.oid != InvalidOid)
		{
			HeapTupleSetOid(rows[numrows], syscols.oid);
		}
		if (syscols.ybctid != NULL)
		{
			rows[numrows]->t_ybctid = PointerGetDatum(syscols.ybctid);
		}
		rows[numrows]->t_tableOid = relid;
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
 *  Fetch next row from the provided YBCPgStatement and load it into the slot.
 *
 * The statement must be ready to be fetched from, in other words it should be
 * executed, that means request is sent to the DocDB.
 *
 * Fetched values are copied from the DocDB response and memory for by-reference
 * data types is allocated from the current memory context, so be sure that
 * lifetime of that context is appropriate.
 *
 * By default the slot holds a virtual tuple, a heap tuple is only formed if
 * the DocDB returns system columns. Virtual tuple has no storage for system
 * attributes. If heap tuple is formed, its t_tableOid field is updated with
 * provided relid and t_ybctid field is set to returned ybctid value. The heap
 * tuple is allocated in the slot's memory context.
 */
TupleTableSlot *
ybFetchNext(YBCPgStatement handle,
			TupleTableSlot *slot, Oid relid)
{
	TupleDesc	tupdesc = slot->tts_tupleDescriptor;
	Datum	   *values = slot->tts_values;
	bool	   *nulls = slot->tts_isnull;
	YBCPgSysColumns syscols;
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
		bool has_syscols;
		slot->tts_nvalid = tupdesc->natts;
		slot->tts_isempty = false;
		slot->tts_ybctid = PointerGetDatum(syscols.ybctid);
		HandleYBStatus(YBCPgDmlHasSystemTargets(handle, &has_syscols));
		if (has_syscols)
		{
			MemoryContext oldcontext = MemoryContextSwitchTo(slot->tts_mcxt);
			HeapTuple tuple = heap_form_tuple(tupdesc, values, nulls);
			if (OidIsValid(syscols.oid))
				HeapTupleSetOid(tuple, syscols.oid);
			tuple->t_tableOid = relid;
			tuple->t_ybctid = slot->tts_ybctid;
			slot = ExecStoreHeapTuple(tuple, slot, true);
			MemoryContextSwitchTo(oldcontext);
		}
	}

	return slot;
}
