/*-------------------------------------------------------------------------
 *
 * execProcnode.c
 *	 contains dispatch functions which call the appropriate "initialize",
 *	 "get a tuple", and "cleanup" routines for the given node type.
 *	 If the node has children, then it will presumably call ExecInitNode,
 *	 ExecProcNode, or ExecEndNode on its subnodes and do the appropriate
 *	 processing.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/execProcnode.c
 *
 *-------------------------------------------------------------------------
 */
/*
 *	 NOTES
 *		This used to be three files.  It is now all combined into
 *		one file so that it is easier to keep the dispatch routines
 *		in sync when new nodes are added.
 *
 *	 EXAMPLE
 *		Suppose we want the age of the manager of the shoe department and
 *		the number of employees in that department.  So we have the query:
 *
 *				select DEPT.no_emps, EMP.age
 *				from DEPT, EMP
 *				where EMP.name = DEPT.mgr and
 *					  DEPT.name = "shoe"
 *
 *		Suppose the planner gives us the following plan:
 *
 *						Nest Loop (DEPT.mgr = EMP.name)
 *						/		\
 *					   /		 \
 *				   Seq Scan		Seq Scan
 *					DEPT		  EMP
 *				(name = "shoe")
 *
 *		ExecutorStart() is called first.
 *		It calls InitPlan() which calls ExecInitNode() on
 *		the root of the plan -- the nest loop node.
 *
 *	  * ExecInitNode() notices that it is looking at a nest loop and
 *		as the code below demonstrates, it calls ExecInitNestLoop().
 *		Eventually this calls ExecInitNode() on the right and left subplans
 *		and so forth until the entire plan is initialized.  The result
 *		of ExecInitNode() is a plan state tree built with the same structure
 *		as the underlying plan tree.
 *
 *	  * Then when ExecutorRun() is called, it calls ExecutePlan() which calls
 *		ExecProcNode() repeatedly on the top node of the plan state tree.
 *		Each time this happens, ExecProcNode() will end up calling
 *		ExecNestLoop(), which calls ExecProcNode() on its subplans.
 *		Each of these subplans is a sequential scan so ExecSeqScan() is
 *		called.  The slots returned by ExecSeqScan() may contain
 *		tuples which contain the attributes ExecNestLoop() uses to
 *		form the tuples it returns.
 *
 *	  * Eventually ExecSeqScan() stops returning tuples and the nest
 *		loop join ends.  Lastly, ExecutorEnd() calls ExecEndNode() which
 *		calls ExecEndNestLoop() which in turn calls ExecEndNode() on
 *		its subplans which result in ExecEndSeqScan().
 *
 *		This should show how the executor works by having
 *		ExecInitNode(), ExecProcNode() and ExecEndNode() dispatch
 *		their work to the appropriate node support routines which may
 *		in turn call these routines themselves on their subplans.
 */
#include "postgres.h"

#include "executor/executor.h"
#include "executor/nodeAgg.h"
#include "executor/nodeAppend.h"
#include "executor/nodeBitmapAnd.h"
#include "executor/nodeBitmapHeapscan.h"
#include "executor/nodeBitmapIndexscan.h"
#include "executor/nodeBitmapOr.h"
#include "executor/nodeCtescan.h"
#include "executor/nodeCustom.h"
#include "executor/nodeForeignscan.h"
#include "executor/nodeFunctionscan.h"
#include "executor/nodeGather.h"
#include "executor/nodeGatherMerge.h"
#include "executor/nodeGroup.h"
#include "executor/nodeHash.h"
#include "executor/nodeHashjoin.h"
#include "executor/nodeIncrementalSort.h"
#include "executor/nodeIndexonlyscan.h"
#include "executor/nodeIndexscan.h"
#include "executor/nodeLimit.h"
#include "executor/nodeLockRows.h"
#include "executor/nodeMaterial.h"
#include "executor/nodeMemoize.h"
#include "executor/nodeMergeAppend.h"
#include "executor/nodeMergejoin.h"
#include "executor/nodeModifyTable.h"
#include "executor/nodeNamedtuplestorescan.h"
#include "executor/nodeNestloop.h"
#include "executor/nodeProjectSet.h"
#include "executor/nodeRecursiveunion.h"
#include "executor/nodeResult.h"
#include "executor/nodeSamplescan.h"
#include "executor/nodeSeqscan.h"
#include "executor/nodeSetOp.h"
#include "executor/nodeSort.h"
#include "executor/nodeSubplan.h"
#include "executor/nodeSubqueryscan.h"
#include "executor/nodeTableFuncscan.h"
#include "executor/nodeTidrangescan.h"
#include "executor/nodeTidscan.h"
#include "executor/nodeUnique.h"
#include "executor/nodeValuesscan.h"
#include "executor/nodeWindowAgg.h"
#include "executor/nodeWorktablescan.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"

/* YB includes */
#include "executor/nodeYbBatchedNestloop.h"
#include "executor/nodeYbBitmapIndexscan.h"
#include "executor/nodeYbBitmapTablescan.h"
#include "executor/nodeYbSeqscan.h"
#include "pg_yb_utils.h"

static TupleTableSlot *ExecProcNodeFirst(PlanState *node);
static TupleTableSlot *ExecProcNodeInstr(PlanState *node);
static TupleTableSlot *ExecProcNodeYbDistTrace(PlanState *node);
static const char *YbGetExecNodeSpanName(PlanState *node);
static bool ExecShutdownNode_walker(PlanState *node, void *context);


/* ------------------------------------------------------------------------
 *		ExecInitNode
 *
 *		Recursively initializes all the nodes in the plan tree rooted
 *		at 'node'.
 *
 *		Inputs:
 *		  'node' is the current node of the plan produced by the query planner
 *		  'estate' is the shared execution state for the plan tree
 *		  'eflags' is a bitwise OR of flag bits described in executor.h
 *
 *		Returns a PlanState node corresponding to the given Plan node.
 * ------------------------------------------------------------------------
 */
PlanState *
ExecInitNode(Plan *node, EState *estate, int eflags)
{
	PlanState  *result;
	List	   *subps;
	ListCell   *l;

	/*
	 * do nothing when we get to the end of a leaf on tree.
	 */
	if (node == NULL)
		return NULL;

	/*
	 * Make sure there's enough stack available. Need to check here, in
	 * addition to ExecProcNode() (via ExecProcNodeFirst()), to ensure the
	 * stack isn't overrun while initializing the node tree.
	 */
	check_stack_depth();

	switch (nodeTag(node))
	{
			/*
			 * control nodes
			 */
		case T_Result:
			result = (PlanState *) ExecInitResult((Result *) node,
												  estate, eflags);
			break;

		case T_ProjectSet:
			result = (PlanState *) ExecInitProjectSet((ProjectSet *) node,
													  estate, eflags);
			break;

		case T_ModifyTable:
			result = (PlanState *) ExecInitModifyTable((ModifyTable *) node,
													   estate, eflags);
			break;

		case T_Append:
			result = (PlanState *) ExecInitAppend((Append *) node,
												  estate, eflags);
			break;

		case T_MergeAppend:
			result = (PlanState *) ExecInitMergeAppend((MergeAppend *) node,
													   estate, eflags);
			break;

		case T_RecursiveUnion:
			result = (PlanState *) ExecInitRecursiveUnion((RecursiveUnion *) node,
														  estate, eflags);
			break;

		case T_BitmapAnd:
			result = (PlanState *) ExecInitBitmapAnd((BitmapAnd *) node,
													 estate, eflags);
			break;

		case T_BitmapOr:
			result = (PlanState *) ExecInitBitmapOr((BitmapOr *) node,
													estate, eflags);
			break;

			/*
			 * scan nodes
			 */
		case T_SeqScan:
			result = (PlanState *) ExecInitSeqScan((SeqScan *) node,
												   estate, eflags);
			break;

		case T_YbSeqScan:
			result = (PlanState *) ExecInitYbSeqScan((YbSeqScan *) node,
													 estate, eflags);
			break;

		case T_SampleScan:
			result = (PlanState *) ExecInitSampleScan((SampleScan *) node,
													  estate, eflags);
			break;

		case T_IndexScan:
			result = (PlanState *) ExecInitIndexScan((IndexScan *) node,
													 estate, eflags);
			break;

		case T_IndexOnlyScan:
			result = (PlanState *) ExecInitIndexOnlyScan((IndexOnlyScan *) node,
														 estate, eflags);
			break;

		case T_BitmapIndexScan:
			result = (PlanState *) ExecInitBitmapIndexScan((BitmapIndexScan *) node,
														   estate, eflags);
			break;

		case T_YbBitmapIndexScan:
			result = (PlanState *) ExecInitYbBitmapIndexScan((YbBitmapIndexScan *) node,
															 estate, eflags);
			break;

		case T_BitmapHeapScan:
			result = (PlanState *) ExecInitBitmapHeapScan((BitmapHeapScan *) node,
														  estate, eflags);
			break;

		case T_YbBitmapTableScan:
			result = (PlanState *) ExecInitYbBitmapTableScan((YbBitmapTableScan *) node,
															 estate, eflags);
			break;

		case T_TidScan:
			result = (PlanState *) ExecInitTidScan((TidScan *) node,
												   estate, eflags);
			break;

		case T_TidRangeScan:
			result = (PlanState *) ExecInitTidRangeScan((TidRangeScan *) node,
														estate, eflags);
			break;

		case T_SubqueryScan:
			result = (PlanState *) ExecInitSubqueryScan((SubqueryScan *) node,
														estate, eflags);
			break;

		case T_FunctionScan:
			result = (PlanState *) ExecInitFunctionScan((FunctionScan *) node,
														estate, eflags);
			break;

		case T_TableFuncScan:
			result = (PlanState *) ExecInitTableFuncScan((TableFuncScan *) node,
														 estate, eflags);
			break;

		case T_ValuesScan:
			result = (PlanState *) ExecInitValuesScan((ValuesScan *) node,
													  estate, eflags);
			break;

		case T_CteScan:
			result = (PlanState *) ExecInitCteScan((CteScan *) node,
												   estate, eflags);
			break;

		case T_NamedTuplestoreScan:
			result = (PlanState *) ExecInitNamedTuplestoreScan((NamedTuplestoreScan *) node,
															   estate, eflags);
			break;

		case T_WorkTableScan:
			result = (PlanState *) ExecInitWorkTableScan((WorkTableScan *) node,
														 estate, eflags);
			break;

		case T_ForeignScan:
			result = (PlanState *) ExecInitForeignScan((ForeignScan *) node,
													   estate, eflags);
			break;

		case T_CustomScan:
			result = (PlanState *) ExecInitCustomScan((CustomScan *) node,
													  estate, eflags);
			break;

			/*
			 * join nodes
			 */
		case T_NestLoop:
			result = (PlanState *) ExecInitNestLoop((NestLoop *) node,
													estate, eflags);
			break;

		case T_YbBatchedNestLoop:
			result = (PlanState *) ExecInitYbBatchedNestLoop((YbBatchedNestLoop *) node,
															 estate, eflags);
			break;

		case T_MergeJoin:
			result = (PlanState *) ExecInitMergeJoin((MergeJoin *) node,
													 estate, eflags);
			break;

		case T_HashJoin:
			result = (PlanState *) ExecInitHashJoin((HashJoin *) node,
													estate, eflags);
			break;

			/*
			 * materialization nodes
			 */
		case T_Material:
			result = (PlanState *) ExecInitMaterial((Material *) node,
													estate, eflags);
			break;

		case T_Sort:
			result = (PlanState *) ExecInitSort((Sort *) node,
												estate, eflags);
			break;

		case T_IncrementalSort:
			result = (PlanState *) ExecInitIncrementalSort((IncrementalSort *) node,
														   estate, eflags);
			break;

		case T_Memoize:
			result = (PlanState *) ExecInitMemoize((Memoize *) node, estate,
												   eflags);
			break;

		case T_Group:
			result = (PlanState *) ExecInitGroup((Group *) node,
												 estate, eflags);
			break;

		case T_Agg:
			result = (PlanState *) ExecInitAgg((Agg *) node,
											   estate, eflags);
			break;

		case T_WindowAgg:
			result = (PlanState *) ExecInitWindowAgg((WindowAgg *) node,
													 estate, eflags);
			break;

		case T_Unique:
			result = (PlanState *) ExecInitUnique((Unique *) node,
												  estate, eflags);
			break;

		case T_Gather:
			result = (PlanState *) ExecInitGather((Gather *) node,
												  estate, eflags);
			break;

		case T_GatherMerge:
			result = (PlanState *) ExecInitGatherMerge((GatherMerge *) node,
													   estate, eflags);
			break;

		case T_Hash:
			result = (PlanState *) ExecInitHash((Hash *) node,
												estate, eflags);
			break;

		case T_SetOp:
			result = (PlanState *) ExecInitSetOp((SetOp *) node,
												 estate, eflags);
			break;

		case T_LockRows:
			result = (PlanState *) ExecInitLockRows((LockRows *) node,
													estate, eflags);
			break;

		case T_Limit:
			result = (PlanState *) ExecInitLimit((Limit *) node,
												 estate, eflags);
			break;

		default:
			elog(ERROR, "unrecognized node type: %d", (int) nodeTag(node));
			result = NULL;		/* keep compiler quiet */
			break;
	}

	ExecSetExecProcNode(result, result->ExecProcNode);

	/*
	 * Initialize any initPlans present in this node.  The planner put them in
	 * a separate list for us.
	 */
	subps = NIL;
	foreach(l, node->initPlan)
	{
		SubPlan    *subplan = (SubPlan *) lfirst(l);
		SubPlanState *sstate;

		Assert(IsA(subplan, SubPlan));
		sstate = ExecInitSubPlan(subplan, result);
		subps = lappend(subps, sstate);
	}
	result->initPlan = subps;

	/* Set up instrumentation for this node if requested */
	if (estate->es_instrument)
		result->instrument = InstrAlloc(1, estate->es_instrument,
										result->async_capable);

	return result;
}


/*
 * If a node wants to change its ExecProcNode function after ExecInitNode()
 * has finished, it should do so with this function.  That way any wrapper
 * functions can be reinstalled, without the node having to know how that
 * works.
 */
void
ExecSetExecProcNode(PlanState *node, ExecProcNodeMtd function)
{
	/*
	 * Add a wrapper around the ExecProcNode callback that checks stack depth
	 * during the first execution and maybe adds an instrumentation wrapper.
	 * When the callback is changed after execution has already begun that
	 * means we'll superfluously execute ExecProcNodeFirst, but that seems ok.
	 */
	node->ExecProcNodeReal = function;
	node->ExecProcNode = ExecProcNodeFirst;
}


/*
 * ExecProcNode wrapper that performs some one-time checks, before calling
 * the relevant node method (possibly via an instrumentation wrapper).
 */
static TupleTableSlot *
ExecProcNodeFirst(PlanState *node)
{
	/*
	 * Perform stack depth check during the first execution of the node.  We
	 * only do so the first time round because it turns out to not be cheap on
	 * some common architectures (eg. x86).  This relies on the assumption
	 * that ExecProcNode calls for a given plan node will always be made at
	 * roughly the same stack depth.
	 */
	check_stack_depth();

	/*
	 * If instrumentation is required, change the wrapper to one that just
	 * does instrumentation.  Otherwise we can dispense with all wrappers and
	 * have ExecProcNode() directly call the relevant function from now on.
	 *
	 * YB: When distributed tracing is active, ExecProcNodeInstr wraps spans
	 * around ExecProcNodeReal internally so that span durations exclude
	 * instrumentation overhead.  When there is no instrumentation but tracing
	 * is enabled, ExecProcNodeYbDistTrace is installed instead.
	 */
	if (node->instrument)
		node->ExecProcNode = ExecProcNodeInstr;
	else if (YBCIsDistTraceActive())
		node->ExecProcNode = ExecProcNodeYbDistTrace;
	else
		node->ExecProcNode = node->ExecProcNodeReal;

	return node->ExecProcNode(node);
}


/*
 * YbGetExecNodeSpanName
 *
 * Map a PlanState node tag to the corresponding executor span name
 * for distributed tracing.
 */
static const char *
YbGetExecNodeSpanName(PlanState *node)
{
	switch (nodeTag(node))
	{
		/* control nodes */
		case T_ResultState:
			return "Result";
		case T_ProjectSetState:
			return "ProjectSet";
		case T_ModifyTableState:
			switch (((ModifyTableState *) node)->operation)
			{
				case CMD_INSERT:
					return "Insert";
				case CMD_UPDATE:
					return "Update";
				case CMD_DELETE:
					return "Delete";
				case CMD_MERGE:
					return "Merge";
				default:
					return "ModifyTable ???"; /* Following explain.c */
			}
		case T_AppendState:
			return "Append";
		case T_MergeAppendState:
			return "Merge Append";
		case T_RecursiveUnionState:
			return "Recursive Union";
		case T_BitmapAndState:
			return "BitmapAnd";
		case T_BitmapOrState:
			return "BitmapOr";

		/* scan nodes */
		case T_SeqScanState:
			return "Seq Scan";
		case T_YbSeqScanState:
			return "Seq Scan";
		case T_SampleScanState:
			return "Sample Scan";
		case T_IndexScanState:
			return "Index Scan";
		case T_IndexOnlyScanState:
			return "Index Only Scan";
		case T_BitmapIndexScanState:
			return "Bitmap Index Scan";
		case T_BitmapHeapScanState:
			return "Bitmap Heap Scan";
		case T_YbBitmapIndexScanState:
			return "Bitmap Index Scan";
		case T_YbBitmapTableScanState:
			return "YB Bitmap Table Scan";
		case T_TidScanState:
			return "Tid Scan";
		case T_TidRangeScanState:
			return "Tid Range Scan";
		case T_SubqueryScanState:
			return "Subquery Scan";
		case T_FunctionScanState:
			return "Function Scan";
		case T_TableFuncScanState:
			return "Table Function Scan";
		case T_ValuesScanState:
			return "Values Scan";
		case T_CteScanState:
			return "CTE Scan";
		case T_NamedTuplestoreScanState:
			return "Named Tuplestore Scan";
		case T_WorkTableScanState:
			return "WorkTable Scan";
		case T_ForeignScanState:
			switch (((ForeignScan *) node->plan)->operation)
			{
				case CMD_SELECT:
					if (IsYBRelation(((ScanState *) node)->ss_currentRelation))
						return "YB Foreign Scan";
					return "Foreign Scan";
				case CMD_INSERT:
					return "Foreign Insert";
				case CMD_UPDATE:
					return "Foreign Update";
				case CMD_DELETE:
					return "Foreign Delete";
				default:
					return "Foreign ???";  /* Following explain.c */
			}
		case T_CustomScanState:
			return "Custom Scan";

		/* join nodes */
		case T_NestLoopState:
			return "Nested Loop";
		case T_YbBatchedNestLoopState:
			return "YB Batched Nested Loop";
		case T_MergeJoinState:
			return "Merge Join";
		case T_HashJoinState:
			return "Hash Join";

		/* materialization nodes */
		case T_MaterialState:
			return "Materialize";
		case T_MemoizeState:
			return "Memoize";
		case T_SortState:
			return "Sort";
		case T_IncrementalSortState:
			return "Incremental Sort";
		case T_GroupState:
			return "Group";
		case T_AggState:
			return "Aggregate";
		case T_WindowAggState:
			return "WindowAgg";
		case T_UniqueState:
			return "Unique";
		case T_GatherState:
			return "Gather";
		case T_GatherMergeState:
			return "Gather Merge";
		case T_HashState:
			return "Hash";
		case T_SetOpState:
			return "SetOp";
		case T_LockRowsState:
			return "LockRows";
		case T_LimitState:
			return "Limit";
		/*
		 * Non-executor-state node tags listed explicitly so the compiler
		 * warns if a new NodeTag value is added without updating this
		 * function.
		 */

		/* executor support nodes (execnodes.h) */
		case T_Invalid:
		case T_IndexInfo:
		case T_ExprContext:
		case T_ProjectionInfo:
		case T_JunkFilter:
		case T_OnConflictSetState:
		case T_MergeActionState:
		case T_ResultRelInfo:
		case T_EState:
		case T_TupleTableSlot:

		/* plan nodes (plannodes.h) */
		case T_Plan:
		case T_Result:
		case T_ProjectSet:
		case T_ModifyTable:
		case T_Append:
		case T_MergeAppend:
		case T_RecursiveUnion:
		case T_BitmapAnd:
		case T_BitmapOr:
		case T_Scan:
		case T_SeqScan:
		case T_SampleScan:
		case T_IndexScan:
		case T_IndexOnlyScan:
		case T_BitmapIndexScan:
		case T_BitmapHeapScan:
		case T_YbBitmapIndexScan:
		case T_YbBitmapTableScan:
		case T_TidScan:
		case T_TidRangeScan:
		case T_SubqueryScan:
		case T_FunctionScan:
		case T_ValuesScan:
		case T_TableFuncScan:
		case T_CteScan:
		case T_NamedTuplestoreScan:
		case T_WorkTableScan:
		case T_ForeignScan:
		case T_CustomScan:
		case T_Join:
		case T_NestLoop:
		case T_MergeJoin:
		case T_HashJoin:
		case T_Material:
		case T_Memoize:
		case T_Sort:
		case T_IncrementalSort:
		case T_Group:
		case T_Agg:
		case T_WindowAgg:
		case T_Unique:
		case T_Gather:
		case T_GatherMerge:
		case T_Hash:
		case T_SetOp:
		case T_LockRows:
		case T_Limit:
		case T_NestLoopParam:
		case T_PlanRowMark:
		case T_PartitionPruneInfo:
		case T_PartitionedRelPruneInfo:
		case T_PartitionPruneStepOp:
		case T_PartitionPruneStepCombine:
		case T_PlanInvalItem:

		/* base plan state types */
		case T_PlanState:
		case T_ScanState:
		case T_JoinState:

		/* primitive nodes (primnodes.h) */
		case T_Alias:
		case T_RangeVar:
		case T_TableFunc:
		case T_Var:
		case T_Const:
		case T_Param:
		case T_YbBatchedExpr:
		case T_Aggref:
		case T_GroupingFunc:
		case T_WindowFunc:
		case T_SubscriptingRef:
		case T_FuncExpr:
		case T_NamedArgExpr:
		case T_OpExpr:
		case T_DistinctExpr:
		case T_NullIfExpr:
		case T_ScalarArrayOpExpr:
		case T_BoolExpr:
		case T_SubLink:
		case T_SubPlan:
		case T_AlternativeSubPlan:
		case T_FieldSelect:
		case T_FieldStore:
		case T_RelabelType:
		case T_CoerceViaIO:
		case T_ArrayCoerceExpr:
		case T_ConvertRowtypeExpr:
		case T_CollateExpr:
		case T_CaseExpr:
		case T_CaseWhen:
		case T_CaseTestExpr:
		case T_ArrayExpr:
		case T_RowExpr:
		case T_RowCompareExpr:
		case T_CoalesceExpr:
		case T_MinMaxExpr:
		case T_SQLValueFunction:
		case T_XmlExpr:
		case T_NullTest:
		case T_BooleanTest:
		case T_CoerceToDomain:
		case T_CoerceToDomainValue:
		case T_SetToDefault:
		case T_CurrentOfExpr:
		case T_NextValueExpr:
		case T_InferenceElem:
		case T_TargetEntry:
		case T_RangeTblRef:
		case T_JoinExpr:
		case T_FromExpr:
		case T_OnConflictExpr:
		case T_IntoClause:

		/* expression state nodes (execnodes.h) */
		case T_ExprState:
		case T_WindowFuncExprState:
		case T_SetExprState:
		case T_SubPlanState:
		case T_DomainConstraintState:

		/* planner nodes (pathnodes.h) */
		case T_PlannerInfo:
		case T_PlannerGlobal:
		case T_RelOptInfo:
		case T_IndexOptInfo:
		case T_ForeignKeyOptInfo:
		case T_ParamPathInfo:
		case T_Path:
		case T_IndexPath:
		case T_BitmapHeapPath:
		case T_YbBitmapTablePath:
		case T_BitmapAndPath:
		case T_BitmapOrPath:
		case T_TidPath:
		case T_TidRangePath:
		case T_SubqueryScanPath:
		case T_ForeignPath:
		case T_CustomPath:
		case T_NestPath:
		case T_MergePath:
		case T_HashPath:
		case T_AppendPath:
		case T_MergeAppendPath:
		case T_GroupResultPath:
		case T_MaterialPath:
		case T_MemoizePath:
		case T_UniquePath:
		case T_GatherPath:
		case T_GatherMergePath:
		case T_ProjectionPath:
		case T_ProjectSetPath:
		case T_SortPath:
		case T_IncrementalSortPath:
		case T_GroupPath:
		case T_UpperUniquePath:
		case T_AggPath:
		case T_GroupingSetsPath:
		case T_MinMaxAggPath:
		case T_WindowAggPath:
		case T_SetOpPath:
		case T_RecursiveUnionPath:
		case T_LockRowsPath:
		case T_ModifyTablePath:
		case T_LimitPath:
		case T_EquivalenceClass:
		case T_EquivalenceMember:
		case T_PathKey:
		case T_PathKeyInfo:
		case T_PathTarget:
		case T_RestrictInfo:
		case T_IndexClause:
		case T_PlaceHolderVar:
		case T_SpecialJoinInfo:
		case T_AppendRelInfo:
		case T_RowIdentityVarInfo:
		case T_PlaceHolderInfo:
		case T_MinMaxAggInfo:
		case T_PlannerParamItem:
		case T_RollupData:
		case T_GroupingSetData:
		case T_StatisticExtInfo:
		case T_MergeAction:

		/* memory nodes (memnodes.h) */
		case T_AllocSetContext:
		case T_SlabContext:
		case T_GenerationContext:

		/* value nodes (value.h) */
		case T_Integer:
		case T_Float:
		case T_Boolean:
		case T_String:
		case T_BitString:

		/* list nodes (pg_list.h) */
		case T_List:
		case T_IntList:
		case T_OidList:

		/* extensible nodes (extensible.h) */
		case T_ExtensibleNode:

		/* statement nodes (parsenodes.h) */
		case T_RawStmt:
		case T_Query:
		case T_PlannedStmt:
		case T_InsertStmt:
		case T_DeleteStmt:
		case T_UpdateStmt:
		case T_MergeStmt:
		case T_SelectStmt:
		case T_ReturnStmt:
		case T_PLAssignStmt:
		case T_AlterTableStmt:
		case T_AlterTableCmd:
		case T_AlterDomainStmt:
		case T_SetOperationStmt:
		case T_GrantStmt:
		case T_GrantRoleStmt:
		case T_AlterDefaultPrivilegesStmt:
		case T_ClosePortalStmt:
		case T_ClusterStmt:
		case T_CopyStmt:
		case T_CreateStmt:
		case T_DefineStmt:
		case T_DropStmt:
		case T_TruncateStmt:
		case T_CommentStmt:
		case T_FetchStmt:
		case T_IndexStmt:
		case T_CreateFunctionStmt:
		case T_AlterFunctionStmt:
		case T_DoStmt:
		case T_RenameStmt:
		case T_RuleStmt:
		case T_NotifyStmt:
		case T_ListenStmt:
		case T_UnlistenStmt:
		case T_TransactionStmt:
		case T_ViewStmt:
		case T_LoadStmt:
		case T_CreateDomainStmt:
		case T_CreatedbStmt:
		case T_DropdbStmt:
		case T_VacuumStmt:
		case T_ExplainStmt:
		case T_CreateTableAsStmt:
		case T_CreateSeqStmt:
		case T_AlterSeqStmt:
		case T_VariableSetStmt:
		case T_VariableShowStmt:
		case T_DiscardStmt:
		case T_CreateTrigStmt:
		case T_CreatePLangStmt:
		case T_CreateRoleStmt:
		case T_AlterRoleStmt:
		case T_DropRoleStmt:
		case T_LockStmt:
		case T_ConstraintsSetStmt:
		case T_ReindexStmt:
		case T_YbBackfillIndexStmt:
		case T_CheckPointStmt:
		case T_CreateSchemaStmt:
		case T_AlterDatabaseStmt:
		case T_AlterDatabaseRefreshCollStmt:
		case T_AlterDatabaseSetStmt:
		case T_AlterRoleSetStmt:
		case T_CreateConversionStmt:
		case T_CreateCastStmt:
		case T_CreateOpClassStmt:
		case T_CreateOpFamilyStmt:
		case T_AlterOpFamilyStmt:
		case T_PrepareStmt:
		case T_ExecuteStmt:
		case T_DeallocateStmt:
		case T_DeclareCursorStmt:
		case T_YbCreateTableGroupStmt:
		case T_CreateTableSpaceStmt:
		case T_DropTableSpaceStmt:
		case T_AlterObjectDependsStmt:
		case T_AlterObjectSchemaStmt:
		case T_AlterOwnerStmt:
		case T_AlterOperatorStmt:
		case T_AlterTypeStmt:
		case T_DropOwnedStmt:
		case T_ReassignOwnedStmt:
		case T_CompositeTypeStmt:
		case T_CreateEnumStmt:
		case T_CreateRangeStmt:
		case T_AlterEnumStmt:
		case T_AlterTSDictionaryStmt:
		case T_AlterTSConfigurationStmt:
		case T_CreateFdwStmt:
		case T_AlterFdwStmt:
		case T_CreateForeignServerStmt:
		case T_AlterForeignServerStmt:
		case T_CreateUserMappingStmt:
		case T_AlterUserMappingStmt:
		case T_DropUserMappingStmt:
		case T_AlterTableSpaceOptionsStmt:
		case T_AlterTableMoveAllStmt:
		case T_SecLabelStmt:
		case T_CreateForeignTableStmt:
		case T_ImportForeignSchemaStmt:
		case T_CreateExtensionStmt:
		case T_AlterExtensionStmt:
		case T_AlterExtensionContentsStmt:
		case T_CreateEventTrigStmt:
		case T_AlterEventTrigStmt:
		case T_RefreshMatViewStmt:
		case T_ReplicaIdentityStmt:
		case T_AlterSystemStmt:
		case T_CreatePolicyStmt:
		case T_AlterPolicyStmt:
		case T_CreateTransformStmt:
		case T_CreateAmStmt:
		case T_CreatePublicationStmt:
		case T_AlterPublicationStmt:
		case T_CreateSubscriptionStmt:
		case T_AlterSubscriptionStmt:
		case T_DropSubscriptionStmt:
		case T_CreateStatsStmt:
		case T_AlterCollationStmt:
		case T_CallStmt:
		case T_AlterStatsStmt:

		/* parse tree nodes (parsenodes.h) */
		case T_A_Expr:
		case T_ColumnRef:
		case T_ParamRef:
		case T_A_Const:
		case T_FuncCall:
		case T_A_Star:
		case T_A_Indices:
		case T_A_Indirection:
		case T_A_ArrayExpr:
		case T_ResTarget:
		case T_MultiAssignRef:
		case T_TypeCast:
		case T_CollateClause:
		case T_SortBy:
		case T_WindowDef:
		case T_RangeSubselect:
		case T_RangeFunction:
		case T_RangeTableSample:
		case T_RangeTableFunc:
		case T_RangeTableFuncCol:
		case T_TypeName:
		case T_ColumnDef:
		case T_IndexElem:
		case T_StatsElem:
		case T_Constraint:
		case T_DefElem:
		case T_RangeTblEntry:
		case T_RangeTblFunction:
		case T_TableSampleClause:
		case T_WithCheckOption:
		case T_SortGroupClause:
		case T_GroupingSet:
		case T_WindowClause:
		case T_ObjectWithArgs:
		case T_AccessPriv:
		case T_CreateOpClassItem:
		case T_TableLikeClause:
		case T_FunctionParameter:
		case T_LockingClause:
		case T_RowMarkClause:
		case T_XmlSerialize:
		case T_WithClause:
		case T_InferClause:
		case T_OnConflictClause:
		case T_CTESearchClause:
		case T_CTECycleClause:
		case T_CommonTableExpr:
		case T_MergeWhenClause:
		case T_RoleSpec:
		case T_TriggerTransition:
		case T_PartitionElem:
		case T_PartitionSpec:
		case T_PartitionBoundSpec:
		case T_PartitionRangeDatum:
		case T_PartitionCmd:
		case T_VacuumRelation:
		case T_PublicationObjSpec:
		case T_PublicationTable:
		case T_YbOptSplit:
		case T_YbRowBounds:

		/* replication grammar parse nodes (replnodes.h) */
		case T_IdentifySystemCmd:
		case T_BaseBackupCmd:
		case T_CreateReplicationSlotCmd:
		case T_DropReplicationSlotCmd:
		case T_ReadReplicationSlotCmd:
		case T_StartReplicationCmd:
		case T_TimeLineHistoryCmd:

		/* random other stuff */
		case T_TriggerData:
		case T_EventTriggerData:
		case T_ReturnSetInfo:
		case T_WindowObjectData:
		case T_TIDBitmap:
		case T_InlineCodeBlock:
		case T_FdwRoutine:
		case T_IndexAmRoutine:
		case T_TableAmRoutine:
		case T_TsmRoutine:
		case T_ForeignKeyCacheInfo:
		case T_CallContext:
		case T_SupportRequestSimplify:
		case T_SupportRequestSelectivity:
		case T_SupportRequestCost:
		case T_SupportRequestRows:
		case T_SupportRequestIndexCondition:
		case T_SupportRequestWFuncMonotonic:

		/* yugabyte nodes */
		case T_YbPgExecOutParam:
		case T_YbBackfillInfo:
		case T_YbPartitionPruneStepFuncOp:
		case T_YbExprColrefDesc:
		case T_YbSeqScan:
		case T_YbBatchedNestLoop:
		case T_YbCreateProfileStmt:
		case T_YbDropProfileStmt:
		case T_YbTIDBitmap:
		case T_YbSkippableEntities:
		case T_YbUpdateAffectedEntities:
		case T_YbMergeScanInfo:
		case T_YbMergeScanSaopColInfo:
		case T_YbSortInfo:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("unrecognized node type: %d", (int) nodeTag(node))));
	}
	return NULL; /* keep compiler quiet */
}


/*
 * ExecProcNode wrapper that performs instrumentation calls.  By keeping
 * this a separate function, we avoid overhead in the normal case where
 * no instrumentation is wanted.
 */
static TupleTableSlot *
ExecProcNodeInstr(PlanState *node)
{
	TupleTableSlot *result;

	InstrStartNode(node->instrument);

	if (YBCIsDistTraceActive())
	{
		YB_DIST_TRACE_START_SPAN(YbGetExecNodeSpanName(node));
		result = node->ExecProcNodeReal(node);
		YB_DIST_TRACE_END_SPAN();
	}
	else
		result = node->ExecProcNodeReal(node);

	InstrStopNode(node->instrument, TupIsNull(result) ? 0.0 : 1.0);
	YbUpdateSessionStats(&node->instrument->yb_instr);

	return result;
}


/*
 * ExecProcNodeYbDistTrace
 *
 * ExecProcNode wrapper that starts/ends a distributed tracing span around
 * the real node execution.  Installed by ExecProcNodeFirst when distributed
 * tracing is enabled but no instrumentation is active.
 */
static TupleTableSlot *
ExecProcNodeYbDistTrace(PlanState *node)
{
	TupleTableSlot *result;

	YB_DIST_TRACE_START_SPAN(YbGetExecNodeSpanName(node));

	result = node->ExecProcNodeReal(node);

	YB_DIST_TRACE_END_SPAN();

	return result;
}


/* ----------------------------------------------------------------
 *		MultiExecProcNode
 *
 *		Execute a node that doesn't return individual tuples
 *		(it might return a hashtable, bitmap, etc).  Caller should
 *		check it got back the expected kind of Node.
 *
 * This has essentially the same responsibilities as ExecProcNode,
 * but it does not do InstrStartNode/InstrStopNode (mainly because
 * it can't tell how many returned tuples to count).  Each per-node
 * function must provide its own instrumentation support.
 * ----------------------------------------------------------------
 */
Node *
MultiExecProcNode(PlanState *node)
{
	Node	   *result;

	check_stack_depth();

	CHECK_FOR_INTERRUPTS();

	if (node->chgParam != NULL) /* something changed */
		ExecReScan(node);		/* let ReScan handle this */

	switch (nodeTag(node))
	{
			/*
			 * Only node types that actually support multiexec will be listed
			 */

		case T_HashState:
			result = MultiExecHash((HashState *) node);
			break;

		case T_BitmapIndexScanState:
			result = MultiExecBitmapIndexScan((BitmapIndexScanState *) node);
			break;

		case T_YbBitmapIndexScanState:
			result = MultiExecYbBitmapIndexScan((YbBitmapIndexScanState *) node);
			break;

		case T_BitmapAndState:
			result = MultiExecBitmapAnd((BitmapAndState *) node);
			break;

		case T_BitmapOrState:
			result = MultiExecBitmapOr((BitmapOrState *) node);
			break;

		default:
			elog(ERROR, "unrecognized node type: %d", (int) nodeTag(node));
			result = NULL;
			break;
	}

	/*
	 * YB: Specifically this is required after the MultiExecBitmapIndexScan,
	 * but it doesn't hurt to call it here after any of the above.
	 */
	if (IsYugaByteEnabled() && node->instrument)
		YbUpdateSessionStats(&node->instrument->yb_instr);

	return result;
}


/* ----------------------------------------------------------------
 *		ExecEndNode
 *
 *		Recursively cleans up all the nodes in the plan rooted
 *		at 'node'.
 *
 *		After this operation, the query plan will not be able to be
 *		processed any further.  This should be called only after
 *		the query plan has been fully executed.
 * ----------------------------------------------------------------
 */
void
ExecEndNode(PlanState *node)
{
	/*
	 * do nothing when we get to the end of a leaf on tree.
	 */
	if (node == NULL)
		return;

	/*
	 * Make sure there's enough stack available. Need to check here, in
	 * addition to ExecProcNode() (via ExecProcNodeFirst()), because it's not
	 * guaranteed that ExecProcNode() is reached for all nodes.
	 */
	check_stack_depth();

	if (node->chgParam != NULL)
	{
		bms_free(node->chgParam);
		node->chgParam = NULL;
	}

	switch (nodeTag(node))
	{
			/*
			 * control nodes
			 */
		case T_ResultState:
			ExecEndResult((ResultState *) node);
			break;

		case T_ProjectSetState:
			ExecEndProjectSet((ProjectSetState *) node);
			break;

		case T_ModifyTableState:
			ExecEndModifyTable((ModifyTableState *) node);
			break;

		case T_AppendState:
			ExecEndAppend((AppendState *) node);
			break;

		case T_MergeAppendState:
			ExecEndMergeAppend((MergeAppendState *) node);
			break;

		case T_RecursiveUnionState:
			ExecEndRecursiveUnion((RecursiveUnionState *) node);
			break;

		case T_BitmapAndState:
			ExecEndBitmapAnd((BitmapAndState *) node);
			break;

		case T_BitmapOrState:
			ExecEndBitmapOr((BitmapOrState *) node);
			break;

			/*
			 * scan nodes
			 */
		case T_SeqScanState:
			ExecEndSeqScan((SeqScanState *) node);
			break;

		case T_YbSeqScanState:
			ExecEndYbSeqScan((YbSeqScanState *) node);
			break;

		case T_SampleScanState:
			ExecEndSampleScan((SampleScanState *) node);
			break;

		case T_GatherState:
			ExecEndGather((GatherState *) node);
			break;

		case T_GatherMergeState:
			ExecEndGatherMerge((GatherMergeState *) node);
			break;

		case T_IndexScanState:
			ExecEndIndexScan((IndexScanState *) node);
			break;

		case T_IndexOnlyScanState:
			ExecEndIndexOnlyScan((IndexOnlyScanState *) node);
			break;

		case T_BitmapIndexScanState:
			ExecEndBitmapIndexScan((BitmapIndexScanState *) node);
			break;

		case T_YbBitmapIndexScanState:
			ExecEndYbBitmapIndexScan((YbBitmapIndexScanState *) node);
			break;

		case T_BitmapHeapScanState:
			ExecEndBitmapHeapScan((BitmapHeapScanState *) node);
			break;

		case T_YbBitmapTableScanState:
			ExecEndYbBitmapTableScan((YbBitmapTableScanState *) node);
			break;

		case T_TidScanState:
			ExecEndTidScan((TidScanState *) node);
			break;

		case T_TidRangeScanState:
			ExecEndTidRangeScan((TidRangeScanState *) node);
			break;

		case T_SubqueryScanState:
			ExecEndSubqueryScan((SubqueryScanState *) node);
			break;

		case T_FunctionScanState:
			ExecEndFunctionScan((FunctionScanState *) node);
			break;

		case T_TableFuncScanState:
			ExecEndTableFuncScan((TableFuncScanState *) node);
			break;

		case T_ValuesScanState:
			ExecEndValuesScan((ValuesScanState *) node);
			break;

		case T_CteScanState:
			ExecEndCteScan((CteScanState *) node);
			break;

		case T_NamedTuplestoreScanState:
			ExecEndNamedTuplestoreScan((NamedTuplestoreScanState *) node);
			break;

		case T_WorkTableScanState:
			ExecEndWorkTableScan((WorkTableScanState *) node);
			break;

		case T_ForeignScanState:
			ExecEndForeignScan((ForeignScanState *) node);
			break;

		case T_CustomScanState:
			ExecEndCustomScan((CustomScanState *) node);
			break;

			/*
			 * join nodes
			 */
		case T_NestLoopState:
			ExecEndNestLoop((NestLoopState *) node);
			break;

		case T_YbBatchedNestLoopState:
			ExecEndYbBatchedNestLoop((YbBatchedNestLoopState *) node);
			break;

		case T_MergeJoinState:
			ExecEndMergeJoin((MergeJoinState *) node);
			break;

		case T_HashJoinState:
			ExecEndHashJoin((HashJoinState *) node);
			break;

			/*
			 * materialization nodes
			 */
		case T_MaterialState:
			ExecEndMaterial((MaterialState *) node);
			break;

		case T_SortState:
			ExecEndSort((SortState *) node);
			break;

		case T_IncrementalSortState:
			ExecEndIncrementalSort((IncrementalSortState *) node);
			break;

		case T_MemoizeState:
			ExecEndMemoize((MemoizeState *) node);
			break;

		case T_GroupState:
			ExecEndGroup((GroupState *) node);
			break;

		case T_AggState:
			ExecEndAgg((AggState *) node);
			break;

		case T_WindowAggState:
			ExecEndWindowAgg((WindowAggState *) node);
			break;

		case T_UniqueState:
			ExecEndUnique((UniqueState *) node);
			break;

		case T_HashState:
			ExecEndHash((HashState *) node);
			break;

		case T_SetOpState:
			ExecEndSetOp((SetOpState *) node);
			break;

		case T_LockRowsState:
			ExecEndLockRows((LockRowsState *) node);
			break;

		case T_LimitState:
			ExecEndLimit((LimitState *) node);
			break;

		default:
			elog(ERROR, "unrecognized node type: %d", (int) nodeTag(node));
			break;
	}
}

/*
 * ExecShutdownNode
 *
 * Give execution nodes a chance to stop asynchronous resource consumption
 * and release any resources still held.
 */
bool
ExecShutdownNode(PlanState *node)
{
	return ExecShutdownNode_walker(node, NULL);
}

static bool
ExecShutdownNode_walker(PlanState *node, void *context)
{
	if (node == NULL)
		return false;

	check_stack_depth();

	/*
	 * Treat the node as running while we shut it down, but only if it's run
	 * at least once already.  We don't expect much CPU consumption during
	 * node shutdown, but in the case of Gather or Gather Merge, we may shut
	 * down workers at this stage.  If so, their buffer usage will get
	 * propagated into pgBufferUsage at this point, and we want to make sure
	 * that it gets associated with the Gather node.  We skip this if the node
	 * has never been executed, so as to avoid incorrectly making it appear
	 * that it has.
	 */
	if (node->instrument && node->instrument->running)
		InstrStartNode(node->instrument);

	planstate_tree_walker(node, ExecShutdownNode_walker, context);

	switch (nodeTag(node))
	{
		case T_GatherState:
			ExecShutdownGather((GatherState *) node);
			break;
		case T_ForeignScanState:
			ExecShutdownForeignScan((ForeignScanState *) node);
			break;
		case T_CustomScanState:
			ExecShutdownCustomScan((CustomScanState *) node);
			break;
		case T_GatherMergeState:
			ExecShutdownGatherMerge((GatherMergeState *) node);
			break;
		case T_HashState:
			ExecShutdownHash((HashState *) node);
			break;
		case T_HashJoinState:
			ExecShutdownHashJoin((HashJoinState *) node);
			break;
		case T_LockRowsState:
			ExecShutdownLockRows((LockRowsState *) node);
			break;
		default:
			break;
	}

	/* Stop the node if we started it above, reporting 0 tuples. */
	if (node->instrument && node->instrument->running)
		InstrStopNode(node->instrument, 0);

	return false;
}

/*
 * ExecSetTupleBound
 *
 * Set a tuple bound for a planstate node.  This lets child plan nodes
 * optimize based on the knowledge that the maximum number of tuples that
 * their parent will demand is limited.  The tuple bound for a node may
 * only be changed between scans (i.e., after node initialization or just
 * before an ExecReScan call).
 *
 * Any negative tuples_needed value means "no limit", which should be the
 * default assumption when this is not called at all for a particular node.
 *
 * Note: if this is called repeatedly on a plan tree, the exact same set
 * of nodes must be updated with the new limit each time; be careful that
 * only unchanging conditions are tested here.
 */
void
ExecSetTupleBound(int64 tuples_needed, PlanState *child_node)
{
	/*
	 * Since this function recurses, in principle we should check stack depth
	 * here.  In practice, it's probably pointless since the earlier node
	 * initialization tree traversal would surely have consumed more stack.
	 */

	if (IsA(child_node, SortState))
	{
		/*
		 * If it is a Sort node, notify it that it can use bounded sort.
		 *
		 * Note: it is the responsibility of nodeSort.c to react properly to
		 * changes of these parameters.  If we ever redesign this, it'd be a
		 * good idea to integrate this signaling with the parameter-change
		 * mechanism.
		 */
		SortState  *sortState = (SortState *) child_node;

		if (tuples_needed < 0)
		{
			/* make sure flag gets reset if needed upon rescan */
			sortState->bounded = false;
		}
		else
		{
			sortState->bounded = true;
			sortState->bound = tuples_needed;
		}
	}
	else if (IsA(child_node, IncrementalSortState))
	{
		/*
		 * If it is an IncrementalSort node, notify it that it can use bounded
		 * sort.
		 *
		 * Note: it is the responsibility of nodeIncrementalSort.c to react
		 * properly to changes of these parameters.  If we ever redesign this,
		 * it'd be a good idea to integrate this signaling with the
		 * parameter-change mechanism.
		 */
		IncrementalSortState *sortState = (IncrementalSortState *) child_node;

		if (tuples_needed < 0)
		{
			/* make sure flag gets reset if needed upon rescan */
			sortState->bounded = false;
		}
		else
		{
			sortState->bounded = true;
			sortState->bound = tuples_needed;
		}
	}
	else if (IsA(child_node, AppendState))
	{
		/*
		 * If it is an Append, we can apply the bound to any nodes that are
		 * children of the Append, since the Append surely need read no more
		 * than that many tuples from any one input.
		 */
		AppendState *aState = (AppendState *) child_node;
		int			i;

		for (i = 0; i < aState->as_nplans; i++)
			ExecSetTupleBound(tuples_needed, aState->appendplans[i]);
	}
	else if (IsA(child_node, MergeAppendState))
	{
		/*
		 * If it is a MergeAppend, we can apply the bound to any nodes that
		 * are children of the MergeAppend, since the MergeAppend surely need
		 * read no more than that many tuples from any one input.
		 */
		MergeAppendState *maState = (MergeAppendState *) child_node;
		int			i;

		for (i = 0; i < maState->ms_nplans; i++)
			ExecSetTupleBound(tuples_needed, maState->mergeplans[i]);
	}
	else if (IsA(child_node, ResultState))
	{
		/*
		 * Similarly, for a projecting Result, we can apply the bound to its
		 * child node.
		 *
		 * If Result supported qual checking, we'd have to punt on seeing a
		 * qual.  Note that having a resconstantqual is not a showstopper: if
		 * that condition succeeds it affects nothing, while if it fails, no
		 * rows will be demanded from the Result child anyway.
		 */
		if (outerPlanState(child_node))
			ExecSetTupleBound(tuples_needed, outerPlanState(child_node));
	}
	else if (IsA(child_node, SubqueryScanState))
	{
		/*
		 * We can also descend through SubqueryScan, but only if it has no
		 * qual (otherwise it might discard rows).
		 */
		SubqueryScanState *subqueryState = (SubqueryScanState *) child_node;

		if (subqueryState->ss.ps.qual == NULL)
			ExecSetTupleBound(tuples_needed, subqueryState->subplan);
	}
	else if (IsA(child_node, GatherState))
	{
		/*
		 * A Gather node can propagate the bound to its workers.  As with
		 * MergeAppend, no one worker could possibly need to return more
		 * tuples than the Gather itself needs to.
		 *
		 * Note: As with Sort, the Gather node is responsible for reacting
		 * properly to changes to this parameter.
		 */
		GatherState *gstate = (GatherState *) child_node;

		gstate->tuples_needed = tuples_needed;

		/* Also pass down the bound to our own copy of the child plan */
		ExecSetTupleBound(tuples_needed, outerPlanState(child_node));
	}
	else if (IsA(child_node, GatherMergeState))
	{
		/* Same comments as for Gather */
		GatherMergeState *gstate = (GatherMergeState *) child_node;

		gstate->tuples_needed = tuples_needed;

		ExecSetTupleBound(tuples_needed, outerPlanState(child_node));
	}
	else if (IsA(child_node, YbBatchedNestLoopState))
	{
		YbBatchedNestLoopState *bnl_state = (YbBatchedNestLoopState *) child_node;

		if (bnl_state->bnl_is_sorted)
		{
			if (tuples_needed < 0)
				bnl_state->bound = 0;
			else
				bnl_state->bound = tuples_needed;
		}
	}

	/*
	 * In principle we could descend through any plan node type that is
	 * certain not to discard or combine input rows; but on seeing a node that
	 * can do that, we can't propagate the bound any further.  For the moment
	 * it's unclear that any other cases are worth checking here.
	 */
}
