/*-------------------------------------------------------------------------
 *
 * yb_table_scan.c
 *	  Public YB scan interface: descriptor accessors.
 *
 *	  These thin wrappers let executor nodes manipulate a
 *	  YB-backed TableScanDesc without knowing about internal
 *	  scan types.
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
 * src/backend/access/yb_scan/yb_table_scan.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/yb_scan_core.h"
#include "access/yb_table_scan.h"
#include "access/yb_target.h"
#include "pg_yb_utils.h"

struct YBParallelPartitionKeysData *
yb_scan_desc_pscan(TableScanDesc scan)
{
	return ((YbTableScanDesc) scan)->ybscan->pscan;
}

bool
yb_scan_desc_is_exec_done(TableScanDesc scan)
{
	return ((YbTableScanDesc) scan)->ybscan->is_exec_done;
}

void
yb_scan_desc_set_exec_done(TableScanDesc scan, bool done)
{
	((YbTableScanDesc) scan)->ybscan->is_exec_done = done;
}

void
yb_scan_desc_exec_select(TableScanDesc scan)
{
	YbOpaque	opaque = ((YbTableScanDesc) scan)->ybscan;

	HandleYBStatus(YBCPgExecSelect(opaque->handle, opaque->exec_params));
	opaque->is_exec_done = true;
}

void
yb_scan_desc_fetch_next(TableScanDesc scan, TupleTableSlot *slot, Oid relid)
{
	YbOpaque	opaque = ((YbTableScanDesc) scan)->ybscan;

	ybFetchNext(opaque->handle, slot, relid);
}

/*
 * Obtain the next parallel range and apply it to the scan.  Returns
 * false when no more ranges remain and the worker should stop.
 */
bool
yb_scan_desc_apply_next_parallel_range(TableScanDesc scan)
{
	YbOpaque	opaque = ((YbTableScanDesc) scan)->ybscan;

	return yb_scan_apply_next_parallel_range(opaque->handle,
											 opaque->exec_params,
											 opaque->pscan);
}

int
yb_scan_desc_get_fetch_row_limit(TableScanDesc scan)
{
	return ((YbTableScanDesc) scan)->ybscan->exec_params->yb_fetch_row_limit;
}

int
yb_scan_desc_get_fetch_size_limit(TableScanDesc scan)
{
	return ((YbTableScanDesc) scan)->ybscan->exec_params->yb_fetch_size_limit;
}

void
yb_scan_desc_fetch_ybctids(TableScanDesc scan, YbcConstSliceVector ybctids)
{
	YbOpaque	opaque = ((YbTableScanDesc) scan)->ybscan;

	HandleYBStatus(YBCPgFetchRequestedYbctids(opaque->handle,
											  opaque->exec_params,
											  ybctids));
}

void
yb_scan_desc_apply_primary_pushdown(TableScanDesc scan,
									const YbPushdownExprs *pushdown)
{
	YbOpaque	opaque = ((YbTableScanDesc) scan)->ybscan;

	YbApplyPrimaryPushdown(opaque->handle, pushdown);
}

void
yb_scan_desc_bind_ybctids(TableScanDesc scan, int nybctids, Datum *ybctids)
{
	YbOpaque	opaque = ((YbTableScanDesc) scan)->ybscan;

	HandleYBStatus(YBCPgBindYbctids(opaque->handle, nybctids, ybctids));
}
