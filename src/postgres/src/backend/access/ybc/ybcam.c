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
#include "executor/ybcScan.h"
#include "miscadmin.h"
#include "nodes/relation.h"
#include "utils/datum.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "utils/resowner_private.h"
#include "utils/syscache.h"
#include "utils/snapmgr.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

static YbScanDesc
setup_ybcscan_from_scankey(Relation relation,
						   Relation index,
						   bool index_cols_only,
						   int nkeys,
						   ScanKey key)
{
	TupleDesc  tupdesc       = RelationGetDescr(relation);
	TupleDesc  where_tupdesc = NULL;
	List       *where_cond   = NIL;
	List       *target_attrs = NIL;
	AttrNumber sk_attno[INDEX_MAX_KEYS * 2]; /* A pair of lower/upper bounds per column max */

	if (nkeys > INDEX_MAX_KEYS * 2)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_COLUMNS),
				 errmsg("cannot use more than %d predicates in a table or index scan",
						INDEX_MAX_KEYS * 2)));

	if (index)
	{
		if (index->rd_index->indisprimary)
		{
			/*
			 * If scanning a table using the primary key, change the attribute numbers in the
			 * scan key to the table's column numbers.
			 */
			for (int i = 0; i < nkeys; i++)
			{
				sk_attno[i] = index->rd_index->indkey.values[key[i].sk_attno - 1];
			}
			where_tupdesc = RelationGetDescr(relation);
		}
		else
		{
			/*
			 * If scanning a table using a local index in the same scan, change the attribute
			 * numbers in the scan key to the index's column numbers.
			 */
			int	i, j;

			for (i = 0; i < nkeys; i++)
			{
				for (j = 0; j < index->rd_index->indnatts; j++)
				{
					if (key[i].sk_attno == index->rd_index->indkey.values[j])
					{
						sk_attno[i] = j + 1;
						break;
					}
				}
				if (j == index->rd_index->indnatts)
					elog(ERROR, "column is not in index");
			}
			where_tupdesc = RelationGetDescr(index);
		}
	}
	else
	{
		/* For regular table / index fetch, just copy the scan attribute numbers. */
		for (int i = 0; i < nkeys; i++)
		{
			sk_attno[i] = key[i].sk_attno;
		}
		where_tupdesc = RelationGetDescr(relation);
	}

	/*
	 * Set up the where clause condition (for the YB scan).
	 */
	for (int i = 0; i < nkeys; i++)
	{
		if (sk_attno[i] == InvalidOid)
			break;

		/*
		 * TODO: support the different search options like SK_SEARCHARRAY and SK_SEARCHNULL,
		 *  SK_SEARCHNOTNULL.
		 */
		if (key[i].sk_flags != 0)
			continue;

		OpExpr *cond = makeNode(OpExpr);
		/*
		 * Note: YugaByte will only use the operator to decide whether it is
		 * an equality operator (which it can handle) or not (we will
		 * need to filter the results below).
		 * So we do not bother finding the correctly typed operator to pass it
		 * to YugaByte here.
		 * TODO: We should clean this up as part of refactoring the scan API
		 * to use common utilities for internal syscatalog scans and FDW scans.
		 */
		if (key[i].sk_strategy == BTEqualStrategyNumber)
			cond->opno = Int4EqualOperator;
		else
			cond->opno = InvalidOid;

		/* Set up column (lhs) */
		Var *lhs = makeNode(Var);
		lhs->varattno = sk_attno[i];
		if (lhs->varattno > 0)
		{
			/* Get the type from the description */
			lhs->vartype = TupleDescAttr(where_tupdesc, lhs->varattno - 1)->atttypid;
		}
		else
		{
			/* This must be an OID column. */
			lhs->vartype = OIDOID;
		}
		cond->args = lappend(cond->args, lhs);

		/* Set up value (rhs) */
		Const *rhs = makeNode(Const);
		rhs->constvalue  = key[i].sk_argument;
		rhs->constisnull = false;
		cond->args       = lappend(cond->args, rhs);

		where_cond = lappend(where_cond, cond);
	}

	/*
	 * Set up the scan targets. If the table is indexed and only the indexed columns should be
	 * returned, fetch just those columns. Otherwise, fetch all "real" columns.
	 */
	if (relation->rd_rel->relhasoids)
	{
		TargetEntry *target = makeNode(TargetEntry);
		target->resno = ObjectIdAttributeNumber;
		target_attrs = lappend(target_attrs, target);
	}
	if (index != NULL && index_cols_only)
	{
		for (int i = 0; i < index->rd_index->indnatts; i++)
		{
			TargetEntry *target = makeNode(TargetEntry);
			target->resno = index->rd_index->indkey.values[i];
			target_attrs = lappend(target_attrs, target);
		}
	}
	else
	{
		for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
		{
			TargetEntry *target = makeNode(TargetEntry);
			target->resno = attnum;
			target_attrs = lappend(target_attrs, target);
		}
	}

	/*
	 * If it is an index, select the ybbasectid for index scan. Otherwise,
	 * include ybctid column also for building indexes.
	 */
	TargetEntry *target = makeNode(TargetEntry);
	target->resno = (relation->rd_index != NULL) ? YBBaseTupleIdAttributeNumber
			                                     : YBTupleIdAttributeNumber;
	target_attrs = lappend(target_attrs, target);

	/* Set up YugaByte scan description. Scan with an index if it is a secondary index */
	bool indisprimary = (index && index->rd_index->indisprimary);
	bool indissecondary = (index && !index->rd_index->indisprimary);
	YbScanDesc ybScan = (YbScanDesc) palloc0(sizeof(YbScanDescData));
	ybScan->state = ybcBeginScan(relation, indissecondary ? index : NULL, target_attrs, where_cond);
	ybScan->key = key;
	ybScan->nkeys = nkeys;
	for (int i = 0; i < nkeys; i++)
	{
		/* If scanning with the primary key, switch the attribute numbers in the scan keys
		 * to the table's column numbers.
		 */
		ybScan->sk_attno[i] = indisprimary ? index->rd_index->indkey.values[key[i].sk_attno - 1]
				                           : key[i].sk_attno;
	}

	return ybScan;
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
	YbScanDesc ybScan = setup_ybcscan_from_scankey(relation, NULL /* index */,
												   false /* index_cols_only */, nkeys, key);

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

	YbScanDesc ybScan = setup_ybcscan_from_scankey(relation, index,
												   false /* index_cols_only */, nkeys, key);

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

	YbScanDesc ybScan = setup_ybcscan_from_scankey(relation, index,
												   scan_desc->xs_want_itup /* index_cols_only */,
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
	YbScanDesc ybScan = setup_ybcscan_from_scankey(index /* relation */, NULL /* index */,
												   false /* index_cols_only */, nkeys, key);
	ybScan->index = index;

	scan_desc->opaque = ybScan;
}

static HeapTuple ybc_getnext_heaptuple(YbScanDesc ybScan, bool *recheck)
{
	YbScanState scan_state = ybScan->state;
	int         nkeys      = ybScan->nkeys;
	ScanKey     key        = ybScan->key;
	AttrNumber *sk_attno   = ybScan->sk_attno;
	HeapTuple   tup = NULL;

	/*
	 * YB Scan may not be able to push down the scan key condition so we may
	 * need additional filtering here.
	 */
	while (HeapTupleIsValid(tup = ybcFetchNextHeapTuple(scan_state)))
	{
		if (heaptuple_matches_key(tup, scan_state->tupleDesc, nkeys, key, sk_attno, recheck))
			return tup;

		heap_freetuple(tup);
	}

	return NULL;
}

static IndexTuple ybc_getnext_indextuple(YbScanDesc ybScan, bool *recheck)
{
	YbScanState scan_state = ybScan->state;
	int         nkeys      = ybScan->nkeys;
	ScanKey     key        = ybScan->key;
	AttrNumber *sk_attno   = ybScan->sk_attno;
	Relation    index      = ybScan->index;
	IndexTuple  tup = NULL;

	/*
	 * YB Scan may not be able to push down the scan key condition so we may
	 * need additional filtering here.
	 */
	while (PointerIsValid(tup = ybcFetchNextIndexTuple(scan_state, index)))
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

	HeapTuple tuple = ybc_getnext_heaptuple(scan_desc->ybscan, &recheck);
	
	Assert(!recheck);

	return tuple;
}

HeapTuple ybc_systable_getnext(SysScanDesc scan_desc)
{
	bool recheck = false;

	Assert(PointerIsValid(scan_desc->ybscan));

	HeapTuple tuple = ybc_getnext_heaptuple(scan_desc->ybscan, &recheck);
	
	Assert(!recheck);

	return tuple;
}

HeapTuple ybc_pkey_getnext(IndexScanDesc scan_desc)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));

	return ybc_getnext_heaptuple(ybscan, &scan_desc->xs_recheck);
}

IndexTuple ybc_index_getnext(IndexScanDesc scan_desc)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));

	return ybc_getnext_indextuple(ybscan, &scan_desc->xs_recheck);
}

void ybc_heap_endscan(HeapScanDesc scan_desc)
{
	Assert(PointerIsValid(scan_desc->ybscan));
	ybcEndScan(scan_desc->ybscan->state);
	if (scan_desc->rs_temp_snap)
		UnregisterSnapshot(scan_desc->rs_snapshot);
	pfree(scan_desc->ybscan);
	pfree(scan_desc);
}

void ybc_systable_endscan(SysScanDesc scan_desc)
{
	Assert(PointerIsValid(scan_desc->ybscan));
	ybcEndScan(scan_desc->ybscan->state);
	pfree(scan_desc->ybscan);
	pfree(scan_desc);
}

void ybc_pkey_endscan(IndexScanDesc scan_desc)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));
	ybcEndScan(ybscan->state);
	pfree(ybscan);
}

void ybc_index_endscan(IndexScanDesc scan_desc)
{
	YbScanDesc ybscan = (YbScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));
	ybcEndScan(ybscan->state);
	pfree(ybscan);
}

/* --------------------------------------------------------------------------------------------- */

HeapTuple YBCFetchTuple(Relation relation, Datum ybctid)
{
	YBCPgStatement ybc_stmt;
	TupleDesc      tupdesc = RelationGetDescr(relation);

	HandleYBStatus(YBCPgNewSelect(ybc_pg_session,
								  YBCGetDatabaseOid(relation),
								  RelationGetRelid(relation),
								  InvalidOid,
								  &ybc_stmt,
								  NULL /* read_time */));

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

	/* Execute the select statement. */
	HandleYBStmtStatus(YBCPgExecSelect(ybc_stmt), ybc_stmt);

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
	}
	pfree(values);
	pfree(nulls);

	/* Complete execution */
	HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));

	return tuple;
}
