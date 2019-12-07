/*--------------------------------------------------------------------------------------------------
 *
 * ybcam.c
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
 * src/backend/executor/ybcam.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include <string.h>
#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "commands/dbcommands.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/catalog.h"
#include "catalog/pg_database.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "nodes/relation.h"
#include "optimizer/cost.h"
#include "utils/datum.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "utils/resowner_private.h"
#include "utils/syscache.h"
#include "utils/selfuncs.h"
#include "utils/snapmgr.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "access/nbtree.h"

typedef struct YbScanPlanData
{
	/* Primary and hash key columns of the referenced table/relation. */
	Bitmapset *primary_key;
	Bitmapset *hash_key;

	/* The set of scan key columns to apply. */
	Bitmapset *sk_cols;

} YbScanPlanData;

typedef YbScanPlanData *YbScanPlan;

static void ybcAddAttributeColumn(YbScanPlan scan_plan, AttrNumber attnum)
{
  const int idx = attnum - FirstLowInvalidHeapAttributeNumber;

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
	bool is_primary = false;
	bool is_hash    = false;

	HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
											   attnum,
											   &is_primary,
											   &is_hash), ybc_table_desc);

	int idx = attnum - FirstLowInvalidHeapAttributeNumber;

	if (is_hash)
	{
		scan_plan->hash_key = bms_add_member(scan_plan->hash_key, idx);
	}
	if (is_primary)
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
	Oid            relid          = RelationGetRelid(relation);
	YBCPgTableDesc ybc_table_desc = NULL;

	HandleYBStatus(YBCPgGetTableDesc(ybc_pg_session, dboid, relid, &ybc_table_desc));

	for (AttrNumber attnum = 1; attnum <= relation->rd_att->natts; attnum++)
	{
		ybcCheckPrimaryKeyAttribute(scan_plan, ybc_table_desc, attnum);
	}
	if (relation->rd_rel->relhasoids)
	{
		ybcCheckPrimaryKeyAttribute(scan_plan, ybc_table_desc, ObjectIdAttributeNumber);
	}

	HandleYBStatus(YBCPgDeleteTableDesc(ybc_table_desc));
}

static Oid ybc_get_atttypid(YbScanDesc ybScan, AttrNumber attnum)
{
	Oid	atttypid;

	if (attnum > 0)
	{
		/* Get the type from the description */
		atttypid = TupleDescAttr(ybScan->tupdesc, attnum - 1)->atttypid;
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
static void ybcBindColumn(YbScanDesc ybScan, bool useIndex, AttrNumber attnum, Datum value)
{
	Oid	atttypid = ybc_get_atttypid(ybScan, attnum);

	YBCPgExpr ybc_expr = YBCNewConstant(ybScan->handle, atttypid, value, false /* isnull */);

	if (useIndex)
		HandleYBStmtStatusWithOwner(YBCPgDmlBindIndexColumn(ybScan->handle, attnum, ybc_expr),
									ybScan->handle,
									ybScan->stmt_owner);
	else
		HandleYBStmtStatusWithOwner(YBCPgDmlBindColumn(ybScan->handle, attnum, ybc_expr),
									ybScan->handle,
									ybScan->stmt_owner);
}

void ybcBindColumnCondEq(YbScanDesc ybScan, bool is_hash_key, AttrNumber attnum, Datum value)
{
	Oid	atttypid = ybc_get_atttypid(ybScan, attnum);

	YBCPgExpr ybc_expr = YBCNewConstant(ybScan->handle, atttypid, value, false /* isnull */);

	if (is_hash_key)
		HandleYBStmtStatusWithOwner(YBCPgDmlBindColumn(ybScan->handle, attnum, ybc_expr),
									ybScan->handle,
									ybScan->stmt_owner);
	else
		HandleYBStmtStatusWithOwner(YBCPgDmlBindColumnCondEq(ybScan->handle, attnum, ybc_expr),
									ybScan->handle,
									ybScan->stmt_owner);
}

static void ybcBindColumnCondBetween(YbScanDesc ybScan, AttrNumber attnum, 
    bool start_valid, Datum value, bool end_valid, Datum value_end)
{
	Oid	atttypid = ybc_get_atttypid(ybScan, attnum);

	YBCPgExpr ybc_expr = start_valid ? YBCNewConstant(ybScan->handle, atttypid, value, 
      false /* isnull */) : NULL;
	YBCPgExpr ybc_expr_end = end_valid ? YBCNewConstant(ybScan->handle, atttypid, value_end, 
      false /* isnull */) : NULL;

  HandleYBStmtStatusWithOwner(YBCPgDmlBindColumnCondBetween(ybScan->handle, attnum, ybc_expr, 
        ybc_expr_end),
      ybScan->handle,
      ybScan->stmt_owner);
}

/*
 * Bind an array of scan keys for a column.
 */
static void ybcBindColumnCondIn(YbScanDesc ybScan, AttrNumber attnum, int nvalues, 
    Datum *values)
{
	Oid	atttypid = ybc_get_atttypid(ybScan, attnum);

  YBCPgExpr ybc_exprs[nvalues]; /* VLA - scratch space */
  for (int i = 0; i < nvalues; i++) {
    ybc_exprs[i] = YBCNewConstant(ybScan->handle, atttypid, values[i], false /* isnull */);
  }

  HandleYBStmtStatusWithOwner(YBCPgDmlBindColumnCondIn(ybScan->handle, attnum, nvalues, ybc_exprs),
      ybScan->handle,
      ybScan->stmt_owner);
}

/*
 * Add a target column.
 */
static void ybcAddTargetColumn(YbScanDesc ybScan, AttrNumber attnum)
{
	/* Regular (non-system) attribute. */
	Oid atttypid = InvalidOid;
	int32 atttypmod = 0;
	if (attnum > 0)
	{
		Form_pg_attribute attr = TupleDescAttr(ybScan->tupdesc, attnum - 1);
		/* Ignore dropped attributes */
		if (attr->attisdropped)
			return;
		atttypid = attr->atttypid;
		atttypmod = attr->atttypmod;
	}

	YBCPgTypeAttrs type_attrs = { atttypmod };
	YBCPgExpr expr = YBCNewColumnRef(ybScan->handle, attnum, atttypid, &type_attrs);
	HandleYBStmtStatusWithOwner(YBCPgDmlAppendTarget(ybScan->handle, expr),
								ybScan->handle,
								ybScan->stmt_owner);
}

static HeapTuple ybcFetchNextHeapTuple(YbScanDesc ybScan, bool is_forward_scan)
{
	HeapTuple tuple    = NULL;
	bool      has_data = false;
	TupleDesc tupdesc  = ybScan->tupdesc;

	Datum           *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool            *nulls  = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Execute the select statement. */
	if (!ybScan->is_exec_done)
	{
		HandleYBStmtStatusWithOwner(YBCPgSetForwardScan(ybScan->handle, is_forward_scan),
									ybScan->handle,
									ybScan->stmt_owner);
		HandleYBStmtStatusWithOwner(YBCPgExecSelect(ybScan->handle, ybScan->exec_params),
									ybScan->handle,
									ybScan->stmt_owner);
		ybScan->is_exec_done = true;
	}

	/* Fetch one row. */
	HandleYBStmtStatusWithOwner(YBCPgDmlFetch(ybScan->handle,
	                                          tupdesc->natts,
	                                          (uint64_t *) values,
	                                          nulls,
	                                          &syscols,
	                                          &has_data),
	                            ybScan->handle,
	                            ybScan->stmt_owner);

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
		if (ybScan->tableOid != InvalidOid)
		{
			tuple->t_tableOid = ybScan->tableOid;
		}
	}
	pfree(values);
	pfree(nulls);

	return tuple;
}

static IndexTuple ybcFetchNextIndexTuple(YbScanDesc ybScan, Relation index, bool is_forward_scan)
{
	IndexTuple tuple    = NULL;
	bool       has_data = false;
	TupleDesc  tupdesc  = ybScan->tupdesc;

	Datum           *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool            *nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Execute the select statement. */
	if (!ybScan->is_exec_done)
	{
		HandleYBStmtStatusWithOwner(YBCPgSetForwardScan(ybScan->handle, is_forward_scan),
									ybScan->handle,
									ybScan->stmt_owner);
		HandleYBStmtStatusWithOwner(YBCPgExecSelect(ybScan->handle, ybScan->exec_params),
									ybScan->handle,
									ybScan->stmt_owner);
		ybScan->is_exec_done = true;
	}

	/* Fetch one row. */
	HandleYBStmtStatusWithOwner(YBCPgDmlFetch(ybScan->handle,
	                                          tupdesc->natts,
	                                          (uint64_t *) values,
	                                          nulls,
	                                          &syscols,
	                                          &has_data),
	                            ybScan->handle,
	                            ybScan->stmt_owner);

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
			}
		}
		else
		{
			tuple = index_form_tuple(tupdesc, values, nulls);
			if (syscols.ybbasectid != NULL)
			{
				tuple->t_ybctid = PointerGetDatum(syscols.ybbasectid);
			}
		}

	}
	pfree(values);
	pfree(nulls);

	return tuple;
}

/*
 * Set up scan plan:
 * 1. map the scan keys to the base table / index columns
 * 2. load table info
 * 3. set up tuple descriptor for the scan keys.
 */
static void
ybcSetupScanPlan(Relation relation, Relation index, YbScanDesc ybScan, YbScanPlan scan_plan)
{
	memset(scan_plan, 0, sizeof(*scan_plan));

	int i, j;

	/*
	 * For regular table scan (or index-only scan like plain table scan), just copy the scan
	 * attribute numbers.
	 */
	if (!index)
	{
		for (i = 0; i < ybScan->nkeys; i++)
		{
			ybScan->sk_attno[i] = ybScan->key[i].sk_attno;
		}

		ybcLoadTableInfo(relation, scan_plan);
		ybScan->tupdesc = RelationGetDescr(relation);
		return;
	}

	/*
	 * If scanning a table using the primary key, change the attribute numbers in the scan key
	 * to the table's column numbers.
	 */
	if (index->rd_index->indisprimary)
	{
		for (i = 0; i < ybScan->nkeys; i++)
		{
			ybScan->sk_attno[i] = index->rd_index->indkey.values[ybScan->key[i].sk_attno - 1];
		}

		ybcLoadTableInfo(relation, scan_plan);
		ybScan->tupdesc = RelationGetDescr(relation);
		return;
	}

	/*
	 * If scanning a table using a local index in the same scan, change the attribute numbers
	 * in the scan key to the index's column numbers.
	 */
	for (i = 0; i < ybScan->nkeys; i++)
	{
		for (j = 0; j < index->rd_index->indnatts; j++)
		{
			if (ybScan->key[i].sk_attno == index->rd_index->indkey.values[j])
			{
				ybScan->sk_attno[i] = j + 1;
				break;
			}
		}
		if (j == index->rd_index->indnatts)
			elog(ERROR, "column is not in index");
	}

	ybcLoadTableInfo(index, scan_plan);
	ybScan->tupdesc = RelationGetDescr(index);
}

static bool ybc_should_pushdown_op(YbScanPlan scan_plan, AttrNumber attnum, int op_strategy)
{
  const int idx = attnum - FirstLowInvalidHeapAttributeNumber;

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
ShouldPushdownScanKey(Relation relation, YbScanPlan scan_plan, AttrNumber attnum, ScanKey key, 
    bool is_search_array_only, bool is_hash_or_primary_key) {
  /*
   * TODO: support the different search options like SK_SEARCHNULL and SK_SEARCHNOTNULL.
   */

  if (IsSystemRelation(relation)) 
  {
    return key->sk_flags == 0 && key->sk_strategy == BTEqualStrategyNumber;
  } 
  else 
  {
    return (key->sk_flags == 0 || (is_search_array_only && is_hash_or_primary_key)) && 
      ybc_should_pushdown_op(scan_plan, attnum, key->sk_strategy);
  }
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

/*
 * Begin a scan of a table or index. When "relation" is a table, an optional local "index" may be
 * given, in which case we will scan using that index co-located with the table (which currently
 * applies to sys catalog table in yb-master only) in the same scan.
 *
 * Alternatively, when scanning an index directly, "relation" points to the index itself and "index"
 * will be NULL.
 */
static YbScanDesc
ybcBeginScan(Relation relation, Relation index, bool index_cols_only, int nkeys, ScanKey key)
{
	if (nkeys > YB_MAX_SCAN_KEYS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_COLUMNS),
				 errmsg("cannot use more than %d predicates in a table or index scan",
						YB_MAX_SCAN_KEYS)));

	/* Set up YugaByte scan description */
	YbScanDesc ybScan = (YbScanDesc) palloc0(sizeof(YbScanDescData));
	ybScan->key   = key;
	ybScan->nkeys = nkeys;
	ybScan->exec_params = NULL;
	ybScan->tableOid = RelationGetRelid(relation);

	/* Setup up the scan plan */
	YbScanPlanData	scan_plan;
	ybcSetupScanPlan(relation, index, ybScan, &scan_plan);

	/*
	 * Find the scan keys that are the primary key.
	 */
	for (int i = 0; i < ybScan->nkeys; i++)
	{
		if (ybScan->sk_attno[i] == InvalidOid)
			break;

		int idx = ybScan->sk_attno[i] - FirstLowInvalidHeapAttributeNumber;
		
    bool is_hash_or_primary_key = (bms_is_member(idx, scan_plan.hash_key) ||
        bms_is_member(idx, scan_plan.primary_key));
    bool is_search_array_only = ((key[i].sk_flags & SK_SEARCHARRAY) == SK_SEARCHARRAY);

    if (!ShouldPushdownScanKey(relation, &scan_plan, ybScan->sk_attno[i], &ybScan->key[i], 
          is_search_array_only, is_hash_or_primary_key))
      continue;

    if (is_hash_or_primary_key)
			scan_plan.sk_cols = bms_add_member(scan_plan.sk_cols, idx);
	}

	/* All or some of the keys should not be pushed down if any of the following is true.
	 * - If hash key is not fully set, we must do a full-table scan so we will clear all the scan
	 * keys.
	 * - For RANGE columns, if condition on a precedent column in RANGE is not specified, the
	 * subsequent columns in RANGE are dropped from the optimization.
	 *
	 * Implementation Notes:
	 * Because internally, hash and range columns are cached and stored prior to other columns in
	 * YugaByte, the columns' indexes are different from the columns' attnum.
	 * Example:
	 *   CREATE TABLE tab(i int, j int, k int, primary key(k HASH, j ASC))
	 *   Column k's index is 1, but its attnum is 3.
	 *
	 * Additionally, we currently have the following setup.
	 * - For PRIMARY KEY SCAN, the key is specified by columns' attnums by both Postgres and YugaByte
	 *   code components.
	 * - For SECONDARY INDEX SCAN and INDEX-ONLY SCAN, column_attnums and column_indexes are
	 *   identical, so they can be both used interchangeably. This is because of CREATE_INDEX
	 *   syntax rules enforce that HASH columns are specified before RANGE columns which comes
	 *   before INCLUDE columns.
	 * - For SYSTEM SCAN, Postgres's layer use attnums to specify a catalog INDEX, but YugaByte
	 *   layer is using column_indexes to specify them.
	 * - For SEQUENTIAL SCAN, column_attnums and column_indexes are the same.
	 *
	 * TODO(neil) The above differences between different INDEX code path should be changed so that
	 * different kinds of indexes and scans share the same behavior.
	 */
	bool delete_key = !bms_is_subset(scan_plan.hash_key, scan_plan.sk_cols);
	if (index && index->rd_index->indisprimary) {
		/* For primary key, column_attnums are used, so we process it different from other scans */
		for (int i = 0; i < index->rd_index->indnatts; i++) {
			int key_column = index->rd_index->indkey.values[i] - FirstLowInvalidHeapAttributeNumber;
			if (!delete_key && !bms_is_member(key_column, scan_plan.sk_cols)) {
				delete_key = true;
			}

			if (delete_key)
				bms_del_member(scan_plan.sk_cols, key_column);
		}
	} else {
		for (int idx = 0; idx <= ybScan->tupdesc->natts - FirstLowInvalidHeapAttributeNumber; idx++)
		{
			if (!delete_key &&
					bms_is_member(idx, scan_plan.primary_key) &&
					!bms_is_member(idx, scan_plan.sk_cols))
				delete_key = true;

			if (delete_key)
				bms_del_member(scan_plan.sk_cols, idx);
		}
	}

	Oid		dboid    = YBCGetDatabaseOid(relation);
	Oid		relid    = RelationGetRelid(relation);
	bool	useIndex = (index && !index->rd_index->indisprimary);
	Oid		index_id = useIndex ? RelationGetRelid(index) : InvalidOid;

	HandleYBStatus(YBCPgNewSelect(ybc_pg_session, dboid, relid, index_id,
	                              &ybScan->handle));
	ResourceOwnerEnlargeYugaByteStmts(CurrentResourceOwner);
	ResourceOwnerRememberYugaByteStmt(CurrentResourceOwner, ybScan->handle);
	ybScan->stmt_owner = CurrentResourceOwner;

  if (IsSystemRelation(relation))
  {
    /* Bind the scan keys */
    for (int i = 0; i < ybScan->nkeys; i++)
    {
      int idx = ybScan->sk_attno[i] - FirstLowInvalidHeapAttributeNumber;

      bool is_hash_or_primary_key = (bms_is_member(idx, scan_plan.hash_key) ||
          bms_is_member(idx, scan_plan.primary_key));
      bool is_search_array_only = ((key[i].sk_flags & SK_SEARCHARRAY) == SK_SEARCHARRAY);

      if (!ShouldPushdownScanKey(relation, &scan_plan, ybScan->sk_attno[i], &ybScan->key[i], 
            is_search_array_only, is_hash_or_primary_key))
        continue;

      if (bms_is_member(idx, scan_plan.sk_cols))
        ybcBindColumn(ybScan, useIndex, ybScan->sk_attno[i], key[i].sk_argument);
    }
  }
  else
  {
    /* Find max number of cols in schema in use in query */
    int max_idx = 0;
    for (int i = 0; i < ybScan->nkeys; i++)
    {
      int idx = ybScan->sk_attno[i] - FirstLowInvalidHeapAttributeNumber;

      if (!bms_is_member(idx, scan_plan.sk_cols))
        continue;

      if (max_idx < idx)
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

    Datum start[max_idx]; /* VLA - scratch space */
    Datum end[max_idx]; /* VLA - scratch space */

    /* find an order of relevant keys such that for the same column, an EQUAL 
     * condition is encountered before IN or BETWEEN. is_column_bound is then used 
     * to establish priority order EQUAL > IN > BETWEEN. */

    int noffsets = 0;
    int offsets[ybScan->nkeys + 1]; /* VLA - scratch space: +1 to avoid zero elements */

    for (int i = 0; i < ybScan->nkeys; i++)
    {
      int idx = ybScan->sk_attno[i] - FirstLowInvalidHeapAttributeNumber;

      bool is_hash_or_primary_key = (bms_is_member(idx, scan_plan.hash_key) ||
          bms_is_member(idx, scan_plan.primary_key));
      bool is_search_array_only = ((key[i].sk_flags & SK_SEARCHARRAY) == SK_SEARCHARRAY);

      if (!ShouldPushdownScanKey(relation, &scan_plan, ybScan->sk_attno[i], &ybScan->key[i], 
            is_search_array_only, is_hash_or_primary_key))
        continue;

      if (!bms_is_member(idx, scan_plan.sk_cols))
        continue;

      switch (ybScan->key[i].sk_strategy)
      {
        case BTEqualStrategyNumber:
          if (ybScan->key[i].sk_flags == 0)
            /* Use a -ve value so that qsort places EQUAL before others */
            offsets[noffsets++] = -i;
          else if (is_search_array_only && !IsSystemRelation(relation) && 
              is_hash_or_primary_key)
            offsets[noffsets++] = i;
          break;

        case BTGreaterEqualStrategyNumber:
        case BTGreaterStrategyNumber:
        case BTLessStrategyNumber:
        case BTLessEqualStrategyNumber:
          offsets[noffsets++] = i;

        default:
          break; /* unreachable */
      }
    }

    qsort(offsets, noffsets, sizeof(int), int_compar_cb);
    /* restore -ve offsets to +ve */
    for (int i = 0; i < noffsets; i++)
      if (offsets[i] < 0)
        offsets[i] = -offsets[i];
      else
        break;

    /* Bind keys for EQUALS and IN */
    for (int k = 0; k < noffsets; k++)
    {
      int i = offsets[k];
      int idx = ybScan->sk_attno[i] - FirstLowInvalidHeapAttributeNumber;

      /* Do not bind more than one condition to a column */
      if (is_column_bound[idx])
        continue;

      bool is_hash_key = bms_is_member(idx, scan_plan.hash_key);
      bool is_hash_or_primary_key = is_hash_key || bms_is_member(idx, scan_plan.primary_key);
      bool is_search_array_only = ((key[i].sk_flags & SK_SEARCHARRAY) == SK_SEARCHARRAY);

      switch (ybScan->key[i].sk_strategy)
      {
        case BTEqualStrategyNumber:
          /* Bind the scan keys */
          if (ybScan->key[i].sk_flags == 0) {
            ybcBindColumnCondEq(ybScan, is_hash_key, ybScan->sk_attno[i], key[i].sk_argument);
            is_column_bound[idx] = true; 
          } else if (is_search_array_only && !IsSystemRelation(relation) && 
              is_hash_or_primary_key) {

            /* based on _bt_preprocess_array_keys() */

            ArrayType  *arrayval;
            int16		elmlen;
            bool		elmbyval;
            char		elmalign;
            int			num_elems;
            Datum	   *elem_values;
            bool	   *elem_nulls;
            int			num_nonnulls;
            int			j;

            ScanKey cur = &key[i];

            /*
             * First, deconstruct the array into elements.  Anything allocated
             * here (including a possibly detoasted array value) is in the
             * workspace context.
             */
            arrayval = DatumGetArrayTypeP(cur->sk_argument);
            /* We could cache this data, but not clear it's worth it */
            get_typlenbyvalalign(ARR_ELEMTYPE(arrayval),
                &elmlen, &elmbyval, &elmalign);
            deconstruct_array(arrayval,
                ARR_ELEMTYPE(arrayval),
                elmlen, elmbyval, elmalign,
                &elem_values, &elem_nulls, &num_elems);

            /*
             * Compress out any null elements.  We can ignore them since we assume
             * all btree operators are strict.
             */
            num_nonnulls = 0;
            for (j = 0; j < num_elems; j++)
            {
              if (!elem_nulls[j])
                elem_values[num_nonnulls++] = elem_values[j];
            }

            /* We could pfree(elem_nulls) now, but not worth the cycles */

            /* If there's no non-nulls, the scan qual is unsatisfiable */
            /* TODO(rajukumaryb): when num_nonnulls is zero, the query should not be 
             * sent to DocDB as it will return rows that will all be dropped. 
             * Example: SELECT ... FROM ... WHERE h = ... AND r IN (NULL,NULL); */
            if (num_nonnulls == 0)
              break;

            /* Build temporary vars */
            IndexScanDescData tmp_scan_desc;
            memset(&tmp_scan_desc, 0, sizeof(IndexScanDescData));
            tmp_scan_desc.indexRelation = index;

            /*
             * Sort the non-null elements and eliminate any duplicates.  We must
             * sort in the same ordering used by the index column, so that the
             * successive primitive indexscans produce data in index order.
             */
            num_elems = _bt_sort_array_elements(&tmp_scan_desc, cur,
                false, elem_values, num_nonnulls);

            /*
             * And set up the BTArrayKeyInfo data.
             */
            ybcBindColumnCondIn(ybScan, ybScan->sk_attno[i], num_elems, elem_values);
            is_column_bound[idx] = true; 
          } else {
            /* unreachable */
          }
          break;

        case BTGreaterEqualStrategyNumber:
        case BTGreaterStrategyNumber:
          if (start_valid[idx]) {
            /* take max of old value and new value */
            bool is_gt = DatumGetBool(FunctionCall2Coll(&key[i].sk_func, key[i].sk_collation, 
                  start[idx], key[i].sk_argument));
            if (!is_gt) {
              start[idx] = key[i].sk_argument;
            }
          } 
          else 
          {
            start[idx] = key[i].sk_argument;
            start_valid[idx] = true;
          }
          break;

        case BTLessStrategyNumber:
        case BTLessEqualStrategyNumber:
          if (end_valid[idx]) 
          {
            /* take min of old value and new value */
            bool is_lt = DatumGetBool(FunctionCall2Coll(&key[i].sk_func, key[i].sk_collation, 
                  end[idx], key[i].sk_argument));
            if (!is_lt) {
              end[idx] = key[i].sk_argument;
            }
          } 
          else 
          {
            end[idx] = key[i].sk_argument;
            end_valid[idx] = true;
          }
          break;

        default:
          break; /* unreachable */
      }
    }

    /* Bind keys for BETWEEN */
    for (int idx = 1 - FirstLowInvalidHeapAttributeNumber; idx < max_idx; idx++)
    {
      /* Do not bind more than one condition to a column */
      if (is_column_bound[idx]) 
        continue;

      if (!start_valid[idx] && !end_valid[idx])
        continue;

      ybcBindColumnCondBetween(ybScan, idx + FirstLowInvalidHeapAttributeNumber, 
          start_valid[idx], start[idx], end_valid[idx], end[idx]);
      is_column_bound[idx] = true; 
    }
  }

	/* If scanning with the primary key, switch the attribute numbers in the scan keys
	 * to the table's column numbers. Switch the tuple descriptor to the table's tuple
	 * to set up the target columns.
	 */
	bool indisprimary = (index && index->rd_index->indisprimary);
	for (int i = 0; i < ybScan->nkeys; i++)
	{
		ybScan->sk_attno[i] = indisprimary ? index->rd_index->indkey.values[key[i].sk_attno - 1]
				                           : key[i].sk_attno;
	}
	ybScan->tupdesc = RelationGetDescr(relation);

	/*
	 * Set up the scan targets. If the table is indexed and only the indexed columns should be
	 * returned, fetch just those columns. Otherwise, fetch all "real" columns.
	 */
	if (relation->rd_rel->relhasoids)
		ybcAddTargetColumn(ybScan, ObjectIdAttributeNumber);

	if (index != NULL && index_cols_only)
	{
		for (int i = 0; i < index->rd_index->indnatts; i++)
		{
			ybcAddTargetColumn(ybScan, index->rd_index->indkey.values[i]);
		}
	}
	else
	{
		for (AttrNumber attnum = 1; attnum <= ybScan->tupdesc->natts; attnum++)
		{
			ybcAddTargetColumn(ybScan, attnum);
		}
	}

	/*
	 * If it is an index, select the ybbasectid for index scan. Otherwise,
	 * include ybctid column also for building indexes.
	 */
	if (relation->rd_index)
		ybcAddTargetColumn(ybScan, YBIdxBaseTupleIdAttributeNumber);
	else
		ybcAddTargetColumn(ybScan, YBTupleIdAttributeNumber);

	/*
	 * Set the current syscatalog version (will check that we are up to date).
	 * Avoid it for syscatalog tables so that we can still use this for
	 * refreshing the caches when we are behind.
	 * Note: This works because we do not allow modifying schemas (alter/drop)
	 * for system catalog tables.
	 */
	if (!IsSystemRelation(relation))
	{
		HandleYBStmtStatusWithOwner(YBCPgSetCatalogCacheVersion(ybScan->handle,
		                                                        yb_catalog_cache_version),
		                            ybScan->handle,
		                            ybScan->stmt_owner);
	}

	bms_free(scan_plan.hash_key);
	bms_free(scan_plan.primary_key);
	bms_free(scan_plan.sk_cols);

	return ybScan;
}

static void ybcEndScan(YbScanDesc ybScan)
{
	if (ybScan->handle)
	{
		HandleYBStatus(YBCPgDeleteStatement(ybScan->handle));
		ResourceOwnerForgetYugaByteStmt(ybScan->stmt_owner, ybScan->handle);
	}
	pfree(ybScan);
}

static bool
heaptuple_matches_key(HeapTuple tup,
					  TupleDesc tupdesc,
					  int nkeys,
					  ScanKey key,
					  AttrNumber sk_attno[],
					  bool *recheck)
{
	*recheck = false;

	for (int i = 0; i < nkeys; i++)
	{
		if (sk_attno[i] == InvalidOid)
			break;

		bool  is_null = false;
		Datum res_datum = heap_getattr(tup, sk_attno[i], tupdesc, &is_null);

		if (key[i].sk_flags & SK_SEARCHNULL)
		{
			if (is_null)
				continue;
			else
				return false;
		}

		if (key[i].sk_flags & SK_SEARCHNOTNULL)
		{
			if (!is_null)
				continue;
			else
				return false;
		}

		/*
		 * TODO: support the different search options like SK_SEARCHARRAY.
		 */
		if (key[i].sk_flags != 0)
		{
			*recheck = true;
			continue;
		}

		if (is_null)
			return false;

		bool matches = DatumGetBool(FunctionCall2Coll(&key[i].sk_func,
		                                              key[i].sk_collation,
		                                              res_datum,
		                                              key[i].sk_argument));
		if (!matches)
			return false;
	}

	return true;
}

static bool
indextuple_matches_key(IndexTuple tup,
					   TupleDesc tupdesc,
					   int nkeys,
					   ScanKey key,
					   AttrNumber sk_attno[],
					   bool *recheck)
{
	*recheck = false;

	for (int i = 0; i < nkeys; i++)
	{
		if (sk_attno[i] == InvalidOid)
			break;

		bool  is_null = false;
		Datum res_datum = index_getattr(tup, sk_attno[i], tupdesc, &is_null);

		if (key[i].sk_flags & SK_SEARCHNULL)
		{
			if (is_null)
				continue;
			else
				return false;
		}

		if (key[i].sk_flags & SK_SEARCHNOTNULL)
		{
			if (!is_null)
				continue;
			else
				return false;
		}

		/*
		 * TODO: support the different search options like SK_SEARCHARRAY.
		 */
		if (key[i].sk_flags != 0)
		{
			*recheck = true;
			continue;
		}

		if (is_null)
			return false;

		bool matches = DatumGetBool(FunctionCall2Coll(&key[i].sk_func,
		                                              key[i].sk_collation,
		                                              res_datum,
		                                              key[i].sk_argument));
		if (!matches)
			return false;
	}

	return true;
}

HeapScanDesc ybc_heap_beginscan(Relation relation,
                                Snapshot snapshot,
                                int nkeys,
                                ScanKey key,
                                bool temp_snap)
{
	// Restart should not be prevented if operation caused by system read of system table.
	YbScanDesc ybScan = ybcBeginScan(relation, NULL /* index */, false /* index_cols_only */,
	                                 nkeys, key);

	/* Set up Postgres sys table scan description */
	HeapScanDesc scan_desc = (HeapScanDesc) palloc0(sizeof(HeapScanDescData));
	scan_desc->rs_rd        = relation;
	scan_desc->rs_snapshot  = snapshot;
	scan_desc->rs_temp_snap = temp_snap;
	scan_desc->rs_cblock    = InvalidBlockNumber;
	scan_desc->ybscan       = ybScan;

	return scan_desc;
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
	}

	YbScanDesc ybScan = ybcBeginScan(relation, index, false /* index_cols_only */, nkeys, key);

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

void ybc_pkey_beginscan(Relation relation,
						Relation index,
						IndexScanDesc scan_desc,
						int nkeys,
						ScanKey key)
{
	/* For rescan, end the previous scan. */
	if (scan_desc->opaque)
	{
		ybc_pkey_endscan(scan_desc);
		scan_desc->opaque = NULL;
	}

	/*
	 * In YugaByte, every table is organized by its primary key. Therefore, if we are scanning
	 * the primary key, look up the base table to prepare scanning it directly.
	 */
	Assert(index->rd_index->indisprimary);

	YbScanDesc ybScan = ybcBeginScan(relation, index, scan_desc->xs_want_itup /* index_cols_only */,
	                                 nkeys, key);
	ybScan->index = index;

	scan_desc->opaque = ybScan;
}

void ybc_index_beginscan(Relation index,
						 IndexScanDesc scan_desc,
						 int nkeys,
						 ScanKey key)
{
	/* For rescan, end the previous scan. */
	if (scan_desc->opaque)
	{
		ybc_index_endscan(scan_desc);
		scan_desc->opaque = NULL;
	}

	/*
	 * Scan the index directly as if we are scanning a table. Passing "index_cols_only" as false
	 * because we are scanning the index directly, not scanning base table with an index.
	 */
	YbScanDesc ybScan = ybcBeginScan(index /* relation */, NULL /* index */,
	                                 false /* index_cols_only */, nkeys, key);
	ybScan->index = index;

	scan_desc->opaque = ybScan;
}

static HeapTuple ybc_getnext_heaptuple(YbScanDesc ybScan, bool is_forward_scan, bool *recheck)
{
	int         nkeys    = ybScan->nkeys;
	ScanKey     key      = ybScan->key;
	AttrNumber *sk_attno = ybScan->sk_attno;
	HeapTuple   tup      = NULL;

	/*
	 * YB Scan may not be able to push down the scan key condition so we may
	 * need additional filtering here.
	 */
	while (HeapTupleIsValid(tup = ybcFetchNextHeapTuple(ybScan, is_forward_scan)))
	{
		if (heaptuple_matches_key(tup, ybScan->tupdesc, nkeys, key, sk_attno, recheck))
			return tup;

		heap_freetuple(tup);
	}

	return NULL;
}

static IndexTuple ybc_getnext_indextuple(YbScanDesc ybScan, bool is_forward_scan, bool *recheck)
{
	int         nkeys    = ybScan->nkeys;
	ScanKey     key      = ybScan->key;
	AttrNumber *sk_attno = ybScan->sk_attno;
	Relation    index    = ybScan->index;
	IndexTuple  tup      = NULL;

	/*
	 * YB Scan may not be able to push down the scan key condition so we may
	 * need additional filtering here.
	 */
	while (PointerIsValid(tup = ybcFetchNextIndexTuple(ybScan, index, is_forward_scan)))
	{
		if (indextuple_matches_key(tup, RelationGetDescr(index), nkeys, key, sk_attno, recheck))
			return tup;
 
		pfree(tup);
	}

	return NULL;
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

HeapTuple ybc_systable_getnext(SysScanDesc scan_desc)
{
	bool recheck = false;

	Assert(PointerIsValid(scan_desc->ybscan));

	HeapTuple tuple = ybc_getnext_heaptuple(scan_desc->ybscan, true /* is_forward_scan */,
											&recheck);
	
	Assert(!recheck);

	return tuple;
}

HeapTuple ybc_pkey_getnext(IndexScanDesc scan_desc, bool is_forward_scan)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	ybscan->exec_params = scan_desc->yb_exec_params;
	Assert(PointerIsValid(ybscan));

	return ybc_getnext_heaptuple(ybscan, is_forward_scan, &scan_desc->xs_recheck);
}

IndexTuple ybc_index_getnext(IndexScanDesc scan_desc, bool is_forward_scan)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	ybscan->exec_params = scan_desc->yb_exec_params;
	Assert(PointerIsValid(ybscan));

	return ybc_getnext_indextuple(ybscan, is_forward_scan, &scan_desc->xs_recheck);
}

void ybc_heap_endscan(HeapScanDesc scan_desc)
{
	Assert(PointerIsValid(scan_desc->ybscan));
	ybcEndScan(scan_desc->ybscan);
	if (scan_desc->rs_temp_snap)
		UnregisterSnapshot(scan_desc->rs_snapshot);
	pfree(scan_desc);
}

void ybc_systable_endscan(SysScanDesc scan_desc)
{
	Assert(PointerIsValid(scan_desc->ybscan));
	ybcEndScan(scan_desc->ybscan);
}

void ybc_pkey_endscan(IndexScanDesc scan_desc)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));
	ybcEndScan(ybscan);
}

void ybc_index_endscan(IndexScanDesc scan_desc)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));
	ybcEndScan(ybscan);
}

/* --------------------------------------------------------------------------------------------- */

void ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
					 Cost *startup_cost, Cost *total_cost)
{
	Cost cpu_per_tuple = cpu_tuple_cost * 10 + baserel->baserestrictcost.per_tuple;

	*startup_cost = baserel->baserestrictcost.startup;
	*total_cost   = *startup_cost + cpu_per_tuple * baserel->rows * selectivity;
}

void ybcIndexCostEstimate(IndexPath *path, Selectivity *selectivity,
						  Cost *startup_cost, Cost *total_cost)
{
	Relation	index = RelationIdGetRelation(path->indexinfo->indexoid);
	bool		isprimary = index->rd_index->indisprimary;
	Relation	relation = isprimary ? RelationIdGetRelation(index->rd_index->indrelid) : NULL;
	RelOptInfo *baserel = path->path.parent;
	List	   *qinfos;
	ListCell   *lc;

	YbScanPlanData	scan_plan;
	memset(&scan_plan, 0, sizeof(scan_plan));
	ybcLoadTableInfo(isprimary ? relation : index, &scan_plan);

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
		Oid			clause_op;
		int			op_strategy;

		/* TODO: support array search condition */
		if (IsA(clause, ScalarArrayOpExpr))
			continue;

    if (IsA(clause, NullTest))
      ybcAddAttributeColumn(&scan_plan, attnum);
    else
    {
      clause_op = qinfo->clause_op;

      if (OidIsValid(clause_op))
      {
        op_strategy = get_op_opfamily_strategy(clause_op,
                                               path->indexinfo->opfamily[qinfo->indexcol]);
        Assert(op_strategy != 0);  /* not a member of opfamily?? */

        if (ybc_should_pushdown_op(&scan_plan, attnum, op_strategy))
          ybcAddAttributeColumn(&scan_plan, attnum);
      }
    }
	}

	/*
	 * If there is no search condition, or not all of the hash columns have search conditions, it
	 * will be a full-table scan. Otherwise, it will be either a primary key lookup or range scan
	 * on a hash key.
	 */
	if (bms_is_empty(scan_plan.sk_cols) || !bms_is_subset(scan_plan.hash_key, scan_plan.sk_cols))
	{
		*selectivity = YBC_FULL_SCAN_SELECTIVITY;
	}
	else
	{
		*selectivity = bms_is_subset(scan_plan.primary_key, scan_plan.sk_cols)
						? YBC_SINGLE_KEY_SELECTIVITY
						: YBC_HASH_SCAN_SELECTIVITY;
	}
	path->path.rows = baserel->rows * (*selectivity);

	ybcCostEstimate(baserel, *selectivity, startup_cost, total_cost);

	if (relation)
		RelationClose(relation);

	RelationClose(index);
}

HeapTuple YBCFetchTuple(Relation relation, Datum ybctid)
{
	YBCPgStatement ybc_stmt;
	TupleDesc      tupdesc = RelationGetDescr(relation);

	HandleYBStatus(YBCPgNewSelect(ybc_pg_session,
								  YBCGetDatabaseOid(relation),
								  RelationGetRelid(relation),
								  InvalidOid,
								  &ybc_stmt));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(ybc_stmt,
										   BYTEAOID,
										   ybctid,
										   false);
	HandleYBStmtStatus(YBCPgDmlBindColumn(ybc_stmt,
										  YBTupleIdAttributeNumber,
										  ybctid_expr), ybc_stmt);

	/*
	 * Set up the scan targets. For index-based scan we need to return all "real" columns.
	 */
	if (RelationGetForm(relation)->relhasoids)
	{
		YBCPgTypeAttrs type_attrs = { 0 };
		YBCPgExpr   expr = YBCNewColumnRef(ybc_stmt, ObjectIdAttributeNumber, InvalidOid,
										   &type_attrs);
		HandleYBStmtStatus(YBCPgDmlAppendTarget(ybc_stmt, expr), ybc_stmt);
	}
	for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		Form_pg_attribute att = TupleDescAttr(tupdesc, attnum - 1);
		YBCPgTypeAttrs type_attrs = { att->atttypmod };
		YBCPgExpr   expr = YBCNewColumnRef(ybc_stmt, attnum, att->atttypid, &type_attrs);
		HandleYBStmtStatus(YBCPgDmlAppendTarget(ybc_stmt, expr), ybc_stmt);
	}
	YBCPgTypeAttrs type_attrs = { 0 };
	YBCPgExpr   expr = YBCNewColumnRef(ybc_stmt, YBTupleIdAttributeNumber, InvalidOid,
									   &type_attrs);
	HandleYBStmtStatus(YBCPgDmlAppendTarget(ybc_stmt, expr), ybc_stmt);

	/* Execute the select statement.
	 * This select statement fetch the row for a specific YBCTID, LIMIT setting is not needed.
	 */
	HandleYBStmtStatus(YBCPgExecSelect(ybc_stmt, NULL /* exec_params */), ybc_stmt);

	HeapTuple tuple    = NULL;
	bool      has_data = false;

	Datum           *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool            *nulls  = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Fetch one row. */
	HandleYBStmtStatus(YBCPgDmlFetch(ybc_stmt,
									 tupdesc->natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data),
					   ybc_stmt);

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

	/* Complete execution */
	HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));

	return tuple;
}
