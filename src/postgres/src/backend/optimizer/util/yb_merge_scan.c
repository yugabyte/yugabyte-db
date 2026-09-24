/*-------------------------------------------------------------------------
 *
 * yb_merge_scan.c
 *	  Utilities for merge scan
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
 *	  src/backend/optimizer/util/yb_merge_scan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/stratnum.h"
#include "access/table.h"
#include "access/transam.h"
#include "access/yb_cost.h"
#include "catalog/pg_operator_d.h"
#include "catalog/pg_type_d.h"
#include "common/int.h"
#include "executor/ybExpr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/paths.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/yb_merge_scan.h"
#include "optimizer/ybplan.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "rewrite/rewriteHandler.h"
#include "utils/array.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"

/* GUC options */
bool		yb_enable_derived_saops;
int			yb_max_merge_scan_streams;

/*
 * Whether the given clause is an eligible SAOP.  Eligibility details are in
 * the code itself.  Return whether eligible, and if so, fill out param
 * num_elems.
 */
static bool
ybIsClauseEligibleSaop(Node *clause,
					   Expr *expr,
					   Oid opfamily,
					   Oid idxcollation,
					   int *num_elems)
{
	/*
	 * 1. Check structure is SAOP.
	 */
	if (!(clause && IsA(clause, ScalarArrayOpExpr)))
		return false;

	/*
	 * 2. Check operator (part 1).
	 */
	ScalarArrayOpExpr *opexpr = (ScalarArrayOpExpr *) clause;
	Oid			oprid = opexpr->opno;
	Expr	   *lhs = linitial(opexpr->args);
	Expr	   *rhs = lsecond(opexpr->args);

	/* Disallow NOT IN and op ALL. */
	if (!opexpr->useOr)
		return false;

	/*
	 * 3. Check LHS.
	 *
	 * We don't care about SAOPs bound to expressions besides the given expr.
	 * Strip any RelabelType node so that index columns whose type differs by
	 * only a no-op coercion (e.g. varchar to text) can still match.
	 */
	if (IsA(lhs, RelabelType))
		lhs = ((RelabelType *) lhs)->arg;
	if (!equal(lhs, expr))
		return false;

	/*
	 * 4. Check RHS.
	 *
	 * Only allow simple Const RHS:
	 * - IN (1, 2) --> Const
	 * - IN (1, random()::int) --> ArrayExpr[Const, FuncExpr]
	 * - IN (1, (5 + random()::int)) --> ArrayExpr[Const, OpExpr[Const,
	 *															 FuncExpr]]
	 * - IN (1, (SELECT count(*) FROM t)) --> ArrayExpr[Const, Param]
	 */
	if (!IsA(rhs, Const))
		return false;

	if (castNode(Const, rhs)->constisnull)
		return false;

	/*
	 * 5. Check collation.
	 *
	 * As in match_saopclause_to_indexcol (see IndexCollMatchesExprColl),
	 * reject the clause unless the index collation matches the clause's
	 * comparison collation.  A SAOP pinned here is expected to reach the
	 * executor among the index conditions, but match_saopclause_to_indexcol
	 * drops such a clause from them.
	 */
	if (OidIsValid(idxcollation) && idxcollation != opexpr->inputcollid)
		return false;

	/*
	 * 6. Check operator (part 2).
	 *
	 * This is last as it is more expensive than the other checks.
	 */
	if (get_op_opfamily_strategy(oprid, opfamily) != BTEqualStrategyNumber)
		return false;

	/*
	 * 7. Checks passed.  Collect data.
	 */
	ArrayType  *arrayval;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	Datum	   *elem_values;
	bool	   *elem_nulls;

	/*
	 * Fill out param num_elems.
	 *
	 * TODO(#29073): use YbCullArray to get a more accurate count and avoid the
	 * case of zero-length arrays (like arrays with only nulls as elements).
	 */
	arrayval = DatumGetArrayTypeP(castNode(Const, rhs)->constvalue);
	get_typlenbyvalalign(ARR_ELEMTYPE(arrayval),
						 &elmlen, &elmbyval, &elmalign);
	deconstruct_array(arrayval,
					  ARR_ELEMTYPE(arrayval),
					  elmlen, elmbyval, elmalign,
					  &elem_values, &elem_nulls, num_elems);

	return true;
}

/*
 * Try to derive a SAOP on the given opexpr.  Currently, this only looks for
 * operator=%(int4, int4), LHS=yb_hash_code(...), RHS=int4.
 */
static bool
ybDeriveSaopFromOpExpr(OpExpr *opexpr,
					   ScalarArrayOpExpr **best_saop,
					   int *best_num_elems)
{
	Oid			oprid = opexpr->opno;

	/* %(int4, int4) */
	if (oprid != 530)
		return false;

	Expr	   *lhs = linitial(opexpr->args);
	Expr	   *rhs = lsecond(opexpr->args);

	/* Check each operand of the modulo operator. */
	if (!(IsA(lhs, FuncExpr) && IsA(rhs, Const)))
		return false;

	FuncExpr   *funcexpr = castNode(FuncExpr, lhs);
	Const	   *const_node = castNode(Const, rhs);

	/*
	 * For now, only allow function yb_hash_code as it has the property of
	 * being immutable and always yields a non-null, non-negative number.
	 */
	if (funcexpr->funcid != F_YB_HASH_CODE)
		return false;

	/*
	 * Reject degenerate moduli.  A null modulus makes the whole expression
	 * yield null for every row, and modulo zero raises an error, so neither
	 * yields any stream.  A negative modulus buckets the same as its
	 * absolute value because yb_hash_code is non-negative, but such a schema
	 * is almost certainly a mistake, and normalizing it by negation would
	 * overflow for INT32_MIN, so do not derive from it either.
	 */
	if (const_node->constisnull)
		return false;

	int			modulus = DatumGetInt32(const_node->constvalue);

	if (modulus <= 0)
		return false;

	/*
	 * No point trying to derive a SAOP that has higher cardinality than an
	 * existing one.
	 */
	if (*best_num_elems >= 0 && modulus >= *best_num_elems)
		return false;

	/* Build the new SAOP */
	ScalarArrayOpExpr *saop = makeNode(ScalarArrayOpExpr);

	saop->opno = Int4EqualOperator;
	saop->opfuncid = F_INT4EQ;
	saop->useOr = true;
	saop->inputcollid = InvalidOid;

	saop->args = list_make1(opexpr);

	Datum	   *elems = palloc(sizeof(Datum) * modulus);

	for (int i = 0; i < modulus; ++i)
		elems[i] = i;

	ArrayType  *arr = construct_array(elems,
									  modulus,
									  INT4OID,
									  4,
									  true,
									  TYPALIGN_INT);
	Const	   *arr_const = makeConst(INT4ARRAYOID,
									  -1,
									  InvalidOid,
									  -1,
									  PointerGetDatum(arr),
									  false,
									  false);
	saop->args = lappend(saop->args, arr_const);

	/* Fill the out params. */
	*best_saop = saop;
	*best_num_elems = modulus;
	return true;
}

/*
 * Try to derive a SAOP on the given var.  Currently, this only looks for
 * generated columns and tries to derive SAOP on the generation expression.
 */
static bool
ybDeriveSaopFromVar(Var *var,
					PlannerInfo *root,
					Relids relids,
					ScalarArrayOpExpr **best_saop,
					int *best_num_elems)
{
	bool		derived = false;
	int			i = var->varattno - 1;
	int			rtindex;

	if (!bms_get_singleton_member(relids, &rtindex))
		return false;

	RangeTblEntry *rte = planner_rt_fetch(rtindex, root);
	Relation	rel = table_open(rte->relid, NoLock);
	TupleDesc	tupdesc = RelationGetDescr(rel);

	/* Parts taken from ExecInitStoredGenerated */
	/* Nothing to do if no generated columns */
	if (tupdesc->constr && tupdesc->constr->has_generated_stored)
	{
		if (TupleDescAttr(tupdesc, i)->attgenerated == ATTRIBUTE_GENERATED_STORED)
		{
			Expr	   *expr;

			/* Fetch the GENERATED AS expression tree */
			expr = (Expr *) build_column_default(rel, i + 1);
			if (expr == NULL)
				elog(ERROR, "no generation expression found for column number %d of table \"%s\"",
					 i + 1, RelationGetRelationName(rel));

			if (IsA(expr, OpExpr))
			{
				derived = ybDeriveSaopFromOpExpr(castNode(OpExpr, expr),
												 best_saop, best_num_elems);
				/*
				 * Ensure var is used instead of the generated expression for
				 * LHS of derived SAOP.
				 */
				if (derived)
					linitial((*best_saop)->args) = var;
			}
		}
	}

	table_close(rel, NoLock);
	return derived;
}

/*
 * Whether this index column is able to be part of merge scan.  If true, find
 * the best SAOP (best meaning having the smallest cardinality), and fill
 * in/out params merge_scan_cardinality and merge_scan_stream_cols.
 */
bool
yb_indexcol_can_merge_scan(PlannerInfo *root,
						   IndexOptInfo *index,
						   Expr *expr,
						   int indexcol,
						   int *merge_scan_cardinality,
						   List **merge_scan_stream_cols)
{
	ListCell   *lc;
	int			best_num_elems = -1;
	ScalarArrayOpExpr *best_saop;
	YbMergeScanStreamColInfo *stream_col_info;

	/*
	 * Abort if any of the following hold:
	 * - the caller disables merge scan (in/out param merge_scan_stream_cols is
	 *   NULL)
	 * - the session disables merge scan (GUC yb_max_merge_scan_streams is 0 or
	 *   yb_enable_base_scans_cost_model is false)
	 * - merge scan is not supported for this relation (not a YB relation)
	 */
	if (!(merge_scan_stream_cols &&
		  yb_max_merge_scan_streams > 0 && yb_enable_base_scans_cost_model &&
		  index->rel->is_yb_relation))
		return false;

	/*
	 * Strip any RelabelType node so that index columns whose type differs by
	 * only a no-op coercion (e.g. varchar to text) can still match.
	 */
	if (IsA(expr, RelabelType))
		expr = ((RelabelType *) expr)->arg;

	/*
	 * If same expr already used in stream_cols, then redundant.  Every entry
	 * here carries a SAOP: the single-value equality entries are appended
	 * after the pathkeys walk that calls this function.
	 */
	foreach(lc, *merge_scan_stream_cols)
	{
		YbMergeScanStreamColInfo *old_stream_col_info =
			lfirst_node(YbMergeScanStreamColInfo, lc);
		Expr	   *old_lhs =
			linitial(castNode(ScalarArrayOpExpr,
							  old_stream_col_info->clause)->args);

		/*
		 * Strip any RelabelType node so that index columns whose type differs
		 * by only a no-op coercion (e.g. varchar to text) can still match.
		 */
		if (IsA(old_lhs, RelabelType))
			old_lhs = ((RelabelType *) old_lhs)->arg;
		if (equal(expr, old_lhs))
			return true;
	}

	/*
	 * Loop over index clauses looking for the smallest SAOP for this index
	 * expr.  (Parts copied from indexcol_is_bool_constant_for_query.)
	 */
	foreach(lc, index->rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);
		int			num_elems;

		/*
		 * As in match_clause_to_indexcol, never match pseudoconstants to
		 * indexes.  (It might be semantically okay to do so here, but the
		 * odds of getting a match are negligible, so don't waste the cycles.)
		 */
		if (rinfo->pseudoconstant)
			continue;

		/*
		 * As in match_clause_to_index, if the clause can't be used as an
		 * indexqual because it must wait till after some lower-security-level
		 * restriction clause, reject it.  A SAOP pinned here is expected to
		 * reach the executor among the index conditions, but
		 * match_clause_to_index drops such a clause from them.
		 */
		if (!restriction_is_securely_promotable(rinfo, index->rel))
			continue;

		/*
		 * If this is an eligible SAOP index clause, keep track of it if it is
		 * better than the last one seen.
		 */
		if (ybIsClauseEligibleSaop((Node *) rinfo->clause, expr,
									   index->opfamily[indexcol],
									   index->indexcollations[indexcol],
									   &num_elems) &&
			(num_elems < best_num_elems || best_num_elems == -1))
		{
			best_num_elems = num_elems;
			best_saop = (ScalarArrayOpExpr *) rinfo->clause;

			/* Optimization when the cardinality can't get better than zero. */
			if (best_num_elems == 0 || *merge_scan_cardinality == 0)
				break;
		}
	}

	bool		derived = false;

	/*
	 * Derived SAOPs need no securely-promotable check.  They are not query
	 * clauses subject to RLS evaluation order but tautologies fabricated over
	 * the index expression, and they bind against stored index values, which
	 * the owner-defined expression already produced at write time, using the
	 * builtin int equality.
	 */
	if (yb_enable_derived_saops)
	{
		if (IsA(expr, OpExpr))
			derived = ybDeriveSaopFromOpExpr(castNode(OpExpr, expr),
											 &best_saop, &best_num_elems);
		else if (IsA(expr, Var))
			derived = ybDeriveSaopFromVar(castNode(Var, expr), root,
										  index->rel->relids,
										  &best_saop, &best_num_elems);
	}

	/* Abort if no eligible SAOP index clauses were found. */
	if (best_num_elems == -1)
		return false;

	/*
	 * Fill out param merge_scan_cardinality.  Abort upon hitting the
	 * cardinality limit.
	 */
	if (unlikely(pg_mul_s32_overflow(*merge_scan_cardinality, best_num_elems,
									 merge_scan_cardinality)) ||
		*merge_scan_cardinality > yb_max_merge_scan_streams)
		return false;

	/* Fill out param merge_scan_stream_cols. */
	stream_col_info = makeNode(YbMergeScanStreamColInfo);
	stream_col_info->clause = (Expr *) best_saop;
	stream_col_info->indexcol = indexcol;
	stream_col_info->num_elems = best_num_elems;
	stream_col_info->derived = derived;
	*merge_scan_stream_cols = lappend(*merge_scan_stream_cols,
									  stream_col_info);
	return true;
}

/*
 * Get sort info for given pathkeys corresponding to tlist.  In case a pathkey
 * matches multiple columns in tlist, avoid the columns that are pinned as
 * stream key columns.
 *
 * (Parts copied from prepare_sort_from_pathkeys.)
 */
void
yb_get_sort_info_from_pathkeys(List *tlist,
							   List *pathkeys,
							   Relids relids,
							   Bitmapset *stream_col_idxs,
							   int *p_numsortkeys,
							   AttrNumber **p_sortColIdx,
							   Oid **p_sortOperators,
							   Oid **p_collations,
							   bool **p_nullsFirst)
{
	ListCell   *i;
	int			numsortkeys;
	AttrNumber *sortColIdx;
	Oid		   *sortOperators;
	Oid		   *collations;
	bool	   *nullsFirst;

	/*
	 * We will need at most list_length(pathkeys) sort columns; possibly less
	 */
	numsortkeys = list_length(pathkeys);
	sortColIdx = (AttrNumber *) palloc(numsortkeys * sizeof(AttrNumber));
	sortOperators = (Oid *) palloc(numsortkeys * sizeof(Oid));
	collations = (Oid *) palloc(numsortkeys * sizeof(Oid));
	nullsFirst = (bool *) palloc(numsortkeys * sizeof(bool));

	numsortkeys = 0;

	foreach(i, pathkeys)
	{
		PathKey    *pathkey = (PathKey *) lfirst(i);
		EquivalenceClass *ec = pathkey->pk_eclass;
		EquivalenceMember *em;
		TargetEntry *tle = NULL;
		Oid			pk_datatype = InvalidOid;
		Oid			sortop;
		ListCell   *j;

		{
			/*
			 * Otherwise, we can sort by any non-constant expression listed in
			 * the pathkey's EquivalenceClass.  For now, we take the first
			 * tlist item found in the EC. If there's no match, we'll generate
			 * a resjunk entry using the first EC member that is an expression
			 * in the input's vars.  (The non-const restriction only matters
			 * if the EC is below_outer_join; but if it isn't, it won't
			 * contain consts anyway, else we'd have discarded the pathkey as
			 * redundant.)
			 *
			 * XXX if we have a choice, is there any way of figuring out which
			 * might be cheapest to execute?  (For example, int4lt is likely
			 * much cheaper to execute than numericlt, but both might appear
			 * in the same equivalence class...)  Not clear that we ever will
			 * have an interesting choice in practice, so it may not matter.
			 */
			int			indexcol = 0;

			foreach(j, tlist)
			{
				/*
				 * YB: skip over stream key cols, which may be part of sort ECs
				 * in case they are equal to other columns part of sort.
				 */
				if (bms_is_member(indexcol++, stream_col_idxs))
					continue;

				tle = (TargetEntry *) lfirst(j);
				em = find_ec_member_matching_expr(ec, tle->expr, relids);
				if (em)
				{
					/* found expr already in tlist */
					pk_datatype = em->em_datatype;
					break;
				}
				tle = NULL;
			}
		}

		if (!tle)
			elog(ERROR, "could not find pathkey item to sort");

		/*
		 * Look up the correct sort operator from the PathKey's slightly
		 * abstracted representation.
		 */
		sortop = get_opfamily_member(pathkey->pk_opfamily,
									 pk_datatype,
									 pk_datatype,
									 pathkey->pk_strategy);
		if (!OidIsValid(sortop))	/* should not happen */
			elog(ERROR, "missing operator %d(%u,%u) in opfamily %u",
				 pathkey->pk_strategy, pk_datatype, pk_datatype,
				 pathkey->pk_opfamily);

		/* Add the column to the sort arrays */
		sortColIdx[numsortkeys] = tle->resno;
		sortOperators[numsortkeys] = sortop;
		collations[numsortkeys] = ec->ec_collation;
		nullsFirst[numsortkeys] = pathkey->pk_nulls_first;
		numsortkeys++;
	}

	/* Return results */
	*p_numsortkeys = numsortkeys;
	*p_sortColIdx = sortColIdx;
	*p_sortOperators = sortOperators;
	*p_collations = collations;
	*p_nullsFirst = nullsFirst;
}

/*
 * Whether the operator clause is an equality, per the index column's
 * opfamily, between the given index column expression and anything else.
 */
static bool
ybIsEqualityOnIndexCol(IndexOptInfo *index, int indexcol, Expr *expr,
					   Expr *clause)
{
	OpExpr	   *op;
	Expr	   *lhs;
	Expr	   *rhs;

	if (!is_opclause(clause) || list_length(((OpExpr *) clause)->args) != 2)
		return false;
	op = (OpExpr *) clause;
	if (get_op_opfamily_strategy(op->opno, index->opfamily[indexcol]) !=
		BTEqualStrategyNumber)
		return false;
	lhs = linitial(op->args);
	rhs = lsecond(op->args);
	if (IsA(lhs, RelabelType))
		lhs = ((RelabelType *) lhs)->arg;
	if (IsA(rhs, RelabelType))
		rhs = ((RelabelType *) rhs)->arg;
	return equal(lhs, expr) || equal(rhs, expr);
}

/*
 * The first equality index clause on the column whose index column expression
 * is expr, or NULL if there is none.
 */
static Expr *
ybEqualityIndexClause(IndexOptInfo *index, List *indexclauses, int indexcol,
					  Expr *expr)
{
	ListCell   *lc;

	foreach(lc, indexclauses)
	{
		IndexClause *iclause = lfirst_node(IndexClause, lc);
		ListCell   *lc2;

		if (iclause->indexcol != indexcol)
			continue;
		foreach(lc2, iclause->indexquals)
		{
			RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc2);

			if (ybIsEqualityOnIndexCol(index, indexcol, expr, rinfo->clause))
				return rinfo->clause;
		}
	}
	return NULL;
}

/*
 * Whether check_index_predicates dropped an equality on the index column from
 * the index conditions because the partial index predicate implies it: such a
 * clause is in the relation's baserestrictinfo but not in indrestrictinfo.
 */
static bool
ybEqualityImpliedByIndexPredicate(IndexOptInfo *index, int indexcol,
								  Expr *expr)
{
	ListCell   *lc;

	if (index->indpred == NIL)
		return false;
	foreach(lc, index->rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);

		if (list_member_ptr(index->indrestrictinfo, rinfo))
			continue;
		if (ybIsEqualityOnIndexCol(index, indexcol, expr, rinfo->clause))
			return true;
	}
	return false;
}

/*
 * Prune the stream key columns whose bind the merge order does not need, and
 * set the clause of each single-value equality column kept to its equality
 * index clause.  This happens here because only the plan knows its last merge
 * sort column, after truncate_useless_pathkeys, and its index clauses.
 * Dropped:
 *
 * - A column at or after the last merge sort column (0-based
 *   last_sort_indexcol), which does not affect the order and fans out no
 *   streams.
 * - A duplicate index column, such as the second b of (a, b, b, c), which
 *   holds the same value as the first.
 * - A range column whose equality a partial index predicate implies, which
 *   check_index_predicates removes from the index conditions.
 * - A range column whose EquivalenceClass is broken.  The planner could not
 *   build the clause equating the column to the constant and kept the original
 *   clauses as filters.  The merge order holds only if those filters run within
 *   each stream, before the merge.  Otherwise the pathkeys walk has cut the
 *   merge sort keys at the column, see yb_merge_scan_gap_covered.
 *
 * TODO(#34120): drop hash columns of the last two kinds too, which pggate
 * cannot merge without a condition on each.
 *
 * A column with an equality index clause is dropped only in the first case.
 * Any other equality column without an equality index clause stays declared
 * with a NULL clause, so that ybValidateMergeScanBinds reports it.  A change
 * that evaluates such an equality as a filter inside the per-stream storage
 * request keeps the order and must add that case here, or the validation
 * rejects the plan.  Returns a new list.
 */
List *
yb_prune_merge_scan_stream_cols(IndexOptInfo *index, List *stream_cols,
								List *indexclauses, int last_sort_indexcol)
{
	List	   *result = NIL;
	ListCell   *lc;

	foreach(lc, stream_cols)
	{
		YbMergeScanStreamColInfo *info = lfirst_node(YbMergeScanStreamColInfo,
													 lc);
		Expr	   *clause;
		Expr	   *expr;
		bool		dup = false;
		int			i;

		/* At or after the last merge sort column: not needed for order. */
		if (info->indexcol >= last_sort_indexcol)
			continue;

		/* A SAOP column ahead of it: keep the entry as it is. */
		if (info->clause)
		{
			result = lappend(result, info);
			continue;
		}

		expr = ((TargetEntry *) list_nth(index->indextlist,
										 info->indexcol))->expr;
		clause = ybEqualityIndexClause(index, indexclauses, info->indexcol,
									   expr);
		if (clause)
		{
			/*
			 * Give the plan its own entry rather than filling in the path's,
			 * which stays free of plan state and can be planned again.
			 */
			YbMergeScanStreamColInfo *plan_info =
				makeNode(YbMergeScanStreamColInfo);

			*plan_info = *info;
			plan_info->clause = clause;
			result = lappend(result, plan_info);
			continue;
		}

		for (i = 0; i < info->indexcol; i++)
		{
			if (equal(expr,
					  ((TargetEntry *) list_nth(index->indextlist, i))->expr))
			{
				dup = true;
				break;
			}
		}
		if (dup ||
			(info->indexcol >= index->nhashcolumns &&
			 (info->broken_ec ||
			  ybEqualityImpliedByIndexPredicate(index, info->indexcol,
												expr))))
			continue;

		result = lappend(result, info);
	}
	return result;
}

/* Whether op is an equality operator of one of ec's operator families. */
static bool
ybIsEquivalenceClassEquality(EquivalenceClass *ec, OpExpr *op)
{
	ListCell   *lc;

	foreach(lc, ec->ec_opfamilies)
	{
		if (get_op_opfamily_strategy(op->opno, lfirst_oid(lc)) ==
			BTEqualStrategyNumber)
			return true;
	}
	return false;
}

/*
 * Whether colrefs refers to a primary key column that index returns only by
 * decoding the base table row reference.  An index-only scan evaluates a filter
 * on such a column locally, after the merge.
 */
static bool
ybRefersToDecodedPkCol(List *colrefs, IndexOptInfo *index)
{
	ListCell   *lc;

	foreach(lc, colrefs)
	{
		YbExprColrefDesc *colref = castNode(YbExprColrefDesc, lfirst(lc));

		for (int i = index->ncolumns - index->yb_num_decoded_pk_cols;
			 i < index->ncolumns; ++i)
		{
			if (colref->attno == index->indexkeys[i])
				return true;
		}
	}
	return false;
}

/*
 * Whether a merge scan may keep sorting past index column indexcol.  The
 * pathkeys walk skipped the column as redundant through ec, an EquivalenceClass
 * that yields no index condition on it.  Either ec has no constant, or ec is
 * broken.
 *
 * A duplicate of an earlier index column is always fine.  Otherwise the column
 * is redundant through an equality such as c1 = c2 or c1 = 5, and each stream
 * is sorted by the merge sort keys only among the rows that satisfy it.  So the
 * equality must be filtered within each stream, before the merge.  A
 * pushed-down filter does that, except a table filter on a secondary index that
 * is not embedded, which runs after the index-side merge, and a filter on a
 * decoded primary key column.
 *
 * Every base restriction clause joining two members of ec must qualify.
 * Return false if there is none.
 */
bool
yb_merge_scan_gap_covered(PlannerInfo *root, IndexOptInfo *index,
						  int indexcol, EquivalenceClass *ec)
{
	RelOptInfo *rel = index->rel;
	Oid			relid = planner_rt_fetch(rel->relid, root)->relid;
	Expr	   *expr = ((TargetEntry *) list_nth(index->indextlist,
												 indexcol))->expr;
	bool		index_side_only = true;
	bool		found = false;
	ListCell   *lc;

	for (int i = 0; i < indexcol; ++i)
	{
		if (equal(expr, ((TargetEntry *) list_nth(index->indextlist, i))->expr))
			return true;
	}

	/*
	 * A primary key or embedded index reads the table within the per-stream
	 * request, so a filter pushed to the table runs before the merge there.
	 */
	if (!index->hypothetical)
	{
		Relation	table_rel = RelationIdGetRelation(relid);
		Relation	index_rel = RelationIdGetRelation(index->indexoid);

		index_side_only = (!index_rel->rd_index->indisprimary &&
						   !YbIsScanningEmbeddedIdx(table_rel, index_rel));
		RelationClose(index_rel);
		RelationClose(table_rel);
	}

	foreach(lc, rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);
		OpExpr	   *op;
		bool		lhs_member = false;
		bool		rhs_member = false;
		List	   *colrefs = NIL;
		ListCell   *lc2;

		if (!is_opclause(rinfo->clause))
			continue;
		op = (OpExpr *) rinfo->clause;
		if (list_length(op->args) != 2 ||
			!ybIsEquivalenceClassEquality(ec, op))
			continue;
		foreach(lc2, ec->ec_members)
		{
			EquivalenceMember *em = lfirst_node(EquivalenceMember, lc2);

			/*
			 * On a partition, the restriction clauses use the partition's own
			 * columns, which ec holds as child members.
			 */
			if (em->em_is_child && !bms_equal(em->em_relids, rel->relids))
				continue;
			if (equal(em->em_expr, linitial(op->args)))
				lhs_member = true;
			if (equal(em->em_expr, lsecond(op->args)))
				rhs_member = true;
		}
		if (!lhs_member || !rhs_member)
			continue;

		found = true;
		if (!YbCanPushdownExpr(rinfo->clause, &colrefs, relid) ||
			ybRefersToDecodedPkCol(colrefs, index))
			return false;
		if (index_side_only &&
			!is_index_only_attribute_nums(colrefs, index, false))
			return false;
	}
	return found;
}
