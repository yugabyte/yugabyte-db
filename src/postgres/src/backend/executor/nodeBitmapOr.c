/*-------------------------------------------------------------------------
 *
 * nodeBitmapOr.c
 *	  routines to handle BitmapOr nodes.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeBitmapOr.c
 *
 *-------------------------------------------------------------------------
 */
/* INTERFACE ROUTINES
 *		ExecInitBitmapOr	- initialize the BitmapOr node
 *		MultiExecBitmapOr	- retrieve the result bitmap from the node
 *		ExecEndBitmapOr		- shut down the BitmapOr node
 *		ExecReScanBitmapOr	- rescan the BitmapOr node
 *
 *	 NOTES
 *		BitmapOr nodes don't make use of their left and right
 *		subtrees, rather they maintain a list of subplans,
 *		much like Append nodes.  The logic is much simpler than
 *		Append, however, since we needn't cope with forward/backward
 *		execution.
 */

#include "postgres.h"

#include "access/relscan.h"
#include "executor/execdebug.h"
#include "executor/nodeBitmapOr.h"
#include "miscadmin.h"
#include "nodes/tidbitmap.h"
#include "pg_yb_utils.h"


/* ----------------------------------------------------------------
 *		ExecBitmapOr
 *
 *		stub for pro forma compliance
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecBitmapOr(PlanState *pstate)
{
	elog(ERROR, "BitmapOr node does not support ExecProcNode call convention");
	return NULL;
}

/* ----------------------------------------------------------------
 *		ExecInitBitmapOr
 *
 *		Begin all of the subscans of the BitmapOr node.
 * ----------------------------------------------------------------
 */
BitmapOrState *
ExecInitBitmapOr(BitmapOr *node, EState *estate, int eflags)
{
	BitmapOrState *bitmaporstate = makeNode(BitmapOrState);
	PlanState **bitmapplanstates;
	int			nplans;
	int			i;
	ListCell   *l;
	Plan	   *initNode;

	/* check for unsupported flags */
	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	/*
	 * Set up empty vector of subplan states
	 */
	nplans = list_length(node->bitmapplans);

	bitmapplanstates = (PlanState **) palloc0(nplans * sizeof(PlanState *));

	/*
	 * create new BitmapOrState for our BitmapOr node
	 */
	bitmaporstate->ps.plan = (Plan *) node;
	bitmaporstate->ps.state = estate;
	bitmaporstate->ps.ExecProcNode = ExecBitmapOr;
	bitmaporstate->bitmapplans = bitmapplanstates;
	bitmaporstate->nplans = nplans;

	/*
	 * call ExecInitNode on each of the plans to be executed and save the
	 * results into the array "bitmapplanstates".
	 */
	i = 0;
	foreach(l, node->bitmapplans)
	{
		initNode = (Plan *) lfirst(l);
		bitmapplanstates[i] = ExecInitNode(initNode, estate, eflags);
		i++;
	}

	/*
	 * Miscellaneous initialization
	 *
	 * BitmapOr plans don't have expression contexts because they never call
	 * ExecQual or ExecProject.  They don't need any tuple slots either.
	 */

	return bitmaporstate;
}

/* ----------------------------------------------------------------
 *	   MultiExecBitmapOr
 * ----------------------------------------------------------------
 */
Node *
MultiExecBitmapOr(BitmapOrState *node)
{
	PlanState **bitmapplans;
	int			nplans;
	int			i;
	TupleBitmap result = {NULL};

	/* must provide our own instrumentation support */
	if (node->ps.instrument)
		InstrStartNode(node->ps.instrument);

	/*
	 * get information from the node
	 */
	bitmapplans = node->bitmapplans;
	nplans = node->nplans;

	/*
	 * Scan all the subplans and OR their result bitmaps
	 */
	for (i = 0; i < nplans; i++)
	{
		PlanState  *subnode = bitmapplans[i];
		TupleBitmap subresult = {NULL};

		/*
		 * We can special-case BitmapIndexScan children to avoid an explicit
		 * tbm_union step for each child: just pass down the current result
		 * bitmap and let the child OR directly into it.
		 */
		if (IsA(subnode, BitmapIndexScanState))
		{
			bool is_yugabyte = IsYugaByteEnabled() &&
				IsYBRelation(
					((BitmapIndexScanState *) subnode)->ss.ss_currentRelation);

			if (result.tbm == NULL) /* first subplan */
			{
				if (is_yugabyte)
					result.ybtbm = yb_tbm_create(work_mem * 1024L);
				else
					/* XXX should we use less than work_mem for this? */
					result.tbm = tbm_create(work_mem * 1024L,
											((BitmapOr *) node->ps.plan)->isshared
												? node->ps.state->es_query_dsa
												: NULL);
			}

			if (is_yugabyte)
			{
				((BitmapIndexScanState *) subnode)->biss_result = result.ybtbm;
				subresult.ybtbm = (YbTIDBitmap *) MultiExecProcNode(subnode);
			}
			else
			{
				((BitmapIndexScanState *) subnode)->biss_result = result.tbm;
				subresult.tbm = (TIDBitmap *) MultiExecProcNode(subnode);
			}

			if (subresult.tbm != result.tbm)
				elog(ERROR, "unrecognized result from subplan");
		}
		else
		{
			subresult.tbm = (TIDBitmap *) MultiExecProcNode(subnode);
			if (!subresult.tbm)
				elog(ERROR, "unrecognized result from subplan");

			if (result.tbm == NULL)
				result = subresult; /* first subplan */
			else if (IsA(subresult.tbm, TIDBitmap))
			{
				tbm_union(result.tbm, subresult.tbm);
				tbm_free(subresult.tbm);
			}
			else if (IsA(subresult.ybtbm, YbTIDBitmap))
				yb_tbm_union_and_free(result.ybtbm, subresult.ybtbm);
			else
				elog(ERROR, "unrecognized result from subplan");
		}
	}

	/* We could return an empty result set here? */
	if (result.tbm == NULL)
		elog(ERROR, "BitmapOr doesn't support zero inputs");

	/* must provide our own instrumentation support */
	if (node->ps.instrument)
		InstrStopNode(node->ps.instrument,
					  IsA(result.ybtbm, YbTIDBitmap)
						? yb_tbm_get_size(result.ybtbm) : 0);


	return (Node *) result.tbm;
}

/* ----------------------------------------------------------------
 *		ExecEndBitmapOr
 *
 *		Shuts down the subscans of the BitmapOr node.
 *
 *		Returns nothing of interest.
 * ----------------------------------------------------------------
 */
void
ExecEndBitmapOr(BitmapOrState *node)
{
	PlanState **bitmapplans;
	int			nplans;
	int			i;

	/*
	 * get information from the node
	 */
	bitmapplans = node->bitmapplans;
	nplans = node->nplans;

	/*
	 * shut down each of the subscans (that we've initialized)
	 */
	for (i = 0; i < nplans; i++)
	{
		if (bitmapplans[i])
			ExecEndNode(bitmapplans[i]);
	}
}

void
ExecReScanBitmapOr(BitmapOrState *node)
{
	int			i;

	for (i = 0; i < node->nplans; i++)
	{
		PlanState  *subnode = node->bitmapplans[i];

		/*
		 * ExecReScan doesn't know about my subplans, so I have to do
		 * changed-parameter signaling myself.
		 */
		if (node->ps.chgParam != NULL)
			UpdateChangedParamSet(subnode, node->ps.chgParam);

		/*
		 * If chgParam of subnode is not null then plan will be re-scanned by
		 * first ExecProcNode.
		 */
		if (subnode->chgParam == NULL)
			ExecReScan(subnode);
	}
}
