/*-------------------------------------------------------------------------
 *
 * instrument.c
 *	 functions for instrumentation of plan execution
 *
 *
 * Copyright (c) 2001-2018, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/executor/instrument.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>

#include "executor/instrument.h"

BufferUsage pgBufferUsage;
static BufferUsage save_pgBufferUsage;

static void BufferUsageAdd(BufferUsage *dst, const BufferUsage *add);
static void BufferUsageAccumDiff(BufferUsage *dst,
					 const BufferUsage *add, const BufferUsage *sub);


/* Allocate new instrumentation structure(s) */
Instrumentation *
InstrAlloc(int n, int instrument_options)
{
	Instrumentation *instr;

	/* initialize all fields to zeroes, then modify as needed */
	instr = palloc0(n * sizeof(Instrumentation));
	if (instrument_options & (INSTRUMENT_BUFFERS | INSTRUMENT_TIMER))
	{
		bool		need_buffers = (instrument_options & INSTRUMENT_BUFFERS) != 0;
		bool		need_timer = (instrument_options & INSTRUMENT_TIMER) != 0;
		int			i;

		for (i = 0; i < n; i++)
		{
			instr[i].need_bufusage = need_buffers;
			instr[i].need_timer = need_timer;
		}
	}

	return instr;
}

/* Initialize a pre-allocated instrumentation structure. */
void
InstrInit(Instrumentation *instr, int instrument_options)
{
	memset(instr, 0, sizeof(Instrumentation));
	instr->need_bufusage = (instrument_options & INSTRUMENT_BUFFERS) != 0;
	instr->need_timer = (instrument_options & INSTRUMENT_TIMER) != 0;
}

/* Entry to a plan node */
void
InstrStartNode(Instrumentation *instr)
{
	if (instr->need_timer)
	{
		if (INSTR_TIME_IS_ZERO(instr->starttime))
			INSTR_TIME_SET_CURRENT(instr->starttime);
		else
			elog(ERROR, "InstrStartNode called twice in a row");
	}

	/* save buffer usage totals at node entry, if needed */
	if (instr->need_bufusage)
		instr->bufusage_start = pgBufferUsage;
}

/* Exit from a plan node */
void
InstrStopNode(Instrumentation *instr, double nTuples)
{
	instr_time	endtime;

	/* count the returned tuples */
	instr->tuplecount += nTuples;

	/* let's update the time only if the timer was requested */
	if (instr->need_timer)
	{
		if (INSTR_TIME_IS_ZERO(instr->starttime))
			elog(ERROR, "InstrStopNode called without start");

		INSTR_TIME_SET_CURRENT(endtime);
		INSTR_TIME_ACCUM_DIFF(instr->counter, endtime, instr->starttime);

		INSTR_TIME_SET_ZERO(instr->starttime);
	}

	/* Add delta of buffer usage since entry to node's totals */
	if (instr->need_bufusage)
		BufferUsageAccumDiff(&instr->bufusage,
							 &pgBufferUsage, &instr->bufusage_start);

	/* Is this the first tuple of this cycle? */
	if (!instr->running)
	{
		instr->running = true;
		instr->firsttuple = INSTR_TIME_GET_DOUBLE(instr->counter);
	}
}

/* Finish a run cycle for a plan node */
void
InstrEndLoop(Instrumentation *instr)
{
	double		totaltime;

	/* Skip if nothing has happened, or already shut down */
	if (!instr->running)
		return;

	if (!INSTR_TIME_IS_ZERO(instr->starttime))
		elog(ERROR, "InstrEndLoop called on running node");

	/* Accumulate per-cycle statistics into totals */
	totaltime = INSTR_TIME_GET_DOUBLE(instr->counter);

	instr->startup += instr->firsttuple;
	instr->total += totaltime;
	instr->ntuples += instr->tuplecount;
	instr->nloops += 1;

	/* Reset for next cycle (if any) */
	instr->running = false;
	INSTR_TIME_SET_ZERO(instr->starttime);
	INSTR_TIME_SET_ZERO(instr->counter);
	instr->firsttuple = 0;
	instr->tuplecount = 0;
}

/* aggregate instrumentation information held in a YbRpcStats structure */
static void
InstrAggYbPgRpcStats(YbPgRpcStats *dst, YbPgRpcStats *add)
{
	dst->count += add->count;
	dst->rows_scanned += add->rows_scanned;
	dst->wait_time += add->wait_time;
}

static void
YbInstrAggRpcMetrics(YBCPgExecStorageMetrics **dst, YBCPgExecStorageMetrics *add)
{
	if (!add)
		return;

	/* Aggregate metrics */
	if (add->version == 0)
		return;

	if (*dst == NULL)
		*dst = (YBCPgExecStorageMetrics *) palloc0(sizeof(YBCPgExecStorageMetrics));

	for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; i++)
		(*dst)->gauges[i] += add->gauges[i];

	for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; i++)
		(*dst)->counters[i] += add->counters[i];

	for (int i = 0; i < YB_STORAGE_EVENT_COUNT; i++)
	{
		YBCPgExecEventMetric *add_event = &add->events[i];
		YBCPgExecEventMetric *dst_event = &(*dst)->events[i];
		dst_event->sum += add_event->sum;
		dst_event->count += add_event->count;
	}

	(*dst)->version += add->version;
}

/* aggregate instrumentation information */
void
InstrAggNode(Instrumentation *dst, Instrumentation *add)
{
	if (!dst->running && add->running)
	{
		dst->running = true;
		dst->firsttuple = add->firsttuple;
	}
	else if (dst->running && add->running && dst->firsttuple > add->firsttuple)
		dst->firsttuple = add->firsttuple;

	INSTR_TIME_ADD(dst->counter, add->counter);

	dst->tuplecount += add->tuplecount;
	dst->startup += add->startup;
	dst->total += add->total;
	dst->ntuples += add->ntuples;
	dst->ntuples2 += add->ntuples2;
	dst->nloops += add->nloops;
	dst->nfiltered1 += add->nfiltered1;
	dst->nfiltered2 += add->nfiltered2;

	/* Add delta of buffer usage since entry to node's totals */
	if (dst->need_bufusage)
		BufferUsageAdd(&dst->bufusage, &add->bufusage);

	/* Add Yugabyte specific instrumentation information */
	InstrAggYbPgRpcStats(&dst->yb_instr.tbl_reads, &add->yb_instr.tbl_reads);
	InstrAggYbPgRpcStats(&dst->yb_instr.index_reads, &add->yb_instr.index_reads);
	InstrAggYbPgRpcStats(&dst->yb_instr.catalog_reads, &add->yb_instr.catalog_reads);
	InstrAggYbPgRpcStats(&dst->yb_instr.write_flushes, &add->yb_instr.write_flushes);
	dst->yb_instr.tbl_writes += add->yb_instr.tbl_writes;
	dst->yb_instr.index_writes += add->yb_instr.index_writes;
	dst->yb_instr.catalog_writes += add->yb_instr.catalog_writes;

	/* Aggregate metrics */
	YbInstrAggRpcMetrics(&dst->yb_instr.read_metrics, add->yb_instr.read_metrics);
	YbInstrAggRpcMetrics(&dst->yb_instr.write_metrics, add->yb_instr.write_metrics);

	dst->yb_instr.rows_removed_by_recheck += add->yb_instr.rows_removed_by_recheck;
}

/* note current values during parallel executor startup */
void
InstrStartParallelQuery(void)
{
	save_pgBufferUsage = pgBufferUsage;
}

/* report usage after parallel executor shutdown */
void
InstrEndParallelQuery(BufferUsage *result)
{
	memset(result, 0, sizeof(BufferUsage));
	BufferUsageAccumDiff(result, &pgBufferUsage, &save_pgBufferUsage);
}

/* accumulate work done by workers in leader's stats */
void
InstrAccumParallelQuery(BufferUsage *result)
{
	BufferUsageAdd(&pgBufferUsage, result);
}

/* dst += add */
static void
BufferUsageAdd(BufferUsage *dst, const BufferUsage *add)
{
	dst->shared_blks_hit += add->shared_blks_hit;
	dst->shared_blks_read += add->shared_blks_read;
	dst->shared_blks_dirtied += add->shared_blks_dirtied;
	dst->shared_blks_written += add->shared_blks_written;
	dst->local_blks_hit += add->local_blks_hit;
	dst->local_blks_read += add->local_blks_read;
	dst->local_blks_dirtied += add->local_blks_dirtied;
	dst->local_blks_written += add->local_blks_written;
	dst->temp_blks_read += add->temp_blks_read;
	dst->temp_blks_written += add->temp_blks_written;
	INSTR_TIME_ADD(dst->blk_read_time, add->blk_read_time);
	INSTR_TIME_ADD(dst->blk_write_time, add->blk_write_time);
}

/* dst += add - sub */
static void
BufferUsageAccumDiff(BufferUsage *dst,
					 const BufferUsage *add,
					 const BufferUsage *sub)
{
	dst->shared_blks_hit += add->shared_blks_hit - sub->shared_blks_hit;
	dst->shared_blks_read += add->shared_blks_read - sub->shared_blks_read;
	dst->shared_blks_dirtied += add->shared_blks_dirtied - sub->shared_blks_dirtied;
	dst->shared_blks_written += add->shared_blks_written - sub->shared_blks_written;
	dst->local_blks_hit += add->local_blks_hit - sub->local_blks_hit;
	dst->local_blks_read += add->local_blks_read - sub->local_blks_read;
	dst->local_blks_dirtied += add->local_blks_dirtied - sub->local_blks_dirtied;
	dst->local_blks_written += add->local_blks_written - sub->local_blks_written;
	dst->temp_blks_read += add->temp_blks_read - sub->temp_blks_read;
	dst->temp_blks_written += add->temp_blks_written - sub->temp_blks_written;
	INSTR_TIME_ACCUM_DIFF(dst->blk_read_time,
						  add->blk_read_time, sub->blk_read_time);
	INSTR_TIME_ACCUM_DIFF(dst->blk_write_time,
						  add->blk_write_time, sub->blk_write_time);
}
