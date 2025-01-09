/*-------------------------------------------------------------------------
 *
 * pathnode.c
 *	  Routines to manipulate pathlists and create path nodes
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/optimizer/util/pathnode.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>

#include "catalog/pg_am.h"
#include "foreign/fdwapi.h"
#include "miscadmin.h"
#include "nodes/extensible.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/appendinfo.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/optimizer.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/planmain.h"
#include "optimizer/prep.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/tlist.h"
#include "parser/parsetree.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/selfuncs.h"

#include "pg_yb_utils.h"
#include "access/xact.h"
#include "access/yb_scan.h"

typedef enum
{
	COSTS_EQUAL,				/* path costs are fuzzily equal */
	COSTS_BETTER1,				/* first path is cheaper than second */
	COSTS_BETTER2,				/* second path is cheaper than first */
	COSTS_DIFFERENT				/* neither path dominates the other on cost */
} PathCostComparison;

/*
 * STD_FUZZ_FACTOR is the normal fuzz factor for compare_path_costs_fuzzily.
 * XXX is it worth making this user-controllable?  It provides a tradeoff
 * between planner runtime and the accuracy of path cost comparisons.
 */
#define STD_FUZZ_FACTOR 1.01

static List *translate_sub_tlist(List *tlist, int relid);
static int	append_total_cost_compare(const ListCell *a, const ListCell *b);
static int	append_startup_cost_compare(const ListCell *a, const ListCell *b);
static List *reparameterize_pathlist_by_child(PlannerInfo *root,
											  List *pathlist,
											  RelOptInfo *child_rel);


/*****************************************************************************
 *		MISC. PATH UTILITIES
 *****************************************************************************/

/*
 * compare_path_costs
 *	  Return -1, 0, or +1 according as path1 is cheaper, the same cost,
 *	  or more expensive than path2 for the specified criterion.
 */
int
compare_path_costs(Path *path1, Path *path2, CostSelector criterion)
{
	if (criterion == STARTUP_COST)
	{
		if (path1->startup_cost < path2->startup_cost)
			return -1;
		if (path1->startup_cost > path2->startup_cost)
			return +1;

		/*
		 * If paths have the same startup cost (not at all unlikely), order
		 * them by total cost.
		 */
		if (path1->total_cost < path2->total_cost)
			return -1;
		if (path1->total_cost > path2->total_cost)
			return +1;
	}
	else
	{
		if (path1->total_cost < path2->total_cost)
			return -1;
		if (path1->total_cost > path2->total_cost)
			return +1;

		/*
		 * If paths have the same total cost, order them by startup cost.
		 */
		if (path1->startup_cost < path2->startup_cost)
			return -1;
		if (path1->startup_cost > path2->startup_cost)
			return +1;
	}
	return 0;
}

/*
 * compare_fractional_path_costs
 *	  Return -1, 0, or +1 according as path1 is cheaper, the same cost,
 *	  or more expensive than path2 for fetching the specified fraction
 *	  of the total tuples.
 *
 * If fraction is <= 0 or > 1, we interpret it as 1, ie, we select the
 * path with the cheaper total_cost.
 */
int
compare_fractional_path_costs(Path *path1, Path *path2,
							  double fraction)
{
	Cost		cost1,
				cost2;

	if (fraction <= 0.0 || fraction >= 1.0)
		return compare_path_costs(path1, path2, TOTAL_COST);
	cost1 = path1->startup_cost +
		fraction * (path1->total_cost - path1->startup_cost);
	cost2 = path2->startup_cost +
		fraction * (path2->total_cost - path2->startup_cost);
	if (cost1 < cost2)
		return -1;
	if (cost1 > cost2)
		return +1;
	return 0;
}

/*
 * compare_path_costs_fuzzily
 *	  Compare the costs of two paths to see if either can be said to
 *	  dominate the other.
 *
 * We use fuzzy comparisons so that add_path() can avoid keeping both of
 * a pair of paths that really have insignificantly different cost.
 *
 * The fuzz_factor argument must be 1.0 plus delta, where delta is the
 * fraction of the smaller cost that is considered to be a significant
 * difference.  For example, fuzz_factor = 1.01 makes the fuzziness limit
 * be 1% of the smaller cost.
 *
 * The two paths are said to have "equal" costs if both startup and total
 * costs are fuzzily the same.  Path1 is said to be better than path2 if
 * it has fuzzily better startup cost and fuzzily no worse total cost,
 * or if it has fuzzily better total cost and fuzzily no worse startup cost.
 * Path2 is better than path1 if the reverse holds.  Finally, if one path
 * is fuzzily better than the other on startup cost and fuzzily worse on
 * total cost, we just say that their costs are "different", since neither
 * dominates the other across the whole performance spectrum.
 *
 * This function also enforces a policy rule that paths for which the relevant
 * one of parent->consider_startup and parent->consider_param_startup is false
 * cannot survive comparisons solely on the grounds of good startup cost, so
 * we never return COSTS_DIFFERENT when that is true for the total-cost loser.
 * (But if total costs are fuzzily equal, we compare startup costs anyway,
 * in hopes of eliminating one path or the other.)
 */
static PathCostComparison
compare_path_costs_fuzzily(Path *path1, Path *path2, double fuzz_factor)
{
#define CONSIDER_PATH_STARTUP_COST(p)  \
	((p)->param_info == NULL ? (p)->parent->consider_startup : (p)->parent->consider_param_startup)

	/*
	 * Check total cost first since it's more likely to be different; many
	 * paths have zero startup cost.
	 */
	if (path1->total_cost > path2->total_cost * fuzz_factor)
	{
		/* path1 fuzzily worse on total cost */
		if (CONSIDER_PATH_STARTUP_COST(path1) &&
			path2->startup_cost > path1->startup_cost * fuzz_factor)
		{
			/* ... but path2 fuzzily worse on startup, so DIFFERENT */
			return COSTS_DIFFERENT;
		}
		/* else path2 dominates */
		return COSTS_BETTER2;
	}
	if (path2->total_cost > path1->total_cost * fuzz_factor)
	{
		/* path2 fuzzily worse on total cost */
		if (CONSIDER_PATH_STARTUP_COST(path2) &&
			path1->startup_cost > path2->startup_cost * fuzz_factor)
		{
			/* ... but path1 fuzzily worse on startup, so DIFFERENT */
			return COSTS_DIFFERENT;
		}
		/* else path1 dominates */
		return COSTS_BETTER1;
	}
	/* fuzzily the same on total cost ... */
	if (path1->startup_cost > path2->startup_cost * fuzz_factor)
	{
		/* ... but path1 fuzzily worse on startup, so path2 wins */
		return COSTS_BETTER2;
	}
	if (path2->startup_cost > path1->startup_cost * fuzz_factor)
	{
		/* ... but path2 fuzzily worse on startup, so path1 wins */
		return COSTS_BETTER1;
	}
	/* fuzzily the same on both costs */
	return COSTS_EQUAL;

#undef CONSIDER_PATH_STARTUP_COST
}

static BMS_Comparison
yb_bms_compare_ppi(Path *path1, Path *path2)
{
	Relids path1_batchinfo = YB_PATH_REQ_OUTER_BATCHED(path1);

	Relids path2_batchinfo = YB_PATH_REQ_OUTER_BATCHED(path2);

	if (bms_is_empty(path1_batchinfo) ^ bms_is_empty(path2_batchinfo))
		return BMS_DIFFERENT;

	return bms_subset_compare(PATH_REQ_OUTER(path1), PATH_REQ_OUTER(path2));
}

/*
 * set_cheapest
 *	  Find the minimum-cost paths from among a relation's paths,
 *	  and save them in the rel's cheapest-path fields.
 *
 * cheapest_total_path is normally the cheapest-total-cost unparameterized
 * path; but if there are no unparameterized paths, we assign it to be the
 * best (cheapest least-parameterized) parameterized path.  However, only
 * unparameterized paths are considered candidates for cheapest_startup_path,
 * so that will be NULL if there are no unparameterized paths.
 *
 * The cheapest_parameterized_paths list collects all parameterized paths
 * that have survived the add_path() tournament for this relation.  (Since
 * add_path ignores pathkeys for a parameterized path, these will be paths
 * that have best cost or best row count for their parameterization.  We
 * may also have both a parallel-safe and a non-parallel-safe path in some
 * cases for the same parameterization in some cases, but this should be
 * relatively rare since, most typically, all paths for the same relation
 * will be parallel-safe or none of them will.)
 *
 * cheapest_parameterized_paths always includes the cheapest-total
 * unparameterized path, too, if there is one; the users of that list find
 * it more convenient if that's included.
 *
 * This is normally called only after we've finished constructing the path
 * list for the rel node.
 */
void
set_cheapest(RelOptInfo *parent_rel)
{
	Path	   *cheapest_startup_path;
	Path	   *cheapest_total_path;
	Path	   *best_param_path;
	List	   *parameterized_paths;
	ListCell   *p;

	Assert(IsA(parent_rel, RelOptInfo));

	if (parent_rel->pathlist == NIL)
		elog(ERROR, "could not devise a query plan for the given query");

	cheapest_startup_path = cheapest_total_path = best_param_path = NULL;
	parameterized_paths = NIL;

	foreach(p, parent_rel->pathlist)
	{
		Path	   *path = (Path *) lfirst(p);
		int			cmp;

		if (path->param_info)
		{
			/* Parameterized path, so add it to parameterized_paths */
			parameterized_paths = lappend(parameterized_paths, path);

			/*
			 * If we have an unparameterized cheapest-total, we no longer care
			 * about finding the best parameterized path, so move on.
			 */
			if (cheapest_total_path)
				continue;

			/*
			 * Otherwise, track the best parameterized path, which is the one
			 * with least total cost among those of the minimum
			 * parameterization.
			 */
			if (best_param_path == NULL)
				best_param_path = path;
			else
			{
				switch (yb_bms_compare_ppi(path,
													best_param_path))
				{
					case BMS_EQUAL:
						/* keep the cheaper one */
						if (compare_path_costs(path, best_param_path,
											   TOTAL_COST) < 0)
							best_param_path = path;
						break;
					case BMS_SUBSET1:
						/* new path is less-parameterized */
						best_param_path = path;
						break;
					case BMS_SUBSET2:
						/* old path is less-parameterized, keep it */
						break;
					case BMS_DIFFERENT:

						/*
						 * This means that neither path has the least possible
						 * parameterization for the rel.  We'll sit on the old
						 * path until something better comes along.
						 */
						break;
				}
			}
		}
		else
		{
			/* Unparameterized path, so consider it for cheapest slots */
			if (cheapest_total_path == NULL)
			{
				cheapest_startup_path = cheapest_total_path = path;
				continue;
			}

			/*
			 * If we find two paths of identical costs, try to keep the
			 * better-sorted one.  The paths might have unrelated sort
			 * orderings, in which case we can only guess which might be
			 * better to keep, but if one is superior then we definitely
			 * should keep that one.
			 */
			cmp = compare_path_costs(cheapest_startup_path, path, STARTUP_COST);
			if (cmp > 0 ||
				(cmp == 0 &&
				 compare_pathkeys(cheapest_startup_path->pathkeys,
								  path->pathkeys) == PATHKEYS_BETTER2))
				cheapest_startup_path = path;

			cmp = compare_path_costs(cheapest_total_path, path, TOTAL_COST);
			if (cmp > 0 ||
				(cmp == 0 &&
				 compare_pathkeys(cheapest_total_path->pathkeys,
								  path->pathkeys) == PATHKEYS_BETTER2))
				cheapest_total_path = path;
		}
	}

	/* Add cheapest unparameterized path, if any, to parameterized_paths */
	if (cheapest_total_path)
		parameterized_paths = lcons(cheapest_total_path, parameterized_paths);

	/*
	 * If there is no unparameterized path, use the best parameterized path as
	 * cheapest_total_path (but not as cheapest_startup_path).
	 */
	if (cheapest_total_path == NULL)
		cheapest_total_path = best_param_path;
	Assert(cheapest_total_path != NULL);

	parent_rel->cheapest_startup_path = cheapest_startup_path;
	parent_rel->cheapest_total_path = cheapest_total_path;
	parent_rel->cheapest_unique_path = NULL;	/* computed only if needed */
	parent_rel->cheapest_parameterized_paths = parameterized_paths;
}

/*
 * add_path
 *	  Consider a potential implementation path for the specified parent rel,
 *	  and add it to the rel's pathlist if it is worthy of consideration.
 *	  A path is worthy if it has a better sort order (better pathkeys) or
 *	  cheaper cost (on either dimension), or generates fewer rows, than any
 *	  existing path that has the same or superset parameterization rels.
 *	  We also consider parallel-safe paths more worthy than others.
 *
 *	  We also remove from the rel's pathlist any old paths that are dominated
 *	  by new_path --- that is, new_path is cheaper, at least as well ordered,
 *	  generates no more rows, requires no outer rels not required by the old
 *	  path, and is no less parallel-safe.
 *
 *	  In most cases, a path with a superset parameterization will generate
 *	  fewer rows (since it has more join clauses to apply), so that those two
 *	  figures of merit move in opposite directions; this means that a path of
 *	  one parameterization can seldom dominate a path of another.  But such
 *	  cases do arise, so we make the full set of checks anyway.
 *
 *	  There are two policy decisions embedded in this function, along with
 *	  its sibling add_path_precheck.  First, we treat all parameterized paths
 *	  as having NIL pathkeys, so that they cannot win comparisons on the
 *	  basis of sort order.  This is to reduce the number of parameterized
 *	  paths that are kept; see discussion in src/backend/optimizer/README.
 *
 *	  Second, we only consider cheap startup cost to be interesting if
 *	  parent_rel->consider_startup is true for an unparameterized path, or
 *	  parent_rel->consider_param_startup is true for a parameterized one.
 *	  Again, this allows discarding useless paths sooner.
 *
 *	  The pathlist is kept sorted by total_cost, with cheaper paths
 *	  at the front.  Within this routine, that's simply a speed hack:
 *	  doing it that way makes it more likely that we will reject an inferior
 *	  path after a few comparisons, rather than many comparisons.
 *	  However, add_path_precheck relies on this ordering to exit early
 *	  when possible.
 *
 *	  NOTE: discarded Path objects are immediately pfree'd to reduce planner
 *	  memory consumption.  We dare not try to free the substructure of a Path,
 *	  since much of it may be shared with other Paths or the query tree itself;
 *	  but just recycling discarded Path nodes is a very useful savings in
 *	  a large join tree.  We can recycle the List nodes of pathlist, too.
 *
 *	  As noted in optimizer/README, deleting a previously-accepted Path is
 *	  safe because we know that Paths of this rel cannot yet be referenced
 *	  from any other rel, such as a higher-level join.  However, in some cases
 *	  it is possible that a Path is referenced by another Path for its own
 *	  rel; we must not delete such a Path, even if it is dominated by the new
 *	  Path.  Currently this occurs only for IndexPath objects, which may be
 *	  referenced as children of BitmapHeapPaths as well as being paths in
 *	  their own right.  Hence, we don't pfree IndexPaths when rejecting them.
 *	  YB: We ensure that such behavior is avoided for distinct pushdown paths
 *	  in create_distinct_paths by avoiding distinctifying already distinct
 *	  paths.
 *
 * 'parent_rel' is the relation entry to which the path corresponds.
 * 'new_path' is a potential path for parent_rel.
 *
 * Returns nothing, but modifies parent_rel->pathlist.
 */
void
add_path(RelOptInfo *parent_rel, Path *new_path)
{
	bool		accept_new = true;	/* unless we find a superior old path */
	int			insert_at = 0;	/* where to insert new item */
	List	   *new_path_pathkeys;
	ListCell   *p1;

	/*
	 * This is a convenient place to check for query cancel --- no part of the
	 * planner goes very long without calling add_path().
	 */
	CHECK_FOR_INTERRUPTS();

	/* Pretend parameterized paths have no pathkeys, per comment above */
	new_path_pathkeys = new_path->param_info ? NIL : new_path->pathkeys;

	/*
	 * Loop to check proposed new path against old paths.  Note it is possible
	 * for more than one old path to be tossed out because new_path dominates
	 * it.
	 */
	foreach(p1, parent_rel->pathlist)
	{
		Path	 		   *old_path = (Path *) lfirst(p1);
		bool				remove_old = false; /* unless new proves superior */
		PathCostComparison  costcmp;
		PathKeysComparison  keyscmp;
		BMS_Comparison		outercmp;

		/*
		 * Do a fuzzy cost comparison with standard fuzziness limit.
		 */
		costcmp = compare_path_costs_fuzzily(new_path, old_path,
											 STD_FUZZ_FACTOR);

		/*
		 * If the two paths compare differently for startup and total cost,
		 * then we want to keep both, and we can skip comparing pathkeys and
		 * required_outer rels.  If they compare the same, proceed with the
		 * other comparisons.  Row count is checked last.  (We make the tests
		 * in this order because the cost comparison is most likely to turn
		 * out "different", and the pathkeys comparison next most likely.  As
		 * explained above, row count very seldom makes a difference, so even
		 * though it's cheap to compare there's not much point in checking it
		 * earlier.)
		 */
		if (costcmp != COSTS_DIFFERENT)
		{
			/* Similarly check to see if either dominates on pathkeys */
			List	   *old_path_pathkeys;

			old_path_pathkeys = old_path->param_info ? NIL : old_path->pathkeys;
			keyscmp = compare_pathkeys(new_path_pathkeys,
									   old_path_pathkeys);

			/*
			 * YB: If one is batched and the other isn't we consider
			 * the two parameterizations to be different.
			 */
			bool yb_does_new_path_req_batch =
				YB_PATH_NEEDS_BATCHED_RELS(new_path);

			bool yb_does_old_path_req_batch =
				YB_PATH_NEEDS_BATCHED_RELS(old_path);
			bool yb_has_diff_req_batch =
				(yb_does_new_path_req_batch != yb_does_old_path_req_batch);

			/*
			 * YB: If CBO is on, force batch-requiring plans to not be pruned
			 * early. Without this protection, they'd be pruned undesirably early
			 * as these batched paths will output more rows than their
			 * unbatched equivalents.
			 */
			bool yb_should_keep_all_batched_plans = yb_has_diff_req_batch &&
				yb_enable_base_scans_cost_model;

			if (yb_prefer_bnl &&
				IsA(old_path, NestPath) && IsA(new_path, NestPath))
			{
				/*
				 * YB: If yb_prefer_bnl is on and we are comparing a classic NL
				 * with its BNL equivalent, prefer the BNL and remove the NL.
				 * Assuming that if the costs are exactly equal, the two joins are
				 * NL/BNL equivalents of each other.
				 * 3fc44600e789aaad69df0d83a9d503c693d408d2 made sure that BNL/NL
				 * equivalents have the exact same cost if
				 * the CBO (yb_enable_base_scans_cost_model) is off.
				 * TODO: Remove this entire branch once CBO is GA and let BNL's
				 * naturally overcome NL's.
				 */
				bool yb_old_is_bnl =
					yb_is_nestloop_batched((NestPath *) old_path);
				bool yb_new_is_bnl =
					yb_is_nestloop_batched((NestPath *) new_path);

				Relids yb_old_outer_rels =
					((NestPath *) old_path)->jpath.outerjoinpath->parent->relids;
				Relids yb_new_outer_rels =
					((NestPath *) new_path)->jpath.outerjoinpath->parent->relids;
				bool is_different_nl_batchedness =
					yb_old_is_bnl != yb_new_is_bnl;
				if (yb_prefer_bnl && is_different_nl_batchedness &&
					bms_equal(yb_old_outer_rels, yb_new_outer_rels) &&
					compare_path_costs_fuzzily(new_path,
											   old_path,
											   1.0000000001) == COSTS_EQUAL)
				{
					if (yb_old_is_bnl)
						accept_new = false; /* Reject new classic NL. */
					else
						remove_old = true; /* Forget old classic NL. */
					break;
				}
			}

			if (keyscmp != PATHKEYS_DIFFERENT)
			{
				switch (costcmp)
				{
					case COSTS_EQUAL:
						outercmp = bms_subset_compare(PATH_REQ_OUTER(new_path),
													  PATH_REQ_OUTER(old_path));
						if (yb_should_keep_all_batched_plans)
						{
							outercmp = BMS_DIFFERENT;
							if (!yb_does_new_path_req_batch)
								insert_at = foreach_current_index(p1) + 1;
						}

						if (keyscmp == PATHKEYS_BETTER1)
						{
							if ((outercmp == BMS_EQUAL ||
								 outercmp == BMS_SUBSET1) &&
								new_path->rows <= old_path->rows &&
								new_path->parallel_safe >= old_path->parallel_safe)
								remove_old = true;	/* new dominates old */
						}
						else if (keyscmp == PATHKEYS_BETTER2)
						{
							if ((outercmp == BMS_EQUAL ||
								 outercmp == BMS_SUBSET2) &&
								new_path->rows >= old_path->rows &&
								new_path->parallel_safe <= old_path->parallel_safe)
								accept_new = false; /* old dominates new */
						}
						else	/* keyscmp == PATHKEYS_EQUAL */
						{
							if (outercmp == BMS_EQUAL)
							{
								/*
								 * Same pathkeys and outer rels, and fuzzily
								 * the same cost, so keep just one; to decide
								 * which, first check parallel-safety, then
								 * rows, then do a fuzzy cost comparison with
								 * very small fuzz limit.  (We used to do an
								 * exact cost comparison, but that results in
								 * annoying platform-specific plan variations
								 * due to roundoff in the cost estimates.)	If
								 * things are still tied, arbitrarily keep
								 * only the old path.  Notice that we will
								 * keep only the old path even if the
								 * less-fuzzy comparison decides the startup
								 * and total costs compare differently.
								 */
								if (new_path->parallel_safe >
									old_path->parallel_safe)
									remove_old = true;	/* new dominates old */
								else if (new_path->parallel_safe <
										 old_path->parallel_safe)
									accept_new = false; /* old dominates new */
								else if (new_path->rows < old_path->rows)
									remove_old = true;	/* new dominates old */
								else if (new_path->rows > old_path->rows)
									accept_new = false; /* old dominates new */
								else if (compare_path_costs_fuzzily(new_path,
																	old_path,
																	1.0000000001) ==
																	COSTS_EQUAL &&
																	yb_has_diff_req_batch)
								{
									/*
									 * YB: Keep both but put the batched path higher up
									 * in the queue.
									 */
									accept_new = true;
									if (yb_does_new_path_req_batch)
										insert_at = 0;
									else
										insert_at = foreach_current_index(p1) + 1;
								}
								else if (compare_path_costs_fuzzily(new_path,
																	old_path,
																	1.0000000001) == COSTS_BETTER1)
									remove_old = true;	/* new dominates old */
								else
									accept_new = false; /* old equals or
														 * dominates new */
							}
							else if (outercmp == BMS_SUBSET1 &&
									 new_path->rows <= old_path->rows &&
									 new_path->parallel_safe >= old_path->parallel_safe)
								remove_old = true;	/* new dominates old */
							else if (outercmp == BMS_SUBSET2 &&
									 new_path->rows >= old_path->rows &&
									 new_path->parallel_safe <= old_path->parallel_safe)
								accept_new = false; /* old dominates new */
							/* else different parameterizations, keep both */
						}
						break;
					case COSTS_BETTER1:
						if (keyscmp != PATHKEYS_BETTER2)
						{
							outercmp = bms_subset_compare(PATH_REQ_OUTER(new_path),
														  PATH_REQ_OUTER(old_path));

							if (yb_should_keep_all_batched_plans)
							{
								outercmp = BMS_DIFFERENT;
								if (!yb_does_new_path_req_batch)
									insert_at = foreach_current_index(p1) + 1;
							}

							if ((outercmp == BMS_EQUAL ||
								 outercmp == BMS_SUBSET1) &&
								new_path->rows <= old_path->rows &&
								new_path->parallel_safe >= old_path->parallel_safe)
								remove_old = true;	/* new dominates old */
						}
						break;
					case COSTS_BETTER2:
						if (keyscmp != PATHKEYS_BETTER1)
						{
							outercmp = bms_subset_compare(PATH_REQ_OUTER(new_path),
														  PATH_REQ_OUTER(old_path));

							if (yb_should_keep_all_batched_plans)
							{
								outercmp = BMS_DIFFERENT;
								if (!yb_does_new_path_req_batch)
									insert_at = foreach_current_index(p1) + 1;
							}

							if ((outercmp == BMS_EQUAL ||
								 outercmp == BMS_SUBSET2) &&
								new_path->rows >= old_path->rows &&
								new_path->parallel_safe <= old_path->parallel_safe)
								accept_new = false; /* old dominates new */
						}
						break;
					case COSTS_DIFFERENT:

						/*
						 * can't get here, but keep this case to keep compiler
						 * quiet
						 */
						break;
				}
			}
		}

		/*
		 * Remove current element from pathlist if dominated by new.
		 */
		if (remove_old)
		{
			parent_rel->pathlist = foreach_delete_current(parent_rel->pathlist,
														  p1);

			/*
			 * Delete the data pointed-to by the deleted cell, if possible
			 * YB: UpperUniquePath is also generated by build_index_paths
			 * and must be preserved.
			 */
			if (!IsA(old_path, IndexPath) && !IsA(old_path, UpperUniquePath))
				pfree(old_path);
		}
		else
		{
			if (new_path->total_cost >= old_path->total_cost)
				insert_at = foreach_current_index(p1) + 1;
		}

		/*
		 * If we found an old path that dominates new_path, we can quit
		 * scanning the pathlist; we will not add new_path, and we assume
		 * new_path cannot dominate any other elements of the pathlist.
		 */
		if (!accept_new)
			break;
	}

	if (accept_new)
	{
		/* Accept the new path: insert it at proper place in pathlist */
		parent_rel->pathlist =
			list_insert_nth(parent_rel->pathlist, insert_at, new_path);
	}
	else
	{
		/*
		 * Reject and recycle the new path
		 * YB: UpperUniquePath is also generated by build_index_paths and must
		 * be preserved.
		 */
		if (!IsA(new_path, IndexPath) && !IsA(new_path, UpperUniquePath))
			pfree(new_path);
	}
}

/*
 * add_path_precheck
 *	  Check whether a proposed new path could possibly get accepted.
 *	  We assume we know the path's pathkeys and parameterization accurately,
 *	  and have lower bounds for its costs.
 *
 * Note that we do not know the path's rowcount, since getting an estimate for
 * that is too expensive to do before prechecking.  We assume here that paths
 * of a superset parameterization will generate fewer rows; if that holds,
 * then paths with different parameterizations cannot dominate each other
 * and so we can simply ignore existing paths of another parameterization.
 * (In the infrequent cases where that rule of thumb fails, add_path will
 * get rid of the inferior path.)
 *
 * At the time this is called, we haven't actually built a Path structure,
 * so the required information has to be passed piecemeal.
 */
bool
add_path_precheck(RelOptInfo *parent_rel,
				  Cost startup_cost, Cost total_cost,
				  List *pathkeys, Relids required_outer)
{
	List	   *new_path_pathkeys;
	bool		consider_startup;
	ListCell   *p1;

	/* Pretend parameterized paths have no pathkeys, per add_path policy */
	new_path_pathkeys = required_outer ? NIL : pathkeys;

	/* Decide whether new path's startup cost is interesting */
	consider_startup = required_outer ? parent_rel->consider_param_startup : parent_rel->consider_startup;

	foreach(p1, parent_rel->pathlist)
	{
		Path	   *old_path = (Path *) lfirst(p1);
		PathKeysComparison keyscmp;

		/*
		 * We are looking for an old_path with the same parameterization (and
		 * by assumption the same rowcount) that dominates the new path on
		 * pathkeys as well as both cost metrics.  If we find one, we can
		 * reject the new path.
		 *
		 * Cost comparisons here should match compare_path_costs_fuzzily.
		 */
		if (total_cost > old_path->total_cost * STD_FUZZ_FACTOR)
		{
			/* new path can win on startup cost only if consider_startup */
			if (startup_cost > old_path->startup_cost * STD_FUZZ_FACTOR ||
				!consider_startup)
			{
				/* new path loses on cost, so check pathkeys... */
				List	   *old_path_pathkeys;

				old_path_pathkeys = old_path->param_info ? NIL : old_path->pathkeys;
				keyscmp = compare_pathkeys(new_path_pathkeys,
										   old_path_pathkeys);
				if (keyscmp == PATHKEYS_EQUAL ||
					keyscmp == PATHKEYS_BETTER2)
				{
					/* new path does not win on pathkeys... */
					if (bms_equal(required_outer, PATH_REQ_OUTER(old_path)))
					{
						/* Found an old path that dominates the new one */
						return false;
					}
				}
			}
		}
		else
		{
			/*
			 * Since the pathlist is sorted by total_cost, we can stop looking
			 * once we reach a path with a total_cost larger than the new
			 * path's.
			 */
			break;
		}
	}

	return true;
}

/*
 * add_partial_path
 *	  Like add_path, our goal here is to consider whether a path is worthy
 *	  of being kept around, but the considerations here are a bit different.
 *	  A partial path is one which can be executed in any number of workers in
 *	  parallel such that each worker will generate a subset of the path's
 *	  overall result.
 *
 *	  As in add_path, the partial_pathlist is kept sorted with the cheapest
 *	  total path in front.  This is depended on by multiple places, which
 *	  just take the front entry as the cheapest path without searching.
 *
 *	  We don't generate parameterized partial paths for several reasons.  Most
 *	  importantly, they're not safe to execute, because there's nothing to
 *	  make sure that a parallel scan within the parameterized portion of the
 *	  plan is running with the same value in every worker at the same time.
 *	  Fortunately, it seems unlikely to be worthwhile anyway, because having
 *	  each worker scan the entire outer relation and a subset of the inner
 *	  relation will generally be a terrible plan.  The inner (parameterized)
 *	  side of the plan will be small anyway.  There could be rare cases where
 *	  this wins big - e.g. if join order constraints put a 1-row relation on
 *	  the outer side of the topmost join with a parameterized plan on the inner
 *	  side - but we'll have to be content not to handle such cases until
 *	  somebody builds an executor infrastructure that can cope with them.
 *
 *	  Because we don't consider parameterized paths here, we also don't
 *	  need to consider the row counts as a measure of quality: every path will
 *	  produce the same number of rows.  Neither do we need to consider startup
 *	  costs: parallelism is only used for plans that will be run to completion.
 *	  Therefore, this routine is much simpler than add_path: it needs to
 *	  consider only pathkeys and total cost.
 *
 *	  As with add_path, we pfree paths that are found to be dominated by
 *	  another partial path; this requires that there be no other references to
 *	  such paths yet.  Hence, GatherPaths must not be created for a rel until
 *	  we're done creating all partial paths for it.  Unlike add_path, we don't
 *	  take an exception for IndexPaths as partial index paths won't be
 *	  referenced by partial BitmapHeapPaths.
 */
void
add_partial_path(RelOptInfo *parent_rel, Path *new_path)
{
	bool		accept_new = true;	/* unless we find a superior old path */
	int			insert_at = 0;	/* where to insert new item */
	ListCell   *p1;

	/* Check for query cancel. */
	CHECK_FOR_INTERRUPTS();

	/* Path to be added must be parallel safe. */
	Assert(new_path->parallel_safe);

	/* Relation should be OK for parallelism, too. */
	Assert(parent_rel->consider_parallel);

	/*
	 * As in add_path, throw out any paths which are dominated by the new
	 * path, but throw out the new path if some existing path dominates it.
	 */
	foreach(p1, parent_rel->partial_pathlist)
	{
		Path	   *old_path = (Path *) lfirst(p1);
		bool		remove_old = false; /* unless new proves superior */
		PathKeysComparison keyscmp;

		/* Compare pathkeys. */
		keyscmp = compare_pathkeys(new_path->pathkeys, old_path->pathkeys);

		/* Unless pathkeys are incompatible, keep just one of the two paths. */
		if (keyscmp != PATHKEYS_DIFFERENT)
		{
			if (new_path->total_cost > old_path->total_cost * STD_FUZZ_FACTOR)
			{
				/* New path costs more; keep it only if pathkeys are better. */
				if (keyscmp != PATHKEYS_BETTER1)
					accept_new = false;
			}
			else if (old_path->total_cost > new_path->total_cost
					 * STD_FUZZ_FACTOR)
			{
				/* Old path costs more; keep it only if pathkeys are better. */
				if (keyscmp != PATHKEYS_BETTER2)
					remove_old = true;
			}
			else if (keyscmp == PATHKEYS_BETTER1)
			{
				/* Costs are about the same, new path has better pathkeys. */
				remove_old = true;
			}
			else if (keyscmp == PATHKEYS_BETTER2)
			{
				/* Costs are about the same, old path has better pathkeys. */
				accept_new = false;
			}
			else if (old_path->total_cost > new_path->total_cost * 1.0000000001)
			{
				/* Pathkeys are the same, and the old path costs more. */
				remove_old = true;
			}
			else
			{
				/*
				 * Pathkeys are the same, and new path isn't materially
				 * cheaper.
				 */
				accept_new = false;
			}
		}

		/*
		 * Remove current element from partial_pathlist if dominated by new.
		 */
		if (remove_old)
		{
			parent_rel->partial_pathlist =
				foreach_delete_current(parent_rel->partial_pathlist, p1);
			pfree(old_path);
		}
		else
		{
			/* new belongs after this old path if it has cost >= old's */
			if (new_path->total_cost >= old_path->total_cost)
				insert_at = foreach_current_index(p1) + 1;
		}

		/*
		 * If we found an old path that dominates new_path, we can quit
		 * scanning the partial_pathlist; we will not add new_path, and we
		 * assume new_path cannot dominate any later path.
		 */
		if (!accept_new)
			break;
	}

	if (accept_new)
	{
		/* Accept the new path: insert it at proper place */
		parent_rel->partial_pathlist =
			list_insert_nth(parent_rel->partial_pathlist, insert_at, new_path);
	}
	else
	{
		/* Reject and recycle the new path */
		pfree(new_path);
	}
}

/*
 * add_partial_path_precheck
 *	  Check whether a proposed new partial path could possibly get accepted.
 *
 * Unlike add_path_precheck, we can ignore startup cost and parameterization,
 * since they don't matter for partial paths (see add_partial_path).  But
 * we do want to make sure we don't add a partial path if there's already
 * a complete path that dominates it, since in that case the proposed path
 * is surely a loser.
 */
bool
add_partial_path_precheck(RelOptInfo *parent_rel, Cost total_cost,
						  List *pathkeys)
{
	ListCell   *p1;

	/*
	 * Our goal here is twofold.  First, we want to find out whether this path
	 * is clearly inferior to some existing partial path.  If so, we want to
	 * reject it immediately.  Second, we want to find out whether this path
	 * is clearly superior to some existing partial path -- at least, modulo
	 * final cost computations.  If so, we definitely want to consider it.
	 *
	 * Unlike add_path(), we always compare pathkeys here.  This is because we
	 * expect partial_pathlist to be very short, and getting a definitive
	 * answer at this stage avoids the need to call add_path_precheck.
	 */
	foreach(p1, parent_rel->partial_pathlist)
	{
		Path	   *old_path = (Path *) lfirst(p1);
		PathKeysComparison keyscmp;

		keyscmp = compare_pathkeys(pathkeys, old_path->pathkeys);
		if (keyscmp != PATHKEYS_DIFFERENT)
		{
			if (total_cost > old_path->total_cost * STD_FUZZ_FACTOR &&
				keyscmp != PATHKEYS_BETTER1)
				return false;
			if (old_path->total_cost > total_cost * STD_FUZZ_FACTOR &&
				keyscmp != PATHKEYS_BETTER2)
				return true;
		}
	}

	/*
	 * This path is neither clearly inferior to an existing partial path nor
	 * clearly good enough that it might replace one.  Compare it to
	 * non-parallel plans.  If it loses even before accounting for the cost of
	 * the Gather node, we should definitely reject it.
	 *
	 * Note that we pass the total_cost to add_path_precheck twice.  This is
	 * because it's never advantageous to consider the startup cost of a
	 * partial path; the resulting plans, if run in parallel, will be run to
	 * completion.
	 */
	if (!add_path_precheck(parent_rel, total_cost, total_cost, pathkeys,
						   NULL))
		return false;

	return true;
}

/*
 * Propagate YugabyteDB fields between a parent and a single child.
 *
 * Path data generally flows upward, from children to parents. Therefore this
 * function is expected to simply copy information from children to parents for
 * future fields.
 */
static void
yb_propagate_fields(YbPathInfo *parent_fields, YbPathInfo *child_fields)
{
	if (!IsYugaByteEnabled())
		return;

	parent_fields->yb_uniqkeys = list_copy(child_fields->yb_uniqkeys);
}

/*
 * Propagate YugabyteDB fields between a parent and two children.
 * See comment for yb_propagate_fields.
 */
static void
yb_propagate_fields2(YbPathInfo *parent_fields, YbPathInfo *child1_fields,
					 YbPathInfo *child2_fields)
{
	if (!IsYugaByteEnabled())
		return;

	/*
	 * Compute uniqkeys for the parent only when uniqkeys are available on both
	 * the children. We do this because uniqkeys = NIL does not mean that the
	 * path has no uniqkeys. For example, consider a plain sequential scan such
	 * as that from 'SELECT * FROM t'. The path has no uniqkeys, but it is
	 * still distinct on its primary key. In other words, uniqkeys = NIL is a
	 * proxy for uniqkeys being indeterminate and we should avoid setting
	 * uniqkeys in such a case.
	 */
	if (child1_fields->yb_uniqkeys && child2_fields->yb_uniqkeys)
		parent_fields->yb_uniqkeys =
			list_concat(list_copy(child1_fields->yb_uniqkeys),
						list_copy(child2_fields->yb_uniqkeys));
}

/*
 * Propagate YugabyteDB fields between a parent and a list of children.
 * See comment for yb_propagate_fields.
 */
static void
yb_propagate_fields_list(YbPathInfo *parent_fields, List *child_paths)
{
	if (!IsYugaByteEnabled())
		return;

	/*
	 * TODO: Leaving this for a future change since computing uniqkeys optimally
	 * is involved. For example, we can set the parent's uniqkeys to those of
	 * the children. However, we need to ensure that there are no duplicate
	 * values across the child pathnodes before we can do that.
	 */
	parent_fields->yb_uniqkeys = NIL;
}

/*
 * Propagate YugabyteDB fields between a parent and a list of MinMaxAggregate
 * children.
 * See comment for yb_propagate_fields.
 */
static void
yb_propagate_mmagg_fields(YbPathInfo *parent_fields, List *mmaggregates)
{
	if (!IsYugaByteEnabled())
		return;
}


/*****************************************************************************
 *		PATH NODE CREATION ROUTINES
 *****************************************************************************/

/*
 * create_seqscan_path
 *	  Creates a path corresponding to a sequential scan, returning the
 *	  pathnode.
 */
Path *
create_seqscan_path(PlannerInfo *root, RelOptInfo *rel,
					Relids required_outer, int parallel_workers)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_SeqScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = (parallel_workers > 0);
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = parallel_workers;
	pathnode->pathkeys = NIL;	/* seqscan has unordered result */

	/*
	 * The ybcCostEstimate is used to cost a ForeignScan node on YB table,
	 * so use it here too, to get consistent results.
	 */
	if (rel->is_yb_relation)
	{
		if (yb_enable_base_scans_cost_model)
		{
			yb_cost_seqscan(pathnode, root, rel, pathnode->param_info);
		}
		else
		{
			ybcCostEstimate(rel, YBC_FULL_SCAN_SELECTIVITY,
							false, /* is_backward_scan */
							true, /* is_seq_scan */
							false, /* is_uncovered_idx_scan */
							&pathnode->startup_cost,
							&pathnode->total_cost,
							rel->reltablespace);
			pathnode->rows = rel->rows;
		}
	}
	else
		cost_seqscan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_samplescan_path
 *	  Creates a path node for a sampled table scan.
 */
Path *
create_samplescan_path(PlannerInfo *root, RelOptInfo *rel, Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_SampleScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = NIL;	/* samplescan has unordered result */

	cost_samplescan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_index_path
 *	  Creates a path node for an index scan.
 *
 * 'index' is a usable index.
 * 'indexclauses' is a list of IndexClause nodes representing clauses
 *			to be enforced as qual conditions in the scan.
 * 'yb_bitmap_idx_pushdowns' is a set of pushable clauses for a bitmap index scan.
 *    These are extracted during bitmap planning and allow pushdowns that are
 *    not possible to determine at a later stage.
 * 'indexorderbys' is a list of bare expressions (no RestrictInfos)
 *			to be used as index ordering operators in the scan.
 * 'indexorderbycols' is an integer list of index column numbers (zero based)
 *			the ordering operators can be used with.
 * 'pathkeys' describes the ordering of the path.
 * 'indexscandir' is ForwardScanDirection or BackwardScanDirection
 *			for an ordered index, or NoMovementScanDirection for
 *			an unordered index.
 * 'indexonly' is true if an index-only scan is wanted.
 * 'required_outer' is the set of outer relids for a parameterized path.
 * 'loop_count' is the number of repetitions of the indexscan to factor into
 *		estimates of caching behavior.
 * 'partial_path' is true if constructing a parallel index scan path.
 *
 * Returns the new path node.
 */
IndexPath *
create_index_path(PlannerInfo *root,
				  IndexOptInfo *index,
				  List *indexclauses,
				  List *yb_bitmap_idx_pushdowns,
				  List *indexorderbys,
				  List *indexorderbycols,
				  List *pathkeys,
				  ScanDirection indexscandir,
				  bool indexonly,
				  Relids required_outer,
				  double loop_count,
				  bool partial_path)
{
	IndexPath  *pathnode = makeNode(IndexPath);
	RelOptInfo *rel = index->rel;

	pathnode->path.pathtype = indexonly ? T_IndexOnlyScan : T_IndexScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.pathkeys = pathkeys;

	pathnode->indexinfo = index;
	pathnode->indexclauses = indexclauses;
	pathnode->yb_bitmap_idx_pushdowns = yb_bitmap_idx_pushdowns;
	pathnode->indexorderbys = indexorderbys;
	pathnode->indexorderbycols = indexorderbycols;
	pathnode->indexscandir = rel->is_yb_relation && pathkeys == NIL ?
		NoMovementScanDirection : indexscandir;

	if (IsYugaByteEnabled() &&
		yb_enable_base_scans_cost_model &&
		index->relam == LSM_AM_OID)
	{
		yb_cost_index(pathnode, root, loop_count, partial_path);
	}
	else
	{
		cost_index(pathnode, root, loop_count, partial_path);
	}


	return pathnode;
}

/*
 * create_bitmap_heap_path
 *	  Creates a path node for a bitmap scan.
 *
 * 'bitmapqual' is a tree of IndexPath, BitmapAndPath, and BitmapOrPath nodes.
 * 'required_outer' is the set of outer relids for a parameterized path.
 * 'loop_count' is the number of repetitions of the indexscan to factor into
 *		estimates of caching behavior.
 *
 * loop_count should match the value used when creating the component
 * IndexPaths.
 */
BitmapHeapPath *
create_bitmap_heap_path(PlannerInfo *root,
						RelOptInfo *rel,
						Path *bitmapqual,
						Relids required_outer,
						double loop_count,
						int parallel_degree)
{
	BitmapHeapPath *pathnode = makeNode(BitmapHeapPath);

	pathnode->path.pathtype = T_BitmapHeapScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = (parallel_degree > 0);
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = parallel_degree;
	pathnode->path.pathkeys = NIL;	/* always unordered */

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&bitmapqual->yb_path_info);

	pathnode->bitmapqual = bitmapqual;

	cost_bitmap_heap_scan(&pathnode->path, root, rel,
						  pathnode->path.param_info,
						  bitmapqual, loop_count);

	return pathnode;
}

/*
 * create_yb_bitmap_table_path
 *	  Creates a path node for a YB bitmap scan.
 *
 * 'bitmapqual' is a tree of IndexPath, BitmapAndPath, and BitmapOrPath nodes.
 * 'required_outer' is the set of outer relids for a parameterized path.
 * 'loop_count' is the number of repetitions of the indexscan to factor into
 *		estimates of caching behavior.
 *
 * loop_count should match the value used when creating the component
 * IndexPaths.
 */
YbBitmapTablePath *
create_yb_bitmap_table_path(PlannerInfo *root,
						RelOptInfo *rel,
						Path *bitmapqual,
						Relids required_outer,
						double loop_count,
						int parallel_degree)
{
	YbBitmapTablePath *pathnode = makeNode(YbBitmapTablePath);

	pathnode->path.pathtype = T_YbBitmapTableScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = parallel_degree > 0 ? true : false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = parallel_degree;
	pathnode->path.pathkeys = NIL;	/* always unordered */

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&bitmapqual->yb_path_info);

	pathnode->bitmapqual = bitmapqual;

	if (yb_enable_base_scans_cost_model)
		yb_cost_bitmap_table_scan(&pathnode->path, root, rel,
								  pathnode->path.param_info,
								  bitmapqual, loop_count);
	else
		cost_bitmap_heap_scan(&pathnode->path, root, rel,
							  pathnode->path.param_info,
							  bitmapqual, loop_count);

	return pathnode;
}

/*
 * create_bitmap_and_path
 *	  Creates a path node representing a BitmapAnd.
 */
BitmapAndPath *
create_bitmap_and_path(PlannerInfo *root,
					   RelOptInfo *rel,
					   List *bitmapquals)
{
	BitmapAndPath *pathnode = makeNode(BitmapAndPath);
	Relids		required_outer = NULL;
	ListCell   *lc;

	pathnode->path.pathtype = T_BitmapAnd;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;

	/*
	 * Identify the required outer rels as the union of what the child paths
	 * depend on.  (Alternatively, we could insist that the caller pass this
	 * in, but it's more convenient and reliable to compute it here.)
	 */
	foreach(lc, bitmapquals)
	{
		Path	   *bitmapqual = (Path *) lfirst(lc);

		required_outer = bms_add_members(required_outer,
										 PATH_REQ_OUTER(bitmapqual));
	}
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);

	/*
	 * Currently, a BitmapHeapPath, BitmapAndPath, or BitmapOrPath will be
	 * parallel-safe if and only if rel->consider_parallel is set.  So, we can
	 * set the flag for this path based only on the relation-level flag,
	 * without actually iterating over the list of children.
	 */
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;

	pathnode->path.pathkeys = NIL;	/* always unordered */

	yb_propagate_fields_list(&pathnode->path.yb_path_info, bitmapquals);

	pathnode->bitmapquals = bitmapquals;

	/* this sets bitmapselectivity as well as the regular cost fields: */
	if (IsYugaByteEnabled() && yb_enable_base_scans_cost_model &&
		pathnode->path.parent->is_yb_relation)
		yb_cost_bitmap_and_node(pathnode, root);
	else
		cost_bitmap_and_node(pathnode, root);

	return pathnode;
}

/*
 * create_bitmap_or_path
 *	  Creates a path node representing a BitmapOr.
 */
BitmapOrPath *
create_bitmap_or_path(PlannerInfo *root,
					  RelOptInfo *rel,
					  List *bitmapquals)
{
	BitmapOrPath *pathnode = makeNode(BitmapOrPath);
	Relids		required_outer = NULL;
	ListCell   *lc;

	pathnode->path.pathtype = T_BitmapOr;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;

	/*
	 * Identify the required outer rels as the union of what the child paths
	 * depend on.  (Alternatively, we could insist that the caller pass this
	 * in, but it's more convenient and reliable to compute it here.)
	 */
	foreach(lc, bitmapquals)
	{
		Path	   *bitmapqual = (Path *) lfirst(lc);

		required_outer = bms_add_members(required_outer,
										 PATH_REQ_OUTER(bitmapqual));
	}
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);

	/*
	 * Currently, a BitmapHeapPath, BitmapAndPath, or BitmapOrPath will be
	 * parallel-safe if and only if rel->consider_parallel is set.  So, we can
	 * set the flag for this path based only on the relation-level flag,
	 * without actually iterating over the list of children.
	 */
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;

	pathnode->path.pathkeys = NIL;	/* always unordered */

	yb_propagate_fields_list(&pathnode->path.yb_path_info, bitmapquals);

	pathnode->bitmapquals = bitmapquals;

	/* this sets bitmapselectivity as well as the regular cost fields: */
	if (IsYugaByteEnabled() && yb_enable_base_scans_cost_model &&
		pathnode->path.parent->is_yb_relation)
		yb_cost_bitmap_or_node(pathnode, root);
	else
		cost_bitmap_or_node(pathnode, root);

	return pathnode;
}

/*
 * create_tidscan_path
 *	  Creates a path corresponding to a scan by TID, returning the pathnode.
 */
TidPath *
create_tidscan_path(PlannerInfo *root, RelOptInfo *rel, List *tidquals,
					Relids required_outer)
{
	TidPath    *pathnode = makeNode(TidPath);

	pathnode->path.pathtype = T_TidScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.pathkeys = NIL;	/* always unordered */

	pathnode->tidquals = tidquals;

	cost_tidscan(&pathnode->path, root, rel, tidquals,
				 pathnode->path.param_info);

	return pathnode;
}

/*
 * create_tidrangescan_path
 *	  Creates a path corresponding to a scan by a range of TIDs, returning
 *	  the pathnode.
 */
TidRangePath *
create_tidrangescan_path(PlannerInfo *root, RelOptInfo *rel,
						 List *tidrangequals, Relids required_outer)
{
	TidRangePath *pathnode = makeNode(TidRangePath);

	pathnode->path.pathtype = T_TidRangeScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.pathkeys = NIL;	/* always unordered */

	pathnode->tidrangequals = tidrangequals;

	cost_tidrangescan(&pathnode->path, root, rel, tidrangequals,
					  pathnode->path.param_info);

	return pathnode;
}

/*
 * create_append_path
 *	  Creates a path corresponding to an Append plan, returning the
 *	  pathnode.
 *
 * Note that we must handle subpaths = NIL, representing a dummy access path.
 * Also, there are callers that pass root = NULL.
 */
AppendPath *
create_append_path(PlannerInfo *root,
				   RelOptInfo *rel,
				   List *subpaths, List *partial_subpaths,
				   List *pathkeys, Relids required_outer,
				   int parallel_workers, bool parallel_aware,
				   double rows)
{
	AppendPath *pathnode = makeNode(AppendPath);
	ListCell   *l;

	Assert(!parallel_aware || parallel_workers > 0);

	pathnode->path.pathtype = T_Append;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;

	/*
	 * When generating an Append path for a partitioned table, there may be
	 * parameterized quals that are useful for run-time pruning.  Hence,
	 * compute path.param_info the same way as for any other baserel, so that
	 * such quals will be available for make_partition_pruneinfo().  (This
	 * would not work right for a non-baserel, ie a scan on a non-leaf child
	 * partition, and it's not necessary anyway in that case.  Must skip it if
	 * we don't have "root", too.)
	 */
	if (root && rel->reloptkind == RELOPT_BASEREL && IS_PARTITIONED_REL(rel))
	{
		if (subpaths)
		{
			/* YB: Accumulate batching info from subpaths for this "baserel". */
			Assert(yb_has_same_batching_reqs(subpaths));

			root->yb_cur_batched_relids =
				YB_PATH_REQ_OUTER_BATCHED((Path *) linitial(subpaths));
		}
		pathnode->path.param_info = get_baserel_parampathinfo(root,
															  rel,
															  required_outer);
		root->yb_cur_batched_relids = NULL;
	}
	else
		pathnode->path.param_info = get_appendrel_parampathinfo(rel,
																required_outer);

	pathnode->path.parallel_aware = parallel_aware;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = parallel_workers;
	pathnode->path.pathkeys = pathkeys;

	/*
	 * For parallel append, non-partial paths are sorted by descending total
	 * costs. That way, the total time to finish all non-partial paths is
	 * minimized.  Also, the partial paths are sorted by descending startup
	 * costs.  There may be some paths that require to do startup work by a
	 * single worker.  In such case, it's better for workers to choose the
	 * expensive ones first, whereas the leader should choose the cheapest
	 * startup plan.
	 */
	if (pathnode->path.parallel_aware)
	{
		/*
		 * We mustn't fiddle with the order of subpaths when the Append has
		 * pathkeys.  The order they're listed in is critical to keeping the
		 * pathkeys valid.
		 */
		Assert(pathkeys == NIL);

		list_sort(subpaths, append_total_cost_compare);
		list_sort(partial_subpaths, append_startup_cost_compare);
	}
	pathnode->first_partial_path = list_length(subpaths);
	pathnode->subpaths = list_concat(subpaths, partial_subpaths);

	/*
	 * Apply query-wide LIMIT if known and path is for sole base relation.
	 * (Handling this at this low level is a bit klugy.)
	 */
	if (root != NULL && bms_equal(rel->relids, root->all_baserels))
		pathnode->limit_tuples = root->limit_tuples;
	else
		pathnode->limit_tuples = -1.0;

	foreach(l, pathnode->subpaths)
	{
		Path	   *subpath = (Path *) lfirst(l);

		pathnode->path.parallel_safe = pathnode->path.parallel_safe &&
			subpath->parallel_safe;

		/* All child paths must have same parameterization */
		Assert(bms_equal(PATH_REQ_OUTER(subpath), required_outer));
	}

	yb_propagate_fields_list(&pathnode->path.yb_path_info,
							 pathnode->subpaths);

	Assert(!parallel_aware || pathnode->path.parallel_safe);

	/*
	 * If there's exactly one child path, the Append is a no-op and will be
	 * discarded later (in setrefs.c); therefore, we can inherit the child's
	 * size and cost, as well as its pathkeys if any (overriding whatever the
	 * caller might've said).  Otherwise, we must do the normal costsize
	 * calculation.
	 */
	if (list_length(pathnode->subpaths) == 1)
	{
		Path	   *child = (Path *) linitial(pathnode->subpaths);

		pathnode->path.rows = child->rows;
		pathnode->path.startup_cost = child->startup_cost;
		pathnode->path.total_cost = child->total_cost;
		pathnode->path.pathkeys = child->pathkeys;
	}
	else
		cost_append(pathnode);

	/* If the caller provided a row estimate, override the computed value. */
	if (rows >= 0)
		pathnode->path.rows = rows;

	return pathnode;
}

/*
 * append_total_cost_compare
 *	  list_sort comparator for sorting append child paths
 *	  by total_cost descending
 *
 * For equal total costs, we fall back to comparing startup costs; if those
 * are equal too, break ties using bms_compare on the paths' relids.
 * (This is to avoid getting unpredictable results from list_sort.)
 */
static int
append_total_cost_compare(const ListCell *a, const ListCell *b)
{
	Path	   *path1 = (Path *) lfirst(a);
	Path	   *path2 = (Path *) lfirst(b);
	int			cmp;

	cmp = compare_path_costs(path1, path2, TOTAL_COST);
	if (cmp != 0)
		return -cmp;
	return bms_compare(path1->parent->relids, path2->parent->relids);
}

/*
 * append_startup_cost_compare
 *	  list_sort comparator for sorting append child paths
 *	  by startup_cost descending
 *
 * For equal startup costs, we fall back to comparing total costs; if those
 * are equal too, break ties using bms_compare on the paths' relids.
 * (This is to avoid getting unpredictable results from list_sort.)
 */
static int
append_startup_cost_compare(const ListCell *a, const ListCell *b)
{
	Path	   *path1 = (Path *) lfirst(a);
	Path	   *path2 = (Path *) lfirst(b);
	int			cmp;

	cmp = compare_path_costs(path1, path2, STARTUP_COST);
	if (cmp != 0)
		return -cmp;
	return bms_compare(path1->parent->relids, path2->parent->relids);
}

/*
 * create_merge_append_path
 *	  Creates a path corresponding to a MergeAppend plan, returning the
 *	  pathnode.
 */
MergeAppendPath *
create_merge_append_path(PlannerInfo *root,
						 RelOptInfo *rel,
						 List *subpaths,
						 List *pathkeys,
						 Relids required_outer)
{
	MergeAppendPath *pathnode = makeNode(MergeAppendPath);
	Cost		input_startup_cost;
	Cost		input_total_cost;
	ListCell   *l;

	pathnode->path.pathtype = T_MergeAppend;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = get_appendrel_parampathinfo(rel,
															required_outer);
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.pathkeys = pathkeys;
	yb_propagate_fields_list(&pathnode->path.yb_path_info, subpaths);
	pathnode->subpaths = subpaths;

	/*
	 * Apply query-wide LIMIT if known and path is for sole base relation.
	 * (Handling this at this low level is a bit klugy.)
	 */
	if (bms_equal(rel->relids, root->all_baserels))
		pathnode->limit_tuples = root->limit_tuples;
	else
		pathnode->limit_tuples = -1.0;

	/*
	 * Add up the sizes and costs of the input paths.
	 */
	pathnode->path.rows = 0;
	input_startup_cost = 0;
	input_total_cost = 0;
	foreach(l, subpaths)
	{
		Path	   *subpath = (Path *) lfirst(l);

		pathnode->path.rows += subpath->rows;
		pathnode->path.parallel_safe = pathnode->path.parallel_safe &&
			subpath->parallel_safe;

		if (pathkeys_contained_in(pathkeys, subpath->pathkeys))
		{
			/* Subpath is adequately ordered, we won't need to sort it */
			input_startup_cost += subpath->startup_cost;
			input_total_cost += subpath->total_cost;
		}
		else
		{
			/* We'll need to insert a Sort node, so include cost for that */
			Path		sort_path;	/* dummy for result of cost_sort */

			cost_sort(&sort_path,
					  root,
					  pathkeys,
					  subpath->total_cost,
					  subpath->parent->tuples,
					  subpath->pathtarget->width,
					  0.0,
					  work_mem,
					  pathnode->limit_tuples);
			input_startup_cost += sort_path.startup_cost;
			input_total_cost += sort_path.total_cost;
		}

		/* All child paths must have same parameterization */
		Assert(bms_equal(PATH_REQ_OUTER(subpath), required_outer));
	}

	/*
	 * Now we can compute total costs of the MergeAppend.  If there's exactly
	 * one child path, the MergeAppend is a no-op and will be discarded later
	 * (in setrefs.c); otherwise we do the normal cost calculation.
	 */
	if (list_length(subpaths) == 1)
	{
		pathnode->path.startup_cost = input_startup_cost;
		pathnode->path.total_cost = input_total_cost;
	}
	else
		cost_merge_append(&pathnode->path, root,
						  pathkeys, list_length(subpaths),
						  input_startup_cost, input_total_cost,
						  pathnode->path.rows);

	return pathnode;
}

/*
 * create_group_result_path
 *	  Creates a path representing a Result-and-nothing-else plan.
 *
 * This is only used for degenerate grouping cases, in which we know we
 * need to produce one result row, possibly filtered by a HAVING qual.
 */
GroupResultPath *
create_group_result_path(PlannerInfo *root, RelOptInfo *rel,
						 PathTarget *target, List *havingqual)
{
	GroupResultPath *pathnode = makeNode(GroupResultPath);

	pathnode->path.pathtype = T_Result;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	pathnode->path.param_info = NULL;	/* there are no other rels... */
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.pathkeys = NIL;
	pathnode->quals = havingqual;

	/*
	 * We can't quite use cost_resultscan() because the quals we want to
	 * account for are not baserestrict quals of the rel.  Might as well just
	 * hack it here.
	 */
	pathnode->path.rows = 1;
	pathnode->path.startup_cost = target->cost.startup;
	pathnode->path.total_cost = target->cost.startup +
		cpu_tuple_cost + target->cost.per_tuple;

	/*
	 * Add cost of qual, if any --- but we ignore its selectivity, since our
	 * rowcount estimate should be 1 no matter what the qual is.
	 */
	if (havingqual)
	{
		QualCost	qual_cost;

		cost_qual_eval(&qual_cost, havingqual, root);
		/* havingqual is evaluated once at startup */
		pathnode->path.startup_cost += qual_cost.startup + qual_cost.per_tuple;
		pathnode->path.total_cost += qual_cost.startup + qual_cost.per_tuple;
	}

	return pathnode;
}

/*
 * create_material_path
 *	  Creates a path corresponding to a Material plan, returning the
 *	  pathnode.
 */
MaterialPath *
create_material_path(RelOptInfo *rel, Path *subpath)
{
	MaterialPath *pathnode = makeNode(MaterialPath);

	Assert(subpath->parent == rel);

	pathnode->path.pathtype = T_Material;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = subpath->param_info;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	pathnode->path.pathkeys = subpath->pathkeys;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;

	cost_material(&pathnode->path,
				  subpath->startup_cost,
				  subpath->total_cost,
				  subpath->rows,
				  subpath->pathtarget->width);

	return pathnode;
}

/*
 * create_memoize_path
 *	  Creates a path corresponding to a Memoize plan, returning the pathnode.
 */
MemoizePath *
create_memoize_path(PlannerInfo *root, RelOptInfo *rel, Path *subpath,
					List *param_exprs, List *hash_operators,
					bool singlerow, bool binary_mode, double calls)
{
	MemoizePath *pathnode = makeNode(MemoizePath);

	Assert(subpath->parent == rel);

	pathnode->path.pathtype = T_Memoize;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = subpath->param_info;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	pathnode->path.pathkeys = subpath->pathkeys;

	pathnode->subpath = subpath;
	pathnode->hash_operators = hash_operators;
	pathnode->param_exprs = param_exprs;
	pathnode->singlerow = singlerow;
	pathnode->binary_mode = binary_mode;
	pathnode->calls = calls;

	/*
	 * For now we set est_entries to 0.  cost_memoize_rescan() does all the
	 * hard work to determine how many cache entries there are likely to be,
	 * so it seems best to leave it up to that function to fill this field in.
	 * If left at 0, the executor will make a guess at a good value.
	 */
	pathnode->est_entries = 0;

	/*
	 * Add a small additional charge for caching the first entry.  All the
	 * harder calculations for rescans are performed in cost_memoize_rescan().
	 */
	pathnode->path.startup_cost = subpath->startup_cost + cpu_tuple_cost;
	pathnode->path.total_cost = subpath->total_cost + cpu_tuple_cost;
	pathnode->path.rows = subpath->rows;

	return pathnode;
}

/*
 * create_unique_path
 *	  Creates a path representing elimination of distinct rows from the
 *	  input data.  Distinct-ness is defined according to the needs of the
 *	  semijoin represented by sjinfo.  If it is not possible to identify
 *	  how to make the data unique, NULL is returned.
 *
 * If used at all, this is likely to be called repeatedly on the same rel;
 * and the input subpath should always be the same (the cheapest_total path
 * for the rel).  So we cache the result.
 */
UniquePath *
create_unique_path(PlannerInfo *root, RelOptInfo *rel, Path *subpath,
				   SpecialJoinInfo *sjinfo)
{
	UniquePath *pathnode;
	Path		sort_path;		/* dummy for result of cost_sort */
	Path		agg_path;		/* dummy for result of cost_agg */
	MemoryContext oldcontext;
	int			numCols;

	/* Caller made a mistake if subpath isn't cheapest_total ... */
	Assert(subpath == rel->cheapest_total_path);
	Assert(subpath->parent == rel);
	/* ... or if SpecialJoinInfo is the wrong one */
	Assert(sjinfo->jointype == JOIN_SEMI);
	Assert(bms_equal(rel->relids, sjinfo->syn_righthand));

	/* If result already cached, return it */
	if (rel->cheapest_unique_path)
		return (UniquePath *) rel->cheapest_unique_path;

	/* If it's not possible to unique-ify, return NULL */
	if (!(sjinfo->semi_can_btree || sjinfo->semi_can_hash))
		return NULL;

	/*
	 * When called during GEQO join planning, we are in a short-lived memory
	 * context.  We must make sure that the path and any subsidiary data
	 * structures created for a baserel survive the GEQO cycle, else the
	 * baserel is trashed for future GEQO cycles.  On the other hand, when we
	 * are creating those for a joinrel during GEQO, we don't want them to
	 * clutter the main planning context.  Upshot is that the best solution is
	 * to explicitly allocate memory in the same context the given RelOptInfo
	 * is in.
	 */
	oldcontext = MemoryContextSwitchTo(GetMemoryChunkContext(rel));

	pathnode = makeNode(UniquePath);

	pathnode->path.pathtype = T_Unique;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = subpath->param_info;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;

	/*
	 * Assume the output is unsorted, since we don't necessarily have pathkeys
	 * to represent it.  (This might get overridden below.)
	 */
	pathnode->path.pathkeys = NIL;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;
	pathnode->in_operators = sjinfo->semi_operators;
	pathnode->uniq_exprs = sjinfo->semi_rhs_exprs;

	/*
	 * If the input is a relation and it has a unique index that proves the
	 * semi_rhs_exprs are unique, then we don't need to do anything.  Note
	 * that relation_has_unique_index_for automatically considers restriction
	 * clauses for the rel, as well.
	 */
	if (rel->rtekind == RTE_RELATION && sjinfo->semi_can_btree &&
		relation_has_unique_index_for(root, rel, NIL,
									  sjinfo->semi_rhs_exprs,
									  sjinfo->semi_operators))
	{
		pathnode->umethod = UNIQUE_PATH_NOOP;
		pathnode->path.rows = rel->rows;
		pathnode->path.startup_cost = subpath->startup_cost;
		pathnode->path.total_cost = subpath->total_cost;
		pathnode->path.pathkeys = subpath->pathkeys;

		rel->cheapest_unique_path = (Path *) pathnode;

		MemoryContextSwitchTo(oldcontext);

		return pathnode;
	}

	/*
	 * If the input is a subquery whose output must be unique already, then we
	 * don't need to do anything.  The test for uniqueness has to consider
	 * exactly which columns we are extracting; for example "SELECT DISTINCT
	 * x,y" doesn't guarantee that x alone is distinct. So we cannot check for
	 * this optimization unless semi_rhs_exprs consists only of simple Vars
	 * referencing subquery outputs.  (Possibly we could do something with
	 * expressions in the subquery outputs, too, but for now keep it simple.)
	 */
	if (rel->rtekind == RTE_SUBQUERY)
	{
		RangeTblEntry *rte = planner_rt_fetch(rel->relid, root);

		if (query_supports_distinctness(rte->subquery))
		{
			List	   *sub_tlist_colnos;

			sub_tlist_colnos = translate_sub_tlist(sjinfo->semi_rhs_exprs,
												   rel->relid);

			if (sub_tlist_colnos &&
				query_is_distinct_for(rte->subquery,
									  sub_tlist_colnos,
									  sjinfo->semi_operators))
			{
				pathnode->umethod = UNIQUE_PATH_NOOP;
				pathnode->path.rows = rel->rows;
				pathnode->path.startup_cost = subpath->startup_cost;
				pathnode->path.total_cost = subpath->total_cost;
				pathnode->path.pathkeys = subpath->pathkeys;

				rel->cheapest_unique_path = (Path *) pathnode;

				MemoryContextSwitchTo(oldcontext);

				return pathnode;
			}
		}
	}

	/* Estimate number of output rows */
	pathnode->path.rows = estimate_num_groups(root,
											  sjinfo->semi_rhs_exprs,
											  rel->rows,
											  NULL,
											  NULL);
	numCols = list_length(sjinfo->semi_rhs_exprs);

	if (sjinfo->semi_can_btree)
	{
		/*
		 * Estimate cost for sort+unique implementation
		 */
		cost_sort(&sort_path, root, NIL,
				  subpath->total_cost,
				  rel->rows,
				  subpath->pathtarget->width,
				  0.0,
				  work_mem,
				  -1.0);

		/*
		 * Charge one cpu_operator_cost per comparison per input tuple. We
		 * assume all columns get compared at most of the tuples. (XXX
		 * probably this is an overestimate.)  This should agree with
		 * create_upper_unique_path.
		 */
		sort_path.total_cost += cpu_operator_cost * rel->rows * numCols;
	}

	if (sjinfo->semi_can_hash)
	{
		/*
		 * Estimate the overhead per hashtable entry at 64 bytes (same as in
		 * planner.c).
		 */
		int			hashentrysize = subpath->pathtarget->width + 64;

		if (hashentrysize * pathnode->path.rows > get_hash_memory_limit())
		{
			/*
			 * We should not try to hash.  Hack the SpecialJoinInfo to
			 * remember this, in case we come through here again.
			 */
			sjinfo->semi_can_hash = false;
		}
		else
			cost_agg(&agg_path, root,
					 AGG_HASHED, NULL,
					 numCols, pathnode->path.rows,
					 NIL,
					 subpath->startup_cost,
					 subpath->total_cost,
					 rel->rows,
					 subpath->pathtarget->width);
	}

	if (sjinfo->semi_can_btree && sjinfo->semi_can_hash)
	{
		if (agg_path.total_cost < sort_path.total_cost)
			pathnode->umethod = UNIQUE_PATH_HASH;
		else
			pathnode->umethod = UNIQUE_PATH_SORT;
	}
	else if (sjinfo->semi_can_btree)
		pathnode->umethod = UNIQUE_PATH_SORT;
	else if (sjinfo->semi_can_hash)
		pathnode->umethod = UNIQUE_PATH_HASH;
	else
	{
		/* we can get here only if we abandoned hashing above */
		MemoryContextSwitchTo(oldcontext);
		return NULL;
	}

	if (pathnode->umethod == UNIQUE_PATH_HASH)
	{
		pathnode->path.startup_cost = agg_path.startup_cost;
		pathnode->path.total_cost = agg_path.total_cost;
	}
	else
	{
		pathnode->path.startup_cost = sort_path.startup_cost;
		pathnode->path.total_cost = sort_path.total_cost;
	}

	rel->cheapest_unique_path = (Path *) pathnode;

	MemoryContextSwitchTo(oldcontext);

	return pathnode;
}

/*
 * create_gather_merge_path
 *
 *	  Creates a path corresponding to a gather merge scan, returning
 *	  the pathnode.
 */
GatherMergePath *
create_gather_merge_path(PlannerInfo *root, RelOptInfo *rel, Path *subpath,
						 PathTarget *target, List *pathkeys,
						 Relids required_outer, double *rows)
{
	GatherMergePath *pathnode = makeNode(GatherMergePath);
	Cost		input_startup_cost = 0;
	Cost		input_total_cost = 0;

	Assert(subpath->parallel_safe);
	Assert(pathkeys);

	pathnode->path.pathtype = T_GatherMerge;
	pathnode->path.parent = rel;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = false;

	pathnode->subpath = subpath;
	pathnode->num_workers = subpath->parallel_workers;
	pathnode->path.pathkeys = pathkeys;
	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);
	/* YB: Sub paths may contain duplicate rows. */
	pathnode->path.yb_path_info.yb_uniqkeys = NIL;
	pathnode->path.pathtarget = target ? target : rel->reltarget;
	pathnode->path.rows += subpath->rows;

	if (pathkeys_contained_in(pathkeys, subpath->pathkeys))
	{
		/* Subpath is adequately ordered, we won't need to sort it */
		input_startup_cost += subpath->startup_cost;
		input_total_cost += subpath->total_cost;
	}
	else
	{
		/* We'll need to insert a Sort node, so include cost for that */
		Path		sort_path;	/* dummy for result of cost_sort */

		cost_sort(&sort_path,
				  root,
				  pathkeys,
				  subpath->total_cost,
				  subpath->rows,
				  subpath->pathtarget->width,
				  0.0,
				  work_mem,
				  -1);
		input_startup_cost += sort_path.startup_cost;
		input_total_cost += sort_path.total_cost;
	}

	cost_gather_merge(pathnode, root, rel, pathnode->path.param_info,
					  input_startup_cost, input_total_cost, rows);

	return pathnode;
}

/*
 * translate_sub_tlist - get subquery column numbers represented by tlist
 *
 * The given targetlist usually contains only Vars referencing the given relid.
 * Extract their varattnos (ie, the column numbers of the subquery) and return
 * as an integer List.
 *
 * If any of the tlist items is not a simple Var, we cannot determine whether
 * the subquery's uniqueness condition (if any) matches ours, so punt and
 * return NIL.
 */
static List *
translate_sub_tlist(List *tlist, int relid)
{
	List	   *result = NIL;
	ListCell   *l;

	foreach(l, tlist)
	{
		Var		   *var = (Var *) lfirst(l);

		if (!var || !IsA(var, Var) ||
			var->varno != relid)
			return NIL;			/* punt */

		result = lappend_int(result, var->varattno);
	}
	return result;
}

/*
 * create_gather_path
 *	  Creates a path corresponding to a gather scan, returning the
 *	  pathnode.
 *
 * 'rows' may optionally be set to override row estimates from other sources.
 */
GatherPath *
create_gather_path(PlannerInfo *root, RelOptInfo *rel, Path *subpath,
				   PathTarget *target, Relids required_outer, double *rows)
{
	GatherPath *pathnode = makeNode(GatherPath);

	Assert(subpath->parallel_safe);

	pathnode->path.pathtype = T_Gather;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = false;
	pathnode->path.parallel_workers = 0;
	pathnode->path.pathkeys = NIL;	/* Gather has unordered result */

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);
	/* YB: There may be duplicate rows across sub paths. */
	pathnode->path.yb_path_info.yb_uniqkeys = NIL;

	pathnode->subpath = subpath;
	pathnode->num_workers = subpath->parallel_workers;
	pathnode->single_copy = false;

	if (pathnode->num_workers == 0)
	{
		pathnode->path.pathkeys = subpath->pathkeys;
		pathnode->num_workers = 1;
		pathnode->single_copy = true;
	}

	cost_gather(pathnode, root, rel, pathnode->path.param_info, rows);

	return pathnode;
}

/*
 * create_subqueryscan_path
 *	  Creates a path corresponding to a scan of a subquery,
 *	  returning the pathnode.
 */
SubqueryScanPath *
create_subqueryscan_path(PlannerInfo *root, RelOptInfo *rel, Path *subpath,
						 List *pathkeys, Relids required_outer)
{
	SubqueryScanPath *pathnode = makeNode(SubqueryScanPath);

	pathnode->path.pathtype = T_SubqueryScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = rel->reltarget;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	pathnode->path.pathkeys = pathkeys;
	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);
	pathnode->subpath = subpath;

	cost_subqueryscan(pathnode, root, rel, pathnode->path.param_info);

	return pathnode;
}

/*
 * create_functionscan_path
 *	  Creates a path corresponding to a sequential scan of a function,
 *	  returning the pathnode.
 */
Path *
create_functionscan_path(PlannerInfo *root, RelOptInfo *rel,
						 List *pathkeys, Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_FunctionScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = pathkeys;

	cost_functionscan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_tablefuncscan_path
 *	  Creates a path corresponding to a sequential scan of a table function,
 *	  returning the pathnode.
 */
Path *
create_tablefuncscan_path(PlannerInfo *root, RelOptInfo *rel,
						  Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_TableFuncScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = NIL;	/* result is always unordered */

	cost_tablefuncscan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_valuesscan_path
 *	  Creates a path corresponding to a scan of a VALUES list,
 *	  returning the pathnode.
 */
Path *
create_valuesscan_path(PlannerInfo *root, RelOptInfo *rel,
					   Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_ValuesScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = NIL;	/* result is always unordered */

	cost_valuesscan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_ctescan_path
 *	  Creates a path corresponding to a scan of a non-self-reference CTE,
 *	  returning the pathnode.
 */
Path *
create_ctescan_path(PlannerInfo *root, RelOptInfo *rel, Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_CteScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = NIL;	/* XXX for now, result is always unordered */

	cost_ctescan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_namedtuplestorescan_path
 *	  Creates a path corresponding to a scan of a named tuplestore, returning
 *	  the pathnode.
 */
Path *
create_namedtuplestorescan_path(PlannerInfo *root, RelOptInfo *rel,
								Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_NamedTuplestoreScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = NIL;	/* result is always unordered */

	cost_namedtuplestorescan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_resultscan_path
 *	  Creates a path corresponding to a scan of an RTE_RESULT relation,
 *	  returning the pathnode.
 */
Path *
create_resultscan_path(PlannerInfo *root, RelOptInfo *rel,
					   Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_Result;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = NIL;	/* result is always unordered */

	cost_resultscan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_worktablescan_path
 *	  Creates a path corresponding to a scan of a self-reference CTE,
 *	  returning the pathnode.
 */
Path *
create_worktablescan_path(PlannerInfo *root, RelOptInfo *rel,
						  Relids required_outer)
{
	Path	   *pathnode = makeNode(Path);

	pathnode->pathtype = T_WorkTableScan;
	pathnode->parent = rel;
	pathnode->pathtarget = rel->reltarget;
	pathnode->param_info = get_baserel_parampathinfo(root, rel,
													 required_outer);
	pathnode->parallel_aware = false;
	pathnode->parallel_safe = rel->consider_parallel;
	pathnode->parallel_workers = 0;
	pathnode->pathkeys = NIL;	/* result is always unordered */

	/* Cost is the same as for a regular CTE scan */
	cost_ctescan(pathnode, root, rel, pathnode->param_info);

	return pathnode;
}

/*
 * create_foreignscan_path
 *	  Creates a path corresponding to a scan of a foreign base table,
 *	  returning the pathnode.
 *
 * This function is never called from core Postgres; rather, it's expected
 * to be called by the GetForeignPaths function of a foreign data wrapper.
 * We make the FDW supply all fields of the path, since we do not have any way
 * to calculate them in core.  However, there is a usually-sane default for
 * the pathtarget (rel->reltarget), so we let a NULL for "target" select that.
 */
ForeignPath *
create_foreignscan_path(PlannerInfo *root, RelOptInfo *rel,
						PathTarget *target,
						double rows, Cost startup_cost, Cost total_cost,
						List *pathkeys,
						Relids required_outer,
						Path *fdw_outerpath,
						List *fdw_private)
{
	ForeignPath *pathnode = makeNode(ForeignPath);

	/* Historically some FDWs were confused about when to use this */
	Assert(IS_SIMPLE_REL(rel));

	pathnode->path.pathtype = T_ForeignScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target ? target : rel->reltarget;
	pathnode->path.param_info = get_baserel_parampathinfo(root, rel,
														  required_outer);
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.rows = rows;
	pathnode->path.startup_cost = startup_cost;
	pathnode->path.total_cost = total_cost;
	pathnode->path.pathkeys = pathkeys;

	pathnode->fdw_outerpath = fdw_outerpath;
	pathnode->fdw_private = fdw_private;

	return pathnode;
}

/*
 * create_foreign_join_path
 *	  Creates a path corresponding to a scan of a foreign join,
 *	  returning the pathnode.
 *
 * This function is never called from core Postgres; rather, it's expected
 * to be called by the GetForeignJoinPaths function of a foreign data wrapper.
 * We make the FDW supply all fields of the path, since we do not have any way
 * to calculate them in core.  However, there is a usually-sane default for
 * the pathtarget (rel->reltarget), so we let a NULL for "target" select that.
 */
ForeignPath *
create_foreign_join_path(PlannerInfo *root, RelOptInfo *rel,
						 PathTarget *target,
						 double rows, Cost startup_cost, Cost total_cost,
						 List *pathkeys,
						 Relids required_outer,
						 Path *fdw_outerpath,
						 List *fdw_private)
{
	ForeignPath *pathnode = makeNode(ForeignPath);

	/*
	 * We should use get_joinrel_parampathinfo to handle parameterized paths,
	 * but the API of this function doesn't support it, and existing
	 * extensions aren't yet trying to build such paths anyway.  For the
	 * moment just throw an error if someone tries it; eventually we should
	 * revisit this.
	 */
	if (!bms_is_empty(required_outer) || !bms_is_empty(rel->lateral_relids))
		elog(ERROR, "parameterized foreign joins are not supported yet");

	pathnode->path.pathtype = T_ForeignScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target ? target : rel->reltarget;
	pathnode->path.param_info = NULL;	/* XXX see above */
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.rows = rows;
	pathnode->path.startup_cost = startup_cost;
	pathnode->path.total_cost = total_cost;
	pathnode->path.pathkeys = pathkeys;

	pathnode->fdw_outerpath = fdw_outerpath;
	pathnode->fdw_private = fdw_private;

	return pathnode;
}

/*
 * create_foreign_upper_path
 *	  Creates a path corresponding to an upper relation that's computed
 *	  directly by an FDW, returning the pathnode.
 *
 * This function is never called from core Postgres; rather, it's expected to
 * be called by the GetForeignUpperPaths function of a foreign data wrapper.
 * We make the FDW supply all fields of the path, since we do not have any way
 * to calculate them in core.  However, there is a usually-sane default for
 * the pathtarget (rel->reltarget), so we let a NULL for "target" select that.
 */
ForeignPath *
create_foreign_upper_path(PlannerInfo *root, RelOptInfo *rel,
						  PathTarget *target,
						  double rows, Cost startup_cost, Cost total_cost,
						  List *pathkeys,
						  Path *fdw_outerpath,
						  List *fdw_private)
{
	ForeignPath *pathnode = makeNode(ForeignPath);

	/*
	 * Upper relations should never have any lateral references, since joining
	 * is complete.
	 */
	Assert(bms_is_empty(rel->lateral_relids));

	pathnode->path.pathtype = T_ForeignScan;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target ? target : rel->reltarget;
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel;
	pathnode->path.parallel_workers = 0;
	pathnode->path.rows = rows;
	pathnode->path.startup_cost = startup_cost;
	pathnode->path.total_cost = total_cost;
	pathnode->path.pathkeys = pathkeys;

	pathnode->fdw_outerpath = fdw_outerpath;
	pathnode->fdw_private = fdw_private;

	return pathnode;
}

/*
 * calc_nestloop_required_outer
 *	  Compute the required_outer set for a nestloop join path
 *
 * Note: result must not share storage with either input
 */
Relids
calc_nestloop_required_outer(Relids outerrelids,
							 Relids outer_paramrels,
							 Relids innerrelids,
							 Relids inner_paramrels)
{
	Relids		required_outer;

	/* inner_path can require rels from outer path, but not vice versa */
	Assert(!bms_overlap(outer_paramrels, innerrelids));
	/* easy case if inner path is not parameterized */
	if (!inner_paramrels)
		return bms_copy(outer_paramrels);
	/* else, form the union ... */
	required_outer = bms_union(outer_paramrels, inner_paramrels);
	/* ... and remove any mention of now-satisfied outer rels */
	required_outer = bms_del_members(required_outer,
									 outerrelids);
	/* maintain invariant that required_outer is exactly NULL if empty */
	if (bms_is_empty(required_outer))
	{
		bms_free(required_outer);
		required_outer = NULL;
	}
	return required_outer;
}

/*
 * calc_non_nestloop_required_outer
 *	  Compute the required_outer set for a merge or hash join path
 *
 * Note: result must not share storage with either input
 */
Relids
calc_non_nestloop_required_outer(Path *outer_path, Path *inner_path)
{
	Relids		outer_paramrels = PATH_REQ_OUTER(outer_path);
	Relids		inner_paramrels = PATH_REQ_OUTER(inner_path);
	Relids		required_outer;

	/* neither path can require rels from the other */
	Assert(!bms_overlap(outer_paramrels, inner_path->parent->relids));
	Assert(!bms_overlap(inner_paramrels, outer_path->parent->relids));
	/* form the union ... */
	required_outer = bms_union(outer_paramrels, inner_paramrels);
	/* we do not need an explicit test for empty; bms_union gets it right */
	return required_outer;
}

extern int yb_bnl_batch_size;

/*
 * create_nestloop_path
 *	  Creates a pathnode corresponding to a nestloop join between two
 *	  relations.
 *
 * 'joinrel' is the join relation.
 * 'jointype' is the type of join required
 * 'workspace' is the result from initial_cost_nestloop
 * 'extra' contains various information about the join
 * 'outer_path' is the outer path
 * 'inner_path' is the inner path
 * 'restrict_clauses' are the RestrictInfo nodes to apply at the join
 * 'pathkeys' are the path keys of the new join path
 * 'required_outer' is the set of required outer rels
 *
 * Returns the resulting path node.
 */
NestPath *
create_nestloop_path(PlannerInfo *root,
					 RelOptInfo *joinrel,
					 JoinType jointype,
					 JoinCostWorkspace *workspace,
					 JoinPathExtraData *extra,
					 Path *outer_path,
					 Path *inner_path,
					 List *restrict_clauses,
					 List *pathkeys,
					 Relids required_outer)
{
	NestPath   *pathnode = makeNode(NestPath);
	Relids		inner_req_outer = PATH_REQ_OUTER(inner_path);

	/*
	 * If the inner path is parameterized by the outer, we must drop any
	 * restrict_clauses that are due to be moved into the inner path.  We have
	 * to do this now, rather than postpone the work till createplan time,
	 * because the restrict_clauses list can affect the size and cost
	 * estimates for this path.
	 */
	 Relids inner_req_batched = YB_PATH_REQ_OUTER_BATCHED(inner_path);

	 Relids outer_req_unbatched = YB_PATH_REQ_OUTER_UNBATCHED(outer_path);

	 bool is_batched = (bms_overlap(inner_req_batched,
									outer_path->parent->relids) &&
						!bms_overlap(outer_req_unbatched, inner_req_batched));
	if (!is_batched && bms_overlap(inner_req_outer, outer_path->parent->relids))
	{
		Relids		inner_and_outer = bms_union(inner_path->parent->relids,
												inner_req_outer);
		List	   *jclauses = NIL;
		ListCell   *lc;

		foreach(lc, restrict_clauses)
		{
			RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

			if (!join_clause_is_movable_into(rinfo,
											 inner_path->parent->relids,
											 inner_and_outer))
				jclauses = lappend(jclauses, rinfo);
		}
		restrict_clauses = jclauses;
	}

	pathnode->jpath.path.pathtype = T_NestLoop;
	pathnode->jpath.path.parent = joinrel;
	pathnode->jpath.path.pathtarget = joinrel->reltarget;
	pathnode->jpath.path.param_info =
		get_joinrel_parampathinfo(root,
								  joinrel,
								  outer_path,
								  inner_path,
								  extra->sjinfo,
								  required_outer,
								  &restrict_clauses);
	pathnode->jpath.path.parallel_aware = false;
	pathnode->jpath.path.parallel_safe = joinrel->consider_parallel &&
		outer_path->parallel_safe && inner_path->parallel_safe;
	/* This is a foolish way to estimate parallel_workers, but for now... */
	pathnode->jpath.path.parallel_workers = outer_path->parallel_workers;
	pathnode->jpath.path.pathkeys = pathkeys;
	yb_propagate_fields2(&pathnode->jpath.path.yb_path_info,
						 &inner_path->yb_path_info,
						 &outer_path->yb_path_info);
	pathnode->jpath.jointype = jointype;
	pathnode->jpath.inner_unique = extra->inner_unique;
	pathnode->jpath.outerjoinpath = outer_path;
	pathnode->jpath.innerjoinpath = inner_path;
	pathnode->jpath.joinrestrictinfo = restrict_clauses;

	final_cost_nestloop(root, pathnode, workspace, extra);

	return pathnode;
}

/*
 * create_mergejoin_path
 *	  Creates a pathnode corresponding to a mergejoin join between
 *	  two relations
 *
 * 'joinrel' is the join relation
 * 'jointype' is the type of join required
 * 'workspace' is the result from initial_cost_mergejoin
 * 'extra' contains various information about the join
 * 'outer_path' is the outer path
 * 'inner_path' is the inner path
 * 'restrict_clauses' are the RestrictInfo nodes to apply at the join
 * 'pathkeys' are the path keys of the new join path
 * 'required_outer' is the set of required outer rels
 * 'mergeclauses' are the RestrictInfo nodes to use as merge clauses
 *		(this should be a subset of the restrict_clauses list)
 * 'outersortkeys' are the sort varkeys for the outer relation
 * 'innersortkeys' are the sort varkeys for the inner relation
 */
MergePath *
create_mergejoin_path(PlannerInfo *root,
					  RelOptInfo *joinrel,
					  JoinType jointype,
					  JoinCostWorkspace *workspace,
					  JoinPathExtraData *extra,
					  Path *outer_path,
					  Path *inner_path,
					  List *restrict_clauses,
					  List *pathkeys,
					  Relids required_outer,
					  List *mergeclauses,
					  List *outersortkeys,
					  List *innersortkeys)
{
	MergePath  *pathnode = makeNode(MergePath);

	pathnode->jpath.path.pathtype = T_MergeJoin;
	pathnode->jpath.path.parent = joinrel;
	pathnode->jpath.path.pathtarget = joinrel->reltarget;
	pathnode->jpath.path.param_info =
		get_joinrel_parampathinfo(root,
								  joinrel,
								  outer_path,
								  inner_path,
								  extra->sjinfo,
								  required_outer,
								  &restrict_clauses);
	pathnode->jpath.path.parallel_aware = false;
	pathnode->jpath.path.parallel_safe = joinrel->consider_parallel &&
		outer_path->parallel_safe && inner_path->parallel_safe;
	/* This is a foolish way to estimate parallel_workers, but for now... */
	pathnode->jpath.path.parallel_workers = outer_path->parallel_workers;
	pathnode->jpath.path.pathkeys = pathkeys;
	yb_propagate_fields2(&pathnode->jpath.path.yb_path_info,
						 &outer_path->yb_path_info,
						 &inner_path->yb_path_info);
	pathnode->jpath.jointype = jointype;
	pathnode->jpath.inner_unique = extra->inner_unique;
	pathnode->jpath.outerjoinpath = outer_path;
	pathnode->jpath.innerjoinpath = inner_path;
	pathnode->jpath.joinrestrictinfo = restrict_clauses;
	pathnode->path_mergeclauses = mergeclauses;
	pathnode->outersortkeys = outersortkeys;
	pathnode->innersortkeys = innersortkeys;
	/* pathnode->skip_mark_restore will be set by final_cost_mergejoin */
	/* pathnode->materialize_inner will be set by final_cost_mergejoin */

	final_cost_mergejoin(root, pathnode, workspace, extra);

	return pathnode;
}

/*
 * create_hashjoin_path
 *	  Creates a pathnode corresponding to a hash join between two relations.
 *
 * 'joinrel' is the join relation
 * 'jointype' is the type of join required
 * 'workspace' is the result from initial_cost_hashjoin
 * 'extra' contains various information about the join
 * 'outer_path' is the cheapest outer path
 * 'inner_path' is the cheapest inner path
 * 'parallel_hash' to select Parallel Hash of inner path (shared hash table)
 * 'restrict_clauses' are the RestrictInfo nodes to apply at the join
 * 'required_outer' is the set of required outer rels
 * 'hashclauses' are the RestrictInfo nodes to use as hash clauses
 *		(this should be a subset of the restrict_clauses list)
 */
HashPath *
create_hashjoin_path(PlannerInfo *root,
					 RelOptInfo *joinrel,
					 JoinType jointype,
					 JoinCostWorkspace *workspace,
					 JoinPathExtraData *extra,
					 Path *outer_path,
					 Path *inner_path,
					 bool parallel_hash,
					 List *restrict_clauses,
					 Relids required_outer,
					 List *hashclauses)
{
	HashPath   *pathnode = makeNode(HashPath);

	pathnode->jpath.path.pathtype = T_HashJoin;
	pathnode->jpath.path.parent = joinrel;
	pathnode->jpath.path.pathtarget = joinrel->reltarget;
	pathnode->jpath.path.param_info =
		get_joinrel_parampathinfo(root,
								  joinrel,
								  outer_path,
								  inner_path,
								  extra->sjinfo,
								  required_outer,
								  &restrict_clauses);
	pathnode->jpath.path.parallel_aware =
		joinrel->consider_parallel && parallel_hash;
	pathnode->jpath.path.parallel_safe = joinrel->consider_parallel &&
		outer_path->parallel_safe && inner_path->parallel_safe;
	/* This is a foolish way to estimate parallel_workers, but for now... */
	pathnode->jpath.path.parallel_workers = outer_path->parallel_workers;

	/*
	 * A hashjoin never has pathkeys, since its output ordering is
	 * unpredictable due to possible batching.  XXX If the inner relation is
	 * small enough, we could instruct the executor that it must not batch,
	 * and then we could assume that the output inherits the outer relation's
	 * ordering, which might save a sort step.  However there is considerable
	 * downside if our estimate of the inner relation size is badly off. For
	 * the moment we don't risk it.  (Note also that if we wanted to take this
	 * seriously, joinpath.c would have to consider many more paths for the
	 * outer rel than it does now.)
	 */
	pathnode->jpath.path.pathkeys = NIL;
	yb_propagate_fields2(&pathnode->jpath.path.yb_path_info,
						 &outer_path->yb_path_info,
						 &inner_path->yb_path_info);
	pathnode->jpath.jointype = jointype;
	pathnode->jpath.inner_unique = extra->inner_unique;
	pathnode->jpath.outerjoinpath = outer_path;
	pathnode->jpath.innerjoinpath = inner_path;
	pathnode->jpath.joinrestrictinfo = restrict_clauses;
	pathnode->path_hashclauses = hashclauses;
	/* final_cost_hashjoin will fill in pathnode->num_batches */

	final_cost_hashjoin(root, pathnode, workspace, extra);

	return pathnode;
}

/*
 * create_projection_path
 *	  Creates a pathnode that represents performing a projection.
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'target' is the PathTarget to be computed
 */
ProjectionPath *
create_projection_path(PlannerInfo *root,
					   RelOptInfo *rel,
					   Path *subpath,
					   PathTarget *target)
{
	ProjectionPath *pathnode = makeNode(ProjectionPath);
	PathTarget *oldtarget;

	/*
	 * We mustn't put a ProjectionPath directly above another; it's useless
	 * and will confuse create_projection_plan.  Rather than making sure all
	 * callers handle that, let's implement it here, by stripping off any
	 * ProjectionPath in what we're given.  Given this rule, there won't be
	 * more than one.
	 */
	if (IsA(subpath, ProjectionPath))
	{
		ProjectionPath *subpp = (ProjectionPath *) subpath;

		Assert(subpp->path.parent == rel);
		subpath = subpp->subpath;
		Assert(!IsA(subpath, ProjectionPath));
	}

	pathnode->path.pathtype = T_Result;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe &&
		is_parallel_safe(root, (Node *) target->exprs);
	pathnode->path.parallel_workers = subpath->parallel_workers;
	/* Projection does not change the sort order */
	pathnode->path.pathkeys = subpath->pathkeys;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;

	/*
	 * We might not need a separate Result node.  If the input plan node type
	 * can project, we can just tell it to project something else.  Or, if it
	 * can't project but the desired target has the same expression list as
	 * what the input will produce anyway, we can still give it the desired
	 * tlist (possibly changing its ressortgroupref labels, but nothing else).
	 * Note: in the latter case, create_projection_plan has to recheck our
	 * conclusion; see comments therein.
	 */
	oldtarget = subpath->pathtarget;
	if (is_projection_capable_path(subpath) ||
		equal(oldtarget->exprs, target->exprs))
	{
		/* No separate Result node needed */
		pathnode->dummypp = true;

		/*
		 * Set cost of plan as subpath's cost, adjusted for tlist replacement.
		 */
		pathnode->path.rows = subpath->rows;
		pathnode->path.startup_cost = subpath->startup_cost +
			(target->cost.startup - oldtarget->cost.startup);
		pathnode->path.total_cost = subpath->total_cost +
			(target->cost.startup - oldtarget->cost.startup) +
			(target->cost.per_tuple - oldtarget->cost.per_tuple) * subpath->rows;
	}
	else
	{
		/* We really do need the Result node */
		pathnode->dummypp = false;

		/*
		 * The Result node's cost is cpu_tuple_cost per row, plus the cost of
		 * evaluating the tlist.  There is no qual to worry about.
		 */
		pathnode->path.rows = subpath->rows;
		pathnode->path.startup_cost = subpath->startup_cost +
			target->cost.startup;
		pathnode->path.total_cost = subpath->total_cost +
			target->cost.startup +
			(cpu_tuple_cost + target->cost.per_tuple) * subpath->rows;
	}

	return pathnode;
}

/*
 * apply_projection_to_path
 *	  Add a projection step, or just apply the target directly to given path.
 *
 * This has the same net effect as create_projection_path(), except that if
 * a separate Result plan node isn't needed, we just replace the given path's
 * pathtarget with the desired one.  This must be used only when the caller
 * knows that the given path isn't referenced elsewhere and so can be modified
 * in-place.
 *
 * If the input path is a GatherPath or GatherMergePath, we try to push the
 * new target down to its input as well; this is a yet more invasive
 * modification of the input path, which create_projection_path() can't do.
 *
 * Note that we mustn't change the source path's parent link; so when it is
 * add_path'd to "rel" things will be a bit inconsistent.  So far that has
 * not caused any trouble.
 *
 * 'rel' is the parent relation associated with the result
 * 'path' is the path representing the source of data
 * 'target' is the PathTarget to be computed
 */
Path *
apply_projection_to_path(PlannerInfo *root,
						 RelOptInfo *rel,
						 Path *path,
						 PathTarget *target)
{
	QualCost	oldcost;

	/*
	 * If given path can't project, we might need a Result node, so make a
	 * separate ProjectionPath.
	 */
	if (!is_projection_capable_path(path))
		return (Path *) create_projection_path(root, rel, path, target);

	/*
	 * We can just jam the desired tlist into the existing path, being sure to
	 * update its cost estimates appropriately.
	 */
	oldcost = path->pathtarget->cost;
	path->pathtarget = target;

	path->startup_cost += target->cost.startup - oldcost.startup;
	path->total_cost += target->cost.startup - oldcost.startup +
		(target->cost.per_tuple - oldcost.per_tuple) * path->rows;

	/*
	 * If the path happens to be a Gather or GatherMerge path, we'd like to
	 * arrange for the subpath to return the required target list so that
	 * workers can help project.  But if there is something that is not
	 * parallel-safe in the target expressions, then we can't.
	 */
	if ((IsA(path, GatherPath) || IsA(path, GatherMergePath)) &&
		is_parallel_safe(root, (Node *) target->exprs))
	{
		/*
		 * We always use create_projection_path here, even if the subpath is
		 * projection-capable, so as to avoid modifying the subpath in place.
		 * It seems unlikely at present that there could be any other
		 * references to the subpath, but better safe than sorry.
		 *
		 * Note that we don't change the parallel path's cost estimates; it
		 * might be appropriate to do so, to reflect the fact that the bulk of
		 * the target evaluation will happen in workers.
		 */
		if (IsA(path, GatherPath))
		{
			GatherPath *gpath = (GatherPath *) path;

			gpath->subpath = (Path *)
				create_projection_path(root,
									   gpath->subpath->parent,
									   gpath->subpath,
									   target);
		}
		else
		{
			GatherMergePath *gmpath = (GatherMergePath *) path;

			gmpath->subpath = (Path *)
				create_projection_path(root,
									   gmpath->subpath->parent,
									   gmpath->subpath,
									   target);
		}
	}
	else if (path->parallel_safe &&
			 !is_parallel_safe(root, (Node *) target->exprs))
	{
		/*
		 * We're inserting a parallel-restricted target list into a path
		 * currently marked parallel-safe, so we have to mark it as no longer
		 * safe.
		 */
		path->parallel_safe = false;
	}

	return path;
}

/*
 * create_set_projection_path
 *	  Creates a pathnode that represents performing a projection that
 *	  includes set-returning functions.
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'target' is the PathTarget to be computed
 */
ProjectSetPath *
create_set_projection_path(PlannerInfo *root,
						   RelOptInfo *rel,
						   Path *subpath,
						   PathTarget *target)
{
	ProjectSetPath *pathnode = makeNode(ProjectSetPath);
	double		tlist_rows;
	ListCell   *lc;

	pathnode->path.pathtype = T_ProjectSet;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe &&
		is_parallel_safe(root, (Node *) target->exprs);
	pathnode->path.parallel_workers = subpath->parallel_workers;
	/* Projection does not change the sort order XXX? */
	pathnode->path.pathkeys = subpath->pathkeys;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);
	/*
	 * YB: SRFs can produce multiple rows for each row.
	 * Example: col1, GENERATE_SERIES(1, 1000) produces 1000 rows for each col1.
	 */
	pathnode->path.yb_path_info.yb_uniqkeys = NIL;

	pathnode->subpath = subpath;

	/*
	 * Estimate number of rows produced by SRFs for each row of input; if
	 * there's more than one in this node, use the maximum.
	 */
	tlist_rows = 1;
	foreach(lc, target->exprs)
	{
		Node	   *node = (Node *) lfirst(lc);
		double		itemrows;

		itemrows = expression_returns_set_rows(root, node);
		if (tlist_rows < itemrows)
			tlist_rows = itemrows;
	}

	/*
	 * In addition to the cost of evaluating the tlist, charge cpu_tuple_cost
	 * per input row, and half of cpu_tuple_cost for each added output row.
	 * This is slightly bizarre maybe, but it's what 9.6 did; we may revisit
	 * this estimate later.
	 */
	pathnode->path.rows = subpath->rows * tlist_rows;
	pathnode->path.startup_cost = subpath->startup_cost +
		target->cost.startup;
	pathnode->path.total_cost = subpath->total_cost +
		target->cost.startup +
		(cpu_tuple_cost + target->cost.per_tuple) * subpath->rows +
		(pathnode->path.rows - subpath->rows) * cpu_tuple_cost / 2;

	return pathnode;
}

/*
 * create_incremental_sort_path
 *	  Creates a pathnode that represents performing an incremental sort.
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'pathkeys' represents the desired sort order
 * 'presorted_keys' is the number of keys by which the input path is
 *		already sorted
 * 'limit_tuples' is the estimated bound on the number of output tuples,
 *		or -1 if no LIMIT or couldn't estimate
 */
IncrementalSortPath *
create_incremental_sort_path(PlannerInfo *root,
							 RelOptInfo *rel,
							 Path *subpath,
							 List *pathkeys,
							 int presorted_keys,
							 double limit_tuples)
{
	IncrementalSortPath *sort = makeNode(IncrementalSortPath);
	SortPath   *pathnode = &sort->spath;

	pathnode->path.pathtype = T_IncrementalSort;
	pathnode->path.parent = rel;
	/* Sort doesn't project, so use source path's pathtarget */
	pathnode->path.pathtarget = subpath->pathtarget;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	pathnode->path.pathkeys = pathkeys;

	pathnode->subpath = subpath;

	cost_incremental_sort(&pathnode->path,
						  root, pathkeys, presorted_keys,
						  subpath->startup_cost,
						  subpath->total_cost,
						  subpath->rows,
						  subpath->pathtarget->width,
						  0.0,	/* XXX comparison_cost shouldn't be 0? */
						  work_mem, limit_tuples);

	sort->nPresortedCols = presorted_keys;

	return sort;
}

/*
 * create_sort_path
 *	  Creates a pathnode that represents performing an explicit sort.
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'pathkeys' represents the desired sort order
 * 'limit_tuples' is the estimated bound on the number of output tuples,
 *		or -1 if no LIMIT or couldn't estimate
 */
SortPath *
create_sort_path(PlannerInfo *root,
				 RelOptInfo *rel,
				 Path *subpath,
				 List *pathkeys,
				 double limit_tuples)
{
	SortPath   *pathnode = makeNode(SortPath);

	pathnode->path.pathtype = T_Sort;
	pathnode->path.parent = rel;
	/* Sort doesn't project, so use source path's pathtarget */
	pathnode->path.pathtarget = subpath->pathtarget;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	pathnode->path.pathkeys = pathkeys;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;

	cost_sort(&pathnode->path, root, pathkeys,
			  subpath->total_cost,
			  subpath->rows,
			  subpath->pathtarget->width,
			  0.0,				/* XXX comparison_cost shouldn't be 0? */
			  work_mem, limit_tuples);

	return pathnode;
}

/*
 * create_group_path
 *	  Creates a pathnode that represents performing grouping of presorted input
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'target' is the PathTarget to be computed
 * 'groupClause' is a list of SortGroupClause's representing the grouping
 * 'qual' is the HAVING quals if any
 * 'numGroups' is the estimated number of groups
 */
GroupPath *
create_group_path(PlannerInfo *root,
				  RelOptInfo *rel,
				  Path *subpath,
				  List *groupClause,
				  List *qual,
				  double numGroups)
{
	GroupPath  *pathnode = makeNode(GroupPath);
	PathTarget *target = rel->reltarget;

	pathnode->path.pathtype = T_Group;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	/* Group doesn't change sort ordering */
	pathnode->path.pathkeys = subpath->pathkeys;

	yb_propagate_fields(&pathnode->path.yb_path_info, &subpath->yb_path_info);

	pathnode->subpath = subpath;

	pathnode->groupClause = groupClause;
	pathnode->qual = qual;

	cost_group(&pathnode->path, root,
			   list_length(groupClause),
			   numGroups,
			   qual,
			   subpath->startup_cost, subpath->total_cost,
			   subpath->rows);

	/* add tlist eval cost for each output row */
	pathnode->path.startup_cost += target->cost.startup;
	pathnode->path.total_cost += target->cost.startup +
		target->cost.per_tuple * pathnode->path.rows;

	return pathnode;
}

/*
 * create_upper_unique_path
 *	  Creates a pathnode that represents performing an explicit Unique step
 *	  on presorted input.
 *
 * This produces a Unique plan node, but the use-case is so different from
 * create_unique_path that it doesn't seem worth trying to merge the two.
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'numCols' is the number of grouping columns
 * 'numGroups' is the estimated number of groups
 *
 * The input path must be sorted on the grouping columns, plus possibly
 * additional columns; so the first numCols pathkeys are the grouping columns
 */
UpperUniquePath *
create_upper_unique_path(PlannerInfo *root,
						 RelOptInfo *rel,
						 Path *subpath,
						 int numCols,
						 double numGroups)
{
	UpperUniquePath *pathnode = makeNode(UpperUniquePath);

	pathnode->path.pathtype = T_Unique;
	pathnode->path.parent = rel;
	/* Unique doesn't project, so use source path's pathtarget */
	pathnode->path.pathtarget = subpath->pathtarget;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	/* Unique doesn't change the input ordering */
	pathnode->path.pathkeys = subpath->pathkeys;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;
	pathnode->numkeys = numCols;

	/*
	 * Charge one cpu_operator_cost per comparison per input tuple. We assume
	 * all columns get compared at most of the tuples.  (XXX probably this is
	 * an overestimate.)
	 */
	pathnode->path.startup_cost = subpath->startup_cost;
	pathnode->path.total_cost = subpath->total_cost +
		cpu_operator_cost * subpath->rows * numCols;
	pathnode->path.rows = numGroups;

	return pathnode;
}

/*
 * create_agg_path
 *	  Creates a pathnode that represents performing aggregation/grouping
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'target' is the PathTarget to be computed
 * 'aggstrategy' is the Agg node's basic implementation strategy
 * 'aggsplit' is the Agg node's aggregate-splitting mode
 * 'groupClause' is a list of SortGroupClause's representing the grouping
 * 'qual' is the HAVING quals if any
 * 'aggcosts' contains cost info about the aggregate functions to be computed
 * 'numGroups' is the estimated number of groups (1 if not grouping)
 */
AggPath *
create_agg_path(PlannerInfo *root,
				RelOptInfo *rel,
				Path *subpath,
				PathTarget *target,
				AggStrategy aggstrategy,
				AggSplit aggsplit,
				List *groupClause,
				List *qual,
				const AggClauseCosts *aggcosts,
				double numGroups)
{
	AggPath    *pathnode = makeNode(AggPath);

	pathnode->path.pathtype = T_Agg;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	if (aggstrategy == AGG_SORTED)
		pathnode->path.pathkeys = subpath->pathkeys;	/* preserves order */
	else
		pathnode->path.pathkeys = NIL;	/* output is unordered */
	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);
	pathnode->subpath = subpath;

	pathnode->aggstrategy = aggstrategy;
	pathnode->aggsplit = aggsplit;
	pathnode->numGroups = numGroups;
	pathnode->transitionSpace = aggcosts ? aggcosts->transitionSpace : 0;
	pathnode->groupClause = groupClause;
	pathnode->qual = qual;

	cost_agg(&pathnode->path, root,
			 aggstrategy, aggcosts,
			 list_length(groupClause), numGroups,
			 qual,
			 subpath->startup_cost, subpath->total_cost,
			 subpath->rows, subpath->pathtarget->width);

	/* add tlist eval cost for each output row */
	pathnode->path.startup_cost += target->cost.startup;
	pathnode->path.total_cost += target->cost.startup +
		target->cost.per_tuple * pathnode->path.rows;

	return pathnode;
}

/*
 * create_groupingsets_path
 *	  Creates a pathnode that represents performing GROUPING SETS aggregation
 *
 * GroupingSetsPath represents sorted grouping with one or more grouping sets.
 * The input path's result must be sorted to match the last entry in
 * rollup_groupclauses.
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'target' is the PathTarget to be computed
 * 'having_qual' is the HAVING quals if any
 * 'rollups' is a list of RollupData nodes
 * 'agg_costs' contains cost info about the aggregate functions to be computed
 * 'numGroups' is the estimated total number of groups
 */
GroupingSetsPath *
create_groupingsets_path(PlannerInfo *root,
						 RelOptInfo *rel,
						 Path *subpath,
						 List *having_qual,
						 AggStrategy aggstrategy,
						 List *rollups,
						 const AggClauseCosts *agg_costs,
						 double numGroups)
{
	GroupingSetsPath *pathnode = makeNode(GroupingSetsPath);
	PathTarget *target = rel->reltarget;
	ListCell   *lc;
	bool		is_first = true;
	bool		is_first_sort = true;

	/* The topmost generated Plan node will be an Agg */
	pathnode->path.pathtype = T_Agg;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	pathnode->path.param_info = subpath->param_info;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	pathnode->subpath = subpath;

	/*
	 * Simplify callers by downgrading AGG_SORTED to AGG_PLAIN, and AGG_MIXED
	 * to AGG_HASHED, here if possible.
	 */
	if (aggstrategy == AGG_SORTED &&
		list_length(rollups) == 1 &&
		((RollupData *) linitial(rollups))->groupClause == NIL)
		aggstrategy = AGG_PLAIN;

	if (aggstrategy == AGG_MIXED &&
		list_length(rollups) == 1)
		aggstrategy = AGG_HASHED;

	/*
	 * Output will be in sorted order by group_pathkeys if, and only if, there
	 * is a single rollup operation on a non-empty list of grouping
	 * expressions.
	 */
	if (aggstrategy == AGG_SORTED && list_length(rollups) == 1)
		pathnode->path.pathkeys = root->group_pathkeys;
	else
		pathnode->path.pathkeys = NIL;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);
	/* YB: Set of unique keys is not preserved. */
	pathnode->path.yb_path_info.yb_uniqkeys = NIL;

	pathnode->aggstrategy = aggstrategy;
	pathnode->rollups = rollups;
	pathnode->qual = having_qual;
	pathnode->transitionSpace = agg_costs ? agg_costs->transitionSpace : 0;

	Assert(rollups != NIL);
	Assert(aggstrategy != AGG_PLAIN || list_length(rollups) == 1);
	Assert(aggstrategy != AGG_MIXED || list_length(rollups) > 1);

	foreach(lc, rollups)
	{
		RollupData *rollup = lfirst(lc);
		List	   *gsets = rollup->gsets;
		int			numGroupCols = list_length(linitial(gsets));

		/*
		 * In AGG_SORTED or AGG_PLAIN mode, the first rollup takes the
		 * (already-sorted) input, and following ones do their own sort.
		 *
		 * In AGG_HASHED mode, there is one rollup for each grouping set.
		 *
		 * In AGG_MIXED mode, the first rollups are hashed, the first
		 * non-hashed one takes the (already-sorted) input, and following ones
		 * do their own sort.
		 */
		if (is_first)
		{
			cost_agg(&pathnode->path, root,
					 aggstrategy,
					 agg_costs,
					 numGroupCols,
					 rollup->numGroups,
					 having_qual,
					 subpath->startup_cost,
					 subpath->total_cost,
					 subpath->rows,
					 subpath->pathtarget->width);
			is_first = false;
			if (!rollup->is_hashed)
				is_first_sort = false;
		}
		else
		{
			Path		sort_path;	/* dummy for result of cost_sort */
			Path		agg_path;	/* dummy for result of cost_agg */

			if (rollup->is_hashed || is_first_sort)
			{
				/*
				 * Account for cost of aggregation, but don't charge input
				 * cost again
				 */
				cost_agg(&agg_path, root,
						 rollup->is_hashed ? AGG_HASHED : AGG_SORTED,
						 agg_costs,
						 numGroupCols,
						 rollup->numGroups,
						 having_qual,
						 0.0, 0.0,
						 subpath->rows,
						 subpath->pathtarget->width);
				if (!rollup->is_hashed)
					is_first_sort = false;
			}
			else
			{
				/* Account for cost of sort, but don't charge input cost again */
				cost_sort(&sort_path, root, NIL,
						  0.0,
						  subpath->rows,
						  subpath->pathtarget->width,
						  0.0,
						  work_mem,
						  -1.0);

				/* Account for cost of aggregation */

				cost_agg(&agg_path, root,
						 AGG_SORTED,
						 agg_costs,
						 numGroupCols,
						 rollup->numGroups,
						 having_qual,
						 sort_path.startup_cost,
						 sort_path.total_cost,
						 sort_path.rows,
						 subpath->pathtarget->width);
			}

			pathnode->path.total_cost += agg_path.total_cost;
			pathnode->path.rows += agg_path.rows;
		}
	}

	/* add tlist eval cost for each output row */
	pathnode->path.startup_cost += target->cost.startup;
	pathnode->path.total_cost += target->cost.startup +
		target->cost.per_tuple * pathnode->path.rows;

	return pathnode;
}

/*
 * create_minmaxagg_path
 *	  Creates a pathnode that represents computation of MIN/MAX aggregates
 *
 * 'rel' is the parent relation associated with the result
 * 'target' is the PathTarget to be computed
 * 'mmaggregates' is a list of MinMaxAggInfo structs
 * 'quals' is the HAVING quals if any
 */
MinMaxAggPath *
create_minmaxagg_path(PlannerInfo *root,
					  RelOptInfo *rel,
					  PathTarget *target,
					  List *mmaggregates,
					  List *quals)
{
	MinMaxAggPath *pathnode = makeNode(MinMaxAggPath);
	Cost		initplan_cost;
	ListCell   *lc;

	/* The topmost generated Plan node will be a Result */
	pathnode->path.pathtype = T_Result;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	/* A MinMaxAggPath implies use of subplans, so cannot be parallel-safe */
	pathnode->path.parallel_safe = false;
	pathnode->path.parallel_workers = 0;
	/* Result is one unordered row */
	pathnode->path.rows = 1;
	pathnode->path.pathkeys = NIL;

	yb_propagate_mmagg_fields(&pathnode->path.yb_path_info, mmaggregates);

	pathnode->mmaggregates = mmaggregates;
	pathnode->quals = quals;

	/* Calculate cost of all the initplans ... */
	initplan_cost = 0;
	foreach(lc, mmaggregates)
	{
		MinMaxAggInfo *mminfo = (MinMaxAggInfo *) lfirst(lc);

		initplan_cost += mminfo->pathcost;
	}

	/* add tlist eval cost for each output row, plus cpu_tuple_cost */
	pathnode->path.startup_cost = initplan_cost + target->cost.startup;
	pathnode->path.total_cost = initplan_cost + target->cost.startup +
		target->cost.per_tuple + cpu_tuple_cost;

	/*
	 * Add cost of qual, if any --- but we ignore its selectivity, since our
	 * rowcount estimate should be 1 no matter what the qual is.
	 */
	if (quals)
	{
		QualCost	qual_cost;

		cost_qual_eval(&qual_cost, quals, root);
		pathnode->path.startup_cost += qual_cost.startup;
		pathnode->path.total_cost += qual_cost.startup + qual_cost.per_tuple;
	}

	return pathnode;
}

/*
 * create_windowagg_path
 *	  Creates a pathnode that represents computation of window functions
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'target' is the PathTarget to be computed
 * 'windowFuncs' is a list of WindowFunc structs
 * 'winclause' is a WindowClause that is common to all the WindowFuncs
 * 'qual' WindowClause.runconditions from lower-level WindowAggPaths.
 *		Must always be NIL when topwindow == false
 * 'topwindow' pass as true only for the top-level WindowAgg. False for all
 *		intermediate WindowAggs.
 *
 * The input must be sorted according to the WindowClause's PARTITION keys
 * plus ORDER BY keys.
 */
WindowAggPath *
create_windowagg_path(PlannerInfo *root,
					  RelOptInfo *rel,
					  Path *subpath,
					  PathTarget *target,
					  List *windowFuncs,
					  WindowClause *winclause,
					  List *qual,
					  bool topwindow)
{
	WindowAggPath *pathnode = makeNode(WindowAggPath);

	/* qual can only be set for the topwindow */
	Assert(qual == NIL || topwindow);

	pathnode->path.pathtype = T_WindowAgg;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	/* WindowAgg preserves the input sort order */
	pathnode->path.pathkeys = subpath->pathkeys;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;
	pathnode->winclause = winclause;
	pathnode->qual = qual;
	pathnode->topwindow = topwindow;

	/*
	 * For costing purposes, assume that there are no redundant partitioning
	 * or ordering columns; it's not worth the trouble to deal with that
	 * corner case here.  So we just pass the unmodified list lengths to
	 * cost_windowagg.
	 */
	cost_windowagg(&pathnode->path, root,
				   windowFuncs,
				   list_length(winclause->partitionClause),
				   list_length(winclause->orderClause),
				   subpath->startup_cost,
				   subpath->total_cost,
				   subpath->rows);

	/* add tlist eval cost for each output row */
	pathnode->path.startup_cost += target->cost.startup;
	pathnode->path.total_cost += target->cost.startup +
		target->cost.per_tuple * pathnode->path.rows;

	return pathnode;
}

/*
 * create_setop_path
 *	  Creates a pathnode that represents computation of INTERSECT or EXCEPT
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'cmd' is the specific semantics (INTERSECT or EXCEPT, with/without ALL)
 * 'strategy' is the implementation strategy (sorted or hashed)
 * 'distinctList' is a list of SortGroupClause's representing the grouping
 * 'flagColIdx' is the column number where the flag column will be, if any
 * 'firstFlag' is the flag value for the first input relation when hashing;
 *		or -1 when sorting
 * 'numGroups' is the estimated number of distinct groups
 * 'outputRows' is the estimated number of output rows
 */
SetOpPath *
create_setop_path(PlannerInfo *root,
				  RelOptInfo *rel,
				  Path *subpath,
				  SetOpCmd cmd,
				  SetOpStrategy strategy,
				  List *distinctList,
				  AttrNumber flagColIdx,
				  int firstFlag,
				  double numGroups,
				  double outputRows)
{
	SetOpPath  *pathnode = makeNode(SetOpPath);

	pathnode->path.pathtype = T_SetOp;
	pathnode->path.parent = rel;
	/* SetOp doesn't project, so use source path's pathtarget */
	pathnode->path.pathtarget = subpath->pathtarget;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	/* SetOp preserves the input sort order if in sort mode */
	pathnode->path.pathkeys =
		(strategy == SETOP_SORTED) ? subpath->pathkeys : NIL;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;
	pathnode->cmd = cmd;
	pathnode->strategy = strategy;
	pathnode->distinctList = distinctList;
	pathnode->flagColIdx = flagColIdx;
	pathnode->firstFlag = firstFlag;
	pathnode->numGroups = numGroups;

	/*
	 * Charge one cpu_operator_cost per comparison per input tuple. We assume
	 * all columns get compared at most of the tuples.
	 */
	pathnode->path.startup_cost = subpath->startup_cost;
	pathnode->path.total_cost = subpath->total_cost +
		cpu_operator_cost * subpath->rows * list_length(distinctList);
	pathnode->path.rows = outputRows;

	return pathnode;
}

/*
 * create_recursiveunion_path
 *	  Creates a pathnode that represents a recursive UNION node
 *
 * 'rel' is the parent relation associated with the result
 * 'leftpath' is the source of data for the non-recursive term
 * 'rightpath' is the source of data for the recursive term
 * 'target' is the PathTarget to be computed
 * 'distinctList' is a list of SortGroupClause's representing the grouping
 * 'wtParam' is the ID of Param representing work table
 * 'numGroups' is the estimated number of groups
 *
 * For recursive UNION ALL, distinctList is empty and numGroups is zero
 */
RecursiveUnionPath *
create_recursiveunion_path(PlannerInfo *root,
						   RelOptInfo *rel,
						   Path *leftpath,
						   Path *rightpath,
						   PathTarget *target,
						   List *distinctList,
						   int wtParam,
						   double numGroups)
{
	RecursiveUnionPath *pathnode = makeNode(RecursiveUnionPath);

	pathnode->path.pathtype = T_RecursiveUnion;
	pathnode->path.parent = rel;
	pathnode->path.pathtarget = target;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		leftpath->parallel_safe && rightpath->parallel_safe;
	/* Foolish, but we'll do it like joins for now: */
	pathnode->path.parallel_workers = leftpath->parallel_workers;
	/* RecursiveUnion result is always unsorted */
	pathnode->path.pathkeys = NIL;

	yb_propagate_fields2(&pathnode->path.yb_path_info,
						 &leftpath->yb_path_info,
						 &rightpath->yb_path_info);
	/* YB: Union may introduce duplicate rows. */
	pathnode->path.yb_path_info.yb_uniqkeys = NIL;

	pathnode->leftpath = leftpath;
	pathnode->rightpath = rightpath;
	pathnode->distinctList = distinctList;
	pathnode->wtParam = wtParam;
	pathnode->numGroups = numGroups;

	cost_recursive_union(&pathnode->path, leftpath, rightpath);

	return pathnode;
}

/*
 * create_lockrows_path
 *	  Creates a pathnode that represents acquiring row locks
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'rowMarks' is a list of PlanRowMark's
 * 'epqParam' is the ID of Param for EvalPlanQual re-eval
 */
LockRowsPath *
create_lockrows_path(PlannerInfo *root, RelOptInfo *rel,
					 Path *subpath, List *rowMarks, int epqParam)
{
	LockRowsPath *pathnode = makeNode(LockRowsPath);

	pathnode->path.pathtype = T_LockRows;
	pathnode->path.parent = rel;
	/* LockRows doesn't project, so use source path's pathtarget */
	pathnode->path.pathtarget = subpath->pathtarget;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = false;
	pathnode->path.parallel_workers = 0;
	pathnode->path.rows = subpath->rows;

	/*
	 * The result cannot be assumed sorted, since locking might cause the sort
	 * key columns to be replaced with new values.
	 */
	pathnode->path.pathkeys = NIL;

	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);

	pathnode->subpath = subpath;
	pathnode->rowMarks = rowMarks;
	pathnode->epqParam = epqParam;

	/*
	 * We should charge something extra for the costs of row locking and
	 * possible refetches, but it's hard to say how much.  For now, use
	 * cpu_tuple_cost per row.
	 */
	pathnode->path.startup_cost = subpath->startup_cost;
	pathnode->path.total_cost = subpath->total_cost +
		cpu_tuple_cost * subpath->rows;

	return pathnode;
}

/*
 * create_modifytable_path
 *	  Creates a pathnode that represents performing INSERT/UPDATE/DELETE/MERGE
 *	  mods
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is a Path producing source data
 * 'operation' is the operation type
 * 'canSetTag' is true if we set the command tag/es_processed
 * 'nominalRelation' is the parent RT index for use of EXPLAIN
 * 'rootRelation' is the partitioned table root RT index, or 0 if none
 * 'partColsUpdated' is true if any partitioning columns are being updated,
 *		either from the target relation or a descendent partitioned table.
 * 'resultRelations' is an integer list of actual RT indexes of target rel(s)
 * 'updateColnosLists' is a list of UPDATE target column number lists
 *		(one sublist per rel); or NIL if not an UPDATE
 * 'withCheckOptionLists' is a list of WCO lists (one per rel)
 * 'returningLists' is a list of RETURNING tlists (one per rel)
 * 'rowMarks' is a list of PlanRowMarks (non-locking only)
 * 'onconflict' is the ON CONFLICT clause, or NULL
 * 'epqParam' is the ID of Param for EvalPlanQual re-eval
 * 'mergeActionLists' is a list of lists of MERGE actions (one per rel)
 */
ModifyTablePath *
create_modifytable_path(PlannerInfo *root, RelOptInfo *rel,
						Path *subpath,
						CmdType operation, bool canSetTag,
						Index nominalRelation, Index rootRelation,
						bool partColsUpdated,
						List *resultRelations,
						List *updateColnosLists,
						List *withCheckOptionLists, List *returningLists,
						List *rowMarks, OnConflictExpr *onconflict,
						List *mergeActionLists, int epqParam)
{
	ModifyTablePath *pathnode = makeNode(ModifyTablePath);

	Assert(operation == CMD_MERGE ||
		   (operation == CMD_UPDATE ?
			list_length(resultRelations) == list_length(updateColnosLists) :
			updateColnosLists == NIL));
	Assert(withCheckOptionLists == NIL ||
		   list_length(resultRelations) == list_length(withCheckOptionLists));
	Assert(returningLists == NIL ||
		   list_length(resultRelations) == list_length(returningLists));

	pathnode->path.pathtype = T_ModifyTable;
	pathnode->path.parent = rel;
	/* pathtarget is not interesting, just make it minimally valid */
	pathnode->path.pathtarget = rel->reltarget;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = false;
	pathnode->path.parallel_workers = 0;
	pathnode->path.pathkeys = NIL;

#ifdef YB_TODO
	/* YB_TODO(jasonk) subpaths is changed in Pg15 */
	yb_propagate_fields_list(&pathnode->path.yb_path_info, subpaths);
#endif

	/*
	 * Compute cost & rowcount as subpath cost & rowcount (if RETURNING)
	 *
	 * Currently, we don't charge anything extra for the actual table
	 * modification work, nor for the WITH CHECK OPTIONS or RETURNING
	 * expressions if any.  It would only be window dressing, since
	 * ModifyTable is always a top-level node and there is no way for the
	 * costs to change any higher-level planning choices.  But we might want
	 * to make it look better sometime.
	 */
	pathnode->path.startup_cost = subpath->startup_cost;
	pathnode->path.total_cost = subpath->total_cost;
	if (returningLists != NIL)
	{
		pathnode->path.rows = subpath->rows;

		/*
		 * Set width to match the subpath output.  XXX this is totally wrong:
		 * we should return an average of the RETURNING tlist widths.  But
		 * it's what happened historically, and improving it is a task for
		 * another day.  (Again, it's mostly window dressing.)
		 */
		pathnode->path.pathtarget->width = subpath->pathtarget->width;
	}
	else
	{
		pathnode->path.rows = 0;
		pathnode->path.pathtarget->width = 0;
	}

	pathnode->subpath = subpath;
	pathnode->operation = operation;
	pathnode->canSetTag = canSetTag;
	pathnode->nominalRelation = nominalRelation;
	pathnode->rootRelation = rootRelation;
	pathnode->partColsUpdated = partColsUpdated;
	pathnode->resultRelations = resultRelations;
	pathnode->updateColnosLists = updateColnosLists;
	pathnode->withCheckOptionLists = withCheckOptionLists;
	pathnode->returningLists = returningLists;
	pathnode->rowMarks = rowMarks;
	pathnode->onconflict = onconflict;
	pathnode->epqParam = epqParam;
	pathnode->mergeActionLists = mergeActionLists;

	return pathnode;
}

/*
 * create_limit_path
 *	  Creates a pathnode that represents performing LIMIT/OFFSET
 *
 * In addition to providing the actual OFFSET and LIMIT expressions,
 * the caller must provide estimates of their values for costing purposes.
 * The estimates are as computed by preprocess_limit(), ie, 0 represents
 * the clause not being present, and -1 means it's present but we could
 * not estimate its value.
 *
 * 'rel' is the parent relation associated with the result
 * 'subpath' is the path representing the source of data
 * 'limitOffset' is the actual OFFSET expression, or NULL
 * 'limitCount' is the actual LIMIT expression, or NULL
 * 'offset_est' is the estimated value of the OFFSET expression
 * 'count_est' is the estimated value of the LIMIT expression
 */
LimitPath *
create_limit_path(PlannerInfo *root, RelOptInfo *rel,
				  Path *subpath,
				  Node *limitOffset, Node *limitCount,
				  LimitOption limitOption,
				  int64 offset_est, int64 count_est)
{
	LimitPath  *pathnode = makeNode(LimitPath);

	pathnode->path.pathtype = T_Limit;
	pathnode->path.parent = rel;
	/* Limit doesn't project, so use source path's pathtarget */
	pathnode->path.pathtarget = subpath->pathtarget;
	/* For now, assume we are above any joins, so no parameterization */
	pathnode->path.param_info = NULL;
	pathnode->path.parallel_aware = false;
	pathnode->path.parallel_safe = rel->consider_parallel &&
		subpath->parallel_safe;
	pathnode->path.parallel_workers = subpath->parallel_workers;
	pathnode->path.rows = subpath->rows;
	pathnode->path.startup_cost = subpath->startup_cost;
	pathnode->path.total_cost = subpath->total_cost;
	pathnode->path.pathkeys = subpath->pathkeys;
	yb_propagate_fields(&pathnode->path.yb_path_info,
						&subpath->yb_path_info);
	pathnode->subpath = subpath;
	pathnode->limitOffset = limitOffset;
	pathnode->limitCount = limitCount;
	pathnode->limitOption = limitOption;

	/*
	 * Adjust the output rows count and costs according to the offset/limit.
	 */
	adjust_limit_rows_costs(&pathnode->path.rows,
							&pathnode->path.startup_cost,
							&pathnode->path.total_cost,
							offset_est, count_est);

	return pathnode;
}

/*
 * adjust_limit_rows_costs
 *	  Adjust the size and cost estimates for a LimitPath node according to the
 *	  offset/limit.
 *
 * This is only a cosmetic issue if we are at top level, but if we are
 * building a subquery then it's important to report correct info to the outer
 * planner.
 *
 * When the offset or count couldn't be estimated, use 10% of the estimated
 * number of rows emitted from the subpath.
 *
 * XXX we don't bother to add eval costs of the offset/limit expressions
 * themselves to the path costs.  In theory we should, but in most cases those
 * expressions are trivial and it's just not worth the trouble.
 */
void
adjust_limit_rows_costs(double *rows,	/* in/out parameter */
						Cost *startup_cost, /* in/out parameter */
						Cost *total_cost,	/* in/out parameter */
						int64 offset_est,
						int64 count_est)
{
	double		input_rows = *rows;
	Cost		input_startup_cost = *startup_cost;
	Cost		input_total_cost = *total_cost;

	if (offset_est != 0)
	{
		double		offset_rows;

		if (offset_est > 0)
			offset_rows = (double) offset_est;
		else
			offset_rows = clamp_row_est(input_rows * 0.10);
		if (offset_rows > *rows)
			offset_rows = *rows;
		if (input_rows > 0)
			*startup_cost +=
				(input_total_cost - input_startup_cost)
				* offset_rows / input_rows;
		*rows -= offset_rows;
		if (*rows < 1)
			*rows = 1;
	}

	if (count_est != 0)
	{
		double		count_rows;

		if (count_est > 0)
			count_rows = (double) count_est;
		else
			count_rows = clamp_row_est(input_rows * 0.10);
		if (count_rows > *rows)
			count_rows = *rows;
		if (input_rows > 0)
			*total_cost = *startup_cost +
				(input_total_cost - input_startup_cost)
				* count_rows / input_rows;
		*rows = count_rows;
		if (*rows < 1)
			*rows = 1;
	}
}


/*
 * reparameterize_path
 *		Attempt to modify a Path to have greater parameterization
 *
 * We use this to attempt to bring all child paths of an appendrel to the
 * same parameterization level, ensuring that they all enforce the same set
 * of join quals (and thus that that parameterization can be attributed to
 * an append path built from such paths).  Currently, only a few path types
 * are supported here, though more could be added at need.  We return NULL
 * if we can't reparameterize the given path.
 *
 * Note: we intentionally do not pass created paths to add_path(); it would
 * possibly try to delete them on the grounds of being cost-inferior to the
 * paths they were made from, and we don't want that.  Paths made here are
 * not necessarily of general-purpose usefulness, but they can be useful
 * as members of an append path.
 */
Path *
reparameterize_path(PlannerInfo *root, Path *path,
					Relids required_outer,
					double loop_count)
{
	RelOptInfo *rel = path->parent;

	/* Can only increase, not decrease, path's parameterization */
	if (!bms_is_subset(PATH_REQ_OUTER(path), required_outer))
		return NULL;
	switch (path->pathtype)
	{
		case T_SeqScan:
			return create_seqscan_path(root, rel, required_outer, 0);
		case T_SampleScan:
			return (Path *) create_samplescan_path(root, rel, required_outer);
		case T_IndexScan:
		case T_IndexOnlyScan:
			{
				IndexPath  *ipath = (IndexPath *) path;
				IndexPath  *newpath = makeNode(IndexPath);

				/*
				 * We can't use create_index_path directly, and would not want
				 * to because it would re-compute the indexqual conditions
				 * which is wasted effort.  Instead we hack things a bit:
				 * flat-copy the path node, revise its param_info, and redo
				 * the cost estimate.
				 */
				memcpy(newpath, ipath, sizeof(IndexPath));
				newpath->path.param_info =
					get_baserel_parampathinfo(root, rel, required_outer);
				cost_index(newpath, root, loop_count, false);
				return (Path *) newpath;
			}
		case T_BitmapHeapScan:
			{
				BitmapHeapPath *bpath = (BitmapHeapPath *) path;

				return (Path *) create_bitmap_heap_path(root,
														rel,
														bpath->bitmapqual,
														required_outer,
														loop_count, 0);
			}
		case T_YbBitmapTableScan:
			{
				YbBitmapTablePath *bpath = (YbBitmapTablePath *) path;

				return (Path *) create_yb_bitmap_table_path(root,
															rel,
															bpath->bitmapqual,
															required_outer,
															loop_count, 0);
			}
		case T_SubqueryScan:
			{
				SubqueryScanPath *spath = (SubqueryScanPath *) path;

				return (Path *) create_subqueryscan_path(root,
														 rel,
														 spath->subpath,
														 spath->path.pathkeys,
														 required_outer);
			}
		case T_Result:
			/* Supported only for RTE_RESULT scan paths */
			if (IsA(path, Path))
				return create_resultscan_path(root, rel, required_outer);
			break;
		case T_Append:
			{
				AppendPath *apath = (AppendPath *) path;
				List	   *childpaths = NIL;
				List	   *partialpaths = NIL;
				int			i;
				ListCell   *lc;

				/* Reparameterize the children */
				i = 0;
				foreach(lc, apath->subpaths)
				{
					Path	   *spath = (Path *) lfirst(lc);

					spath = reparameterize_path(root, spath,
												required_outer,
												loop_count);
					if (spath == NULL)
						return NULL;
					/* We have to re-split the regular and partial paths */
					if (i < apath->first_partial_path)
						childpaths = lappend(childpaths, spath);
					else
						partialpaths = lappend(partialpaths, spath);
					i++;
				}
				return (Path *)
					create_append_path(root, rel, childpaths, partialpaths,
									   apath->path.pathkeys, required_outer,
									   apath->path.parallel_workers,
									   apath->path.parallel_aware,
									   -1);
			}
		case T_Memoize:
			{
				MemoizePath *mpath = (MemoizePath *) path;
				Path	   *spath = mpath->subpath;

				spath = reparameterize_path(root, spath,
											required_outer,
											loop_count);
				if (spath == NULL)
					return NULL;
				return (Path *) create_memoize_path(root, rel,
													spath,
													mpath->param_exprs,
													mpath->hash_operators,
													mpath->singlerow,
													mpath->binary_mode,
													mpath->calls);
			}
		default:
			break;
	}
	return NULL;
}

/*
 * reparameterize_path_by_child
 * 		Given a path parameterized by the parent of the given child relation,
 * 		translate the path to be parameterized by the given child relation.
 *
 * The function creates a new path of the same type as the given path, but
 * parameterized by the given child relation.  Most fields from the original
 * path can simply be flat-copied, but any expressions must be adjusted to
 * refer to the correct varnos, and any paths must be recursively
 * reparameterized.  Other fields that refer to specific relids also need
 * adjustment.
 *
 * The cost, number of rows, width and parallel path properties depend upon
 * path->parent, which does not change during the translation. Hence those
 * members are copied as they are.
 *
 * If the given path can not be reparameterized, the function returns NULL.
 */
Path *
reparameterize_path_by_child(PlannerInfo *root, Path *path,
							 RelOptInfo *child_rel)
{

#define FLAT_COPY_PATH(newnode, node, nodetype)  \
	( (newnode) = makeNode(nodetype), \
	  memcpy((newnode), (node), sizeof(nodetype)) )

#define ADJUST_CHILD_ATTRS(node) \
	((node) = \
	 (List *) adjust_appendrel_attrs_multilevel(root, (Node *) (node), \
												child_rel->relids, \
												child_rel->top_parent_relids))

#define REPARAMETERIZE_CHILD_PATH(path) \
do { \
	(path) = reparameterize_path_by_child(root, (path), child_rel); \
	if ((path) == NULL) \
		return NULL; \
} while(0)

#define REPARAMETERIZE_CHILD_PATH_LIST(pathlist) \
do { \
	if ((pathlist) != NIL) \
	{ \
		(pathlist) = reparameterize_pathlist_by_child(root, (pathlist), \
													  child_rel); \
		if ((pathlist) == NIL) \
			return NULL; \
	} \
} while(0)

	Path	   *new_path;
	ParamPathInfo *new_ppi;
	ParamPathInfo *old_ppi;
	Relids		required_outer;

	/*
	 * If the path is not parameterized by parent of the given relation, it
	 * doesn't need reparameterization.
	 */
	if (!path->param_info ||
		!bms_overlap(PATH_REQ_OUTER(path), child_rel->top_parent_relids))
		return path;

	/*
	 * If possible, reparameterize the given path, making a copy.
	 *
	 * This function is currently only applied to the inner side of a nestloop
	 * join that is being partitioned by the partitionwise-join code.  Hence,
	 * we need only support path types that plausibly arise in that context.
	 * (In particular, supporting sorted path types would be a waste of code
	 * and cycles: even if we translated them here, they'd just lose in
	 * subsequent cost comparisons.)  If we do see an unsupported path type,
	 * that just means we won't be able to generate a partitionwise-join plan
	 * using that path type.
	 */
	switch (nodeTag(path))
	{
		case T_Path:
			FLAT_COPY_PATH(new_path, path, Path);
			break;

		case T_IndexPath:
			{
				IndexPath  *ipath;

				FLAT_COPY_PATH(ipath, path, IndexPath);
				ADJUST_CHILD_ATTRS(ipath->indexclauses);
				new_path = (Path *) ipath;
			}
			break;

		case T_BitmapHeapPath:
			{
				BitmapHeapPath *bhpath;

				FLAT_COPY_PATH(bhpath, path, BitmapHeapPath);
				REPARAMETERIZE_CHILD_PATH(bhpath->bitmapqual);
				new_path = (Path *) bhpath;
			}
			break;

		case T_YbBitmapTablePath:
			{
				YbBitmapTablePath *bhpath;

				FLAT_COPY_PATH(bhpath, path, YbBitmapTablePath);
				REPARAMETERIZE_CHILD_PATH(bhpath->bitmapqual);
				new_path = (Path *) bhpath;
			}
			break;

		case T_BitmapAndPath:
			{
				BitmapAndPath *bapath;

				FLAT_COPY_PATH(bapath, path, BitmapAndPath);
				REPARAMETERIZE_CHILD_PATH_LIST(bapath->bitmapquals);
				new_path = (Path *) bapath;
			}
			break;

		case T_BitmapOrPath:
			{
				BitmapOrPath *bopath;

				FLAT_COPY_PATH(bopath, path, BitmapOrPath);
				REPARAMETERIZE_CHILD_PATH_LIST(bopath->bitmapquals);
				new_path = (Path *) bopath;
			}
			break;

		case T_ForeignPath:
			{
				ForeignPath *fpath;
				ReparameterizeForeignPathByChild_function rfpc_func;

				FLAT_COPY_PATH(fpath, path, ForeignPath);
				if (fpath->fdw_outerpath)
					REPARAMETERIZE_CHILD_PATH(fpath->fdw_outerpath);

				/* Hand over to FDW if needed. */
				rfpc_func =
					path->parent->fdwroutine->ReparameterizeForeignPathByChild;
				if (rfpc_func)
					fpath->fdw_private = rfpc_func(root, fpath->fdw_private,
												   child_rel);
				new_path = (Path *) fpath;
			}
			break;

		case T_CustomPath:
			{
				CustomPath *cpath;

				FLAT_COPY_PATH(cpath, path, CustomPath);
				REPARAMETERIZE_CHILD_PATH_LIST(cpath->custom_paths);
				if (cpath->methods &&
					cpath->methods->ReparameterizeCustomPathByChild)
					cpath->custom_private =
						cpath->methods->ReparameterizeCustomPathByChild(root,
																		cpath->custom_private,
																		child_rel);
				new_path = (Path *) cpath;
			}
			break;

		case T_NestPath:
			{
				JoinPath   *jpath;
				NestPath   *npath;

				FLAT_COPY_PATH(npath, path, NestPath);

				jpath = (JoinPath *) npath;
				REPARAMETERIZE_CHILD_PATH(jpath->outerjoinpath);
				REPARAMETERIZE_CHILD_PATH(jpath->innerjoinpath);
				ADJUST_CHILD_ATTRS(jpath->joinrestrictinfo);
				new_path = (Path *) npath;
			}
			break;

		case T_MergePath:
			{
				JoinPath   *jpath;
				MergePath  *mpath;

				FLAT_COPY_PATH(mpath, path, MergePath);

				jpath = (JoinPath *) mpath;
				REPARAMETERIZE_CHILD_PATH(jpath->outerjoinpath);
				REPARAMETERIZE_CHILD_PATH(jpath->innerjoinpath);
				ADJUST_CHILD_ATTRS(jpath->joinrestrictinfo);
				ADJUST_CHILD_ATTRS(mpath->path_mergeclauses);
				new_path = (Path *) mpath;
			}
			break;

		case T_HashPath:
			{
				JoinPath   *jpath;
				HashPath   *hpath;

				FLAT_COPY_PATH(hpath, path, HashPath);

				jpath = (JoinPath *) hpath;
				REPARAMETERIZE_CHILD_PATH(jpath->outerjoinpath);
				REPARAMETERIZE_CHILD_PATH(jpath->innerjoinpath);
				ADJUST_CHILD_ATTRS(jpath->joinrestrictinfo);
				ADJUST_CHILD_ATTRS(hpath->path_hashclauses);
				new_path = (Path *) hpath;
			}
			break;

		case T_AppendPath:
			{
				AppendPath *apath;

				FLAT_COPY_PATH(apath, path, AppendPath);
				REPARAMETERIZE_CHILD_PATH_LIST(apath->subpaths);
				new_path = (Path *) apath;
			}
			break;

		case T_MemoizePath:
			{
				MemoizePath *mpath;

				FLAT_COPY_PATH(mpath, path, MemoizePath);
				REPARAMETERIZE_CHILD_PATH(mpath->subpath);
				ADJUST_CHILD_ATTRS(mpath->param_exprs);
				new_path = (Path *) mpath;
			}
			break;

		case T_GatherPath:
			{
				GatherPath *gpath;

				FLAT_COPY_PATH(gpath, path, GatherPath);
				REPARAMETERIZE_CHILD_PATH(gpath->subpath);
				new_path = (Path *) gpath;
			}
			break;

		default:

			/* We don't know how to reparameterize this path. */
			return NULL;
	}

	/*
	 * Adjust the parameterization information, which refers to the topmost
	 * parent. The topmost parent can be multiple levels away from the given
	 * child, hence use multi-level expression adjustment routines.
	 */
	old_ppi = new_path->param_info;
	required_outer =
		adjust_child_relids_multilevel(root, old_ppi->ppi_req_outer,
									   child_rel->relids,
									   child_rel->top_parent_relids);

	/* If we already have a PPI for this parameterization, just return it */
	new_ppi = find_param_path_info(new_path->parent, required_outer);

	/*
	 * If not, build a new one and link it to the list of PPIs. For the same
	 * reason as explained in mark_dummy_rel(), allocate new PPI in the same
	 * context the given RelOptInfo is in.
	 */
	if (new_ppi == NULL)
	{
		MemoryContext oldcontext;
		RelOptInfo *rel = path->parent;

		oldcontext = MemoryContextSwitchTo(GetMemoryChunkContext(rel));

		new_ppi = makeNode(ParamPathInfo);
		new_ppi->ppi_req_outer = bms_copy(required_outer);
		new_ppi->ppi_rows = old_ppi->ppi_rows;
		new_ppi->ppi_clauses = old_ppi->ppi_clauses;
		ADJUST_CHILD_ATTRS(new_ppi->ppi_clauses);
		rel->ppilist = lappend(rel->ppilist, new_ppi);

		MemoryContextSwitchTo(oldcontext);
	}
	bms_free(required_outer);

	new_path->param_info = new_ppi;

	/*
	 * Adjust the path target if the parent of the outer relation is
	 * referenced in the targetlist. This can happen when only the parent of
	 * outer relation is laterally referenced in this relation.
	 */
	if (bms_overlap(path->parent->lateral_relids,
					child_rel->top_parent_relids))
	{
		new_path->pathtarget = copy_pathtarget(new_path->pathtarget);
		ADJUST_CHILD_ATTRS(new_path->pathtarget->exprs);
	}

	return new_path;
}

/*
 * reparameterize_pathlist_by_child
 * 		Helper function to reparameterize a list of paths by given child rel.
 */
static List *
reparameterize_pathlist_by_child(PlannerInfo *root,
								 List *pathlist,
								 RelOptInfo *child_rel)
{
	ListCell   *lc;
	List	   *result = NIL;

	foreach(lc, pathlist)
	{
		Path	   *path = reparameterize_path_by_child(root, lfirst(lc),
														child_rel);

		if (path == NULL)
		{
			list_free(result);
			return NIL;
		}

		result = lappend(result, path);
	}

	return result;
}

/*
 * YB: yb_create_unique_path
 *
 * Reuses create_upper_unique_path since that path already provides most of
 * the functionality required.
 */
static UpperUniquePath *
yb_create_unique_path(PlannerInfo *root,
					  RelOptInfo *rel,
					  Path *subpath,
					  int numCols,
					  double numGroups)
{
	UpperUniquePath *pathnode;

	pathnode = create_upper_unique_path(root, rel, subpath, numCols, numGroups);
	/*
	 * create_upper_unique_path does not copy param info since it assumes
	 * that join paths are all already created.
	 * Cannot make that assumption here since this is not an upper path.
	 * XXX: Hopefully, no other such assumptions were made.
	 */
	pathnode->path.param_info = subpath->param_info;
	/* Typically there aren't many duplicate values. */
	pathnode->path.total_cost = subpath->total_cost + cpu_operator_cost;
	return pathnode;
}

/*
 * YB: yb_create_distinct_index_path
 *
 * A distinct index scan fetches distinct values of the index's prefix. A
 * prefix is a list of leading columns of the 'index' that we want to be
 * distinct.
 *
 * Creating a distinct index scan path is similar to creating a regular index
 * scan path. For this reason, we copy the path 'basepath' and modify it as
 * necessary. We make the following modifications:
 *
 * Prefix Length
 * =============
 * 'yb_distinct_prefixlen' represents the minimal number of columns that can
 * cover the necessary distinct key columns for the scan. We choose a minimal
 * prefix since shorter prefixes are more efficient. This also means that we
 * want to exclude trailing columns that are constant from the prefix. Constants
 * have two key properties that make this possible.
 *
 * a. Constants are included in index clauses. For example, if r2 is equal to
 * 1, r2 = 1 is always an index clause. This means that the clause always seeps
 * past the DISTINCT pushdown operation on the DocDB side. Here, r2 is a range
 * column of the index.
 *
 * b. On top of that, there is at most one distinct value of a constant, i.e.
 * there exists at most one distinct tuple that satisfies the constant
 * constraint for each distinct tuple of other columns. This means that the
 * constant column need not be included in the prefix because any tuple
 * returned by the distinct index scan must satisfy the index conditions.
 * Constant hash columns do not make index conditions.
 * On the other hand, this property does not necessarily hold for other index
 * conditions, say IN queries. For example, a constraint such as r2 IN (0, 1)
 * cannot eliminate r2 from the prefix since there can be two distinct values of
 * r2 that satisfy the constraint. And distinct values of r1 cannot pick up
 * both values of r2. Here, r1 and r2 are leading range columns of the index.
 *
 * Furthermore, DocDB requires that the prefix length be at least 1 in
 * range-partitioned tables and at least the number of hash columns in hash
 * partitioned tables. If the prefix length is zero because all the columns
 * are constant, we stick a unique node on top of the path to pick at most
 * one tuple from the scan.
 *
 * Cost
 * ====
 * Distinct index scans are so useful because they are retrieved efficiently
 * and pull fewer tuples from the underlying storage. Hence, we need to adjust
 * the cost accordingly. For that, we first estimate the number of distinct
 * tuples that will be returned by the prefix and then scale down the cost of
 * the path by the selectivity of the scan.
 *
 * Unique Node
 * ===========
 * We discussed a few scenarios where we add a Unique node on top when all
 * the columns are constant. However, we also require one when the
 * distinct index scan itself may return duplicate values. This can happen when
 * the table is range-partitioned. A distinct index scan only removes duplicate
 * values within a tablet. See the long comment inside the function for
 * examples and further details.
 *
 * Uniqkeys
 * ========
 * Uniqkeys represents the collective set of expressions that is distinct for
 * the distinct index scan. These keys are pivotal to prove whether the
 * distinct index scan is distinct enough for the query. The set of uniqkeys
 * must include all trailing constant columns even though they are not part of
 * the prefix, because all the unique columns are necessary to prove that the
 * keys required by the query are distinct. On the other hand, when all the
 * columns are constant, we do not include the leading column even though it is
 * part of the prefix, since the unique node on top ensures that the constant
 * columns are distinct.
 *
 * Here, we use a separate set of keys instead of using pathkeys directly.
 * DISTINCT possesses some key properties that makes such an approach
 * attractive.
 *
 * First, DISTINCT on a superset of distinct keys requested by the query
 * produces at least all the required data for the final result.
 * Example: SELECT DISTINCT r1, r2 includes all the rows produced by
 * 			SELECT DISTINCT r2
 * On the other hand, only prefixes of sort keys can be assumed sorted.
 * Example: When tuples are sorted by r1, r2, they are also sorted by r1
 * 			but not r2.
 * This difference has an important implication: prefix based distinct index
 * scans are not just useful for DISTINCT operations on prefixes but also
 * arbitrary subsets of index key columns.
 *
 * Second, DISTINCT can permute its columns without changing the result.
 * Example: Tuples ordered by r1, r2 are not equivalent to tuples ordered by
 * r2, r1. However, DISTINCT r1, r2 is equivalent to DISTINCT r2, r1. The
 * columns simply have to be rearranged.
 * Having a separate list of keys lets us avoid being held back by pathkeys
 * machinery that prevents us from making such inferences.
 *
 * Third, DISTINCT can distribute more easily than sort.
 * Example: DISTINCT t1.r, t2.r FROM t1, t2 is, in many cases, same as
 * 			(DISTINCT r FROM t1), (DISTINCT r FROM t2)
 * Unlike pathkeys, uniqkeys can be propagated across joins using a union
 * of the uniqkeys of the constituent relations (bar some exceptions).
 *
 * 'index' is the index on which the distinct index scan is performed.
 * 'basepath' is the index scan path that is being modified to perform a
 * 			distinct index scan. The path is copied before modification.
 * 'yb_distinct_prefixlen' is the prefix length, in columns, of the distinct
 * 			index scan. This value is sent to DocDB as a scan parameter.
 * 'yb_distinct_nkeys' is the number of pathkeys corresponding to the distinct
 * 			prefix.
 *
 * Returns a polymorphic path.
 * - either a bare distinct index scan path
 * - or an UpperUniquePath on top of a distinct index scan path
 */
Path *
yb_create_distinct_index_path(PlannerInfo *root,
							  IndexOptInfo *index,
							  IndexPath *basepath,
							  int yb_distinct_prefixlen,
							  int yb_distinct_nkeys)
{
	IndexPath  *pathnode = makeNode(IndexPath);
	int			numDistinctRows;
	bool		ignore_prefix_for_uniqkeys;
	List	   *prefixExprs;
	ListCell   *lc;
	int			i;
	Selectivity selectivity;
	double		run_cost = 0;

	/*
	 * XXX: Memcpy'ing the index scan path the same way it is done in the
	 * reparameterize_path function.
	 */
	memcpy(pathnode, basepath, sizeof(IndexPath));

	/*
	 * Adjust prefix length appropriately.
	 * Prefix length must be at least max(1, index->nhashcolumns).
	 * Input prefix length is zero => all referenced columns are constant.
	 */
	Assert(yb_distinct_prefixlen >= 0);
	ignore_prefix_for_uniqkeys = false;
	if (yb_distinct_prefixlen == 0)
	{
		Assert(index->nhashcolumns > 0 || yb_distinct_nkeys == 0);
		yb_distinct_prefixlen = 1;
		ignore_prefix_for_uniqkeys = true;
	}
	if (yb_distinct_prefixlen < index->nhashcolumns)
		yb_distinct_prefixlen = index->nhashcolumns;
	pathnode->yb_index_path_info.yb_distinct_prefixlen = yb_distinct_prefixlen;

	/*
	 * Compute the set of uniqkeys.
	 * Ignore prefix when all columns are constant.
	 */
	pathnode->path.yb_path_info.yb_uniqkeys =
		yb_get_uniqkeys(index,
						ignore_prefix_for_uniqkeys ? 0 : yb_distinct_prefixlen);

	/* Estimate cost. */
	prefixExprs = NIL;
	i = 0;
	foreach(lc, index->indextlist)
	{
		TargetEntry *tle;

		if (i >= yb_distinct_prefixlen)
			break;

		tle = (TargetEntry *) lfirst(lc);
		prefixExprs = lappend(prefixExprs, tle->expr);
		i++;
	}
	pathnode->path.rows = clamp_row_est(pathnode->path.rows);
	numDistinctRows = estimate_num_groups(root,
										  prefixExprs,
										  pathnode->path.rows,
										  NULL,
										  NULL);
	selectivity = ((Cost) numDistinctRows) / ((Cost) pathnode->path.rows);

	run_cost = pathnode->path.total_cost - pathnode->path.startup_cost;
	run_cost *= selectivity;
	pathnode->path.total_cost = pathnode->path.startup_cost + run_cost;
	pathnode->path.rows = numDistinctRows;
	pathnode->indextotalcost *= selectivity;
	pathnode->indexselectivity *= selectivity;

	Assert(yb_distinct_prefixlen >= index->nhashcolumns);
	/*
	 * DocDB may return duplicate rows from different tablets.
	 * So, attach an upper unique node in that case.
	 *
	 * The decision to stick a Unique node is subtler than it looks.
	 * Here are a few examples for further understanding.
	 * h = hash column, r = range column.
	 *
	 * 1. SELECT DISTINCT r2
	 * 	  This is an example where a distinct index scan works well but
	 * 	  still insufficient. The planner further DISTINCT'ifies the column
	 * 	  using either sort or agg methods.
	 * 	  As a consequence, a Unique node on top is not very helpful.
	 * 	  More importantly, the pathkey corresponding to r2 is not part of
	 * 	  this pathnode's pathkeys since column r2 is not a prefix.
	 *
	 * 2. SELECT DISTINCT r1, r2, r3 WHERE r1 = r2
	 * 	  This is another tricky example. The prefix length here is 3 since
	 * 	  all the keys r1, r2, r3 must be DISTINCT. However, after filtering
	 * 	  r1 and r2 are the same, requiring the Unique node only DISTINCTify
	 * 	  r1 and r3. This is represented by a prefix of pathnode's pathkeys
	 * 	  corresponding to the DISTINCT prefix requested.
	 * 	  'yb_distinct_nkeys' represents precisely this.
	 *
	 * 3. SELECT DISTINCT h1, h2 WHERE h1 IN (0, 1) AND h2 IN (0, 1)
	 * 	  Easy case. YB's LSM indexes support IN clauses natively,
	 * 	  so the corresponding pathkeys are readily available.
	 * 	  However, do not stick a unique node on top since hash columns
	 * 	  seperate keys across the tablets cleanly unlike range columns.
	 *
	 * 4. SELECT DISTINCT h1, h2, r1
	 * 	  Almost easy. Even though the query selects a range column, a hash
	 * 	  prefix is sufficient to cleanly separate the keys.
	 * 	  Again, a Unique node is not necessary in this case.
	 *
	 * 5. SELECT DISTINCT r2 WHERE r2 = 1
	 * 	  yb_distinct_nkeys == 0. In this case all tuples are equal to 1.
	 * 	  Hence, 0 or 1 tuples are returned with a unique node on top.
	 *
	 * 6. SELECT DISTINCT h1, h2 WHERE h1 = 1 AND h2 = 1
	 * 	  No unique node necessary.
	 *
	 * Informal correctness argument:
	 * - There exists at least one hash column => No unique node necessary.
	 * 	 i.e. Unique Node => nhashcolumns == 0.
	 * - yb_distinct_nkeys < 0 => Keys missing from prefix.
	 * 	 At least one key missing from prefix => No unique node necessary
	 * 	 because the keys are not sufficiently distinct for the query anyway.
	 * 	 i.e. Unique Node => yb_distinct_nkeys >= 0.
	 * Hence, a unique node is unnecessary when there are hash columns or
	 * when some keys are missing from the prefix. For simplicity, the above
	 * argument excluded the degenerate case where all the referenced columns
	 * are constant, in which case we do add a unique node.
	 */
	if (index->nhashcolumns == 0)
	{
		/* Range partitioned */
		if (yb_distinct_nkeys >= 0)
			/* pathkeys available. Can use UpperUniquePath here. */
			return (Path *)
				yb_create_unique_path(root, index->rel, (Path *) pathnode,
									yb_distinct_nkeys, numDistinctRows);

		/*
		 * Unique path cannot be added on top => possible duplicate tuples
		 * => no uniqkeys.
		 */
		pathnode->path.yb_path_info.yb_uniqkeys = NIL;
	}

	return (Path *) pathnode;
}
