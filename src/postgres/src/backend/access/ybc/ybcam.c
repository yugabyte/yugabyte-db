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
						   int nkeys,
						   ScanKey key)
{
	TupleDesc tupdesc       = RelationGetDescr(relation);
	List      *where_cond   = NIL;
	List      *target_attrs = NIL;

	/*
	 * Set up the where clause condition (for the YB scan).
	 */
	for (int i  = 0; i < nkeys; i++)
	{
		if (key[i].sk_attno == InvalidOid)
			break;

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
		lhs->varattno = key[i].sk_attno;
		if (lhs->varattno > 0)
		{
			/* Get the type from the description */
			lhs->vartype = tupdesc->attrs[lhs->varattno - 1]->atttypid;
		}
		else
		{
			/* This must be an OID column. */
			lhs->vartype = OIDOID;
		}
		cond->args    = lappend(cond->args, lhs);

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

	/* Set up YugaByte system table description */
	YbSysScanDesc ybScan = (YbSysScanDesc) palloc0(sizeof(YbSysScanDescData));
	ybScan->state = ybcBeginScan(relation, target_attrs, where_cond);
	ybScan->key = key;
	ybScan->nkeys = nkeys;
	return ybScan;
}


static bool
tuple_matches_key(HeapTuple tup,
                  TupleDesc tupdesc,
                  int nkeys,
                  ScanKey key)
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
                                ScanKey key)
{
	YbSysScanDesc ybScan = setup_ybcscan_from_scankey(relation, nkeys, key);

	/* Set up Postgres sys table scan description */
	HeapScanDesc scan_desc = (HeapScanDesc) palloc0(sizeof(HeapScanDescData));
	scan_desc->rs_rd  = relation;
	scan_desc->rs_snapshot = snapshot;
	scan_desc->ybscan = ybScan;

	return scan_desc;
}

SysScanDesc ybc_systable_beginscan(Relation relation,
                                   Oid indexId,
                                   bool first_time,
                                   Snapshot snapshot,
                                   int nkeys,
                                   ScanKey key)
{
	YbSysScanDesc ybScan = setup_ybcscan_from_scankey(relation, nkeys, key);

	/* Set up Postgres sys table scan description */
	SysScanDesc scan_desc = (SysScanDesc) palloc0(sizeof(SysScanDescData));
	scan_desc->heap_rel = relation;
	scan_desc->snapshot = snapshot;
	scan_desc->ybscan   = ybScan;

	return scan_desc;
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

void ybc_heap_endscan(HeapScanDesc scan_desc)
{
	Assert(PointerIsValid(scan_desc->ybscan));
	ybcEndScan(scan_desc->ybscan->state);
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
