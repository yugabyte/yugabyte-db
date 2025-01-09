/*-------------------------------------------------------------------------
 *
 * copyfuncs.c
 *	  Copy functions for Postgres tree nodes.
 *
 * NOTE: we currently support copying all node types found in parse and
 * plan trees.  We do not support copying executor state trees; there
 * is no need for that, and no point in maintaining all the code that
 * would be needed.  We also do not support copying Path trees, mainly
 * because the circular linkages between RelOptInfo and Path nodes can't
 * be handled easily in a simple depth-first traversal.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/nodes/copyfuncs.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"
#include "nodes/extensible.h"
#include "nodes/pathnodes.h"
#include "nodes/plannodes.h"
#include "utils/datum.h"
#include "utils/rel.h"

/* YB includes */
#include "nodes/ybbitmatrix.h"

/*
 * Macros to simplify copying of different kinds of fields.  Use these
 * wherever possible to reduce the chance for silly typos.  Note that these
 * hard-wire the convention that the local variables in a Copy routine are
 * named 'newnode' and 'from'.
 */

/* Copy a simple scalar field (int, float, bool, enum, etc) */
#define COPY_SCALAR_FIELD(fldname) \
	(newnode->fldname = from->fldname)

/* Copy a field that is a pointer to some kind of Node or Node tree */
#define COPY_NODE_FIELD(fldname) \
	(newnode->fldname = copyObjectImpl(from->fldname))

/* Copy a field that is a pointer to a Bitmapset */
#define COPY_BITMAPSET_FIELD(fldname) \
	(newnode->fldname = bms_copy(from->fldname))

/* Copy a field that is a pointer to a C string, or perhaps NULL */
#define COPY_STRING_FIELD(fldname) \
	(newnode->fldname = from->fldname ? pstrdup(from->fldname) : (char *) NULL)

/* Copy a field that is an inline array */
#define COPY_ARRAY_FIELD(fldname) \
	memcpy(newnode->fldname, from->fldname, sizeof(newnode->fldname))

/* Copy a field that is a pointer to a simple palloc'd object of size sz */
#define COPY_POINTER_FIELD(fldname, sz) \
	do { \
		Size	_size = (sz); \
		if (_size > 0) \
		{ \
			newnode->fldname = palloc(_size); \
			memcpy(newnode->fldname, from->fldname, _size); \
		} \
	} while (0)

/* Copy a parse location field (for Copy, this is same as scalar case) */
#define COPY_LOCATION_FIELD(fldname) \
	(newnode->fldname = from->fldname)


/* ****************************************************************
 *					 plannodes.h copy functions
 * ****************************************************************
 */

/*
 * _copyPlannedStmt
 */
static PlannedStmt *
_copyPlannedStmt(const PlannedStmt *from)
{
	PlannedStmt *newnode = makeNode(PlannedStmt);

	COPY_SCALAR_FIELD(commandType);
	COPY_SCALAR_FIELD(queryId);
	COPY_SCALAR_FIELD(hasReturning);
	COPY_SCALAR_FIELD(hasModifyingCTE);
	COPY_SCALAR_FIELD(canSetTag);
	COPY_SCALAR_FIELD(transientPlan);
	COPY_SCALAR_FIELD(dependsOnRole);
	COPY_SCALAR_FIELD(parallelModeNeeded);
	COPY_SCALAR_FIELD(jitFlags);
	COPY_NODE_FIELD(planTree);
	COPY_NODE_FIELD(rtable);
	COPY_NODE_FIELD(resultRelations);
	COPY_NODE_FIELD(appendRelations);
	COPY_NODE_FIELD(subplans);
	COPY_BITMAPSET_FIELD(rewindPlanIDs);
	COPY_NODE_FIELD(rowMarks);
	COPY_NODE_FIELD(relationOids);
	COPY_NODE_FIELD(invalItems);
	COPY_NODE_FIELD(paramExecTypes);
	COPY_NODE_FIELD(utilityStmt);
	COPY_LOCATION_FIELD(stmt_location);
	COPY_SCALAR_FIELD(stmt_len);

	COPY_SCALAR_FIELD(yb_num_referenced_relations);

	return newnode;
}

/*
 * CopyPlanFields
 *
 *		This function copies the fields of the Plan node.  It is used by
 *		all the copy functions for classes which inherit from Plan.
 */
static void
CopyPlanFields(const Plan *from, Plan *newnode)
{
	COPY_SCALAR_FIELD(startup_cost);
	COPY_SCALAR_FIELD(total_cost);
	COPY_SCALAR_FIELD(plan_rows);
	COPY_SCALAR_FIELD(plan_width);
	COPY_SCALAR_FIELD(parallel_aware);
	COPY_SCALAR_FIELD(parallel_safe);
	COPY_SCALAR_FIELD(async_capable);
	COPY_SCALAR_FIELD(plan_node_id);
	COPY_NODE_FIELD(targetlist);
	COPY_NODE_FIELD(qual);
	COPY_NODE_FIELD(lefttree);
	COPY_NODE_FIELD(righttree);
	COPY_NODE_FIELD(initPlan);
	COPY_BITMAPSET_FIELD(extParam);
	COPY_BITMAPSET_FIELD(allParam);
}

/*
 * _copyPlan
 */
static Plan *
_copyPlan(const Plan *from)
{
	Plan	   *newnode = makeNode(Plan);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields(from, newnode);

	return newnode;
}


/*
 * _copyResult
 */
static Result *
_copyResult(const Result *from)
{
	Result	   *newnode = makeNode(Result);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(resconstantqual);

	return newnode;
}

/*
 * _copyProjectSet
 */
static ProjectSet *
_copyProjectSet(const ProjectSet *from)
{
	ProjectSet *newnode = makeNode(ProjectSet);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	return newnode;
}

/*
 * _copyModifyTable
 */
static ModifyTable *
_copyModifyTable(const ModifyTable *from)
{
	ModifyTable *newnode = makeNode(ModifyTable);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(operation);
	COPY_SCALAR_FIELD(canSetTag);
	COPY_SCALAR_FIELD(nominalRelation);
	COPY_SCALAR_FIELD(rootRelation);
	COPY_SCALAR_FIELD(partColsUpdated);
	COPY_NODE_FIELD(resultRelations);
	COPY_NODE_FIELD(updateColnosLists);
	COPY_NODE_FIELD(withCheckOptionLists);
	COPY_NODE_FIELD(returningLists);
	COPY_NODE_FIELD(fdwPrivLists);
	COPY_BITMAPSET_FIELD(fdwDirectModifyPlans);
	COPY_NODE_FIELD(rowMarks);
	COPY_SCALAR_FIELD(epqParam);
	COPY_SCALAR_FIELD(onConflictAction);
	COPY_NODE_FIELD(arbiterIndexes);
	COPY_NODE_FIELD(onConflictSet);
	COPY_NODE_FIELD(onConflictCols);
	COPY_NODE_FIELD(onConflictWhere);
	COPY_SCALAR_FIELD(exclRelRTI);
	COPY_NODE_FIELD(exclRelTlist);
	COPY_NODE_FIELD(mergeActionLists);

	COPY_NODE_FIELD(ybPushdownTlist);
	COPY_NODE_FIELD(ybReturningColumns);
	COPY_NODE_FIELD(ybColumnRefs);
	COPY_NODE_FIELD(yb_skip_entities);
	COPY_NODE_FIELD(yb_update_affected_entities);
	COPY_SCALAR_FIELD(no_row_trigger);

	return newnode;
}

/*
 * _copyAppend
 */
static Append *
_copyAppend(const Append *from)
{
	Append	   *newnode = makeNode(Append);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_BITMAPSET_FIELD(apprelids);
	COPY_NODE_FIELD(appendplans);
	COPY_SCALAR_FIELD(nasyncplans);
	COPY_SCALAR_FIELD(first_partial_plan);
	COPY_NODE_FIELD(part_prune_info);

	return newnode;
}

/*
 * _copyMergeAppend
 */
static MergeAppend *
_copyMergeAppend(const MergeAppend *from)
{
	MergeAppend *newnode = makeNode(MergeAppend);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_BITMAPSET_FIELD(apprelids);
	COPY_NODE_FIELD(mergeplans);
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(sortColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(sortOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(collations, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(nullsFirst, from->numCols * sizeof(bool));
	COPY_NODE_FIELD(part_prune_info);

	return newnode;
}

/*
 * _copyRecursiveUnion
 */
static RecursiveUnion *
_copyRecursiveUnion(const RecursiveUnion *from)
{
	RecursiveUnion *newnode = makeNode(RecursiveUnion);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(wtParam);
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(dupColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(dupOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(dupCollations, from->numCols * sizeof(Oid));
	COPY_SCALAR_FIELD(numGroups);

	return newnode;
}

/*
 * _copyBitmapAnd
 */
static BitmapAnd *
_copyBitmapAnd(const BitmapAnd *from)
{
	BitmapAnd  *newnode = makeNode(BitmapAnd);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(bitmapplans);

	return newnode;
}

/*
 * _copyBitmapOr
 */
static BitmapOr *
_copyBitmapOr(const BitmapOr *from)
{
	BitmapOr   *newnode = makeNode(BitmapOr);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(isshared);
	COPY_NODE_FIELD(bitmapplans);

	return newnode;
}

/*
 * _copyGather
 */
static Gather *
_copyGather(const Gather *from)
{
	Gather	   *newnode = makeNode(Gather);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(num_workers);
	COPY_SCALAR_FIELD(rescan_param);
	COPY_SCALAR_FIELD(single_copy);
	COPY_SCALAR_FIELD(invisible);
	COPY_BITMAPSET_FIELD(initParam);

	return newnode;
}

/*
 * _copyGatherMerge
 */
static GatherMerge *
_copyGatherMerge(const GatherMerge *from)
{
	GatherMerge *newnode = makeNode(GatherMerge);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(num_workers);
	COPY_SCALAR_FIELD(rescan_param);
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(sortColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(sortOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(collations, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(nullsFirst, from->numCols * sizeof(bool));
	COPY_BITMAPSET_FIELD(initParam);

	return newnode;
}

/*
 * CopyScanFields
 *
 *		This function copies the fields of the Scan node.  It is used by
 *		all the copy functions for classes which inherit from Scan.
 */
static void
CopyScanFields(const Scan *from, Scan *newnode)
{
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(scanrelid);
}

/*
 * _copyScan
 */
static Scan *
_copyScan(const Scan *from)
{
	Scan	   *newnode = makeNode(Scan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	return newnode;
}

/*
 * _copySeqScan
 */
static SeqScan *
_copySeqScan(const SeqScan *from)
{
	SeqScan    *newnode = makeNode(SeqScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	return newnode;
}

/*
 * _copyYbSeqScan
 */
static YbSeqScan *
_copyYbSeqScan(const YbSeqScan *from)
{
	YbSeqScan    *newnode = makeNode(YbSeqScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	COPY_NODE_FIELD(yb_pushdown.quals);
	COPY_NODE_FIELD(yb_pushdown.colrefs);

	return newnode;
}

/*
 * _copySampleScan
 */
static SampleScan *
_copySampleScan(const SampleScan *from)
{
	SampleScan *newnode = makeNode(SampleScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(tablesample);

	return newnode;
}

/*
 * _copyIndexScan
 */
static IndexScan *
_copyIndexScan(const IndexScan *from)
{
	IndexScan  *newnode = makeNode(IndexScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(indexid);
	COPY_NODE_FIELD(indexqual);
	COPY_NODE_FIELD(indexqualorig);
	COPY_NODE_FIELD(indexorderby);
	COPY_NODE_FIELD(indexorderbyorig);
	COPY_NODE_FIELD(indexorderbyops);
	COPY_NODE_FIELD(indextlist);
	COPY_SCALAR_FIELD(indexorderdir);
	COPY_NODE_FIELD(yb_idx_pushdown.quals);
	COPY_NODE_FIELD(yb_idx_pushdown.colrefs);
	COPY_NODE_FIELD(yb_rel_pushdown.quals);
	COPY_NODE_FIELD(yb_rel_pushdown.colrefs);
	COPY_SCALAR_FIELD(yb_distinct_prefixlen);
	COPY_SCALAR_FIELD(yb_lock_mechanism);

	return newnode;
}

/*
 * _copyIndexOnlyScan
 */
static IndexOnlyScan *
_copyIndexOnlyScan(const IndexOnlyScan *from)
{
	IndexOnlyScan *newnode = makeNode(IndexOnlyScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(indexid);
	COPY_NODE_FIELD(indexqual);
	COPY_NODE_FIELD(recheckqual);
	COPY_NODE_FIELD(indexorderby);
	COPY_NODE_FIELD(indextlist);
	COPY_SCALAR_FIELD(indexorderdir);
	COPY_NODE_FIELD(yb_pushdown.quals);
	COPY_NODE_FIELD(yb_pushdown.colrefs);
	COPY_NODE_FIELD(yb_indexqual_for_recheck);
	COPY_SCALAR_FIELD(yb_distinct_prefixlen);

	return newnode;
}

/*
 * _copyBitmapIndexScan
 */
static BitmapIndexScan *
_copyBitmapIndexScan(const BitmapIndexScan *from)
{
	BitmapIndexScan *newnode = makeNode(BitmapIndexScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(indexid);
	COPY_SCALAR_FIELD(isshared);
	COPY_NODE_FIELD(indexqual);
	COPY_NODE_FIELD(indexqualorig);

	return newnode;
}

/*
 * _copyYbBitmapIndexScan
 */
static YbBitmapIndexScan *
_copyYbBitmapIndexScan(const YbBitmapIndexScan *from)
{
	YbBitmapIndexScan *newnode = makeNode(YbBitmapIndexScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(indexid);
	COPY_SCALAR_FIELD(isshared);
	COPY_NODE_FIELD(indexqual);
	COPY_NODE_FIELD(indexqualorig);

	COPY_NODE_FIELD(indextlist);

	COPY_NODE_FIELD(yb_idx_pushdown.quals);
	COPY_NODE_FIELD(yb_idx_pushdown.colrefs);

	return newnode;
}

/*
 * _copyBitmapHeapScan
 */
static BitmapHeapScan *
_copyBitmapHeapScan(const BitmapHeapScan *from)
{
	BitmapHeapScan *newnode = makeNode(BitmapHeapScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(bitmapqualorig);

	return newnode;
}

/*
 * _copyBitmapHeapScan
 */
static YbBitmapTableScan *
_copyYbBitmapTableScan(const YbBitmapTableScan *from)
{
	YbBitmapTableScan *newnode = makeNode(YbBitmapTableScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(rel_pushdown.quals);
	COPY_NODE_FIELD(rel_pushdown.colrefs);

	COPY_NODE_FIELD(recheck_pushdown.quals);
	COPY_NODE_FIELD(recheck_pushdown.colrefs);
	COPY_NODE_FIELD(recheck_local_quals);

	COPY_NODE_FIELD(fallback_pushdown.quals);
	COPY_NODE_FIELD(fallback_pushdown.colrefs);
	COPY_NODE_FIELD(fallback_local_quals);

	return newnode;
}

/*
 * _copyTidScan
 */
static TidScan *
_copyTidScan(const TidScan *from)
{
	TidScan    *newnode = makeNode(TidScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(tidquals);

	return newnode;
}

/*
 * _copyTidRangeScan
 */
static TidRangeScan *
_copyTidRangeScan(const TidRangeScan *from)
{
	TidRangeScan *newnode = makeNode(TidRangeScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(tidrangequals);

	return newnode;
}

/*
 * _copySubqueryScan
 */
static SubqueryScan *
_copySubqueryScan(const SubqueryScan *from)
{
	SubqueryScan *newnode = makeNode(SubqueryScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(subplan);
	COPY_SCALAR_FIELD(scanstatus);

	return newnode;
}

/*
 * _copyFunctionScan
 */
static FunctionScan *
_copyFunctionScan(const FunctionScan *from)
{
	FunctionScan *newnode = makeNode(FunctionScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(functions);
	COPY_SCALAR_FIELD(funcordinality);

	return newnode;
}

/*
 * _copyTableFuncScan
 */
static TableFuncScan *
_copyTableFuncScan(const TableFuncScan *from)
{
	TableFuncScan *newnode = makeNode(TableFuncScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(tablefunc);

	return newnode;
}

/*
 * _copyValuesScan
 */
static ValuesScan *
_copyValuesScan(const ValuesScan *from)
{
	ValuesScan *newnode = makeNode(ValuesScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(values_lists);

	return newnode;
}

/*
 * _copyCteScan
 */
static CteScan *
_copyCteScan(const CteScan *from)
{
	CteScan    *newnode = makeNode(CteScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(ctePlanId);
	COPY_SCALAR_FIELD(cteParam);

	return newnode;
}

/*
 * _copyNamedTuplestoreScan
 */
static NamedTuplestoreScan *
_copyNamedTuplestoreScan(const NamedTuplestoreScan *from)
{
	NamedTuplestoreScan *newnode = makeNode(NamedTuplestoreScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_STRING_FIELD(enrname);

	return newnode;
}

/*
 * _copyWorkTableScan
 */
static WorkTableScan *
_copyWorkTableScan(const WorkTableScan *from)
{
	WorkTableScan *newnode = makeNode(WorkTableScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(wtParam);

	return newnode;
}

/*
 * _copyForeignScan
 */
static ForeignScan *
_copyForeignScan(const ForeignScan *from)
{
	ForeignScan *newnode = makeNode(ForeignScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(operation);
	COPY_SCALAR_FIELD(resultRelation);
	COPY_SCALAR_FIELD(fs_server);
	COPY_NODE_FIELD(fdw_exprs);
	COPY_NODE_FIELD(fdw_private);
	COPY_NODE_FIELD(fdw_scan_tlist);
	COPY_NODE_FIELD(fdw_recheck_quals);
	COPY_BITMAPSET_FIELD(fs_relids);
	COPY_SCALAR_FIELD(fsSystemCol);

	return newnode;
}

/*
 * _copyCustomScan
 */
static CustomScan *
_copyCustomScan(const CustomScan *from)
{
	CustomScan *newnode = makeNode(CustomScan);

	/*
	 * copy node superclass fields
	 */
	CopyScanFields((const Scan *) from, (Scan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(flags);
	COPY_NODE_FIELD(custom_plans);
	COPY_NODE_FIELD(custom_exprs);
	COPY_NODE_FIELD(custom_private);
	COPY_NODE_FIELD(custom_scan_tlist);
	COPY_BITMAPSET_FIELD(custom_relids);

	/*
	 * NOTE: The method field of CustomScan is required to be a pointer to a
	 * static table of callback functions.  So we don't copy the table itself,
	 * just reference the original one.
	 */
	COPY_SCALAR_FIELD(methods);

	return newnode;
}

/*
 * CopyJoinFields
 *
 *		This function copies the fields of the Join node.  It is used by
 *		all the copy functions for classes which inherit from Join.
 */
static void
CopyJoinFields(const Join *from, Join *newnode)
{
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(jointype);
	COPY_SCALAR_FIELD(inner_unique);
	COPY_NODE_FIELD(joinqual);
}


/*
 * _copyJoin
 */
static Join *
_copyJoin(const Join *from)
{
	Join	   *newnode = makeNode(Join);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields(from, newnode);

	return newnode;
}


/*
 * _copyNestLoop
 */
static NestLoop *
_copyNestLoop(const NestLoop *from)
{
	NestLoop   *newnode = makeNode(NestLoop);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields((const Join *) from, (Join *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(nestParams);

	return newnode;
}

/*
 * _copyYbBatchedNestLoop
 */
static YbBatchedNestLoop *
_copyYbBatchedNestLoop(const YbBatchedNestLoop *from)
{
	YbBatchedNestLoop   *newnode = makeNode(YbBatchedNestLoop);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields((const Join *) from, (Join *) newnode);
	COPY_NODE_FIELD(nl.nestParams);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(num_hashClauseInfos);

	if (from->num_hashClauseInfos > 0)
		COPY_POINTER_FIELD(hashClauseInfos,
						   (from->num_hashClauseInfos *
							sizeof(YbBNLHashClauseInfo)));

	for (int i = 0; i < from->num_hashClauseInfos; i++)
		newnode->hashClauseInfos[i].outerParamExpr = (Expr *)
			copyObject(from->hashClauseInfos[i].outerParamExpr);

	for (int i = 0; i < from->num_hashClauseInfos; i++)
		newnode->hashClauseInfos[i].orig_expr = (Expr *)
			copyObject(from->hashClauseInfos[i].orig_expr);

	COPY_SCALAR_FIELD(numSortCols);
	COPY_POINTER_FIELD(sortColIdx, from->numSortCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(sortOperators, from->numSortCols * sizeof(Oid));
	COPY_POINTER_FIELD(collations, from->numSortCols * sizeof(Oid));
	COPY_POINTER_FIELD(nullsFirst, from->numSortCols * sizeof(bool));

	return newnode;
}


/*
 * _copyMergeJoin
 */
static MergeJoin *
_copyMergeJoin(const MergeJoin *from)
{
	MergeJoin  *newnode = makeNode(MergeJoin);
	int			numCols;

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields((const Join *) from, (Join *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(skip_mark_restore);
	COPY_NODE_FIELD(mergeclauses);
	numCols = list_length(from->mergeclauses);
	COPY_POINTER_FIELD(mergeFamilies, numCols * sizeof(Oid));
	COPY_POINTER_FIELD(mergeCollations, numCols * sizeof(Oid));
	COPY_POINTER_FIELD(mergeStrategies, numCols * sizeof(int));
	COPY_POINTER_FIELD(mergeNullsFirst, numCols * sizeof(bool));

	return newnode;
}

/*
 * _copyHashJoin
 */
static HashJoin *
_copyHashJoin(const HashJoin *from)
{
	HashJoin   *newnode = makeNode(HashJoin);

	/*
	 * copy node superclass fields
	 */
	CopyJoinFields((const Join *) from, (Join *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(hashclauses);
	COPY_NODE_FIELD(hashoperators);
	COPY_NODE_FIELD(hashcollations);
	COPY_NODE_FIELD(hashkeys);

	return newnode;
}


/*
 * _copyMaterial
 */
static Material *
_copyMaterial(const Material *from)
{
	Material   *newnode = makeNode(Material);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	return newnode;
}


/*
 * _copyMemoize
 */
static Memoize *
_copyMemoize(const Memoize *from)
{
	Memoize    *newnode = makeNode(Memoize);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(numKeys);
	COPY_POINTER_FIELD(hashOperators, sizeof(Oid) * from->numKeys);
	COPY_POINTER_FIELD(collations, sizeof(Oid) * from->numKeys);
	COPY_NODE_FIELD(param_exprs);
	COPY_SCALAR_FIELD(singlerow);
	COPY_SCALAR_FIELD(binary_mode);
	COPY_SCALAR_FIELD(est_entries);
	COPY_BITMAPSET_FIELD(keyparamids);

	return newnode;
}


/*
 * CopySortFields
 *
 *		This function copies the fields of the Sort node.  It is used by
 *		all the copy functions for classes which inherit from Sort.
 */
static void
CopySortFields(const Sort *from, Sort *newnode)
{
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(sortColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(sortOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(collations, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(nullsFirst, from->numCols * sizeof(bool));
}

/*
 * _copySort
 */
static Sort *
_copySort(const Sort *from)
{
	Sort	   *newnode = makeNode(Sort);

	/*
	 * copy node superclass fields
	 */
	CopySortFields(from, newnode);

	return newnode;
}


/*
 * _copyIncrementalSort
 */
static IncrementalSort *
_copyIncrementalSort(const IncrementalSort *from)
{
	IncrementalSort *newnode = makeNode(IncrementalSort);

	/*
	 * copy node superclass fields
	 */
	CopySortFields((const Sort *) from, (Sort *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(nPresortedCols);

	return newnode;
}


/*
 * _copyGroup
 */
static Group *
_copyGroup(const Group *from)
{
	Group	   *newnode = makeNode(Group);

	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(grpColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(grpOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(grpCollations, from->numCols * sizeof(Oid));

	return newnode;
}

/*
 * _copyAgg
 */
static Agg *
_copyAgg(const Agg *from)
{
	Agg		   *newnode = makeNode(Agg);

	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(aggstrategy);
	COPY_SCALAR_FIELD(aggsplit);
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(grpColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(grpOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(grpCollations, from->numCols * sizeof(Oid));
	COPY_SCALAR_FIELD(numGroups);
	COPY_SCALAR_FIELD(transitionSpace);
	COPY_BITMAPSET_FIELD(aggParams);
	COPY_NODE_FIELD(groupingSets);
	COPY_NODE_FIELD(chain);

	return newnode;
}

/*
 * _copyWindowAgg
 */
static WindowAgg *
_copyWindowAgg(const WindowAgg *from)
{
	WindowAgg  *newnode = makeNode(WindowAgg);

	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	COPY_SCALAR_FIELD(winref);
	COPY_SCALAR_FIELD(partNumCols);
	COPY_POINTER_FIELD(partColIdx, from->partNumCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(partOperators, from->partNumCols * sizeof(Oid));
	COPY_POINTER_FIELD(partCollations, from->partNumCols * sizeof(Oid));
	COPY_SCALAR_FIELD(ordNumCols);
	COPY_POINTER_FIELD(ordColIdx, from->ordNumCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(ordOperators, from->ordNumCols * sizeof(Oid));
	COPY_POINTER_FIELD(ordCollations, from->ordNumCols * sizeof(Oid));
	COPY_SCALAR_FIELD(frameOptions);
	COPY_NODE_FIELD(startOffset);
	COPY_NODE_FIELD(endOffset);
	COPY_NODE_FIELD(runCondition);
	COPY_NODE_FIELD(runConditionOrig);
	COPY_SCALAR_FIELD(startInRangeFunc);
	COPY_SCALAR_FIELD(endInRangeFunc);
	COPY_SCALAR_FIELD(inRangeColl);
	COPY_SCALAR_FIELD(inRangeAsc);
	COPY_SCALAR_FIELD(inRangeNullsFirst);
	COPY_SCALAR_FIELD(topWindow);

	return newnode;
}

/*
 * _copyUnique
 */
static Unique *
_copyUnique(const Unique *from)
{
	Unique	   *newnode = makeNode(Unique);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(uniqColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(uniqOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(uniqCollations, from->numCols * sizeof(Oid));

	return newnode;
}

/*
 * _copyHash
 */
static Hash *
_copyHash(const Hash *from)
{
	Hash	   *newnode = makeNode(Hash);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(hashkeys);
	COPY_SCALAR_FIELD(skewTable);
	COPY_SCALAR_FIELD(skewColumn);
	COPY_SCALAR_FIELD(skewInherit);
	COPY_SCALAR_FIELD(rows_total);

	return newnode;
}

/*
 * _copySetOp
 */
static SetOp *
_copySetOp(const SetOp *from)
{
	SetOp	   *newnode = makeNode(SetOp);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_SCALAR_FIELD(cmd);
	COPY_SCALAR_FIELD(strategy);
	COPY_SCALAR_FIELD(numCols);
	COPY_POINTER_FIELD(dupColIdx, from->numCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(dupOperators, from->numCols * sizeof(Oid));
	COPY_POINTER_FIELD(dupCollations, from->numCols * sizeof(Oid));
	COPY_SCALAR_FIELD(flagColIdx);
	COPY_SCALAR_FIELD(firstFlag);
	COPY_SCALAR_FIELD(numGroups);

	return newnode;
}

/*
 * _copyLockRows
 */
static LockRows *
_copyLockRows(const LockRows *from)
{
	LockRows   *newnode = makeNode(LockRows);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(rowMarks);
	COPY_SCALAR_FIELD(epqParam);

	return newnode;
}

/*
 * _copyLimit
 */
static Limit *
_copyLimit(const Limit *from)
{
	Limit	   *newnode = makeNode(Limit);

	/*
	 * copy node superclass fields
	 */
	CopyPlanFields((const Plan *) from, (Plan *) newnode);

	/*
	 * copy remainder of node
	 */
	COPY_NODE_FIELD(limitOffset);
	COPY_NODE_FIELD(limitCount);
	COPY_SCALAR_FIELD(limitOption);
	COPY_SCALAR_FIELD(uniqNumCols);
	COPY_POINTER_FIELD(uniqColIdx, from->uniqNumCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(uniqOperators, from->uniqNumCols * sizeof(Oid));
	COPY_POINTER_FIELD(uniqCollations, from->uniqNumCols * sizeof(Oid));

	return newnode;
}

/*
 * _copyNestLoopParam
 */
static NestLoopParam *
_copyNestLoopParam(const NestLoopParam *from)
{
	NestLoopParam *newnode = makeNode(NestLoopParam);

	COPY_SCALAR_FIELD(paramno);
	COPY_NODE_FIELD(paramval);
	COPY_SCALAR_FIELD(yb_batch_size);

	return newnode;
}

/*
 * _copyPlanRowMark
 */
static PlanRowMark *
_copyPlanRowMark(const PlanRowMark *from)
{
	PlanRowMark *newnode = makeNode(PlanRowMark);

	COPY_SCALAR_FIELD(rti);
	COPY_SCALAR_FIELD(prti);
	COPY_SCALAR_FIELD(rowmarkId);
	COPY_SCALAR_FIELD(markType);
	COPY_SCALAR_FIELD(allMarkTypes);
	COPY_SCALAR_FIELD(strength);
	COPY_SCALAR_FIELD(waitPolicy);
	COPY_SCALAR_FIELD(isParent);

	return newnode;
}

static PartitionPruneInfo *
_copyPartitionPruneInfo(const PartitionPruneInfo *from)
{
	PartitionPruneInfo *newnode = makeNode(PartitionPruneInfo);

	COPY_NODE_FIELD(prune_infos);
	COPY_BITMAPSET_FIELD(other_subplans);

	return newnode;
}

static PartitionedRelPruneInfo *
_copyPartitionedRelPruneInfo(const PartitionedRelPruneInfo *from)
{
	PartitionedRelPruneInfo *newnode = makeNode(PartitionedRelPruneInfo);

	COPY_SCALAR_FIELD(rtindex);
	COPY_BITMAPSET_FIELD(present_parts);
	COPY_SCALAR_FIELD(nparts);
	COPY_POINTER_FIELD(subplan_map, from->nparts * sizeof(int));
	COPY_POINTER_FIELD(subpart_map, from->nparts * sizeof(int));
	COPY_POINTER_FIELD(relid_map, from->nparts * sizeof(Oid));
	COPY_NODE_FIELD(initial_pruning_steps);
	COPY_NODE_FIELD(exec_pruning_steps);
	COPY_BITMAPSET_FIELD(execparamids);

	return newnode;
}

/*
 * _copyPartitionPruneStepOp
 */
static PartitionPruneStepOp *
_copyPartitionPruneStepOp(const PartitionPruneStepOp *from)
{
	PartitionPruneStepOp *newnode = makeNode(PartitionPruneStepOp);

	COPY_SCALAR_FIELD(step.step_id);
	COPY_SCALAR_FIELD(opstrategy);
	COPY_NODE_FIELD(exprs);
	COPY_NODE_FIELD(cmpfns);
	COPY_BITMAPSET_FIELD(nullkeys);

	return newnode;
}

/*
 * _copyPartitionPruneStepCombine
 */
static PartitionPruneStepCombine *
_copyPartitionPruneStepCombine(const PartitionPruneStepCombine *from)
{
	PartitionPruneStepCombine *newnode = makeNode(PartitionPruneStepCombine);

	COPY_SCALAR_FIELD(step.step_id);
	COPY_SCALAR_FIELD(combineOp);
	COPY_NODE_FIELD(source_stepids);

	return newnode;
}

static PartitionPruneStepFuncOp *
_copyPartitionPruneStepFuncOp(const PartitionPruneStepFuncOp *from)
{
	PartitionPruneStepFuncOp *newnode = makeNode(PartitionPruneStepFuncOp);

	COPY_SCALAR_FIELD(step.step_id);
	COPY_NODE_FIELD(exprs);

	return newnode;
}

/*
 * _copyPlanInvalItem
 */
static PlanInvalItem *
_copyPlanInvalItem(const PlanInvalItem *from)
{
	PlanInvalItem *newnode = makeNode(PlanInvalItem);

	COPY_SCALAR_FIELD(cacheId);
	COPY_SCALAR_FIELD(hashValue);

	return newnode;
}

/* ****************************************************************
 *					   primnodes.h copy functions
 * ****************************************************************
 */

/*
 * _copyAlias
 */
static Alias *
_copyAlias(const Alias *from)
{
	Alias	   *newnode = makeNode(Alias);

	COPY_STRING_FIELD(aliasname);
	COPY_NODE_FIELD(colnames);

	return newnode;
}

/*
 * _copyRangeVar
 */
static RangeVar *
_copyRangeVar(const RangeVar *from)
{
	RangeVar   *newnode = makeNode(RangeVar);

	COPY_STRING_FIELD(catalogname);
	COPY_STRING_FIELD(schemaname);
	COPY_STRING_FIELD(relname);
	COPY_SCALAR_FIELD(inh);
	COPY_SCALAR_FIELD(relpersistence);
	COPY_NODE_FIELD(alias);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyTableFunc
 */
static TableFunc *
_copyTableFunc(const TableFunc *from)
{
	TableFunc  *newnode = makeNode(TableFunc);

	COPY_NODE_FIELD(ns_uris);
	COPY_NODE_FIELD(ns_names);
	COPY_NODE_FIELD(docexpr);
	COPY_NODE_FIELD(rowexpr);
	COPY_NODE_FIELD(colnames);
	COPY_NODE_FIELD(coltypes);
	COPY_NODE_FIELD(coltypmods);
	COPY_NODE_FIELD(colcollations);
	COPY_NODE_FIELD(colexprs);
	COPY_NODE_FIELD(coldefexprs);
	COPY_BITMAPSET_FIELD(notnulls);
	COPY_SCALAR_FIELD(ordinalitycol);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyIntoClause
 */
static IntoClause *
_copyIntoClause(const IntoClause *from)
{
	IntoClause *newnode = makeNode(IntoClause);

	COPY_NODE_FIELD(rel);
	COPY_NODE_FIELD(colNames);
	COPY_STRING_FIELD(accessMethod);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(onCommit);
	COPY_STRING_FIELD(tableSpaceName);
	COPY_NODE_FIELD(viewQuery);
	COPY_SCALAR_FIELD(skipData);

	return newnode;
}

/*
 * We don't need a _copyExpr because Expr is an abstract supertype which
 * should never actually get instantiated.  Also, since it has no common
 * fields except NodeTag, there's no need for a helper routine to factor
 * out copying the common fields...
 */

/*
 * _copyVar
 */
static Var *
_copyVar(const Var *from)
{
	Var		   *newnode = makeNode(Var);

	COPY_SCALAR_FIELD(varno);
	COPY_SCALAR_FIELD(varattno);
	COPY_SCALAR_FIELD(vartype);
	COPY_SCALAR_FIELD(vartypmod);
	COPY_SCALAR_FIELD(varcollid);
	COPY_SCALAR_FIELD(varlevelsup);
	COPY_SCALAR_FIELD(varnosyn);
	COPY_SCALAR_FIELD(varattnosyn);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyConst
 */
static Const *
_copyConst(const Const *from)
{
	Const	   *newnode = makeNode(Const);

	COPY_SCALAR_FIELD(consttype);
	COPY_SCALAR_FIELD(consttypmod);
	COPY_SCALAR_FIELD(constcollid);
	COPY_SCALAR_FIELD(constlen);

	if (from->constbyval || from->constisnull)
	{
		/*
		 * passed by value so just copy the datum. Also, don't try to copy
		 * struct when value is null!
		 */
		newnode->constvalue = from->constvalue;
	}
	else
	{
		/*
		 * passed by reference.  We need a palloc'd copy.
		 */
		newnode->constvalue = datumCopy(from->constvalue,
										from->constbyval,
										from->constlen);
	}

	COPY_SCALAR_FIELD(constisnull);
	COPY_SCALAR_FIELD(constbyval);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyParam
 */
static Param *
_copyParam(const Param *from)
{
	Param	   *newnode = makeNode(Param);

	COPY_SCALAR_FIELD(paramkind);
	COPY_SCALAR_FIELD(paramid);
	COPY_SCALAR_FIELD(paramtype);
	COPY_SCALAR_FIELD(paramtypmod);
	COPY_SCALAR_FIELD(paramcollid);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyAggref
 */
static Aggref *
_copyAggref(const Aggref *from)
{
	Aggref	   *newnode = makeNode(Aggref);

	COPY_SCALAR_FIELD(aggfnoid);
	COPY_SCALAR_FIELD(aggtype);
	COPY_SCALAR_FIELD(aggcollid);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_SCALAR_FIELD(aggtranstype);
	COPY_NODE_FIELD(aggargtypes);
	COPY_NODE_FIELD(aggdirectargs);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(aggorder);
	COPY_NODE_FIELD(aggdistinct);
	COPY_NODE_FIELD(aggfilter);
	COPY_SCALAR_FIELD(aggstar);
	COPY_SCALAR_FIELD(aggvariadic);
	COPY_SCALAR_FIELD(aggkind);
	COPY_SCALAR_FIELD(agglevelsup);
	COPY_SCALAR_FIELD(aggsplit);
	COPY_SCALAR_FIELD(aggno);
	COPY_SCALAR_FIELD(aggtransno);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyGroupingFunc
 */
static GroupingFunc *
_copyGroupingFunc(const GroupingFunc *from)
{
	GroupingFunc *newnode = makeNode(GroupingFunc);

	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(refs);
	COPY_NODE_FIELD(cols);
	COPY_SCALAR_FIELD(agglevelsup);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyWindowFunc
 */
static WindowFunc *
_copyWindowFunc(const WindowFunc *from)
{
	WindowFunc *newnode = makeNode(WindowFunc);

	COPY_SCALAR_FIELD(winfnoid);
	COPY_SCALAR_FIELD(wintype);
	COPY_SCALAR_FIELD(wincollid);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(aggfilter);
	COPY_SCALAR_FIELD(winref);
	COPY_SCALAR_FIELD(winstar);
	COPY_SCALAR_FIELD(winagg);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copySubscriptingRef
 */
static SubscriptingRef *
_copySubscriptingRef(const SubscriptingRef *from)
{
	SubscriptingRef *newnode = makeNode(SubscriptingRef);

	COPY_SCALAR_FIELD(refcontainertype);
	COPY_SCALAR_FIELD(refelemtype);
	COPY_SCALAR_FIELD(refrestype);
	COPY_SCALAR_FIELD(reftypmod);
	COPY_SCALAR_FIELD(refcollid);
	COPY_NODE_FIELD(refupperindexpr);
	COPY_NODE_FIELD(reflowerindexpr);
	COPY_NODE_FIELD(refexpr);
	COPY_NODE_FIELD(refassgnexpr);

	return newnode;
}

/*
 * _copyFuncExpr
 */
static FuncExpr *
_copyFuncExpr(const FuncExpr *from)
{
	FuncExpr   *newnode = makeNode(FuncExpr);

	COPY_SCALAR_FIELD(funcid);
	COPY_SCALAR_FIELD(funcresulttype);
	COPY_SCALAR_FIELD(funcretset);
	COPY_SCALAR_FIELD(funcvariadic);
	COPY_SCALAR_FIELD(funcformat);
	COPY_SCALAR_FIELD(funccollid);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyNamedArgExpr *
 */
static NamedArgExpr *
_copyNamedArgExpr(const NamedArgExpr *from)
{
	NamedArgExpr *newnode = makeNode(NamedArgExpr);

	COPY_NODE_FIELD(arg);
	COPY_STRING_FIELD(name);
	COPY_SCALAR_FIELD(argnumber);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyOpExpr
 */
static OpExpr *
_copyOpExpr(const OpExpr *from)
{
	OpExpr	   *newnode = makeNode(OpExpr);

	COPY_SCALAR_FIELD(opno);
	COPY_SCALAR_FIELD(opfuncid);
	COPY_SCALAR_FIELD(opresulttype);
	COPY_SCALAR_FIELD(opretset);
	COPY_SCALAR_FIELD(opcollid);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyDistinctExpr (same as OpExpr)
 */
static DistinctExpr *
_copyDistinctExpr(const DistinctExpr *from)
{
	DistinctExpr *newnode = makeNode(DistinctExpr);

	COPY_SCALAR_FIELD(opno);
	COPY_SCALAR_FIELD(opfuncid);
	COPY_SCALAR_FIELD(opresulttype);
	COPY_SCALAR_FIELD(opretset);
	COPY_SCALAR_FIELD(opcollid);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyNullIfExpr (same as OpExpr)
 */
static NullIfExpr *
_copyNullIfExpr(const NullIfExpr *from)
{
	NullIfExpr *newnode = makeNode(NullIfExpr);

	COPY_SCALAR_FIELD(opno);
	COPY_SCALAR_FIELD(opfuncid);
	COPY_SCALAR_FIELD(opresulttype);
	COPY_SCALAR_FIELD(opretset);
	COPY_SCALAR_FIELD(opcollid);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyScalarArrayOpExpr
 */
static ScalarArrayOpExpr *
_copyScalarArrayOpExpr(const ScalarArrayOpExpr *from)
{
	ScalarArrayOpExpr *newnode = makeNode(ScalarArrayOpExpr);

	COPY_SCALAR_FIELD(opno);
	COPY_SCALAR_FIELD(opfuncid);
	COPY_SCALAR_FIELD(hashfuncid);
	COPY_SCALAR_FIELD(negfuncid);
	COPY_SCALAR_FIELD(useOr);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyBoolExpr
 */
static BoolExpr *
_copyBoolExpr(const BoolExpr *from)
{
	BoolExpr   *newnode = makeNode(BoolExpr);

	COPY_SCALAR_FIELD(boolop);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copySubLink
 */
static SubLink *
_copySubLink(const SubLink *from)
{
	SubLink    *newnode = makeNode(SubLink);

	COPY_SCALAR_FIELD(subLinkType);
	COPY_SCALAR_FIELD(subLinkId);
	COPY_NODE_FIELD(testexpr);
	COPY_NODE_FIELD(operName);
	COPY_NODE_FIELD(subselect);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copySubPlan
 */
static SubPlan *
_copySubPlan(const SubPlan *from)
{
	SubPlan    *newnode = makeNode(SubPlan);

	COPY_SCALAR_FIELD(subLinkType);
	COPY_NODE_FIELD(testexpr);
	COPY_NODE_FIELD(paramIds);
	COPY_SCALAR_FIELD(plan_id);
	COPY_STRING_FIELD(plan_name);
	COPY_SCALAR_FIELD(firstColType);
	COPY_SCALAR_FIELD(firstColTypmod);
	COPY_SCALAR_FIELD(firstColCollation);
	COPY_SCALAR_FIELD(useHashTable);
	COPY_SCALAR_FIELD(unknownEqFalse);
	COPY_SCALAR_FIELD(parallel_safe);
	COPY_NODE_FIELD(setParam);
	COPY_NODE_FIELD(parParam);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(startup_cost);
	COPY_SCALAR_FIELD(per_call_cost);

	return newnode;
}

/*
 * _copyAlternativeSubPlan
 */
static AlternativeSubPlan *
_copyAlternativeSubPlan(const AlternativeSubPlan *from)
{
	AlternativeSubPlan *newnode = makeNode(AlternativeSubPlan);

	COPY_NODE_FIELD(subplans);

	return newnode;
}

/*
 * _copyFieldSelect
 */
static FieldSelect *
_copyFieldSelect(const FieldSelect *from)
{
	FieldSelect *newnode = makeNode(FieldSelect);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(fieldnum);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(resulttypmod);
	COPY_SCALAR_FIELD(resultcollid);

	return newnode;
}

/*
 * _copyFieldStore
 */
static FieldStore *
_copyFieldStore(const FieldStore *from)
{
	FieldStore *newnode = makeNode(FieldStore);

	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(newvals);
	COPY_NODE_FIELD(fieldnums);
	COPY_SCALAR_FIELD(resulttype);

	return newnode;
}

/*
 * _copyRelabelType
 */
static RelabelType *
_copyRelabelType(const RelabelType *from)
{
	RelabelType *newnode = makeNode(RelabelType);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(resulttypmod);
	COPY_SCALAR_FIELD(resultcollid);
	COPY_SCALAR_FIELD(relabelformat);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCoerceViaIO
 */
static CoerceViaIO *
_copyCoerceViaIO(const CoerceViaIO *from)
{
	CoerceViaIO *newnode = makeNode(CoerceViaIO);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(resultcollid);
	COPY_SCALAR_FIELD(coerceformat);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyArrayCoerceExpr
 */
static ArrayCoerceExpr *
_copyArrayCoerceExpr(const ArrayCoerceExpr *from)
{
	ArrayCoerceExpr *newnode = makeNode(ArrayCoerceExpr);

	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(elemexpr);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(resulttypmod);
	COPY_SCALAR_FIELD(resultcollid);
	COPY_SCALAR_FIELD(coerceformat);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyConvertRowtypeExpr
 */
static ConvertRowtypeExpr *
_copyConvertRowtypeExpr(const ConvertRowtypeExpr *from)
{
	ConvertRowtypeExpr *newnode = makeNode(ConvertRowtypeExpr);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(convertformat);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCollateExpr
 */
static CollateExpr *
_copyCollateExpr(const CollateExpr *from)
{
	CollateExpr *newnode = makeNode(CollateExpr);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(collOid);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCaseExpr
 */
static CaseExpr *
_copyCaseExpr(const CaseExpr *from)
{
	CaseExpr   *newnode = makeNode(CaseExpr);

	COPY_SCALAR_FIELD(casetype);
	COPY_SCALAR_FIELD(casecollid);
	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(defresult);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCaseWhen
 */
static CaseWhen *
_copyCaseWhen(const CaseWhen *from)
{
	CaseWhen   *newnode = makeNode(CaseWhen);

	COPY_NODE_FIELD(expr);
	COPY_NODE_FIELD(result);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCaseTestExpr
 */
static CaseTestExpr *
_copyCaseTestExpr(const CaseTestExpr *from)
{
	CaseTestExpr *newnode = makeNode(CaseTestExpr);

	COPY_SCALAR_FIELD(typeId);
	COPY_SCALAR_FIELD(typeMod);
	COPY_SCALAR_FIELD(collation);

	return newnode;
}

/*
 * _copyArrayExpr
 */
static ArrayExpr *
_copyArrayExpr(const ArrayExpr *from)
{
	ArrayExpr  *newnode = makeNode(ArrayExpr);

	COPY_SCALAR_FIELD(array_typeid);
	COPY_SCALAR_FIELD(array_collid);
	COPY_SCALAR_FIELD(element_typeid);
	COPY_NODE_FIELD(elements);
	COPY_SCALAR_FIELD(multidims);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyRowExpr
 */
static RowExpr *
_copyRowExpr(const RowExpr *from)
{
	RowExpr    *newnode = makeNode(RowExpr);

	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(row_typeid);
	COPY_SCALAR_FIELD(row_format);
	COPY_NODE_FIELD(colnames);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyRowCompareExpr
 */
static RowCompareExpr *
_copyRowCompareExpr(const RowCompareExpr *from)
{
	RowCompareExpr *newnode = makeNode(RowCompareExpr);

	COPY_SCALAR_FIELD(rctype);
	COPY_NODE_FIELD(opnos);
	COPY_NODE_FIELD(opfamilies);
	COPY_NODE_FIELD(inputcollids);
	COPY_NODE_FIELD(largs);
	COPY_NODE_FIELD(rargs);

	return newnode;
}

/*
 * _copyCoalesceExpr
 */
static CoalesceExpr *
_copyCoalesceExpr(const CoalesceExpr *from)
{
	CoalesceExpr *newnode = makeNode(CoalesceExpr);

	COPY_SCALAR_FIELD(coalescetype);
	COPY_SCALAR_FIELD(coalescecollid);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyMinMaxExpr
 */
static MinMaxExpr *
_copyMinMaxExpr(const MinMaxExpr *from)
{
	MinMaxExpr *newnode = makeNode(MinMaxExpr);

	COPY_SCALAR_FIELD(minmaxtype);
	COPY_SCALAR_FIELD(minmaxcollid);
	COPY_SCALAR_FIELD(inputcollid);
	COPY_SCALAR_FIELD(op);
	COPY_NODE_FIELD(args);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copySQLValueFunction
 */
static SQLValueFunction *
_copySQLValueFunction(const SQLValueFunction *from)
{
	SQLValueFunction *newnode = makeNode(SQLValueFunction);

	COPY_SCALAR_FIELD(op);
	COPY_SCALAR_FIELD(type);
	COPY_SCALAR_FIELD(typmod);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyXmlExpr
 */
static XmlExpr *
_copyXmlExpr(const XmlExpr *from)
{
	XmlExpr    *newnode = makeNode(XmlExpr);

	COPY_SCALAR_FIELD(op);
	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(named_args);
	COPY_NODE_FIELD(arg_names);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(xmloption);
	COPY_SCALAR_FIELD(type);
	COPY_SCALAR_FIELD(typmod);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyNullTest
 */
static NullTest *
_copyNullTest(const NullTest *from)
{
	NullTest   *newnode = makeNode(NullTest);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(nulltesttype);
	COPY_SCALAR_FIELD(argisrow);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyBooleanTest
 */
static BooleanTest *
_copyBooleanTest(const BooleanTest *from)
{
	BooleanTest *newnode = makeNode(BooleanTest);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(booltesttype);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCoerceToDomain
 */
static CoerceToDomain *
_copyCoerceToDomain(const CoerceToDomain *from)
{
	CoerceToDomain *newnode = makeNode(CoerceToDomain);

	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(resulttype);
	COPY_SCALAR_FIELD(resulttypmod);
	COPY_SCALAR_FIELD(resultcollid);
	COPY_SCALAR_FIELD(coercionformat);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCoerceToDomainValue
 */
static CoerceToDomainValue *
_copyCoerceToDomainValue(const CoerceToDomainValue *from)
{
	CoerceToDomainValue *newnode = makeNode(CoerceToDomainValue);

	COPY_SCALAR_FIELD(typeId);
	COPY_SCALAR_FIELD(typeMod);
	COPY_SCALAR_FIELD(collation);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copySetToDefault
 */
static SetToDefault *
_copySetToDefault(const SetToDefault *from)
{
	SetToDefault *newnode = makeNode(SetToDefault);

	COPY_SCALAR_FIELD(typeId);
	COPY_SCALAR_FIELD(typeMod);
	COPY_SCALAR_FIELD(collation);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

/*
 * _copyCurrentOfExpr
 */
static CurrentOfExpr *
_copyCurrentOfExpr(const CurrentOfExpr *from)
{
	CurrentOfExpr *newnode = makeNode(CurrentOfExpr);

	COPY_SCALAR_FIELD(cvarno);
	COPY_STRING_FIELD(cursor_name);
	COPY_SCALAR_FIELD(cursor_param);

	return newnode;
}

 /*
  * _copyNextValueExpr
  */
static NextValueExpr *
_copyNextValueExpr(const NextValueExpr *from)
{
	NextValueExpr *newnode = makeNode(NextValueExpr);

	COPY_SCALAR_FIELD(seqid);
	COPY_SCALAR_FIELD(typeId);

	return newnode;
}

/*
 * _copyInferenceElem
 */
static InferenceElem *
_copyInferenceElem(const InferenceElem *from)
{
	InferenceElem *newnode = makeNode(InferenceElem);

	COPY_NODE_FIELD(expr);
	COPY_SCALAR_FIELD(infercollid);
	COPY_SCALAR_FIELD(inferopclass);

	return newnode;
}

/*
 * _copyTargetEntry
 */
static TargetEntry *
_copyTargetEntry(const TargetEntry *from)
{
	TargetEntry *newnode = makeNode(TargetEntry);

	COPY_NODE_FIELD(expr);
	COPY_SCALAR_FIELD(resno);
	COPY_STRING_FIELD(resname);
	COPY_SCALAR_FIELD(ressortgroupref);
	COPY_SCALAR_FIELD(resorigtbl);
	COPY_SCALAR_FIELD(resorigcol);
	COPY_SCALAR_FIELD(resjunk);

	return newnode;
}

/*
 * _copyRangeTblRef
 */
static RangeTblRef *
_copyRangeTblRef(const RangeTblRef *from)
{
	RangeTblRef *newnode = makeNode(RangeTblRef);

	COPY_SCALAR_FIELD(rtindex);

	return newnode;
}

/*
 * _copyJoinExpr
 */
static JoinExpr *
_copyJoinExpr(const JoinExpr *from)
{
	JoinExpr   *newnode = makeNode(JoinExpr);

	COPY_SCALAR_FIELD(jointype);
	COPY_SCALAR_FIELD(isNatural);
	COPY_NODE_FIELD(larg);
	COPY_NODE_FIELD(rarg);
	COPY_NODE_FIELD(usingClause);
	COPY_NODE_FIELD(join_using_alias);
	COPY_NODE_FIELD(quals);
	COPY_NODE_FIELD(alias);
	COPY_SCALAR_FIELD(rtindex);

	return newnode;
}

/*
 * _copyFromExpr
 */
static FromExpr *
_copyFromExpr(const FromExpr *from)
{
	FromExpr   *newnode = makeNode(FromExpr);

	COPY_NODE_FIELD(fromlist);
	COPY_NODE_FIELD(quals);

	return newnode;
}

/*
 * _copyOnConflictExpr
 */
static OnConflictExpr *
_copyOnConflictExpr(const OnConflictExpr *from)
{
	OnConflictExpr *newnode = makeNode(OnConflictExpr);

	COPY_SCALAR_FIELD(action);
	COPY_NODE_FIELD(arbiterElems);
	COPY_NODE_FIELD(arbiterWhere);
	COPY_SCALAR_FIELD(constraint);
	COPY_NODE_FIELD(onConflictSet);
	COPY_NODE_FIELD(onConflictWhere);
	COPY_SCALAR_FIELD(exclRelIndex);
	COPY_NODE_FIELD(exclRelTlist);

	return newnode;
}

/* ****************************************************************
 *						pathnodes.h copy functions
 *
 * We don't support copying RelOptInfo, IndexOptInfo, or Path nodes.
 * There are some subsidiary structs that are useful to copy, though.
 * ****************************************************************
 */

/*
 * _copyPathKey
 */
static PathKey *
_copyPathKey(const PathKey *from)
{
	PathKey    *newnode = makeNode(PathKey);

	/* EquivalenceClasses are never moved, so just shallow-copy the pointer */
	COPY_SCALAR_FIELD(pk_eclass);
	COPY_SCALAR_FIELD(pk_opfamily);
	COPY_SCALAR_FIELD(pk_strategy);
	COPY_SCALAR_FIELD(pk_nulls_first);

	return newnode;
}

/*
 * _copyRestrictInfo
 */
static RestrictInfo *
_copyRestrictInfo(const RestrictInfo *from)
{
	RestrictInfo *newnode = makeNode(RestrictInfo);

	COPY_NODE_FIELD(clause);
	COPY_SCALAR_FIELD(is_pushed_down);
	COPY_SCALAR_FIELD(outerjoin_delayed);
	COPY_SCALAR_FIELD(can_join);
	COPY_SCALAR_FIELD(pseudoconstant);
	COPY_SCALAR_FIELD(leakproof);
	COPY_SCALAR_FIELD(has_volatile);
	COPY_SCALAR_FIELD(security_level);
	COPY_BITMAPSET_FIELD(clause_relids);
	COPY_BITMAPSET_FIELD(required_relids);
	COPY_BITMAPSET_FIELD(outer_relids);
	COPY_BITMAPSET_FIELD(nullable_relids);
	COPY_BITMAPSET_FIELD(left_relids);
	COPY_BITMAPSET_FIELD(right_relids);
	COPY_NODE_FIELD(orclause);
	/* EquivalenceClasses are never copied, so shallow-copy the pointers */
	COPY_SCALAR_FIELD(parent_ec);
	COPY_SCALAR_FIELD(eval_cost);
	COPY_SCALAR_FIELD(norm_selec);
	COPY_SCALAR_FIELD(outer_selec);
	COPY_NODE_FIELD(mergeopfamilies);
	/* EquivalenceClasses are never copied, so shallow-copy the pointers */
	COPY_SCALAR_FIELD(left_ec);
	COPY_SCALAR_FIELD(right_ec);
	COPY_SCALAR_FIELD(left_em);
	COPY_SCALAR_FIELD(right_em);
	/* MergeScanSelCache isn't a Node, so hard to copy; just reset cache */
	newnode->scansel_cache = NIL;
	COPY_SCALAR_FIELD(outer_is_left);
	COPY_SCALAR_FIELD(hashjoinoperator);
	COPY_SCALAR_FIELD(left_bucketsize);
	COPY_SCALAR_FIELD(right_bucketsize);
	COPY_SCALAR_FIELD(left_mcvfreq);
	COPY_SCALAR_FIELD(right_mcvfreq);
	COPY_SCALAR_FIELD(left_hasheqoperator);
	COPY_SCALAR_FIELD(right_hasheqoperator);

	return newnode;
}

/*
 * _copyPlaceHolderVar
 */
static PlaceHolderVar *
_copyPlaceHolderVar(const PlaceHolderVar *from)
{
	PlaceHolderVar *newnode = makeNode(PlaceHolderVar);

	COPY_NODE_FIELD(phexpr);
	COPY_BITMAPSET_FIELD(phrels);
	COPY_SCALAR_FIELD(phid);
	COPY_SCALAR_FIELD(phlevelsup);

	return newnode;
}

/*
 * _copySpecialJoinInfo
 */
static SpecialJoinInfo *
_copySpecialJoinInfo(const SpecialJoinInfo *from)
{
	SpecialJoinInfo *newnode = makeNode(SpecialJoinInfo);

	COPY_BITMAPSET_FIELD(min_lefthand);
	COPY_BITMAPSET_FIELD(min_righthand);
	COPY_BITMAPSET_FIELD(syn_lefthand);
	COPY_BITMAPSET_FIELD(syn_righthand);
	COPY_SCALAR_FIELD(jointype);
	COPY_SCALAR_FIELD(lhs_strict);
	COPY_SCALAR_FIELD(delay_upper_joins);
	COPY_SCALAR_FIELD(semi_can_btree);
	COPY_SCALAR_FIELD(semi_can_hash);
	COPY_NODE_FIELD(semi_operators);
	COPY_NODE_FIELD(semi_rhs_exprs);

	return newnode;
}

/*
 * _copyAppendRelInfo
 */
static AppendRelInfo *
_copyAppendRelInfo(const AppendRelInfo *from)
{
	AppendRelInfo *newnode = makeNode(AppendRelInfo);

	COPY_SCALAR_FIELD(parent_relid);
	COPY_SCALAR_FIELD(child_relid);
	COPY_SCALAR_FIELD(parent_reltype);
	COPY_SCALAR_FIELD(child_reltype);
	COPY_NODE_FIELD(translated_vars);
	COPY_SCALAR_FIELD(num_child_cols);
	COPY_POINTER_FIELD(parent_colnos, from->num_child_cols * sizeof(AttrNumber));
	COPY_SCALAR_FIELD(parent_reloid);

	return newnode;
}

/*
 * _copyPlaceHolderInfo
 */
static PlaceHolderInfo *
_copyPlaceHolderInfo(const PlaceHolderInfo *from)
{
	PlaceHolderInfo *newnode = makeNode(PlaceHolderInfo);

	COPY_SCALAR_FIELD(phid);
	COPY_NODE_FIELD(ph_var);
	COPY_BITMAPSET_FIELD(ph_eval_at);
	COPY_BITMAPSET_FIELD(ph_lateral);
	COPY_BITMAPSET_FIELD(ph_needed);
	COPY_SCALAR_FIELD(ph_width);

	return newnode;
}

/* ****************************************************************
 *					parsenodes.h copy functions
 * ****************************************************************
 */

static RangeTblEntry *
_copyRangeTblEntry(const RangeTblEntry *from)
{
	RangeTblEntry *newnode = makeNode(RangeTblEntry);

	COPY_SCALAR_FIELD(rtekind);
	COPY_SCALAR_FIELD(relid);
	COPY_SCALAR_FIELD(relkind);
	COPY_SCALAR_FIELD(rellockmode);
	COPY_NODE_FIELD(tablesample);
	COPY_NODE_FIELD(subquery);
	COPY_SCALAR_FIELD(security_barrier);
	COPY_SCALAR_FIELD(jointype);
	COPY_SCALAR_FIELD(joinmergedcols);
	COPY_NODE_FIELD(joinaliasvars);
	COPY_NODE_FIELD(joinleftcols);
	COPY_NODE_FIELD(joinrightcols);
	COPY_NODE_FIELD(join_using_alias);
	COPY_NODE_FIELD(functions);
	COPY_SCALAR_FIELD(funcordinality);
	COPY_NODE_FIELD(tablefunc);
	COPY_NODE_FIELD(values_lists);
	COPY_STRING_FIELD(ctename);
	COPY_SCALAR_FIELD(ctelevelsup);
	COPY_SCALAR_FIELD(self_reference);
	COPY_NODE_FIELD(coltypes);
	COPY_NODE_FIELD(coltypmods);
	COPY_NODE_FIELD(colcollations);
	COPY_STRING_FIELD(enrname);
	COPY_SCALAR_FIELD(enrtuples);
	COPY_NODE_FIELD(alias);
	COPY_NODE_FIELD(eref);
	COPY_SCALAR_FIELD(lateral);
	COPY_SCALAR_FIELD(inh);
	COPY_SCALAR_FIELD(inFromCl);
	COPY_SCALAR_FIELD(requiredPerms);
	COPY_SCALAR_FIELD(checkAsUser);
	COPY_BITMAPSET_FIELD(selectedCols);
	COPY_BITMAPSET_FIELD(insertedCols);
	COPY_BITMAPSET_FIELD(updatedCols);
	COPY_BITMAPSET_FIELD(extraUpdatedCols);
	COPY_NODE_FIELD(securityQuals);

	return newnode;
}

static RangeTblFunction *
_copyRangeTblFunction(const RangeTblFunction *from)
{
	RangeTblFunction *newnode = makeNode(RangeTblFunction);

	COPY_NODE_FIELD(funcexpr);
	COPY_SCALAR_FIELD(funccolcount);
	COPY_NODE_FIELD(funccolnames);
	COPY_NODE_FIELD(funccoltypes);
	COPY_NODE_FIELD(funccoltypmods);
	COPY_NODE_FIELD(funccolcollations);
	COPY_BITMAPSET_FIELD(funcparams);

	return newnode;
}

static TableSampleClause *
_copyTableSampleClause(const TableSampleClause *from)
{
	TableSampleClause *newnode = makeNode(TableSampleClause);

	COPY_SCALAR_FIELD(tsmhandler);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(repeatable);

	return newnode;
}

static WithCheckOption *
_copyWithCheckOption(const WithCheckOption *from)
{
	WithCheckOption *newnode = makeNode(WithCheckOption);

	COPY_SCALAR_FIELD(kind);
	COPY_STRING_FIELD(relname);
	COPY_STRING_FIELD(polname);
	COPY_NODE_FIELD(qual);
	COPY_SCALAR_FIELD(cascaded);

	return newnode;
}

static SortGroupClause *
_copySortGroupClause(const SortGroupClause *from)
{
	SortGroupClause *newnode = makeNode(SortGroupClause);

	COPY_SCALAR_FIELD(tleSortGroupRef);
	COPY_SCALAR_FIELD(eqop);
	COPY_SCALAR_FIELD(sortop);
	COPY_SCALAR_FIELD(nulls_first);
	COPY_SCALAR_FIELD(hashable);

	return newnode;
}

static GroupingSet *
_copyGroupingSet(const GroupingSet *from)
{
	GroupingSet *newnode = makeNode(GroupingSet);

	COPY_SCALAR_FIELD(kind);
	COPY_NODE_FIELD(content);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static WindowClause *
_copyWindowClause(const WindowClause *from)
{
	WindowClause *newnode = makeNode(WindowClause);

	COPY_STRING_FIELD(name);
	COPY_STRING_FIELD(refname);
	COPY_NODE_FIELD(partitionClause);
	COPY_NODE_FIELD(orderClause);
	COPY_SCALAR_FIELD(frameOptions);
	COPY_NODE_FIELD(startOffset);
	COPY_NODE_FIELD(endOffset);
	COPY_NODE_FIELD(runCondition);
	COPY_SCALAR_FIELD(startInRangeFunc);
	COPY_SCALAR_FIELD(endInRangeFunc);
	COPY_SCALAR_FIELD(inRangeColl);
	COPY_SCALAR_FIELD(inRangeAsc);
	COPY_SCALAR_FIELD(inRangeNullsFirst);
	COPY_SCALAR_FIELD(winref);
	COPY_SCALAR_FIELD(copiedOrder);

	return newnode;
}

static RowMarkClause *
_copyRowMarkClause(const RowMarkClause *from)
{
	RowMarkClause *newnode = makeNode(RowMarkClause);

	COPY_SCALAR_FIELD(rti);
	COPY_SCALAR_FIELD(strength);
	COPY_SCALAR_FIELD(waitPolicy);
	COPY_SCALAR_FIELD(pushedDown);

	return newnode;
}

static WithClause *
_copyWithClause(const WithClause *from)
{
	WithClause *newnode = makeNode(WithClause);

	COPY_NODE_FIELD(ctes);
	COPY_SCALAR_FIELD(recursive);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static InferClause *
_copyInferClause(const InferClause *from)
{
	InferClause *newnode = makeNode(InferClause);

	COPY_NODE_FIELD(indexElems);
	COPY_NODE_FIELD(whereClause);
	COPY_STRING_FIELD(conname);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static OnConflictClause *
_copyOnConflictClause(const OnConflictClause *from)
{
	OnConflictClause *newnode = makeNode(OnConflictClause);

	COPY_SCALAR_FIELD(action);
	COPY_NODE_FIELD(infer);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(whereClause);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static CTESearchClause *
_copyCTESearchClause(const CTESearchClause *from)
{
	CTESearchClause *newnode = makeNode(CTESearchClause);

	COPY_NODE_FIELD(search_col_list);
	COPY_SCALAR_FIELD(search_breadth_first);
	COPY_STRING_FIELD(search_seq_column);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static CTECycleClause *
_copyCTECycleClause(const CTECycleClause *from)
{
	CTECycleClause *newnode = makeNode(CTECycleClause);

	COPY_NODE_FIELD(cycle_col_list);
	COPY_STRING_FIELD(cycle_mark_column);
	COPY_NODE_FIELD(cycle_mark_value);
	COPY_NODE_FIELD(cycle_mark_default);
	COPY_STRING_FIELD(cycle_path_column);
	COPY_LOCATION_FIELD(location);
	COPY_SCALAR_FIELD(cycle_mark_type);
	COPY_SCALAR_FIELD(cycle_mark_typmod);
	COPY_SCALAR_FIELD(cycle_mark_collation);
	COPY_SCALAR_FIELD(cycle_mark_neop);

	return newnode;
}

static CommonTableExpr *
_copyCommonTableExpr(const CommonTableExpr *from)
{
	CommonTableExpr *newnode = makeNode(CommonTableExpr);

	COPY_STRING_FIELD(ctename);
	COPY_NODE_FIELD(aliascolnames);
	COPY_SCALAR_FIELD(ctematerialized);
	COPY_NODE_FIELD(ctequery);
	COPY_NODE_FIELD(search_clause);
	COPY_NODE_FIELD(cycle_clause);
	COPY_LOCATION_FIELD(location);
	COPY_SCALAR_FIELD(cterecursive);
	COPY_SCALAR_FIELD(cterefcount);
	COPY_NODE_FIELD(ctecolnames);
	COPY_NODE_FIELD(ctecoltypes);
	COPY_NODE_FIELD(ctecoltypmods);
	COPY_NODE_FIELD(ctecolcollations);

	return newnode;
}

static MergeWhenClause *
_copyMergeWhenClause(const MergeWhenClause *from)
{
	MergeWhenClause *newnode = makeNode(MergeWhenClause);

	COPY_SCALAR_FIELD(matched);
	COPY_SCALAR_FIELD(commandType);
	COPY_SCALAR_FIELD(override);
	COPY_NODE_FIELD(condition);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(values);
	return newnode;
}

static MergeAction *
_copyMergeAction(const MergeAction *from)
{
	MergeAction *newnode = makeNode(MergeAction);

	COPY_SCALAR_FIELD(matched);
	COPY_SCALAR_FIELD(commandType);
	COPY_SCALAR_FIELD(override);
	COPY_NODE_FIELD(qual);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(updateColnos);

	return newnode;
}

static A_Expr *
_copyA_Expr(const A_Expr *from)
{
	A_Expr	   *newnode = makeNode(A_Expr);

	COPY_SCALAR_FIELD(kind);
	COPY_NODE_FIELD(name);
	COPY_NODE_FIELD(lexpr);
	COPY_NODE_FIELD(rexpr);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static ColumnRef *
_copyColumnRef(const ColumnRef *from)
{
	ColumnRef  *newnode = makeNode(ColumnRef);

	COPY_NODE_FIELD(fields);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static ParamRef *
_copyParamRef(const ParamRef *from)
{
	ParamRef   *newnode = makeNode(ParamRef);

	COPY_SCALAR_FIELD(number);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static A_Const *
_copyA_Const(const A_Const *from)
{
	A_Const    *newnode = makeNode(A_Const);

	COPY_SCALAR_FIELD(isnull);
	if (!from->isnull)
	{
		/* This part must duplicate other _copy*() functions. */
		COPY_SCALAR_FIELD(val.node.type);
		switch (nodeTag(&from->val))
		{
			case T_Integer:
				COPY_SCALAR_FIELD(val.ival.ival);
				break;
			case T_Float:
				COPY_STRING_FIELD(val.fval.fval);
				break;
			case T_Boolean:
				COPY_SCALAR_FIELD(val.boolval.boolval);
				break;
			case T_String:
				COPY_STRING_FIELD(val.sval.sval);
				break;
			case T_BitString:
				COPY_STRING_FIELD(val.bsval.bsval);
				break;
			default:
				elog(ERROR, "unrecognized node type: %d",
					 (int) nodeTag(&from->val));
				break;
		}
	}

	COPY_LOCATION_FIELD(location);

	return newnode;
}

static FuncCall *
_copyFuncCall(const FuncCall *from)
{
	FuncCall   *newnode = makeNode(FuncCall);

	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(agg_order);
	COPY_NODE_FIELD(agg_filter);
	COPY_NODE_FIELD(over);
	COPY_SCALAR_FIELD(agg_within_group);
	COPY_SCALAR_FIELD(agg_star);
	COPY_SCALAR_FIELD(agg_distinct);
	COPY_SCALAR_FIELD(func_variadic);
	COPY_SCALAR_FIELD(funcformat);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static A_Star *
_copyA_Star(const A_Star *from)
{
	A_Star	   *newnode = makeNode(A_Star);

	return newnode;
}

static A_Indices *
_copyA_Indices(const A_Indices *from)
{
	A_Indices  *newnode = makeNode(A_Indices);

	COPY_SCALAR_FIELD(is_slice);
	COPY_NODE_FIELD(lidx);
	COPY_NODE_FIELD(uidx);

	return newnode;
}

static A_Indirection *
_copyA_Indirection(const A_Indirection *from)
{
	A_Indirection *newnode = makeNode(A_Indirection);

	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(indirection);

	return newnode;
}

static A_ArrayExpr *
_copyA_ArrayExpr(const A_ArrayExpr *from)
{
	A_ArrayExpr *newnode = makeNode(A_ArrayExpr);

	COPY_NODE_FIELD(elements);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static ResTarget *
_copyResTarget(const ResTarget *from)
{
	ResTarget  *newnode = makeNode(ResTarget);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(indirection);
	COPY_NODE_FIELD(val);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static MultiAssignRef *
_copyMultiAssignRef(const MultiAssignRef *from)
{
	MultiAssignRef *newnode = makeNode(MultiAssignRef);

	COPY_NODE_FIELD(source);
	COPY_SCALAR_FIELD(colno);
	COPY_SCALAR_FIELD(ncolumns);

	return newnode;
}

static TypeName *
_copyTypeName(const TypeName *from)
{
	TypeName   *newnode = makeNode(TypeName);

	COPY_NODE_FIELD(names);
	COPY_SCALAR_FIELD(typeOid);
	COPY_SCALAR_FIELD(setof);
	COPY_SCALAR_FIELD(pct_type);
	COPY_NODE_FIELD(typmods);
	COPY_SCALAR_FIELD(typemod);
	COPY_NODE_FIELD(arrayBounds);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static SortBy *
_copySortBy(const SortBy *from)
{
	SortBy	   *newnode = makeNode(SortBy);

	COPY_NODE_FIELD(node);
	COPY_SCALAR_FIELD(sortby_dir);
	COPY_SCALAR_FIELD(sortby_nulls);
	COPY_NODE_FIELD(useOp);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static WindowDef *
_copyWindowDef(const WindowDef *from)
{
	WindowDef  *newnode = makeNode(WindowDef);

	COPY_STRING_FIELD(name);
	COPY_STRING_FIELD(refname);
	COPY_NODE_FIELD(partitionClause);
	COPY_NODE_FIELD(orderClause);
	COPY_SCALAR_FIELD(frameOptions);
	COPY_NODE_FIELD(startOffset);
	COPY_NODE_FIELD(endOffset);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static RangeSubselect *
_copyRangeSubselect(const RangeSubselect *from)
{
	RangeSubselect *newnode = makeNode(RangeSubselect);

	COPY_SCALAR_FIELD(lateral);
	COPY_NODE_FIELD(subquery);
	COPY_NODE_FIELD(alias);

	return newnode;
}

static RangeFunction *
_copyRangeFunction(const RangeFunction *from)
{
	RangeFunction *newnode = makeNode(RangeFunction);

	COPY_SCALAR_FIELD(lateral);
	COPY_SCALAR_FIELD(ordinality);
	COPY_SCALAR_FIELD(is_rowsfrom);
	COPY_NODE_FIELD(functions);
	COPY_NODE_FIELD(alias);
	COPY_NODE_FIELD(coldeflist);

	return newnode;
}

static RangeTableSample *
_copyRangeTableSample(const RangeTableSample *from)
{
	RangeTableSample *newnode = makeNode(RangeTableSample);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(method);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(repeatable);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static RangeTableFunc *
_copyRangeTableFunc(const RangeTableFunc *from)
{
	RangeTableFunc *newnode = makeNode(RangeTableFunc);

	COPY_SCALAR_FIELD(lateral);
	COPY_NODE_FIELD(docexpr);
	COPY_NODE_FIELD(rowexpr);
	COPY_NODE_FIELD(namespaces);
	COPY_NODE_FIELD(columns);
	COPY_NODE_FIELD(alias);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static RangeTableFuncCol *
_copyRangeTableFuncCol(const RangeTableFuncCol *from)
{
	RangeTableFuncCol *newnode = makeNode(RangeTableFuncCol);

	COPY_STRING_FIELD(colname);
	COPY_NODE_FIELD(typeName);
	COPY_SCALAR_FIELD(for_ordinality);
	COPY_SCALAR_FIELD(is_not_null);
	COPY_NODE_FIELD(colexpr);
	COPY_NODE_FIELD(coldefexpr);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static TypeCast *
_copyTypeCast(const TypeCast *from)
{
	TypeCast   *newnode = makeNode(TypeCast);

	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(typeName);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static CollateClause *
_copyCollateClause(const CollateClause *from)
{
	CollateClause *newnode = makeNode(CollateClause);

	COPY_NODE_FIELD(arg);
	COPY_NODE_FIELD(collname);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static IndexElem *
_copyIndexElem(const IndexElem *from)
{
	IndexElem  *newnode = makeNode(IndexElem);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(expr);
	COPY_STRING_FIELD(indexcolname);
	COPY_NODE_FIELD(collation);
	COPY_NODE_FIELD(opclass);
	COPY_NODE_FIELD(opclassopts);
	COPY_SCALAR_FIELD(ordering);
	COPY_SCALAR_FIELD(nulls_ordering);

	return newnode;
}

static StatsElem *
_copyStatsElem(const StatsElem *from)
{
	StatsElem  *newnode = makeNode(StatsElem);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(expr);

	return newnode;
}

static ColumnDef *
_copyColumnDef(const ColumnDef *from)
{
	ColumnDef  *newnode = makeNode(ColumnDef);

	COPY_STRING_FIELD(colname);
	COPY_NODE_FIELD(typeName);
	COPY_STRING_FIELD(compression);
	COPY_SCALAR_FIELD(inhcount);
	COPY_SCALAR_FIELD(is_local);
	COPY_SCALAR_FIELD(is_not_null);
	COPY_SCALAR_FIELD(is_from_type);
	COPY_SCALAR_FIELD(storage);
	COPY_NODE_FIELD(raw_default);
	COPY_NODE_FIELD(cooked_default);
	COPY_SCALAR_FIELD(identity);
	COPY_NODE_FIELD(identitySequence);
	COPY_SCALAR_FIELD(generated);
	COPY_NODE_FIELD(collClause);
	COPY_SCALAR_FIELD(collOid);
	COPY_NODE_FIELD(constraints);
	COPY_NODE_FIELD(fdwoptions);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static Constraint *
_copyConstraint(const Constraint *from)
{
	Constraint *newnode = makeNode(Constraint);

	COPY_SCALAR_FIELD(contype);
	COPY_STRING_FIELD(conname);
	COPY_SCALAR_FIELD(deferrable);
	COPY_SCALAR_FIELD(initdeferred);
	COPY_LOCATION_FIELD(location);
	COPY_SCALAR_FIELD(is_no_inherit);
	COPY_NODE_FIELD(raw_expr);
	COPY_STRING_FIELD(cooked_expr);
	COPY_SCALAR_FIELD(generated_when);
	COPY_SCALAR_FIELD(nulls_not_distinct);
	COPY_NODE_FIELD(keys);
	COPY_NODE_FIELD(including);
	COPY_NODE_FIELD(exclusions);
	COPY_NODE_FIELD(options);
	COPY_STRING_FIELD(indexname);
	COPY_STRING_FIELD(indexspace);
	COPY_SCALAR_FIELD(reset_default_tblspc);
	COPY_STRING_FIELD(access_method);
	COPY_NODE_FIELD(where_clause);
	COPY_NODE_FIELD(pktable);
	COPY_NODE_FIELD(fk_attrs);
	COPY_NODE_FIELD(pk_attrs);
	COPY_SCALAR_FIELD(fk_matchtype);
	COPY_SCALAR_FIELD(fk_upd_action);
	COPY_SCALAR_FIELD(fk_del_action);
	COPY_NODE_FIELD(fk_del_set_cols);
	COPY_NODE_FIELD(old_conpfeqop);
	COPY_SCALAR_FIELD(old_pktable_oid);
	COPY_SCALAR_FIELD(skip_validation);
	COPY_SCALAR_FIELD(initially_valid);
	COPY_NODE_FIELD(yb_index_params);

	return newnode;
}

static DefElem *
_copyDefElem(const DefElem *from)
{
	DefElem    *newnode = makeNode(DefElem);

	COPY_STRING_FIELD(defnamespace);
	COPY_STRING_FIELD(defname);
	COPY_NODE_FIELD(arg);
	COPY_SCALAR_FIELD(defaction);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static LockingClause *
_copyLockingClause(const LockingClause *from)
{
	LockingClause *newnode = makeNode(LockingClause);

	COPY_NODE_FIELD(lockedRels);
	COPY_SCALAR_FIELD(strength);
	COPY_SCALAR_FIELD(waitPolicy);

	return newnode;
}

static XmlSerialize *
_copyXmlSerialize(const XmlSerialize *from)
{
	XmlSerialize *newnode = makeNode(XmlSerialize);

	COPY_SCALAR_FIELD(xmloption);
	COPY_NODE_FIELD(expr);
	COPY_NODE_FIELD(typeName);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static RoleSpec *
_copyRoleSpec(const RoleSpec *from)
{
	RoleSpec   *newnode = makeNode(RoleSpec);

	COPY_SCALAR_FIELD(roletype);
	COPY_STRING_FIELD(rolename);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static TriggerTransition *
_copyTriggerTransition(const TriggerTransition *from)
{
	TriggerTransition *newnode = makeNode(TriggerTransition);

	COPY_STRING_FIELD(name);
	COPY_SCALAR_FIELD(isNew);
	COPY_SCALAR_FIELD(isTable);

	return newnode;
}

static Query *
_copyQuery(const Query *from)
{
	Query	   *newnode = makeNode(Query);

	COPY_SCALAR_FIELD(commandType);
	COPY_SCALAR_FIELD(querySource);
	COPY_SCALAR_FIELD(queryId);
	COPY_SCALAR_FIELD(canSetTag);
	COPY_NODE_FIELD(utilityStmt);
	COPY_SCALAR_FIELD(resultRelation);
	COPY_SCALAR_FIELD(hasAggs);
	COPY_SCALAR_FIELD(hasWindowFuncs);
	COPY_SCALAR_FIELD(hasTargetSRFs);
	COPY_SCALAR_FIELD(hasSubLinks);
	COPY_SCALAR_FIELD(hasDistinctOn);
	COPY_SCALAR_FIELD(hasRecursive);
	COPY_SCALAR_FIELD(hasModifyingCTE);
	COPY_SCALAR_FIELD(hasForUpdate);
	COPY_SCALAR_FIELD(hasRowSecurity);
	COPY_SCALAR_FIELD(isReturn);
	COPY_NODE_FIELD(cteList);
	COPY_NODE_FIELD(rtable);
	COPY_NODE_FIELD(jointree);
	COPY_NODE_FIELD(targetList);
	COPY_SCALAR_FIELD(override);
	COPY_NODE_FIELD(onConflict);
	COPY_NODE_FIELD(returningList);
	COPY_NODE_FIELD(groupClause);
	COPY_SCALAR_FIELD(groupDistinct);
	COPY_NODE_FIELD(groupingSets);
	COPY_NODE_FIELD(havingQual);
	COPY_NODE_FIELD(windowClause);
	COPY_NODE_FIELD(distinctClause);
	COPY_NODE_FIELD(sortClause);
	COPY_NODE_FIELD(limitOffset);
	COPY_NODE_FIELD(limitCount);
	COPY_SCALAR_FIELD(limitOption);
	COPY_NODE_FIELD(rowMarks);
	COPY_NODE_FIELD(setOperations);
	COPY_NODE_FIELD(constraintDeps);
	COPY_NODE_FIELD(withCheckOptions);
	COPY_NODE_FIELD(mergeActionList);
	COPY_SCALAR_FIELD(mergeUseOuterJoin);
	COPY_LOCATION_FIELD(stmt_location);
	COPY_SCALAR_FIELD(stmt_len);

	return newnode;
}

static RawStmt *
_copyRawStmt(const RawStmt *from)
{
	RawStmt    *newnode = makeNode(RawStmt);

	COPY_NODE_FIELD(stmt);
	COPY_LOCATION_FIELD(stmt_location);
	COPY_SCALAR_FIELD(stmt_len);

	return newnode;
}

static InsertStmt *
_copyInsertStmt(const InsertStmt *from)
{
	InsertStmt *newnode = makeNode(InsertStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(cols);
	COPY_NODE_FIELD(selectStmt);
	COPY_NODE_FIELD(onConflictClause);
	COPY_NODE_FIELD(returningList);
	COPY_NODE_FIELD(withClause);
	COPY_SCALAR_FIELD(override);

	return newnode;
}

static DeleteStmt *
_copyDeleteStmt(const DeleteStmt *from)
{
	DeleteStmt *newnode = makeNode(DeleteStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(usingClause);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(returningList);
	COPY_NODE_FIELD(withClause);

	return newnode;
}

static UpdateStmt *
_copyUpdateStmt(const UpdateStmt *from)
{
	UpdateStmt *newnode = makeNode(UpdateStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(fromClause);
	COPY_NODE_FIELD(returningList);
	COPY_NODE_FIELD(withClause);

	return newnode;
}

static MergeStmt *
_copyMergeStmt(const MergeStmt *from)
{
	MergeStmt  *newnode = makeNode(MergeStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(sourceRelation);
	COPY_NODE_FIELD(joinCondition);
	COPY_NODE_FIELD(mergeWhenClauses);
	COPY_NODE_FIELD(withClause);

	return newnode;
}

static SelectStmt *
_copySelectStmt(const SelectStmt *from)
{
	SelectStmt *newnode = makeNode(SelectStmt);

	COPY_NODE_FIELD(distinctClause);
	COPY_NODE_FIELD(intoClause);
	COPY_NODE_FIELD(targetList);
	COPY_NODE_FIELD(fromClause);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(groupClause);
	COPY_SCALAR_FIELD(groupDistinct);
	COPY_NODE_FIELD(havingClause);
	COPY_NODE_FIELD(windowClause);
	COPY_NODE_FIELD(valuesLists);
	COPY_NODE_FIELD(sortClause);
	COPY_NODE_FIELD(limitOffset);
	COPY_NODE_FIELD(limitCount);
	COPY_SCALAR_FIELD(limitOption);
	COPY_NODE_FIELD(lockingClause);
	COPY_NODE_FIELD(withClause);
	COPY_SCALAR_FIELD(op);
	COPY_SCALAR_FIELD(all);
	COPY_NODE_FIELD(larg);
	COPY_NODE_FIELD(rarg);

	return newnode;
}

static SetOperationStmt *
_copySetOperationStmt(const SetOperationStmt *from)
{
	SetOperationStmt *newnode = makeNode(SetOperationStmt);

	COPY_SCALAR_FIELD(op);
	COPY_SCALAR_FIELD(all);
	COPY_NODE_FIELD(larg);
	COPY_NODE_FIELD(rarg);
	COPY_NODE_FIELD(colTypes);
	COPY_NODE_FIELD(colTypmods);
	COPY_NODE_FIELD(colCollations);
	COPY_NODE_FIELD(groupClauses);

	return newnode;
}

static ReturnStmt *
_copyReturnStmt(const ReturnStmt *from)
{
	ReturnStmt *newnode = makeNode(ReturnStmt);

	COPY_NODE_FIELD(returnval);

	return newnode;
}

static PLAssignStmt *
_copyPLAssignStmt(const PLAssignStmt *from)
{
	PLAssignStmt *newnode = makeNode(PLAssignStmt);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(indirection);
	COPY_SCALAR_FIELD(nnames);
	COPY_NODE_FIELD(val);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static AlterTableStmt *
_copyAlterTableStmt(const AlterTableStmt *from)
{
	AlterTableStmt *newnode = makeNode(AlterTableStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(cmds);
	COPY_SCALAR_FIELD(objtype);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static AlterTableCmd *
_copyAlterTableCmd(const AlterTableCmd *from)
{
	AlterTableCmd *newnode = makeNode(AlterTableCmd);

	COPY_SCALAR_FIELD(subtype);
	COPY_STRING_FIELD(name);
	COPY_SCALAR_FIELD(num);
	COPY_NODE_FIELD(newowner);
	COPY_NODE_FIELD(def);
	COPY_SCALAR_FIELD(behavior);
	COPY_SCALAR_FIELD(missing_ok);
	COPY_SCALAR_FIELD(recurse);
	COPY_SCALAR_FIELD(yb_is_add_primary_key);
	COPY_SCALAR_FIELD(yb_cascade);

	return newnode;
}

static AlterCollationStmt *
_copyAlterCollationStmt(const AlterCollationStmt *from)
{
	AlterCollationStmt *newnode = makeNode(AlterCollationStmt);

	COPY_NODE_FIELD(collname);

	return newnode;
}

static AlterDomainStmt *
_copyAlterDomainStmt(const AlterDomainStmt *from)
{
	AlterDomainStmt *newnode = makeNode(AlterDomainStmt);

	COPY_SCALAR_FIELD(subtype);
	COPY_NODE_FIELD(typeName);
	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(def);
	COPY_SCALAR_FIELD(behavior);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static GrantStmt *
_copyGrantStmt(const GrantStmt *from)
{
	GrantStmt  *newnode = makeNode(GrantStmt);

	COPY_SCALAR_FIELD(is_grant);
	COPY_SCALAR_FIELD(targtype);
	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(objects);
	COPY_NODE_FIELD(privileges);
	COPY_NODE_FIELD(grantees);
	COPY_SCALAR_FIELD(grant_option);
	COPY_NODE_FIELD(grantor);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static ObjectWithArgs *
_copyObjectWithArgs(const ObjectWithArgs *from)
{
	ObjectWithArgs *newnode = makeNode(ObjectWithArgs);

	COPY_NODE_FIELD(objname);
	COPY_NODE_FIELD(objargs);
	COPY_NODE_FIELD(objfuncargs);
	COPY_SCALAR_FIELD(args_unspecified);

	return newnode;
}

static AccessPriv *
_copyAccessPriv(const AccessPriv *from)
{
	AccessPriv *newnode = makeNode(AccessPriv);

	COPY_STRING_FIELD(priv_name);
	COPY_NODE_FIELD(cols);

	return newnode;
}

static GrantRoleStmt *
_copyGrantRoleStmt(const GrantRoleStmt *from)
{
	GrantRoleStmt *newnode = makeNode(GrantRoleStmt);

	COPY_NODE_FIELD(granted_roles);
	COPY_NODE_FIELD(grantee_roles);
	COPY_SCALAR_FIELD(is_grant);
	COPY_SCALAR_FIELD(admin_opt);
	COPY_NODE_FIELD(grantor);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static AlterDefaultPrivilegesStmt *
_copyAlterDefaultPrivilegesStmt(const AlterDefaultPrivilegesStmt *from)
{
	AlterDefaultPrivilegesStmt *newnode = makeNode(AlterDefaultPrivilegesStmt);

	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(action);

	return newnode;
}

static DeclareCursorStmt *
_copyDeclareCursorStmt(const DeclareCursorStmt *from)
{
	DeclareCursorStmt *newnode = makeNode(DeclareCursorStmt);

	COPY_STRING_FIELD(portalname);
	COPY_SCALAR_FIELD(options);
	COPY_NODE_FIELD(query);

	return newnode;
}

static ClosePortalStmt *
_copyClosePortalStmt(const ClosePortalStmt *from)
{
	ClosePortalStmt *newnode = makeNode(ClosePortalStmt);

	COPY_STRING_FIELD(portalname);

	return newnode;
}

static CallStmt *
_copyCallStmt(const CallStmt *from)
{
	CallStmt   *newnode = makeNode(CallStmt);

	COPY_NODE_FIELD(funccall);
	COPY_NODE_FIELD(funcexpr);
	COPY_NODE_FIELD(outargs);

	return newnode;
}

static ClusterStmt *
_copyClusterStmt(const ClusterStmt *from)
{
	ClusterStmt *newnode = makeNode(ClusterStmt);

	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(indexname);
	COPY_NODE_FIELD(params);

	return newnode;
}

static CopyStmt *
_copyCopyStmt(const CopyStmt *from)
{
	CopyStmt   *newnode = makeNode(CopyStmt);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(query);
	COPY_NODE_FIELD(attlist);
	COPY_SCALAR_FIELD(is_from);
	COPY_SCALAR_FIELD(is_program);
	COPY_STRING_FIELD(filename);
	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(whereClause);

	return newnode;
}

static OptSplit *
_copyOptSplit(const OptSplit *from)
{
	OptSplit *newnode = makeNode(OptSplit);

	COPY_SCALAR_FIELD(split_type);
	COPY_SCALAR_FIELD(num_tablets);
	COPY_NODE_FIELD(split_points);

	return newnode;
}

/*
 * CopyCreateStmtFields
 *
 *		This function copies the fields of the CreateStmt node.  It is used by
 *		copy functions for classes which inherit from CreateStmt.
 */
static void
CopyCreateStmtFields(const CreateStmt *from, CreateStmt *newnode)
{
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(tableElts);
	COPY_NODE_FIELD(inhRelations);
	COPY_NODE_FIELD(partspec);
	COPY_NODE_FIELD(partbound);
	COPY_NODE_FIELD(ofTypename);
	COPY_NODE_FIELD(constraints);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(oncommit);
	COPY_STRING_FIELD(tablespacename);
	COPY_STRING_FIELD(tablegroupname);
	COPY_STRING_FIELD(accessMethod);
	COPY_SCALAR_FIELD(if_not_exists);
	COPY_NODE_FIELD(split_options);
}

static CreateStmt *
_copyCreateStmt(const CreateStmt *from)
{
	CreateStmt *newnode = makeNode(CreateStmt);

	CopyCreateStmtFields(from, newnode);

	return newnode;
}

static TableLikeClause *
_copyTableLikeClause(const TableLikeClause *from)
{
	TableLikeClause *newnode = makeNode(TableLikeClause);

	COPY_NODE_FIELD(relation);
	COPY_SCALAR_FIELD(options);
	COPY_SCALAR_FIELD(relationOid);

	return newnode;
}

static DefineStmt *
_copyDefineStmt(const DefineStmt *from)
{
	DefineStmt *newnode = makeNode(DefineStmt);

	COPY_SCALAR_FIELD(kind);
	COPY_SCALAR_FIELD(oldstyle);
	COPY_NODE_FIELD(defnames);
	COPY_NODE_FIELD(args);
	COPY_NODE_FIELD(definition);
	COPY_SCALAR_FIELD(if_not_exists);
	COPY_SCALAR_FIELD(replace);

	return newnode;
}

static DropStmt *
_copyDropStmt(const DropStmt *from)
{
	DropStmt   *newnode = makeNode(DropStmt);

	COPY_NODE_FIELD(objects);
	COPY_SCALAR_FIELD(removeType);
	COPY_SCALAR_FIELD(behavior);
	COPY_SCALAR_FIELD(missing_ok);
	COPY_SCALAR_FIELD(concurrent);

	return newnode;
}

static TruncateStmt *
_copyTruncateStmt(const TruncateStmt *from)
{
	TruncateStmt *newnode = makeNode(TruncateStmt);

	COPY_NODE_FIELD(relations);
	COPY_SCALAR_FIELD(restart_seqs);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static CommentStmt *
_copyCommentStmt(const CommentStmt *from)
{
	CommentStmt *newnode = makeNode(CommentStmt);

	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(object);
	COPY_STRING_FIELD(comment);

	return newnode;
}

static SecLabelStmt *
_copySecLabelStmt(const SecLabelStmt *from)
{
	SecLabelStmt *newnode = makeNode(SecLabelStmt);

	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(object);
	COPY_STRING_FIELD(provider);
	COPY_STRING_FIELD(label);

	return newnode;
}

static FetchStmt *
_copyFetchStmt(const FetchStmt *from)
{
	FetchStmt  *newnode = makeNode(FetchStmt);

	COPY_SCALAR_FIELD(direction);
	COPY_SCALAR_FIELD(howMany);
	COPY_STRING_FIELD(portalname);
	COPY_SCALAR_FIELD(ismove);

	return newnode;
}

static IndexStmt *
_copyIndexStmt(const IndexStmt *from)
{
	IndexStmt  *newnode = makeNode(IndexStmt);

	COPY_STRING_FIELD(idxname);
	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(accessMethod);
	COPY_STRING_FIELD(tableSpace);
	COPY_NODE_FIELD(indexParams);
	COPY_NODE_FIELD(indexIncludingParams);
	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(excludeOpNames);
	COPY_STRING_FIELD(idxcomment);
	COPY_SCALAR_FIELD(indexOid);
	COPY_SCALAR_FIELD(oldNode);
	COPY_SCALAR_FIELD(oldCreateSubid);
	COPY_SCALAR_FIELD(oldFirstRelfilenodeSubid);
	COPY_SCALAR_FIELD(unique);
	COPY_SCALAR_FIELD(nulls_not_distinct);
	COPY_SCALAR_FIELD(primary);
	COPY_SCALAR_FIELD(isconstraint);
	COPY_SCALAR_FIELD(deferrable);
	COPY_SCALAR_FIELD(initdeferred);
	COPY_SCALAR_FIELD(transformed);
	COPY_SCALAR_FIELD(concurrent);
	COPY_SCALAR_FIELD(if_not_exists);
	COPY_SCALAR_FIELD(reset_default_tblspc);
	COPY_NODE_FIELD(split_options);

	return newnode;
}

static CreateStatsStmt *
_copyCreateStatsStmt(const CreateStatsStmt *from)
{
	CreateStatsStmt *newnode = makeNode(CreateStatsStmt);

	COPY_NODE_FIELD(defnames);
	COPY_NODE_FIELD(stat_types);
	COPY_NODE_FIELD(exprs);
	COPY_NODE_FIELD(relations);
	COPY_STRING_FIELD(stxcomment);
	COPY_SCALAR_FIELD(transformed);
	COPY_SCALAR_FIELD(if_not_exists);

	return newnode;
}

static AlterStatsStmt *
_copyAlterStatsStmt(const AlterStatsStmt *from)
{
	AlterStatsStmt *newnode = makeNode(AlterStatsStmt);

	COPY_NODE_FIELD(defnames);
	COPY_SCALAR_FIELD(stxstattarget);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static CreateFunctionStmt *
_copyCreateFunctionStmt(const CreateFunctionStmt *from)
{
	CreateFunctionStmt *newnode = makeNode(CreateFunctionStmt);

	COPY_SCALAR_FIELD(is_procedure);
	COPY_SCALAR_FIELD(replace);
	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(parameters);
	COPY_NODE_FIELD(returnType);
	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(sql_body);

	return newnode;
}

static FunctionParameter *
_copyFunctionParameter(const FunctionParameter *from)
{
	FunctionParameter *newnode = makeNode(FunctionParameter);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(argType);
	COPY_SCALAR_FIELD(mode);
	COPY_NODE_FIELD(defexpr);

	return newnode;
}

static AlterFunctionStmt *
_copyAlterFunctionStmt(const AlterFunctionStmt *from)
{
	AlterFunctionStmt *newnode = makeNode(AlterFunctionStmt);

	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(func);
	COPY_NODE_FIELD(actions);

	return newnode;
}

static DoStmt *
_copyDoStmt(const DoStmt *from)
{
	DoStmt	   *newnode = makeNode(DoStmt);

	COPY_NODE_FIELD(args);

	return newnode;
}

static RenameStmt *
_copyRenameStmt(const RenameStmt *from)
{
	RenameStmt *newnode = makeNode(RenameStmt);

	COPY_SCALAR_FIELD(renameType);
	COPY_SCALAR_FIELD(relationType);
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(object);
	COPY_STRING_FIELD(subname);
	COPY_STRING_FIELD(newname);
	COPY_SCALAR_FIELD(behavior);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static AlterObjectDependsStmt *
_copyAlterObjectDependsStmt(const AlterObjectDependsStmt *from)
{
	AlterObjectDependsStmt *newnode = makeNode(AlterObjectDependsStmt);

	COPY_SCALAR_FIELD(objectType);
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(object);
	COPY_NODE_FIELD(extname);
	COPY_SCALAR_FIELD(remove);

	return newnode;
}

static AlterObjectSchemaStmt *
_copyAlterObjectSchemaStmt(const AlterObjectSchemaStmt *from)
{
	AlterObjectSchemaStmt *newnode = makeNode(AlterObjectSchemaStmt);

	COPY_SCALAR_FIELD(objectType);
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(object);
	COPY_STRING_FIELD(newschema);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static AlterOwnerStmt *
_copyAlterOwnerStmt(const AlterOwnerStmt *from)
{
	AlterOwnerStmt *newnode = makeNode(AlterOwnerStmt);

	COPY_SCALAR_FIELD(objectType);
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(object);
	COPY_NODE_FIELD(newowner);

	return newnode;
}

static AlterOperatorStmt *
_copyAlterOperatorStmt(const AlterOperatorStmt *from)
{
	AlterOperatorStmt *newnode = makeNode(AlterOperatorStmt);

	COPY_NODE_FIELD(opername);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterTypeStmt *
_copyAlterTypeStmt(const AlterTypeStmt *from)
{
	AlterTypeStmt *newnode = makeNode(AlterTypeStmt);

	COPY_NODE_FIELD(typeName);
	COPY_NODE_FIELD(options);

	return newnode;
}

static RuleStmt *
_copyRuleStmt(const RuleStmt *from)
{
	RuleStmt   *newnode = makeNode(RuleStmt);

	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(rulename);
	COPY_NODE_FIELD(whereClause);
	COPY_SCALAR_FIELD(event);
	COPY_SCALAR_FIELD(instead);
	COPY_NODE_FIELD(actions);
	COPY_SCALAR_FIELD(replace);

	return newnode;
}

static NotifyStmt *
_copyNotifyStmt(const NotifyStmt *from)
{
	NotifyStmt *newnode = makeNode(NotifyStmt);

	COPY_STRING_FIELD(conditionname);
	COPY_STRING_FIELD(payload);

	return newnode;
}

static ListenStmt *
_copyListenStmt(const ListenStmt *from)
{
	ListenStmt *newnode = makeNode(ListenStmt);

	COPY_STRING_FIELD(conditionname);

	return newnode;
}

static UnlistenStmt *
_copyUnlistenStmt(const UnlistenStmt *from)
{
	UnlistenStmt *newnode = makeNode(UnlistenStmt);

	COPY_STRING_FIELD(conditionname);

	return newnode;
}

static TransactionStmt *
_copyTransactionStmt(const TransactionStmt *from)
{
	TransactionStmt *newnode = makeNode(TransactionStmt);

	COPY_SCALAR_FIELD(kind);
	COPY_NODE_FIELD(options);
	COPY_STRING_FIELD(savepoint_name);
	COPY_STRING_FIELD(gid);
	COPY_SCALAR_FIELD(chain);

	return newnode;
}

static CompositeTypeStmt *
_copyCompositeTypeStmt(const CompositeTypeStmt *from)
{
	CompositeTypeStmt *newnode = makeNode(CompositeTypeStmt);

	COPY_NODE_FIELD(typevar);
	COPY_NODE_FIELD(coldeflist);

	return newnode;
}

static CreateEnumStmt *
_copyCreateEnumStmt(const CreateEnumStmt *from)
{
	CreateEnumStmt *newnode = makeNode(CreateEnumStmt);

	COPY_NODE_FIELD(typeName);
	COPY_NODE_FIELD(vals);

	return newnode;
}

static CreateRangeStmt *
_copyCreateRangeStmt(const CreateRangeStmt *from)
{
	CreateRangeStmt *newnode = makeNode(CreateRangeStmt);

	COPY_NODE_FIELD(typeName);
	COPY_NODE_FIELD(params);

	return newnode;
}

static AlterEnumStmt *
_copyAlterEnumStmt(const AlterEnumStmt *from)
{
	AlterEnumStmt *newnode = makeNode(AlterEnumStmt);

	COPY_NODE_FIELD(typeName);
	COPY_STRING_FIELD(oldVal);
	COPY_STRING_FIELD(newVal);
	COPY_STRING_FIELD(newValNeighbor);
	COPY_SCALAR_FIELD(newValIsAfter);
	COPY_SCALAR_FIELD(skipIfNewValExists);

	return newnode;
}

static ViewStmt *
_copyViewStmt(const ViewStmt *from)
{
	ViewStmt   *newnode = makeNode(ViewStmt);

	COPY_NODE_FIELD(view);
	COPY_NODE_FIELD(aliases);
	COPY_NODE_FIELD(query);
	COPY_SCALAR_FIELD(replace);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(withCheckOption);

	return newnode;
}

static LoadStmt *
_copyLoadStmt(const LoadStmt *from)
{
	LoadStmt   *newnode = makeNode(LoadStmt);

	COPY_STRING_FIELD(filename);

	return newnode;
}

static CreateDomainStmt *
_copyCreateDomainStmt(const CreateDomainStmt *from)
{
	CreateDomainStmt *newnode = makeNode(CreateDomainStmt);

	COPY_NODE_FIELD(domainname);
	COPY_NODE_FIELD(typeName);
	COPY_NODE_FIELD(collClause);
	COPY_NODE_FIELD(constraints);

	return newnode;
}

static CreateOpClassStmt *
_copyCreateOpClassStmt(const CreateOpClassStmt *from)
{
	CreateOpClassStmt *newnode = makeNode(CreateOpClassStmt);

	COPY_NODE_FIELD(opclassname);
	COPY_NODE_FIELD(opfamilyname);
	COPY_STRING_FIELD(amname);
	COPY_NODE_FIELD(datatype);
	COPY_NODE_FIELD(items);
	COPY_SCALAR_FIELD(isDefault);

	return newnode;
}

static CreateOpClassItem *
_copyCreateOpClassItem(const CreateOpClassItem *from)
{
	CreateOpClassItem *newnode = makeNode(CreateOpClassItem);

	COPY_SCALAR_FIELD(itemtype);
	COPY_NODE_FIELD(name);
	COPY_SCALAR_FIELD(number);
	COPY_NODE_FIELD(order_family);
	COPY_NODE_FIELD(class_args);
	COPY_NODE_FIELD(storedtype);

	return newnode;
}

static CreateOpFamilyStmt *
_copyCreateOpFamilyStmt(const CreateOpFamilyStmt *from)
{
	CreateOpFamilyStmt *newnode = makeNode(CreateOpFamilyStmt);

	COPY_NODE_FIELD(opfamilyname);
	COPY_STRING_FIELD(amname);

	return newnode;
}

static AlterOpFamilyStmt *
_copyAlterOpFamilyStmt(const AlterOpFamilyStmt *from)
{
	AlterOpFamilyStmt *newnode = makeNode(AlterOpFamilyStmt);

	COPY_NODE_FIELD(opfamilyname);
	COPY_STRING_FIELD(amname);
	COPY_SCALAR_FIELD(isDrop);
	COPY_NODE_FIELD(items);

	return newnode;
}

static CreatedbStmt *
_copyCreatedbStmt(const CreatedbStmt *from)
{
	CreatedbStmt *newnode = makeNode(CreatedbStmt);

	COPY_STRING_FIELD(dbname);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterDatabaseStmt *
_copyAlterDatabaseStmt(const AlterDatabaseStmt *from)
{
	AlterDatabaseStmt *newnode = makeNode(AlterDatabaseStmt);

	COPY_STRING_FIELD(dbname);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterDatabaseRefreshCollStmt *
_copyAlterDatabaseRefreshCollStmt(const AlterDatabaseRefreshCollStmt *from)
{
	AlterDatabaseRefreshCollStmt *newnode = makeNode(AlterDatabaseRefreshCollStmt);

	COPY_STRING_FIELD(dbname);

	return newnode;
}

static AlterDatabaseSetStmt *
_copyAlterDatabaseSetStmt(const AlterDatabaseSetStmt *from)
{
	AlterDatabaseSetStmt *newnode = makeNode(AlterDatabaseSetStmt);

	COPY_STRING_FIELD(dbname);
	COPY_NODE_FIELD(setstmt);

	return newnode;
}

static DropdbStmt *
_copyDropdbStmt(const DropdbStmt *from)
{
	DropdbStmt *newnode = makeNode(DropdbStmt);

	COPY_STRING_FIELD(dbname);
	COPY_SCALAR_FIELD(missing_ok);
	COPY_NODE_FIELD(options);

	return newnode;
}

static VacuumStmt *
_copyVacuumStmt(const VacuumStmt *from)
{
	VacuumStmt *newnode = makeNode(VacuumStmt);

	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(rels);
	COPY_SCALAR_FIELD(is_vacuumcmd);

	return newnode;
}

static VacuumRelation *
_copyVacuumRelation(const VacuumRelation *from)
{
	VacuumRelation *newnode = makeNode(VacuumRelation);

	COPY_NODE_FIELD(relation);
	COPY_SCALAR_FIELD(oid);
	COPY_NODE_FIELD(va_cols);

	return newnode;
}

static ExplainStmt *
_copyExplainStmt(const ExplainStmt *from)
{
	ExplainStmt *newnode = makeNode(ExplainStmt);

	COPY_NODE_FIELD(query);
	COPY_NODE_FIELD(options);

	return newnode;
}

static CreateTableAsStmt *
_copyCreateTableAsStmt(const CreateTableAsStmt *from)
{
	CreateTableAsStmt *newnode = makeNode(CreateTableAsStmt);

	COPY_NODE_FIELD(query);
	COPY_NODE_FIELD(into);
	COPY_SCALAR_FIELD(objtype);
	COPY_SCALAR_FIELD(is_select_into);
	COPY_SCALAR_FIELD(if_not_exists);

	return newnode;
}

static RefreshMatViewStmt *
_copyRefreshMatViewStmt(const RefreshMatViewStmt *from)
{
	RefreshMatViewStmt *newnode = makeNode(RefreshMatViewStmt);

	COPY_SCALAR_FIELD(concurrent);
	COPY_SCALAR_FIELD(skipData);
	COPY_NODE_FIELD(relation);

	return newnode;
}

static ReplicaIdentityStmt *
_copyReplicaIdentityStmt(const ReplicaIdentityStmt *from)
{
	ReplicaIdentityStmt *newnode = makeNode(ReplicaIdentityStmt);

	COPY_SCALAR_FIELD(identity_type);
	COPY_STRING_FIELD(name);

	return newnode;
}

static AlterSystemStmt *
_copyAlterSystemStmt(const AlterSystemStmt *from)
{
	AlterSystemStmt *newnode = makeNode(AlterSystemStmt);

	COPY_NODE_FIELD(setstmt);

	return newnode;
}

static CreateSeqStmt *
_copyCreateSeqStmt(const CreateSeqStmt *from)
{
	CreateSeqStmt *newnode = makeNode(CreateSeqStmt);

	COPY_NODE_FIELD(sequence);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(ownerId);
	COPY_SCALAR_FIELD(for_identity);
	COPY_SCALAR_FIELD(if_not_exists);

	return newnode;
}

static AlterSeqStmt *
_copyAlterSeqStmt(const AlterSeqStmt *from)
{
	AlterSeqStmt *newnode = makeNode(AlterSeqStmt);

	COPY_NODE_FIELD(sequence);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(for_identity);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static VariableSetStmt *
_copyVariableSetStmt(const VariableSetStmt *from)
{
	VariableSetStmt *newnode = makeNode(VariableSetStmt);

	COPY_SCALAR_FIELD(kind);
	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(is_local);

	return newnode;
}

static VariableShowStmt *
_copyVariableShowStmt(const VariableShowStmt *from)
{
	VariableShowStmt *newnode = makeNode(VariableShowStmt);

	COPY_STRING_FIELD(name);

	return newnode;
}

static DiscardStmt *
_copyDiscardStmt(const DiscardStmt *from)
{
	DiscardStmt *newnode = makeNode(DiscardStmt);

	COPY_SCALAR_FIELD(target);

	return newnode;
}

static YbCreateProfileStmt *
_copyCreateProfileStmt(const YbCreateProfileStmt *from)
{
	YbCreateProfileStmt *newnode = makeNode(YbCreateProfileStmt);

	COPY_STRING_FIELD(prfname);
	COPY_SCALAR_FIELD(prffailedloginattempts);
	return newnode;
}

static YbDropProfileStmt *
_copyDropProfileStmt(const YbDropProfileStmt *from)
{
	YbDropProfileStmt *newnode = makeNode(YbDropProfileStmt);

	COPY_STRING_FIELD(prfname);
	COPY_SCALAR_FIELD(missing_ok);
	return newnode;
}

static CreateTableGroupStmt *
_copyCreateTableGroupStmt(const CreateTableGroupStmt *from)
{
	CreateTableGroupStmt *newnode = makeNode(CreateTableGroupStmt);

	COPY_STRING_FIELD(tablegroupname);
	COPY_STRING_FIELD(tablespacename);
	COPY_NODE_FIELD(owner);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(implicit);
	return newnode;
}

static CreateTableSpaceStmt *
_copyCreateTableSpaceStmt(const CreateTableSpaceStmt *from)
{
	CreateTableSpaceStmt *newnode = makeNode(CreateTableSpaceStmt);

	COPY_STRING_FIELD(tablespacename);
	COPY_NODE_FIELD(owner);
	COPY_STRING_FIELD(location);
	COPY_NODE_FIELD(options);

	return newnode;
}

static DropTableSpaceStmt *
_copyDropTableSpaceStmt(const DropTableSpaceStmt *from)
{
	DropTableSpaceStmt *newnode = makeNode(DropTableSpaceStmt);

	COPY_STRING_FIELD(tablespacename);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static AlterTableSpaceOptionsStmt *
_copyAlterTableSpaceOptionsStmt(const AlterTableSpaceOptionsStmt *from)
{
	AlterTableSpaceOptionsStmt *newnode = makeNode(AlterTableSpaceOptionsStmt);

	COPY_STRING_FIELD(tablespacename);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(isReset);

	return newnode;
}

static AlterTableMoveAllStmt *
_copyAlterTableMoveAllStmt(const AlterTableMoveAllStmt *from)
{
	AlterTableMoveAllStmt *newnode = makeNode(AlterTableMoveAllStmt);

	COPY_STRING_FIELD(orig_tablespacename);
	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(roles);
	COPY_STRING_FIELD(new_tablespacename);
	COPY_SCALAR_FIELD(nowait);
	COPY_NODE_FIELD(yb_relation);
	COPY_SCALAR_FIELD(yb_cascade);

	return newnode;
}

static CreateExtensionStmt *
_copyCreateExtensionStmt(const CreateExtensionStmt *from)
{
	CreateExtensionStmt *newnode = makeNode(CreateExtensionStmt);

	COPY_STRING_FIELD(extname);
	COPY_SCALAR_FIELD(if_not_exists);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterExtensionStmt *
_copyAlterExtensionStmt(const AlterExtensionStmt *from)
{
	AlterExtensionStmt *newnode = makeNode(AlterExtensionStmt);

	COPY_STRING_FIELD(extname);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterExtensionContentsStmt *
_copyAlterExtensionContentsStmt(const AlterExtensionContentsStmt *from)
{
	AlterExtensionContentsStmt *newnode = makeNode(AlterExtensionContentsStmt);

	COPY_STRING_FIELD(extname);
	COPY_SCALAR_FIELD(action);
	COPY_SCALAR_FIELD(objtype);
	COPY_NODE_FIELD(object);

	return newnode;
}

static CreateFdwStmt *
_copyCreateFdwStmt(const CreateFdwStmt *from)
{
	CreateFdwStmt *newnode = makeNode(CreateFdwStmt);

	COPY_STRING_FIELD(fdwname);
	COPY_NODE_FIELD(func_options);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterFdwStmt *
_copyAlterFdwStmt(const AlterFdwStmt *from)
{
	AlterFdwStmt *newnode = makeNode(AlterFdwStmt);

	COPY_STRING_FIELD(fdwname);
	COPY_NODE_FIELD(func_options);
	COPY_NODE_FIELD(options);

	return newnode;
}

static CreateForeignServerStmt *
_copyCreateForeignServerStmt(const CreateForeignServerStmt *from)
{
	CreateForeignServerStmt *newnode = makeNode(CreateForeignServerStmt);

	COPY_STRING_FIELD(servername);
	COPY_STRING_FIELD(servertype);
	COPY_STRING_FIELD(version);
	COPY_STRING_FIELD(fdwname);
	COPY_SCALAR_FIELD(if_not_exists);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterForeignServerStmt *
_copyAlterForeignServerStmt(const AlterForeignServerStmt *from)
{
	AlterForeignServerStmt *newnode = makeNode(AlterForeignServerStmt);

	COPY_STRING_FIELD(servername);
	COPY_STRING_FIELD(version);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(has_version);

	return newnode;
}

static CreateUserMappingStmt *
_copyCreateUserMappingStmt(const CreateUserMappingStmt *from)
{
	CreateUserMappingStmt *newnode = makeNode(CreateUserMappingStmt);

	COPY_NODE_FIELD(user);
	COPY_STRING_FIELD(servername);
	COPY_SCALAR_FIELD(if_not_exists);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterUserMappingStmt *
_copyAlterUserMappingStmt(const AlterUserMappingStmt *from)
{
	AlterUserMappingStmt *newnode = makeNode(AlterUserMappingStmt);

	COPY_NODE_FIELD(user);
	COPY_STRING_FIELD(servername);
	COPY_NODE_FIELD(options);

	return newnode;
}

static DropUserMappingStmt *
_copyDropUserMappingStmt(const DropUserMappingStmt *from)
{
	DropUserMappingStmt *newnode = makeNode(DropUserMappingStmt);

	COPY_NODE_FIELD(user);
	COPY_STRING_FIELD(servername);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static CreateForeignTableStmt *
_copyCreateForeignTableStmt(const CreateForeignTableStmt *from)
{
	CreateForeignTableStmt *newnode = makeNode(CreateForeignTableStmt);

	CopyCreateStmtFields((const CreateStmt *) from, (CreateStmt *) newnode);

	COPY_STRING_FIELD(servername);
	COPY_NODE_FIELD(options);

	return newnode;
}

static ImportForeignSchemaStmt *
_copyImportForeignSchemaStmt(const ImportForeignSchemaStmt *from)
{
	ImportForeignSchemaStmt *newnode = makeNode(ImportForeignSchemaStmt);

	COPY_STRING_FIELD(server_name);
	COPY_STRING_FIELD(remote_schema);
	COPY_STRING_FIELD(local_schema);
	COPY_SCALAR_FIELD(list_type);
	COPY_NODE_FIELD(table_list);
	COPY_NODE_FIELD(options);

	return newnode;
}

static CreateTransformStmt *
_copyCreateTransformStmt(const CreateTransformStmt *from)
{
	CreateTransformStmt *newnode = makeNode(CreateTransformStmt);

	COPY_SCALAR_FIELD(replace);
	COPY_NODE_FIELD(type_name);
	COPY_STRING_FIELD(lang);
	COPY_NODE_FIELD(fromsql);
	COPY_NODE_FIELD(tosql);

	return newnode;
}

static CreateAmStmt *
_copyCreateAmStmt(const CreateAmStmt *from)
{
	CreateAmStmt *newnode = makeNode(CreateAmStmt);

	COPY_STRING_FIELD(amname);
	COPY_NODE_FIELD(handler_name);
	COPY_SCALAR_FIELD(amtype);

	return newnode;
}

static CreateTrigStmt *
_copyCreateTrigStmt(const CreateTrigStmt *from)
{
	CreateTrigStmt *newnode = makeNode(CreateTrigStmt);

	COPY_SCALAR_FIELD(replace);
	COPY_SCALAR_FIELD(isconstraint);
	COPY_STRING_FIELD(trigname);
	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(funcname);
	COPY_NODE_FIELD(args);
	COPY_SCALAR_FIELD(row);
	COPY_SCALAR_FIELD(timing);
	COPY_SCALAR_FIELD(events);
	COPY_NODE_FIELD(columns);
	COPY_NODE_FIELD(whenClause);
	COPY_NODE_FIELD(transitionRels);
	COPY_SCALAR_FIELD(deferrable);
	COPY_SCALAR_FIELD(initdeferred);
	COPY_NODE_FIELD(constrrel);

	return newnode;
}

static CreateEventTrigStmt *
_copyCreateEventTrigStmt(const CreateEventTrigStmt *from)
{
	CreateEventTrigStmt *newnode = makeNode(CreateEventTrigStmt);

	COPY_STRING_FIELD(trigname);
	COPY_STRING_FIELD(eventname);
	COPY_NODE_FIELD(whenclause);
	COPY_NODE_FIELD(funcname);

	return newnode;
}

static AlterEventTrigStmt *
_copyAlterEventTrigStmt(const AlterEventTrigStmt *from)
{
	AlterEventTrigStmt *newnode = makeNode(AlterEventTrigStmt);

	COPY_STRING_FIELD(trigname);
	COPY_SCALAR_FIELD(tgenabled);

	return newnode;
}

static CreatePLangStmt *
_copyCreatePLangStmt(const CreatePLangStmt *from)
{
	CreatePLangStmt *newnode = makeNode(CreatePLangStmt);

	COPY_SCALAR_FIELD(replace);
	COPY_STRING_FIELD(plname);
	COPY_NODE_FIELD(plhandler);
	COPY_NODE_FIELD(plinline);
	COPY_NODE_FIELD(plvalidator);
	COPY_SCALAR_FIELD(pltrusted);

	return newnode;
}

static CreateRoleStmt *
_copyCreateRoleStmt(const CreateRoleStmt *from)
{
	CreateRoleStmt *newnode = makeNode(CreateRoleStmt);

	COPY_SCALAR_FIELD(stmt_type);
	COPY_STRING_FIELD(role);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterRoleStmt *
_copyAlterRoleStmt(const AlterRoleStmt *from)
{
	AlterRoleStmt *newnode = makeNode(AlterRoleStmt);

	COPY_NODE_FIELD(role);
	COPY_NODE_FIELD(options);
	COPY_SCALAR_FIELD(action);

	return newnode;
}

static AlterRoleSetStmt *
_copyAlterRoleSetStmt(const AlterRoleSetStmt *from)
{
	AlterRoleSetStmt *newnode = makeNode(AlterRoleSetStmt);

	COPY_NODE_FIELD(role);
	COPY_STRING_FIELD(database);
	COPY_NODE_FIELD(setstmt);

	return newnode;
}

static DropRoleStmt *
_copyDropRoleStmt(const DropRoleStmt *from)
{
	DropRoleStmt *newnode = makeNode(DropRoleStmt);

	COPY_NODE_FIELD(roles);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static LockStmt *
_copyLockStmt(const LockStmt *from)
{
	LockStmt   *newnode = makeNode(LockStmt);

	COPY_NODE_FIELD(relations);
	COPY_SCALAR_FIELD(mode);
	COPY_SCALAR_FIELD(nowait);

	return newnode;
}

static ConstraintsSetStmt *
_copyConstraintsSetStmt(const ConstraintsSetStmt *from)
{
	ConstraintsSetStmt *newnode = makeNode(ConstraintsSetStmt);

	COPY_NODE_FIELD(constraints);
	COPY_SCALAR_FIELD(deferred);

	return newnode;
}

static ReindexStmt *
_copyReindexStmt(const ReindexStmt *from)
{
	ReindexStmt *newnode = makeNode(ReindexStmt);

	COPY_SCALAR_FIELD(kind);
	COPY_NODE_FIELD(relation);
	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(params);

	return newnode;
}

static CreateSchemaStmt *
_copyCreateSchemaStmt(const CreateSchemaStmt *from)
{
	CreateSchemaStmt *newnode = makeNode(CreateSchemaStmt);

	COPY_STRING_FIELD(schemaname);
	COPY_NODE_FIELD(authrole);
	COPY_NODE_FIELD(schemaElts);
	COPY_SCALAR_FIELD(if_not_exists);

	return newnode;
}

static CreateConversionStmt *
_copyCreateConversionStmt(const CreateConversionStmt *from)
{
	CreateConversionStmt *newnode = makeNode(CreateConversionStmt);

	COPY_NODE_FIELD(conversion_name);
	COPY_STRING_FIELD(for_encoding_name);
	COPY_STRING_FIELD(to_encoding_name);
	COPY_NODE_FIELD(func_name);
	COPY_SCALAR_FIELD(def);

	return newnode;
}

static CreateCastStmt *
_copyCreateCastStmt(const CreateCastStmt *from)
{
	CreateCastStmt *newnode = makeNode(CreateCastStmt);

	COPY_NODE_FIELD(sourcetype);
	COPY_NODE_FIELD(targettype);
	COPY_NODE_FIELD(func);
	COPY_SCALAR_FIELD(context);
	COPY_SCALAR_FIELD(inout);

	return newnode;
}

static PrepareStmt *
_copyPrepareStmt(const PrepareStmt *from)
{
	PrepareStmt *newnode = makeNode(PrepareStmt);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(argtypes);
	COPY_NODE_FIELD(query);

	return newnode;
}

static ExecuteStmt *
_copyExecuteStmt(const ExecuteStmt *from)
{
	ExecuteStmt *newnode = makeNode(ExecuteStmt);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(params);

	return newnode;
}

static DeallocateStmt *
_copyDeallocateStmt(const DeallocateStmt *from)
{
	DeallocateStmt *newnode = makeNode(DeallocateStmt);

	COPY_STRING_FIELD(name);

	return newnode;
}

static DropOwnedStmt *
_copyDropOwnedStmt(const DropOwnedStmt *from)
{
	DropOwnedStmt *newnode = makeNode(DropOwnedStmt);

	COPY_NODE_FIELD(roles);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

static ReassignOwnedStmt *
_copyReassignOwnedStmt(const ReassignOwnedStmt *from)
{
	ReassignOwnedStmt *newnode = makeNode(ReassignOwnedStmt);

	COPY_NODE_FIELD(roles);
	COPY_NODE_FIELD(newrole);

	return newnode;
}

static AlterTSDictionaryStmt *
_copyAlterTSDictionaryStmt(const AlterTSDictionaryStmt *from)
{
	AlterTSDictionaryStmt *newnode = makeNode(AlterTSDictionaryStmt);

	COPY_NODE_FIELD(dictname);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterTSConfigurationStmt *
_copyAlterTSConfigurationStmt(const AlterTSConfigurationStmt *from)
{
	AlterTSConfigurationStmt *newnode = makeNode(AlterTSConfigurationStmt);

	COPY_SCALAR_FIELD(kind);
	COPY_NODE_FIELD(cfgname);
	COPY_NODE_FIELD(tokentype);
	COPY_NODE_FIELD(dicts);
	COPY_SCALAR_FIELD(override);
	COPY_SCALAR_FIELD(replace);
	COPY_SCALAR_FIELD(missing_ok);

	return newnode;
}

static CreatePolicyStmt *
_copyCreatePolicyStmt(const CreatePolicyStmt *from)
{
	CreatePolicyStmt *newnode = makeNode(CreatePolicyStmt);

	COPY_STRING_FIELD(policy_name);
	COPY_NODE_FIELD(table);
	COPY_STRING_FIELD(cmd_name);
	COPY_SCALAR_FIELD(permissive);
	COPY_NODE_FIELD(roles);
	COPY_NODE_FIELD(qual);
	COPY_NODE_FIELD(with_check);

	return newnode;
}

static AlterPolicyStmt *
_copyAlterPolicyStmt(const AlterPolicyStmt *from)
{
	AlterPolicyStmt *newnode = makeNode(AlterPolicyStmt);

	COPY_STRING_FIELD(policy_name);
	COPY_NODE_FIELD(table);
	COPY_NODE_FIELD(roles);
	COPY_NODE_FIELD(qual);
	COPY_NODE_FIELD(with_check);

	return newnode;
}

static PartitionElem *
_copyPartitionElem(const PartitionElem *from)
{
	PartitionElem *newnode = makeNode(PartitionElem);

	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(expr);
	COPY_NODE_FIELD(collation);
	COPY_NODE_FIELD(opclass);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static PartitionSpec *
_copyPartitionSpec(const PartitionSpec *from)
{
	PartitionSpec *newnode = makeNode(PartitionSpec);

	COPY_STRING_FIELD(strategy);
	COPY_NODE_FIELD(partParams);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static PartitionBoundSpec *
_copyPartitionBoundSpec(const PartitionBoundSpec *from)
{
	PartitionBoundSpec *newnode = makeNode(PartitionBoundSpec);

	COPY_SCALAR_FIELD(strategy);
	COPY_SCALAR_FIELD(is_default);
	COPY_SCALAR_FIELD(modulus);
	COPY_SCALAR_FIELD(remainder);
	COPY_NODE_FIELD(listdatums);
	COPY_NODE_FIELD(lowerdatums);
	COPY_NODE_FIELD(upperdatums);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static PartitionRangeDatum *
_copyPartitionRangeDatum(const PartitionRangeDatum *from)
{
	PartitionRangeDatum *newnode = makeNode(PartitionRangeDatum);

	COPY_SCALAR_FIELD(kind);
	COPY_NODE_FIELD(value);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static PartitionCmd *
_copyPartitionCmd(const PartitionCmd *from)
{
	PartitionCmd *newnode = makeNode(PartitionCmd);

	COPY_NODE_FIELD(name);
	COPY_NODE_FIELD(bound);
	COPY_SCALAR_FIELD(concurrent);

	return newnode;
}

static PublicationObjSpec *
_copyPublicationObject(const PublicationObjSpec *from)
{
	PublicationObjSpec *newnode = makeNode(PublicationObjSpec);

	COPY_SCALAR_FIELD(pubobjtype);
	COPY_STRING_FIELD(name);
	COPY_NODE_FIELD(pubtable);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static PublicationTable *
_copyPublicationTable(const PublicationTable *from)
{
	PublicationTable *newnode = makeNode(PublicationTable);

	COPY_NODE_FIELD(relation);
	COPY_NODE_FIELD(whereClause);
	COPY_NODE_FIELD(columns);

	return newnode;
}

static CreatePublicationStmt *
_copyCreatePublicationStmt(const CreatePublicationStmt *from)
{
	CreatePublicationStmt *newnode = makeNode(CreatePublicationStmt);

	COPY_STRING_FIELD(pubname);
	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(pubobjects);
	COPY_SCALAR_FIELD(for_all_tables);

	return newnode;
}

static AlterPublicationStmt *
_copyAlterPublicationStmt(const AlterPublicationStmt *from)
{
	AlterPublicationStmt *newnode = makeNode(AlterPublicationStmt);

	COPY_STRING_FIELD(pubname);
	COPY_NODE_FIELD(options);
	COPY_NODE_FIELD(pubobjects);
	COPY_SCALAR_FIELD(for_all_tables);
	COPY_SCALAR_FIELD(action);

	return newnode;
}

static CreateSubscriptionStmt *
_copyCreateSubscriptionStmt(const CreateSubscriptionStmt *from)
{
	CreateSubscriptionStmt *newnode = makeNode(CreateSubscriptionStmt);

	COPY_STRING_FIELD(subname);
	COPY_STRING_FIELD(conninfo);
	COPY_NODE_FIELD(publication);
	COPY_NODE_FIELD(options);

	return newnode;
}

static AlterSubscriptionStmt *
_copyAlterSubscriptionStmt(const AlterSubscriptionStmt *from)
{
	AlterSubscriptionStmt *newnode = makeNode(AlterSubscriptionStmt);

	COPY_SCALAR_FIELD(kind);
	COPY_STRING_FIELD(subname);
	COPY_STRING_FIELD(conninfo);
	COPY_NODE_FIELD(publication);
	COPY_NODE_FIELD(options);

	return newnode;
}

static DropSubscriptionStmt *
_copyDropSubscriptionStmt(const DropSubscriptionStmt *from)
{
	DropSubscriptionStmt *newnode = makeNode(DropSubscriptionStmt);

	COPY_STRING_FIELD(subname);
	COPY_SCALAR_FIELD(missing_ok);
	COPY_SCALAR_FIELD(behavior);

	return newnode;
}

/* ****************************************************************
 *					extensible.h copy functions
 * ****************************************************************
 */
static ExtensibleNode *
_copyExtensibleNode(const ExtensibleNode *from)
{
	ExtensibleNode *newnode;
	const ExtensibleNodeMethods *methods;

	methods = GetExtensibleNodeMethods(from->extnodename, false);
	newnode = (ExtensibleNode *) newNode(methods->node_size,
										 T_ExtensibleNode);
	COPY_STRING_FIELD(extnodename);

	/* copy the private fields */
	methods->nodeCopy(newnode, from);

	return newnode;
}

/* ****************************************************************
 *					value.h copy functions
 * ****************************************************************
 */
static Integer *
_copyInteger(const Integer *from)
{
	Integer    *newnode = makeNode(Integer);

	COPY_SCALAR_FIELD(ival);

	return newnode;
}

static Float *
_copyFloat(const Float *from)
{
	Float	   *newnode = makeNode(Float);

	COPY_STRING_FIELD(fval);

	return newnode;
}

static Boolean *
_copyBoolean(const Boolean *from)
{
	Boolean    *newnode = makeNode(Boolean);

	COPY_SCALAR_FIELD(boolval);

	return newnode;
}

static String *
_copyString(const String *from)
{
	String	   *newnode = makeNode(String);

	COPY_STRING_FIELD(sval);

	return newnode;
}

static BitString *
_copyBitString(const BitString *from)
{
	BitString  *newnode = makeNode(BitString);

	COPY_STRING_FIELD(bsval);

	return newnode;
}


static ForeignKeyCacheInfo *
_copyForeignKeyCacheInfo(const ForeignKeyCacheInfo *from)
{
	ForeignKeyCacheInfo *newnode = makeNode(ForeignKeyCacheInfo);

	COPY_SCALAR_FIELD(conoid);
	COPY_SCALAR_FIELD(conrelid);
	COPY_SCALAR_FIELD(confrelid);
	COPY_SCALAR_FIELD(nkeys);
	COPY_ARRAY_FIELD(conkey);
	COPY_ARRAY_FIELD(confkey);
	COPY_ARRAY_FIELD(conpfeqop);
	COPY_SCALAR_FIELD(ybconindid);

	return newnode;
}

static BackfillIndexStmt *
_copyBackfillIndexStmt(const BackfillIndexStmt *from)
{
	BackfillIndexStmt *newnode = makeNode(BackfillIndexStmt);

	COPY_SCALAR_FIELD(oid_list);
	COPY_NODE_FIELD(bfinfo);

	return newnode;
}

static YbBackfillInfo *
_copyYbBackfillInfo(const YbBackfillInfo *from)
{
	YbBackfillInfo *newnode = makeNode(YbBackfillInfo);

	COPY_STRING_FIELD(bfinstr);
	COPY_SCALAR_FIELD(read_time);
	COPY_NODE_FIELD(row_bounds);

	return newnode;
}

static RowBounds *
_copyRowBounds(const RowBounds *from)
{
	RowBounds *newnode = makeNode(RowBounds);

	COPY_STRING_FIELD(partition_key);
	COPY_STRING_FIELD(row_key_start);
	COPY_STRING_FIELD(row_key_end);

	return newnode;
}

static YbExprColrefDesc *
_copyYbExprColrefDesc(const YbExprColrefDesc *from)
{
	YbExprColrefDesc *newnode = makeNode(YbExprColrefDesc);

	COPY_SCALAR_FIELD(attno);
	COPY_SCALAR_FIELD(typid);
	COPY_SCALAR_FIELD(typmod);
	COPY_SCALAR_FIELD(collid);

	return newnode;
}

static YbBatchedExpr *
_copyYbBatchedExpr(const YbBatchedExpr *from)
{
	YbBatchedExpr *newnode = makeNode(YbBatchedExpr);
	COPY_NODE_FIELD(orig_expr);

	return newnode;
}

static YbUpdateAffectedEntities *
_copyYbUpdateAffectedEntities(const YbUpdateAffectedEntities *from)
{
	YbUpdateAffectedEntities *newnode = makeNode(YbUpdateAffectedEntities);

	COPY_POINTER_FIELD(entity_list, from->matrix.ncols * sizeof(struct YbUpdateEntity));
	COPY_POINTER_FIELD(col_info_list, from->matrix.nrows * sizeof(struct YbUpdateColInfo));
	YbCopyBitMatrix(&newnode->matrix, &from->matrix);

	return newnode;
}

static YbSkippableEntities *
_copyYbSkippableEntities(const YbSkippableEntities *from)
{
	YbSkippableEntities *newnode = makeNode(YbSkippableEntities);

	COPY_NODE_FIELD(index_list);
	COPY_NODE_FIELD(referencing_fkey_list);
	COPY_NODE_FIELD(referenced_fkey_list);

	return newnode;
}

/*
 * copyObjectImpl -- implementation of copyObject(); see nodes/nodes.h
 *
 * Create a copy of a Node tree or list.  This is a "deep" copy: all
 * substructure is copied too, recursively.
 */
void *
copyObjectImpl(const void *from)
{
	void	   *retval;

	if (from == NULL)
		return NULL;

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	switch (nodeTag(from))
	{
			/*
			 * PLAN NODES
			 */
		case T_PlannedStmt:
			retval = _copyPlannedStmt(from);
			break;
		case T_Plan:
			retval = _copyPlan(from);
			break;
		case T_Result:
			retval = _copyResult(from);
			break;
		case T_ProjectSet:
			retval = _copyProjectSet(from);
			break;
		case T_ModifyTable:
			retval = _copyModifyTable(from);
			break;
		case T_Append:
			retval = _copyAppend(from);
			break;
		case T_MergeAppend:
			retval = _copyMergeAppend(from);
			break;
		case T_RecursiveUnion:
			retval = _copyRecursiveUnion(from);
			break;
		case T_BitmapAnd:
			retval = _copyBitmapAnd(from);
			break;
		case T_BitmapOr:
			retval = _copyBitmapOr(from);
			break;
		case T_Scan:
			retval = _copyScan(from);
			break;
		case T_Gather:
			retval = _copyGather(from);
			break;
		case T_GatherMerge:
			retval = _copyGatherMerge(from);
			break;
		case T_SeqScan:
			retval = _copySeqScan(from);
			break;
		case T_YbSeqScan:
			retval = _copyYbSeqScan(from);
			break;
		case T_SampleScan:
			retval = _copySampleScan(from);
			break;
		case T_IndexScan:
			retval = _copyIndexScan(from);
			break;
		case T_IndexOnlyScan:
			retval = _copyIndexOnlyScan(from);
			break;
		case T_BitmapIndexScan:
			retval = _copyBitmapIndexScan(from);
			break;
		case T_YbBitmapIndexScan:
			retval = _copyYbBitmapIndexScan(from);
			break;
		case T_BitmapHeapScan:
			retval = _copyBitmapHeapScan(from);
			break;
		case T_YbBitmapTableScan:
			retval = _copyYbBitmapTableScan(from);
			break;
		case T_TidScan:
			retval = _copyTidScan(from);
			break;
		case T_TidRangeScan:
			retval = _copyTidRangeScan(from);
			break;
		case T_SubqueryScan:
			retval = _copySubqueryScan(from);
			break;
		case T_FunctionScan:
			retval = _copyFunctionScan(from);
			break;
		case T_TableFuncScan:
			retval = _copyTableFuncScan(from);
			break;
		case T_ValuesScan:
			retval = _copyValuesScan(from);
			break;
		case T_CteScan:
			retval = _copyCteScan(from);
			break;
		case T_NamedTuplestoreScan:
			retval = _copyNamedTuplestoreScan(from);
			break;
		case T_WorkTableScan:
			retval = _copyWorkTableScan(from);
			break;
		case T_ForeignScan:
			retval = _copyForeignScan(from);
			break;
		case T_CustomScan:
			retval = _copyCustomScan(from);
			break;
		case T_Join:
			retval = _copyJoin(from);
			break;
		case T_NestLoop:
			retval = _copyNestLoop(from);
			break;
		case T_YbBatchedNestLoop:
			retval = _copyYbBatchedNestLoop(from);
			break;
		case T_MergeJoin:
			retval = _copyMergeJoin(from);
			break;
		case T_HashJoin:
			retval = _copyHashJoin(from);
			break;
		case T_Material:
			retval = _copyMaterial(from);
			break;
		case T_Memoize:
			retval = _copyMemoize(from);
			break;
		case T_Sort:
			retval = _copySort(from);
			break;
		case T_IncrementalSort:
			retval = _copyIncrementalSort(from);
			break;
		case T_Group:
			retval = _copyGroup(from);
			break;
		case T_Agg:
			retval = _copyAgg(from);
			break;
		case T_WindowAgg:
			retval = _copyWindowAgg(from);
			break;
		case T_Unique:
			retval = _copyUnique(from);
			break;
		case T_Hash:
			retval = _copyHash(from);
			break;
		case T_SetOp:
			retval = _copySetOp(from);
			break;
		case T_LockRows:
			retval = _copyLockRows(from);
			break;
		case T_Limit:
			retval = _copyLimit(from);
			break;
		case T_NestLoopParam:
			retval = _copyNestLoopParam(from);
			break;
		case T_PlanRowMark:
			retval = _copyPlanRowMark(from);
			break;
		case T_PartitionPruneInfo:
			retval = _copyPartitionPruneInfo(from);
			break;
		case T_PartitionedRelPruneInfo:
			retval = _copyPartitionedRelPruneInfo(from);
			break;
		case T_PartitionPruneStepOp:
			retval = _copyPartitionPruneStepOp(from);
			break;
		case T_PartitionPruneStepCombine:
			retval = _copyPartitionPruneStepCombine(from);
			break;
		case T_PlanInvalItem:
			retval = _copyPlanInvalItem(from);
			break;

			/*
			 * PRIMITIVE NODES
			 */
		case T_Alias:
			retval = _copyAlias(from);
			break;
		case T_RangeVar:
			retval = _copyRangeVar(from);
			break;
		case T_TableFunc:
			retval = _copyTableFunc(from);
			break;
		case T_IntoClause:
			retval = _copyIntoClause(from);
			break;
		case T_Var:
			retval = _copyVar(from);
			break;
		case T_Const:
			retval = _copyConst(from);
			break;
		case T_Param:
			retval = _copyParam(from);
			break;
		case T_Aggref:
			retval = _copyAggref(from);
			break;
		case T_GroupingFunc:
			retval = _copyGroupingFunc(from);
			break;
		case T_WindowFunc:
			retval = _copyWindowFunc(from);
			break;
		case T_SubscriptingRef:
			retval = _copySubscriptingRef(from);
			break;
		case T_FuncExpr:
			retval = _copyFuncExpr(from);
			break;
		case T_NamedArgExpr:
			retval = _copyNamedArgExpr(from);
			break;
		case T_OpExpr:
			retval = _copyOpExpr(from);
			break;
		case T_DistinctExpr:
			retval = _copyDistinctExpr(from);
			break;
		case T_NullIfExpr:
			retval = _copyNullIfExpr(from);
			break;
		case T_ScalarArrayOpExpr:
			retval = _copyScalarArrayOpExpr(from);
			break;
		case T_BoolExpr:
			retval = _copyBoolExpr(from);
			break;
		case T_SubLink:
			retval = _copySubLink(from);
			break;
		case T_SubPlan:
			retval = _copySubPlan(from);
			break;
		case T_AlternativeSubPlan:
			retval = _copyAlternativeSubPlan(from);
			break;
		case T_FieldSelect:
			retval = _copyFieldSelect(from);
			break;
		case T_FieldStore:
			retval = _copyFieldStore(from);
			break;
		case T_RelabelType:
			retval = _copyRelabelType(from);
			break;
		case T_CoerceViaIO:
			retval = _copyCoerceViaIO(from);
			break;
		case T_ArrayCoerceExpr:
			retval = _copyArrayCoerceExpr(from);
			break;
		case T_ConvertRowtypeExpr:
			retval = _copyConvertRowtypeExpr(from);
			break;
		case T_CollateExpr:
			retval = _copyCollateExpr(from);
			break;
		case T_CaseExpr:
			retval = _copyCaseExpr(from);
			break;
		case T_CaseWhen:
			retval = _copyCaseWhen(from);
			break;
		case T_CaseTestExpr:
			retval = _copyCaseTestExpr(from);
			break;
		case T_ArrayExpr:
			retval = _copyArrayExpr(from);
			break;
		case T_RowExpr:
			retval = _copyRowExpr(from);
			break;
		case T_RowCompareExpr:
			retval = _copyRowCompareExpr(from);
			break;
		case T_CoalesceExpr:
			retval = _copyCoalesceExpr(from);
			break;
		case T_MinMaxExpr:
			retval = _copyMinMaxExpr(from);
			break;
		case T_SQLValueFunction:
			retval = _copySQLValueFunction(from);
			break;
		case T_XmlExpr:
			retval = _copyXmlExpr(from);
			break;
		case T_NullTest:
			retval = _copyNullTest(from);
			break;
		case T_BooleanTest:
			retval = _copyBooleanTest(from);
			break;
		case T_CoerceToDomain:
			retval = _copyCoerceToDomain(from);
			break;
		case T_CoerceToDomainValue:
			retval = _copyCoerceToDomainValue(from);
			break;
		case T_SetToDefault:
			retval = _copySetToDefault(from);
			break;
		case T_CurrentOfExpr:
			retval = _copyCurrentOfExpr(from);
			break;
		case T_NextValueExpr:
			retval = _copyNextValueExpr(from);
			break;
		case T_InferenceElem:
			retval = _copyInferenceElem(from);
			break;
		case T_TargetEntry:
			retval = _copyTargetEntry(from);
			break;
		case T_RangeTblRef:
			retval = _copyRangeTblRef(from);
			break;
		case T_JoinExpr:
			retval = _copyJoinExpr(from);
			break;
		case T_FromExpr:
			retval = _copyFromExpr(from);
			break;
		case T_OnConflictExpr:
			retval = _copyOnConflictExpr(from);
			break;

			/*
			 * RELATION NODES
			 */
		case T_PathKey:
			retval = _copyPathKey(from);
			break;
		case T_RestrictInfo:
			retval = _copyRestrictInfo(from);
			break;
		case T_PlaceHolderVar:
			retval = _copyPlaceHolderVar(from);
			break;
		case T_SpecialJoinInfo:
			retval = _copySpecialJoinInfo(from);
			break;
		case T_AppendRelInfo:
			retval = _copyAppendRelInfo(from);
			break;
		case T_PlaceHolderInfo:
			retval = _copyPlaceHolderInfo(from);
			break;

			/*
			 * VALUE NODES
			 */
		case T_Integer:
			retval = _copyInteger(from);
			break;
		case T_Float:
			retval = _copyFloat(from);
			break;
		case T_Boolean:
			retval = _copyBoolean(from);
			break;
		case T_String:
			retval = _copyString(from);
			break;
		case T_BitString:
			retval = _copyBitString(from);
			break;

			/*
			 * LIST NODES
			 */
		case T_List:
			retval = list_copy_deep(from);
			break;

			/*
			 * Lists of integers and OIDs don't need to be deep-copied, so we
			 * perform a shallow copy via list_copy()
			 */
		case T_IntList:
		case T_OidList:
			retval = list_copy(from);
			break;

			/*
			 * EXTENSIBLE NODES
			 */
		case T_ExtensibleNode:
			retval = _copyExtensibleNode(from);
			break;

			/*
			 * PARSE NODES
			 */
		case T_Query:
			retval = _copyQuery(from);
			break;
		case T_RawStmt:
			retval = _copyRawStmt(from);
			break;
		case T_InsertStmt:
			retval = _copyInsertStmt(from);
			break;
		case T_DeleteStmt:
			retval = _copyDeleteStmt(from);
			break;
		case T_UpdateStmt:
			retval = _copyUpdateStmt(from);
			break;
		case T_MergeStmt:
			retval = _copyMergeStmt(from);
			break;
		case T_SelectStmt:
			retval = _copySelectStmt(from);
			break;
		case T_SetOperationStmt:
			retval = _copySetOperationStmt(from);
			break;
		case T_ReturnStmt:
			retval = _copyReturnStmt(from);
			break;
		case T_PLAssignStmt:
			retval = _copyPLAssignStmt(from);
			break;
		case T_AlterTableStmt:
			retval = _copyAlterTableStmt(from);
			break;
		case T_AlterTableCmd:
			retval = _copyAlterTableCmd(from);
			break;
		case T_AlterCollationStmt:
			retval = _copyAlterCollationStmt(from);
			break;
		case T_AlterDomainStmt:
			retval = _copyAlterDomainStmt(from);
			break;
		case T_GrantStmt:
			retval = _copyGrantStmt(from);
			break;
		case T_GrantRoleStmt:
			retval = _copyGrantRoleStmt(from);
			break;
		case T_AlterDefaultPrivilegesStmt:
			retval = _copyAlterDefaultPrivilegesStmt(from);
			break;
		case T_DeclareCursorStmt:
			retval = _copyDeclareCursorStmt(from);
			break;
		case T_ClosePortalStmt:
			retval = _copyClosePortalStmt(from);
			break;
		case T_CallStmt:
			retval = _copyCallStmt(from);
			break;
		case T_ClusterStmt:
			retval = _copyClusterStmt(from);
			break;
		case T_CopyStmt:
			retval = _copyCopyStmt(from);
			break;
		case T_CreateStmt:
			retval = _copyCreateStmt(from);
			break;
		case T_TableLikeClause:
			retval = _copyTableLikeClause(from);
			break;
		case T_DefineStmt:
			retval = _copyDefineStmt(from);
			break;
		case T_DropStmt:
			retval = _copyDropStmt(from);
			break;
		case T_TruncateStmt:
			retval = _copyTruncateStmt(from);
			break;
		case T_CommentStmt:
			retval = _copyCommentStmt(from);
			break;
		case T_SecLabelStmt:
			retval = _copySecLabelStmt(from);
			break;
		case T_FetchStmt:
			retval = _copyFetchStmt(from);
			break;
		case T_IndexStmt:
			retval = _copyIndexStmt(from);
			break;
		case T_CreateStatsStmt:
			retval = _copyCreateStatsStmt(from);
			break;
		case T_AlterStatsStmt:
			retval = _copyAlterStatsStmt(from);
			break;
		case T_CreateFunctionStmt:
			retval = _copyCreateFunctionStmt(from);
			break;
		case T_FunctionParameter:
			retval = _copyFunctionParameter(from);
			break;
		case T_AlterFunctionStmt:
			retval = _copyAlterFunctionStmt(from);
			break;
		case T_DoStmt:
			retval = _copyDoStmt(from);
			break;
		case T_RenameStmt:
			retval = _copyRenameStmt(from);
			break;
		case T_AlterObjectDependsStmt:
			retval = _copyAlterObjectDependsStmt(from);
			break;
		case T_AlterObjectSchemaStmt:
			retval = _copyAlterObjectSchemaStmt(from);
			break;
		case T_AlterOwnerStmt:
			retval = _copyAlterOwnerStmt(from);
			break;
		case T_AlterOperatorStmt:
			retval = _copyAlterOperatorStmt(from);
			break;
		case T_AlterTypeStmt:
			retval = _copyAlterTypeStmt(from);
			break;
		case T_RuleStmt:
			retval = _copyRuleStmt(from);
			break;
		case T_NotifyStmt:
			retval = _copyNotifyStmt(from);
			break;
		case T_ListenStmt:
			retval = _copyListenStmt(from);
			break;
		case T_UnlistenStmt:
			retval = _copyUnlistenStmt(from);
			break;
		case T_TransactionStmt:
			retval = _copyTransactionStmt(from);
			break;
		case T_CompositeTypeStmt:
			retval = _copyCompositeTypeStmt(from);
			break;
		case T_CreateEnumStmt:
			retval = _copyCreateEnumStmt(from);
			break;
		case T_CreateRangeStmt:
			retval = _copyCreateRangeStmt(from);
			break;
		case T_AlterEnumStmt:
			retval = _copyAlterEnumStmt(from);
			break;
		case T_ViewStmt:
			retval = _copyViewStmt(from);
			break;
		case T_LoadStmt:
			retval = _copyLoadStmt(from);
			break;
		case T_CreateDomainStmt:
			retval = _copyCreateDomainStmt(from);
			break;
		case T_CreateOpClassStmt:
			retval = _copyCreateOpClassStmt(from);
			break;
		case T_CreateOpClassItem:
			retval = _copyCreateOpClassItem(from);
			break;
		case T_CreateOpFamilyStmt:
			retval = _copyCreateOpFamilyStmt(from);
			break;
		case T_AlterOpFamilyStmt:
			retval = _copyAlterOpFamilyStmt(from);
			break;
		case T_CreatedbStmt:
			retval = _copyCreatedbStmt(from);
			break;
		case T_AlterDatabaseStmt:
			retval = _copyAlterDatabaseStmt(from);
			break;
		case T_AlterDatabaseRefreshCollStmt:
			retval = _copyAlterDatabaseRefreshCollStmt(from);
			break;
		case T_AlterDatabaseSetStmt:
			retval = _copyAlterDatabaseSetStmt(from);
			break;
		case T_DropdbStmt:
			retval = _copyDropdbStmt(from);
			break;
		case T_VacuumStmt:
			retval = _copyVacuumStmt(from);
			break;
		case T_VacuumRelation:
			retval = _copyVacuumRelation(from);
			break;
		case T_ExplainStmt:
			retval = _copyExplainStmt(from);
			break;
		case T_CreateTableAsStmt:
			retval = _copyCreateTableAsStmt(from);
			break;
		case T_RefreshMatViewStmt:
			retval = _copyRefreshMatViewStmt(from);
			break;
		case T_ReplicaIdentityStmt:
			retval = _copyReplicaIdentityStmt(from);
			break;
		case T_AlterSystemStmt:
			retval = _copyAlterSystemStmt(from);
			break;
		case T_CreateSeqStmt:
			retval = _copyCreateSeqStmt(from);
			break;
		case T_AlterSeqStmt:
			retval = _copyAlterSeqStmt(from);
			break;
		case T_VariableSetStmt:
			retval = _copyVariableSetStmt(from);
			break;
		case T_VariableShowStmt:
			retval = _copyVariableShowStmt(from);
			break;
		case T_DiscardStmt:
			retval = _copyDiscardStmt(from);
			break;
		case T_YbCreateProfileStmt:
			retval = _copyCreateProfileStmt(from);
			break;
		case T_YbDropProfileStmt:
			retval = _copyDropProfileStmt(from);
			break;
		case T_CreateTableGroupStmt:
			retval = _copyCreateTableGroupStmt(from);
			break;
		case T_CreateTableSpaceStmt:
			retval = _copyCreateTableSpaceStmt(from);
			break;
		case T_DropTableSpaceStmt:
			retval = _copyDropTableSpaceStmt(from);
			break;
		case T_AlterTableSpaceOptionsStmt:
			retval = _copyAlterTableSpaceOptionsStmt(from);
			break;
		case T_AlterTableMoveAllStmt:
			retval = _copyAlterTableMoveAllStmt(from);
			break;
		case T_CreateExtensionStmt:
			retval = _copyCreateExtensionStmt(from);
			break;
		case T_AlterExtensionStmt:
			retval = _copyAlterExtensionStmt(from);
			break;
		case T_AlterExtensionContentsStmt:
			retval = _copyAlterExtensionContentsStmt(from);
			break;
		case T_CreateFdwStmt:
			retval = _copyCreateFdwStmt(from);
			break;
		case T_AlterFdwStmt:
			retval = _copyAlterFdwStmt(from);
			break;
		case T_CreateForeignServerStmt:
			retval = _copyCreateForeignServerStmt(from);
			break;
		case T_AlterForeignServerStmt:
			retval = _copyAlterForeignServerStmt(from);
			break;
		case T_CreateUserMappingStmt:
			retval = _copyCreateUserMappingStmt(from);
			break;
		case T_AlterUserMappingStmt:
			retval = _copyAlterUserMappingStmt(from);
			break;
		case T_DropUserMappingStmt:
			retval = _copyDropUserMappingStmt(from);
			break;
		case T_CreateForeignTableStmt:
			retval = _copyCreateForeignTableStmt(from);
			break;
		case T_ImportForeignSchemaStmt:
			retval = _copyImportForeignSchemaStmt(from);
			break;
		case T_CreateTransformStmt:
			retval = _copyCreateTransformStmt(from);
			break;
		case T_CreateAmStmt:
			retval = _copyCreateAmStmt(from);
			break;
		case T_CreateTrigStmt:
			retval = _copyCreateTrigStmt(from);
			break;
		case T_CreateEventTrigStmt:
			retval = _copyCreateEventTrigStmt(from);
			break;
		case T_AlterEventTrigStmt:
			retval = _copyAlterEventTrigStmt(from);
			break;
		case T_CreatePLangStmt:
			retval = _copyCreatePLangStmt(from);
			break;
		case T_CreateRoleStmt:
			retval = _copyCreateRoleStmt(from);
			break;
		case T_AlterRoleStmt:
			retval = _copyAlterRoleStmt(from);
			break;
		case T_AlterRoleSetStmt:
			retval = _copyAlterRoleSetStmt(from);
			break;
		case T_DropRoleStmt:
			retval = _copyDropRoleStmt(from);
			break;
		case T_LockStmt:
			retval = _copyLockStmt(from);
			break;
		case T_ConstraintsSetStmt:
			retval = _copyConstraintsSetStmt(from);
			break;
		case T_ReindexStmt:
			retval = _copyReindexStmt(from);
			break;
		case T_CheckPointStmt:
			retval = (void *) makeNode(CheckPointStmt);
			break;
		case T_CreateSchemaStmt:
			retval = _copyCreateSchemaStmt(from);
			break;
		case T_CreateConversionStmt:
			retval = _copyCreateConversionStmt(from);
			break;
		case T_CreateCastStmt:
			retval = _copyCreateCastStmt(from);
			break;
		case T_PrepareStmt:
			retval = _copyPrepareStmt(from);
			break;
		case T_ExecuteStmt:
			retval = _copyExecuteStmt(from);
			break;
		case T_DeallocateStmt:
			retval = _copyDeallocateStmt(from);
			break;
		case T_DropOwnedStmt:
			retval = _copyDropOwnedStmt(from);
			break;
		case T_ReassignOwnedStmt:
			retval = _copyReassignOwnedStmt(from);
			break;
		case T_AlterTSDictionaryStmt:
			retval = _copyAlterTSDictionaryStmt(from);
			break;
		case T_AlterTSConfigurationStmt:
			retval = _copyAlterTSConfigurationStmt(from);
			break;
		case T_CreatePolicyStmt:
			retval = _copyCreatePolicyStmt(from);
			break;
		case T_AlterPolicyStmt:
			retval = _copyAlterPolicyStmt(from);
			break;
		case T_CreatePublicationStmt:
			retval = _copyCreatePublicationStmt(from);
			break;
		case T_AlterPublicationStmt:
			retval = _copyAlterPublicationStmt(from);
			break;
		case T_CreateSubscriptionStmt:
			retval = _copyCreateSubscriptionStmt(from);
			break;
		case T_AlterSubscriptionStmt:
			retval = _copyAlterSubscriptionStmt(from);
			break;
		case T_DropSubscriptionStmt:
			retval = _copyDropSubscriptionStmt(from);
			break;
		case T_A_Expr:
			retval = _copyA_Expr(from);
			break;
		case T_ColumnRef:
			retval = _copyColumnRef(from);
			break;
		case T_ParamRef:
			retval = _copyParamRef(from);
			break;
		case T_A_Const:
			retval = _copyA_Const(from);
			break;
		case T_FuncCall:
			retval = _copyFuncCall(from);
			break;
		case T_A_Star:
			retval = _copyA_Star(from);
			break;
		case T_A_Indices:
			retval = _copyA_Indices(from);
			break;
		case T_A_Indirection:
			retval = _copyA_Indirection(from);
			break;
		case T_A_ArrayExpr:
			retval = _copyA_ArrayExpr(from);
			break;
		case T_ResTarget:
			retval = _copyResTarget(from);
			break;
		case T_MultiAssignRef:
			retval = _copyMultiAssignRef(from);
			break;
		case T_TypeCast:
			retval = _copyTypeCast(from);
			break;
		case T_CollateClause:
			retval = _copyCollateClause(from);
			break;
		case T_SortBy:
			retval = _copySortBy(from);
			break;
		case T_WindowDef:
			retval = _copyWindowDef(from);
			break;
		case T_RangeSubselect:
			retval = _copyRangeSubselect(from);
			break;
		case T_RangeFunction:
			retval = _copyRangeFunction(from);
			break;
		case T_RangeTableSample:
			retval = _copyRangeTableSample(from);
			break;
		case T_RangeTableFunc:
			retval = _copyRangeTableFunc(from);
			break;
		case T_RangeTableFuncCol:
			retval = _copyRangeTableFuncCol(from);
			break;
		case T_TypeName:
			retval = _copyTypeName(from);
			break;
		case T_IndexElem:
			retval = _copyIndexElem(from);
			break;
		case T_StatsElem:
			retval = _copyStatsElem(from);
			break;
		case T_ColumnDef:
			retval = _copyColumnDef(from);
			break;
		case T_Constraint:
			retval = _copyConstraint(from);
			break;
		case T_DefElem:
			retval = _copyDefElem(from);
			break;
		case T_LockingClause:
			retval = _copyLockingClause(from);
			break;
		case T_RangeTblEntry:
			retval = _copyRangeTblEntry(from);
			break;
		case T_RangeTblFunction:
			retval = _copyRangeTblFunction(from);
			break;
		case T_TableSampleClause:
			retval = _copyTableSampleClause(from);
			break;
		case T_WithCheckOption:
			retval = _copyWithCheckOption(from);
			break;
		case T_SortGroupClause:
			retval = _copySortGroupClause(from);
			break;
		case T_GroupingSet:
			retval = _copyGroupingSet(from);
			break;
		case T_WindowClause:
			retval = _copyWindowClause(from);
			break;
		case T_RowMarkClause:
			retval = _copyRowMarkClause(from);
			break;
		case T_WithClause:
			retval = _copyWithClause(from);
			break;
		case T_InferClause:
			retval = _copyInferClause(from);
			break;
		case T_OnConflictClause:
			retval = _copyOnConflictClause(from);
			break;
		case T_CTESearchClause:
			retval = _copyCTESearchClause(from);
			break;
		case T_CTECycleClause:
			retval = _copyCTECycleClause(from);
			break;
		case T_CommonTableExpr:
			retval = _copyCommonTableExpr(from);
			break;
		case T_MergeWhenClause:
			retval = _copyMergeWhenClause(from);
			break;
		case T_MergeAction:
			retval = _copyMergeAction(from);
			break;
		case T_ObjectWithArgs:
			retval = _copyObjectWithArgs(from);
			break;
		case T_AccessPriv:
			retval = _copyAccessPriv(from);
			break;
		case T_XmlSerialize:
			retval = _copyXmlSerialize(from);
			break;
		case T_RoleSpec:
			retval = _copyRoleSpec(from);
			break;
		case T_TriggerTransition:
			retval = _copyTriggerTransition(from);
			break;
		case T_PartitionElem:
			retval = _copyPartitionElem(from);
			break;
		case T_PartitionSpec:
			retval = _copyPartitionSpec(from);
			break;
		case T_PartitionBoundSpec:
			retval = _copyPartitionBoundSpec(from);
			break;
		case T_PartitionRangeDatum:
			retval = _copyPartitionRangeDatum(from);
			break;
		case T_PartitionCmd:
			retval = _copyPartitionCmd(from);
			break;
		case T_PublicationObjSpec:
			retval = _copyPublicationObject(from);
			break;
		case T_PublicationTable:
			retval = _copyPublicationTable(from);
			break;
		case T_OptSplit:
			retval = _copyOptSplit(from);
			break;

			/*
			 * MISCELLANEOUS NODES
			 */
		case T_ForeignKeyCacheInfo:
			retval = _copyForeignKeyCacheInfo(from);
			break;

		case T_BackfillIndexStmt:
			retval = _copyBackfillIndexStmt(from);
			break;

		case T_YbBackfillInfo:
			retval = _copyYbBackfillInfo(from);
			break;

		case T_RowBounds:
			retval = _copyRowBounds(from);
			break;

		case T_YbExprColrefDesc:
			retval = _copyYbExprColrefDesc(from);
			break;

		case T_YbBatchedExpr:
			retval = _copyYbBatchedExpr(from);
			break;

		case T_YbSkippableEntities:
			retval = _copyYbSkippableEntities(from);
			break;

		case T_YbUpdateAffectedEntities:
			retval = _copyYbUpdateAffectedEntities(from);
			break;

		default:
			elog(ERROR, "unrecognized node type: %d", (int) nodeTag(from));
			retval = 0;			/* keep compiler quiet */
			break;
	}

	return retval;
}
