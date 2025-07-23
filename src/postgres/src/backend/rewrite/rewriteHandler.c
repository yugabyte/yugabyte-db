/*-------------------------------------------------------------------------
 *
 * rewriteHandler.c
 *		Primary module of query rewriter.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/rewrite/rewriteHandler.c
 *
 * NOTES
 *	  Some of the terms used in this file are of historic nature: "retrieve"
 *	  was the PostQUEL keyword for what today is SELECT. "RIR" stands for
 *	  "Retrieve-Instead-Retrieve", that is an ON SELECT DO INSTEAD SELECT rule
 *	  (which has to be unconditional and where only one rule can exist on each
 *	  relation).
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/sysattr.h"
#include "catalog/dependency.h"
#include "catalog/pg_type.h"
#include "commands/trigger.h"
#include "foreign/fdwapi.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/analyze.h"
#include "parser/parse_coerce.h"
#include "parser/parse_relation.h"
#include "parser/parsetree.h"
#include "rewrite/rewriteDefine.h"
#include "rewrite/rewriteHandler.h"
#include "rewrite/rewriteManip.h"
#include "rewrite/rowsecurity.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"

/* YB includes. */
#include "access/transam.h"
#include "executor/ybcModifyTable.h"
#include "miscadmin.h"
#include "pg_yb_utils.h"

/* We use a list of these to detect recursion in RewriteQuery */
typedef struct rewrite_event
{
	Oid			relation;		/* OID of relation having rules */
	CmdType		event;			/* type of rule being fired */
} rewrite_event;

typedef struct acquireLocksOnSubLinks_context
{
	bool		for_execute;	/* AcquireRewriteLocks' forExecute param */
} acquireLocksOnSubLinks_context;

typedef struct fireRIRonSubLink_context
{
	List	   *activeRIRs;
	bool		hasRowSecurity;
} fireRIRonSubLink_context;

static bool acquireLocksOnSubLinks(Node *node,
					   acquireLocksOnSubLinks_context *context);
static Query *rewriteRuleAction(Query *parsetree,
				  Query *rule_action,
				  Node *rule_qual,
				  int rt_index,
				  CmdType event,
				  bool *returning_flag);
static List *adjustJoinTreeList(Query *parsetree, bool removert, int rt_index);
static List *rewriteTargetListIU(List *targetList,
					CmdType commandType,
					OverridingKind override,
					Relation target_relation,
					int result_rti,
					List **attrno_list);
static TargetEntry *process_matched_tle(TargetEntry *src_tle,
					TargetEntry *prior_tle,
					const char *attrName);
static Node *get_assignment_input(Node *node);
static void rewriteValuesRTE(RangeTblEntry *rte, Relation target_relation,
				 List *attrnos);
static void markQueryForLocking(Query *qry, Node *jtnode,
					LockClauseStrength strength, LockWaitPolicy waitPolicy,
					bool pushedDown);
static List *matchLocks(CmdType event, RuleLock *rulelocks,
		   int varno, Query *parsetree, bool *hasUpdate);
static Query *fireRIRrules(Query *parsetree, List *activeRIRs);
static bool view_has_instead_trigger(Relation view, CmdType event);

static Bitmapset *adjust_view_column_set(Bitmapset *cols,
                                         List *targetlist,
                                         Relation view_rel,
                                         Oid base_relid);

/*
 * AcquireRewriteLocks -
 *	  Acquire suitable locks on all the relations mentioned in the Query.
 *	  These locks will ensure that the relation schemas don't change under us
 *	  while we are rewriting and planning the query.
 *
 * forExecute indicates that the query is about to be executed.
 * If so, we'll acquire RowExclusiveLock on the query's resultRelation,
 * RowShareLock on any relation accessed FOR [KEY] UPDATE/SHARE, and
 * AccessShareLock on all other relations mentioned.
 *
 * If forExecute is false, AccessShareLock is acquired on all relations.
 * This case is suitable for ruleutils.c, for example, where we only need
 * schema stability and we don't intend to actually modify any relations.
 *
 * forUpdatePushedDown indicates that a pushed-down FOR [KEY] UPDATE/SHARE
 * applies to the current subquery, requiring all rels to be opened with at
 * least RowShareLock.  This should always be false at the top of the
 * recursion.  This flag is ignored if forExecute is false.
 *
 * A secondary purpose of this routine is to fix up JOIN RTE references to
 * dropped columns (see details below).  Because the RTEs are modified in
 * place, it is generally appropriate for the caller of this routine to have
 * first done a copyObject() to make a writable copy of the querytree in the
 * current memory context.
 *
 * This processing can, and for efficiency's sake should, be skipped when the
 * querytree has just been built by the parser: parse analysis already got
 * all the same locks we'd get here, and the parser will have omitted dropped
 * columns from JOINs to begin with.  But we must do this whenever we are
 * dealing with a querytree produced earlier than the current command.
 *
 * About JOINs and dropped columns: although the parser never includes an
 * already-dropped column in a JOIN RTE's alias var list, it is possible for
 * such a list in a stored rule to include references to dropped columns.
 * (If the column is not explicitly referenced anywhere else in the query,
 * the dependency mechanism won't consider it used by the rule and so won't
 * prevent the column drop.)  To support get_rte_attribute_is_dropped(), we
 * replace join alias vars that reference dropped columns with null pointers.
 *
 * (In PostgreSQL 8.0, we did not do this processing but instead had
 * get_rte_attribute_is_dropped() recurse to detect dropped columns in joins.
 * That approach had horrible performance unfortunately; in particular
 * construction of a nested join was O(N^2) in the nesting depth.)
 */
void
AcquireRewriteLocks(Query *parsetree,
					bool forExecute,
					bool forUpdatePushedDown)
{
	ListCell   *l;
	int			rt_index;
	acquireLocksOnSubLinks_context context;

	context.for_execute = forExecute;

	/*
	 * First, process RTEs of the current query level.
	 */
	rt_index = 0;
	foreach(l, parsetree->rtable)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(l);
		Relation	rel;
		LOCKMODE	lockmode;
		List	   *newaliasvars;
		Index		curinputvarno;
		RangeTblEntry *curinputrte;
		ListCell   *ll;

		++rt_index;
		switch (rte->rtekind)
		{
			case RTE_RELATION:

				/*
				 * Grab the appropriate lock type for the relation, and do not
				 * release it until end of transaction. This protects the
				 * rewriter and planner against schema changes mid-query.
				 *
				 * Assuming forExecute is true, this logic must match what the
				 * executor will do, else we risk lock-upgrade deadlocks.
				 */
				if (!forExecute)
					lockmode = AccessShareLock;
				else if (rt_index == parsetree->resultRelation)
					lockmode = RowExclusiveLock;
				else if (forUpdatePushedDown ||
						 get_parse_rowmark(parsetree, rt_index) != NULL)
					lockmode = RowShareLock;
				else
					lockmode = AccessShareLock;

				rel = heap_open(rte->relid, lockmode);

				/*
				 * While we have the relation open, update the RTE's relkind,
				 * just in case it changed since this rule was made.
				 */
				rte->relkind = rel->rd_rel->relkind;

				heap_close(rel, NoLock);
				break;

			case RTE_JOIN:

				/*
				 * Scan the join's alias var list to see if any columns have
				 * been dropped, and if so replace those Vars with null
				 * pointers.
				 *
				 * Since a join has only two inputs, we can expect to see
				 * multiple references to the same input RTE; optimize away
				 * multiple fetches.
				 */
				newaliasvars = NIL;
				curinputvarno = 0;
				curinputrte = NULL;
				foreach(ll, rte->joinaliasvars)
				{
					Var		   *aliasitem = (Var *) lfirst(ll);
					Var		   *aliasvar = aliasitem;

					/* Look through any implicit coercion */
					aliasvar = (Var *) strip_implicit_coercions((Node *) aliasvar);

					/*
					 * If the list item isn't a simple Var, then it must
					 * represent a merged column, ie a USING column, and so it
					 * couldn't possibly be dropped, since it's referenced in
					 * the join clause.  (Conceivably it could also be a null
					 * pointer already?  But that's OK too.)
					 */
					if (aliasvar && IsA(aliasvar, Var))
					{
						/*
						 * The elements of an alias list have to refer to
						 * earlier RTEs of the same rtable, because that's the
						 * order the planner builds things in.  So we already
						 * processed the referenced RTE, and so it's safe to
						 * use get_rte_attribute_is_dropped on it. (This might
						 * not hold after rewriting or planning, but it's OK
						 * to assume here.)
						 */
						Assert(aliasvar->varlevelsup == 0);
						if (aliasvar->varno != curinputvarno)
						{
							curinputvarno = aliasvar->varno;
							if (curinputvarno >= rt_index)
								elog(ERROR, "unexpected varno %d in JOIN RTE %d",
									 curinputvarno, rt_index);
							curinputrte = rt_fetch(curinputvarno,
												   parsetree->rtable);
						}
						if (get_rte_attribute_is_dropped(curinputrte,
														 aliasvar->varattno))
						{
							/* Replace the join alias item with a NULL */
							aliasitem = NULL;
						}
					}
					newaliasvars = lappend(newaliasvars, aliasitem);
				}
				rte->joinaliasvars = newaliasvars;
				break;

			case RTE_SUBQUERY:

				/*
				 * The subquery RTE itself is all right, but we have to
				 * recurse to process the represented subquery.
				 */
				AcquireRewriteLocks(rte->subquery,
									forExecute,
									(forUpdatePushedDown ||
									 get_parse_rowmark(parsetree, rt_index) != NULL));
				break;

			default:
				/* ignore other types of RTEs */
				break;
		}
	}

	/* Recurse into subqueries in WITH */
	foreach(l, parsetree->cteList)
	{
		CommonTableExpr *cte = (CommonTableExpr *) lfirst(l);

		AcquireRewriteLocks((Query *) cte->ctequery, forExecute, false);
	}

	/*
	 * Recurse into sublink subqueries, too.  But we already did the ones in
	 * the rtable and cteList.
	 */
	if (parsetree->hasSubLinks)
		query_tree_walker(parsetree, acquireLocksOnSubLinks, &context,
						  QTW_IGNORE_RC_SUBQUERIES);
}

/*
 * Walker to find sublink subqueries for AcquireRewriteLocks
 */
static bool
acquireLocksOnSubLinks(Node *node, acquireLocksOnSubLinks_context *context)
{
	if (node == NULL)
		return false;
	if (IsA(node, SubLink))
	{
		SubLink    *sub = (SubLink *) node;

		/* Do what we came for */
		AcquireRewriteLocks((Query *) sub->subselect,
							context->for_execute,
							false);
		/* Fall through to process lefthand args of SubLink */
	}

	/*
	 * Do NOT recurse into Query nodes, because AcquireRewriteLocks already
	 * processed subselects of subselects for us.
	 */
	return expression_tree_walker(node, acquireLocksOnSubLinks, context);
}


/*
 * rewriteRuleAction -
 *	  Rewrite the rule action with appropriate qualifiers (taken from
 *	  the triggering query).
 *
 * Input arguments:
 *	parsetree - original query
 *	rule_action - one action (query) of a rule
 *	rule_qual - WHERE condition of rule, or NULL if unconditional
 *	rt_index - RT index of result relation in original query
 *	event - type of rule event
 * Output arguments:
 *	*returning_flag - set true if we rewrite RETURNING clause in rule_action
 *					(must be initialized to false)
 * Return value:
 *	rewritten form of rule_action
 */
static Query *
rewriteRuleAction(Query *parsetree,
				  Query *rule_action,
				  Node *rule_qual,
				  int rt_index,
				  CmdType event,
				  bool *returning_flag)
{
	int			current_varno,
				new_varno;
	int			rt_length;
	Query	   *sub_action;
	Query	  **sub_action_ptr;
	acquireLocksOnSubLinks_context context;

	context.for_execute = true;

	/*
	 * Make modifiable copies of rule action and qual (what we're passed are
	 * the stored versions in the relcache; don't touch 'em!).
	 */
	rule_action = copyObject(rule_action);
	rule_qual = copyObject(rule_qual);

	/*
	 * Acquire necessary locks and fix any deleted JOIN RTE entries.
	 */
	AcquireRewriteLocks(rule_action, true, false);
	(void) acquireLocksOnSubLinks(rule_qual, &context);

	current_varno = rt_index;
	rt_length = list_length(parsetree->rtable);
	new_varno = PRS2_NEW_VARNO + rt_length;

	/*
	 * Adjust rule action and qual to offset its varnos, so that we can merge
	 * its rtable with the main parsetree's rtable.
	 *
	 * If the rule action is an INSERT...SELECT, the OLD/NEW rtable entries
	 * will be in the SELECT part, and we have to modify that rather than the
	 * top-level INSERT (kluge!).
	 */
	sub_action = getInsertSelectQuery(rule_action, &sub_action_ptr);

	OffsetVarNodes((Node *) sub_action, rt_length, 0);
	OffsetVarNodes(rule_qual, rt_length, 0);
	/* but references to OLD should point at original rt_index */
	ChangeVarNodes((Node *) sub_action,
				   PRS2_OLD_VARNO + rt_length, rt_index, 0);
	ChangeVarNodes(rule_qual,
				   PRS2_OLD_VARNO + rt_length, rt_index, 0);

	/*
	 * Generate expanded rtable consisting of main parsetree's rtable plus
	 * rule action's rtable; this becomes the complete rtable for the rule
	 * action.  Some of the entries may be unused after we finish rewriting,
	 * but we leave them all in place for two reasons:
	 *
	 * We'd have a much harder job to adjust the query's varnos if we
	 * selectively removed RT entries.
	 *
	 * If the rule is INSTEAD, then the original query won't be executed at
	 * all, and so its rtable must be preserved so that the executor will do
	 * the correct permissions checks on it.
	 *
	 * RT entries that are not referenced in the completed jointree will be
	 * ignored by the planner, so they do not affect query semantics.  But any
	 * permissions checks specified in them will be applied during executor
	 * startup (see ExecCheckRTEPerms()).  This allows us to check that the
	 * caller has, say, insert-permission on a view, when the view is not
	 * semantically referenced at all in the resulting query.
	 *
	 * When a rule is not INSTEAD, the permissions checks done on its copied
	 * RT entries will be redundant with those done during execution of the
	 * original query, but we don't bother to treat that case differently.
	 *
	 * NOTE: because planner will destructively alter rtable, we must ensure
	 * that rule action's rtable is separate and shares no substructure with
	 * the main rtable.  Hence do a deep copy here.
	 */
	sub_action->rtable = list_concat(copyObject(parsetree->rtable),
									 sub_action->rtable);

	/*
	 * There could have been some SubLinks in parsetree's rtable, in which
	 * case we'd better mark the sub_action correctly.
	 */
	if (parsetree->hasSubLinks && !sub_action->hasSubLinks)
	{
		ListCell   *lc;

		foreach(lc, parsetree->rtable)
		{
			RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);

			switch (rte->rtekind)
			{
				case RTE_RELATION:
					sub_action->hasSubLinks =
						checkExprHasSubLink((Node *) rte->tablesample);
					break;
				case RTE_FUNCTION:
					sub_action->hasSubLinks =
						checkExprHasSubLink((Node *) rte->functions);
					break;
				case RTE_TABLEFUNC:
					sub_action->hasSubLinks =
						checkExprHasSubLink((Node *) rte->tablefunc);
					break;
				case RTE_VALUES:
					sub_action->hasSubLinks =
						checkExprHasSubLink((Node *) rte->values_lists);
					break;
				default:
					/* other RTE types don't contain bare expressions */
					break;
			}
			if (sub_action->hasSubLinks)
				break;			/* no need to keep scanning rtable */
		}
	}

	/*
	 * Also, we might have absorbed some RTEs with RLS conditions into the
	 * sub_action.  If so, mark it as hasRowSecurity, whether or not those
	 * RTEs will be referenced after we finish rewriting.  (Note: currently
	 * this is a no-op because RLS conditions aren't added till later, but it
	 * seems like good future-proofing to do this anyway.)
	 */
	sub_action->hasRowSecurity |= parsetree->hasRowSecurity;

	/*
	 * Each rule action's jointree should be the main parsetree's jointree
	 * plus that rule's jointree, but usually *without* the original rtindex
	 * that we're replacing (if present, which it won't be for INSERT). Note
	 * that if the rule action refers to OLD, its jointree will add a
	 * reference to rt_index.  If the rule action doesn't refer to OLD, but
	 * either the rule_qual or the user query quals do, then we need to keep
	 * the original rtindex in the jointree to provide data for the quals.  We
	 * don't want the original rtindex to be joined twice, however, so avoid
	 * keeping it if the rule action mentions it.
	 *
	 * As above, the action's jointree must not share substructure with the
	 * main parsetree's.
	 */
	if (sub_action->commandType != CMD_UTILITY)
	{
		bool		keeporig;
		List	   *newjointree;

		Assert(sub_action->jointree != NULL);
		keeporig = (!rangeTableEntry_used((Node *) sub_action->jointree,
										  rt_index, 0)) &&
			(rangeTableEntry_used(rule_qual, rt_index, 0) ||
			 rangeTableEntry_used(parsetree->jointree->quals, rt_index, 0));
		newjointree = adjustJoinTreeList(parsetree, !keeporig, rt_index);
		if (newjointree != NIL)
		{
			/*
			 * If sub_action is a setop, manipulating its jointree will do no
			 * good at all, because the jointree is dummy.  (Perhaps someday
			 * we could push the joining and quals down to the member
			 * statements of the setop?)
			 */
			if (sub_action->setOperations != NULL)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("conditional UNION/INTERSECT/EXCEPT statements are not implemented")));

			sub_action->jointree->fromlist =
				list_concat(newjointree, sub_action->jointree->fromlist);

			/*
			 * There could have been some SubLinks in newjointree, in which
			 * case we'd better mark the sub_action correctly.
			 */
			if (parsetree->hasSubLinks && !sub_action->hasSubLinks)
				sub_action->hasSubLinks =
					checkExprHasSubLink((Node *) newjointree);
		}
	}

	/*
	 * If the original query has any CTEs, copy them into the rule action. But
	 * we don't need them for a utility action.
	 */
	if (parsetree->cteList != NIL && sub_action->commandType != CMD_UTILITY)
	{
		ListCell   *lc;

		/*
		 * Annoying implementation restriction: because CTEs are identified by
		 * name within a cteList, we can't merge a CTE from the original query
		 * if it has the same name as any CTE in the rule action.
		 *
		 * This could possibly be fixed by using some sort of internally
		 * generated ID, instead of names, to link CTE RTEs to their CTEs.
		 */
		foreach(lc, parsetree->cteList)
		{
			CommonTableExpr *cte = (CommonTableExpr *) lfirst(lc);
			ListCell   *lc2;

			foreach(lc2, sub_action->cteList)
			{
				CommonTableExpr *cte2 = (CommonTableExpr *) lfirst(lc2);

				if (strcmp(cte->ctename, cte2->ctename) == 0)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("WITH query name \"%s\" appears in both a rule action and the query being rewritten",
									cte->ctename)));
			}
		}

		/* OK, it's safe to combine the CTE lists */
		sub_action->cteList = list_concat(sub_action->cteList,
										  copyObject(parsetree->cteList));
	}

	/*
	 * Event Qualification forces copying of parsetree and splitting into two
	 * queries one w/rule_qual, one w/NOT rule_qual. Also add user query qual
	 * onto rule action
	 */
	AddQual(sub_action, rule_qual);

	AddQual(sub_action, parsetree->jointree->quals);

	/*
	 * Rewrite new.attribute with right hand side of target-list entry for
	 * appropriate field name in insert/update.
	 *
	 * KLUGE ALERT: since ReplaceVarsFromTargetList returns a mutated copy, we
	 * can't just apply it to sub_action; we have to remember to update the
	 * sublink inside rule_action, too.
	 */
	if ((event == CMD_INSERT || event == CMD_UPDATE) &&
		sub_action->commandType != CMD_UTILITY)
	{
		sub_action = (Query *)
			ReplaceVarsFromTargetList((Node *) sub_action,
									  new_varno,
									  0,
									  rt_fetch(new_varno, sub_action->rtable),
									  parsetree->targetList,
									  (event == CMD_UPDATE) ?
									  REPLACEVARS_CHANGE_VARNO :
									  REPLACEVARS_SUBSTITUTE_NULL,
									  current_varno,
									  NULL);
		if (sub_action_ptr)
			*sub_action_ptr = sub_action;
		else
			rule_action = sub_action;
	}

	/*
	 * If rule_action has a RETURNING clause, then either throw it away if the
	 * triggering query has no RETURNING clause, or rewrite it to emit what
	 * the triggering query's RETURNING clause asks for.  Throw an error if
	 * more than one rule has a RETURNING clause.
	 */
	if (!parsetree->returningList)
		rule_action->returningList = NIL;
	else if (rule_action->returningList)
	{
		if (*returning_flag)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot have RETURNING lists in multiple rules")));
		*returning_flag = true;
		rule_action->returningList = (List *)
			ReplaceVarsFromTargetList((Node *) parsetree->returningList,
									  parsetree->resultRelation,
									  0,
									  rt_fetch(parsetree->resultRelation,
											   parsetree->rtable),
									  rule_action->returningList,
									  REPLACEVARS_REPORT_ERROR,
									  0,
									  &rule_action->hasSubLinks);

		/*
		 * There could have been some SubLinks in parsetree's returningList,
		 * in which case we'd better mark the rule_action correctly.
		 */
		if (parsetree->hasSubLinks && !rule_action->hasSubLinks)
			rule_action->hasSubLinks =
				checkExprHasSubLink((Node *) rule_action->returningList);
	}

	return rule_action;
}

/*
 * Copy the query's jointree list, and optionally attempt to remove any
 * occurrence of the given rt_index as a top-level join item (we do not look
 * for it within join items; this is OK because we are only expecting to find
 * it as an UPDATE or DELETE target relation, which will be at the top level
 * of the join).  Returns modified jointree list --- this is a separate copy
 * sharing no nodes with the original.
 */
static List *
adjustJoinTreeList(Query *parsetree, bool removert, int rt_index)
{
	List	   *newjointree = copyObject(parsetree->jointree->fromlist);
	ListCell   *l;

	if (removert)
	{
		foreach(l, newjointree)
		{
			RangeTblRef *rtr = lfirst(l);

			if (IsA(rtr, RangeTblRef) &&
				rtr->rtindex == rt_index)
			{
				newjointree = list_delete_ptr(newjointree, rtr);

				/*
				 * foreach is safe because we exit loop after list_delete...
				 */
				break;
			}
		}
	}
	return newjointree;
}


/*
 * rewriteTargetListIU - rewrite INSERT/UPDATE targetlist into standard form
 *
 * This has the following responsibilities:
 *
 * 1. For an INSERT, add tlist entries to compute default values for any
 * attributes that have defaults and are not assigned to in the given tlist.
 * (We do not insert anything for default-less attributes, however.  The
 * planner will later insert NULLs for them, but there's no reason to slow
 * down rewriter processing with extra tlist nodes.)  Also, for both INSERT
 * and UPDATE, replace explicit DEFAULT specifications with column default
 * expressions.
 *
 * 2. For an UPDATE on a trigger-updatable view, add tlist entries for any
 * unassigned-to attributes, assigning them their old values.  These will
 * later get expanded to the output values of the view.  (This is equivalent
 * to what the planner's expand_targetlist() will do for UPDATE on a regular
 * table, but it's more convenient to do it here while we still have easy
 * access to the view's original RT index.)  This is only necessary for
 * trigger-updatable views, for which the view remains the result relation of
 * the query.  For auto-updatable views we must not do this, since it might
 * add assignments to non-updatable view columns.  For rule-updatable views it
 * is unnecessary extra work, since the query will be rewritten with a
 * different result relation which will be processed when we recurse via
 * RewriteQuery.
 *
 * 3. Merge multiple entries for the same target attribute, or declare error
 * if we can't.  Multiple entries are only allowed for INSERT/UPDATE of
 * portions of an array or record field, for example
 *			UPDATE table SET foo[2] = 42, foo[4] = 43;
 * We can merge such operations into a single assignment op.  Essentially,
 * the expression we want to produce in this case is like
 *		foo = array_set_element(array_set_element(foo, 2, 42), 4, 43)
 *
 * 4. Sort the tlist into standard order: non-junk fields in order by resno,
 * then junk fields (these in no particular order).
 *
 * We must do items 1,2,3 before firing rewrite rules, else rewritten
 * references to NEW.foo will produce wrong or incomplete results.  Item 4
 * is not needed for rewriting, but will be needed by the planner, and we
 * can do it essentially for free while handling the other items.
 *
 * If attrno_list isn't NULL, we return an additional output besides the
 * rewritten targetlist: an integer list of the assigned-to attnums, in
 * order of the original tlist's non-junk entries.  This is needed for
 * processing VALUES RTEs.
 *
 * Note that for an inheritable UPDATE, this processing is only done once,
 * using the parent relation as reference.  It must not do anything that
 * will not be correct when transposed to the child relation(s).  (Step 4
 * is incorrect by this light, since child relations might have different
 * column ordering, but the planner will fix things by re-sorting the tlist
 * for each child.)
 */
static List *
rewriteTargetListIU(List *targetList,
					CmdType commandType,
					OverridingKind override,
					Relation target_relation,
					int result_rti,
					List **attrno_list)
{
	TargetEntry **new_tles;
	List	   *new_tlist = NIL;
	List	   *junk_tlist = NIL;
	Form_pg_attribute att_tup;
	int			attrno,
				next_junk_attrno,
				numattrs;
	ListCell   *temp;

	if (attrno_list)			/* initialize optional result list */
		*attrno_list = NIL;

	/*
	 * We process the normal (non-junk) attributes by scanning the input tlist
	 * once and transferring TLEs into an array, then scanning the array to
	 * build an output tlist.  This avoids O(N^2) behavior for large numbers
	 * of attributes.
	 *
	 * Junk attributes are tossed into a separate list during the same tlist
	 * scan, then appended to the reconstructed tlist.
	 */
	numattrs = RelationGetNumberOfAttributes(target_relation);
	new_tles = (TargetEntry **) palloc0(numattrs * sizeof(TargetEntry *));
	next_junk_attrno = numattrs + 1;

	foreach(temp, targetList)
	{
		TargetEntry *old_tle = (TargetEntry *) lfirst(temp);

		if (!old_tle->resjunk)
		{
			/* Normal attr: stash it into new_tles[] */
			attrno = old_tle->resno;
			if ((!IsYsqlUpgrade && attrno < 1) || attrno > numattrs)
				elog(ERROR, "bogus resno %d in targetlist", attrno);

			/* put attrno into attrno_list even if it's dropped */
			if (attrno_list)
				*attrno_list = lappend_int(*attrno_list, attrno);

			/* Pass system column as-is. */
			if (IsYsqlUpgrade && attrno < 1)
			{
				if (attrno != ObjectIdAttributeNumber)
					elog(ERROR, "can't reference system columns other than oid");

				if (commandType != CMD_INSERT)
					elog(ERROR, "can't UPDATE oid");

				if (old_tle == NULL || !old_tle->expr || IsA(old_tle->expr, SetToDefault))
					elog(ERROR, "oid should have a value specified");

				new_tlist = lappend(new_tlist, old_tle);
				continue;
			}

			att_tup = TupleDescAttr(target_relation->rd_att, attrno - 1);

			/* We can (and must) ignore deleted attributes */
			if (att_tup->attisdropped)
				continue;

			/* Merge with any prior assignment to same attribute */
			new_tles[attrno - 1] =
				process_matched_tle(old_tle,
									new_tles[attrno - 1],
									NameStr(att_tup->attname));
		}
		else
		{
			/*
			 * Copy all resjunk tlist entries to junk_tlist, and assign them
			 * resnos above the last real resno.
			 *
			 * Typical junk entries include ORDER BY or GROUP BY expressions
			 * (are these actually possible in an INSERT or UPDATE?), system
			 * attribute references, etc.
			 */

			/* Get the resno right, but don't copy unnecessarily */
			if (old_tle->resno != next_junk_attrno)
			{
				old_tle = flatCopyTargetEntry(old_tle);
				old_tle->resno = next_junk_attrno;
			}
			junk_tlist = lappend(junk_tlist, old_tle);
			next_junk_attrno++;
		}
	}

	for (attrno = 1; attrno <= numattrs; attrno++)
	{
		TargetEntry *new_tle = new_tles[attrno - 1];
		bool		apply_default;

		att_tup = TupleDescAttr(target_relation->rd_att, attrno - 1);

		/* We can (and must) ignore deleted attributes */
		if (att_tup->attisdropped)
			continue;

		/*
		 * Handle the two cases where we need to insert a default expression:
		 * it's an INSERT and there's no tlist entry for the column, or the
		 * tlist entry is a DEFAULT placeholder node.
		 */
		apply_default = ((new_tle == NULL && commandType == CMD_INSERT) ||
						 (new_tle && new_tle->expr && IsA(new_tle->expr, SetToDefault)));

		if (commandType == CMD_INSERT)
		{
			if (att_tup->attidentity == ATTRIBUTE_IDENTITY_ALWAYS && !apply_default)
			{
				if (override != OVERRIDING_SYSTEM_VALUE)
					ereport(ERROR,
							(errcode(ERRCODE_GENERATED_ALWAYS),
							 errmsg("cannot insert into column \"%s\"", NameStr(att_tup->attname)),
							 errdetail("Column \"%s\" is an identity column defined as GENERATED ALWAYS.",
									   NameStr(att_tup->attname)),
							 errhint("Use OVERRIDING SYSTEM VALUE to override.")));
			}

			if (att_tup->attidentity == ATTRIBUTE_IDENTITY_BY_DEFAULT && override == OVERRIDING_USER_VALUE)
				apply_default = true;
		}

		if (commandType == CMD_UPDATE)
		{
			if (att_tup->attidentity == ATTRIBUTE_IDENTITY_ALWAYS && new_tle && !apply_default)
				ereport(ERROR,
						(errcode(ERRCODE_GENERATED_ALWAYS),
						 errmsg("column \"%s\" can only be updated to DEFAULT", NameStr(att_tup->attname)),
						 errdetail("Column \"%s\" is an identity column defined as GENERATED ALWAYS.",
								   NameStr(att_tup->attname))));
		}

		if (apply_default)
		{
			Node	   *new_expr;

			new_expr = build_column_default(target_relation, attrno);

			/*
			 * If there is no default (ie, default is effectively NULL), we
			 * can omit the tlist entry in the INSERT case, since the planner
			 * can insert a NULL for itself, and there's no point in spending
			 * any more rewriter cycles on the entry.  But in the UPDATE case
			 * we've got to explicitly set the column to NULL.
			 */
			if (!new_expr)
			{
				if (commandType == CMD_INSERT)
					new_tle = NULL;
				else
				{
					new_expr = (Node *) makeConst(att_tup->atttypid,
												  -1,
												  att_tup->attcollation,
												  att_tup->attlen,
												  (Datum) 0,
												  true, /* isnull */
												  att_tup->attbyval);
					/* this is to catch a NOT NULL domain constraint */
					new_expr = coerce_to_domain(new_expr,
												InvalidOid, -1,
												att_tup->atttypid,
												COERCION_IMPLICIT,
												COERCE_IMPLICIT_CAST,
												-1,
												false);
				}
			}

			if (new_expr)
				new_tle = makeTargetEntry((Expr *) new_expr,
										  attrno,
										  pstrdup(NameStr(att_tup->attname)),
										  false);
		}

		/*
		 * For an UPDATE on a trigger-updatable view, provide a dummy entry
		 * whenever there is no explicit assignment.
		 */
		if (new_tle == NULL && commandType == CMD_UPDATE &&
			target_relation->rd_rel->relkind == RELKIND_VIEW &&
			view_has_instead_trigger(target_relation, CMD_UPDATE))
		{
			Node	   *new_expr;

			new_expr = (Node *) makeVar(result_rti,
										attrno,
										att_tup->atttypid,
										att_tup->atttypmod,
										att_tup->attcollation,
										0);

			new_tle = makeTargetEntry((Expr *) new_expr,
									  attrno,
									  pstrdup(NameStr(att_tup->attname)),
									  false);
		}

		if (new_tle)
			new_tlist = lappend(new_tlist, new_tle);
	}

	pfree(new_tles);

	return list_concat(new_tlist, junk_tlist);
}


/*
 * Convert a matched TLE from the original tlist into a correct new TLE.
 *
 * This routine detects and handles multiple assignments to the same target
 * attribute.  (The attribute name is needed only for error messages.)
 */
static TargetEntry *
process_matched_tle(TargetEntry *src_tle,
					TargetEntry *prior_tle,
					const char *attrName)
{
	TargetEntry *result;
	CoerceToDomain *coerce_expr = NULL;
	Node	   *src_expr;
	Node	   *prior_expr;
	Node	   *src_input;
	Node	   *prior_input;
	Node	   *priorbottom;
	Node	   *newexpr;

	if (prior_tle == NULL)
	{
		/*
		 * Normal case where this is the first assignment to the attribute.
		 */
		return src_tle;
	}

	/*----------
	 * Multiple assignments to same attribute.  Allow only if all are
	 * FieldStore or ArrayRef assignment operations.  This is a bit
	 * tricky because what we may actually be looking at is a nest of
	 * such nodes; consider
	 *		UPDATE tab SET col.fld1.subfld1 = x, col.fld2.subfld2 = y
	 * The two expressions produced by the parser will look like
	 *		FieldStore(col, fld1, FieldStore(placeholder, subfld1, x))
	 *		FieldStore(col, fld2, FieldStore(placeholder, subfld2, y))
	 * However, we can ignore the substructure and just consider the top
	 * FieldStore or ArrayRef from each assignment, because it works to
	 * combine these as
	 *		FieldStore(FieldStore(col, fld1,
	 *							  FieldStore(placeholder, subfld1, x)),
	 *				   fld2, FieldStore(placeholder, subfld2, y))
	 * Note the leftmost expression goes on the inside so that the
	 * assignments appear to occur left-to-right.
	 *
	 * For FieldStore, instead of nesting we can generate a single
	 * FieldStore with multiple target fields.  We must nest when
	 * ArrayRefs are involved though.
	 *
	 * As a further complication, the destination column might be a domain,
	 * resulting in each assignment containing a CoerceToDomain node over a
	 * FieldStore or ArrayRef.  These should have matching target domains,
	 * so we strip them and reconstitute a single CoerceToDomain over the
	 * combined FieldStore/ArrayRef nodes.  (Notice that this has the result
	 * that the domain's checks are applied only after we do all the field or
	 * element updates, not after each one.  This is arguably desirable.)
	 *----------
	 */
	src_expr = (Node *) src_tle->expr;
	prior_expr = (Node *) prior_tle->expr;

	if (src_expr && IsA(src_expr, CoerceToDomain) &&
		prior_expr && IsA(prior_expr, CoerceToDomain) &&
		((CoerceToDomain *) src_expr)->resulttype ==
		((CoerceToDomain *) prior_expr)->resulttype)
	{
		/* we assume without checking that resulttypmod/resultcollid match */
		coerce_expr = (CoerceToDomain *) src_expr;
		src_expr = (Node *) ((CoerceToDomain *) src_expr)->arg;
		prior_expr = (Node *) ((CoerceToDomain *) prior_expr)->arg;
	}

	src_input = get_assignment_input(src_expr);
	prior_input = get_assignment_input(prior_expr);
	if (src_input == NULL ||
		prior_input == NULL ||
		exprType(src_expr) != exprType(prior_expr))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("multiple assignments to same column \"%s\"",
						attrName)));

	/*
	 * Prior TLE could be a nest of assignments if we do this more than once.
	 */
	priorbottom = prior_input;
	for (;;)
	{
		Node	   *newbottom = get_assignment_input(priorbottom);

		if (newbottom == NULL)
			break;				/* found the original Var reference */
		priorbottom = newbottom;
	}
	if (!equal(priorbottom, src_input))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("multiple assignments to same column \"%s\"",
						attrName)));

	/*
	 * Looks OK to nest 'em.
	 */
	if (IsA(src_expr, FieldStore))
	{
		FieldStore *fstore = makeNode(FieldStore);

		if (IsA(prior_expr, FieldStore))
		{
			/* combine the two */
			memcpy(fstore, prior_expr, sizeof(FieldStore));
			fstore->newvals =
				list_concat(list_copy(((FieldStore *) prior_expr)->newvals),
							list_copy(((FieldStore *) src_expr)->newvals));
			fstore->fieldnums =
				list_concat(list_copy(((FieldStore *) prior_expr)->fieldnums),
							list_copy(((FieldStore *) src_expr)->fieldnums));
		}
		else
		{
			/* general case, just nest 'em */
			memcpy(fstore, src_expr, sizeof(FieldStore));
			fstore->arg = (Expr *) prior_expr;
		}
		newexpr = (Node *) fstore;
	}
	else if (IsA(src_expr, ArrayRef))
	{
		ArrayRef   *aref = makeNode(ArrayRef);

		memcpy(aref, src_expr, sizeof(ArrayRef));
		aref->refexpr = (Expr *) prior_expr;
		newexpr = (Node *) aref;
	}
	else
	{
		elog(ERROR, "cannot happen");
		newexpr = NULL;
	}

	if (coerce_expr)
	{
		/* put back the CoerceToDomain */
		CoerceToDomain *newcoerce = makeNode(CoerceToDomain);

		memcpy(newcoerce, coerce_expr, sizeof(CoerceToDomain));
		newcoerce->arg = (Expr *) newexpr;
		newexpr = (Node *) newcoerce;
	}

	result = flatCopyTargetEntry(src_tle);
	result->expr = (Expr *) newexpr;
	return result;
}

/*
 * If node is an assignment node, return its input; else return NULL
 */
static Node *
get_assignment_input(Node *node)
{
	if (node == NULL)
		return NULL;
	if (IsA(node, FieldStore))
	{
		FieldStore *fstore = (FieldStore *) node;

		return (Node *) fstore->arg;
	}
	else if (IsA(node, ArrayRef))
	{
		ArrayRef   *aref = (ArrayRef *) node;

		if (aref->refassgnexpr == NULL)
			return NULL;
		return (Node *) aref->refexpr;
	}
	return NULL;
}

/*
 * Make an expression tree for the default value for a column.
 *
 * If there is no default, return a NULL instead.
 */
Node *
build_column_default(Relation rel, int attrno)
{
	TupleDesc	rd_att = rel->rd_att;
	Form_pg_attribute att_tup = TupleDescAttr(rd_att, attrno - 1);
	Oid			atttype = att_tup->atttypid;
	int32		atttypmod = att_tup->atttypmod;
	Node	   *expr = NULL;
	Oid			exprtype;

	if (att_tup->attidentity)
	{
		NextValueExpr *nve = makeNode(NextValueExpr);

		nve->seqid = getOwnedSequence(RelationGetRelid(rel), attrno);
		nve->typeId = att_tup->atttypid;

		return (Node *) nve;
	}

	/*
	 * Scan to see if relation has a default for this column.
	 */
	if (att_tup->atthasdef && rd_att->constr &&
		rd_att->constr->num_defval > 0)
	{
		AttrDefault *defval = rd_att->constr->defval;
		int			ndef = rd_att->constr->num_defval;

		while (--ndef >= 0)
		{
			if (attrno == defval[ndef].adnum)
			{
				/*
				 * Found it, convert string representation to node tree.
				 */
				expr = stringToNode(defval[ndef].adbin);
				break;
			}
		}
	}

	if (expr == NULL)
	{
		/*
		 * No per-column default, so look for a default for the type itself.
		 */
		expr = get_typdefault(atttype);
	}

	if (expr == NULL)
		return NULL;			/* No default anywhere */

	/*
	 * Make sure the value is coerced to the target column type; this will
	 * generally be true already, but there seem to be some corner cases
	 * involving domain defaults where it might not be true. This should match
	 * the parser's processing of non-defaulted expressions --- see
	 * transformAssignedExpr().
	 */
	exprtype = exprType(expr);

	expr = coerce_to_target_type(NULL,	/* no UNKNOWN params here */
								 expr, exprtype,
								 atttype, atttypmod,
								 COERCION_ASSIGNMENT,
								 COERCE_IMPLICIT_CAST,
								 -1);
	if (expr == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("column \"%s\" is of type %s"
						" but default expression is of type %s",
						NameStr(att_tup->attname),
						format_type_be(atttype),
						format_type_be(exprtype)),
				 errhint("You will need to rewrite or cast the expression.")));

	return expr;
}


/* Does VALUES RTE contain any SetToDefault items? */
static bool
searchForDefault(RangeTblEntry *rte)
{
	ListCell   *lc;

	foreach(lc, rte->values_lists)
	{
		List	   *sublist = (List *) lfirst(lc);
		ListCell   *lc2;

		foreach(lc2, sublist)
		{
			Node	   *col = (Node *) lfirst(lc2);

			if (IsA(col, SetToDefault))
				return true;
		}
	}
	return false;
}

/*
 * When processing INSERT ... VALUES with a VALUES RTE (ie, multiple VALUES
 * lists), we have to replace any DEFAULT items in the VALUES lists with
 * the appropriate default expressions.  The other aspects of targetlist
 * rewriting need be applied only to the query's targetlist proper.
 *
 * Note that we currently can't support subscripted or field assignment
 * in the multi-VALUES case.  The targetlist will contain simple Vars
 * referencing the VALUES RTE, and therefore process_matched_tle() will
 * reject any such attempt with "multiple assignments to same column".
 */
static void
rewriteValuesRTE(RangeTblEntry *rte, Relation target_relation, List *attrnos)
{
	List	   *newValues;
	ListCell   *lc;

	/*
	 * Rebuilding all the lists is a pretty expensive proposition in a big
	 * VALUES list, and it's a waste of time if there aren't any DEFAULT
	 * placeholders.  So first scan to see if there are any.
	 */
	if (!searchForDefault(rte))
		return;					/* nothing to do */

	/* Check list lengths (we can assume all the VALUES sublists are alike) */
	Assert(list_length(attrnos) == list_length(linitial(rte->values_lists)));

	newValues = NIL;
	foreach(lc, rte->values_lists)
	{
		List	   *sublist = (List *) lfirst(lc);
		List	   *newList = NIL;
		ListCell   *lc2;
		ListCell   *lc3;

		forboth(lc2, sublist, lc3, attrnos)
		{
			Node	   *col = (Node *) lfirst(lc2);
			int			attrno = lfirst_int(lc3);

			if (IsA(col, SetToDefault))
			{
				Form_pg_attribute att_tup;
				Node	   *new_expr;

				att_tup = TupleDescAttr(target_relation->rd_att, attrno - 1);

				if (!att_tup->attisdropped)
					new_expr = build_column_default(target_relation, attrno);
				else
					new_expr = NULL;	/* force a NULL if dropped */

				/*
				 * If there is no default (ie, default is effectively NULL),
				 * we've got to explicitly set the column to NULL.
				 */
				if (!new_expr)
				{
					new_expr = (Node *) makeConst(att_tup->atttypid,
												  -1,
												  att_tup->attcollation,
												  att_tup->attlen,
												  (Datum) 0,
												  true, /* isnull */
												  att_tup->attbyval);
					/* this is to catch a NOT NULL domain constraint */
					new_expr = coerce_to_domain(new_expr,
												InvalidOid, -1,
												att_tup->atttypid,
												COERCION_IMPLICIT,
												COERCE_IMPLICIT_CAST,
												-1,
												false);
				}
				newList = lappend(newList, new_expr);
			}
			else
				newList = lappend(newList, col);
		}
		newValues = lappend(newValues, newList);
	}
	rte->values_lists = newValues;
}

static void
YbAddWholeRowAttrIfNeeded(Query *parsetree,
						  RangeTblEntry *target_rte,
						  Relation target_relation)
{
	bool isneeded = false;
	switch(parsetree->commandType)
	{
		case CMD_UPDATE:
			if (YbIsUpdateOptimizationEnabled())
			{
				isneeded = true;
				break;
			}

			/* If update optimizations are not enabled */
			switch_fallthrough();
		case CMD_DELETE:
			if (YBRelHasOldRowTriggers(target_relation,
									   parsetree->commandType) ||
				YBRelHasSecondaryIndices(target_relation))
			{
				isneeded = true;
			}

			/* wholerow attr not needed */
			break;
		default:
			elog(ERROR, "expected command to be one of (UPDATE, DELETE)");
	}

	if (!isneeded)
		return;

	Var		   *var = NULL;
	const char *attrname;
	TargetEntry *tle;

	var = makeWholeRowVar(target_rte, parsetree->resultRelation, 0, false);
	attrname = "wholerow";

	tle = makeTargetEntry((Expr *) var,
						  list_length(parsetree->targetList) + 1,
						  pstrdup(attrname),
						  true);
	parsetree->targetList = lappend(parsetree->targetList, tle);
}

/*
 * rewriteTargetListUD - rewrite UPDATE/DELETE targetlist as needed
 *
 * This function adds a "junk" TLE that is needed to allow the executor to
 * find the original row for the update or delete.  When the target relation
 * is a regular table, the junk TLE emits the ctid attribute of the original
 * row.  When the target relation is a foreign table, we let the FDW decide
 * what to add.
 *
 * We used to do this during RewriteQuery(), but now that inheritance trees
 * can contain a mix of regular and foreign tables, we must postpone it till
 * planning, after the inheritance tree has been expanded.  In that way we
 * can do the right thing for each child table.
 */
void
rewriteTargetListUD(Query *parsetree, RangeTblEntry *target_rte,
					Relation target_relation)
{
	Var		   *var = NULL;
	const char *attrname;
	TargetEntry *tle;

	if (IsYBRelation(target_relation))
	{
		YbAddWholeRowAttrIfNeeded(parsetree, target_rte, target_relation);

		/*
		 * Emit ybctid so that executor can find the row to update or delete from YugaByte tables.
		 */
		var = makeVar(parsetree->resultRelation,
					  YBTupleIdAttributeNumber,
					  BYTEAOID,
					  -1,
					  InvalidOid,
					  0);
		attrname = "ybctid";
	}
	else if (target_relation->rd_rel->relkind == RELKIND_RELATION ||
		target_relation->rd_rel->relkind == RELKIND_MATVIEW ||
		target_relation->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		/*
		 * Emit CTID so that executor can find the row to update or delete.
		 */
		var = makeVar(parsetree->resultRelation,
					  SelfItemPointerAttributeNumber,
					  TIDOID,
					  -1,
					  InvalidOid,
					  0);
		attrname = "ctid";
	}
	else if (target_relation->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
	{
		/*
		 * Let the foreign table's FDW add whatever junk TLEs it wants.
		 */
		FdwRoutine *fdwroutine;

		fdwroutine = GetFdwRoutineForRelation(target_relation, false);

		if (fdwroutine->AddForeignUpdateTargets != NULL)
			fdwroutine->AddForeignUpdateTargets(parsetree, target_rte,
												target_relation);

		/*
		 * If we have a row-level trigger corresponding to the operation, emit
		 * a whole-row Var so that executor will have the "old" row to pass to
		 * the trigger.  Alas, this misses system columns.
		 */
		if (target_relation->trigdesc &&
			((parsetree->commandType == CMD_UPDATE &&
			  (target_relation->trigdesc->trig_update_after_row ||
			   target_relation->trigdesc->trig_update_before_row)) ||
			 (parsetree->commandType == CMD_DELETE &&
			  (target_relation->trigdesc->trig_delete_after_row ||
			   target_relation->trigdesc->trig_delete_before_row))))
		{
			var = makeWholeRowVar(target_rte,
								  parsetree->resultRelation,
								  0,
								  false);

			attrname = "wholerow";
		}
	}

	if (var != NULL)
	{
		tle = makeTargetEntry((Expr *) var,
							  list_length(parsetree->targetList) + 1,
							  pstrdup(attrname),
							  true);

		parsetree->targetList = lappend(parsetree->targetList, tle);
	}
}


/*
 * matchLocks -
 *	  match the list of locks and returns the matching rules
 */
static List *
matchLocks(CmdType event,
		   RuleLock *rulelocks,
		   int varno,
		   Query *parsetree,
		   bool *hasUpdate)
{
	List	   *matching_locks = NIL;
	int			nlocks;
	int			i;

	if (rulelocks == NULL)
		return NIL;

	if (parsetree->commandType != CMD_SELECT)
	{
		if (parsetree->resultRelation != varno)
			return NIL;
	}

	nlocks = rulelocks->numLocks;

	for (i = 0; i < nlocks; i++)
	{
		RewriteRule *oneLock = rulelocks->rules[i];

		if (oneLock->event == CMD_UPDATE)
			*hasUpdate = true;

		/*
		 * Suppress ON INSERT/UPDATE/DELETE rules that are disabled or
		 * configured to not fire during the current sessions replication
		 * role. ON SELECT rules will always be applied in order to keep views
		 * working even in LOCAL or REPLICA role.
		 */
		if (oneLock->event != CMD_SELECT)
		{
			if (SessionReplicationRole == SESSION_REPLICATION_ROLE_REPLICA)
			{
				if (oneLock->enabled == RULE_FIRES_ON_ORIGIN ||
					oneLock->enabled == RULE_DISABLED)
					continue;
			}
			else				/* ORIGIN or LOCAL ROLE */
			{
				if (oneLock->enabled == RULE_FIRES_ON_REPLICA ||
					oneLock->enabled == RULE_DISABLED)
					continue;
			}
		}

		if (oneLock->event == event)
		{
			if (parsetree->commandType != CMD_SELECT ||
				rangeTableEntry_used((Node *) parsetree, varno, 0))
				matching_locks = lappend(matching_locks, oneLock);
		}
	}

	return matching_locks;
}


/*
 * ApplyRetrieveRule - expand an ON SELECT rule
 */
static Query *
ApplyRetrieveRule(Query *parsetree,
				  RewriteRule *rule,
				  int rt_index,
				  Relation relation,
				  List *activeRIRs)
{
	Query	   *rule_action;
	RangeTblEntry *rte,
			   *subrte;
	RowMarkClause *rc;

	if (list_length(rule->actions) != 1)
		elog(ERROR, "expected just one rule action");
	if (rule->qual != NULL)
		elog(ERROR, "cannot handle qualified ON SELECT rule");

	/* Check if the expansion of non-system views are restricted */
	if (unlikely((restrict_nonsystem_relation_kind & RESTRICT_RELKIND_VIEW) != 0 &&
				 RelationGetRelid(relation) >= FirstNormalObjectId))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("access to non-system view \"%s\" is restricted",
						RelationGetRelationName(relation))));

	if (rt_index == parsetree->resultRelation)
	{
		/*
		 * We have a view as the result relation of the query, and it wasn't
		 * rewritten by any rule.  This case is supported if there is an
		 * INSTEAD OF trigger that will trap attempts to insert/update/delete
		 * view rows.  The executor will check that; for the moment just plow
		 * ahead.  We have two cases:
		 *
		 * For INSERT, we needn't do anything.  The unmodified RTE will serve
		 * fine as the result relation.
		 *
		 * For UPDATE/DELETE, we need to expand the view so as to have source
		 * data for the operation.  But we also need an unmodified RTE to
		 * serve as the target.  So, copy the RTE and add the copy to the
		 * rangetable.  Note that the copy does not get added to the jointree.
		 * Also note that there's a hack in fireRIRrules to avoid calling this
		 * function again when it arrives at the copied RTE.
		 */
		if (parsetree->commandType == CMD_INSERT)
			return parsetree;
		else if (parsetree->commandType == CMD_UPDATE ||
				 parsetree->commandType == CMD_DELETE)
		{
			RangeTblEntry *newrte;
			Var		   *var;
			TargetEntry *tle;

			rte = rt_fetch(rt_index, parsetree->rtable);
			newrte = copyObject(rte);
			parsetree->rtable = lappend(parsetree->rtable, newrte);
			parsetree->resultRelation = list_length(parsetree->rtable);

			/*
			 * There's no need to do permissions checks twice, so wipe out the
			 * permissions info for the original RTE (we prefer to keep the
			 * bits set on the result RTE).
			 */
			rte->requiredPerms = 0;
			rte->checkAsUser = InvalidOid;
			rte->selectedCols = NULL;
			rte->insertedCols = NULL;
			rte->updatedCols = NULL;

			/*
			 * For the most part, Vars referencing the view should remain as
			 * they are, meaning that they implicitly represent OLD values.
			 * But in the RETURNING list if any, we want such Vars to
			 * represent NEW values, so change them to reference the new RTE.
			 *
			 * Since ChangeVarNodes scribbles on the tree in-place, copy the
			 * RETURNING list first for safety.
			 */
			parsetree->returningList = copyObject(parsetree->returningList);
			ChangeVarNodes((Node *) parsetree->returningList, rt_index,
						   parsetree->resultRelation, 0);

			/*
			 * To allow the executor to compute the original view row to pass
			 * to the INSTEAD OF trigger, we add a resjunk whole-row Var
			 * referencing the original RTE.  This will later get expanded
			 * into a RowExpr computing all the OLD values of the view row.
			 */
			var = makeWholeRowVar(rte, rt_index, 0, false);
			tle = makeTargetEntry((Expr *) var,
								  list_length(parsetree->targetList) + 1,
								  pstrdup("wholerow"),
								  true);

			parsetree->targetList = lappend(parsetree->targetList, tle);

			/* Now, continue with expanding the original view RTE */
		}
		else
			elog(ERROR, "unrecognized commandType: %d",
				 (int) parsetree->commandType);
	}

	/*
	 * Check if there's a FOR [KEY] UPDATE/SHARE clause applying to this view.
	 *
	 * Note: we needn't explicitly consider any such clauses appearing in
	 * ancestor query levels; their effects have already been pushed down to
	 * here by markQueryForLocking, and will be reflected in "rc".
	 */
	rc = get_parse_rowmark(parsetree, rt_index);

	/*
	 * Make a modifiable copy of the view query, and acquire needed locks on
	 * the relations it mentions.  Force at least RowShareLock for all such
	 * rels if there's a FOR [KEY] UPDATE/SHARE clause affecting this view.
	 */
	rule_action = copyObject(linitial(rule->actions));

	AcquireRewriteLocks(rule_action, true, (rc != NULL));

	/*
	 * If FOR [KEY] UPDATE/SHARE of view, mark all the contained tables as
	 * implicit FOR [KEY] UPDATE/SHARE, the same as the parser would have done
	 * if the view's subquery had been written out explicitly.
	 */
	if (rc != NULL)
		markQueryForLocking(rule_action, (Node *) rule_action->jointree,
							rc->strength, rc->waitPolicy, true);

	/*
	 * Recursively expand any view references inside the view.
	 *
	 * Note: this must happen after markQueryForLocking.  That way, any UPDATE
	 * permission bits needed for sub-views are initially applied to their
	 * RTE_RELATION RTEs by markQueryForLocking, and then transferred to their
	 * OLD rangetable entries by the action below (in a recursive call of this
	 * routine).
	 */
	rule_action = fireRIRrules(rule_action, activeRIRs);

	/*
	 * Make sure the query is marked as having row security if the view query
	 * does.
	 */
	parsetree->hasRowSecurity |= rule_action->hasRowSecurity;

	/*
	 * Now, plug the view query in as a subselect, replacing the relation's
	 * original RTE.
	 */
	rte = rt_fetch(rt_index, parsetree->rtable);

	rte->rtekind = RTE_SUBQUERY;
	rte->relid = InvalidOid;
	rte->security_barrier = RelationIsSecurityView(relation);
	rte->subquery = rule_action;
	rte->inh = false;			/* must not be set for a subquery */

	/*
	 * We move the view's permission check data down to its rangetable. The
	 * checks will actually be done against the OLD entry therein.
	 */
	subrte = rt_fetch(PRS2_OLD_VARNO, rule_action->rtable);
	Assert(subrte->relid == relation->rd_id);
	subrte->requiredPerms = rte->requiredPerms;
	subrte->checkAsUser = rte->checkAsUser;
	subrte->selectedCols = rte->selectedCols;
	subrte->insertedCols = rte->insertedCols;
	subrte->updatedCols = rte->updatedCols;

	rte->requiredPerms = 0;		/* no permission check on subquery itself */
	rte->checkAsUser = InvalidOid;
	rte->selectedCols = NULL;
	rte->insertedCols = NULL;
	rte->updatedCols = NULL;

	return parsetree;
}

/*
 * Recursively mark all relations used by a view as FOR [KEY] UPDATE/SHARE.
 *
 * This may generate an invalid query, eg if some sub-query uses an
 * aggregate.  We leave it to the planner to detect that.
 *
 * NB: this must agree with the parser's transformLockingClause() routine.
 * However, unlike the parser we have to be careful not to mark a view's
 * OLD and NEW rels for updating.  The best way to handle that seems to be
 * to scan the jointree to determine which rels are used.
 */
static void
markQueryForLocking(Query *qry, Node *jtnode,
					LockClauseStrength strength, LockWaitPolicy waitPolicy,
					bool pushedDown)
{
	if (jtnode == NULL)
		return;
	if (IsA(jtnode, RangeTblRef))
	{
		int			rti = ((RangeTblRef *) jtnode)->rtindex;
		RangeTblEntry *rte = rt_fetch(rti, qry->rtable);

		if (rte->rtekind == RTE_RELATION)
		{
			applyLockingClause(qry, rti, strength, waitPolicy, pushedDown);
			rte->requiredPerms |= ACL_SELECT_FOR_UPDATE;
		}
		else if (rte->rtekind == RTE_SUBQUERY)
		{
			applyLockingClause(qry, rti, strength, waitPolicy, pushedDown);
			/* FOR UPDATE/SHARE of subquery is propagated to subquery's rels */
			markQueryForLocking(rte->subquery, (Node *) rte->subquery->jointree,
								strength, waitPolicy, true);
		}
		/* other RTE types are unaffected by FOR UPDATE */
	}
	else if (IsA(jtnode, FromExpr))
	{
		FromExpr   *f = (FromExpr *) jtnode;
		ListCell   *l;

		foreach(l, f->fromlist)
			markQueryForLocking(qry, lfirst(l), strength, waitPolicy, pushedDown);
	}
	else if (IsA(jtnode, JoinExpr))
	{
		JoinExpr   *j = (JoinExpr *) jtnode;

		markQueryForLocking(qry, j->larg, strength, waitPolicy, pushedDown);
		markQueryForLocking(qry, j->rarg, strength, waitPolicy, pushedDown);
	}
	else
		elog(ERROR, "unrecognized node type: %d",
			 (int) nodeTag(jtnode));
}


/*
 * fireRIRonSubLink -
 *	Apply fireRIRrules() to each SubLink (subselect in expression) found
 *	in the given tree.
 *
 * NOTE: although this has the form of a walker, we cheat and modify the
 * SubLink nodes in-place.  It is caller's responsibility to ensure that
 * no unwanted side-effects occur!
 *
 * This is unlike most of the other routines that recurse into subselects,
 * because we must take control at the SubLink node in order to replace
 * the SubLink's subselect link with the possibly-rewritten subquery.
 */
static bool
fireRIRonSubLink(Node *node, fireRIRonSubLink_context *context)
{
	if (node == NULL)
		return false;
	if (IsA(node, SubLink))
	{
		SubLink    *sub = (SubLink *) node;

		/* Do what we came for */
		sub->subselect = (Node *) fireRIRrules((Query *) sub->subselect,
											   context->activeRIRs);

		/*
		 * Remember if any of the sublinks have row security.
		 */
		context->hasRowSecurity |= ((Query *) sub->subselect)->hasRowSecurity;

		/* Fall through to process lefthand args of SubLink */
	}

	/*
	 * Do NOT recurse into Query nodes, because fireRIRrules already processed
	 * subselects of subselects for us.
	 */
	return expression_tree_walker(node, fireRIRonSubLink,
								  (void *) context);
}


/*
 * fireRIRrules -
 *	Apply all RIR rules on each rangetable entry in the given query
 *
 * activeRIRs is a list of the OIDs of views we're already processing RIR
 * rules for, used to detect/reject recursion.
 */
static Query *
fireRIRrules(Query *parsetree, List *activeRIRs)
{
	int			origResultRelation = parsetree->resultRelation;
	int			rt_index;
	ListCell   *lc;

	/*
	 * don't try to convert this into a foreach loop, because rtable list can
	 * get changed each time through...
	 */
	rt_index = 0;
	while (rt_index < list_length(parsetree->rtable))
	{
		RangeTblEntry *rte;
		Relation	rel;
		List	   *locks;
		RuleLock   *rules;
		RewriteRule *rule;
		int			i;

		++rt_index;

		rte = rt_fetch(rt_index, parsetree->rtable);

		/*
		 * A subquery RTE can't have associated rules, so there's nothing to
		 * do to this level of the query, but we must recurse into the
		 * subquery to expand any rule references in it.
		 */
		if (rte->rtekind == RTE_SUBQUERY)
		{
			rte->subquery = fireRIRrules(rte->subquery, activeRIRs);

			/*
			 * While we are here, make sure the query is marked as having row
			 * security if any of its subqueries do.
			 */
			parsetree->hasRowSecurity |= rte->subquery->hasRowSecurity;

			continue;
		}

		/*
		 * Joins and other non-relation RTEs can be ignored completely.
		 */
		if (rte->rtekind != RTE_RELATION)
			continue;

		/*
		 * Always ignore RIR rules for materialized views referenced in
		 * queries.  (This does not prevent refreshing MVs, since they aren't
		 * referenced in their own query definitions.)
		 *
		 * Note: in the future we might want to allow MVs to be conditionally
		 * expanded as if they were regular views, if they are not scannable.
		 * In that case this test would need to be postponed till after we've
		 * opened the rel, so that we could check its state.
		 */
		if (rte->relkind == RELKIND_MATVIEW)
			continue;

		/*
		 * In INSERT ... ON CONFLICT, ignore the EXCLUDED pseudo-relation;
		 * even if it points to a view, we needn't expand it, and should not
		 * because we want the RTE to remain of RTE_RELATION type.  Otherwise,
		 * it would get changed to RTE_SUBQUERY type, which is an
		 * untested/unsupported situation.
		 */
		if (parsetree->onConflict &&
			rt_index == parsetree->onConflict->exclRelIndex)
			continue;

		/*
		 * If the table is not referenced in the query, then we ignore it.
		 * This prevents infinite expansion loop due to new rtable entries
		 * inserted by expansion of a rule. A table is referenced if it is
		 * part of the join set (a source table), or is referenced by any Var
		 * nodes, or is the result table.
		 */
		if (rt_index != parsetree->resultRelation &&
			!rangeTableEntry_used((Node *) parsetree, rt_index, 0))
			continue;

		/*
		 * Also, if this is a new result relation introduced by
		 * ApplyRetrieveRule, we don't want to do anything more with it.
		 */
		if (rt_index == parsetree->resultRelation &&
			rt_index != origResultRelation)
			continue;

		/*
		 * We can use NoLock here since either the parser or
		 * AcquireRewriteLocks should have locked the rel already.
		 */
		rel = heap_open(rte->relid, NoLock);

		/*
		 * Collect the RIR rules that we must apply
		 */
		rules = rel->rd_rules;
		if (rules != NULL)
		{
			locks = NIL;
			for (i = 0; i < rules->numLocks; i++)
			{
				rule = rules->rules[i];
				if (rule->event != CMD_SELECT)
					continue;

				locks = lappend(locks, rule);
			}

			/*
			 * If we found any, apply them --- but first check for recursion!
			 */
			if (locks != NIL)
			{
				ListCell   *l;

				if (list_member_oid(activeRIRs, RelationGetRelid(rel)))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("infinite recursion detected in rules for relation \"%s\"",
									RelationGetRelationName(rel))));
				activeRIRs = lcons_oid(RelationGetRelid(rel), activeRIRs);

				foreach(l, locks)
				{
					rule = lfirst(l);

					parsetree = ApplyRetrieveRule(parsetree,
												  rule,
												  rt_index,
												  rel,
												  activeRIRs);
				}

				activeRIRs = list_delete_first(activeRIRs);
			}
		}

		heap_close(rel, NoLock);
	}

	/* Recurse into subqueries in WITH */
	foreach(lc, parsetree->cteList)
	{
		CommonTableExpr *cte = (CommonTableExpr *) lfirst(lc);

		cte->ctequery = (Node *)
			fireRIRrules((Query *) cte->ctequery, activeRIRs);

		/*
		 * While we are here, make sure the query is marked as having row
		 * security if any of its CTEs do.
		 */
		parsetree->hasRowSecurity |= ((Query *) cte->ctequery)->hasRowSecurity;
	}

	/*
	 * Recurse into sublink subqueries, too.  But we already did the ones in
	 * the rtable and cteList.
	 */
	if (parsetree->hasSubLinks)
	{
		fireRIRonSubLink_context context;

		context.activeRIRs = activeRIRs;
		context.hasRowSecurity = false;

		query_tree_walker(parsetree, fireRIRonSubLink, (void *) &context,
						  QTW_IGNORE_RC_SUBQUERIES);

		/*
		 * Make sure the query is marked as having row security if any of its
		 * sublinks do.
		 */
		parsetree->hasRowSecurity |= context.hasRowSecurity;
	}

	/*
	 * Apply any row level security policies.  We do this last because it
	 * requires special recursion detection if the new quals have sublink
	 * subqueries, and if we did it in the loop above query_tree_walker would
	 * then recurse into those quals a second time.
	 */
	rt_index = 0;
	foreach(lc, parsetree->rtable)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);
		Relation	rel;
		List	   *securityQuals;
		List	   *withCheckOptions;
		bool		hasRowSecurity;
		bool		hasSubLinks;

		++rt_index;

		/* Only normal relations can have RLS policies */
		if (rte->rtekind != RTE_RELATION ||
			(rte->relkind != RELKIND_RELATION &&
			 rte->relkind != RELKIND_PARTITIONED_TABLE))
			continue;

		rel = heap_open(rte->relid, NoLock);

		/*
		 * Fetch any new security quals that must be applied to this RTE.
		 */
		get_row_security_policies(parsetree, rte, rt_index,
								  &securityQuals, &withCheckOptions,
								  &hasRowSecurity, &hasSubLinks);

		if (securityQuals != NIL || withCheckOptions != NIL)
		{
			if (hasSubLinks)
			{
				acquireLocksOnSubLinks_context context;
				fireRIRonSubLink_context fire_context;

				/*
				 * Recursively process the new quals, checking for infinite
				 * recursion.
				 */
				if (list_member_oid(activeRIRs, RelationGetRelid(rel)))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("infinite recursion detected in policy for relation \"%s\"",
									RelationGetRelationName(rel))));

				activeRIRs = lcons_oid(RelationGetRelid(rel), activeRIRs);

				/*
				 * get_row_security_policies just passed back securityQuals
				 * and/or withCheckOptions, and there were SubLinks, make sure
				 * we lock any relations which are referenced.
				 *
				 * These locks would normally be acquired by the parser, but
				 * securityQuals and withCheckOptions are added post-parsing.
				 */
				context.for_execute = true;
				(void) acquireLocksOnSubLinks((Node *) securityQuals, &context);
				(void) acquireLocksOnSubLinks((Node *) withCheckOptions,
											  &context);

				/*
				 * Now that we have the locks on anything added by
				 * get_row_security_policies, fire any RIR rules for them.
				 */
				fire_context.activeRIRs = activeRIRs;
				fire_context.hasRowSecurity = false;

				expression_tree_walker((Node *) securityQuals,
									   fireRIRonSubLink, (void *) &fire_context);

				expression_tree_walker((Node *) withCheckOptions,
									   fireRIRonSubLink, (void *) &fire_context);

				/*
				 * We can ignore the value of fire_context.hasRowSecurity
				 * since we only reach this code in cases where hasRowSecurity
				 * is already true.
				 */
				Assert(hasRowSecurity);

				activeRIRs = list_delete_first(activeRIRs);
			}

			/*
			 * Add the new security barrier quals to the start of the RTE's
			 * list so that they get applied before any existing barrier quals
			 * (which would have come from a security-barrier view, and should
			 * get lower priority than RLS conditions on the table itself).
			 */
			rte->securityQuals = list_concat(securityQuals,
											 rte->securityQuals);

			parsetree->withCheckOptions = list_concat(withCheckOptions,
													  parsetree->withCheckOptions);
		}

		/*
		 * Make sure the query is marked correctly if row level security
		 * applies, or if the new quals had sublinks.
		 */
		if (hasRowSecurity)
			parsetree->hasRowSecurity = true;
		if (hasSubLinks)
			parsetree->hasSubLinks = true;

		heap_close(rel, NoLock);
	}

	return parsetree;
}


/*
 * Modify the given query by adding 'AND rule_qual IS NOT TRUE' to its
 * qualification.  This is used to generate suitable "else clauses" for
 * conditional INSTEAD rules.  (Unfortunately we must use "x IS NOT TRUE",
 * not just "NOT x" which the planner is much smarter about, else we will
 * do the wrong thing when the qual evaluates to NULL.)
 *
 * The rule_qual may contain references to OLD or NEW.  OLD references are
 * replaced by references to the specified rt_index (the relation that the
 * rule applies to).  NEW references are only possible for INSERT and UPDATE
 * queries on the relation itself, and so they should be replaced by copies
 * of the related entries in the query's own targetlist.
 */
static Query *
CopyAndAddInvertedQual(Query *parsetree,
					   Node *rule_qual,
					   int rt_index,
					   CmdType event)
{
	/* Don't scribble on the passed qual (it's in the relcache!) */
	Node	   *new_qual = copyObject(rule_qual);
	acquireLocksOnSubLinks_context context;

	context.for_execute = true;

	/*
	 * In case there are subqueries in the qual, acquire necessary locks and
	 * fix any deleted JOIN RTE entries.  (This is somewhat redundant with
	 * rewriteRuleAction, but not entirely ... consider restructuring so that
	 * we only need to process the qual this way once.)
	 */
	(void) acquireLocksOnSubLinks(new_qual, &context);

	/* Fix references to OLD */
	ChangeVarNodes(new_qual, PRS2_OLD_VARNO, rt_index, 0);
	/* Fix references to NEW */
	if (event == CMD_INSERT || event == CMD_UPDATE)
		new_qual = ReplaceVarsFromTargetList(new_qual,
											 PRS2_NEW_VARNO,
											 0,
											 rt_fetch(rt_index,
													  parsetree->rtable),
											 parsetree->targetList,
											 (event == CMD_UPDATE) ?
											 REPLACEVARS_CHANGE_VARNO :
											 REPLACEVARS_SUBSTITUTE_NULL,
											 rt_index,
											 &parsetree->hasSubLinks);
	/* And attach the fixed qual */
	AddInvertedQual(parsetree, new_qual);

	return parsetree;
}


/*
 *	fireRules -
 *	   Iterate through rule locks applying rules.
 *
 * Input arguments:
 *	parsetree - original query
 *	rt_index - RT index of result relation in original query
 *	event - type of rule event
 *	locks - list of rules to fire
 * Output arguments:
 *	*instead_flag - set true if any unqualified INSTEAD rule is found
 *					(must be initialized to false)
 *	*returning_flag - set true if we rewrite RETURNING clause in any rule
 *					(must be initialized to false)
 *	*qual_product - filled with modified original query if any qualified
 *					INSTEAD rule is found (must be initialized to NULL)
 * Return value:
 *	list of rule actions adjusted for use with this query
 *
 * Qualified INSTEAD rules generate their action with the qualification
 * condition added.  They also generate a modified version of the original
 * query with the negated qualification added, so that it will run only for
 * rows that the qualified action doesn't act on.  (If there are multiple
 * qualified INSTEAD rules, we AND all the negated quals onto a single
 * modified original query.)  We won't execute the original, unmodified
 * query if we find either qualified or unqualified INSTEAD rules.  If
 * we find both, the modified original query is discarded too.
 */
static List *
fireRules(Query *parsetree,
		  int rt_index,
		  CmdType event,
		  List *locks,
		  bool *instead_flag,
		  bool *returning_flag,
		  Query **qual_product)
{
	List	   *results = NIL;
	ListCell   *l;

	foreach(l, locks)
	{
		RewriteRule *rule_lock = (RewriteRule *) lfirst(l);
		Node	   *event_qual = rule_lock->qual;
		List	   *actions = rule_lock->actions;
		QuerySource qsrc;
		ListCell   *r;

		/* Determine correct QuerySource value for actions */
		if (rule_lock->isInstead)
		{
			if (event_qual != NULL)
				qsrc = QSRC_QUAL_INSTEAD_RULE;
			else
			{
				qsrc = QSRC_INSTEAD_RULE;
				*instead_flag = true;	/* report unqualified INSTEAD */
			}
		}
		else
			qsrc = QSRC_NON_INSTEAD_RULE;

		if (qsrc == QSRC_QUAL_INSTEAD_RULE)
		{
			/*
			 * If there are INSTEAD rules with qualifications, the original
			 * query is still performed. But all the negated rule
			 * qualifications of the INSTEAD rules are added so it does its
			 * actions only in cases where the rule quals of all INSTEAD rules
			 * are false. Think of it as the default action in a case. We save
			 * this in *qual_product so RewriteQuery() can add it to the query
			 * list after we mangled it up enough.
			 *
			 * If we have already found an unqualified INSTEAD rule, then
			 * *qual_product won't be used, so don't bother building it.
			 */
			if (!*instead_flag)
			{
				if (*qual_product == NULL)
					*qual_product = copyObject(parsetree);
				*qual_product = CopyAndAddInvertedQual(*qual_product,
													   event_qual,
													   rt_index,
													   event);
			}
		}

		/* Now process the rule's actions and add them to the result list */
		foreach(r, actions)
		{
			Query	   *rule_action = lfirst(r);

			if (rule_action->commandType == CMD_NOTHING)
				continue;

			rule_action = rewriteRuleAction(parsetree, rule_action,
											event_qual, rt_index, event,
											returning_flag);

			rule_action->querySource = qsrc;
			rule_action->canSetTag = false; /* might change later */

			results = lappend(results, rule_action);
		}
	}

	return results;
}


/*
 * get_view_query - get the Query from a view's _RETURN rule.
 *
 * Caller should have verified that the relation is a view, and therefore
 * we should find an ON SELECT action.
 *
 * Note that the pointer returned is into the relcache and therefore must
 * be treated as read-only to the caller and not modified or scribbled on.
 */
Query *
get_view_query(Relation view)
{
	int			i;

	Assert(view->rd_rel->relkind == RELKIND_VIEW);

	for (i = 0; i < view->rd_rules->numLocks; i++)
	{
		RewriteRule *rule = view->rd_rules->rules[i];

		if (rule->event == CMD_SELECT)
		{
			/* A _RETURN rule should have only one action */
			if (list_length(rule->actions) != 1)
				elog(ERROR, "invalid _RETURN rule action specification");

			return (Query *) linitial(rule->actions);
		}
	}

	elog(ERROR, "failed to find _RETURN rule for view");
	return NULL;				/* keep compiler quiet */
}


/*
 * view_has_instead_trigger - does view have an INSTEAD OF trigger for event?
 *
 * If it does, we don't want to treat it as auto-updatable.  This test can't
 * be folded into view_query_is_auto_updatable because it's not an error
 * condition.
 */
static bool
view_has_instead_trigger(Relation view, CmdType event)
{
	TriggerDesc *trigDesc = view->trigdesc;

	switch (event)
	{
		case CMD_INSERT:
			if (trigDesc && trigDesc->trig_insert_instead_row)
				return true;
			break;
		case CMD_UPDATE:
			if (trigDesc && trigDesc->trig_update_instead_row)
				return true;
			break;
		case CMD_DELETE:
			if (trigDesc && trigDesc->trig_delete_instead_row)
				return true;
			break;
		default:
			elog(ERROR, "unrecognized CmdType: %d", (int) event);
			break;
	}
	return false;
}


/*
 * view_col_is_auto_updatable - test whether the specified column of a view
 * is auto-updatable. Returns NULL (if the column can be updated) or a message
 * string giving the reason that it cannot be.
 *
 * The returned string has not been translated; if it is shown as an error
 * message, the caller should apply _() to translate it.
 *
 * Note that the checks performed here are local to this view. We do not check
 * whether the referenced column of the underlying base relation is updatable.
 */
static const char *
view_col_is_auto_updatable(RangeTblRef *rtr, TargetEntry *tle)
{
	Var		   *var = (Var *) tle->expr;

	/*
	 * For now, the only updatable columns we support are those that are Vars
	 * referring to user columns of the underlying base relation.
	 *
	 * The view targetlist may contain resjunk columns (e.g., a view defined
	 * like "SELECT * FROM t ORDER BY a+b" is auto-updatable) but such columns
	 * are not auto-updatable, and in fact should never appear in the outer
	 * query's targetlist.
	 */
	if (tle->resjunk)
		return gettext_noop("Junk view columns are not updatable.");

	if (!IsA(var, Var) ||
		var->varno != rtr->rtindex ||
		var->varlevelsup != 0)
		return gettext_noop("View columns that are not columns of their base relation are not updatable.");

	if (var->varattno < 0)
		return gettext_noop("View columns that refer to system columns are not updatable.");

	if (var->varattno == 0)
		return gettext_noop("View columns that return whole-row references are not updatable.");

	return NULL;				/* the view column is updatable */
}


/*
 * view_query_is_auto_updatable - test whether the specified view definition
 * represents an auto-updatable view. Returns NULL (if the view can be updated)
 * or a message string giving the reason that it cannot be.

 * The returned string has not been translated; if it is shown as an error
 * message, the caller should apply _() to translate it.
 *
 * If check_cols is true, the view is required to have at least one updatable
 * column (necessary for INSERT/UPDATE). Otherwise the view's columns are not
 * checked for updatability. See also view_cols_are_auto_updatable.
 *
 * Note that the checks performed here are only based on the view definition.
 * We do not check whether any base relations referred to by the view are
 * updatable.
 */
const char *
view_query_is_auto_updatable(Query *viewquery, bool check_cols)
{
	RangeTblRef *rtr;
	RangeTblEntry *base_rte;

	/*----------
	 * Check if the view is simply updatable.  According to SQL-92 this means:
	 *	- No DISTINCT clause.
	 *	- Each TLE is a column reference, and each column appears at most once.
	 *	- FROM contains exactly one base relation.
	 *	- No GROUP BY or HAVING clauses.
	 *	- No set operations (UNION, INTERSECT or EXCEPT).
	 *	- No sub-queries in the WHERE clause that reference the target table.
	 *
	 * We ignore that last restriction since it would be complex to enforce
	 * and there isn't any actual benefit to disallowing sub-queries.  (The
	 * semantic issues that the standard is presumably concerned about don't
	 * arise in Postgres, since any such sub-query will not see any updates
	 * executed by the outer query anyway, thanks to MVCC snapshotting.)
	 *
	 * We also relax the second restriction by supporting part of SQL:1999
	 * feature T111, which allows for a mix of updatable and non-updatable
	 * columns, provided that an INSERT or UPDATE doesn't attempt to assign to
	 * a non-updatable column.
	 *
	 * In addition we impose these constraints, involving features that are
	 * not part of SQL-92:
	 *	- No CTEs (WITH clauses).
	 *	- No OFFSET or LIMIT clauses (this matches a SQL:2008 restriction).
	 *	- No system columns (including whole-row references) in the tlist.
	 *	- No window functions in the tlist.
	 *	- No set-returning functions in the tlist.
	 *
	 * Note that we do these checks without recursively expanding the view.
	 * If the base relation is a view, we'll recursively deal with it later.
	 *----------
	 */
	if (viewquery->distinctClause != NIL)
		return gettext_noop("Views containing DISTINCT are not automatically updatable.");

	if (viewquery->groupClause != NIL || viewquery->groupingSets)
		return gettext_noop("Views containing GROUP BY are not automatically updatable.");

	if (viewquery->havingQual != NULL)
		return gettext_noop("Views containing HAVING are not automatically updatable.");

	if (viewquery->setOperations != NULL)
		return gettext_noop("Views containing UNION, INTERSECT, or EXCEPT are not automatically updatable.");

	if (viewquery->cteList != NIL)
		return gettext_noop("Views containing WITH are not automatically updatable.");

	if (viewquery->limitOffset != NULL || viewquery->limitCount != NULL)
		return gettext_noop("Views containing LIMIT or OFFSET are not automatically updatable.");

	/*
	 * We must not allow window functions or set returning functions in the
	 * targetlist. Otherwise we might end up inserting them into the quals of
	 * the main query. We must also check for aggregates in the targetlist in
	 * case they appear without a GROUP BY.
	 *
	 * These restrictions ensure that each row of the view corresponds to a
	 * unique row in the underlying base relation.
	 */
	if (viewquery->hasAggs)
		return gettext_noop("Views that return aggregate functions are not automatically updatable.");

	if (viewquery->hasWindowFuncs)
		return gettext_noop("Views that return window functions are not automatically updatable.");

	if (viewquery->hasTargetSRFs)
		return gettext_noop("Views that return set-returning functions are not automatically updatable.");

	/*
	 * The view query should select from a single base relation, which must be
	 * a table or another view.
	 */
	if (list_length(viewquery->jointree->fromlist) != 1)
		return gettext_noop("Views that do not select from a single table or view are not automatically updatable.");

	rtr = (RangeTblRef *) linitial(viewquery->jointree->fromlist);
	if (!IsA(rtr, RangeTblRef))
		return gettext_noop("Views that do not select from a single table or view are not automatically updatable.");

	base_rte = rt_fetch(rtr->rtindex, viewquery->rtable);
	if (base_rte->rtekind != RTE_RELATION ||
		(base_rte->relkind != RELKIND_RELATION &&
		 base_rte->relkind != RELKIND_FOREIGN_TABLE &&
		 base_rte->relkind != RELKIND_VIEW &&
		 base_rte->relkind != RELKIND_PARTITIONED_TABLE))
		return gettext_noop("Views that do not select from a single table or view are not automatically updatable.");

	if (base_rte->tablesample)
		return gettext_noop("Views containing TABLESAMPLE are not automatically updatable.");

	/*
	 * Check that the view has at least one updatable column. This is required
	 * for INSERT/UPDATE but not for DELETE.
	 */
	if (check_cols)
	{
		ListCell   *cell;
		bool		found;

		found = false;
		foreach(cell, viewquery->targetList)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(cell);

			if (view_col_is_auto_updatable(rtr, tle) == NULL)
			{
				found = true;
				break;
			}
		}

		if (!found)
			return gettext_noop("Views that have no updatable columns are not automatically updatable.");
	}

	return NULL;				/* the view is updatable */
}


/*
 * view_cols_are_auto_updatable - test whether all of the required columns of
 * an auto-updatable view are actually updatable. Returns NULL (if all the
 * required columns can be updated) or a message string giving the reason that
 * they cannot be.
 *
 * The returned string has not been translated; if it is shown as an error
 * message, the caller should apply _() to translate it.
 *
 * This should be used for INSERT/UPDATE to ensure that we don't attempt to
 * assign to any non-updatable columns.
 *
 * Additionally it may be used to retrieve the set of updatable columns in the
 * view, or if one or more of the required columns is not updatable, the name
 * of the first offending non-updatable column.
 *
 * The caller must have already verified that this is an auto-updatable view
 * using view_query_is_auto_updatable.
 *
 * Note that the checks performed here are only based on the view definition.
 * We do not check whether the referenced columns of the base relation are
 * updatable.
 */
static const char *
view_cols_are_auto_updatable(Query *viewquery,
							 Bitmapset *required_cols,
							 Bitmapset **updatable_cols,
							 char **non_updatable_col)
{
	RangeTblRef *rtr;
	AttrNumber	col;
	ListCell   *cell;

	/*
	 * The caller should have verified that this view is auto-updatable and so
	 * there should be a single base relation.
	 */
	Assert(list_length(viewquery->jointree->fromlist) == 1);
	rtr = linitial_node(RangeTblRef, viewquery->jointree->fromlist);

	/* Initialize the optional return values */
	if (updatable_cols != NULL)
		*updatable_cols = NULL;
	if (non_updatable_col != NULL)
		*non_updatable_col = NULL;

	/* Test each view column for updatability */
	col = -FirstLowInvalidHeapAttributeNumber;
	foreach(cell, viewquery->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(cell);
		const char *col_update_detail;

		col++;
		col_update_detail = view_col_is_auto_updatable(rtr, tle);

		if (col_update_detail == NULL)
		{
			/* The column is updatable */
			if (updatable_cols != NULL)
				*updatable_cols = bms_add_member(*updatable_cols, col);
		}
		else if (bms_is_member(col, required_cols))
		{
			/* The required column is not updatable */
			if (non_updatable_col != NULL)
				*non_updatable_col = tle->resname;
			return col_update_detail;
		}
	}

	return NULL;				/* all the required view columns are updatable */
}


/*
 * relation_is_updatable - determine which update events the specified
 * relation supports.
 *
 * Note that views may contain a mix of updatable and non-updatable columns.
 * For a view to support INSERT/UPDATE it must have at least one updatable
 * column, but there is no such restriction for DELETE. If include_cols is
 * non-NULL, then only the specified columns are considered when testing for
 * updatability.
 *
 * This is used for the information_schema views, which have separate concepts
 * of "updatable" and "trigger updatable".  A relation is "updatable" if it
 * can be updated without the need for triggers (either because it has a
 * suitable RULE, or because it is simple enough to be automatically updated).
 * A relation is "trigger updatable" if it has a suitable INSTEAD OF trigger.
 * The SQL standard regards this as not necessarily updatable, presumably
 * because there is no way of knowing what the trigger will actually do.
 * The information_schema views therefore call this function with
 * include_triggers = false.  However, other callers might only care whether
 * data-modifying SQL will work, so they can pass include_triggers = true
 * to have trigger updatability included in the result.
 *
 * The return value is a bitmask of rule event numbers indicating which of
 * the INSERT, UPDATE and DELETE operations are supported.  (We do it this way
 * so that we can test for UPDATE plus DELETE support in a single call.)
 */
int
relation_is_updatable(Oid reloid,
					  bool include_triggers,
					  Bitmapset *include_cols)
{
	int			events = 0;
	Relation	rel;
	RuleLock   *rulelocks;

#define ALL_EVENTS ((1 << CMD_INSERT) | (1 << CMD_UPDATE) | (1 << CMD_DELETE))

	rel = try_relation_open(reloid, AccessShareLock);

	/*
	 * If the relation doesn't exist, return zero rather than throwing an
	 * error.  This is helpful since scanning an information_schema view under
	 * MVCC rules can result in referencing rels that have actually been
	 * deleted already.
	 */
	if (rel == NULL)
		return 0;

	/* If the relation is a table, it is always updatable */
	if (rel->rd_rel->relkind == RELKIND_RELATION ||
		rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		relation_close(rel, AccessShareLock);
		return ALL_EVENTS;
	}

	/* Look for unconditional DO INSTEAD rules, and note supported events */
	rulelocks = rel->rd_rules;
	if (rulelocks != NULL)
	{
		int			i;

		for (i = 0; i < rulelocks->numLocks; i++)
		{
			if (rulelocks->rules[i]->isInstead &&
				rulelocks->rules[i]->qual == NULL)
			{
				events |= ((1 << rulelocks->rules[i]->event) & ALL_EVENTS);
			}
		}

		/* If we have rules for all events, we're done */
		if (events == ALL_EVENTS)
		{
			relation_close(rel, AccessShareLock);
			return events;
		}
	}

	/* Similarly look for INSTEAD OF triggers, if they are to be included */
	if (include_triggers)
	{
		TriggerDesc *trigDesc = rel->trigdesc;

		if (trigDesc)
		{
			if (trigDesc->trig_insert_instead_row)
				events |= (1 << CMD_INSERT);
			if (trigDesc->trig_update_instead_row)
				events |= (1 << CMD_UPDATE);
			if (trigDesc->trig_delete_instead_row)
				events |= (1 << CMD_DELETE);

			/* If we have triggers for all events, we're done */
			if (events == ALL_EVENTS)
			{
				relation_close(rel, AccessShareLock);
				return events;
			}
		}
	}

	/* If this is a foreign table, check which update events it supports */
	if (rel->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
	{
		FdwRoutine *fdwroutine = GetFdwRoutineForRelation(rel, false);

		if (fdwroutine->IsForeignRelUpdatable != NULL)
			events |= fdwroutine->IsForeignRelUpdatable(rel);
		else
		{
			/* Assume presence of executor functions is sufficient */
			if (fdwroutine->ExecForeignInsert != NULL)
				events |= (1 << CMD_INSERT);
			if (fdwroutine->ExecForeignUpdate != NULL)
				events |= (1 << CMD_UPDATE);
			if (fdwroutine->ExecForeignDelete != NULL)
				events |= (1 << CMD_DELETE);
		}

		relation_close(rel, AccessShareLock);
		return events;
	}

	/* Check if this is an automatically updatable view */
	if (rel->rd_rel->relkind == RELKIND_VIEW)
	{
		Query	   *viewquery = get_view_query(rel);

		if (view_query_is_auto_updatable(viewquery, false) == NULL)
		{
			Bitmapset  *updatable_cols;
			int			auto_events;
			RangeTblRef *rtr;
			RangeTblEntry *base_rte;
			Oid			baseoid;

			/*
			 * Determine which of the view's columns are updatable. If there
			 * are none within the set of columns we are looking at, then the
			 * view doesn't support INSERT/UPDATE, but it may still support
			 * DELETE.
			 */
			view_cols_are_auto_updatable(viewquery, NULL,
										 &updatable_cols, NULL);

			if (include_cols != NULL)
				updatable_cols = bms_int_members(updatable_cols, include_cols);

			if (bms_is_empty(updatable_cols))
				auto_events = (1 << CMD_DELETE);	/* May support DELETE */
			else
				auto_events = ALL_EVENTS;	/* May support all events */

			/*
			 * The base relation must also support these update commands.
			 * Tables are always updatable, but for any other kind of base
			 * relation we must do a recursive check limited to the columns
			 * referenced by the locally updatable columns in this view.
			 */
			rtr = (RangeTblRef *) linitial(viewquery->jointree->fromlist);
			base_rte = rt_fetch(rtr->rtindex, viewquery->rtable);
			Assert(base_rte->rtekind == RTE_RELATION);
			if (base_rte->relkind != RELKIND_RELATION &&
				base_rte->relkind != RELKIND_PARTITIONED_TABLE)
			{
				baseoid = base_rte->relid;
				include_cols = adjust_view_column_set(updatable_cols,
				                                      viewquery->targetList,
				                                      rel,
				                                      base_rte->relid);
				auto_events &= relation_is_updatable(baseoid,
													 include_triggers,
													 include_cols);
			}
			events |= auto_events;
		}
	}

	/* If we reach here, the relation may support some update commands */
	relation_close(rel, AccessShareLock);
	return events;
}


/*
 * adjust_view_column_set - map a set of column numbers according to targetlist
 *
 * This is used with simply-updatable views to map column-permissions sets for
 * the view columns onto the matching columns in the underlying base relation.
 * The targetlist is expected to be a list of plain Vars of the underlying
 * relation (as per the checks above in view_query_is_auto_updatable).
 */
static Bitmapset *
adjust_view_column_set(Bitmapset *cols,
                       List *targetlist,
                       Relation view_rel,
                       Oid base_relid)
{
	Bitmapset  *result = NULL;
	int			col;
	AttrNumber view_lowattrno = YBGetFirstLowInvalidAttributeNumber(view_rel);
	AttrNumber base_lowattrno = YBGetFirstLowInvalidAttributeNumberFromOid(base_relid);

	col = -1;
	while ((col = bms_next_member(cols, col)) >= 0)
	{
		/* bit numbers are offset by FirstLowInvalidHeapAttributeNumber */
		AttrNumber	attno = col + view_lowattrno;

		if (attno == InvalidAttrNumber)
		{
			/*
			 * There's a whole-row reference to the view.  For permissions
			 * purposes, treat it as a reference to each column available from
			 * the view.  (We should *not* convert this to a whole-row
			 * reference to the base relation, since the view may not touch
			 * all columns of the base relation.)
			 */
			ListCell   *lc;

			foreach(lc, targetlist)
			{
				TargetEntry *tle = lfirst_node(TargetEntry, lc);
				Var		   *var;

				if (tle->resjunk)
					continue;
				var = castNode(Var, tle->expr);
				result = bms_add_member(result,
				                        var->varattno - base_lowattrno);
			}
		}
		else
		{
			/*
			 * Views do not have system columns, so we do not expect to see
			 * any other system attnos here.  If we do find one, the error
			 * case will apply.
			 */
			TargetEntry *tle = get_tle_by_resno(targetlist, attno);

			if (tle != NULL && !tle->resjunk && IsA(tle->expr, Var))
			{
				Var		   *var = (Var *) tle->expr;

				result = bms_add_member(result,
				                        var->varattno - base_lowattrno);
			}
			else
				elog(ERROR, "attribute number %d not found in view targetlist",
					 attno);
		}
	}

	return result;
}


/*
 * rewriteTargetView -
 *	  Attempt to rewrite a query where the target relation is a view, so that
 *	  the view's base relation becomes the target relation.
 *
 * Note that the base relation here may itself be a view, which may or may not
 * have INSTEAD OF triggers or rules to handle the update.  That is handled by
 * the recursion in RewriteQuery.
 */
static Query *
rewriteTargetView(Query *parsetree, Relation view)
{
	Query	   *viewquery;
	const char *auto_update_detail;
	RangeTblRef *rtr;
	int			base_rt_index;
	int			new_rt_index;
	RangeTblEntry *base_rte;
	RangeTblEntry *view_rte;
	RangeTblEntry *new_rte;
	Relation	base_rel;
	List	   *view_targetlist;
	ListCell   *lc;

	/*
	 * Get the Query from the view's ON SELECT rule.  We're going to munge the
	 * Query to change the view's base relation into the target relation,
	 * along with various other changes along the way, so we need to make a
	 * copy of it (get_view_query() returns a pointer into the relcache, so we
	 * have to treat it as read-only).
	 */
	viewquery = copyObject(get_view_query(view));

	/* The view must be updatable, else fail */
	auto_update_detail =
		view_query_is_auto_updatable(viewquery,
									 parsetree->commandType != CMD_DELETE);

	if (auto_update_detail)
	{
		/* messages here should match execMain.c's CheckValidResultRel */
		switch (parsetree->commandType)
		{
			case CMD_INSERT:
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("cannot insert into view \"%s\"",
								RelationGetRelationName(view)),
						 errdetail_internal("%s", _(auto_update_detail)),
						 errhint("To enable inserting into the view, provide an INSTEAD OF INSERT trigger or an unconditional ON INSERT DO INSTEAD rule.")));
				break;
			case CMD_UPDATE:
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("cannot update view \"%s\"",
								RelationGetRelationName(view)),
						 errdetail_internal("%s", _(auto_update_detail)),
						 errhint("To enable updating the view, provide an INSTEAD OF UPDATE trigger or an unconditional ON UPDATE DO INSTEAD rule.")));
				break;
			case CMD_DELETE:
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("cannot delete from view \"%s\"",
								RelationGetRelationName(view)),
						 errdetail_internal("%s", _(auto_update_detail)),
						 errhint("To enable deleting from the view, provide an INSTEAD OF DELETE trigger or an unconditional ON DELETE DO INSTEAD rule.")));
				break;
			default:
				elog(ERROR, "unrecognized CmdType: %d",
					 (int) parsetree->commandType);
				break;
		}
	}

	/* Check if the expansion of non-system views are restricted */
	if (unlikely((restrict_nonsystem_relation_kind & RESTRICT_RELKIND_VIEW) != 0 &&
				 RelationGetRelid(view) >= FirstNormalObjectId))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("access to non-system view \"%s\" is restricted",
						RelationGetRelationName(view))));

	/*
	 * For INSERT/UPDATE the modified columns must all be updatable. Note that
	 * we get the modified columns from the query's targetlist, not from the
	 * result RTE's insertedCols and/or updatedCols set, since
	 * rewriteTargetListIU may have added additional targetlist entries for
	 * view defaults, and these must also be updatable.
	 */
	if (parsetree->commandType != CMD_DELETE)
	{
		Bitmapset  *modified_cols = NULL;
		char	   *non_updatable_col;

		foreach(lc, parsetree->targetList)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(lc);

			if (!tle->resjunk)
				modified_cols = bms_add_member(modified_cols,
											   tle->resno - FirstLowInvalidHeapAttributeNumber);
		}

		if (parsetree->onConflict)
		{
			foreach(lc, parsetree->onConflict->onConflictSet)
			{
				TargetEntry *tle = (TargetEntry *) lfirst(lc);

				if (!tle->resjunk)
					modified_cols = bms_add_member(modified_cols,
												   tle->resno - FirstLowInvalidHeapAttributeNumber);
			}
		}

		auto_update_detail = view_cols_are_auto_updatable(viewquery,
														  modified_cols,
														  NULL,
														  &non_updatable_col);
		if (auto_update_detail)
		{
			/*
			 * This is a different error, caused by an attempt to update a
			 * non-updatable column in an otherwise updatable view.
			 */
			switch (parsetree->commandType)
			{
				case CMD_INSERT:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot insert into column \"%s\" of view \"%s\"",
									non_updatable_col,
									RelationGetRelationName(view)),
							 errdetail_internal("%s", _(auto_update_detail))));
					break;
				case CMD_UPDATE:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot update column \"%s\" of view \"%s\"",
									non_updatable_col,
									RelationGetRelationName(view)),
							 errdetail_internal("%s", _(auto_update_detail))));
					break;
				default:
					elog(ERROR, "unrecognized CmdType: %d",
						 (int) parsetree->commandType);
					break;
			}
		}
	}

	/* Locate RTE describing the view in the outer query */
	view_rte = rt_fetch(parsetree->resultRelation, parsetree->rtable);

	/*
	 * If we get here, view_query_is_auto_updatable() has verified that the
	 * view contains a single base relation.
	 */
	Assert(list_length(viewquery->jointree->fromlist) == 1);
	rtr = linitial_node(RangeTblRef, viewquery->jointree->fromlist);

	base_rt_index = rtr->rtindex;
	base_rte = rt_fetch(base_rt_index, viewquery->rtable);
	Assert(base_rte->rtekind == RTE_RELATION);

	/*
	 * Up to now, the base relation hasn't been touched at all in our query.
	 * We need to acquire lock on it before we try to do anything with it.
	 * (The subsequent recursive call of RewriteQuery will suppose that we
	 * already have the right lock!)  Since it will become the query target
	 * relation, RowExclusiveLock is always the right thing.
	 */
	base_rel = heap_open(base_rte->relid, RowExclusiveLock);

	/*
	 * While we have the relation open, update the RTE's relkind, just in case
	 * it changed since this view was made (cf. AcquireRewriteLocks).
	 */
	base_rte->relkind = base_rel->rd_rel->relkind;

	/*
	 * If the view query contains any sublink subqueries then we need to also
	 * acquire locks on any relations they refer to.  We know that there won't
	 * be any subqueries in the range table or CTEs, so we can skip those, as
	 * in AcquireRewriteLocks.
	 */
	if (viewquery->hasSubLinks)
	{
		acquireLocksOnSubLinks_context context;

		context.for_execute = true;
		query_tree_walker(viewquery, acquireLocksOnSubLinks, &context,
						  QTW_IGNORE_RC_SUBQUERIES);
	}

	/*
	 * Create a new target RTE describing the base relation, and add it to the
	 * outer query's rangetable.  (What's happening in the next few steps is
	 * very much like what the planner would do to "pull up" the view into the
	 * outer query.  Perhaps someday we should refactor things enough so that
	 * we can share code with the planner.)
	 */
	new_rte = (RangeTblEntry *) base_rte;
	parsetree->rtable = lappend(parsetree->rtable, new_rte);
	new_rt_index = list_length(parsetree->rtable);

	/*
	 * INSERTs never inherit.  For UPDATE/DELETE, we use the view query's
	 * inheritance flag for the base relation.
	 */
	if (parsetree->commandType == CMD_INSERT)
		new_rte->inh = false;

	/*
	 * Adjust the view's targetlist Vars to reference the new target RTE, ie
	 * make their varnos be new_rt_index instead of base_rt_index.  There can
	 * be no Vars for other rels in the tlist, so this is sufficient to pull
	 * up the tlist expressions for use in the outer query.  The tlist will
	 * provide the replacement expressions used by ReplaceVarsFromTargetList
	 * below.
	 */
	view_targetlist = viewquery->targetList;

	ChangeVarNodes((Node *) view_targetlist,
				   base_rt_index,
				   new_rt_index,
				   0);

	/*
	 * Mark the new target RTE for the permissions checks that we want to
	 * enforce against the view owner, as distinct from the query caller.  At
	 * the relation level, require the same INSERT/UPDATE/DELETE permissions
	 * that the query caller needs against the view.  We drop the ACL_SELECT
	 * bit that is presumably in new_rte->requiredPerms initially.
	 *
	 * Note: the original view RTE remains in the query's rangetable list.
	 * Although it will be unused in the query plan, we need it there so that
	 * the executor still performs appropriate permissions checks for the
	 * query caller's use of the view.
	 */
	new_rte->checkAsUser = view->rd_rel->relowner;
	new_rte->requiredPerms = view_rte->requiredPerms;

	/*
	 * Now for the per-column permissions bits.
	 *
	 * Initially, new_rte contains selectedCols permission check bits for all
	 * base-rel columns referenced by the view, but since the view is a SELECT
	 * query its insertedCols/updatedCols is empty.  We set insertedCols and
	 * updatedCols to include all the columns the outer query is trying to
	 * modify, adjusting the column numbers as needed.  But we leave
	 * selectedCols as-is, so the view owner must have read permission for all
	 * columns used in the view definition, even if some of them are not read
	 * by the outer query.  We could try to limit selectedCols to only columns
	 * used in the transformed query, but that does not correspond to what
	 * happens in ordinary SELECT usage of a view: all referenced columns must
	 * have read permission, even if optimization finds that some of them can
	 * be discarded during query transformation.  The flattening we're doing
	 * here is an optional optimization, too.  (If you are unpersuaded and
	 * want to change this, note that applying adjust_view_column_set to
	 * view_rte->selectedCols is clearly *not* the right answer, since that
	 * neglects base-rel columns used in the view's WHERE quals.)
	 *
	 * This step needs the modified view targetlist, so we have to do things
	 * in this order.
	 */
	Assert(bms_is_empty(new_rte->insertedCols) &&
		   bms_is_empty(new_rte->updatedCols));

	new_rte->insertedCols = adjust_view_column_set(view_rte->insertedCols,
	                                               view_targetlist,
	                                               view,
	                                               new_rte->relid);

	new_rte->updatedCols = adjust_view_column_set(view_rte->updatedCols,
	                                              view_targetlist,
	                                              view,
	                                              new_rte->relid);

	/*
	 * Move any security barrier quals from the view RTE onto the new target
	 * RTE.  Any such quals should now apply to the new target RTE and will
	 * not reference the original view RTE in the rewritten query.
	 */
	new_rte->securityQuals = view_rte->securityQuals;
	view_rte->securityQuals = NIL;

	/*
	 * Now update all Vars in the outer query that reference the view to
	 * reference the appropriate column of the base relation instead.
	 */
	parsetree = (Query *)
		ReplaceVarsFromTargetList((Node *) parsetree,
								  parsetree->resultRelation,
								  0,
								  view_rte,
								  view_targetlist,
								  REPLACEVARS_REPORT_ERROR,
								  0,
								  &parsetree->hasSubLinks);

	/*
	 * Update all other RTI references in the query that point to the view
	 * (for example, parsetree->resultRelation itself) to point to the new
	 * base relation instead.  Vars will not be affected since none of them
	 * reference parsetree->resultRelation any longer.
	 */
	ChangeVarNodes((Node *) parsetree,
				   parsetree->resultRelation,
				   new_rt_index,
				   0);
	Assert(parsetree->resultRelation == new_rt_index);

	/*
	 * For INSERT/UPDATE we must also update resnos in the targetlist to refer
	 * to columns of the base relation, since those indicate the target
	 * columns to be affected.
	 *
	 * Note that this destroys the resno ordering of the targetlist, but that
	 * will be fixed when we recurse through rewriteQuery, which will invoke
	 * rewriteTargetListIU again on the updated targetlist.
	 */
	if (parsetree->commandType != CMD_DELETE)
	{
		foreach(lc, parsetree->targetList)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(lc);
			TargetEntry *view_tle;

			if (tle->resjunk)
				continue;

			view_tle = get_tle_by_resno(view_targetlist, tle->resno);
			if (view_tle != NULL && !view_tle->resjunk && IsA(view_tle->expr, Var))
				tle->resno = ((Var *) view_tle->expr)->varattno;
			else
				elog(ERROR, "attribute number %d not found in view targetlist",
					 tle->resno);
		}
	}

	/*
	 * For INSERT .. ON CONFLICT .. DO UPDATE, we must also update assorted
	 * stuff in the onConflict data structure.
	 */
	if (parsetree->onConflict &&
		parsetree->onConflict->action == ONCONFLICT_UPDATE)
	{
		Index		old_exclRelIndex,
					new_exclRelIndex;
		RangeTblEntry *new_exclRte;
		List	   *tmp_tlist;

		/*
		 * Like the INSERT/UPDATE code above, update the resnos in the
		 * auxiliary UPDATE targetlist to refer to columns of the base
		 * relation.
		 */
		foreach(lc, parsetree->onConflict->onConflictSet)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(lc);
			TargetEntry *view_tle;

			if (tle->resjunk)
				continue;

			view_tle = get_tle_by_resno(view_targetlist, tle->resno);
			if (view_tle != NULL && !view_tle->resjunk && IsA(view_tle->expr, Var))
				tle->resno = ((Var *) view_tle->expr)->varattno;
			else
				elog(ERROR, "attribute number %d not found in view targetlist",
					 tle->resno);
		}

		/*
		 * Also, create a new RTE for the EXCLUDED pseudo-relation, using the
		 * query's new base rel (which may well have a different column list
		 * from the view, hence we need a new column alias list).  This should
		 * match transformOnConflictClause.  In particular, note that the
		 * relkind is set to composite to signal that we're not dealing with
		 * an actual relation, and no permissions checks are wanted.
		 */
		old_exclRelIndex = parsetree->onConflict->exclRelIndex;

		new_exclRte = addRangeTableEntryForRelation(make_parsestate(NULL),
													base_rel,
													makeAlias("excluded",
															  NIL),
													false, false);
		new_exclRte->relkind = RELKIND_COMPOSITE_TYPE;
		new_exclRte->requiredPerms = 0;
		/* other permissions fields in new_exclRte are already empty */

		parsetree->rtable = lappend(parsetree->rtable, new_exclRte);
		new_exclRelIndex = parsetree->onConflict->exclRelIndex =
			list_length(parsetree->rtable);

		/*
		 * Replace the targetlist for the EXCLUDED pseudo-relation with a new
		 * one, representing the columns from the new base relation.
		 */
		parsetree->onConflict->exclRelTlist =
			BuildOnConflictExcludedTargetlist(base_rel, new_exclRelIndex);

		/*
		 * Update all Vars in the ON CONFLICT clause that refer to the old
		 * EXCLUDED pseudo-relation.  We want to use the column mappings
		 * defined in the view targetlist, but we need the outputs to refer to
		 * the new EXCLUDED pseudo-relation rather than the new target RTE.
		 * Also notice that "EXCLUDED.*" will be expanded using the view's
		 * rowtype, which seems correct.
		 */
		tmp_tlist = copyObject(view_targetlist);

		ChangeVarNodes((Node *) tmp_tlist, new_rt_index,
					   new_exclRelIndex, 0);

		parsetree->onConflict = (OnConflictExpr *)
			ReplaceVarsFromTargetList((Node *) parsetree->onConflict,
									  old_exclRelIndex,
									  0,
									  view_rte,
									  tmp_tlist,
									  REPLACEVARS_REPORT_ERROR,
									  0,
									  &parsetree->hasSubLinks);
	}

	/*
	 * For UPDATE/DELETE, pull up any WHERE quals from the view.  We know that
	 * any Vars in the quals must reference the one base relation, so we need
	 * only adjust their varnos to reference the new target (just the same as
	 * we did with the view targetlist).
	 *
	 * If it's a security-barrier view, its WHERE quals must be applied before
	 * quals from the outer query, so we attach them to the RTE as security
	 * barrier quals rather than adding them to the main WHERE clause.
	 *
	 * For INSERT, the view's quals can be ignored in the main query.
	 */
	if (parsetree->commandType != CMD_INSERT &&
		viewquery->jointree->quals != NULL)
	{
		Node	   *viewqual = (Node *) viewquery->jointree->quals;

		/*
		 * Even though we copied viewquery already at the top of this
		 * function, we must duplicate the viewqual again here, because we may
		 * need to use the quals again below for a WithCheckOption clause.
		 */
		viewqual = copyObject(viewqual);

		ChangeVarNodes(viewqual, base_rt_index, new_rt_index, 0);

		if (RelationIsSecurityView(view))
		{
			/*
			 * The view's quals go in front of existing barrier quals: those
			 * would have come from an outer level of security-barrier view,
			 * and so must get evaluated later.
			 *
			 * Note: the parsetree has been mutated, so the new_rte pointer is
			 * stale and needs to be re-computed.
			 */
			new_rte = rt_fetch(new_rt_index, parsetree->rtable);
			new_rte->securityQuals = lcons(viewqual, new_rte->securityQuals);

			/*
			 * Do not set parsetree->hasRowSecurity, because these aren't RLS
			 * conditions (they aren't affected by enabling/disabling RLS).
			 */

			/*
			 * Make sure that the query is marked correctly if the added qual
			 * has sublinks.
			 */
			if (!parsetree->hasSubLinks)
				parsetree->hasSubLinks = checkExprHasSubLink(viewqual);
		}
		else
			AddQual(parsetree, (Node *) viewqual);
	}

	/*
	 * For INSERT/UPDATE, if the view has the WITH CHECK OPTION, or any parent
	 * view specified WITH CASCADED CHECK OPTION, add the quals from the view
	 * to the query's withCheckOptions list.
	 */
	if (parsetree->commandType != CMD_DELETE)
	{
		bool		has_wco = RelationHasCheckOption(view);
		bool		cascaded = RelationHasCascadedCheckOption(view);

		/*
		 * If the parent view has a cascaded check option, treat this view as
		 * if it also had a cascaded check option.
		 *
		 * New WithCheckOptions are added to the start of the list, so if
		 * there is a cascaded check option, it will be the first item in the
		 * list.
		 */
		if (parsetree->withCheckOptions != NIL)
		{
			WithCheckOption *parent_wco =
			(WithCheckOption *) linitial(parsetree->withCheckOptions);

			if (parent_wco->cascaded)
			{
				has_wco = true;
				cascaded = true;
			}
		}

		/*
		 * Add the new WithCheckOption to the start of the list, so that
		 * checks on inner views are run before checks on outer views, as
		 * required by the SQL standard.
		 *
		 * If the new check is CASCADED, we need to add it even if this view
		 * has no quals, since there may be quals on child views.  A LOCAL
		 * check can be omitted if this view has no quals.
		 */
		if (has_wco && (cascaded || viewquery->jointree->quals != NULL))
		{
			WithCheckOption *wco;

			wco = makeNode(WithCheckOption);
			wco->kind = WCO_VIEW_CHECK;
			wco->relname = pstrdup(RelationGetRelationName(view));
			wco->polname = NULL;
			wco->qual = NULL;
			wco->cascaded = cascaded;

			parsetree->withCheckOptions = lcons(wco,
												parsetree->withCheckOptions);

			if (viewquery->jointree->quals != NULL)
			{
				wco->qual = (Node *) viewquery->jointree->quals;
				ChangeVarNodes(wco->qual, base_rt_index, new_rt_index, 0);

				/*
				 * Make sure that the query is marked correctly if the added
				 * qual has sublinks.  We can skip this check if the query is
				 * already marked, or if the command is an UPDATE, in which
				 * case the same qual will have already been added, and this
				 * check will already have been done.
				 */
				if (!parsetree->hasSubLinks &&
					parsetree->commandType != CMD_UPDATE)
					parsetree->hasSubLinks = checkExprHasSubLink(wco->qual);
			}
		}
	}

	heap_close(base_rel, NoLock);

	return parsetree;
}


/*
 * RewriteQuery -
 *	  rewrites the query and apply the rules again on the queries rewritten
 *
 * rewrite_events is a list of open query-rewrite actions, so we can detect
 * infinite recursion.
 */
static List *
RewriteQuery(Query *parsetree, List *rewrite_events)
{
	CmdType		event = parsetree->commandType;
	bool		instead = false;
	bool		returning = false;
	bool		updatableview = false;
	Query	   *qual_product = NULL;
	List	   *rewritten = NIL;
	ListCell   *lc1;

	/*
	 * First, recursively process any insert/update/delete statements in WITH
	 * clauses.  (We have to do this first because the WITH clauses may get
	 * copied into rule actions below.)
	 */
	foreach(lc1, parsetree->cteList)
	{
		CommonTableExpr *cte = lfirst_node(CommonTableExpr, lc1);
		Query	   *ctequery = castNode(Query, cte->ctequery);
		List	   *newstuff;

		if (ctequery->commandType == CMD_SELECT)
			continue;

		newstuff = RewriteQuery(ctequery, rewrite_events);

		/*
		 * Currently we can only handle unconditional, single-statement DO
		 * INSTEAD rules correctly; we have to get exactly one Query out of
		 * the rewrite operation to stuff back into the CTE node.
		 */
		if (list_length(newstuff) == 1)
		{
			/* Push the single Query back into the CTE node */
			ctequery = linitial_node(Query, newstuff);
			/* WITH queries should never be canSetTag */
			Assert(!ctequery->canSetTag);
			cte->ctequery = (Node *) ctequery;
		}
		else if (newstuff == NIL)
		{
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("DO INSTEAD NOTHING rules are not supported for data-modifying statements in WITH")));
		}
		else
		{
			ListCell   *lc2;

			/* examine queries to determine which error message to issue */
			foreach(lc2, newstuff)
			{
				Query	   *q = (Query *) lfirst(lc2);

				if (q->querySource == QSRC_QUAL_INSTEAD_RULE)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("conditional DO INSTEAD rules are not supported for data-modifying statements in WITH")));
				if (q->querySource == QSRC_NON_INSTEAD_RULE)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("DO ALSO rules are not supported for data-modifying statements in WITH")));
			}

			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("multi-statement DO INSTEAD rules are not supported for data-modifying statements in WITH")));
		}
	}

	/*
	 * If the statement is an insert, update, or delete, adjust its targetlist
	 * as needed, and then fire INSERT/UPDATE/DELETE rules on it.
	 *
	 * SELECT rules are handled later when we have all the queries that should
	 * get executed.  Also, utilities aren't rewritten at all (do we still
	 * need that check?)
	 */
	if (event != CMD_SELECT && event != CMD_UTILITY)
	{
		int			result_relation;
		RangeTblEntry *rt_entry;
		Relation	rt_entry_relation;
		List	   *locks;
		List	   *product_queries;
		bool		hasUpdate = false;

		result_relation = parsetree->resultRelation;
		Assert(result_relation != 0);
		rt_entry = rt_fetch(result_relation, parsetree->rtable);
		Assert(rt_entry->rtekind == RTE_RELATION);

		/*
		 * We can use NoLock here since either the parser or
		 * AcquireRewriteLocks should have locked the rel already.
		 */
		rt_entry_relation = heap_open(rt_entry->relid, NoLock);

		/*
		 * Rewrite the targetlist as needed for the command type.
		 */
		if (event == CMD_INSERT)
		{
			RangeTblEntry *values_rte = NULL;

			/*
			 * If it's an INSERT ... VALUES (...), (...), ... there will be a
			 * single RTE for the VALUES targetlists.
			 */
			if (list_length(parsetree->jointree->fromlist) == 1)
			{
				RangeTblRef *rtr = (RangeTblRef *) linitial(parsetree->jointree->fromlist);

				if (IsA(rtr, RangeTblRef))
				{
					RangeTblEntry *rte = rt_fetch(rtr->rtindex,
												  parsetree->rtable);

					if (rte->rtekind == RTE_VALUES)
						values_rte = rte;
				}
			}

			if (values_rte)
			{
				List	   *attrnos;

				/* Process the main targetlist ... */
				parsetree->targetList = rewriteTargetListIU(parsetree->targetList,
															parsetree->commandType,
															parsetree->override,
															rt_entry_relation,
															parsetree->resultRelation,
															&attrnos);
				/* ... and the VALUES expression lists */
				rewriteValuesRTE(values_rte, rt_entry_relation, attrnos);
			}
			else
			{
				/* Process just the main targetlist */
				parsetree->targetList =
					rewriteTargetListIU(parsetree->targetList,
										parsetree->commandType,
										parsetree->override,
										rt_entry_relation,
										parsetree->resultRelation, NULL);
			}

			if (parsetree->onConflict &&
				parsetree->onConflict->action == ONCONFLICT_UPDATE)
			{
				parsetree->onConflict->onConflictSet =
					rewriteTargetListIU(parsetree->onConflict->onConflictSet,
										CMD_UPDATE,
										parsetree->override,
										rt_entry_relation,
										parsetree->resultRelation,
										NULL);
			}
		}
		else if (event == CMD_UPDATE)
		{
			parsetree->targetList =
				rewriteTargetListIU(parsetree->targetList,
									parsetree->commandType,
									parsetree->override,
									rt_entry_relation,
									parsetree->resultRelation, NULL);
		}
		else if (event == CMD_DELETE)
		{
			/* Nothing to do here */
		}
		else
			elog(ERROR, "unrecognized commandType: %d", (int) event);

		/*
		 * Collect and apply the appropriate rules.
		 */
		locks = matchLocks(event, rt_entry_relation->rd_rules,
						   result_relation, parsetree, &hasUpdate);

		product_queries = fireRules(parsetree,
									result_relation,
									event,
									locks,
									&instead,
									&returning,
									&qual_product);

		/*
		 * If there were no INSTEAD rules, and the target relation is a view
		 * without any INSTEAD OF triggers, see if the view can be
		 * automatically updated.  If so, we perform the necessary query
		 * transformation here and add the resulting query to the
		 * product_queries list, so that it gets recursively rewritten if
		 * necessary.
		 */
		if (!instead && qual_product == NULL &&
			rt_entry_relation->rd_rel->relkind == RELKIND_VIEW &&
			!view_has_instead_trigger(rt_entry_relation, event))
		{
			/*
			 * This throws an error if the view can't be automatically
			 * updated, but that's OK since the query would fail at runtime
			 * anyway.
			 */
			parsetree = rewriteTargetView(parsetree, rt_entry_relation);

			/*
			 * At this point product_queries contains any DO ALSO rule
			 * actions. Add the rewritten query before or after those.  This
			 * must match the handling the original query would have gotten
			 * below, if we allowed it to be included again.
			 */
			if (parsetree->commandType == CMD_INSERT)
				product_queries = lcons(parsetree, product_queries);
			else
				product_queries = lappend(product_queries, parsetree);

			/*
			 * Set the "instead" flag, as if there had been an unqualified
			 * INSTEAD, to prevent the original query from being included a
			 * second time below.  The transformation will have rewritten any
			 * RETURNING list, so we can also set "returning" to forestall
			 * throwing an error below.
			 */
			instead = true;
			returning = true;
			updatableview = true;
		}

		/*
		 * If we got any product queries, recursively rewrite them --- but
		 * first check for recursion!
		 */
		if (product_queries != NIL)
		{
			ListCell   *n;
			rewrite_event *rev;

			foreach(n, rewrite_events)
			{
				rev = (rewrite_event *) lfirst(n);
				if (rev->relation == RelationGetRelid(rt_entry_relation) &&
					rev->event == event)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("infinite recursion detected in rules for relation \"%s\"",
									RelationGetRelationName(rt_entry_relation))));
			}

			rev = (rewrite_event *) palloc(sizeof(rewrite_event));
			rev->relation = RelationGetRelid(rt_entry_relation);
			rev->event = event;
			rewrite_events = lcons(rev, rewrite_events);

			foreach(n, product_queries)
			{
				Query	   *pt = (Query *) lfirst(n);
				List	   *newstuff;

				newstuff = RewriteQuery(pt, rewrite_events);
				rewritten = list_concat(rewritten, newstuff);
			}

			rewrite_events = list_delete_first(rewrite_events);
		}

		/*
		 * If there is an INSTEAD, and the original query has a RETURNING, we
		 * have to have found a RETURNING in the rule(s), else fail. (Because
		 * DefineQueryRewrite only allows RETURNING in unconditional INSTEAD
		 * rules, there's no need to worry whether the substituted RETURNING
		 * will actually be executed --- it must be.)
		 */
		if ((instead || qual_product != NULL) &&
			parsetree->returningList &&
			!returning)
		{
			switch (event)
			{
				case CMD_INSERT:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot perform INSERT RETURNING on relation \"%s\"",
									RelationGetRelationName(rt_entry_relation)),
							 errhint("You need an unconditional ON INSERT DO INSTEAD rule with a RETURNING clause.")));
					break;
				case CMD_UPDATE:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot perform UPDATE RETURNING on relation \"%s\"",
									RelationGetRelationName(rt_entry_relation)),
							 errhint("You need an unconditional ON UPDATE DO INSTEAD rule with a RETURNING clause.")));
					break;
				case CMD_DELETE:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot perform DELETE RETURNING on relation \"%s\"",
									RelationGetRelationName(rt_entry_relation)),
							 errhint("You need an unconditional ON DELETE DO INSTEAD rule with a RETURNING clause.")));
					break;
				default:
					elog(ERROR, "unrecognized commandType: %d",
						 (int) event);
					break;
			}
		}

		/*
		 * Updatable views are supported by ON CONFLICT, so don't prevent that
		 * case from proceeding
		 */
		if (parsetree->onConflict &&
			(product_queries != NIL || hasUpdate) &&
			!updatableview)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("INSERT with ON CONFLICT clause cannot be used with table that has INSERT or UPDATE rules")));

		heap_close(rt_entry_relation, NoLock);
	}

	/*
	 * For INSERTs, the original query is done first; for UPDATE/DELETE, it is
	 * done last.  This is needed because update and delete rule actions might
	 * not do anything if they are invoked after the update or delete is
	 * performed. The command counter increment between the query executions
	 * makes the deleted (and maybe the updated) tuples disappear so the scans
	 * for them in the rule actions cannot find them.
	 *
	 * If we found any unqualified INSTEAD, the original query is not done at
	 * all, in any form.  Otherwise, we add the modified form if qualified
	 * INSTEADs were found, else the unmodified form.
	 */
	if (!instead)
	{
		if (parsetree->commandType == CMD_INSERT)
		{
			if (qual_product != NULL)
				rewritten = lcons(qual_product, rewritten);
			else
				rewritten = lcons(parsetree, rewritten);
		}
		else
		{
			if (qual_product != NULL)
				rewritten = lappend(rewritten, qual_product);
			else
				rewritten = lappend(rewritten, parsetree);
		}
	}

	/*
	 * If the original query has a CTE list, and we generated more than one
	 * non-utility result query, we have to fail because we'll have copied the
	 * CTE list into each result query.  That would break the expectation of
	 * single evaluation of CTEs.  This could possibly be fixed by
	 * restructuring so that a CTE list can be shared across multiple Query
	 * and PlannableStatement nodes.
	 */
	if (parsetree->cteList != NIL)
	{
		int			qcount = 0;

		foreach(lc1, rewritten)
		{
			Query	   *q = (Query *) lfirst(lc1);

			if (q->commandType != CMD_UTILITY)
				qcount++;
		}
		if (qcount > 1)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("WITH cannot be used in a query that is rewritten by rules into multiple queries")));
	}

	return rewritten;
}


/*
 * QueryRewrite -
 *	  Primary entry point to the query rewriter.
 *	  Rewrite one query via query rewrite system, possibly returning 0
 *	  or many queries.
 *
 * NOTE: the parsetree must either have come straight from the parser,
 * or have been scanned by AcquireRewriteLocks to acquire suitable locks.
 */
List *
QueryRewrite(Query *parsetree)
{
	uint64		input_query_id = parsetree->queryId;
	List	   *querylist;
	List	   *results;
	ListCell   *l;
	CmdType		origCmdType;
	bool		foundOriginalQuery;
	Query	   *lastInstead;

	/*
	 * This function is only applied to top-level original queries
	 */
	Assert(parsetree->querySource == QSRC_ORIGINAL);
	Assert(parsetree->canSetTag);

	/*
	 * Step 1
	 *
	 * Apply all non-SELECT rules possibly getting 0 or many queries
	 */
	querylist = RewriteQuery(parsetree, NIL);

	/*
	 * Step 2
	 *
	 * Apply all the RIR rules on each query
	 *
	 * This is also a handy place to mark each query with the original queryId
	 */
	results = NIL;
	foreach(l, querylist)
	{
		Query	   *query = (Query *) lfirst(l);

		query = fireRIRrules(query, NIL);

		query->queryId = input_query_id;

		results = lappend(results, query);
	}

	/*
	 * Step 3
	 *
	 * Determine which, if any, of the resulting queries is supposed to set
	 * the command-result tag; and update the canSetTag fields accordingly.
	 *
	 * If the original query is still in the list, it sets the command tag.
	 * Otherwise, the last INSTEAD query of the same kind as the original is
	 * allowed to set the tag.  (Note these rules can leave us with no query
	 * setting the tag.  The tcop code has to cope with this by setting up a
	 * default tag based on the original un-rewritten query.)
	 *
	 * The Asserts verify that at most one query in the result list is marked
	 * canSetTag.  If we aren't checking asserts, we can fall out of the loop
	 * as soon as we find the original query.
	 */
	origCmdType = parsetree->commandType;
	foundOriginalQuery = false;
	lastInstead = NULL;

	foreach(l, results)
	{
		Query	   *query = (Query *) lfirst(l);

		if (query->querySource == QSRC_ORIGINAL)
		{
			Assert(query->canSetTag);
			Assert(!foundOriginalQuery);
			foundOriginalQuery = true;
#ifndef USE_ASSERT_CHECKING
			break;
#endif
		}
		else
		{
			Assert(!query->canSetTag);
			if (query->commandType == origCmdType &&
				(query->querySource == QSRC_INSTEAD_RULE ||
				 query->querySource == QSRC_QUAL_INSTEAD_RULE))
				lastInstead = query;
		}
	}

	if (!foundOriginalQuery && lastInstead != NULL)
		lastInstead->canSetTag = true;

	return results;
}
