/*-------------------------------------------------------------------------
 *
 * core.c
 *	  Routines copied from PostgreSQL core distribution.
 *
 * Portions Copyright (c) 1996-2011, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
/*
 * PostgreSQL 本体から流用した関数
 *
 * src/backend/optimizer/path/allpaths.c
 *     standard_join_search()
 *     set_plain_rel_pathlist()
 *     set_append_rel_pathlist()
 *     accumulate_append_subpath()
 *     set_dummy_rel_pathlist()
 *
 * src/backend/optimizer/path/joinrels.c:
 *     join_search_one_level()
 *     make_rels_by_clause_joins()
 *     make_rels_by_clauseless_joins()
 *     has_join_restriction()
 */

/*
 * standard_join_search
 *	  Find possible joinpaths for a query by successively finding ways
 *	  to join component relations into join relations.
 *
 * 'levels_needed' is the number of iterations needed, ie, the number of
 *		independent jointree items in the query.  This is > 1.
 *
 * 'initial_rels' is a list of RelOptInfo nodes for each independent
 *		jointree item.	These are the components to be joined together.
 *		Note that levels_needed == list_length(initial_rels).
 *
 * Returns the final level of join relations, i.e., the relation that is
 * the result of joining all the original relations together.
 * At least one implementation path must be provided for this relation and
 * all required sub-relations.
 *
 * To support loadable plugins that modify planner behavior by changing the
 * join searching algorithm, we provide a hook variable that lets a plugin
 * replace or supplement this function.  Any such hook must return the same
 * final join relation as the standard code would, but it might have a
 * different set of implementation paths attached, and only the sub-joinrels
 * needed for these paths need have been instantiated.
 *
 * Note to plugin authors: the functions invoked during standard_join_search()
 * modify root->join_rel_list and root->join_rel_hash.	If you want to do more
 * than one join-order search, you'll probably need to save and restore the
 * original states of those data structures.  See geqo_eval() for an example.
 */
RelOptInfo *
standard_join_search(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	int			lev;
	RelOptInfo *rel;

	/*
	 * This function cannot be invoked recursively within any one planning
	 * problem, so join_rel_level[] can't be in use already.
	 */
	Assert(root->join_rel_level == NULL);

	/*
	 * We employ a simple "dynamic programming" algorithm: we first find all
	 * ways to build joins of two jointree items, then all ways to build joins
	 * of three items (from two-item joins and single items), then four-item
	 * joins, and so on until we have considered all ways to join all the
	 * items into one rel.
	 *
	 * root->join_rel_level[j] is a list of all the j-item rels.  Initially we
	 * set root->join_rel_level[1] to represent all the single-jointree-item
	 * relations.
	 */
	root->join_rel_level = (List **) palloc0((levels_needed + 1) * sizeof(List *));

	root->join_rel_level[1] = initial_rels;

	for (lev = 2; lev <= levels_needed; lev++)
	{
		ListCell   *lc;

		/*
		 * Determine all possible pairs of relations to be joined at this
		 * level, and build paths for making each one from every available
		 * pair of lower-level relations.
		 */
		join_search_one_level(root, lev);

		/*
		 * Do cleanup work on each just-processed rel.
		 */
		foreach(lc, root->join_rel_level[lev])
		{
			rel = (RelOptInfo *) lfirst(lc);

			/* Find and save the cheapest paths for this rel */
			set_cheapest(rel);

#ifdef OPTIMIZER_DEBUG
			debug_print_rel(root, rel);
#endif
		}
	}

	/*
	 * We should have a single rel at the final level.
	 */
	if (root->join_rel_level[levels_needed] == NIL)
		elog(ERROR, "failed to build any %d-way joins", levels_needed);
	Assert(list_length(root->join_rel_level[levels_needed]) == 1);

	rel = (RelOptInfo *) linitial(root->join_rel_level[levels_needed]);

	root->join_rel_level = NULL;

	return rel;
}

/*
 * set_plain_rel_pathlist
 *	  Build access paths for a plain relation (no subquery, no inheritance)
 */
static void
set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	/* Consider sequential scan */
	add_path(rel, create_seqscan_path(root, rel));

	/* Consider index scans */
	create_index_paths(root, rel);

	/* Consider TID scans */
	create_tidscan_paths(root, rel);

	/* Now find the cheapest of the paths for this rel */
	set_cheapest(rel);
}

/*
 * set_append_rel_pathlist
 *	  Build access paths for an "append relation"
 *
 * The passed-in rel and RTE represent the entire append relation.	The
 * relation's contents are computed by appending together the output of
 * the individual member relations.  Note that in the inheritance case,
 * the first member relation is actually the same table as is mentioned in
 * the parent RTE ... but it has a different RTE and RelOptInfo.  This is
 * a good thing because their outputs are not the same size.
 */
static void
set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
						Index rti, RangeTblEntry *rte)
{
	int			parentRTindex = rti;
	List	   *live_childrels = NIL;
	List	   *subpaths = NIL;
	List	   *all_child_pathkeys = NIL;
	double		parent_rows;
	double		parent_size;
	double	   *parent_attrsizes;
	int			nattrs;
	ListCell   *l;

	/*
	 * Initialize to compute size estimates for whole append relation.
	 *
	 * We handle width estimates by weighting the widths of different child
	 * rels proportionally to their number of rows.  This is sensible because
	 * the use of width estimates is mainly to compute the total relation
	 * "footprint" if we have to sort or hash it.  To do this, we sum the
	 * total equivalent size (in "double" arithmetic) and then divide by the
	 * total rowcount estimate.  This is done separately for the total rel
	 * width and each attribute.
	 *
	 * Note: if you consider changing this logic, beware that child rels could
	 * have zero rows and/or width, if they were excluded by constraints.
	 */
	parent_rows = 0;
	parent_size = 0;
	nattrs = rel->max_attr - rel->min_attr + 1;
	parent_attrsizes = (double *) palloc0(nattrs * sizeof(double));

	/*
	 * Generate access paths for each member relation, and pick the cheapest
	 * path for each one.
	 */
	foreach(l, root->append_rel_list)
	{
		AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);
		int			childRTindex;
		RangeTblEntry *childRTE;
		RelOptInfo *childrel;
		List	   *childquals;
		Node	   *childqual;
		ListCell   *lcp;
		ListCell   *parentvars;
		ListCell   *childvars;

		/* append_rel_list contains all append rels; ignore others */
		if (appinfo->parent_relid != parentRTindex)
			continue;

		childRTindex = appinfo->child_relid;
		childRTE = root->simple_rte_array[childRTindex];

		/*
		 * The child rel's RelOptInfo was already created during
		 * add_base_rels_to_query.
		 */
		childrel = find_base_rel(root, childRTindex);
		Assert(childrel->reloptkind == RELOPT_OTHER_MEMBER_REL);

		/*
		 * We have to copy the parent's targetlist and quals to the child,
		 * with appropriate substitution of variables.	However, only the
		 * baserestrictinfo quals are needed before we can check for
		 * constraint exclusion; so do that first and then check to see if we
		 * can disregard this child.
		 *
		 * As of 8.4, the child rel's targetlist might contain non-Var
		 * expressions, which means that substitution into the quals could
		 * produce opportunities for const-simplification, and perhaps even
		 * pseudoconstant quals.  To deal with this, we strip the RestrictInfo
		 * nodes, do the substitution, do const-simplification, and then
		 * reconstitute the RestrictInfo layer.
		 */
		childquals = get_all_actual_clauses(rel->baserestrictinfo);
		childquals = (List *) adjust_appendrel_attrs((Node *) childquals,
													 appinfo);
		childqual = eval_const_expressions(root, (Node *)
										   make_ands_explicit(childquals));
		if (childqual && IsA(childqual, Const) &&
			(((Const *) childqual)->constisnull ||
			 !DatumGetBool(((Const *) childqual)->constvalue)))
		{
			/*
			 * Restriction reduces to constant FALSE or constant NULL after
			 * substitution, so this child need not be scanned.
			 */
			set_dummy_rel_pathlist(childrel);
			continue;
		}
		childquals = make_ands_implicit((Expr *) childqual);
		childquals = make_restrictinfos_from_actual_clauses(root,
															childquals);
		childrel->baserestrictinfo = childquals;

		if (relation_excluded_by_constraints(root, childrel, childRTE))
		{
			/*
			 * This child need not be scanned, so we can omit it from the
			 * appendrel.  Mark it with a dummy cheapest-path though, in case
			 * best_appendrel_indexscan() looks at it later.
			 */
			set_dummy_rel_pathlist(childrel);
			continue;
		}

		/*
		 * CE failed, so finish copying/modifying targetlist and join quals.
		 *
		 * Note: the resulting childrel->reltargetlist may contain arbitrary
		 * expressions, which normally would not occur in a reltargetlist.
		 * That is okay because nothing outside of this routine will look at
		 * the child rel's reltargetlist.  We do have to cope with the case
		 * while constructing attr_widths estimates below, though.
		 */
		childrel->joininfo = (List *)
			adjust_appendrel_attrs((Node *) rel->joininfo,
								   appinfo);
		childrel->reltargetlist = (List *)
			adjust_appendrel_attrs((Node *) rel->reltargetlist,
								   appinfo);

		/*
		 * We have to make child entries in the EquivalenceClass data
		 * structures as well.	This is needed either if the parent
		 * participates in some eclass joins (because we will want to consider
		 * inner-indexscan joins on the individual children) or if the parent
		 * has useful pathkeys (because we should try to build MergeAppend
		 * paths that produce those sort orderings).
		 */
		if (rel->has_eclass_joins || has_useful_pathkeys(root, rel))
			add_child_rel_equivalences(root, appinfo, rel, childrel);
		childrel->has_eclass_joins = rel->has_eclass_joins;

		/*
		 * Note: we could compute appropriate attr_needed data for the child's
		 * variables, by transforming the parent's attr_needed through the
		 * translated_vars mapping.  However, currently there's no need
		 * because attr_needed is only examined for base relations not
		 * otherrels.  So we just leave the child's attr_needed empty.
		 */

		/* Remember which childrels are live, for MergeAppend logic below */
		live_childrels = lappend(live_childrels, childrel);

		/*
		 * Compute the child's access paths, and add the cheapest one to the
		 * Append path we are constructing for the parent.
		 */
		set_rel_pathlist(root, childrel, childRTindex, childRTE);

		subpaths = accumulate_append_subpath(subpaths,
											 childrel->cheapest_total_path);

		/*
		 * Collect a list of all the available path orderings for all the
		 * children.  We use this as a heuristic to indicate which sort
		 * orderings we should build MergeAppend paths for.
		 */
		foreach(lcp, childrel->pathlist)
		{
			Path	   *childpath = (Path *) lfirst(lcp);
			List	   *childkeys = childpath->pathkeys;
			ListCell   *lpk;
			bool		found = false;

			/* Ignore unsorted paths */
			if (childkeys == NIL)
				continue;

			/* Have we already seen this ordering? */
			foreach(lpk, all_child_pathkeys)
			{
				List	   *existing_pathkeys = (List *) lfirst(lpk);

				if (compare_pathkeys(existing_pathkeys,
									 childkeys) == PATHKEYS_EQUAL)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				/* No, so add it to all_child_pathkeys */
				all_child_pathkeys = lappend(all_child_pathkeys, childkeys);
			}
		}

		/*
		 * Accumulate size information from each child.
		 */
		if (childrel->rows > 0)
		{
			parent_rows += childrel->rows;
			parent_size += childrel->width * childrel->rows;

			/*
			 * Accumulate per-column estimates too.  We need not do anything
			 * for PlaceHolderVars in the parent list.  If child expression
			 * isn't a Var, or we didn't record a width estimate for it, we
			 * have to fall back on a datatype-based estimate.
			 *
			 * By construction, child's reltargetlist is 1-to-1 with parent's.
			 */
			forboth(parentvars, rel->reltargetlist,
					childvars, childrel->reltargetlist)
			{
				Var		   *parentvar = (Var *) lfirst(parentvars);
				Node	   *childvar = (Node *) lfirst(childvars);

				if (IsA(parentvar, Var))
				{
					int			pndx = parentvar->varattno - rel->min_attr;
					int32		child_width = 0;

					if (IsA(childvar, Var))
					{
						int		cndx = ((Var *) childvar)->varattno - childrel->min_attr;

						child_width = childrel->attr_widths[cndx];
					}
					if (child_width <= 0)
						child_width = get_typavgwidth(exprType(childvar),
													  exprTypmod(childvar));
					Assert(child_width > 0);
					parent_attrsizes[pndx] += child_width * childrel->rows;
				}
			}
		}
	}

	/*
	 * Save the finished size estimates.
	 */
	rel->rows = parent_rows;
	if (parent_rows > 0)
	{
		int			i;

		rel->width = rint(parent_size / parent_rows);
		for (i = 0; i < nattrs; i++)
			rel->attr_widths[i] = rint(parent_attrsizes[i] / parent_rows);
	}
	else
		rel->width = 0;			/* attr_widths should be zero already */

	/*
	 * Set "raw tuples" count equal to "rows" for the appendrel; needed
	 * because some places assume rel->tuples is valid for any baserel.
	 */
	rel->tuples = parent_rows;

	pfree(parent_attrsizes);

	/*
	 * Next, build an unordered Append path for the rel.  (Note: this is
	 * correct even if we have zero or one live subpath due to constraint
	 * exclusion.)
	 */
	add_path(rel, (Path *) create_append_path(rel, subpaths));

	/*
	 * Next, build MergeAppend paths based on the collected list of child
	 * pathkeys.  We consider both cheapest-startup and cheapest-total cases,
	 * ie, for each interesting ordering, collect all the cheapest startup
	 * subpaths and all the cheapest total paths, and build a MergeAppend path
	 * for each list.
	 */
	foreach(l, all_child_pathkeys)
	{
		List	   *pathkeys = (List *) lfirst(l);
		List	   *startup_subpaths = NIL;
		List	   *total_subpaths = NIL;
		bool		startup_neq_total = false;
		ListCell   *lcr;

		/* Select the child paths for this ordering... */
		foreach(lcr, live_childrels)
		{
			RelOptInfo *childrel = (RelOptInfo *) lfirst(lcr);
			Path	   *cheapest_startup,
					   *cheapest_total;

			/* Locate the right paths, if they are available. */
			cheapest_startup =
				get_cheapest_path_for_pathkeys(childrel->pathlist,
											   pathkeys,
											   STARTUP_COST);
			cheapest_total =
				get_cheapest_path_for_pathkeys(childrel->pathlist,
											   pathkeys,
											   TOTAL_COST);

			/*
			 * If we can't find any paths with the right order just add the
			 * cheapest-total path; we'll have to sort it.
			 */
			if (cheapest_startup == NULL)
				cheapest_startup = childrel->cheapest_total_path;
			if (cheapest_total == NULL)
				cheapest_total = childrel->cheapest_total_path;

			/*
			 * Notice whether we actually have different paths for the
			 * "cheapest" and "total" cases; frequently there will be no point
			 * in two create_merge_append_path() calls.
			 */
			if (cheapest_startup != cheapest_total)
				startup_neq_total = true;

			startup_subpaths =
				accumulate_append_subpath(startup_subpaths, cheapest_startup);
			total_subpaths =
				accumulate_append_subpath(total_subpaths, cheapest_total);
		}

		/* ... and build the MergeAppend paths */
		add_path(rel, (Path *) create_merge_append_path(root,
														rel,
														startup_subpaths,
														pathkeys));
		if (startup_neq_total)
			add_path(rel, (Path *) create_merge_append_path(root,
															rel,
															total_subpaths,
															pathkeys));
	}

	/* Select cheapest path */
	set_cheapest(rel);
}

/*
 * accumulate_append_subpath
 *		Add a subpath to the list being built for an Append or MergeAppend
 *
 * It's possible that the child is itself an Append path, in which case
 * we can "cut out the middleman" and just add its child paths to our
 * own list.  (We don't try to do this earlier because we need to
 * apply both levels of transformation to the quals.)
 */
static List *
accumulate_append_subpath(List *subpaths, Path *path)
{
	if (IsA(path, AppendPath))
	{
		AppendPath *apath = (AppendPath *) path;

		/* list_copy is important here to avoid sharing list substructure */
		return list_concat(subpaths, list_copy(apath->subpaths));
	}
	else
		return lappend(subpaths, path);
}

/*
 * set_dummy_rel_pathlist
 *	  Build a dummy path for a relation that's been excluded by constraints
 *
 * Rather than inventing a special "dummy" path type, we represent this as an
 * AppendPath with no members (see also IS_DUMMY_PATH macro).
 */
static void
set_dummy_rel_pathlist(RelOptInfo *rel)
{
	/* Set dummy size estimates --- we leave attr_widths[] as zeroes */
	rel->rows = 0;
	rel->width = 0;

	add_path(rel, (Path *) create_append_path(rel, NIL));

	/* Select cheapest path (pretty easy in this case...) */
	set_cheapest(rel);
}

/*
 * join_search_one_level
 *	  Consider ways to produce join relations containing exactly 'level'
 *	  jointree items.  (This is one step of the dynamic-programming method
 *	  embodied in standard_join_search.)  Join rel nodes for each feasible
 *	  combination of lower-level rels are created and returned in a list.
 *	  Implementation paths are created for each such joinrel, too.
 *
 * level: level of rels we want to make this time
 * root->join_rel_level[j], 1 <= j < level, is a list of rels containing j items
 *
 * The result is returned in root->join_rel_level[level].
 */
void
join_search_one_level(PlannerInfo *root, int level)
{
	List	  **joinrels = root->join_rel_level;
	ListCell   *r;
	int			k;

	Assert(joinrels[level] == NIL);

	/* Set join_cur_level so that new joinrels are added to proper list */
	root->join_cur_level = level;

	/*
	 * First, consider left-sided and right-sided plans, in which rels of
	 * exactly level-1 member relations are joined against initial relations.
	 * We prefer to join using join clauses, but if we find a rel of level-1
	 * members that has no join clauses, we will generate Cartesian-product
	 * joins against all initial rels not already contained in it.
	 *
	 * In the first pass (level == 2), we try to join each initial rel to each
	 * initial rel that appears later in joinrels[1].  (The mirror-image joins
	 * are handled automatically by make_join_rel.)  In later passes, we try
	 * to join rels of size level-1 from joinrels[level-1] to each initial rel
	 * in joinrels[1].
	 */
	foreach(r, joinrels[level - 1])
	{
		RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
		ListCell   *other_rels;

		if (level == 2)
			other_rels = lnext(r);		/* only consider remaining initial
										 * rels */
		else
			other_rels = list_head(joinrels[1]);		/* consider all initial
														 * rels */

		if (old_rel->joininfo != NIL || old_rel->has_eclass_joins ||
			has_join_restriction(root, old_rel))
		{
			/*
			 * Note that if all available join clauses for this rel require
			 * more than one other rel, we will fail to make any joins against
			 * it here.  In most cases that's OK; it'll be considered by
			 * "bushy plan" join code in a higher-level pass where we have
			 * those other rels collected into a join rel.
			 *
			 * See also the last-ditch case below.
			 */
			make_rels_by_clause_joins(root,
									  old_rel,
									  other_rels);
		}
		else
		{
			/*
			 * Oops, we have a relation that is not joined to any other
			 * relation, either directly or by join-order restrictions.
			 * Cartesian product time.
			 */
			make_rels_by_clauseless_joins(root,
										  old_rel,
										  other_rels);
		}
	}

	/*
	 * Now, consider "bushy plans" in which relations of k initial rels are
	 * joined to relations of level-k initial rels, for 2 <= k <= level-2.
	 *
	 * We only consider bushy-plan joins for pairs of rels where there is a
	 * suitable join clause (or join order restriction), in order to avoid
	 * unreasonable growth of planning time.
	 */
	for (k = 2;; k++)
	{
		int			other_level = level - k;

		/*
		 * Since make_join_rel(x, y) handles both x,y and y,x cases, we only
		 * need to go as far as the halfway point.
		 */
		if (k > other_level)
			break;

		foreach(r, joinrels[k])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
			ListCell   *other_rels;
			ListCell   *r2;

			/*
			 * We can ignore clauseless joins here, *except* when they
			 * participate in join-order restrictions --- then we might have
			 * to force a bushy join plan.
			 */
			if (old_rel->joininfo == NIL && !old_rel->has_eclass_joins &&
				!has_join_restriction(root, old_rel))
				continue;

			if (k == other_level)
				other_rels = lnext(r);	/* only consider remaining rels */
			else
				other_rels = list_head(joinrels[other_level]);

			for_each_cell(r2, other_rels)
			{
				RelOptInfo *new_rel = (RelOptInfo *) lfirst(r2);

				if (!bms_overlap(old_rel->relids, new_rel->relids))
				{
					/*
					 * OK, we can build a rel of the right level from this
					 * pair of rels.  Do so if there is at least one usable
					 * join clause or a relevant join restriction.
					 */
					if (have_relevant_joinclause(root, old_rel, new_rel) ||
						have_join_order_restriction(root, old_rel, new_rel))
					{
						(void) make_join_rel(root, old_rel, new_rel);
					}
				}
			}
		}
	}

	/*
	 * Last-ditch effort: if we failed to find any usable joins so far, force
	 * a set of cartesian-product joins to be generated.  This handles the
	 * special case where all the available rels have join clauses but we
	 * cannot use any of those clauses yet.  An example is
	 *
	 * SELECT * FROM a,b,c WHERE (a.f1 + b.f2 + c.f3) = 0;
	 *
	 * The join clause will be usable at level 3, but at level 2 we have no
	 * choice but to make cartesian joins.	We consider only left-sided and
	 * right-sided cartesian joins in this case (no bushy).
	 */
	if (joinrels[level] == NIL)
	{
		/*
		 * This loop is just like the first one, except we always call
		 * make_rels_by_clauseless_joins().
		 */
		foreach(r, joinrels[level - 1])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
			ListCell   *other_rels;

			if (level == 2)
				other_rels = lnext(r);	/* only consider remaining initial
										 * rels */
			else
				other_rels = list_head(joinrels[1]);	/* consider all initial
														 * rels */

			make_rels_by_clauseless_joins(root,
										  old_rel,
										  other_rels);
		}

		/*----------
		 * When special joins are involved, there may be no legal way
		 * to make an N-way join for some values of N.	For example consider
		 *
		 * SELECT ... FROM t1 WHERE
		 *	 x IN (SELECT ... FROM t2,t3 WHERE ...) AND
		 *	 y IN (SELECT ... FROM t4,t5 WHERE ...)
		 *
		 * We will flatten this query to a 5-way join problem, but there are
		 * no 4-way joins that join_is_legal() will consider legal.  We have
		 * to accept failure at level 4 and go on to discover a workable
		 * bushy plan at level 5.
		 *
		 * However, if there are no special joins then join_is_legal() should
		 * never fail, and so the following sanity check is useful.
		 *----------
		 */
		if (joinrels[level] == NIL && root->join_info_list == NIL)
			elog(ERROR, "failed to build any %d-way joins", level);
	}
}

/*
 * make_rels_by_clause_joins
 *	  Build joins between the given relation 'old_rel' and other relations
 *	  that participate in join clauses that 'old_rel' also participates in
 *	  (or participate in join-order restrictions with it).
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * Note: at levels above 2 we will generate the same joined relation in
 * multiple ways --- for example (a join b) join c is the same RelOptInfo as
 * (b join c) join a, though the second case will add a different set of Paths
 * to it.  This is the reason for using the join_rel_level mechanism, which
 * automatically ensures that each new joinrel is only added to the list once.
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell in a linked list containing the other
 * rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it
 * will work for joining to joinrels too.
 */
static void
make_rels_by_clause_joins(PlannerInfo *root,
						  RelOptInfo *old_rel,
						  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(old_rel->relids, other_rel->relids) &&
			(have_relevant_joinclause(root, old_rel, other_rel) ||
			 have_join_order_restriction(root, old_rel, other_rel)))
		{
			(void) make_join_rel(root, old_rel, other_rel);
		}
	}
}

/*
 * make_rels_by_clauseless_joins
 *	  Given a relation 'old_rel' and a list of other relations
 *	  'other_rels', create a join relation between 'old_rel' and each
 *	  member of 'other_rels' that isn't already included in 'old_rel'.
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell of a linked list containing the
 * other rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it would
 * work for joining to joinrels too.
 */
static void
make_rels_by_clauseless_joins(PlannerInfo *root,
							  RelOptInfo *old_rel,
							  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(other_rel->relids, old_rel->relids))
		{
			(void) make_join_rel(root, old_rel, other_rel);
		}
	}
}

/*
 * has_join_restriction
 *		Detect whether the specified relation has join-order restrictions
 *		due to being inside an outer join or an IN (sub-SELECT).
 *
 * Essentially, this tests whether have_join_order_restriction() could
 * succeed with this rel and some other one.  It's OK if we sometimes
 * say "true" incorrectly.	(Therefore, we don't bother with the relatively
 * expensive has_legal_joinclause test.)
 */
static bool
has_join_restriction(PlannerInfo *root, RelOptInfo *rel)
{
	ListCell   *l;

	foreach(l, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(l);

		/* ignore full joins --- other mechanisms preserve their ordering */
		if (sjinfo->jointype == JOIN_FULL)
			continue;

		/* ignore if SJ is already contained in rel */
		if (bms_is_subset(sjinfo->min_lefthand, rel->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel->relids))
			continue;

		/* restricted if it overlaps LHS or RHS, but doesn't contain SJ */
		if (bms_overlap(sjinfo->min_lefthand, rel->relids) ||
			bms_overlap(sjinfo->min_righthand, rel->relids))
			return true;
	}

	return false;
}
