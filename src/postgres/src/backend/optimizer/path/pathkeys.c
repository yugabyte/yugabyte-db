/*-------------------------------------------------------------------------
 *
 * pathkeys.c
 *	  Utilities for matching and building path keys
 *
 * See src/backend/optimizer/README for a great deal of information about
 * the nature and use of path keys.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/optimizer/path/pathkeys.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/stratnum.h"
#include "access/sysattr.h"
#include "catalog/pg_opfamily.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/plannodes.h"
#include "optimizer/optimizer.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "partitioning/partbounds.h"
#include "utils/lsyscache.h"


static bool pathkey_is_redundant(PathKey *new_pathkey, List *pathkeys);
static bool matches_boolean_partition_clause(RestrictInfo *rinfo,
											 RelOptInfo *partrel,
											 int partkeycol);
static Var *find_var_for_subquery_tle(RelOptInfo *rel, TargetEntry *tle);
static bool right_merge_direction(PlannerInfo *root, PathKey *pathkey);


/****************************************************************************
 *		PATHKEY CONSTRUCTION AND REDUNDANCY TESTING
 ****************************************************************************/

/*
 * make_canonical_pathkey
 *	  Given the parameters for a PathKey, find any pre-existing matching
 *	  pathkey in the query's list of "canonical" pathkeys.  Make a new
 *	  entry if there's not one already.
 *
 * Note that this function must not be used until after we have completed
 * merging EquivalenceClasses.
 */
PathKey *
make_canonical_pathkey(PlannerInfo *root,
					   EquivalenceClass *eclass, Oid opfamily,
					   int strategy, bool nulls_first)
{
	PathKey    *pk;
	ListCell   *lc;
	MemoryContext oldcontext;

	/* Can't make canonical pathkeys if the set of ECs might still change */
	if (!root->ec_merging_done)
		elog(ERROR, "too soon to build canonical pathkeys");

	/* The passed eclass might be non-canonical, so chase up to the top */
	while (eclass->ec_merged)
		eclass = eclass->ec_merged;

	foreach(lc, root->canon_pathkeys)
	{
		pk = (PathKey *) lfirst(lc);
		if (eclass == pk->pk_eclass &&
			opfamily == pk->pk_opfamily &&
			strategy == pk->pk_strategy &&
			nulls_first == pk->pk_nulls_first)
			return pk;
	}

	/*
	 * Be sure canonical pathkeys are allocated in the main planning context.
	 * Not an issue in normal planning, but it is for GEQO.
	 */
	oldcontext = MemoryContextSwitchTo(root->planner_cxt);

	pk = makeNode(PathKey);
	pk->pk_eclass = eclass;
	pk->pk_opfamily = opfamily;
	pk->pk_strategy = strategy;
	pk->pk_nulls_first = nulls_first;

	root->canon_pathkeys = lappend(root->canon_pathkeys, pk);

	MemoryContextSwitchTo(oldcontext);

	return pk;
}

/*
 * pathkey_is_redundant
 *	   Is a pathkey redundant with one already in the given list?
 *
 * We detect two cases:
 *
 * 1. If the new pathkey's equivalence class contains a constant, and isn't
 * below an outer join, then we can disregard it as a sort key.  An example:
 *			SELECT ... WHERE x = 42 ORDER BY x, y;
 * We may as well just sort by y.  Note that because of opfamily matching,
 * this is semantically correct: we know that the equality constraint is one
 * that actually binds the variable to a single value in the terms of any
 * ordering operator that might go with the eclass.  This rule not only lets
 * us simplify (or even skip) explicit sorts, but also allows matching index
 * sort orders to a query when there are don't-care index columns.
 *
 * 2. If the new pathkey's equivalence class is the same as that of any
 * existing member of the pathkey list, then it is redundant.  Some examples:
 *			SELECT ... ORDER BY x, x;
 *			SELECT ... ORDER BY x, x DESC;
 *			SELECT ... WHERE x = y ORDER BY x, y;
 * In all these cases the second sort key cannot distinguish values that are
 * considered equal by the first, and so there's no point in using it.
 * Note in particular that we need not compare opfamily (all the opfamilies
 * of the EC have the same notion of equality) nor sort direction.
 *
 * Both the given pathkey and the list members must be canonical for this
 * to work properly, but that's okay since we no longer ever construct any
 * non-canonical pathkeys.  (Note: the notion of a pathkey *list* being
 * canonical includes the additional requirement of no redundant entries,
 * which is exactly what we are checking for here.)
 *
 * Because the equivclass.c machinery forms only one copy of any EC per query,
 * pointer comparison is enough to decide whether canonical ECs are the same.
 */
static bool
pathkey_is_redundant(PathKey *new_pathkey, List *pathkeys)
{
	EquivalenceClass *new_ec = new_pathkey->pk_eclass;
	ListCell   *lc;

	/* Check for EC containing a constant --- unconditionally redundant */
	if (EC_MUST_BE_REDUNDANT(new_ec))
		return true;

	/* If same EC already used in list, then redundant */
	foreach(lc, pathkeys)
	{
		PathKey    *old_pathkey = (PathKey *) lfirst(lc);

		if (new_ec == old_pathkey->pk_eclass)
			return true;
	}

	return false;
}

/*
 * make_pathkey_from_sortinfo
 *	  Given an expression and sort-order information, create a PathKey.
 *	  The result is always a "canonical" PathKey, but it might be redundant.
 *
 * expr is the expression, and nullable_relids is the set of base relids
 * that are potentially nullable below it.
 *
 * If the PathKey is being generated from a SortGroupClause, sortref should be
 * the SortGroupClause's SortGroupRef; otherwise zero.
 *
 * If rel is not NULL, it identifies a specific relation we're considering
 * a path for, and indicates that child EC members for that relation can be
 * considered.  Otherwise child members are ignored.  (See the comments for
 * get_eclass_for_sort_expr.)
 *
 * create_it is true if we should create any missing EquivalenceClass
 * needed to represent the sort key.  If it's false, we return NULL if the
 * sort key isn't already present in any EquivalenceClass.
 */
static PathKey *
make_pathkey_from_sortinfo(PlannerInfo *root,
						   Expr *expr,
						   Relids nullable_relids,
						   Oid opfamily,
						   Oid opcintype,
						   Oid collation,
						   bool reverse_sort,
						   bool nulls_first,
						   Index sortref,
						   Relids rel,
						   bool create_it,
						   bool is_hash_index)
{
	int16		strategy;
	Oid			equality_op;
	List	   *opfamilies;
	EquivalenceClass *eclass;

	if (is_hash_index) {
		/* We are picking a hash index. The strategy can only be BTEqualStrategyNumber */
		strategy = BTEqualStrategyNumber;
	} else {
		strategy = reverse_sort ? BTGreaterStrategyNumber : BTLessStrategyNumber;
	}

	/*
	 * EquivalenceClasses need to contain opfamily lists based on the family
	 * membership of mergejoinable equality operators, which could belong to
	 * more than one opfamily.  So we have to look up the opfamily's equality
	 * operator and get its membership.
	 */
	equality_op = get_opfamily_member(opfamily,
									  opcintype,
									  opcintype,
									  BTEqualStrategyNumber);
	if (!OidIsValid(equality_op))	/* shouldn't happen */
		elog(ERROR, "missing operator %d(%u,%u) in opfamily %u",
			 BTEqualStrategyNumber, opcintype, opcintype, opfamily);
	opfamilies = get_mergejoin_opfamilies(equality_op);
	if (!opfamilies)			/* certainly should find some */
		elog(ERROR, "could not find opfamilies for equality operator %u",
			 equality_op);

	/* Now find or (optionally) create a matching EquivalenceClass */
	eclass = get_eclass_for_sort_expr(root, expr, nullable_relids,
									  opfamilies, opcintype, collation,
									  sortref, rel, create_it);

	/* Fail if no EC and !create_it */
	if (!eclass)
		return NULL;

	/* This "eclass" is either a "=" or "sort" operator, and for hash_columns, we allow equality
	 * condition but not ASC or DESC sorting.
	 */
	if (is_hash_index && eclass->ec_sortref != 0) {
		return NULL;
	}

	/* And finally we can find or create a PathKey node */
	return make_canonical_pathkey(root, eclass, opfamily,
								  strategy, nulls_first);
}

/*
 * make_pathkey_from_sortop
 *	  Like make_pathkey_from_sortinfo, but work from a sort operator.
 *
 * This should eventually go away, but we need to restructure SortGroupClause
 * first.
 */
static PathKey *
make_pathkey_from_sortop(PlannerInfo *root,
						 Expr *expr,
						 Relids nullable_relids,
						 Oid ordering_op,
						 bool nulls_first,
						 Index sortref,
						 bool create_it)
{
	Oid			opfamily,
				opcintype,
				collation;
	int16		strategy;

	/* Find the operator in pg_amop --- failure shouldn't happen */
	if (!get_ordering_op_properties(ordering_op,
									&opfamily, &opcintype, &strategy))
		elog(ERROR, "operator %u is not a valid ordering operator",
			 ordering_op);

	/* Because SortGroupClause doesn't carry collation, consult the expr */
	collation = exprCollation((Node *) expr);

	return make_pathkey_from_sortinfo(root,
									  expr,
									  nullable_relids,
									  opfamily,
									  opcintype,
									  collation,
									  (strategy == BTGreaterStrategyNumber),
									  nulls_first,
									  sortref,
									  NULL,
									  create_it,
									  false);
}


/****************************************************************************
 *		PATHKEY COMPARISONS
 ****************************************************************************/

/*
 * compare_pathkeys
 *	  Compare two pathkeys to see if they are equivalent, and if not whether
 *	  one is "better" than the other.
 *
 *	  We assume the pathkeys are canonical, and so they can be checked for
 *	  equality by simple pointer comparison.
 */
PathKeysComparison
compare_pathkeys(List *keys1, List *keys2)
{
	ListCell   *key1,
			   *key2;

	/*
	 * Fall out quickly if we are passed two identical lists.  This mostly
	 * catches the case where both are NIL, but that's common enough to
	 * warrant the test.
	 */
	if (keys1 == keys2)
		return PATHKEYS_EQUAL;

	forboth(key1, keys1, key2, keys2)
	{
		PathKey    *pathkey1 = (PathKey *) lfirst(key1);
		PathKey    *pathkey2 = (PathKey *) lfirst(key2);

		if (pathkey1 != pathkey2)
			return PATHKEYS_DIFFERENT;	/* no need to keep looking */
	}

	/*
	 * If we reached the end of only one list, the other is longer and
	 * therefore not a subset.
	 */
	if (key1 != NULL)
		return PATHKEYS_BETTER1;	/* key1 is longer */
	if (key2 != NULL)
		return PATHKEYS_BETTER2;	/* key2 is longer */
	return PATHKEYS_EQUAL;
}

/*
 * pathkeys_contained_in
 *	  Common special case of compare_pathkeys: we just want to know
 *	  if keys2 are at least as well sorted as keys1.
 */
bool
pathkeys_contained_in(List *keys1, List *keys2)
{
	switch (compare_pathkeys(keys1, keys2))
	{
		case PATHKEYS_EQUAL:
		case PATHKEYS_BETTER2:
			return true;
		default:
			break;
	}
	return false;
}

/*
 * pathkeys_count_contained_in
 *    Same as pathkeys_contained_in, but also sets length of longest
 *    common prefix of keys1 and keys2.
 */
bool
pathkeys_count_contained_in(List *keys1, List *keys2, int *n_common)
{
	int			n = 0;
	ListCell   *key1,
			   *key2;

	/*
	 * See if we can avoiding looping through both lists. This optimization
	 * gains us several percent in planning time in a worst-case test.
	 */
	if (keys1 == keys2)
	{
		*n_common = list_length(keys1);
		return true;
	}
	else if (keys1 == NIL)
	{
		*n_common = 0;
		return true;
	}
	else if (keys2 == NIL)
	{
		*n_common = 0;
		return false;
	}

	/*
	 * If both lists are non-empty, iterate through both to find out how many
	 * items are shared.
	 */
	forboth(key1, keys1, key2, keys2)
	{
		PathKey    *pathkey1 = (PathKey *) lfirst(key1);
		PathKey    *pathkey2 = (PathKey *) lfirst(key2);

		if (pathkey1 != pathkey2)
		{
			*n_common = n;
			return false;
		}
		n++;
	}

	/* If we ended with a null value, then we've processed the whole list. */
	*n_common = n;
	return (key1 == NULL);
}

/*
 * get_cheapest_path_for_pathkeys
 *	  Find the cheapest path (according to the specified criterion) that
 *	  satisfies the given pathkeys and parameterization.
 *	  Return NULL if no such path.
 *
 * 'paths' is a list of possible paths that all generate the same relation
 * 'pathkeys' represents a required ordering (in canonical form!)
 * 'required_outer' denotes allowable outer relations for parameterized paths
 * 'cost_criterion' is STARTUP_COST or TOTAL_COST
 * 'require_parallel_safe' causes us to consider only parallel-safe paths
 */
Path *
get_cheapest_path_for_pathkeys(List *paths, List *pathkeys,
							   Relids required_outer,
							   CostSelector cost_criterion,
							   bool require_parallel_safe)
{
	Path	   *matched_path = NULL;
	ListCell   *l;

	foreach(l, paths)
	{
		Path	   *path = (Path *) lfirst(l);

		/*
		 * Since cost comparison is a lot cheaper than pathkey comparison, do
		 * that first.  (XXX is that still true?)
		 */
		if (matched_path != NULL &&
			compare_path_costs(matched_path, path, cost_criterion) <= 0)
			continue;

		if (require_parallel_safe && !path->parallel_safe)
			continue;

		if (matched_path != NULL && yb_prefer_bnl &&
			YB_PATH_NEEDS_BATCHED_RELS(matched_path)
			&& !YB_PATH_NEEDS_BATCHED_RELS(path))
			continue;

		if (pathkeys_contained_in(pathkeys, path->pathkeys) &&
			bms_is_subset(PATH_REQ_OUTER(path), required_outer))
			matched_path = path;
	}
	return matched_path;
}

/*
 * get_cheapest_fractional_path_for_pathkeys
 *	  Find the cheapest path (for retrieving a specified fraction of all
 *	  the tuples) that satisfies the given pathkeys and parameterization.
 *	  Return NULL if no such path.
 *
 * See compare_fractional_path_costs() for the interpretation of the fraction
 * parameter.
 *
 * 'paths' is a list of possible paths that all generate the same relation
 * 'pathkeys' represents a required ordering (in canonical form!)
 * 'required_outer' denotes allowable outer relations for parameterized paths
 * 'fraction' is the fraction of the total tuples expected to be retrieved
 */
Path *
get_cheapest_fractional_path_for_pathkeys(List *paths,
										  List *pathkeys,
										  Relids required_outer,
										  double fraction)
{
	Path	   *matched_path = NULL;
	ListCell   *l;

	foreach(l, paths)
	{
		Path	   *path = (Path *) lfirst(l);

		/*
		 * Since cost comparison is a lot cheaper than pathkey comparison, do
		 * that first.  (XXX is that still true?)
		 */
		if (matched_path != NULL &&
			compare_fractional_path_costs(matched_path, path, fraction) <= 0)
			continue;

		if (pathkeys_contained_in(pathkeys, path->pathkeys) &&
			bms_is_subset(PATH_REQ_OUTER(path), required_outer))
			matched_path = path;
	}
	return matched_path;
}


/*
 * get_cheapest_parallel_safe_total_inner
 *	  Find the unparameterized parallel-safe path with the least total cost.
 */
Path *
get_cheapest_parallel_safe_total_inner(List *paths)
{
	ListCell   *l;

	foreach(l, paths)
	{
		Path	   *innerpath = (Path *) lfirst(l);

		if (innerpath->parallel_safe &&
			bms_is_empty(PATH_REQ_OUTER(innerpath)))
			return innerpath;
	}

	return NULL;
}

/****************************************************************************
 *		NEW PATHKEY FORMATION
 ****************************************************************************/

/*
 * build_index_pathkeys
 *	  Build a pathkeys list that describes the ordering induced by an index
 *	  scan using the given index.  (Note that an unordered index doesn't
 *	  induce any ordering, so we return NIL.)
 *
 * If 'scandir' is BackwardScanDirection, build pathkeys representing a
 * backwards scan of the index.
 *
 * We iterate only key columns of covering indexes, since non-key columns
 * don't influence index ordering.  The result is canonical, meaning that
 * redundant pathkeys are removed; it may therefore have fewer entries than
 * there are key columns in the index.
 *
 * Another reason for stopping early is that we may be able to tell that
 * an index column's sort order is uninteresting for this query.  However,
 * that test is just based on the existence of an EquivalenceClass and not
 * on position in pathkey lists, so it's not complete.  Caller should call
 * truncate_useless_pathkeys() to possibly remove more pathkeys.
 *
 * YB: 'yb_distinct_nkeys' is an in-out param.
 * For distinct index scans, we require the set of PathKey's that correspond
 * to the distinct index prefix. This is necessary for de-duplicating some
 * edge cases in range partioned columns, see #18101 for more information.
 * Hence, the function takes the distinct prefix length as input in
 * 'yb_distinct_nkeys'. -1 if the index is hash-partitioned.
 * The function returns the set of pathkeys corresponding to the prefix
 * back again in 'yb_distinct_nkeys'. Since this set is a prefix, we need only
 * return the prefix length of returned pathkeys in 'yb_distinct_nkeys'.
 * This field is eventually used in generating a UpperUniquePath node.
 * Returns -1 in 'yb_distinct_nkeys' if the pathkeys cannot span the prefix.
 * Returns 0 in 'yb_distinct_nkeys' when the prefix is empty.
 */
List *
build_index_pathkeys(PlannerInfo *root,
					 IndexOptInfo *index,
					 ScanDirection scandir,
					 int *yb_distinct_nkeys)
{
	List	   *retval = NIL;
	ListCell   *lc;
	int			i;
	int			yb_distinct_prefixlen;

	if (index->sortopfamily == NULL)
		return NIL;				/* non-orderable index */

	i = 0;
	/*
	 * YB: Compute the set of pathkeys corresponding to the distinct index scan
	 * prefix. 0 when the prefix is empty.
	 */
	yb_distinct_prefixlen = *yb_distinct_nkeys;
	*yb_distinct_nkeys = yb_distinct_prefixlen == 0 ? 0 : -1;
	foreach(lc, index->indextlist)
	{
		TargetEntry *indextle = (TargetEntry *) lfirst(lc);
		Expr	   *indexkey;
		bool		reverse_sort;
		bool		nulls_first;
		PathKey    *cpathkey;

		/*
		 * INCLUDE columns are stored in index unordered, so they don't
		 * support ordered index scan.
		 */
		if (i >= index->nkeycolumns)
			break;

		/* We assume we don't need to make a copy of the tlist item */
		indexkey = indextle->expr;

		if (ScanDirectionIsBackward(scandir))
		{
			reverse_sort = !index->reverse_sort[i];
			nulls_first = !index->nulls_first[i];
		}
		else
		{
			reverse_sort = index->reverse_sort[i];
			nulls_first = index->nulls_first[i];
		}

		/*
		 * OK, try to make a canonical pathkey for this sort key.  Note we're
		 * underneath any outer joins, so nullable_relids should be NULL.
		 */
		cpathkey = make_pathkey_from_sortinfo(root,
											  indexkey,
											  NULL,
											  index->sortopfamily[i],
											  index->opcintype[i],
											  index->indexcollations[i],
											  reverse_sort,
											  nulls_first,
											  0,
											  index->rel->relids,
											  false,
											  i < index->nhashcolumns);

		if (cpathkey)
		{
			/*
			 * We found the sort key in an EquivalenceClass, so it's relevant
			 * for this query.  Add it to list, unless it's redundant.
			 */
			if (!pathkey_is_redundant(cpathkey, retval))
				retval = lappend(retval, cpathkey);
		}
		else
		{
			/*
			 * Boolean index keys might be redundant even if they do not
			 * appear in an EquivalenceClass, because of our special treatment
			 * of boolean equality conditions --- see the comment for
			 * indexcol_is_bool_constant_for_query().  If that applies, we can
			 * continue to examine lower-order index columns.  Otherwise, the
			 * sort key is not an interesting sort order for this query, so we
			 * should stop considering index columns; any lower-order sort
			 * keys won't be useful either.
			 */
			if (!indexcol_is_bool_constant_for_query(root, index, i) || i < index->nhashcolumns)
				break;
		}

		i++;
		/* YB: For later use in creating a UpperUniquePath node. */
		if (i == yb_distinct_prefixlen)
			*yb_distinct_nkeys = list_length(retval);
	}

	/*
	 * YB: Broadly, index paths are generated either for ordering, index
	 * access via predicates supported by the index, or for fetching distinct
	 * tuples from the index.
	 * Hash columns are not used for ordering, however.
	 * To use the index, there must be an index clause on each hash column.
	 * The check below prevents hash columns being used for ordering.
	 *
	 * For the purposes of distinct index scans,
	 * return pathkeys only when all hash columns are requested to be distinct.
	 * Otherwise, while it may still be useful to generate a
	 * distinct index scan, that scan alone may still have duplicate values.
	 * Hence, we return no pathkeys since the result is not actually distinct.
	 *
	 * Example: DISTINCT h1 (both h1 and h2 are hash columns).
	 * We can request a distinct index scan on h1, h2 tuples but there may still
	 * be some duplicate values of h1 in the result.
	 */
	if (i < index->nhashcolumns)
		/* All hash columns must have EQ pathkeys. Otherwise, we cannot use the index */
		return NULL;
	return retval;
}

/*
 * partkey_is_bool_constant_for_query
 *
 * If a partition key column is constrained to have a constant value by the
 * query's WHERE conditions, then it's irrelevant for sort-order
 * considerations.  Usually that means we have a restriction clause
 * WHERE partkeycol = constant, which gets turned into an EquivalenceClass
 * containing a constant, which is recognized as redundant by
 * build_partition_pathkeys().  But if the partition key column is a
 * boolean variable (or expression), then we are not going to see such a
 * WHERE clause, because expression preprocessing will have simplified it
 * to "WHERE partkeycol" or "WHERE NOT partkeycol".  So we are not going
 * to have a matching EquivalenceClass (unless the query also contains
 * "ORDER BY partkeycol").  To allow such cases to work the same as they would
 * for non-boolean values, this function is provided to detect whether the
 * specified partition key column matches a boolean restriction clause.
 */
static bool
partkey_is_bool_constant_for_query(RelOptInfo *partrel, int partkeycol)
{
	PartitionScheme partscheme = partrel->part_scheme;
	ListCell   *lc;

	/* If the partkey isn't boolean, we can't possibly get a match */
	if (!IsBooleanOpfamily(partscheme->partopfamily[partkeycol]))
		return false;

	/* Check each restriction clause for the partitioned rel */
	foreach(lc, partrel->baserestrictinfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		/* Ignore pseudoconstant quals, they won't match */
		if (rinfo->pseudoconstant)
			continue;

		/* See if we can match the clause's expression to the partkey column */
		if (matches_boolean_partition_clause(rinfo, partrel, partkeycol))
			return true;
	}

	return false;
}

/*
 * matches_boolean_partition_clause
 *		Determine if the boolean clause described by rinfo matches
 *		partrel's partkeycol-th partition key column.
 *
 * "Matches" can be either an exact match (equivalent to partkey = true),
 * or a NOT above an exact match (equivalent to partkey = false).
 */
static bool
matches_boolean_partition_clause(RestrictInfo *rinfo,
								 RelOptInfo *partrel, int partkeycol)
{
	Node	   *clause = (Node *) rinfo->clause;
	Node	   *partexpr = (Node *) linitial(partrel->partexprs[partkeycol]);

	/* Direct match? */
	if (equal(partexpr, clause))
		return true;
	/* NOT clause? */
	else if (is_notclause(clause))
	{
		Node	   *arg = (Node *) get_notclausearg((Expr *) clause);

		if (equal(partexpr, arg))
			return true;
	}

	return false;
}

/*
 * build_partition_pathkeys
 *	  Build a pathkeys list that describes the ordering induced by the
 *	  partitions of partrel, under either forward or backward scan
 *	  as per scandir.
 *
 * Caller must have checked that the partitions are properly ordered,
 * as detected by partitions_are_ordered().
 *
 * Sets *partialkeys to true if pathkeys were only built for a prefix of the
 * partition key, or false if the pathkeys include all columns of the
 * partition key.
 */
List *
build_partition_pathkeys(PlannerInfo *root, RelOptInfo *partrel,
						 ScanDirection scandir, bool *partialkeys)
{
	List	   *retval = NIL;
	PartitionScheme partscheme = partrel->part_scheme;
	int			i;

	Assert(partscheme != NULL);
	Assert(partitions_are_ordered(partrel->boundinfo, partrel->live_parts));
	/* For now, we can only cope with baserels */
	Assert(IS_SIMPLE_REL(partrel));

	for (i = 0; i < partscheme->partnatts; i++)
	{
		PathKey    *cpathkey;
		Expr	   *keyCol = (Expr *) linitial(partrel->partexprs[i]);

		/*
		 * Try to make a canonical pathkey for this partkey.
		 *
		 * We're considering a baserel scan, so nullable_relids should be
		 * NULL.  Also, we assume the PartitionDesc lists any NULL partition
		 * last, so we treat the scan like a NULLS LAST index: we have
		 * nulls_first for backwards scan only.
		 */
		cpathkey = make_pathkey_from_sortinfo(root,
											  keyCol,
											  NULL,
											  partscheme->partopfamily[i],
											  partscheme->partopcintype[i],
											  partscheme->partcollation[i],
											  ScanDirectionIsBackward(scandir),
											  ScanDirectionIsBackward(scandir),
											  0,
											  partrel->relids,
											  false,
											  false);


		if (cpathkey)
		{
			/*
			 * We found the sort key in an EquivalenceClass, so it's relevant
			 * for this query.  Add it to list, unless it's redundant.
			 */
			if (!pathkey_is_redundant(cpathkey, retval))
				retval = lappend(retval, cpathkey);
		}
		else
		{
			/*
			 * Boolean partition keys might be redundant even if they do not
			 * appear in an EquivalenceClass, because of our special treatment
			 * of boolean equality conditions --- see the comment for
			 * partkey_is_bool_constant_for_query().  If that applies, we can
			 * continue to examine lower-order partition keys.  Otherwise, the
			 * sort key is not an interesting sort order for this query, so we
			 * should stop considering partition columns; any lower-order sort
			 * keys won't be useful either.
			 */
			if (!partkey_is_bool_constant_for_query(partrel, i))
			{
				*partialkeys = true;
				return retval;
			}
		}
	}

	*partialkeys = false;
	return retval;
}

/*
 * build_expression_pathkey
 *	  Build a pathkeys list that describes an ordering by a single expression
 *	  using the given sort operator.
 *
 * expr, nullable_relids, and rel are as for make_pathkey_from_sortinfo.
 * We induce the other arguments assuming default sort order for the operator.
 *
 * Similarly to make_pathkey_from_sortinfo, the result is NIL if create_it
 * is false and the expression isn't already in some EquivalenceClass.
 */
List *
build_expression_pathkey(PlannerInfo *root,
						 Expr *expr,
						 Relids nullable_relids,
						 Oid opno,
						 Relids rel,
						 bool create_it)
{
	List	   *pathkeys;
	Oid			opfamily,
				opcintype;
	int16		strategy;
	PathKey    *cpathkey;

	/* Find the operator in pg_amop --- failure shouldn't happen */
	if (!get_ordering_op_properties(opno,
									&opfamily, &opcintype, &strategy))
		elog(ERROR, "operator %u is not a valid ordering operator",
			 opno);

	cpathkey = make_pathkey_from_sortinfo(root,
										  expr,
										  nullable_relids,
										  opfamily,
										  opcintype,
										  exprCollation((Node *) expr),
										  (strategy == BTGreaterStrategyNumber),
										  (strategy == BTGreaterStrategyNumber),
										  0,
										  rel,
										  create_it,
										  false);

	if (cpathkey)
		pathkeys = list_make1(cpathkey);
	else
		pathkeys = NIL;

	return pathkeys;
}

/*
 * convert_subquery_pathkeys
 *	  Build a pathkeys list that describes the ordering of a subquery's
 *	  result, in the terms of the outer query.  This is essentially a
 *	  task of conversion.
 *
 * 'rel': outer query's RelOptInfo for the subquery relation.
 * 'subquery_pathkeys': the subquery's output pathkeys, in its terms.
 * 'subquery_tlist': the subquery's output targetlist, in its terms.
 *
 * We intentionally don't do truncate_useless_pathkeys() here, because there
 * are situations where seeing the raw ordering of the subquery is helpful.
 * For example, if it returns ORDER BY x DESC, that may prompt us to
 * construct a mergejoin using DESC order rather than ASC order; but the
 * right_merge_direction heuristic would have us throw the knowledge away.
 */
List *
convert_subquery_pathkeys(PlannerInfo *root, RelOptInfo *rel,
						  List *subquery_pathkeys,
						  List *subquery_tlist)
{
	List	   *retval = NIL;
	int			retvallen = 0;
	int			outer_query_keys = list_length(root->query_pathkeys);
	ListCell   *i;

	foreach(i, subquery_pathkeys)
	{
		PathKey    *sub_pathkey = (PathKey *) lfirst(i);
		EquivalenceClass *sub_eclass = sub_pathkey->pk_eclass;
		PathKey    *best_pathkey = NULL;

		if (sub_eclass->ec_has_volatile)
		{
			/*
			 * If the sub_pathkey's EquivalenceClass is volatile, then it must
			 * have come from an ORDER BY clause, and we have to match it to
			 * that same targetlist entry.
			 */
			TargetEntry *tle;
			Var		   *outer_var;

			if (sub_eclass->ec_sortref == 0)	/* can't happen */
				elog(ERROR, "volatile EquivalenceClass has no sortref");
			tle = get_sortgroupref_tle(sub_eclass->ec_sortref, subquery_tlist);
			Assert(tle);
			/* Is TLE actually available to the outer query? */
			outer_var = find_var_for_subquery_tle(rel, tle);
			if (outer_var)
			{
				/* We can represent this sub_pathkey */
				EquivalenceMember *sub_member;
				EquivalenceClass *outer_ec;

				Assert(list_length(sub_eclass->ec_members) == 1);
				sub_member = (EquivalenceMember *) linitial(sub_eclass->ec_members);

				/*
				 * Note: it might look funny to be setting sortref = 0 for a
				 * reference to a volatile sub_eclass.  However, the
				 * expression is *not* volatile in the outer query: it's just
				 * a Var referencing whatever the subquery emitted. (IOW, the
				 * outer query isn't going to re-execute the volatile
				 * expression itself.)	So this is okay.  Likewise, it's
				 * correct to pass nullable_relids = NULL, because we're
				 * underneath any outer joins appearing in the outer query.
				 */
				outer_ec =
					get_eclass_for_sort_expr(root,
											 (Expr *) outer_var,
											 NULL,
											 sub_eclass->ec_opfamilies,
											 sub_member->em_datatype,
											 sub_eclass->ec_collation,
											 0,
											 rel->relids,
											 false);

				/*
				 * If we don't find a matching EC, sub-pathkey isn't
				 * interesting to the outer query
				 */
				if (outer_ec)
					best_pathkey =
						make_canonical_pathkey(root,
											   outer_ec,
											   sub_pathkey->pk_opfamily,
											   sub_pathkey->pk_strategy,
											   sub_pathkey->pk_nulls_first);
			}
		}
		else
		{
			/*
			 * Otherwise, the sub_pathkey's EquivalenceClass could contain
			 * multiple elements (representing knowledge that multiple items
			 * are effectively equal).  Each element might match none, one, or
			 * more of the output columns that are visible to the outer query.
			 * This means we may have multiple possible representations of the
			 * sub_pathkey in the context of the outer query.  Ideally we
			 * would generate them all and put them all into an EC of the
			 * outer query, thereby propagating equality knowledge up to the
			 * outer query.  Right now we cannot do so, because the outer
			 * query's EquivalenceClasses are already frozen when this is
			 * called. Instead we prefer the one that has the highest "score"
			 * (number of EC peers, plus one if it matches the outer
			 * query_pathkeys). This is the most likely to be useful in the
			 * outer query.
			 */
			int			best_score = -1;
			ListCell   *j;

			foreach(j, sub_eclass->ec_members)
			{
				EquivalenceMember *sub_member = (EquivalenceMember *) lfirst(j);
				Expr	   *sub_expr = sub_member->em_expr;
				Oid			sub_expr_type = sub_member->em_datatype;
				Oid			sub_expr_coll = sub_eclass->ec_collation;
				ListCell   *k;

				if (sub_member->em_is_child)
					continue;	/* ignore children here */

				foreach(k, subquery_tlist)
				{
					TargetEntry *tle = (TargetEntry *) lfirst(k);
					Var		   *outer_var;
					Expr	   *tle_expr;
					EquivalenceClass *outer_ec;
					PathKey    *outer_pk;
					int			score;

					/* Is TLE actually available to the outer query? */
					outer_var = find_var_for_subquery_tle(rel, tle);
					if (!outer_var)
						continue;

					/*
					 * The targetlist entry is considered to match if it
					 * matches after sort-key canonicalization.  That is
					 * needed since the sub_expr has been through the same
					 * process.
					 */
					tle_expr = canonicalize_ec_expression(tle->expr,
														  sub_expr_type,
														  sub_expr_coll);
					if (!equal(tle_expr, sub_expr))
						continue;

					/* See if we have a matching EC for the TLE */
					outer_ec = get_eclass_for_sort_expr(root,
														(Expr *) outer_var,
														NULL,
														sub_eclass->ec_opfamilies,
														sub_expr_type,
														sub_expr_coll,
														0,
														rel->relids,
														false);

					/*
					 * If we don't find a matching EC, this sub-pathkey isn't
					 * interesting to the outer query
					 */
					if (!outer_ec)
						continue;

					outer_pk = make_canonical_pathkey(root,
													  outer_ec,
													  sub_pathkey->pk_opfamily,
													  sub_pathkey->pk_strategy,
													  sub_pathkey->pk_nulls_first);
					/* score = # of equivalence peers */
					score = list_length(outer_ec->ec_members) - 1;
					/* +1 if it matches the proper query_pathkeys item */
					if (retvallen < outer_query_keys &&
						list_nth(root->query_pathkeys, retvallen) == outer_pk)
						score++;
					if (score > best_score)
					{
						best_pathkey = outer_pk;
						best_score = score;
					}
				}
			}
		}

		/*
		 * If we couldn't find a representation of this sub_pathkey, we're
		 * done (we can't use the ones to its right, either).
		 */
		if (!best_pathkey)
			break;

		/*
		 * Eliminate redundant ordering info; could happen if outer query
		 * equivalences subquery keys...
		 */
		if (!pathkey_is_redundant(best_pathkey, retval))
		{
			retval = lappend(retval, best_pathkey);
			retvallen++;
		}
	}

	return retval;
}

/*
 * find_var_for_subquery_tle
 *
 * If the given subquery tlist entry is due to be emitted by the subquery's
 * scan node, return a Var for it, else return NULL.
 *
 * We need this to ensure that we don't return pathkeys describing values
 * that are unavailable above the level of the subquery scan.
 */
static Var *
find_var_for_subquery_tle(RelOptInfo *rel, TargetEntry *tle)
{
	ListCell   *lc;

	/* If the TLE is resjunk, it's certainly not visible to the outer query */
	if (tle->resjunk)
		return NULL;

	/* Search the rel's targetlist to see what it will return */
	foreach(lc, rel->reltarget->exprs)
	{
		Var		   *var = (Var *) lfirst(lc);

		/* Ignore placeholders */
		if (!IsA(var, Var))
			continue;
		Assert(var->varno == rel->relid);

		/* If we find a Var referencing this TLE, we're good */
		if (var->varattno == tle->resno)
			return copyObject(var); /* Make a copy for safety */
	}
	return NULL;
}

/*
 * build_join_pathkeys
 *	  Build the path keys for a join relation constructed by mergejoin or
 *	  nestloop join.  This is normally the same as the outer path's keys.
 *
 *	  EXCEPTION: in a FULL or RIGHT join, we cannot treat the result as
 *	  having the outer path's path keys, because null lefthand rows may be
 *	  inserted at random points.  It must be treated as unsorted.
 *
 *	  We truncate away any pathkeys that are uninteresting for higher joins.
 *
 * 'joinrel' is the join relation that paths are being formed for
 * 'jointype' is the join type (inner, left, full, etc)
 * 'outer_pathkeys' is the list of the current outer path's path keys
 *
 * Returns the list of new path keys.
 */
List *
build_join_pathkeys(PlannerInfo *root,
					RelOptInfo *joinrel,
					JoinType jointype,
					List *outer_pathkeys)
{
	if (jointype == JOIN_FULL || jointype == JOIN_RIGHT)
		return NIL;

	/*
	 * This used to be quite a complex bit of code, but now that all pathkey
	 * sublists start out life canonicalized, we don't have to do a darn thing
	 * here!
	 *
	 * We do, however, need to truncate the pathkeys list, since it may
	 * contain pathkeys that were useful for forming this joinrel but are
	 * uninteresting to higher levels.
	 */
	return truncate_useless_pathkeys(root, joinrel, outer_pathkeys, 0);
}

/****************************************************************************
 *		PATHKEYS AND SORT CLAUSES
 ****************************************************************************/

/*
 * make_pathkeys_for_sortclauses
 *		Generate a pathkeys list that represents the sort order specified
 *		by a list of SortGroupClauses
 *
 * The resulting PathKeys are always in canonical form.  (Actually, there
 * is no longer any code anywhere that creates non-canonical PathKeys.)
 *
 * We assume that root->nullable_baserels is the set of base relids that could
 * have gone to NULL below the SortGroupClause expressions.  This is okay if
 * the expressions came from the query's top level (ORDER BY, DISTINCT, etc)
 * and if this function is only invoked after deconstruct_jointree.  In the
 * future we might have to make callers pass in the appropriate
 * nullable-relids set, but for now it seems unnecessary.
 *
 * 'sortclauses' is a list of SortGroupClause nodes
 * 'tlist' is the targetlist to find the referenced tlist entries in
 */
List *
make_pathkeys_for_sortclauses(PlannerInfo *root,
							  List *sortclauses,
							  List *tlist)
{
	List	   *pathkeys = NIL;
	ListCell   *l;

	foreach(l, sortclauses)
	{
		SortGroupClause *sortcl = (SortGroupClause *) lfirst(l);
		Expr	   *sortkey;
		PathKey    *pathkey;

		sortkey = (Expr *) get_sortgroupclause_expr(sortcl, tlist);
		Assert(OidIsValid(sortcl->sortop));
		pathkey = make_pathkey_from_sortop(root,
										   sortkey,
										   root->nullable_baserels,
										   sortcl->sortop,
										   sortcl->nulls_first,
										   sortcl->tleSortGroupRef,
										   true);

		/* Canonical form eliminates redundant ordering keys */
		if (!pathkey_is_redundant(pathkey, pathkeys))
			pathkeys = lappend(pathkeys, pathkey);
	}
	return pathkeys;
}

/****************************************************************************
 *		PATHKEYS AND MERGECLAUSES
 ****************************************************************************/

/*
 * initialize_mergeclause_eclasses
 *		Set the EquivalenceClass links in a mergeclause restrictinfo.
 *
 * RestrictInfo contains fields in which we may cache pointers to
 * EquivalenceClasses for the left and right inputs of the mergeclause.
 * (If the mergeclause is a true equivalence clause these will be the
 * same EquivalenceClass, otherwise not.)  If the mergeclause is either
 * used to generate an EquivalenceClass, or derived from an EquivalenceClass,
 * then it's easy to set up the left_ec and right_ec members --- otherwise,
 * this function should be called to set them up.  We will generate new
 * EquivalenceClauses if necessary to represent the mergeclause's left and
 * right sides.
 *
 * Note this is called before EC merging is complete, so the links won't
 * necessarily point to canonical ECs.  Before they are actually used for
 * anything, update_mergeclause_eclasses must be called to ensure that
 * they've been updated to point to canonical ECs.
 */
void
initialize_mergeclause_eclasses(PlannerInfo *root, RestrictInfo *restrictinfo)
{
	Expr	   *clause = restrictinfo->clause;
	Oid			lefttype,
				righttype;

	/* Should be a mergeclause ... */
	Assert(restrictinfo->mergeopfamilies != NIL);
	/* ... with links not yet set */
	Assert(restrictinfo->left_ec == NULL);
	Assert(restrictinfo->right_ec == NULL);

	/* Need the declared input types of the operator */
	op_input_types(((OpExpr *) clause)->opno, &lefttype, &righttype);

	/* Find or create a matching EquivalenceClass for each side */
	restrictinfo->left_ec =
		get_eclass_for_sort_expr(root,
								 (Expr *) get_leftop(clause),
								 restrictinfo->nullable_relids,
								 restrictinfo->mergeopfamilies,
								 lefttype,
								 ((OpExpr *) clause)->inputcollid,
								 0,
								 NULL,
								 true);
	restrictinfo->right_ec =
		get_eclass_for_sort_expr(root,
								 (Expr *) get_rightop(clause),
								 restrictinfo->nullable_relids,
								 restrictinfo->mergeopfamilies,
								 righttype,
								 ((OpExpr *) clause)->inputcollid,
								 0,
								 NULL,
								 true);
}

/*
 * update_mergeclause_eclasses
 *		Make the cached EquivalenceClass links valid in a mergeclause
 *		restrictinfo.
 *
 * These pointers should have been set by process_equivalence or
 * initialize_mergeclause_eclasses, but they might have been set to
 * non-canonical ECs that got merged later.  Chase up to the canonical
 * merged parent if so.
 */
void
update_mergeclause_eclasses(PlannerInfo *root, RestrictInfo *restrictinfo)
{
	/* Should be a merge clause ... */
	Assert(restrictinfo->mergeopfamilies != NIL);
	/* ... with pointers already set */
	Assert(restrictinfo->left_ec != NULL);
	Assert(restrictinfo->right_ec != NULL);

	/* Chase up to the top as needed */
	while (restrictinfo->left_ec->ec_merged)
		restrictinfo->left_ec = restrictinfo->left_ec->ec_merged;
	while (restrictinfo->right_ec->ec_merged)
		restrictinfo->right_ec = restrictinfo->right_ec->ec_merged;
}

/*
 * find_mergeclauses_for_outer_pathkeys
 *	  This routine attempts to find a list of mergeclauses that can be
 *	  used with a specified ordering for the join's outer relation.
 *	  If successful, it returns a list of mergeclauses.
 *
 * 'pathkeys' is a pathkeys list showing the ordering of an outer-rel path.
 * 'restrictinfos' is a list of mergejoinable restriction clauses for the
 *			join relation being formed, in no particular order.
 *
 * The restrictinfos must be marked (via outer_is_left) to show which side
 * of each clause is associated with the current outer path.  (See
 * select_mergejoin_clauses())
 *
 * The result is NIL if no merge can be done, else a maximal list of
 * usable mergeclauses (represented as a list of their restrictinfo nodes).
 * The list is ordered to match the pathkeys, as required for execution.
 */
List *
find_mergeclauses_for_outer_pathkeys(PlannerInfo *root,
									 List *pathkeys,
									 List *restrictinfos)
{
	List	   *mergeclauses = NIL;
	ListCell   *i;

	/* make sure we have eclasses cached in the clauses */
	foreach(i, restrictinfos)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(i);

		update_mergeclause_eclasses(root, rinfo);
	}

	foreach(i, pathkeys)
	{
		PathKey    *pathkey = (PathKey *) lfirst(i);
		EquivalenceClass *pathkey_ec = pathkey->pk_eclass;
		List	   *matched_restrictinfos = NIL;
		ListCell   *j;

		/*----------
		 * A mergejoin clause matches a pathkey if it has the same EC.
		 * If there are multiple matching clauses, take them all.  In plain
		 * inner-join scenarios we expect only one match, because
		 * equivalence-class processing will have removed any redundant
		 * mergeclauses.  However, in outer-join scenarios there might be
		 * multiple matches.  An example is
		 *
		 *	select * from a full join b
		 *		on a.v1 = b.v1 and a.v2 = b.v2 and a.v1 = b.v2;
		 *
		 * Given the pathkeys ({a.v1}, {a.v2}) it is okay to return all three
		 * clauses (in the order a.v1=b.v1, a.v1=b.v2, a.v2=b.v2) and indeed
		 * we *must* do so or we will be unable to form a valid plan.
		 *
		 * We expect that the given pathkeys list is canonical, which means
		 * no two members have the same EC, so it's not possible for this
		 * code to enter the same mergeclause into the result list twice.
		 *
		 * It's possible that multiple matching clauses might have different
		 * ECs on the other side, in which case the order we put them into our
		 * result makes a difference in the pathkeys required for the inner
		 * input rel.  However this routine hasn't got any info about which
		 * order would be best, so we don't worry about that.
		 *
		 * It's also possible that the selected mergejoin clauses produce
		 * a noncanonical ordering of pathkeys for the inner side, ie, we
		 * might select clauses that reference b.v1, b.v2, b.v1 in that
		 * order.  This is not harmful in itself, though it suggests that
		 * the clauses are partially redundant.  Since the alternative is
		 * to omit mergejoin clauses and thereby possibly fail to generate a
		 * plan altogether, we live with it.  make_inner_pathkeys_for_merge()
		 * has to delete duplicates when it constructs the inner pathkeys
		 * list, and we also have to deal with such cases specially in
		 * create_mergejoin_plan().
		 *----------
		 */
		foreach(j, restrictinfos)
		{
			RestrictInfo *rinfo = (RestrictInfo *) lfirst(j);
			EquivalenceClass *clause_ec;

			clause_ec = rinfo->outer_is_left ?
				rinfo->left_ec : rinfo->right_ec;
			if (clause_ec == pathkey_ec)
				matched_restrictinfos = lappend(matched_restrictinfos, rinfo);
		}

		/*
		 * If we didn't find a mergeclause, we're done --- any additional
		 * sort-key positions in the pathkeys are useless.  (But we can still
		 * mergejoin if we found at least one mergeclause.)
		 */
		if (matched_restrictinfos == NIL)
			break;

		/*
		 * If we did find usable mergeclause(s) for this sort-key position,
		 * add them to result list.
		 */
		mergeclauses = list_concat(mergeclauses, matched_restrictinfos);
	}

	return mergeclauses;
}

/*
 * select_outer_pathkeys_for_merge
 *	  Builds a pathkey list representing a possible sort ordering
 *	  that can be used with the given mergeclauses.
 *
 * 'mergeclauses' is a list of RestrictInfos for mergejoin clauses
 *			that will be used in a merge join.
 * 'joinrel' is the join relation we are trying to construct.
 *
 * The restrictinfos must be marked (via outer_is_left) to show which side
 * of each clause is associated with the current outer path.  (See
 * select_mergejoin_clauses())
 *
 * Returns a pathkeys list that can be applied to the outer relation.
 *
 * Since we assume here that a sort is required, there is no particular use
 * in matching any available ordering of the outerrel.  (joinpath.c has an
 * entirely separate code path for considering sort-free mergejoins.)  Rather,
 * it's interesting to try to match the requested query_pathkeys so that a
 * second output sort may be avoided; and failing that, we try to list "more
 * popular" keys (those with the most unmatched EquivalenceClass peers)
 * earlier, in hopes of making the resulting ordering useful for as many
 * higher-level mergejoins as possible.
 */
List *
select_outer_pathkeys_for_merge(PlannerInfo *root,
								List *mergeclauses,
								RelOptInfo *joinrel)
{
	List	   *pathkeys = NIL;
	int			nClauses = list_length(mergeclauses);
	EquivalenceClass **ecs;
	int		   *scores;
	int			necs;
	ListCell   *lc;
	int			j;

	/* Might have no mergeclauses */
	if (nClauses == 0)
		return NIL;

	/*
	 * Make arrays of the ECs used by the mergeclauses (dropping any
	 * duplicates) and their "popularity" scores.
	 */
	ecs = (EquivalenceClass **) palloc(nClauses * sizeof(EquivalenceClass *));
	scores = (int *) palloc(nClauses * sizeof(int));
	necs = 0;

	foreach(lc, mergeclauses)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
		EquivalenceClass *oeclass;
		int			score;
		ListCell   *lc2;

		/* get the outer eclass */
		update_mergeclause_eclasses(root, rinfo);

		if (rinfo->outer_is_left)
			oeclass = rinfo->left_ec;
		else
			oeclass = rinfo->right_ec;

		/* reject duplicates */
		for (j = 0; j < necs; j++)
		{
			if (ecs[j] == oeclass)
				break;
		}
		if (j < necs)
			continue;

		/* compute score */
		score = 0;
		foreach(lc2, oeclass->ec_members)
		{
			EquivalenceMember *em = (EquivalenceMember *) lfirst(lc2);

			/* Potential future join partner? */
			if (!em->em_is_const && !em->em_is_child &&
				!bms_overlap(em->em_relids, joinrel->relids))
				score++;
		}

		ecs[necs] = oeclass;
		scores[necs] = score;
		necs++;
	}

	/*
	 * Find out if we have all the ECs mentioned in query_pathkeys; if so we
	 * can generate a sort order that's also useful for final output. There is
	 * no percentage in a partial match, though, so we have to have 'em all.
	 */
	if (root->query_pathkeys)
	{
		foreach(lc, root->query_pathkeys)
		{
			PathKey    *query_pathkey = (PathKey *) lfirst(lc);
			EquivalenceClass *query_ec = query_pathkey->pk_eclass;

			for (j = 0; j < necs; j++)
			{
				if (ecs[j] == query_ec)
					break;		/* found match */
			}
			if (j >= necs)
				break;			/* didn't find match */
		}
		/* if we got to the end of the list, we have them all */
		if (lc == NULL)
		{
			/* copy query_pathkeys as starting point for our output */
			pathkeys = list_copy(root->query_pathkeys);
			/* mark their ECs as already-emitted */
			foreach(lc, root->query_pathkeys)
			{
				PathKey    *query_pathkey = (PathKey *) lfirst(lc);
				EquivalenceClass *query_ec = query_pathkey->pk_eclass;

				for (j = 0; j < necs; j++)
				{
					if (ecs[j] == query_ec)
					{
						scores[j] = -1;
						break;
					}
				}
			}
		}
	}

	/*
	 * Add remaining ECs to the list in popularity order, using a default sort
	 * ordering.  (We could use qsort() here, but the list length is usually
	 * so small it's not worth it.)
	 */
	for (;;)
	{
		int			best_j;
		int			best_score;
		EquivalenceClass *ec;
		PathKey    *pathkey;

		best_j = 0;
		best_score = scores[0];
		for (j = 1; j < necs; j++)
		{
			if (scores[j] > best_score)
			{
				best_j = j;
				best_score = scores[j];
			}
		}
		if (best_score < 0)
			break;				/* all done */
		ec = ecs[best_j];
		scores[best_j] = -1;
		pathkey = make_canonical_pathkey(root,
										 ec,
										 linitial_oid(ec->ec_opfamilies),
										 BTLessStrategyNumber,
										 false);
		/* can't be redundant because no duplicate ECs */
		Assert(!pathkey_is_redundant(pathkey, pathkeys));
		pathkeys = lappend(pathkeys, pathkey);
	}

	pfree(ecs);
	pfree(scores);

	return pathkeys;
}

/*
 * make_inner_pathkeys_for_merge
 *	  Builds a pathkey list representing the explicit sort order that
 *	  must be applied to an inner path to make it usable with the
 *	  given mergeclauses.
 *
 * 'mergeclauses' is a list of RestrictInfos for the mergejoin clauses
 *			that will be used in a merge join, in order.
 * 'outer_pathkeys' are the already-known canonical pathkeys for the outer
 *			side of the join.
 *
 * The restrictinfos must be marked (via outer_is_left) to show which side
 * of each clause is associated with the current outer path.  (See
 * select_mergejoin_clauses())
 *
 * Returns a pathkeys list that can be applied to the inner relation.
 *
 * Note that it is not this routine's job to decide whether sorting is
 * actually needed for a particular input path.  Assume a sort is necessary;
 * just make the keys, eh?
 */
List *
make_inner_pathkeys_for_merge(PlannerInfo *root,
							  List *mergeclauses,
							  List *outer_pathkeys)
{
	List	   *pathkeys = NIL;
	EquivalenceClass *lastoeclass;
	PathKey    *opathkey;
	ListCell   *lc;
	ListCell   *lop;

	lastoeclass = NULL;
	opathkey = NULL;
	lop = list_head(outer_pathkeys);

	foreach(lc, mergeclauses)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
		EquivalenceClass *oeclass;
		EquivalenceClass *ieclass;
		PathKey    *pathkey;

		update_mergeclause_eclasses(root, rinfo);

		if (rinfo->outer_is_left)
		{
			oeclass = rinfo->left_ec;
			ieclass = rinfo->right_ec;
		}
		else
		{
			oeclass = rinfo->right_ec;
			ieclass = rinfo->left_ec;
		}

		/* outer eclass should match current or next pathkeys */
		/* we check this carefully for debugging reasons */
		if (oeclass != lastoeclass)
		{
			if (!lop)
				elog(ERROR, "too few pathkeys for mergeclauses");
			opathkey = (PathKey *) lfirst(lop);
			lop = lnext(outer_pathkeys, lop);
			lastoeclass = opathkey->pk_eclass;
			if (oeclass != lastoeclass)
				elog(ERROR, "outer pathkeys do not match mergeclause");
		}

		/*
		 * Often, we'll have same EC on both sides, in which case the outer
		 * pathkey is also canonical for the inner side, and we can skip a
		 * useless search.
		 */
		if (ieclass == oeclass)
			pathkey = opathkey;
		else
			pathkey = make_canonical_pathkey(root,
											 ieclass,
											 opathkey->pk_opfamily,
											 opathkey->pk_strategy,
											 opathkey->pk_nulls_first);

		/*
		 * Don't generate redundant pathkeys (which can happen if multiple
		 * mergeclauses refer to the same EC).  Because we do this, the output
		 * pathkey list isn't necessarily ordered like the mergeclauses, which
		 * complicates life for create_mergejoin_plan().  But if we didn't,
		 * we'd have a noncanonical sort key list, which would be bad; for one
		 * reason, it certainly wouldn't match any available sort order for
		 * the input relation.
		 */
		if (!pathkey_is_redundant(pathkey, pathkeys))
			pathkeys = lappend(pathkeys, pathkey);
	}

	return pathkeys;
}

/*
 * trim_mergeclauses_for_inner_pathkeys
 *	  This routine trims a list of mergeclauses to include just those that
 *	  work with a specified ordering for the join's inner relation.
 *
 * 'mergeclauses' is a list of RestrictInfos for mergejoin clauses for the
 *			join relation being formed, in an order known to work for the
 *			currently-considered sort ordering of the join's outer rel.
 * 'pathkeys' is a pathkeys list showing the ordering of an inner-rel path;
 *			it should be equal to, or a truncation of, the result of
 *			make_inner_pathkeys_for_merge for these mergeclauses.
 *
 * What we return will be a prefix of the given mergeclauses list.
 *
 * We need this logic because make_inner_pathkeys_for_merge's result isn't
 * necessarily in the same order as the mergeclauses.  That means that if we
 * consider an inner-rel pathkey list that is a truncation of that result,
 * we might need to drop mergeclauses even though they match a surviving inner
 * pathkey.  This happens when they are to the right of a mergeclause that
 * matches a removed inner pathkey.
 *
 * The mergeclauses must be marked (via outer_is_left) to show which side
 * of each clause is associated with the current outer path.  (See
 * select_mergejoin_clauses())
 */
List *
trim_mergeclauses_for_inner_pathkeys(PlannerInfo *root,
									 List *mergeclauses,
									 List *pathkeys)
{
	List	   *new_mergeclauses = NIL;
	PathKey    *pathkey;
	EquivalenceClass *pathkey_ec;
	bool		matched_pathkey;
	ListCell   *lip;
	ListCell   *i;

	/* No pathkeys => no mergeclauses (though we don't expect this case) */
	if (pathkeys == NIL)
		return NIL;
	/* Initialize to consider first pathkey */
	lip = list_head(pathkeys);
	pathkey = (PathKey *) lfirst(lip);
	pathkey_ec = pathkey->pk_eclass;
	lip = lnext(pathkeys, lip);
	matched_pathkey = false;

	/* Scan mergeclauses to see how many we can use */
	foreach(i, mergeclauses)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(i);
		EquivalenceClass *clause_ec;

		/* Assume we needn't do update_mergeclause_eclasses again here */

		/* Check clause's inner-rel EC against current pathkey */
		clause_ec = rinfo->outer_is_left ?
			rinfo->right_ec : rinfo->left_ec;

		/* If we don't have a match, attempt to advance to next pathkey */
		if (clause_ec != pathkey_ec)
		{
			/* If we had no clauses matching this inner pathkey, must stop */
			if (!matched_pathkey)
				break;

			/* Advance to next inner pathkey, if any */
			if (lip == NULL)
				break;
			pathkey = (PathKey *) lfirst(lip);
			pathkey_ec = pathkey->pk_eclass;
			lip = lnext(pathkeys, lip);
			matched_pathkey = false;
		}

		/* If mergeclause matches current inner pathkey, we can use it */
		if (clause_ec == pathkey_ec)
		{
			new_mergeclauses = lappend(new_mergeclauses, rinfo);
			matched_pathkey = true;
		}
		else
		{
			/* Else, no hope of adding any more mergeclauses */
			break;
		}
	}

	return new_mergeclauses;
}


/****************************************************************************
 *		PATHKEY USEFULNESS CHECKS
 *
 * We only want to remember as many of the pathkeys of a path as have some
 * potential use, either for subsequent mergejoins or for meeting the query's
 * requested output ordering.  This ensures that add_path() won't consider
 * a path to have a usefully different ordering unless it really is useful.
 * These routines check for usefulness of given pathkeys.
 ****************************************************************************/

/*
 * pathkeys_useful_for_merging
 *		Count the number of pathkeys that may be useful for mergejoins
 *		above the given relation.
 *
 * We consider a pathkey potentially useful if it corresponds to the merge
 * ordering of either side of any joinclause for the rel.  This might be
 * overoptimistic, since joinclauses that require different other relations
 * might never be usable at the same time, but trying to be exact is likely
 * to be more trouble than it's worth.
 *
 * To avoid doubling the number of mergejoin paths considered, we would like
 * to consider only one of the two scan directions (ASC or DESC) as useful
 * for merging for any given target column.  The choice is arbitrary unless
 * one of the directions happens to match an ORDER BY key, in which case
 * that direction should be preferred, in hopes of avoiding a final sort step.
 * right_merge_direction() implements this heuristic.
 */
static int
pathkeys_useful_for_merging(PlannerInfo *root, RelOptInfo *rel, List *pathkeys)
{
	int			useful = 0;
	ListCell   *i;

	foreach(i, pathkeys)
	{
		PathKey    *pathkey = (PathKey *) lfirst(i);
		bool		matched = false;
		ListCell   *j;

		/* If "wrong" direction, not useful for merging */
		if (!right_merge_direction(root, pathkey))
			break;

		/*
		 * First look into the EquivalenceClass of the pathkey, to see if
		 * there are any members not yet joined to the rel.  If so, it's
		 * surely possible to generate a mergejoin clause using them.
		 */
		if (rel->has_eclass_joins &&
			eclass_useful_for_merging(root, pathkey->pk_eclass, rel))
			matched = true;
		else
		{
			/*
			 * Otherwise search the rel's joininfo list, which contains
			 * non-EquivalenceClass-derivable join clauses that might
			 * nonetheless be mergejoinable.
			 */
			foreach(j, rel->joininfo)
			{
				RestrictInfo *restrictinfo = (RestrictInfo *) lfirst(j);

				if (restrictinfo->mergeopfamilies == NIL)
					continue;
				update_mergeclause_eclasses(root, restrictinfo);

				if (pathkey->pk_eclass == restrictinfo->left_ec ||
					pathkey->pk_eclass == restrictinfo->right_ec)
				{
					matched = true;
					break;
				}
			}
		}

		/*
		 * If we didn't find a mergeclause, we're done --- any additional
		 * sort-key positions in the pathkeys are useless.  (But we can still
		 * mergejoin if we found at least one mergeclause.)
		 */
		if (matched)
			useful++;
		else
			break;
	}

	return useful;
}

/*
 * right_merge_direction
 *		Check whether the pathkey embodies the preferred sort direction
 *		for merging its target column.
 */
static bool
right_merge_direction(PlannerInfo *root, PathKey *pathkey)
{
	ListCell   *l;

	foreach(l, root->query_pathkeys)
	{
		PathKey    *query_pathkey = (PathKey *) lfirst(l);

		if (pathkey->pk_eclass == query_pathkey->pk_eclass &&
			pathkey->pk_opfamily == query_pathkey->pk_opfamily)
		{
			/*
			 * Found a matching query sort column.  Prefer this pathkey's
			 * direction iff it matches.  Note that we ignore pk_nulls_first,
			 * which means that a sort might be needed anyway ... but we still
			 * want to prefer only one of the two possible directions, and we
			 * might as well use this one.
			 */
			return (pathkey->pk_strategy == query_pathkey->pk_strategy);
		}
	}

	/* If no matching ORDER BY request, prefer the ASC direction */
	return (pathkey->pk_strategy == BTLessStrategyNumber);
}

/*
 * pathkeys_useful_for_ordering
 *		Count the number of pathkeys that are useful for meeting the
 *		query's requested output ordering.
 *
 * Because we the have the possibility of incremental sort, a prefix list of
 * keys is potentially useful for improving the performance of the requested
 * ordering. Thus we return 0, if no valuable keys are found, or the number
 * of leading keys shared by the list and the requested ordering..
 */
static int
pathkeys_useful_for_ordering(PlannerInfo *root, List *pathkeys)
{
	int			n_common_pathkeys;

	if (root->query_pathkeys == NIL)
		return 0;				/* no special ordering requested */

	if (pathkeys == NIL)
		return 0;				/* unordered path */

	(void) pathkeys_count_contained_in(root->query_pathkeys, pathkeys,
									   &n_common_pathkeys);

	return n_common_pathkeys;
}

/*
 * truncate_useless_pathkeys
 *		Shorten the given pathkey list to just the useful pathkeys.
 *
 * YB: Do NOT truncate pathkeys useful for distinct index scan because
 * UpperUniquePath nodes require these pathkeys. 'yb_distinct_nkeys' restricts
 * this.
 *
 * YB: Normally, distinct pathkeys are retained in pathkeys_useful_for_ordering
 * by retaining all query pathkeys (contains both sortkeys and distinct keys).
 * However, the pathkeys_contained_in function used for determing usefulness
 * is insufficient for Distinct Index Scans.
 *
 * Example: say, r1 is sorted ASC, r2 is sorted DESC in the index.
 * Then, the query SELECT DISTINCT r1, r2
 * should be able to generate a distinct index scan for the query since
 * the precise ordering of keys r1, r2 within the index is immaterial for
 * the DISTINCT operation. Such queries are unfortunately disallowed by
 * the pathkeys_contained_in function. This behavior is fixed by
 * 'yb_distinct_nkeys'.
 */
List *
truncate_useless_pathkeys(PlannerInfo *root,
						  RelOptInfo *rel,
						  List *pathkeys,
						  int yb_distinct_nkeys)
{
	int			nuseful;
	int			nuseful2;

	nuseful = pathkeys_useful_for_merging(root, rel, pathkeys);
	nuseful2 = pathkeys_useful_for_ordering(root, pathkeys);
	if (nuseful2 > nuseful)
		nuseful = nuseful2;
	Assert(yb_distinct_nkeys <= list_length(pathkeys));
	/* YB: Use yb_distinct_nkeys and not yb_distinct_prefixlen. */
	if (yb_distinct_nkeys > nuseful)
		nuseful = yb_distinct_nkeys;

	/*
	 * Note: not safe to modify input list destructively, but we can avoid
	 * copying the list if we're not actually going to change it
	 */
	if (nuseful == 0)
		return NIL;
	else if (nuseful == list_length(pathkeys))
		return pathkeys;
	else
		return list_truncate(list_copy(pathkeys), nuseful);
}

/*
 * has_useful_pathkeys
 *		Detect whether the specified rel could have any pathkeys that are
 *		useful according to truncate_useless_pathkeys().
 *
 * This is a cheap test that lets us skip building pathkeys at all in very
 * simple queries.  It's OK to err in the direction of returning "true" when
 * there really aren't any usable pathkeys, but erring in the other direction
 * is bad --- so keep this in sync with the routines above!
 *
 * We could make the test more complex, for example checking to see if any of
 * the joinclauses are really mergejoinable, but that likely wouldn't win
 * often enough to repay the extra cycles.  Queries with neither a join nor
 * a sort are reasonably common, though, so this much work seems worthwhile.
 */
bool
has_useful_pathkeys(PlannerInfo *root, RelOptInfo *rel)
{
	if (rel->joininfo != NIL || rel->has_eclass_joins)
		return true;			/* might be able to use pathkeys for merging */
	if (root->query_pathkeys != NIL)
		return true;			/* might be able to use them for ordering */
	return false;				/* definitely useless */
}

/*
 * YB: yb_get_ecs_for_query_uniqkeys
 *
 * Returns the EquivalenceClasses for the DISTINCT keys in the query.
 */
List *
yb_get_ecs_for_query_uniqkeys(PlannerInfo *root)
{
	ListCell *lc;
	List	 *ecs = NIL;

	foreach(lc, root->parse->distinctClause)
	{
		SortGroupClause *sortcl = (SortGroupClause *) lfirst(lc);
		Expr	   *sortkey;
		PathKey    *pathkey;

		sortkey = (Expr *) get_sortgroupclause_expr(sortcl,
													root->processed_tlist);
		Assert(OidIsValid(sortcl->sortop));
		pathkey = make_pathkey_from_sortop(root,
										   sortkey,
										   root->nullable_baserels,
										   sortcl->sortop,
										   sortcl->nulls_first,
										   sortcl->tleSortGroupRef,
										   false);

		ecs = lappend(ecs, pathkey->pk_eclass);
	}

	return ecs;
}
