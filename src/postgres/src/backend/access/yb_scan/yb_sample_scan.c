/*-------------------------------------------------------------------------
 *
 * yb_sample_scan.c
 *	  YB ANALYZE sampling support.
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
 * src/backend/access/yb_scan/yb_sample_scan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/yb_special_scans.h"
#include "access/yb_target.h"
#include "pg_yb_utils.h"
#include "utils/datum.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/sampling.h"
#include "yb/yql/pggate/ybc_gflags.h"

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
	ybSample->exec_params.rowmark = YBC_NO_ROW_MARK;

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

	MemoryContext perrowcxt = AllocSetContextCreate(CurrentMemoryContext,
													"ybFetchSample row values",
													ALLOCSET_DEFAULT_SIZES);
	MemoryContext samplecxt = CurrentMemoryContext;

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

		MemoryContextSwitchTo(perrowcxt);
		HandleYBStatus(YBCPgDmlFetch(ybSample->handle,
									 tupdesc->natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data));
		MemoryContextSwitchTo(samplecxt);

		if (has_data)
		{
			rows[numrows] = heap_form_tuple(tupdesc, values, nulls);

			if (syscols.ybctid != NULL)
				HEAPTUPLE_YBCTID(rows[numrows]) =
					datumCopy(PointerGetDatum(syscols.ybctid), false, -1);
			rows[numrows]->t_tableOid = relid;
			++numrows;
		}
		MemoryContextReset(perrowcxt);
	}

	if (*YBCGetGFlags()->TEST_delay_after_table_analyze_ms > 0)
	{
		pg_usleep(*YBCGetGFlags()->TEST_delay_after_table_analyze_ms * 1000L);
	}

	MemoryContextDelete(perrowcxt);
	pfree(values);
	pfree(nulls);
	/* Close the DocDB statement */
	YBCPgDeleteStatement(ybSample->handle);
	return numrows;
}
