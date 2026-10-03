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
#include "catalog/pg_operator_d.h"
#include "catalog/pg_type_d.h"
#include "common/int.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/paths.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/yb_merge_scan.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "rewrite/rewriteHandler.h"
#include "utils/array.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
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
	 * here carries a SAOP, since yb_finalize_merge_scan_stream_cols adds the
	 * single-value equality entries only after the pathkeys walk that calls
	 * this function.
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
 * The first entry of tlist, an index target list, whose expression is a member
 * of ec, skipping the index columns in skip_idxs.  Those are the stream key
 * columns, which can belong to a sort EquivalenceClass when they equal a sort
 * column.  Sets *p_em to the member and returns NULL when no entry matches.
 */
static TargetEntry *
ybFindPathkeyTle(List *tlist, EquivalenceClass *ec, Relids relids,
				 Bitmapset *skip_idxs, EquivalenceMember **p_em)
{
	ListCell   *lc;
	int			indexcol = 0;

	foreach(lc, tlist)
	{
		TargetEntry *tle = lfirst_node(TargetEntry, lc);

		if (bms_is_member(indexcol++, skip_idxs))
			continue;
		*p_em = find_ec_member_matching_expr(ec, tle->expr, relids);
		if (*p_em)
			return tle;
	}
	return NULL;
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
			tle = ybFindPathkeyTle(tlist, ec, relids, stream_col_idxs, &em);
			if (tle)
				pk_datatype = em->em_datatype;
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
 * Whether the operator clause is an equality, per the index column's opfamily,
 * between index column indexcol and anything else.  match_index_to_operand
 * matches the column the way the planner matched the clause to it, ignoring a
 * RelabelType on either side.
 */
static bool
ybIsEqualityOnIndexCol(IndexOptInfo *index, int indexcol, Expr *clause)
{
	OpExpr	   *op;

	if (!is_opclause(clause) || list_length(((OpExpr *) clause)->args) != 2)
		return false;
	op = (OpExpr *) clause;
	if (get_op_opfamily_strategy(op->opno, index->opfamily[indexcol]) !=
		BTEqualStrategyNumber)
		return false;
	return match_index_to_operand(linitial(op->args), indexcol, index) ||
		match_index_to_operand(lsecond(op->args), indexcol, index);
}

/*
 * The first equality index clause on index column indexcol, or NULL if there
 * is none.
 */
static Expr *
ybEqualityIndexClause(IndexOptInfo *index, List *indexclauses, int indexcol)
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

			if (ybIsEqualityOnIndexCol(index, indexcol, rinfo->clause))
				return rinfo->clause;
		}
	}
	return NULL;
}

/*
 * Settle the stream key columns of an index path, the columns each merge
 * stream binds to one value.  stream_cols holds the SAOP columns the pathkeys
 * walk picked.  build_index_paths calls this once the path's pathkeys are
 * final, after truncate_useless_pathkeys, with the path's index clauses, so
 * that the path's costing and its plan see the same stream keys.
 *
 * Each index column before the last merge sort column is one of:
 *
 * - A SAOP column, a stream key with its SAOP.
 * - A merge sort column, not a stream key.
 * - A column the pathkeys walk skipped as redundant, one of:
 *   - A hash column, a stream key with its equality index clause, since pggate
 *     cannot merge without a condition on every hash column.
 *     ybValidateMergeScanBinds reports a hash column without an equality index
 *     clause.  TODO(#34120): merge without such a condition.
 *   - A range column with an equality index clause, a single-value stream key
 *     with that clause.  It holds one value in each stream, and the merge
 *     order relies on that.
 *   - A range column without an equality index clause, not a stream key.  It
 *     is one of:
 *     - A copy of an earlier index column, possibly relabeled.
 *     - A column a partial index predicate implies.
 *     - A column tied by filters, not a bind, to one of:
 *       - A constant, as in a broken EquivalenceClass.
 *       - An earlier merge sort column.
 *
 *       The merge order then holds only if those filters run within each
 *       stream, before the merge.  TODO(#33384): ensure that.
 *
 * No column at or after the last merge sort column is a stream key, since the
 * merge order does not depend on it.  That includes a SAOP column the pathkeys
 * walk picked there, whose streams the merge does not need.
 *
 * Returns a new list in index column order, which EXPLAIN shows, or NIL when
 * no stream key column remains, and the path is then not a merge scan.
 */
List *
yb_finalize_merge_scan_stream_cols(IndexOptInfo *index, Relids relids,
								   List *stream_cols, List *indexclauses,
								   List *pathkeys)
{
	List	   *result = NIL;
	ListCell   *lc;
	Bitmapset  *saop_idxs = NULL;
	Bitmapset  *sort_idxs = NULL;
	int			last_sort_indexcol = -1;

	foreach(lc, stream_cols)
	{
		YbMergeScanStreamColInfo *info = lfirst_node(YbMergeScanStreamColInfo,
													 lc);

		saop_idxs = bms_add_member(saop_idxs, info->indexcol);
	}

	/* Find the merge sort columns as yb_get_sort_info_from_pathkeys does. */
	foreach(lc, pathkeys)
	{
		PathKey    *pathkey = lfirst_node(PathKey, lc);
		EquivalenceMember *em;
		TargetEntry *tle = ybFindPathkeyTle(index->indextlist,
											pathkey->pk_eclass, relids,
											saop_idxs, &em);

		/* Not an ordering the merge can compare on. */
		if (!tle)
			return NIL;
		sort_idxs = bms_add_member(sort_idxs, tle->resno - 1);
		last_sort_indexcol = Max(last_sort_indexcol, tle->resno - 1);
	}

	/* Construct the result in index column order. */
	for (int indexcol = 0; indexcol < last_sort_indexcol; indexcol++)
	{
		Expr	   *clause;
		YbMergeScanStreamColInfo *info;

		if (bms_is_member(indexcol, saop_idxs))
		{
			foreach(lc, stream_cols)
			{
				info = lfirst_node(YbMergeScanStreamColInfo, lc);
				if (info->indexcol == indexcol)
					result = lappend(result, info);
			}
			continue;
		}
		if (bms_is_member(indexcol, sort_idxs))
			continue;

		clause = ybEqualityIndexClause(index, indexclauses, indexcol);
		if (!clause && indexcol >= index->nhashcolumns)
			continue;

		info = makeNode(YbMergeScanStreamColInfo);
		info->clause = clause;
		info->indexcol = indexcol;
		info->num_elems = 1;
		info->derived = false;
		result = lappend(result, info);
	}
	return result;
}
