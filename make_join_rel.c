/*-------------------------------------------------------------------------
 *
 * make_join_rel.c
 *	  Routines copied from PostgreSQL core distribution with some
 *	  modifications.
 *
 * src/backend/optimizer/path/joinrels.c
 *
 * This file contains the following functions from corresponding files.
 *
 *	static functions:
 *     make_join_rel()
 *     populate_joinrel_with_paths()
 *
 * Portions Copyright (c) 2013-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

/*
 * adjust_rows: tweak estimated row numbers according to the hint.
 */
static double
adjust_rows(double rows, RowsHint *hint)
{
	double		result = 0.0;	/* keep compiler quiet */

	if (hint->value_type == RVT_ABSOLUTE)
		result = hint->rows;
	else if (hint->value_type == RVT_ADD)
		result = rows + hint->rows;
	else if (hint->value_type == RVT_SUB)
		result =  rows - hint->rows;
	else if (hint->value_type == RVT_MULTI)
		result = rows * hint->rows;
	else
		Assert(false);	/* unrecognized rows value type */

	hint->base.state = HINT_STATE_USED;
	if (result < 1.0)
		ereport(WARNING,
				(errmsg("Force estimate to be at least one row, to avoid possible divide-by-zero when interpolating costs : %s",
					hint->base.hint_str)));
	result = clamp_row_est(result);
	elog(DEBUG1, "adjusted rows %d to %d", (int) rows, (int) result);

	return result;
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
	 * join_info_list.  Make up a SpecialJoinInfo so that selectivity
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
		sjinfo->semi_can_btree = false;
		sjinfo->semi_can_hash = false;
		sjinfo->semi_operators = NIL;
		sjinfo->semi_rhs_exprs = NIL;
	}

	/*
	 * Find or build the join RelOptInfo, and compute the restrictlist that
	 * goes with this particular joining.
	 */
	joinrel = build_join_rel(root, joinrelids, rel1, rel2, sjinfo,
							 &restrictlist);

	/* !!! START: HERE IS THE PART WHICH IS ADDED FOR PG_HINT_PLAN !!! */
	{
		RowsHint   *rows_hint = NULL;
		int			i;
		RowsHint   *justforme = NULL;
		RowsHint   *domultiply = NULL;

		/* Search for applicable rows hint for this join node */
		for (i = 0; i < current_hint_state->num_hints[HINT_TYPE_ROWS]; i++)
		{
			rows_hint = current_hint_state->rows_hints[i];

			/*
			 * Skip this rows_hint if it is invalid from the first or it
			 * doesn't target any join rels.
			 */
			if (!rows_hint->joinrelids ||
				rows_hint->base.state == HINT_STATE_ERROR)
				continue;

			if (bms_equal(joinrelids, rows_hint->joinrelids))
			{
				/*
				 * This joinrel is just the target of this rows_hint, so tweak
				 * rows estimation according to the hint.
				 */
				justforme = rows_hint;
			}
			else if (!(bms_is_subset(rows_hint->joinrelids, rel1->relids) ||
					   bms_is_subset(rows_hint->joinrelids, rel2->relids)) &&
					 bms_is_subset(rows_hint->joinrelids, joinrelids) &&
					 rows_hint->value_type == RVT_MULTI)
			{
				/*
				 * If the rows_hint's target relids is not a subset of both of
				 * component rels and is a subset of this joinrel, ths hint's
				 * targets spread over both component rels. This menas that
				 * this hint has been never applied so far and this joinrel is
				 * the first (and only) chance to fire in current join tree.
				 * Only the multiplication hint has the cumulative nature so we
				 * apply only RVT_MULTI in this way.
				 */
				domultiply = rows_hint;
			}
		}

		if (justforme)
		{
			/*
			 * If a hint just for me is found, no other adjust method is
			 * useles, but this cannot be more than twice becuase this joinrel
			 * is already adjusted by this hint.
			 */
			if (justforme->base.state == HINT_STATE_NOTUSED)
				joinrel->rows = adjust_rows(joinrel->rows, justforme);
		}
		else
		{
			if (domultiply)
			{
				/*
				 * If we have multiple routes up to this joinrel which are not
				 * applicable this hint, this multiply hint will applied more
				 * than twice. But there's no means to know of that,
				 * re-estimate the row number of this joinrel always just
				 * before applying the hint. This is a bit different from
				 * normal planner behavior but it doesn't harm so much.
				 */
				set_joinrel_size_estimates(root, joinrel, rel1, rel2, sjinfo,
										   restrictlist);

				joinrel->rows = adjust_rows(joinrel->rows, domultiply);
			}

		}
	}
	/* !!! END: HERE IS THE PART WHICH IS ADDED FOR PG_HINT_PLAN !!! */

	/*
	 * If we've already proven this join is empty, we needn't consider any
	 * more paths for it.
	 */
	if (is_dummy_rel(joinrel))
	{
		bms_free(joinrelids);
		return joinrel;
	}

	/* Add paths to the join relation. */
	populate_joinrel_with_paths(root, rel1, rel2, joinrel, sjinfo,
								restrictlist);

	bms_free(joinrelids);

	return joinrel;
}


/*
 * populate_joinrel_with_paths
 *	  Add paths to the given joinrel for given pair of joining relations. The
 *	  SpecialJoinInfo provides details about the join and the restrictlist
 *	  contains the join clauses and the other clauses applicable for given pair
 *	  of the joining relations.
 */
static void
populate_joinrel_with_paths(PlannerInfo *root, RelOptInfo *rel1,
							RelOptInfo *rel2, RelOptInfo *joinrel,
							SpecialJoinInfo *sjinfo, List *restrictlist)
{
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
				restriction_is_constant_false(restrictlist, joinrel, false))
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
				restriction_is_constant_false(restrictlist, joinrel, true))
			{
				mark_dummy_rel(joinrel);
				break;
			}
			if (restriction_is_constant_false(restrictlist, joinrel, false) &&
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
				restriction_is_constant_false(restrictlist, joinrel, true))
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
					restriction_is_constant_false(restrictlist, joinrel, false))
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
					restriction_is_constant_false(restrictlist, joinrel, false))
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
				restriction_is_constant_false(restrictlist, joinrel, true))
			{
				mark_dummy_rel(joinrel);
				break;
			}
			if (restriction_is_constant_false(restrictlist, joinrel, false) &&
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

	/* Apply partitionwise join technique, if possible. */
	try_partitionwise_join(root, rel1, rel2, joinrel, sjinfo, restrictlist);
}
