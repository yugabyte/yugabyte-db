/*-------------------------------------------------------------------------
 *
 * nodeBitmapAnd.c
 *	  routines to handle BitmapAnd nodes.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeBitmapAnd.c
 *
 *-------------------------------------------------------------------------
 */
/* INTERFACE ROUTINES
 *		ExecInitBitmapAnd	- initialize the BitmapAnd node
 *		MultiExecBitmapAnd	- retrieve the result bitmap from the node
 *		ExecEndBitmapAnd	- shut down the BitmapAnd node
 *		ExecReScanBitmapAnd - rescan the BitmapAnd node
 *
 *	 NOTES
 *		BitmapAnd nodes don't make use of their left and right
 *		subtrees, rather they maintain a list of subplans,
 *		much like Append nodes.  The logic is much simpler than
 *		Append, however, since we needn't cope with forward/backward
 *		execution.
 */

#include "postgres.h"

#include "executor/execdebug.h"
#include "executor/nodeBitmapAnd.h"
#include "nodes/tidbitmap.h"


/* ----------------------------------------------------------------
 *		ExecBitmapAnd
 *
 *		stub for pro forma compliance
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecBitmapAnd(PlanState *pstate)
{
	elog(ERROR, "BitmapAnd node does not support ExecProcNode call convention");
	return NULL;
}

/* ----------------------------------------------------------------
 *		ExecInitBitmapAnd
 *
 *		Begin all of the subscans of the BitmapAnd node.
 * ----------------------------------------------------------------
 */
BitmapAndState *
ExecInitBitmapAnd(BitmapAnd *node, EState *estate, int eflags)
{
	BitmapAndState *bitmapandstate = makeNode(BitmapAndState);
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
	 * create new BitmapAndState for our BitmapAnd node
	 */
	bitmapandstate->ps.plan = (Plan *) node;
	bitmapandstate->ps.state = estate;
	bitmapandstate->ps.ExecProcNode = ExecBitmapAnd;
	bitmapandstate->bitmapplans = bitmapplanstates;
	bitmapandstate->nplans = nplans;

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
	 * BitmapAnd plans don't have expression contexts because they never call
	 * ExecQual or ExecProject.  They don't need any tuple slots either.
	 */

	return bitmapandstate;
}

/* ----------------------------------------------------------------
 *	   MultiExecBitmapAnd
 * ----------------------------------------------------------------
 */
Node *
MultiExecBitmapAnd(BitmapAndState *node)
{
	PlanState **bitmapplans;
	int			nplans;
	int			i;
	TupleBitmap	result = {NULL};

	/* must provide our own instrumentation support */
	if (node->ps.instrument)
		InstrStartNode(node->ps.instrument);

	/*
	 * get information from the node
	 */
	bitmapplans = node->bitmapplans;
	nplans = node->nplans;

	/*
	 * Scan all the subplans and AND their result bitmaps
	 */
	for (i = 0; i < nplans; i++)
	{
		PlanState  *subnode = bitmapplans[i];
		TupleBitmap	subresult;

		subresult.tbm = (TIDBitmap *) MultiExecProcNode(subnode);
		if (!subresult.tbm)
			elog(ERROR, "unrecognized result from subplan");

		if (result.tbm == NULL)
			result = subresult; /* first subplan */
		else if (IsA(subresult.tbm, TIDBitmap))
		{
			tbm_intersect(result.tbm, subresult.tbm);
			tbm_free(subresult.tbm);
		}
		else if (IsA(subresult.ybtbm, YbTIDBitmap))
			yb_tbm_intersect_and_free(result.ybtbm, subresult.ybtbm);
		else
			elog(ERROR, "unrecognized result from subplan");


		/*
		 * If at any stage we have a completely empty bitmap, we can fall out
		 * without evaluating the remaining subplans, since ANDing them can no
		 * longer change the result.  (Note: the fact that indxpath.c orders
		 * the subplans by selectivity should make this case more likely to
		 * occur.)
		 */
		if ((IsA(result.ybtbm, YbTIDBitmap) &&
			 (yb_tbm_is_empty(result.ybtbm) ||
			  result.ybtbm->work_mem_exceeded))
			|| (IsA(result.tbm, TIDBitmap) && tbm_is_empty(result.tbm)))
			break;
	}

	if (result.tbm == NULL)
		elog(ERROR, "BitmapAnd doesn't support zero inputs");

	/* must provide our own instrumentation support */
	if (node->ps.instrument)
		InstrStopNode(node->ps.instrument,
					  IsA(result.ybtbm, YbTIDBitmap)
						? yb_tbm_get_size(result.ybtbm) : 0);

	return (Node *) result.tbm;
}

/* ----------------------------------------------------------------
 *		ExecEndBitmapAnd
 *
 *		Shuts down the subscans of the BitmapAnd node.
 *
 *		Returns nothing of interest.
 * ----------------------------------------------------------------
 */
void
ExecEndBitmapAnd(BitmapAndState *node)
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
ExecReScanBitmapAnd(BitmapAndState *node)
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
