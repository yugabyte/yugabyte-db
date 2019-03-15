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

static YbSysScanDesc
setup_ybcscan_from_scankey(Relation relation,
						   Relation index,
						   int nkeys,
						   ScanKey key)
{
	TupleDesc  tupdesc       = RelationGetDescr(relation);
	TupleDesc  where_tupdesc = NULL;
	List       *where_cond   = NIL;
	List       *target_attrs = NIL;
	AttrNumber index_attno[CATCACHE_MAXKEYS];

	/*
	 * If the scan uses an index, change attribute numbers to be index column numbers.
	 */
	if (index)
	{
		/*
		 * Scan with index is only used in sys catalog cache currently. Make sure the
		 * number of scan keys does not exceed the allocated size.
		 */
		Assert(nkeys <= CATCACHE_MAXKEYS);

		int	i, j;

		for (i = 0; i < nkeys; i++)
		{
			for (j = 0; j < index->rd_index->indnatts; j++)
			{
				if (key[i].sk_attno == index->rd_index->indkey.values[j])
				{
					index_attno[i] = j + 1;
					break;
				}
			}
			if (j == index->rd_index->indnatts)
				elog(ERROR, "column is not in index");
		}
		where_tupdesc = RelationGetDescr(index);
	}
	else
	{
		where_tupdesc = RelationGetDescr(relation);
	}

	/*
	 * Set up the where clause condition (for the YB scan).
	 */
	for (int i = 0; i < nkeys; i++)
	{
		AttrNumber sk_attno = index ? index_attno[i] : key[i].sk_attno;

		if (sk_attno == InvalidOid)
			break;

		/*
		 * TODO: support the different search options like SK_SEARCHARRAY and SK_SEARCHNULL,
		 *  SK_SEARCHNOTNULL.
		 */
		if (key[i].sk_flags != 0)
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("WHERE condition option %d not supported yet", key[i].sk_flags),
							errdetail("The WHERE condition option is not supported yet."),
							errhint("Rewrite the condition differently.")));

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
		lhs->varattno = sk_attno;
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
	 * Set up the scan targets, for catalog tables always all "real" columns.
	 */
	if (relation->rd_rel->relhasoids)
	{
		TargetEntry *target = makeNode(TargetEntry);
		target->resno = ObjectIdAttributeNumber;
		target_attrs = lappend(target_attrs, target);
	}
	for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		TargetEntry *target = makeNode(TargetEntry);
		target->resno = attnum;
		target_attrs = lappend(target_attrs, target);
	}

	/*
	 * If it is an index, select the ybbasectid for index scan. Otherwise,
	 * include ybctid column also for building indexes.
	 */
	TargetEntry *target = makeNode(TargetEntry);
	target->resno = (relation->rd_index != NULL) ? YBBaseTupleIdAttributeNumber
			                                     : YBTupleIdAttributeNumber;
	target_attrs = lappend(target_attrs, target);

	/* Set up YugaByte system table description */
	YbSysScanDesc ybScan = (YbSysScanDesc) palloc0(sizeof(YbSysScanDescData));
	ybScan->state = ybcBeginScan(relation, index, target_attrs, where_cond);
	ybScan->key = key;
	ybScan->nkeys = nkeys;
	return ybScan;
}


static bool
tuple_matches_key(HeapTuple tup, TupleDesc tupdesc, int nkeys, ScanKey key)
{
	for (int i = 0; i < nkeys; i++)
	{
		if (key[i].sk_attno == InvalidOid)
		{
			break;
		}

		bool  is_null = false;
		Datum res_datum = heap_getattr(tup, key[i].sk_attno, tupdesc, &is_null);

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
	YbSysScanDesc ybScan = setup_ybcscan_from_scankey(relation, NULL /* index */, nkeys, key);

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
	 * Look up the index to scan with if we can.
	 */
	if (indexOK && !IgnoreSystemIndexes && !ReindexIsProcessingIndex(indexId) &&
		indexId != YBSysTablePrimaryKeyOid(RelationGetRelid(relation)))
	{
		index = RelationIdGetRelation(indexId);
	}

	YbSysScanDesc ybScan = setup_ybcscan_from_scankey(relation, index, nkeys, key);

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

void ybc_index_beginscan(Relation relation,
						 IndexScanDesc scan_desc,
						 int nkeys,
						 ScanKey key)
{
	/*
	 * For rescan, end the previous scan.
	 */
	if (scan_desc->opaque)
	{
		ybc_index_endscan(scan_desc);
		scan_desc->opaque = NULL;
	}
	scan_desc->opaque = setup_ybcscan_from_scankey(relation, NULL /* index */, nkeys, key);
}

HeapTuple ybc_scan_getnext(YbScanState scan_state,
                           int nkeys,
                           ScanKey key)
{
	HeapTuple ybtp = NULL;
	/*
	 * YB Scan may not be able to push down the scan key condition so we may
	 * need additional filtering here.
	 */
	while (HeapTupleIsValid(ybtp = ybcFetchNext(scan_state)))
	{
		if (tuple_matches_key(ybtp, scan_state->tupleDesc, nkeys, key))
			return ybtp;

		heap_freetuple(ybtp);
	}

	return NULL;
}

HeapTuple ybc_heap_getnext(HeapScanDesc scan_desc)
{
	Assert(PointerIsValid(scan_desc->ybscan));

	YbScanState scan_state = scan_desc->ybscan->state;
	int nkeys = scan_desc->ybscan->nkeys;
	ScanKey key = scan_desc->ybscan->key;

	return ybc_scan_getnext(scan_state, nkeys, key);
}

HeapTuple ybc_systable_getnext(SysScanDesc scan_desc)
{
	Assert(PointerIsValid(scan_desc->ybscan));

	YbScanState scan_state = scan_desc->ybscan->state;
	int nkeys = scan_desc->ybscan->nkeys;
	ScanKey key = scan_desc->ybscan->key;

	return ybc_scan_getnext(scan_state, nkeys, key);
}

HeapTuple ybc_index_getnext(IndexScanDesc scan_desc)
{
	YbSysScanDesc ybscan = (YbSysScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));

	YbScanState scan_state = ybscan->state;
	int nkeys = ybscan->nkeys;
	ScanKey key = ybscan->key;

	return ybc_scan_getnext(scan_state, nkeys, key);
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

void ybc_index_endscan(IndexScanDesc scan_desc)
{
	YbSysScanDesc ybscan = (YbSysScanDesc) scan_desc->opaque;
	Assert(PointerIsValid(ybscan));
	ybcEndScan(ybscan->state);
	pfree(ybscan);
}
