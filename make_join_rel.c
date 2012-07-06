/*-------------------------------------------------------------------------
 *
 * make_join_rel.c
 *	  Routines copied from PostgreSQL core distribution.
 *
 * Portions Copyright (c) 1996-2011, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

static bool is_dummy_rel(RelOptInfo *rel);
static void mark_dummy_rel(RelOptInfo *rel);
static bool restriction_is_constant_false(List *restrictlist,
							  bool only_pushed_down);

/*
 * join_is_legal
 *	   Determine whether a proposed join is legal given the query's
 *	   join order constraints; and if it is, determine the join type.
 *
 * Caller must supply not only the two rels, but the union of their relids.
 * (We could simplify the API by computing joinrelids locally, but this
 * would be redundant work in the normal path through make_join_rel.)
 *
 * On success, *sjinfo_p is set to NULL if this is to be a plain inner join,
 * else it's set to point to the associated SpecialJoinInfo node.  Also,
 * *reversed_p is set TRUE if the given relations need to be swapped to
 * match the SpecialJoinInfo node.
 */
static bool
join_is_legal(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2,
			  Relids joinrelids,
			  SpecialJoinInfo **sjinfo_p, bool *reversed_p)
{
	SpecialJoinInfo *match_sjinfo;
	bool		reversed;
	bool		unique_ified;
	bool		is_valid_inner;
	ListCell   *l;

	/*
	 * Ensure output params are set on failure return.	This is just to
	 * suppress uninitialized-variable warnings from overly anal compilers.
	 */
	*sjinfo_p = NULL;
	*reversed_p = false;

	/*
	 * If we have any special joins, the proposed join might be illegal; and
	 * in any case we have to determine its join type.	Scan the join info
	 * list for conflicts.
	 */
	match_sjinfo = NULL;
	reversed = false;
	unique_ified = false;
	is_valid_inner = true;

	foreach(l, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(l);

		/*
		 * This special join is not relevant unless its RHS overlaps the
		 * proposed join.  (Check this first as a fast path for dismissing
		 * most irrelevant SJs quickly.)
		 */
		if (!bms_overlap(sjinfo->min_righthand, joinrelids))
			continue;

		/*
		 * Also, not relevant if proposed join is fully contained within RHS
		 * (ie, we're still building up the RHS).
		 */
		if (bms_is_subset(joinrelids, sjinfo->min_righthand))
			continue;

		/*
		 * Also, not relevant if SJ is already done within either input.
		 */
		if (bms_is_subset(sjinfo->min_lefthand, rel1->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel1->relids))
			continue;
		if (bms_is_subset(sjinfo->min_lefthand, rel2->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel2->relids))
			continue;

		/*
		 * If it's a semijoin and we already joined the RHS to any other rels
		 * within either input, then we must have unique-ified the RHS at that
		 * point (see below).  Therefore the semijoin is no longer relevant in
		 * this join path.
		 */
		if (sjinfo->jointype == JOIN_SEMI)
		{
			if (bms_is_subset(sjinfo->syn_righthand, rel1->relids) &&
				!bms_equal(sjinfo->syn_righthand, rel1->relids))
				continue;
			if (bms_is_subset(sjinfo->syn_righthand, rel2->relids) &&
				!bms_equal(sjinfo->syn_righthand, rel2->relids))
				continue;
		}

		/*
		 * If one input contains min_lefthand and the other contains
		 * min_righthand, then we can perform the SJ at this join.
		 *
		 * Barf if we get matches to more than one SJ (is that possible?)
		 */
		if (bms_is_subset(sjinfo->min_lefthand, rel1->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel2->relids))
		{
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = false;
		}
		else if (bms_is_subset(sjinfo->min_lefthand, rel2->relids) &&
				 bms_is_subset(sjinfo->min_righthand, rel1->relids))
		{
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = true;
		}
		else if (sjinfo->jointype == JOIN_SEMI &&
				 bms_equal(sjinfo->syn_righthand, rel2->relids) &&
				 create_unique_path(root, rel2, rel2->cheapest_total_path,
									sjinfo) != NULL)
		{
			/*----------
			 * For a semijoin, we can join the RHS to anything else by
			 * unique-ifying the RHS (if the RHS can be unique-ified).
			 * We will only get here if we have the full RHS but less
			 * than min_lefthand on the LHS.
			 *
			 * The reason to consider such a join path is exemplified by
			 *	SELECT ... FROM a,b WHERE (a.x,b.y) IN (SELECT c1,c2 FROM c)
			 * If we insist on doing this as a semijoin we will first have
			 * to form the cartesian product of A*B.  But if we unique-ify
			 * C then the semijoin becomes a plain innerjoin and we can join
			 * in any order, eg C to A and then to B.  When C is much smaller
			 * than A and B this can be a huge win.  So we allow C to be
			 * joined to just A or just B here, and then make_join_rel has
			 * to handle the case properly.
			 *
			 * Note that actually we'll allow unique-ified C to be joined to
			 * some other relation D here, too.  That is legal, if usually not
			 * very sane, and this routine is only concerned with legality not
			 * with whether the join is good strategy.
			 *----------
			 */
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = false;
			unique_ified = true;
		}
		else if (sjinfo->jointype == JOIN_SEMI &&
				 bms_equal(sjinfo->syn_righthand, rel1->relids) &&
				 create_unique_path(root, rel1, rel1->cheapest_total_path,
									sjinfo) != NULL)
		{
			/* Reversed semijoin case */
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = true;
			unique_ified = true;
		}
		else
		{
			/*----------
			 * Otherwise, the proposed join overlaps the RHS but isn't
			 * a valid implementation of this SJ.  It might still be
			 * a legal join, however.  If both inputs overlap the RHS,
			 * assume that it's OK.  Since the inputs presumably got past
			 * this function's checks previously, they can't overlap the
			 * LHS and their violations of the RHS boundary must represent
			 * SJs that have been determined to commute with this one.
			 * We have to allow this to work correctly in cases like
			 *		(a LEFT JOIN (b JOIN (c LEFT JOIN d)))
			 * when the c/d join has been determined to commute with the join
			 * to a, and hence d is not part of min_righthand for the upper
			 * join.  It should be legal to join b to c/d but this will appear
			 * as a violation of the upper join's RHS.
			 * Furthermore, if one input overlaps the RHS and the other does
			 * not, we should still allow the join if it is a valid
			 * implementation of some other SJ.  We have to allow this to
			 * support the associative identity
			 *		(a LJ b on Pab) LJ c ON Pbc = a LJ (b LJ c ON Pbc) on Pab
			 * since joining B directly to C violates the lower SJ's RHS.
			 * We assume that make_outerjoininfo() set things up correctly
			 * so that we'll only match to some SJ if the join is valid.
			 * Set flag here to check at bottom of loop.
			 *----------
			 */
			if (sjinfo->jointype != JOIN_SEMI &&
				bms_overlap(rel1->relids, sjinfo->min_righthand) &&
				bms_overlap(rel2->relids, sjinfo->min_righthand))
			{
				/* seems OK */
				Assert(!bms_overlap(joinrelids, sjinfo->min_lefthand));
			}
			else
				is_valid_inner = false;
		}
	}

	/*
	 * Fail if violated some SJ's RHS and didn't match to another SJ. However,
	 * "matching" to a semijoin we are implementing by unique-ification
	 * doesn't count (think: it's really an inner join).
	 */
	if (!is_valid_inner &&
		(match_sjinfo == NULL || unique_ified))
		return false;			/* invalid join path */

	/* Otherwise, it's a valid join */
	*sjinfo_p = match_sjinfo;
	*reversed_p = reversed;
	return true;
}


/*
 * make_join_rel
 *	   Find or create a join RelOptInfo that represents the join of
 *	   the two given rels, and add to it path information for paths
 *	   created with the two rels as outer and inner rel.
 *	   (The join rel may already contain paths generated from other
 *	   pairs of rels that add up to the same set of base rels.)
 *
 * NB: will return NULL if attempted join is not valid.  This can happen
 * when working with outer joins, or with IN or EXISTS clauses that have been
 * turned into joins.
 */
RelOptInfo *
make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2)
{
	Relids		joinrelids;
	SpecialJoinInfo *sjinfo;
	bool		reversed;
	SpecialJoinInfo sjinfo_data;
	RelOptInfo *joinrel;
	List	   *restrictlist;

	/* We should never try to join two overlapping sets of rels. */
	Assert(!bms_overlap(rel1->relids, rel2->relids));

	/* Construct Relids set that identifies the joinrel. */
	joinrelids = bms_union(rel1->relids, rel2->relids);

	/* Check validity and determine join type. */
	if (!join_is_legal(root, rel1, rel2, joinrelids,
					   &sjinfo, &reversed))
	{
		/* invalid join path */
		bms_free(joinrelids);
		return NULL;
	}

	/* Swap rels if needed to match the join info. */
	if (reversed)
	{
		RelOptInfo *trel = rel1;

		rel1 = rel2;
		rel2 = trel;
	}

	/*
	 * If it's a plain inner join, then we won't have found anything in
	 * join_info_list.	Make up a SpecialJoinInfo so that selectivity
	 * estimation functions will know what's being joined.
	 */
	if (sjinfo == NULL)
	{
		sjinfo = &sjinfo_data;
		sjinfo->type = T_SpecialJoinInfo;
		sjinfo->min_lefthand = rel1->relids;
		sjinfo->min_righthand = rel2->relids;
		sjinfo->syn_lefthand = rel1->relids;
		sjinfo->syn_righthand = rel2->relids;
		sjinfo->jointype = JOIN_INNER;
		/* we don't bother trying to make the remaining fields valid */
		sjinfo->lhs_strict = false;
		sjinfo->delay_upper_joins = false;
		sjinfo->join_quals = NIL;
	}

	/*
	 * Find or build the join RelOptInfo, and compute the restrictlist that
	 * goes with this particular joining.
	 */
	joinrel = build_join_rel(root, joinrelids, rel1, rel2, sjinfo,
							 &restrictlist);

	/*
	 * If we've already proven this join is empty, we needn't consider any
	 * more paths for it.
	 */
	if (is_dummy_rel(joinrel))
	{
		bms_free(joinrelids);
		return joinrel;
	}

	/*
	 * Consider paths using each rel as both outer and inner.  Depending on
	 * the join type, a provably empty outer or inner rel might mean the join
	 * is provably empty too; in which case throw away any previously computed
	 * paths and mark the join as dummy.  (We do it this way since it's
	 * conceivable that dummy-ness of a multi-element join might only be
	 * noticeable for certain construction paths.)
	 *
	 * Also, a provably constant-false join restriction typically means that
	 * we can skip evaluating one or both sides of the join.  We do this by
	 * marking the appropriate rel as dummy.  For outer joins, a
	 * constant-false restriction that is pushed down still means the whole
	 * join is dummy, while a non-pushed-down one means that no inner rows
	 * will join so we can treat the inner rel as dummy.
	 *
	 * We need only consider the jointypes that appear in join_info_list, plus
	 * JOIN_INNER.
	 */
	switch (sjinfo->jointype)
	{
		case JOIN_INNER:
			if (is_dummy_rel(rel1) || is_dummy_rel(rel2) ||
				restriction_is_constant_false(restrictlist, false))
			{
				mark_dummy_rel(joinrel);
				break;
			}
			add_paths_to_joinrel(root, joinrel, rel1, rel2,
								 JOIN_INNER, sjinfo,
								 restrictlist);
			add_paths_to_joinrel(root, joinrel, rel2, rel1,
								 JOIN_INNER, sjinfo,
								 restrictlist);
			break;
		case JOIN_LEFT:
			if (is_dummy_rel(rel1) ||
				restriction_is_constant_false(restrictlist, true))
			{
				mark_dummy_rel(joinrel);
				break;
			}
			if (restriction_is_constant_false(restrictlist, false) &&
				bms_is_subset(rel2->relids, sjinfo->syn_righthand))
				mark_dummy_rel(rel2);
			add_paths_to_joinrel(root, joinrel, rel1, rel2,
								 JOIN_LEFT, sjinfo,
								 restrictlist);
			add_paths_to_joinrel(root, joinrel, rel2, rel1,
								 JOIN_RIGHT, sjinfo,
								 restrictlist);
			break;
		case JOIN_FULL:
			if ((is_dummy_rel(rel1) && is_dummy_rel(rel2)) ||
				restriction_is_constant_false(restrictlist, true))
			{
				mark_dummy_rel(joinrel);
				break;
			}
			add_paths_to_joinrel(root, joinrel, rel1, rel2,
								 JOIN_FULL, sjinfo,
								 restrictlist);
			add_paths_to_joinrel(root, joinrel, rel2, rel1,
								 JOIN_FULL, sjinfo,
								 restrictlist);

			/*
			 * If there are join quals that aren't mergeable or hashable, we
			 * may not be able to build any valid plan.  Complain here so that
			 * we can give a somewhat-useful error message.  (Since we have no
			 * flexibility of planning for a full join, there's no chance of
			 * succeeding later with another pair of input rels.)
			 */
			if (joinrel->pathlist == NIL)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("FULL JOIN is only supported with merge-joinable or hash-joinable join conditions")));
			break;
		case JOIN_SEMI:

			/*
			 * We might have a normal semijoin, or a case where we don't have
			 * enough rels to do the semijoin but can unique-ify the RHS and
			 * then do an innerjoin (see comments in join_is_legal).  In the
			 * latter case we can't apply JOIN_SEMI joining.
			 */
			if (bms_is_subset(sjinfo->min_lefthand, rel1->relids) &&
				bms_is_subset(sjinfo->min_righthand, rel2->relids))
			{
				if (is_dummy_rel(rel1) || is_dummy_rel(rel2) ||
					restriction_is_constant_false(restrictlist, false))
				{
					mark_dummy_rel(joinrel);
					break;
				}
				add_paths_to_joinrel(root, joinrel, rel1, rel2,
									 JOIN_SEMI, sjinfo,
									 restrictlist);
			}

			/*
			 * If we know how to unique-ify the RHS and one input rel is
			 * exactly the RHS (not a superset) we can consider unique-ifying
			 * it and then doing a regular join.  (The create_unique_path
			 * check here is probably redundant with what join_is_legal did,
			 * but if so the check is cheap because it's cached.  So test
			 * anyway to be sure.)
			 */
			if (bms_equal(sjinfo->syn_righthand, rel2->relids) &&
				create_unique_path(root, rel2, rel2->cheapest_total_path,
								   sjinfo) != NULL)
			{
				if (is_dummy_rel(rel1) || is_dummy_rel(rel2) ||
					restriction_is_constant_false(restrictlist, false))
				{
					mark_dummy_rel(joinrel);
					break;
				}
				add_paths_to_joinrel(root, joinrel, rel1, rel2,
									 JOIN_UNIQUE_INNER, sjinfo,
									 restrictlist);
				add_paths_to_joinrel(root, joinrel, rel2, rel1,
									 JOIN_UNIQUE_OUTER, sjinfo,
									 restrictlist);
			}
			break;
		case JOIN_ANTI:
			if (is_dummy_rel(rel1) ||
				restriction_is_constant_false(restrictlist, true))
			{
				mark_dummy_rel(joinrel);
				break;
			}
			if (restriction_is_constant_false(restrictlist, false) &&
				bms_is_subset(rel2->relids, sjinfo->syn_righthand))
				mark_dummy_rel(rel2);
			add_paths_to_joinrel(root, joinrel, rel1, rel2,
								 JOIN_ANTI, sjinfo,
								 restrictlist);
			break;
		default:
			/* other values not expected here */
			elog(ERROR, "unrecognized join type: %d", (int) sjinfo->jointype);
			break;
	}

	bms_free(joinrelids);

	return joinrel;
}

/*
 * is_dummy_rel --- has relation been proven empty?
 *
 * If so, it will have a single path that is dummy.
 */
static bool
is_dummy_rel(RelOptInfo *rel)
{
	return (rel->cheapest_total_path != NULL &&
			IS_DUMMY_PATH(rel->cheapest_total_path));
}


/*
 * Mark a relation as proven empty.
 *
 * During GEQO planning, this can get invoked more than once on the same
 * baserel struct, so it's worth checking to see if the rel is already marked
 * dummy.
 *
 * Also, when called during GEQO join planning, we are in a short-lived
 * memory context.	We must make sure that the dummy path attached to a
 * baserel survives the GEQO cycle, else the baserel is trashed for future
 * GEQO cycles.  On the other hand, when we are marking a joinrel during GEQO,
 * we don't want the dummy path to clutter the main planning context.  Upshot
 * is that the best solution is to explicitly make the dummy path in the same
 * context the given RelOptInfo is in.
 */
static void
mark_dummy_rel(RelOptInfo *rel)
{
	MemoryContext oldcontext;

	/* Already marked? */
	if (is_dummy_rel(rel))
		return;

	/* No, so choose correct context to make the dummy path in */
	oldcontext = MemoryContextSwitchTo(GetMemoryChunkContext(rel));

	/* Set dummy size estimate */
	rel->rows = 0;

	/* Evict any previously chosen paths */
	rel->pathlist = NIL;

	/* Set up the dummy path */
	add_path(rel, (Path *) create_append_path(rel, NIL));

	/* Set or update cheapest_total_path */
	set_cheapest(rel);

	MemoryContextSwitchTo(oldcontext);
}


/*
 * restriction_is_constant_false --- is a restrictlist just FALSE?
 *
 * In cases where a qual is provably constant FALSE, eval_const_expressions
 * will generally have thrown away anything that's ANDed with it.  In outer
 * join situations this will leave us computing cartesian products only to
 * decide there's no match for an outer row, which is pretty stupid.  So,
 * we need to detect the case.
 *
 * If only_pushed_down is TRUE, then consider only pushed-down quals.
 */
static bool
restriction_is_constant_false(List *restrictlist, bool only_pushed_down)
{
	ListCell   *lc;

	/*
	 * Despite the above comment, the restriction list we see here might
	 * possibly have other members besides the FALSE constant, since other
	 * quals could get "pushed down" to the outer join level.  So we check
	 * each member of the list.
	 */
	foreach(lc, restrictlist)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		Assert(IsA(rinfo, RestrictInfo));
		if (only_pushed_down && !rinfo->is_pushed_down)
			continue;

		if (rinfo->clause && IsA(rinfo->clause, Const))
		{
			Const	   *con = (Const *) rinfo->clause;

			/* constant NULL is as good as constant FALSE for our purposes */
			if (con->constisnull)
				return true;
			if (!DatumGetBool(con->constvalue))
				return true;
		}
	}
	return false;
}
