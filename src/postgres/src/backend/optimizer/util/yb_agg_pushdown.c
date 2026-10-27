/*-------------------------------------------------------------------------
 *
 * yb_agg_pushdown.c
 *	  Planner-side eligibility of aggregate pushdown to DocDB
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
 *	  src/backend/optimizer/util/yb_agg_pushdown.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/table.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_type_d.h"
#include "catalog/yb_type.h"
#include "executor/nodeIndexscan.h"
#include "executor/ybExpr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/appendinfo.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/optimizer.h"
#include "optimizer/paths.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/yb_agg_pushdown.h"
#include "optimizer/ybplan.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "rewrite/rewriteManip.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

static bool yb_scan_path_has_local_quals(PlannerInfo *root, Path *scan,
										 Bitmapset *decoded_pk_attnums);
static bool yb_seqscan_path_has_local_quals(Path *scan);
static bool yb_index_path_has_local_quals(PlannerInfo *root, IndexPath *path,
										  Bitmapset *decoded_pk_attnums);
static bool yb_bitmap_path_has_local_quals(PlannerInfo *root,
										   YbBitmapTablePath *path);
static bool yb_bitmap_path_has_local_recheck_quals(PlannerInfo *root,
												   YbBitmapTablePath *path);
static bool yb_tid_path_has_local_quals(PlannerInfo *root, TidPath *path);
static bool yb_aggrefs_pushable(PlannerInfo *root, RelOptInfo *rel,
								List *aggrefs, Bitmapset *decoded_pk_attnums,
								PathTarget *input_target);
static bool yb_aggref_pushable(Aggref *aggref, Index relid,
							   Bitmapset *decoded_pk_attnums,
							   bool *has_var_arg);
static bool yb_scan_path_might_recheck(PlannerInfo *root, Path *scan);
static bool yb_index_path_might_recheck(PlannerInfo *root, IndexPath *path,
										bool index_only);
static bool yb_bitmap_path_might_recheck(PlannerInfo *root, Path *bitmapqual);
static bool yb_predetermine_index_path_recheck(PlannerInfo *root,
											   IndexPath *path,
											   NodeTag scan_tag,
											   bool xs_want_itup,
											   int *num_runtime_keys_p);

/*
 * yb_agg_pushdown_eligible
 *	  Decide whether a plain Agg over 'scan' can have its aggregates
 *	  evaluated by the scan's DocDB requests.
 *
 * 'scan' is the bare scan path under the Agg (the caller has looked through
 * the path layers that leave no plan node between the two: a dummy
 * ProjectionPath, a single-child AppendPath and a trivial SubqueryScanPath),
 * 'root' is the scan's own PlannerInfo and 'input_target' is the target list
 * the scan node ends up with.  'aggrefs' are the Agg's aggregates, one per
 * distinct Aggref (yb_agg_pushdown_aggrefs), with their arguments over the
 * scan's relation (yb_aggrefs_below_appendrel, yb_aggrefs_below_subqueryscan).
 *
 * A true answer is a commitment: create_agg_path marks the AggPath, the Agg
 * plan node carries the mark, and the executor hands the Aggrefs to the scan
 * without re-checking anything.  So every rule here must hold for the plan
 * that createplan.c builds from this path.  The scan must end up with no
 * qual (nothing can be evaluated on a pushed-down aggregate row, and a
 * pseudoconstant clause would put a gating Result between the Agg and the
 * scan), and an index scan must never recheck its conditions (the recheck
 * would run against that row).  Each local-qual derivation below mirrors
 * the plan builder that fills the scan node's qual; keep them in step.
 *
 * The aggregate rules are a walk over 'aggrefs' and run first; the
 * local-qual derivation of an index path copies its index quals, and the
 * recheck predeterminations open relations and build scan keys, so those
 * run last.  Everything the decision allocates is thrown away with a
 * temporary context.
 */
bool
yb_agg_pushdown_eligible(PlannerInfo *root, List *aggrefs, Path *scan,
						 PathTarget *input_target)
{
	RelOptInfo *rel = scan->parent;
	IndexPath  *ipath = NULL;
	Bitmapset  *decoded_pk_attnums = NULL;
	MemoryContext tmpcxt;
	MemoryContext oldcxt;
	bool		result;

	if (aggrefs == NIL)
		return false;

	/* A plain Agg sits above every join, so its scan is unparameterized. */
	if (scan->param_info != NULL || !rel->is_yb_relation)
		return false;

	switch (nodeTag(scan))
	{
		case T_Path:
			if (scan->pathtype != T_SeqScan)
				return false;
			break;

		case T_IndexPath:
			ipath = (IndexPath *) scan;
			if (scan->pathtype == T_IndexScan)
			{
				/* The GUC governs Index Scan only, not Index Only Scan. */
				if (!yb_enable_index_aggregate_pushdown)
					return false;
			}
			else if (scan->pathtype != T_IndexOnlyScan)
				return false;
			break;

		case T_YbBitmapTablePath:
		case T_TidPath:
			break;

		default:
			return false;
	}

	tmpcxt = AllocSetContextCreate(CurrentMemoryContext,
								   "yb agg pushdown eligibility",
								   ALLOCSET_SMALL_SIZES);
	oldcxt = MemoryContextSwitchTo(tmpcxt);

	/*
	 * An Index Only Scan decodes primary-key columns missing from the index
	 * out of ybidxbasectid after the fetch, so DocDB can neither filter nor
	 * aggregate over them.
	 */
	if (ipath != NULL && scan->pathtype == T_IndexOnlyScan &&
		ipath->indexinfo->yb_num_decoded_pk_cols > 0)
		decoded_pk_attnums =
			yb_get_non_pushable_attnums(ipath->indexinfo);

	result = yb_aggrefs_pushable(root, rel, aggrefs, decoded_pk_attnums,
								 input_target) &&
		!yb_scan_path_has_local_quals(root, scan, decoded_pk_attnums) &&
		!yb_scan_path_might_recheck(root, scan);

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(tmpcxt);
	return result;
}

/*
 * yb_agg_pushdown_aggrefs
 *	  The aggregates of the Agg planned from 'root', one per distinct Aggref,
 *	  as root->agginfos lists them.
 *
 * Those keep AGGSPLIT_SIMPLE even under a partial Agg, so the Agg's split is
 * never read from them.
 */
List *
yb_agg_pushdown_aggrefs(PlannerInfo *root)
{
	List	   *aggrefs = NIL;
	ListCell   *lc;

	foreach(lc, root->agginfos)
		aggrefs = lappend(aggrefs,
						  ((AggInfo *) lfirst(lc))->representative_aggref);
	return aggrefs;
}

/*
 * yb_aggrefs_below_subqueryscan
 *	  Re-express 'aggrefs', whose arguments read the output columns of the
 *	  subquery relation 'rel' of 'root', over the subquery's own target list,
 *	  so that they hold for the scan inside the subquery.  NIL when an
 *	  argument reads a column the subquery does not pass through unchanged.
 *
 * setrefs.c turns the Agg's reference to a subquery output column into a
 * reference to the scan's target entry whatever that entry computes, and the
 * scan resolves an aggregate's Var argument through its target list as a
 * column of its relation.  So only an output column that is a plain Var of
 * the subquery's own relation is a column to DocDB; a computed one, a
 * constant folded from one (NULL = random(), say) or a whole-row reference
 * cannot be pushed, although the translated argument might pass
 * yb_aggref_pushable on its own.
 */
List *
yb_aggrefs_below_subqueryscan(PlannerInfo *root, RelOptInfo *rel,
							  List *aggrefs)
{
	List	   *tlist = rel->subroot->processed_tlist;
	List	   *vars;
	ListCell   *lc;
	bool		has_sublinks = false;

	Assert(rel->rtekind == RTE_SUBQUERY);

	vars = pull_var_clause((Node *) aggrefs,
						   PVC_RECURSE_AGGREGATES | PVC_RECURSE_PLACEHOLDERS);
	foreach(lc, vars)
	{
		Var		   *var = lfirst_node(Var, lc);
		TargetEntry *tle;

		if (var->varno != rel->relid || var->varlevelsup != 0)
			continue;
		if (var->varattno <= 0)
			return NIL;
		tle = get_tle_by_resno(tlist, var->varattno);
		if (tle == NULL || !IsA(tle->expr, Var))
			return NIL;
	}

	return (List *)
		ReplaceVarsFromTargetList((Node *) aggrefs, rel->relid, 0,
								  planner_rt_fetch(rel->relid, root),
								  tlist, REPLACEVARS_REPORT_ERROR, 0,
								  &has_sublinks);
}

/*
 * Whether the scan node built from 'scan' ends up with a qual, or with a
 * gating Result above it.  A pushed-down aggregate row satisfies no qual,
 * and a Result between the Agg and the scan leaves the Agg no scan to push
 * into.
 */
static bool
yb_scan_path_has_local_quals(PlannerInfo *root, Path *scan,
							 Bitmapset *decoded_pk_attnums)
{
	ListCell   *lc;

	/*
	 * A pseudoconstant clause never reaches the scan node: create_scan_plan
	 * puts a gating Result between the Agg and the scan, and the Agg's
	 * input is then not the scan.
	 */
	foreach(lc, scan->parent->baserestrictinfo)
	{
		if (lfirst_node(RestrictInfo, lc)->pseudoconstant)
			return true;
	}

	switch (scan->pathtype)
	{
		case T_SeqScan:
			return yb_seqscan_path_has_local_quals(scan);
		case T_IndexScan:
		case T_IndexOnlyScan:
			return yb_index_path_has_local_quals(root, (IndexPath *) scan,
												 decoded_pk_attnums);
		case T_YbBitmapTableScan:
			return yb_bitmap_path_has_local_quals(root,
												  (YbBitmapTablePath *) scan);
		case T_TidScan:
			return yb_tid_path_has_local_quals(root, (TidPath *) scan);
		default:
			elog(ERROR, "unrecognized node type: %d", (int) scan->pathtype);
			return true;			/* keep compiler quiet */
	}
}

/*
 * Mirrors create_seqscan_plan: a YbSeqScan's qual is what
 * yb_extract_pushdown_clauses leaves local.  RestrictInfo.yb_pushable is
 * never set on a clause that function rejects (it asserts so), so testing
 * it finds every local clause; yb_cost_seqscan partitions the clauses the
 * same way.
 */
static bool
yb_seqscan_path_has_local_quals(Path *scan)
{
	ListCell   *lc;

	foreach(lc, scan->parent->baserestrictinfo)
	{
		if (!lfirst_node(RestrictInfo, lc)->yb_pushable)
			return true;
	}
	return false;
}

/*
 * Mirrors create_indexscan_plan: the qpqual is indrestrictinfo minus the
 * clauses the index conditions already enforce, and
 * yb_extract_pushdown_clauses leaves local what DocDB cannot evaluate,
 * including anything over a decoded primary-key column.  Whether the index
 * or the base table receives a pushable clause (need_idx_remote there) does
 * not change the local set, so no index is passed.
 */
static bool
yb_index_path_has_local_quals(PlannerInfo *root, IndexPath *path,
							  Bitmapset *decoded_pk_attnums)
{
	Oid			relid = planner_rt_fetch(path->path.parent->relid,
										 root)->relid;
	List	   *stripped_indexquals;
	List	   *fixed_indexquals;
	List	   *qpqual = NIL;
	List	   *local_quals = NIL;
	List	   *remote_quals = NIL;
	List	   *colrefs = NIL;
	ListCell   *lc;

	/* Only stripped_indexquals is needed here. */
	yb_fix_indexqual_references(root, path, &stripped_indexquals,
								&fixed_indexquals);

	foreach(lc, path->indexinfo->indrestrictinfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);

		if (rinfo->pseudoconstant)
			continue;
		if (list_member_ptr(stripped_indexquals, rinfo->clause))
			continue;
		if (is_redundant_with_indexclauses(rinfo, path->indexclauses))
			continue;
		if (!contain_mutable_functions((Node *) rinfo->clause) &&
			predicate_implied_by(list_make1(rinfo->clause),
								 stripped_indexquals, false))
			continue;
		qpqual = lappend(qpqual, rinfo);
	}

	yb_extract_pushdown_clauses(qpqual, NULL /* indexinfo */ ,
								false /* bitmapindex */ ,
								&local_quals, &remote_quals, &colrefs,
								NULL /* idx_remote_quals */ ,
								NULL /* idx_colrefs */ ,
								relid, decoded_pk_attnums);

	return local_quals != NIL;
}

/*
 * Mirrors create_yb_bitmap_scan_plan's fallback_local_quals: the clauses
 * the scan evaluates itself when it exceeds work_mem and falls back to a
 * sequential scan, which is every non-pushable restriction clause.  The
 * scan node's own qual is the same split over a subset of these clauses,
 * so it is empty whenever this list is.
 */
static bool
yb_bitmap_path_has_local_quals(PlannerInfo *root, YbBitmapTablePath *path)
{
	RelOptInfo *rel = path->path.parent;
	List	   *local_quals = NIL;
	List	   *remote_quals = NIL;
	List	   *colrefs = NIL;

	yb_extract_pushdown_clauses(rel->baserestrictinfo, NULL /* indexinfo */ ,
								false /* bitmapindex */ ,
								&local_quals, &remote_quals, &colrefs,
								NULL /* idx_remote_quals */ ,
								NULL /* idx_colrefs */ ,
								planner_rt_fetch(rel->relid, root)->relid,
								NULL /* non_pushable_attnums */ );

	return local_quals != NIL;
}

/*
 * Mirrors create_yb_bitmap_scan_plan's recheck_local_quals: the index
 * conditions, and the filters pushed into the bitmap index scans, that
 * DocDB cannot re-evaluate when the bitmap turns lossy.  The plan builder
 * first removes the scan's local quals from the list; the caller has
 * already found those empty.
 */
static bool
yb_bitmap_path_has_local_recheck_quals(PlannerInfo *root,
									   YbBitmapTablePath *path)
{
	RelOptInfo *rel = path->path.parent;
	Oid			relid = planner_rt_fetch(rel->relid, root)->relid;
	List	   *indexquals;
	ListCell   *lc;

	indexquals = yb_get_bitmap_index_quals(root, path->bitmapqual,
										   rel->baserestrictinfo);
	foreach(lc, indexquals)
	{
		List	   *colrefs = NIL;

		if (!YbCanPushdownExpr((Expr *) lfirst(lc), &colrefs, relid))
			return true;
	}
	return false;
}

/*
 * Mirrors create_tidscan_plan: the restriction clauses other than the TID
 * conditions, split by yb_extract_pushdown_clauses.
 */
static bool
yb_tid_path_has_local_quals(PlannerInfo *root, TidPath *path)
{
	RelOptInfo *rel = path->path.parent;
	List	   *tidquals = path->tidquals;
	List	   *scan_clauses = rel->baserestrictinfo;
	List	   *local_quals = NIL;
	List	   *remote_quals = NIL;
	List	   *colrefs = NIL;

	if (list_length(tidquals) == 1)
	{
		List	   *qpqual = NIL;
		ListCell   *lc;

		foreach(lc, scan_clauses)
		{
			RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);

			if (rinfo->pseudoconstant)
				continue;
			if (list_member_ptr(tidquals, rinfo))
				continue;
			if (is_redundant_derived_clause(rinfo, tidquals))
				continue;
			qpqual = lappend(qpqual, rinfo);
		}
		scan_clauses = qpqual;
	}

	yb_extract_pushdown_clauses(scan_clauses, NULL /* indexinfo */ ,
								false /* bitmapindex */ ,
								&local_quals, &remote_quals, &colrefs,
								NULL /* idx_remote_quals */ ,
								NULL /* idx_colrefs */ ,
								planner_rt_fetch(rel->relid, root)->relid,
								NULL /* non_pushable_attnums */ );

	/* Several TID conditions are one OR clause among the restrictions. */
	if (list_length(tidquals) > 1)
	{
		List	   *tidclauses = extract_actual_clauses(tidquals, false);

		local_quals = list_difference(local_quals,
									  list_make1(make_orclause(tidclauses)));
	}

	return local_quals != NIL;
}

/*
 * yb_aggrefs_below_appendrel
 *	  Re-express 'aggrefs', whose arguments read columns of the appendrel
 *	  parent of 'rel', over 'rel' itself when 'rel' is an appendrel member
 *	  (a partition, an inheritance child or a UNION ALL member), so that the
 *	  argument rules see the member's own column numbers and row type.  NIL
 *	  when an argument reads a parent column that the member does not supply
 *	  as a plain column of its own.
 *
 * The member's translation of a parent column is whatever the member
 * outputs there: a UNION ALL member can output a constant or an
 * expression, which the Agg still reads as a column of the scan's output
 * row and which DocDB has no counterpart for, although the translated
 * argument might pass yb_aggref_pushable on its own.  'aggrefs' already
 * expressed over 'rel' come back unchanged.
 */
List *
yb_aggrefs_below_appendrel(PlannerInfo *root, RelOptInfo *rel,
						   List *aggrefs)
{
	List	   *translated;
	ListCell   *lc;
	ListCell   *lc2;

	if (!IS_OTHER_REL(rel))
		return aggrefs;

	translated = (List *)
		adjust_appendrel_attrs_multilevel(root, (Node *) aggrefs, rel->relids,
										  rel->top_parent_relids);
	forboth(lc, aggrefs, lc2, translated)
	{
		Aggref	   *aggref = lfirst_node(Aggref, lc);
		Aggref	   *member_aggref = lfirst_node(Aggref, lc2);
		ListCell   *la;
		ListCell   *la2;

		forboth(la, aggref->args, la2, member_aggref->args)
		{
			Expr	   *arg = lfirst_node(TargetEntry, la)->expr;
			Expr	   *member_arg = lfirst_node(TargetEntry, la2)->expr;

			if (IsA(arg, Var) && !IsA(member_arg, Var))
				return NIL;
		}
	}
	return translated;
}

/*
 * Every aggregate of the Agg must be pushable; 'aggrefs' lists each distinct
 * one, with its arguments over the scanned relation.  A Var argument is read
 * from the scan's output row, which works only when the scan projects plain
 * columns: a computed target entry has no DocDB counterpart.
 */
static bool
yb_aggrefs_pushable(PlannerInfo *root, RelOptInfo *rel, List *aggrefs,
					Bitmapset *decoded_pk_attnums, PathTarget *input_target)
{
	bool		has_var_arg = false;
	ListCell   *lc;

	foreach(lc, aggrefs)
	{
		Aggref	   *aggref = lfirst_node(Aggref, lc);

		if (!yb_aggref_pushable(aggref, rel->relid, decoded_pk_attnums,
								&has_var_arg))
			return false;
	}

	if (has_var_arg)
	{
		foreach(lc, input_target->exprs)
		{
			if (!IsA(lfirst(lc), Var))
				return false;
		}
	}

	return true;
}

/*
 * The per-Aggref rules at path time: the function
 * (yb_aggref_function_pushable) and its arguments
 * (yb_aggref_arg_type_pushable).  A Var argument must be a column of the
 * scanned relation that an Index Only Scan does not decode from the base
 * table's ybctid, and sets *has_var_arg.
 */
static bool
yb_aggref_pushable(Aggref *aggref, Index relid, Bitmapset *decoded_pk_attnums,
				   bool *has_var_arg)
{
	ListCell   *lc;

	if (!yb_aggref_function_pushable(aggref))
		return false;

	foreach(lc, aggref->args)
	{
		TargetEntry *tle = lfirst_node(TargetEntry, lc);
		Oid			type = InvalidOid;

		/* DocDB evaluates no expression: a column or a constant only. */
		if (IsA(tle->expr, Var))
		{
			Var		   *var = castNode(Var, tle->expr);

			if (var->varno != relid || var->varlevelsup != 0)
				return false;
			if (var->varattno > 0 &&
				bms_is_member(var->varattno, decoded_pk_attnums))
				return false;
			*has_var_arg = true;
			type = var->vartype;
		}
		else if (IsA(tle->expr, Const))
		{
			Const	   *const_node = castNode(Const, tle->expr);

			if (const_node->constisnull)
				/* A NULL constant carries no useful type. */
				type = aggref->aggtranstype;
			else if (!const_node->constbyval)
				/* DocDB receives by-value constants only. */
				return false;
			else
				type = const_node->consttype;
		}
		else
			return false;

		if (!yb_aggref_arg_type_pushable(aggref, type))
			return false;
	}

	return true;
}

/*
 * yb_aggref_function_pushable
 *	  Whether DocDB evaluates the aggregate function 'aggref' calls, its
 *	  arguments aside: count, min, max, sum or avg, plain (no ORDER BY,
 *	  DISTINCT, FILTER or variadic arguments, of the normal kind, at this
 *	  query level), with a transition type DocDB can hold as a key, and min
 *	  and max under a C collation only, since DocDB compares bytes.
 *
 * The arguments are checked by the caller, which knows what a Var may
 * reference, with yb_aggref_arg_type_pushable.  yb_agg_pushdown_supported
 * (nodeAgg.c) applies both to an Agg planned without the YB cost model, so a
 * rule added here reaches both decisions.
 */
bool
yb_aggref_function_pushable(Aggref *aggref)
{
	char	   *func_name = get_func_name(aggref->aggfnoid);

	/* Only count, min, max, sum and avg have a DocDB evaluation. */
	if (strcmp(func_name, "count") != 0 &&
		strcmp(func_name, "min") != 0 &&
		strcmp(func_name, "max") != 0 &&
		strcmp(func_name, "sum") != 0 &&
		strcmp(func_name, "avg") != 0)
		return false;

	/* No ORDER BY. */
	if (list_length(aggref->aggorder) != 0)
		return false;

	/* No DISTINCT. */
	if (list_length(aggref->aggdistinct) != 0)
		return false;

	/* No FILTER. */
	if (aggref->aggfilter)
		return false;

	/* No array arguments. */
	if (aggref->aggvariadic)
		return false;

	/* Normal aggregate kind. */
	if (aggref->aggkind != AGGKIND_NORMAL)
		return false;

	/* Belongs to this query level. */
	if (aggref->agglevelsup != 0)
		return false;

	/*
	 * The transition type must be a type DocDB can hold as a key, and not
	 * INTERNAL or NUMERIC, except that avg with an int8 array state is
	 * pushed as its sum and count.
	 */
	if (!YbDataTypeIsValidForKey(aggref->aggtranstype) ||
		aggref->aggtranstype == INTERNALOID ||
		aggref->aggtranstype == NUMERICOID)
	{
		if (!(strcmp(func_name, "avg") == 0 &&
			  aggref->aggtranstype == INT8ARRAYOID))
			return false;
	}

	/*
	 * min and max compare their inputs.  DocDB compares bytes and has no
	 * collation information, which is wrong under any non-C collation.
	 */
	if ((strcmp(func_name, "min") == 0 || strcmp(func_name, "max") == 0) &&
		(YBIsCollationValidNonC(aggref->aggcollid) ||
		 YBIsCollationValidNonC(aggref->inputcollid)))
		return false;

	return true;
}

/*
 * yb_aggref_arg_type_pushable
 *	  Whether an argument of 'aggref' of type 'argtype', a column or a
 *	  by-value constant, has a DocDB evaluation with PostgreSQL's semantics:
 *	  a type DocDB can hold as a key, and money only under min, max and
 *	  count, whose transition state cannot overflow.
 *
 * sum(money) and any user-defined money aggregate stay in PostgreSQL, where
 * the transition function checks overflow: cash_pl raises "money out of
 * range" where DocDB's EvalSumInt wraps around in 64-bit addition.  The rule
 * keys on the argument type rather than the transition type so that an
 * aggregate accumulating money in another form (an int8 array) is caught
 * too.
 */
bool
yb_aggref_arg_type_pushable(Aggref *aggref, Oid argtype)
{
	if (!YbDataTypeIsValidForKey(argtype))
		return false;

	if (argtype == MONEYOID)
	{
		char	   *func_name = get_func_name(aggref->aggfnoid);

		return strcmp(func_name, "min") == 0 ||
			strcmp(func_name, "max") == 0 ||
			strcmp(func_name, "count") == 0;
	}

	return true;
}

/*
 * Whether the scan built from 'scan' might recheck its index conditions in a
 * way a pushed-down aggregate row cannot satisfy.  A bitmap table scan
 * pushes the recheck of its pushable index quals down with the request, so
 * only a local recheck qual matters there.
 */
static bool
yb_scan_path_might_recheck(PlannerInfo *root, Path *scan)
{
	switch (scan->pathtype)
	{
		case T_IndexScan:
		case T_IndexOnlyScan:
			return yb_index_path_might_recheck(root, (IndexPath *) scan,
											   scan->pathtype ==
											   T_IndexOnlyScan);
		case T_YbBitmapTableScan:
			{
				YbBitmapTablePath *bpath = (YbBitmapTablePath *) scan;

				return yb_bitmap_path_has_local_recheck_quals(root, bpath) &&
					yb_bitmap_path_might_recheck(root, bpath->bitmapqual);
			}
		default:
			return false;
	}
}

/*
 * Whether the Index Scan (Index Only Scan when 'index_only') built from
 * 'path' might recheck its index conditions.  The answer is the one
 * YbPredetermineNeedsRecheck gives the executor at scan init, run here on
 * the same inputs.  A pushed-down aggregate is safe only when it is false:
 * the scan then returns aggregate rows, against which the recheck of
 * indexqualorig that a lossy index condition triggers cannot be evaluated
 * (nodeIndexscan.c asserts that no recheck is pending once aggrefs are set).
 */
static bool
yb_index_path_might_recheck(PlannerInfo *root, IndexPath *path,
							bool index_only)
{
	return yb_predetermine_index_path_recheck(root, path,
											  index_only ? T_IndexOnlyScan :
											  T_IndexScan,
											  index_only /* xs_want_itup */ ,
											  NULL /* num_runtime_keys_p */ );
}

/*
 * Whether the bitmap built from the 'bitmapqual' tree might require the
 * bitmap table scan to recheck its index conditions.  The combination
 * follows the executor (YbGetBitmapScanMightRecheck): a BitmapOr rechecks
 * when any child does, a BitmapAnd only when all of them do, and a leaf
 * counts any runtime key as a recheck because the predetermination cannot
 * see the key's value (nodeYbBitmapIndexscan.c).  The bitmap table scan
 * pushes a recheck of its pushable index quals down with the request and
 * evaluates the others locally, which a pushed-down aggregate row cannot
 * satisfy; it asserts that a required recheck has no local recheck quals
 * once aggrefs are set.
 */
static bool
yb_bitmap_path_might_recheck(PlannerInfo *root, Path *bitmapqual)
{
	ListCell   *lc;

	if (IsA(bitmapqual, BitmapOrPath))
	{
		foreach(lc, ((BitmapOrPath *) bitmapqual)->bitmapquals)
		{
			if (yb_bitmap_path_might_recheck(root, (Path *) lfirst(lc)))
				return true;
		}
		return false;
	}
	else if (IsA(bitmapqual, BitmapAndPath))
	{
		foreach(lc, ((BitmapAndPath *) bitmapqual)->bitmapquals)
		{
			if (!yb_bitmap_path_might_recheck(root, (Path *) lfirst(lc)))
				return false;
		}
		return true;
	}
	else if (IsA(bitmapqual, IndexPath))
	{
		int			num_runtime_keys;
		bool		recheck;

		recheck = yb_predetermine_index_path_recheck(root,
													 (IndexPath *) bitmapqual,
													 T_YbBitmapIndexScan,
													 true /* xs_want_itup */ ,
													 &num_runtime_keys);
		return num_runtime_keys > 0 || recheck;
	}

	elog(ERROR, "unrecognized node type: %d", (int) nodeTag(bitmapqual));
	return true;					/* keep compiler quiet */
}

/*
 * Run yb_index_might_recheck, which predetermines the recheck as the executor
 * does at scan init, on the inputs the executor derives from the plan built
 * for 'path'.  'scan_tag' is the plan node the executor passes as the Scan
 * (T_IndexScan, T_IndexOnlyScan or T_YbBitmapIndexScan) and 'xs_want_itup'
 * what that node passes.  *num_runtime_keys_p, when given, receives the
 * number of scan keys whose value is not a constant.
 *
 * YbPredetermineNeedsRecheck reads the index, the heap, the scan keys and two
 * fields of the Scan node, and the planner reproduces each of them:
 *
 * - The scan keys come from ExecIndexBuildScanKeys over the fixed index
 *   quals create_indexscan_plan hands the plan, with their opfuncids filled
 *   in as setrefs does before the executor sees them.  Its PlanState
 *   argument is only the parent of the ExprStates compiled for runtime
 *   keys.  An unparameterized path's right operands are Consts, Params or
 *   expressions over them, which compile without a parent; of the nodes an
 *   index condition can hold, only a SubPlan needs one, so a path carrying
 *   one is taken to recheck.  A runtime key holds (Datum) 0 at the
 *   executor's call too, so the few value-dependent tests see the same
 *   inputs.
 * - A stub Scan node, zero-filled apart from its tag and, for the two index
 *   scan tags, yb_merge_scan_info, stands in for the plan node.  Nothing
 *   else is read from it, and NULL would not do: a NULL Scan marks an
 *   internal scan, which is exempt from yb_test_skip_binding_scan_keys and
 *   skips the merge-scan pre-bind, and both of those make the executor's
 *   bind report a recheck.
 *
 * The relations are locked for planning already (plancat.c opens every index
 * of the rel at rellockmode), so NoLock is enough.  Everything allocated
 * here is thrown away with the temporary context.
 */
static bool
yb_predetermine_index_path_recheck(PlannerInfo *root, IndexPath *path,
								   NodeTag scan_tag, bool xs_want_itup,
								   int *num_runtime_keys_p)
{
	IndexOptInfo *index = path->indexinfo;
	RangeTblEntry *rte = planner_rt_fetch(path->path.parent->relid, root);
	bool		is_bitmap_leaf = (scan_tag == T_YbBitmapIndexScan);
	List	   *stripped_indexquals;
	List	   *fixed_indexquals;
	Relation	heap;
	Relation	indexrel;
	ScanKey		scan_keys = NULL;
	int			num_scan_keys = 0;
	IndexRuntimeKeyInfo *runtime_keys = NULL;
	int			num_runtime_keys = 0;
	IndexArrayKeyInfo *array_keys = NULL;
	int			num_array_keys = 0;
	Scan	   *stub;
	MemoryContext tmpcxt;
	MemoryContext oldcxt;
	bool		result;

	Assert(path->path.param_info == NULL);

	/* No Relation to run against; assume the worst, as yb_cost_index does. */
	if (index->hypothetical)
	{
		if (num_runtime_keys_p)
			*num_runtime_keys_p = 0;
		return true;
	}

	tmpcxt = AllocSetContextCreate(CurrentMemoryContext,
								   "yb agg pushdown recheck",
								   ALLOCSET_SMALL_SIZES);
	oldcxt = MemoryContextSwitchTo(tmpcxt);

	yb_fix_indexqual_references(root, path, &stripped_indexquals,
								&fixed_indexquals);

	/*
	 * A planner-built clause (a commuted or boolean-equality index qual)
	 * still holds opfuncid = InvalidOid, which ScanKeyEntryInitialize
	 * rejects; the fixed quals are fresh copies, so the fix-up is private.
	 */
	fix_opfuncids((Node *) fixed_indexquals);

	if (contain_subplans((Node *) fixed_indexquals))
		result = true;
	else
	{
		heap = table_open(rte->relid, NoLock);
		indexrel = index_open(index->indexoid, NoLock);

		/*
		 * Index Scan and Index Only Scan get no array-key slots: the planner
		 * gives them a ScalarArrayOpExpr condition only on an index that
		 * handles arrays natively.  A bitmap leaf may carry one on any index
		 * and gets the slots, as nodeYbBitmapIndexscan.c passes them.
		 */
		ExecIndexBuildScanKeys(NULL /* planstate */ , indexrel,
							   fixed_indexquals, false /* isorderby */ ,
							   &scan_keys, &num_scan_keys,
							   &runtime_keys, &num_runtime_keys,
							   is_bitmap_leaf ? &array_keys : NULL,
							   is_bitmap_leaf ? &num_array_keys : NULL);

		switch (scan_tag)
		{
			case T_IndexScan:
				{
					IndexScan  *iscan = makeNode(IndexScan);

					iscan->yb_merge_scan_info = yb_make_merge_scan_info(path);
					stub = (Scan *) iscan;
				}
				break;
			case T_IndexOnlyScan:
				{
					IndexOnlyScan *ioscan = makeNode(IndexOnlyScan);

					ioscan->yb_merge_scan_info =
						yb_make_merge_scan_info(path);
					stub = (Scan *) ioscan;
				}
				break;
			case T_YbBitmapIndexScan:
				stub = (Scan *) makeNode(YbBitmapIndexScan);
				break;
			default:
				elog(ERROR, "unrecognized node type: %d", (int) scan_tag);
				stub = NULL;		/* keep compiler quiet */
		}

		result = yb_index_might_recheck(stub, heap, indexrel, xs_want_itup,
										scan_keys, num_scan_keys);

		index_close(indexrel, NoLock);
		table_close(heap, NoLock);
	}

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(tmpcxt);

	if (num_runtime_keys_p)
		*num_runtime_keys_p = num_runtime_keys;
	return result;
}
