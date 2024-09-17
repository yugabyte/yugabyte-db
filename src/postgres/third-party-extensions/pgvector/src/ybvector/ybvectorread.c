/*-------------------------------------------------------------------------
 *
 * ybvectorread.c
 *	  read routines for the Yugabyte vector index access method.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *		third-party-extensions/pgvector/ybvector/ybvectorread.c
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "ybvector.h"

#include "access/genam.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "catalog/pg_type.h"

#include "pgstat.h"
#include "utils/memutils.h"

/*
 * Bind search keys to the ANN scan. These include
 * - the query vector
 * - the prefetch size (how many nearest neighbours we expect to return)
 */
static void bindAnnSearchKeys(IndexScanDesc scan, Relation rel, int nkeys,
							  int norderbys, YbVectorScanOpaque so)
{
	int ind_dim = TupleDescAttr(scan->indexRelation->rd_att, 0)->atttypmod;
	int vec_dim = ((Vector*) scan->orderByData->sk_argument)->dim;
	if (ind_dim != vec_dim)
		ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					errmsg("different vector dimensions %d and %d", ind_dim, vec_dim)));

	so->query_vector = scan->orderByData->sk_argument;
	YBCPgExpr vec_handle = YBCNewConstant(
		so->yb_scan_desc->handle, BYTEAOID, InvalidOid /* collation_id */,
		so->query_vector, false);

	YBCPgDmlANNBindVector(so->yb_scan_desc->handle, vec_handle);
	YBCPgDmlANNSetPrefetchSize(so->yb_scan_desc->handle, so->limit);
}

/*
 * ybvectorbeginscan
 *		Open the scan and initialize its opaque vector scan structure.
 */
IndexScanDesc
ybvectorbeginscan(Relation rel, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	YbVectorScanOpaque so;

	scan = RelationGetIndexScan(rel, nkeys, norderbys);

	/* allocate private workspace */
	so = (YbVectorScanOpaque) palloc(sizeof(YbVectorScanOpaqueData));
	so->limit = -1;

	so->first = true;
	scan->opaque = so;
	return scan;
}

/*
 * ybvectorrescan
 *		Reset temporary structures to prepare for rescan.
 */
void
ybvectorrescan(IndexScanDesc scan, ScanKey scankeys, int nscankeys,
			   ScanKey orderbys, int norderbys)
{
	YbVectorScanOpaque so = scan->opaque;
	if (!so->first)
		ybc_free_ybscan(so->yb_scan_desc);

	YbScanDesc ybScan = ybcBeginScan(scan->heapRelation, scan->indexRelation,
									scan->xs_want_itup, nscankeys, scankeys,
									scan->yb_scan_plan, scan->yb_rel_pushdown,
									scan->yb_idx_pushdown, scan->yb_aggrefs,
									scan->yb_distinct_prefixlen,
									scan->yb_exec_params,
									false /* is_internal_scan */,
									scan->fetch_ybctids_only);
	so->yb_scan_desc = ybScan;

	if (scankeys && scan->numberOfKeys > 0)
		memmove(&scan->keyData, scankeys, scan->numberOfKeys * sizeof(ScanKeyData));

	if (orderbys && scan->numberOfOrderBys > 0)
		memmove(scan->orderByData, orderbys, scan->numberOfOrderBys * sizeof(ScanKeyData));

	if (norderbys > 0)
		bindAnnSearchKeys(scan, scan->heapRelation, nscankeys, norderbys, so);

	so->first = true;
}

bool
ybvectorgettuple(IndexScanDesc scan, ScanDirection dir)
{
	YbVectorScanOpaque so = (YbVectorScanOpaque) scan->opaque;
	so->first = false;
	YbScanDesc ybscan = so->yb_scan_desc;
	ybscan->exec_params = scan->yb_exec_params;
	Assert(ybscan->exec_params != NULL);
	ybscan->exec_params->work_mem = work_mem;

	if (!ybscan->is_exec_done)
		pgstat_count_index_scan(scan->indexRelation);

	/* Lifted from yb_lsm.c ybcingettuple. */
	bool has_tuple = false;
	if (ybscan->prepare_params.index_only_scan)
	{
		IndexTuple tuple =
			ybc_getnext_indextuple(ybscan, dir, &scan->xs_recheck);
		if (tuple)
		{
			scan->xs_itup = tuple;
			scan->xs_itupdesc = RelationGetDescr(scan->indexRelation);
			has_tuple = true;
		}
	}
	else
	{
		HeapTuple tuple =
			ybc_getnext_heaptuple(ybscan, dir, &scan->xs_recheck);
		if (tuple)
		{
			scan->xs_hitup = tuple;
			scan->xs_hitupdesc = RelationGetDescr(scan->heapRelation);
			has_tuple = true;
		}
	}

	scan->xs_recheckorderby = false;

	return has_tuple;
}

/*
 * ybvectorendscan
 *		Close the scan
 */
void
ybvectorendscan(IndexScanDesc scan)
{
	YbVectorScanOpaque so = scan->opaque;

	ybc_free_ybscan(so->yb_scan_desc);
}

/* ybvectormightrecheck
 *	 Assume we'll always recheck this scan.
 *	 TODO(tanuj): Make sure this is false and correct.
 */
bool
ybvectormightrecheck(Relation heapRelation, Relation indexRelation,
					 bool xs_want_itup, ScanKey keys, int nkeys)
{
	return true;
}
