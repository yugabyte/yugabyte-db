/*-------------------------------------------------------------------------
 *
 * prepunion.c
 *	  Routines to plan set-operation queries.  The filename is a leftover
 *	  from a time when only UNIONs were implemented.
 *
 * There are two code paths in the planner for set-operation queries.
 * If a subquery consists entirely of simple UNION ALL operations, it
 * is converted into an "append relation".  Otherwise, it is handled
 * by the general code in this module (plan_set_operations and its
 * subroutines).  There is some support code here for the append-relation
 * case, but most of the heavy lifting for that is done elsewhere,
 * notably in prepjointree.c and allpaths.c.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/optimizer/prep/prepunion.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "catalog/partition.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/cost.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/planmain.h"
#include "optimizer/planner.h"
#include "optimizer/prep.h"
#include "optimizer/tlist.h"
#include "parser/parse_coerce.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/selfuncs.h"
#include "utils/syscache.h"


static RelOptInfo *recurse_set_operations(Node *setOp, PlannerInfo *root,
										  List *colTypes, List *colCollations,
										  bool junkOK,
										  int flag, List *refnames_tlist,
										  List **pTargetList,
										  double *pNumGroups);
static RelOptInfo *generate_recursion_path(SetOperationStmt *setOp,
										   PlannerInfo *root,
										   List *refnames_tlist,
										   List **pTargetList);
static RelOptInfo *generate_union_paths(SetOperationStmt *op, PlannerInfo *root,
										List *refnames_tlist,
										List **pTargetList);
static RelOptInfo *generate_nonunion_paths(SetOperationStmt *op, PlannerInfo *root,
										   List *refnames_tlist,
										   List **pTargetList);
static List *plan_union_children(PlannerInfo *root,
								 SetOperationStmt *top_union,
								 List *refnames_tlist,
								 List **tlist_list);
static Path *make_union_unique(SetOperationStmt *op, Path *path, List *tlist,
							   PlannerInfo *root);
static void postprocess_setop_rel(PlannerInfo *root, RelOptInfo *rel);
static bool choose_hashed_setop(PlannerInfo *root, List *groupClauses,
								Path *input_path,
								double dNumGroups, double dNumOutputRows,
								const char *construct);
static List *generate_setop_tlist(List *colTypes, List *colCollations,
								  int flag,
								  Index varno,
								  bool hack_constants,
								  List *input_tlist,
								  List *refnames_tlist);
static List *generate_append_tlist(List *colTypes, List *colCollations,
								   bool flag,
								   List *input_tlists,
								   List *refnames_tlist);
static List *generate_setop_grouplist(SetOperationStmt *op, List *targetlist);


/*
 * plan_set_operations
 *
 *	  Plans the queries for a tree of set operations (UNION/INTERSECT/EXCEPT)
 *
 * This routine only deals with the setOperations tree of the given query.
 * Any top-level ORDER BY requested in root->parse->sortClause will be handled
 * when we return to grouping_planner; likewise for LIMIT.
 *
 * What we return is an "upperrel" RelOptInfo containing at least one Path
 * that implements the set-operation tree.  In addition, root->processed_tlist
 * receives a targetlist representing the output of the topmost setop node.
 */
RelOptInfo *
plan_set_operations(PlannerInfo *root)
{
	Query	   *parse = root->parse;
	SetOperationStmt *topop = castNode(SetOperationStmt, parse->setOperations);
	Node	   *node;
	RangeTblEntry *leftmostRTE;
	Query	   *leftmostQuery;
	RelOptInfo *setop_rel;
	List	   *top_tlist;

	Assert(topop);

	/* check for unsupported stuff */
	Assert(parse->jointree->fromlist == NIL);
	Assert(parse->jointree->quals == NULL);
	Assert(parse->groupClause == NIL);
	Assert(parse->havingQual == NULL);
	Assert(parse->windowClause == NIL);
	Assert(parse->distinctClause == NIL);

	/*
	 * In the outer query level, we won't have any true equivalences to deal
	 * with; but we do want to be able to make pathkeys, which will require
	 * single-member EquivalenceClasses.  Indicate that EC merging is complete
	 * so that pathkeys.c won't complain.
	 */
	Assert(root->eq_classes == NIL);
	root->ec_merging_done = true;

	/*
	 * We'll need to build RelOptInfos for each of the leaf subqueries, which
	 * are RTE_SUBQUERY rangetable entries in this Query.  Prepare the index
	 * arrays for those, and for AppendRelInfos in case they're needed.
	 */
	setup_simple_rel_arrays(root);

	/*
	 * Find the leftmost component Query.  We need to use its column names for
	 * all generated tlists (else SELECT INTO won't work right).
	 */
	node = topop->larg;
	while (node && IsA(node, SetOperationStmt))
		node = ((SetOperationStmt *) node)->larg;
	Assert(node && IsA(node, RangeTblRef));
	leftmostRTE = root->simple_rte_array[((RangeTblRef *) node)->rtindex];
	leftmostQuery = leftmostRTE->subquery;
	Assert(leftmostQuery != NULL);

	/*
	 * If the topmost node is a recursive union, it needs special processing.
	 */
	if (root->hasRecursion)
	{
		setop_rel = generate_recursion_path(topop, root,
											leftmostQuery->targetList,
											&top_tlist);
	}
	else
	{
		/*
		 * Recurse on setOperations tree to generate paths for set ops. The
		 * final output paths should have just the column types shown as the
		 * output from the top-level node, plus possibly resjunk working
		 * columns (we can rely on upper-level nodes to deal with that).
		 */
		setop_rel = recurse_set_operations((Node *) topop, root,
										   topop->colTypes, topop->colCollations,
										   true, -1,
										   leftmostQuery->targetList,
										   &top_tlist,
										   NULL);
	}

	/* Must return the built tlist into root->processed_tlist. */
	root->processed_tlist = top_tlist;

	return setop_rel;
}

/*
 * recurse_set_operations
 *	  Recursively handle one step in a tree of set operations
 *
 * colTypes: OID list of set-op's result column datatypes
 * colCollations: OID list of set-op's result column collations
 * junkOK: if true, child resjunk columns may be left in the result
 * flag: if >= 0, add a resjunk output column indicating value of flag
 * refnames_tlist: targetlist to take column names from
 *
 * Returns a RelOptInfo for the subtree, as well as these output parameters:
 * *pTargetList: receives the fully-fledged tlist for the subtree's top plan
 * *pNumGroups: if not NULL, we estimate the number of distinct groups
 *		in the result, and store it there
 *
 * The pTargetList output parameter is mostly redundant with the pathtarget
 * of the returned RelOptInfo, but for the moment we need it because much of
 * the logic in this file depends on flag columns being marked resjunk.
 * Pending a redesign of how that works, this is the easy way out.
 *
 * We don't have to care about typmods here: the only allowed difference
 * between set-op input and output typmods is input is a specific typmod
 * and output is -1, and that does not require a coercion.
 */
static RelOptInfo *
recurse_set_operations(Node *setOp, PlannerInfo *root,
					   List *colTypes, List *colCollations,
					   bool junkOK,
					   int flag, List *refnames_tlist,
					   List **pTargetList,
					   double *pNumGroups)
{
	RelOptInfo *rel = NULL;		/* keep compiler quiet */

	/* Guard against stack overflow due to overly complex setop nests */
	check_stack_depth();

	if (IsA(setOp, RangeTblRef))
	{
		RangeTblRef *rtr = (RangeTblRef *) setOp;
		RangeTblEntry *rte = root->simple_rte_array[rtr->rtindex];
		Query	   *subquery = rte->subquery;
		PlannerInfo *subroot;
		RelOptInfo *final_rel;
		Path	   *subpath;
		Path	   *path;
		List	   *tlist;

		Assert(subquery != NULL);

		/* Build a RelOptInfo for this leaf subquery. */
		rel = build_simple_rel(root, rtr->rtindex, NULL);

		/* plan_params should not be in use in current query level */
		Assert(root->plan_params == NIL);

		/* Generate a subroot and Paths for the subquery */
		subroot = rel->subroot = subquery_planner(root->glob, subquery,
												  root,
												  false,
												  root->tuple_fraction);

		/*
		 * It should not be possible for the primitive query to contain any
		 * cross-references to other primitive queries in the setop tree.
		 */
		if (root->plan_params)
			elog(ERROR, "unexpected outer reference in set operation subquery");

		/* Figure out the appropriate target list for this subquery. */
		tlist = generate_setop_tlist(colTypes, colCollations,
									 flag,
									 rtr->rtindex,
									 true,
									 subroot->processed_tlist,
									 refnames_tlist);
		rel->reltarget = create_pathtarget(root, tlist);

		/* Return the fully-fledged tlist to caller, too */
		*pTargetList = tlist;

		/*
		 * Mark rel with estimated output rows, width, etc.  Note that we have
		 * to do this before generating outer-query paths, else
		 * cost_subqueryscan is not happy.
		 */
		set_subquery_size_estimates(root, rel);

		/*
		 * Since we may want to add a partial path to this relation, we must
		 * set its consider_parallel flag correctly.
		 */
		final_rel = fetch_upper_rel(subroot, UPPERREL_FINAL, NULL);
		rel->consider_parallel = final_rel->consider_parallel;

		/*
		 * For the moment, we consider only a single Path for the subquery.
		 * This should change soon (make it look more like
		 * set_subquery_pathlist).
		 */
		subpath = get_cheapest_fractional_path(final_rel,
											   root->tuple_fraction);

		/*
		 * Stick a SubqueryScanPath atop that.
		 *
		 * We don't bother to determine the subquery's output ordering since
		 * it won't be reflected in the set-op result anyhow; so just label
		 * the SubqueryScanPath with nil pathkeys.  (XXX that should change
		 * soon too, likely.)
		 */
		path = (Path *) create_subqueryscan_path(root, rel, subpath,
												 NIL, NULL);

		add_path(rel, path);

		/*
		 * If we have a partial path for the child relation, we can use that
		 * to build a partial path for this relation.  But there's no point in
		 * considering any path but the cheapest.
		 */
		if (rel->consider_parallel && bms_is_empty(rel->lateral_relids) &&
			final_rel->partial_pathlist != NIL)
		{
			Path	   *partial_subpath;
			Path	   *partial_path;

			partial_subpath = linitial(final_rel->partial_pathlist);
			partial_path = (Path *)
				create_subqueryscan_path(root, rel, partial_subpath,
										 NIL, NULL);
			add_partial_path(rel, partial_path);
		}

		/*
		 * Estimate number of groups if caller wants it.  If the subquery used
		 * grouping or aggregation, its output is probably mostly unique
		 * anyway; otherwise do statistical estimation.
		 *
		 * XXX you don't really want to know about this: we do the estimation
		 * using the subquery's original targetlist expressions, not the
		 * subroot->processed_tlist which might seem more appropriate.  The
		 * reason is that if the subquery is itself a setop, it may return a
		 * processed_tlist containing "varno 0" Vars generated by
		 * generate_append_tlist, and those would confuse estimate_num_groups
		 * mightily.  We ought to get rid of the "varno 0" hack, but that
		 * requires a redesign of the parsetree representation of setops, so
		 * that there can be an RTE corresponding to each setop's output.
		 */
		if (pNumGroups)
		{
			if (subquery->groupClause || subquery->groupingSets ||
				subquery->distinctClause ||
				subroot->hasHavingQual || subquery->hasAggs)
				*pNumGroups = subpath->rows;
			else
				*pNumGroups = estimate_num_groups(subroot,
												  get_tlist_exprs(subquery->targetList, false),
												  subpath->rows,
												  NULL,
												  NULL);
		}
	}
	else if (IsA(setOp, SetOperationStmt))
	{
		SetOperationStmt *op = (SetOperationStmt *) setOp;

		/* UNIONs are much different from INTERSECT/EXCEPT */
		if (op->op == SETOP_UNION)
			rel = generate_union_paths(op, root,
									   refnames_tlist,
									   pTargetList);
		else
			rel = generate_nonunion_paths(op, root,
										  refnames_tlist,
										  pTargetList);
		if (pNumGroups)
			*pNumGroups = rel->rows;

		/*
		 * If necessary, add a Result node to project the caller-requested
		 * output columns.
		 *
		 * XXX you don't really want to know about this: setrefs.c will apply
		 * fix_upper_expr() to the Result node's tlist. This would fail if the
		 * Vars generated by generate_setop_tlist() were not exactly equal()
		 * to the corresponding tlist entries of the subplan. However, since
		 * the subplan was generated by generate_union_paths() or
		 * generate_nonunion_paths(), and hence its tlist was generated by
		 * generate_append_tlist(), this will work.  We just tell
		 * generate_setop_tlist() to use varno 0.
		 */
		if (flag >= 0 ||
			!tlist_same_datatypes(*pTargetList, colTypes, junkOK) ||
			!tlist_same_collations(*pTargetList, colCollations, junkOK))
		{
			PathTarget *target;
			ListCell   *lc;

			*pTargetList = generate_setop_tlist(colTypes, colCollations,
												flag,
												0,
												false,
												*pTargetList,
												refnames_tlist);
			target = create_pathtarget(root, *pTargetList);

			/* Apply projection to each path */
			foreach(lc, rel->pathlist)
			{
				Path	   *subpath = (Path *) lfirst(lc);
				Path	   *path;

				Assert(subpath->param_info == NULL);
				path = apply_projection_to_path(root, subpath->parent,
												subpath, target);
				/* If we had to add a Result, path is different from subpath */
				if (path != subpath)
					lfirst(lc) = path;
			}

			/* Apply projection to each partial path */
			foreach(lc, rel->partial_pathlist)
			{
				Path	   *subpath = (Path *) lfirst(lc);
				Path	   *path;

				Assert(subpath->param_info == NULL);

				/* avoid apply_projection_to_path, in case of multiple refs */
				path = (Path *) create_projection_path(root, subpath->parent,
													   subpath, target);
				lfirst(lc) = path;
			}
		}
	}
	else
	{
		elog(ERROR, "unrecognized node type: %d",
			 (int) nodeTag(setOp));
		*pTargetList = NIL;
	}

	postprocess_setop_rel(root, rel);

	return rel;
}

/*
 * Generate paths for a recursive UNION node
 */
static RelOptInfo *
generate_recursion_path(SetOperationStmt *setOp, PlannerInfo *root,
						List *refnames_tlist,
						List **pTargetList)
{
	RelOptInfo *result_rel;
	Path	   *path;
	RelOptInfo *lrel,
			   *rrel;
	Path	   *lpath;
	Path	   *rpath;
	List	   *lpath_tlist;
	List	   *rpath_tlist;
	List	   *tlist;
	List	   *groupList;
	double		dNumGroups;

	/* Parser should have rejected other cases */
	if (setOp->op != SETOP_UNION)
		elog(ERROR, "only UNION queries can be recursive");
	/* Worktable ID should be assigned */
	Assert(root->wt_param_id >= 0);

	/*
	 * Unlike a regular UNION node, process the left and right inputs
	 * separately without any intention of combining them into one Append.
	 */
	lrel = recurse_set_operations(setOp->larg, root,
								  setOp->colTypes, setOp->colCollations,
								  false, -1,
								  refnames_tlist,
								  &lpath_tlist,
								  NULL);
	lpath = lrel->cheapest_total_path;
	/* The right path will want to look at the left one ... */
	root->non_recursive_path = lpath;
	rrel = recurse_set_operations(setOp->rarg, root,
								  setOp->colTypes, setOp->colCollations,
								  false, -1,
								  refnames_tlist,
								  &rpath_tlist,
								  NULL);
	rpath = rrel->cheapest_total_path;
	root->non_recursive_path = NULL;

	/*
	 * Generate tlist for RecursiveUnion path node --- same as in Append cases
	 */
	tlist = generate_append_tlist(setOp->colTypes, setOp->colCollations, false,
								  list_make2(lpath_tlist, rpath_tlist),
								  refnames_tlist);

	*pTargetList = tlist;

	/* Build result relation. */
	result_rel = fetch_upper_rel(root, UPPERREL_SETOP,
								 bms_union(lrel->relids, rrel->relids));
	result_rel->reltarget = create_pathtarget(root, tlist);

	/*
	 * If UNION, identify the grouping operators
	 */
	if (setOp->all)
	{
		groupList = NIL;
		dNumGroups = 0;
	}
	else
	{
		/* Identify the grouping semantics */
		groupList = generate_setop_grouplist(setOp, tlist);

		/* We only support hashing here */
		if (!grouping_is_hashable(groupList))
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("could not implement recursive UNION"),
					 errdetail("All column datatypes must be hashable.")));

		/*
		 * For the moment, take the number of distinct groups as equal to the
		 * total input size, ie, the worst case.
		 */
		dNumGroups = lpath->rows + rpath->rows * 10;
	}

	/*
	 * And make the path node.
	 */
	path = (Path *) create_recursiveunion_path(root,
											   result_rel,
											   lpath,
											   rpath,
											   result_rel->reltarget,
											   groupList,
											   root->wt_param_id,
											   dNumGroups);

	add_path(result_rel, path);
	postprocess_setop_rel(root, result_rel);
	return result_rel;
}

/*
 * Generate paths for a UNION or UNION ALL node
 */
static RelOptInfo *
generate_union_paths(SetOperationStmt *op, PlannerInfo *root,
					 List *refnames_tlist,
					 List **pTargetList)
{
	Relids		relids = NULL;
	RelOptInfo *result_rel;
	double		save_fraction = root->tuple_fraction;
	ListCell   *lc;
	List	   *pathlist = NIL;
	List	   *partial_pathlist = NIL;
	bool		partial_paths_valid = true;
	bool		consider_parallel = true;
	List	   *rellist;
	List	   *tlist_list;
	List	   *tlist;
	Path	   *path;

	/*
	 * If plain UNION, tell children to fetch all tuples.
	 *
	 * Note: in UNION ALL, we pass the top-level tuple_fraction unmodified to
	 * each arm of the UNION ALL.  One could make a case for reducing the
	 * tuple fraction for later arms (discounting by the expected size of the
	 * earlier arms' results) but it seems not worth the trouble. The normal
	 * case where tuple_fraction isn't already zero is a LIMIT at top level,
	 * and passing it down as-is is usually enough to get the desired result
	 * of preferring fast-start plans.
	 */
	if (!op->all)
		root->tuple_fraction = 0.0;

	/*
	 * If any of my children are identical UNION nodes (same op, all-flag, and
	 * colTypes) then they can be merged into this node so that we generate
	 * only one Append and unique-ification for the lot.  Recurse to find such
	 * nodes and compute their children's paths.
	 */
	rellist = plan_union_children(root, op, refnames_tlist, &tlist_list);

	/*
	 * Generate tlist for Append plan node.
	 *
	 * The tlist for an Append plan isn't important as far as the Append is
	 * concerned, but we must make it look real anyway for the benefit of the
	 * next plan level up.
	 */
	tlist = generate_append_tlist(op->colTypes, op->colCollations, false,
								  tlist_list, refnames_tlist);

	*pTargetList = tlist;

	/* Build path lists and relid set. */
	foreach(lc, rellist)
	{
		RelOptInfo *rel = lfirst(lc);

		pathlist = lappend(pathlist, rel->cheapest_total_path);

		if (consider_parallel)
		{
			if (!rel->consider_parallel)
			{
				consider_parallel = false;
				partial_paths_valid = false;
			}
			else if (rel->partial_pathlist == NIL)
				partial_paths_valid = false;
			else
				partial_pathlist = lappend(partial_pathlist,
										   linitial(rel->partial_pathlist));
		}

		relids = bms_union(relids, rel->relids);
	}

	/* Build result relation. */
	result_rel = fetch_upper_rel(root, UPPERREL_SETOP, relids);
	result_rel->reltarget = create_pathtarget(root, tlist);
	result_rel->consider_parallel = consider_parallel;

	/*
	 * Append the child results together.
	 */
	path = (Path *) create_append_path(root, result_rel, pathlist, NIL,
									   NIL, NULL, 0, false, -1);

	/*
	 * For UNION ALL, we just need the Append path.  For UNION, need to add
	 * node(s) to remove duplicates.
	 */
	if (!op->all)
		path = make_union_unique(op, path, tlist, root);

	add_path(result_rel, path);

	/*
	 * Estimate number of groups.  For now we just assume the output is unique
	 * --- this is certainly true for the UNION case, and we want worst-case
	 * estimates anyway.
	 */
	result_rel->rows = path->rows;

	/*
	 * Now consider doing the same thing using the partial paths plus Append
	 * plus Gather.
	 */
	if (partial_paths_valid)
	{
		Path	   *ppath;
		ListCell   *lc;
		int			parallel_workers = 0;

		/* Find the highest number of workers requested for any subpath. */
		foreach(lc, partial_pathlist)
		{
			Path	   *path = lfirst(lc);

			parallel_workers = Max(parallel_workers, path->parallel_workers);
		}
		Assert(parallel_workers > 0);

		/*
		 * If the use of parallel append is permitted, always request at least
		 * log2(# of children) paths.  We assume it can be useful to have
		 * extra workers in this case because they will be spread out across
		 * the children.  The precise formula is just a guess; see
		 * add_paths_to_append_rel.
		 */
		if (enable_parallel_append)
		{
			parallel_workers = Max(parallel_workers,
								   fls(list_length(partial_pathlist)));
			parallel_workers = Min(parallel_workers,
								   max_parallel_workers_per_gather);
		}
		Assert(parallel_workers > 0);

		ppath = (Path *)
			create_append_path(root, result_rel, NIL, partial_pathlist,
							   NIL, NULL,
							   parallel_workers, enable_parallel_append,
							   -1);
		ppath = (Path *)
			create_gather_path(root, result_rel, ppath,
							   result_rel->reltarget, NULL, NULL);
		if (!op->all)
			ppath = make_union_unique(op, ppath, tlist, root);
		add_path(result_rel, ppath);
	}

	/* Undo effects of possibly forcing tuple_fraction to 0 */
	root->tuple_fraction = save_fraction;

	return result_rel;
}

/*
 * Generate paths for an INTERSECT, INTERSECT ALL, EXCEPT, or EXCEPT ALL node
 */
static RelOptInfo *
generate_nonunion_paths(SetOperationStmt *op, PlannerInfo *root,
						List *refnames_tlist,
						List **pTargetList)
{
	RelOptInfo *result_rel;
	RelOptInfo *lrel,
			   *rrel;
	double		save_fraction = root->tuple_fraction;
	Path	   *lpath,
			   *rpath,
			   *path;
	List	   *lpath_tlist,
			   *rpath_tlist,
			   *tlist_list,
			   *tlist,
			   *groupList,
			   *pathlist;
	double		dLeftGroups,
				dRightGroups,
				dNumGroups,
				dNumOutputRows;
	bool		use_hash;
	SetOpCmd	cmd;
	int			firstFlag;

	/*
	 * Tell children to fetch all tuples.
	 */
	root->tuple_fraction = 0.0;

	/* Recurse on children, ensuring their outputs are marked */
	lrel = recurse_set_operations(op->larg, root,
								  op->colTypes, op->colCollations,
								  false, 0,
								  refnames_tlist,
								  &lpath_tlist,
								  &dLeftGroups);
	lpath = lrel->cheapest_total_path;
	rrel = recurse_set_operations(op->rarg, root,
								  op->colTypes, op->colCollations,
								  false, 1,
								  refnames_tlist,
								  &rpath_tlist,
								  &dRightGroups);
	rpath = rrel->cheapest_total_path;

	/* Undo effects of forcing tuple_fraction to 0 */
	root->tuple_fraction = save_fraction;

	/*
	 * For EXCEPT, we must put the left input first.  For INTERSECT, either
	 * order should give the same results, and we prefer to put the smaller
	 * input first in order to minimize the size of the hash table in the
	 * hashing case.  "Smaller" means the one with the fewer groups.
	 */
	if (op->op == SETOP_EXCEPT || dLeftGroups <= dRightGroups)
	{
		pathlist = list_make2(lpath, rpath);
		tlist_list = list_make2(lpath_tlist, rpath_tlist);
		firstFlag = 0;
	}
	else
	{
		pathlist = list_make2(rpath, lpath);
		tlist_list = list_make2(rpath_tlist, lpath_tlist);
		firstFlag = 1;
	}

	/*
	 * Generate tlist for Append plan node.
	 *
	 * The tlist for an Append plan isn't important as far as the Append is
	 * concerned, but we must make it look real anyway for the benefit of the
	 * next plan level up.  In fact, it has to be real enough that the flag
	 * column is shown as a variable not a constant, else setrefs.c will get
	 * confused.
	 */
	tlist = generate_append_tlist(op->colTypes, op->colCollations, true,
								  tlist_list, refnames_tlist);

	*pTargetList = tlist;

	/* Build result relation. */
	result_rel = fetch_upper_rel(root, UPPERREL_SETOP,
								 bms_union(lrel->relids, rrel->relids));
	result_rel->reltarget = create_pathtarget(root, tlist);

	/*
	 * Append the child results together.
	 */
	path = (Path *) create_append_path(root, result_rel, pathlist, NIL,
									   NIL, NULL, 0, false, -1);

	/* Identify the grouping semantics */
	groupList = generate_setop_grouplist(op, tlist);

	/*
	 * Estimate number of distinct groups that we'll need hashtable entries
	 * for; this is the size of the left-hand input for EXCEPT, or the smaller
	 * input for INTERSECT.  Also estimate the number of eventual output rows.
	 * In non-ALL cases, we estimate each group produces one output row; in
	 * ALL cases use the relevant relation size.  These are worst-case
	 * estimates, of course, but we need to be conservative.
	 */
	if (op->op == SETOP_EXCEPT)
	{
		dNumGroups = dLeftGroups;
		dNumOutputRows = op->all ? lpath->rows : dNumGroups;
	}
	else
	{
		dNumGroups = Min(dLeftGroups, dRightGroups);
		dNumOutputRows = op->all ? Min(lpath->rows, rpath->rows) : dNumGroups;
	}

	/*
	 * Decide whether to hash or sort, and add a sort node if needed.
	 */
	use_hash = choose_hashed_setop(root, groupList, path,
								   dNumGroups, dNumOutputRows,
								   (op->op == SETOP_INTERSECT) ? "INTERSECT" : "EXCEPT");

	if (groupList && !use_hash)
		path = (Path *) create_sort_path(root,
										 result_rel,
										 path,
										 make_pathkeys_for_sortclauses(root,
																	   groupList,
																	   tlist),
										 -1.0);

	/*
	 * Finally, add a SetOp path node to generate the correct output.
	 */
	switch (op->op)
	{
		case SETOP_INTERSECT:
			cmd = op->all ? SETOPCMD_INTERSECT_ALL : SETOPCMD_INTERSECT;
			break;
		case SETOP_EXCEPT:
			cmd = op->all ? SETOPCMD_EXCEPT_ALL : SETOPCMD_EXCEPT;
			break;
		default:
			elog(ERROR, "unrecognized set op: %d", (int) op->op);
			cmd = SETOPCMD_INTERSECT;	/* keep compiler quiet */
			break;
	}
	path = (Path *) create_setop_path(root,
									  result_rel,
									  path,
									  cmd,
									  use_hash ? SETOP_HASHED : SETOP_SORTED,
									  groupList,
									  list_length(op->colTypes) + 1,
									  use_hash ? firstFlag : -1,
									  dNumGroups,
									  dNumOutputRows);

	result_rel->rows = path->rows;
	add_path(result_rel, path);
	return result_rel;
}

/*
 * Pull up children of a UNION node that are identically-propertied UNIONs.
 *
 * NOTE: we can also pull a UNION ALL up into a UNION, since the distinct
 * output rows will be lost anyway.
 *
 * NOTE: currently, we ignore collations while determining if a child has
 * the same properties.  This is semantically sound only so long as all
 * collations have the same notion of equality.  It is valid from an
 * implementation standpoint because we don't care about the ordering of
 * a UNION child's result: UNION ALL results are always unordered, and
 * generate_union_paths will force a fresh sort if the top level is a UNION.
 */
static List *
plan_union_children(PlannerInfo *root,
					SetOperationStmt *top_union,
					List *refnames_tlist,
					List **tlist_list)
{
	List	   *pending_rels = list_make1(top_union);
	List	   *result = NIL;
	List	   *child_tlist;

	*tlist_list = NIL;

	while (pending_rels != NIL)
	{
		Node	   *setOp = linitial(pending_rels);

		pending_rels = list_delete_first(pending_rels);

		if (IsA(setOp, SetOperationStmt))
		{
			SetOperationStmt *op = (SetOperationStmt *) setOp;

			if (op->op == top_union->op &&
				(op->all == top_union->all || op->all) &&
				equal(op->colTypes, top_union->colTypes))
			{
				/* Same UNION, so fold children into parent */
				pending_rels = lcons(op->rarg, pending_rels);
				pending_rels = lcons(op->larg, pending_rels);
				continue;
			}
		}

		/*
		 * Not same, so plan this child separately.
		 *
		 * Note we disallow any resjunk columns in child results.  This is
		 * necessary since the Append node that implements the union won't do
		 * any projection, and upper levels will get confused if some of our
		 * output tuples have junk and some don't.  This case only arises when
		 * we have an EXCEPT or INTERSECT as child, else there won't be
		 * resjunk anyway.
		 */
		result = lappend(result, recurse_set_operations(setOp, root,
														top_union->colTypes,
														top_union->colCollations,
														false, -1,
														refnames_tlist,
														&child_tlist,
														NULL));
		*tlist_list = lappend(*tlist_list, child_tlist);
	}

	return result;
}

/*
 * Add nodes to the given path tree to unique-ify the result of a UNION.
 */
static Path *
make_union_unique(SetOperationStmt *op, Path *path, List *tlist,
				  PlannerInfo *root)
{
	RelOptInfo *result_rel = fetch_upper_rel(root, UPPERREL_SETOP, NULL);
	List	   *groupList;
	double		dNumGroups;

	/* Identify the grouping semantics */
	groupList = generate_setop_grouplist(op, tlist);

	/*
	 * XXX for the moment, take the number of distinct groups as equal to the
	 * total input size, ie, the worst case.  This is too conservative, but
	 * it's not clear how to get a decent estimate of the true size.  One
	 * should note as well the propensity of novices to write UNION rather
	 * than UNION ALL even when they don't expect any duplicates...
	 */
	dNumGroups = path->rows;

	/* Decide whether to hash or sort */
	if (choose_hashed_setop(root, groupList, path,
							dNumGroups, dNumGroups,
							"UNION"))
	{
		/* Hashed aggregate plan --- no sort needed */
		path = (Path *) create_agg_path(root,
										result_rel,
										path,
										create_pathtarget(root, tlist),
										AGG_HASHED,
										AGGSPLIT_SIMPLE,
										groupList,
										NIL,
										NULL,
										dNumGroups);
	}
	else
	{
		/* Sort and Unique */
		if (groupList)
			path = (Path *)
				create_sort_path(root,
								 result_rel,
								 path,
								 make_pathkeys_for_sortclauses(root,
															   groupList,
															   tlist),
								 -1.0);
		path = (Path *) create_upper_unique_path(root,
												 result_rel,
												 path,
												 list_length(path->pathkeys),
												 dNumGroups);
	}

	return path;
}

/*
 * postprocess_setop_rel - perform steps required after adding paths
 */
static void
postprocess_setop_rel(PlannerInfo *root, RelOptInfo *rel)
{
	/*
	 * We don't currently worry about allowing FDWs to contribute paths to
	 * this relation, but give extensions a chance.
	 */
	if (create_upper_paths_hook)
		(*create_upper_paths_hook) (root, UPPERREL_SETOP,
									NULL, rel, NULL);

	/* Select cheapest path */
	set_cheapest(rel);
}

/*
 * choose_hashed_setop - should we use hashing for a set operation?
 */
static bool
choose_hashed_setop(PlannerInfo *root, List *groupClauses,
					Path *input_path,
					double dNumGroups, double dNumOutputRows,
					const char *construct)
{
	int			numGroupCols = list_length(groupClauses);
	Size		hash_mem_limit = get_hash_memory_limit();
	bool		can_sort;
	bool		can_hash;
	Size		hashentrysize;
	Path		hashed_p;
	Path		sorted_p;
	double		tuple_fraction;

	/* Check whether the operators support sorting or hashing */
	can_sort = grouping_is_sortable(groupClauses);
	can_hash = grouping_is_hashable(groupClauses);
	if (can_hash && can_sort)
	{
		/* we have a meaningful choice to make, continue ... */
	}
	else if (can_hash)
		return true;
	else if (can_sort)
		return false;
	else
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		/* translator: %s is UNION, INTERSECT, or EXCEPT */
				 errmsg("could not implement %s", construct),
				 errdetail("Some of the datatypes only support hashing, while others only support sorting.")));

	/* Prefer sorting when enable_hashagg is off */
	if (!enable_hashagg)
		return false;

	/*
	 * Don't do it if it doesn't look like the hashtable will fit into
	 * hash_mem.
	 */
	hashentrysize = MAXALIGN(input_path->pathtarget->width) + MAXALIGN(SizeofMinimalTupleHeader);

	if (hashentrysize * dNumGroups > hash_mem_limit)
		return false;

	/*
	 * See if the estimated cost is no more than doing it the other way.
	 *
	 * We need to consider input_plan + hashagg versus input_plan + sort +
	 * group.  Note that the actual result plan might involve a SetOp or
	 * Unique node, not Agg or Group, but the cost estimates for Agg and Group
	 * should be close enough for our purposes here.
	 *
	 * These path variables are dummies that just hold cost fields; we don't
	 * make actual Paths for these steps.
	 */
	cost_agg(&hashed_p, root, AGG_HASHED, NULL,
			 numGroupCols, dNumGroups,
			 NIL,
			 input_path->startup_cost, input_path->total_cost,
			 input_path->rows, input_path->pathtarget->width);

	/*
	 * Now for the sorted case.  Note that the input is *always* unsorted,
	 * since it was made by appending unrelated sub-relations together.
	 */
	sorted_p.startup_cost = input_path->startup_cost;
	sorted_p.total_cost = input_path->total_cost;
	/* XXX cost_sort doesn't actually look at pathkeys, so just pass NIL */
	cost_sort(&sorted_p, root, NIL, sorted_p.total_cost,
			  input_path->rows, input_path->pathtarget->width,
			  0.0, work_mem, -1.0);
	cost_group(&sorted_p, root, numGroupCols, dNumGroups,
			   NIL,
			   sorted_p.startup_cost, sorted_p.total_cost,
			   input_path->rows);

	/*
	 * Now make the decision using the top-level tuple fraction.  First we
	 * have to convert an absolute count (LIMIT) into fractional form.
	 */
	tuple_fraction = root->tuple_fraction;
	if (tuple_fraction >= 1.0)
		tuple_fraction /= dNumOutputRows;

	if (compare_fractional_path_costs(&hashed_p, &sorted_p,
									  tuple_fraction) < 0)
	{
		/* Hashed is cheaper, so use it */
		return true;
	}
	return false;
}

/*
 * Generate targetlist for a set-operation plan node
 *
 * colTypes: OID list of set-op's result column datatypes
 * colCollations: OID list of set-op's result column collations
 * flag: -1 if no flag column needed, 0 or 1 to create a const flag column
 * varno: varno to use in generated Vars
 * hack_constants: true to copy up constants (see comments in code)
 * input_tlist: targetlist of this node's input node
 * refnames_tlist: targetlist to take column names from
 */
static List *
generate_setop_tlist(List *colTypes, List *colCollations,
					 int flag,
					 Index varno,
					 bool hack_constants,
					 List *input_tlist,
					 List *refnames_tlist)
{
	List	   *tlist = NIL;
	int			resno = 1;
	ListCell   *ctlc,
			   *cclc,
			   *itlc,
			   *rtlc;
	TargetEntry *tle;
	Node	   *expr;

	forfour(ctlc, colTypes, cclc, colCollations,
			itlc, input_tlist, rtlc, refnames_tlist)
	{
		Oid			colType = lfirst_oid(ctlc);
		Oid			colColl = lfirst_oid(cclc);
		TargetEntry *inputtle = (TargetEntry *) lfirst(itlc);
		TargetEntry *reftle = (TargetEntry *) lfirst(rtlc);

		Assert(inputtle->resno == resno);
		Assert(reftle->resno == resno);
		Assert(!inputtle->resjunk);
		Assert(!reftle->resjunk);

		/*
		 * Generate columns referencing input columns and having appropriate
		 * data types and column names.  Insert datatype coercions where
		 * necessary.
		 *
		 * HACK: constants in the input's targetlist are copied up as-is
		 * rather than being referenced as subquery outputs.  This is mainly
		 * to ensure that when we try to coerce them to the output column's
		 * datatype, the right things happen for UNKNOWN constants.  But do
		 * this only at the first level of subquery-scan plans; we don't want
		 * phony constants appearing in the output tlists of upper-level
		 * nodes!
		 */
		if (hack_constants && inputtle->expr && IsA(inputtle->expr, Const))
			expr = (Node *) inputtle->expr;
		else
			expr = (Node *) makeVar(varno,
									inputtle->resno,
									exprType((Node *) inputtle->expr),
									exprTypmod((Node *) inputtle->expr),
									exprCollation((Node *) inputtle->expr),
									0);

		if (exprType(expr) != colType)
		{
			/*
			 * Note: it's not really cool to be applying coerce_to_common_type
			 * here; one notable point is that assign_expr_collations never
			 * gets run on any generated nodes.  For the moment that's not a
			 * problem because we force the correct exposed collation below.
			 * It would likely be best to make the parser generate the correct
			 * output tlist for every set-op to begin with, though.
			 */
			expr = coerce_to_common_type(NULL,	/* no UNKNOWNs here */
										 expr,
										 colType,
										 "UNION/INTERSECT/EXCEPT");
		}

		/*
		 * Ensure the tlist entry's exposed collation matches the set-op. This
		 * is necessary because plan_set_operations() reports the result
		 * ordering as a list of SortGroupClauses, which don't carry collation
		 * themselves but just refer to tlist entries.  If we don't show the
		 * right collation then planner.c might do the wrong thing in
		 * higher-level queries.
		 *
		 * Note we use RelabelType, not CollateExpr, since this expression
		 * will reach the executor without any further processing.
		 */
		if (exprCollation(expr) != colColl)
			expr = applyRelabelType(expr,
									exprType(expr), exprTypmod(expr), colColl,
									COERCE_IMPLICIT_CAST, -1, false);

		tle = makeTargetEntry((Expr *) expr,
							  (AttrNumber) resno++,
							  pstrdup(reftle->resname),
							  false);

		/*
		 * By convention, all non-resjunk columns in a setop tree have
		 * ressortgroupref equal to their resno.  In some cases the ref isn't
		 * needed, but this is a cleaner way than modifying the tlist later.
		 */
		tle->ressortgroupref = tle->resno;

		tlist = lappend(tlist, tle);
	}

	if (flag >= 0)
	{
		/* Add a resjunk flag column */
		/* flag value is the given constant */
		expr = (Node *) makeConst(INT4OID,
								  -1,
								  InvalidOid,
								  sizeof(int32),
								  Int32GetDatum(flag),
								  false,
								  true);
		tle = makeTargetEntry((Expr *) expr,
							  (AttrNumber) resno++,
							  pstrdup("flag"),
							  true);
		tlist = lappend(tlist, tle);
	}

	return tlist;
}

/*
 * Generate targetlist for a set-operation Append node
 *
 * colTypes: OID list of set-op's result column datatypes
 * colCollations: OID list of set-op's result column collations
 * flag: true to create a flag column copied up from subplans
 * input_tlists: list of tlists for sub-plans of the Append
 * refnames_tlist: targetlist to take column names from
 *
 * The entries in the Append's targetlist should always be simple Vars;
 * we just have to make sure they have the right datatypes/typmods/collations.
 * The Vars are always generated with varno 0.
 *
 * XXX a problem with the varno-zero approach is that set_pathtarget_cost_width
 * cannot figure out a realistic width for the tlist we make here.  But we
 * ought to refactor this code to produce a PathTarget directly, anyway.
 */
static List *
generate_append_tlist(List *colTypes, List *colCollations,
					  bool flag,
					  List *input_tlists,
					  List *refnames_tlist)
{
	List	   *tlist = NIL;
	int			resno = 1;
	ListCell   *curColType;
	ListCell   *curColCollation;
	ListCell   *ref_tl_item;
	int			colindex;
	TargetEntry *tle;
	Node	   *expr;
	ListCell   *tlistl;
	int32	   *colTypmods;

	/*
	 * First extract typmods to use.
	 *
	 * If the inputs all agree on type and typmod of a particular column, use
	 * that typmod; else use -1.
	 */
	colTypmods = (int32 *) palloc(list_length(colTypes) * sizeof(int32));

	foreach(tlistl, input_tlists)
	{
		List	   *subtlist = (List *) lfirst(tlistl);
		ListCell   *subtlistl;

		curColType = list_head(colTypes);
		colindex = 0;
		foreach(subtlistl, subtlist)
		{
			TargetEntry *subtle = (TargetEntry *) lfirst(subtlistl);

			if (subtle->resjunk)
				continue;
			Assert(curColType != NULL);
			if (exprType((Node *) subtle->expr) == lfirst_oid(curColType))
			{
				/* If first subplan, copy the typmod; else compare */
				int32		subtypmod = exprTypmod((Node *) subtle->expr);

				if (tlistl == list_head(input_tlists))
					colTypmods[colindex] = subtypmod;
				else if (subtypmod != colTypmods[colindex])
					colTypmods[colindex] = -1;
			}
			else
			{
				/* types disagree, so force typmod to -1 */
				colTypmods[colindex] = -1;
			}
			curColType = lnext(colTypes, curColType);
			colindex++;
		}
		Assert(curColType == NULL);
	}

	/*
	 * Now we can build the tlist for the Append.
	 */
	colindex = 0;
	forthree(curColType, colTypes, curColCollation, colCollations,
			 ref_tl_item, refnames_tlist)
	{
		Oid			colType = lfirst_oid(curColType);
		int32		colTypmod = colTypmods[colindex++];
		Oid			colColl = lfirst_oid(curColCollation);
		TargetEntry *reftle = (TargetEntry *) lfirst(ref_tl_item);

		Assert(reftle->resno == resno);
		Assert(!reftle->resjunk);
		expr = (Node *) makeVar(0,
								resno,
								colType,
								colTypmod,
								colColl,
								0);
		tle = makeTargetEntry((Expr *) expr,
							  (AttrNumber) resno++,
							  pstrdup(reftle->resname),
							  false);

		/*
		 * By convention, all non-resjunk columns in a setop tree have
		 * ressortgroupref equal to their resno.  In some cases the ref isn't
		 * needed, but this is a cleaner way than modifying the tlist later.
		 */
		tle->ressortgroupref = tle->resno;

		tlist = lappend(tlist, tle);
	}

	if (flag)
	{
		/* Add a resjunk flag column */
		/* flag value is shown as copied up from subplan */
		expr = (Node *) makeVar(0,
								resno,
								INT4OID,
								-1,
								InvalidOid,
								0);
		tle = makeTargetEntry((Expr *) expr,
							  (AttrNumber) resno++,
							  pstrdup("flag"),
							  true);
		tlist = lappend(tlist, tle);
	}

	pfree(colTypmods);

	return tlist;
}

/*
 * generate_setop_grouplist
 *		Build a SortGroupClause list defining the sort/grouping properties
 *		of the setop's output columns.
 *
 * Parse analysis already determined the properties and built a suitable
 * list, except that the entries do not have sortgrouprefs set because
 * the parser output representation doesn't include a tlist for each
 * setop.  So what we need to do here is copy that list and install
 * proper sortgrouprefs into it (copying those from the targetlist).
 */
static List *
generate_setop_grouplist(SetOperationStmt *op, List *targetlist)
{
	List	   *grouplist = copyObject(op->groupClauses);
	ListCell   *lg;
	ListCell   *lt;

	lg = list_head(grouplist);
	foreach(lt, targetlist)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lt);
		SortGroupClause *sgc;

		if (tle->resjunk)
		{
			/* resjunk columns should not have sortgrouprefs */
			Assert(tle->ressortgroupref == 0);
			continue;			/* ignore resjunk columns */
		}

		/* non-resjunk columns should have sortgroupref = resno */
		Assert(tle->ressortgroupref == tle->resno);

		/* non-resjunk columns should have grouping clauses */
		Assert(lg != NULL);
		sgc = (SortGroupClause *) lfirst(lg);
		lg = lnext(grouplist, lg);
		Assert(sgc->tleSortGroupRef == 0);

		sgc->tleSortGroupRef = tle->ressortgroupref;
	}
	Assert(lg == NULL);
	return grouplist;
}
