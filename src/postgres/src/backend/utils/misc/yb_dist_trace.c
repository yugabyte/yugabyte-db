/*-------------------------------------------------------------------------
 *
 * yb_dist_trace.c
 *	  Distributed tracing/Yugabyte (Postgres layer) executor hooks.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_dist_trace.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "executor/executor.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "pg_yb_utils.h"
#include "utils/portal.h"
#include "yb_dist_trace.h"

static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

static bool
YbDistTraceEndNodeSpans_walker(PlanState *node, void *context)
{
	if (node == NULL)
		return false;

	check_stack_depth();

	/* Post-order traversal to end child spans before the parent's. */
	planstate_tree_walker(node, YbDistTraceEndNodeSpans_walker, context);

	if (node->yb_dist_trace_node_span)
	{
		if (*(bool *) context)
			YBCDistTraceEndNodeSpanOnError(node->yb_dist_trace_node_span);
		else
			YBCDistTraceEndNodeSpan(node->yb_dist_trace_node_span);
		node->yb_dist_trace_node_span = NULL;
	}

	return false;
}

void
YbDistTraceEndNodeSpans(struct PlanState *planstate, bool errored)
{
	YbDistTraceEndNodeSpans_walker((PlanState *) planstate, &errored);
	Assert(planstate && planstate->state);
	planstate->state->yb_dist_trace_has_node_spans = false;
}

static void
YbDistTraceEndPortalNodeSpans(Portal portal)
{
	/* Skip the tree walk for portals that never opened a span this cycle. */
	if (portal->queryDesc && portal->queryDesc->planstate &&
		portal->queryDesc->estate->yb_dist_trace_has_node_spans)
		YbDistTraceEndNodeSpans(portal->queryDesc->planstate, /* errored */ false);
}

/*
 * End node spans still open in suspended portals (cursors, extended-protocol
 * portals with a row limit). This ensures that at every message boundary,
 * there are no exec node spans open. The next ExecProcNode call recreates
 * the span under the new root.
 */
void
YbDistTraceEndOpenNodeSpans(void)
{
	if (!YBCIsDistTraceActive())
		return;

	YbForEachPortal(YbDistTraceEndPortalNodeSpans);
}

static void
YbDistTrace_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction,
					   uint64 count, bool execute_once)
{
	PG_TRY();
	{
		if (prev_ExecutorRun)
			prev_ExecutorRun(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
	}
	PG_CATCH();
	{
		YbDistTraceEndNodeSpans(queryDesc->planstate, /* errored */ true);
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
YbDistTrace_ExecutorFinish(QueryDesc *queryDesc)
{
	PG_TRY();
	{
		if (prev_ExecutorFinish)
			prev_ExecutorFinish(queryDesc);
		else
			standard_ExecutorFinish(queryDesc);
	}
	PG_CATCH();
	{
		YbDistTraceEndNodeSpans(queryDesc->planstate, /* errored */ true);
		PG_RE_THROW();
	}
	PG_END_TRY();
}

static void
YbDistTrace_ExecutorEnd(QueryDesc *queryDesc)
{
	PG_TRY();
	{
		if (prev_ExecutorEnd)
			prev_ExecutorEnd(queryDesc);
		else
			standard_ExecutorEnd(queryDesc);
	}
	PG_CATCH();
	{
		YbDistTraceEndNodeSpans(queryDesc->planstate, /* errored */ true);
		PG_RE_THROW();
	}
	PG_END_TRY();
}

void
YbDistTraceInstallExecutorHooks(void)
{
	prev_ExecutorRun = ExecutorRun_hook;
	ExecutorRun_hook = YbDistTrace_ExecutorRun;

	prev_ExecutorFinish = ExecutorFinish_hook;
	ExecutorFinish_hook = YbDistTrace_ExecutorFinish;

	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = YbDistTrace_ExecutorEnd;
}
