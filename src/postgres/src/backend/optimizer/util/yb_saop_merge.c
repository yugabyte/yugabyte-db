/*-------------------------------------------------------------------------
 *
 * yb_saop_merge.c
 *	  Utilities for SAOP merge
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
 *	  src/backend/optimizer/util/yb_saop_merge.c
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
#include "optimizer/paths.h"
#include "optimizer/yb_saop_merge.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "rewrite/rewriteHandler.h"
#include "utils/array.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

/* GUC options */
bool		yb_enable_derived_saops;
int			yb_max_saop_merge_streams;

/*
 * Whether the given clause is an eligible SAOP.  Eligibility details are in
 * the code itself.  Return whether eligible, and if so, fill out param
 * num_elems.
 */
static bool
ybIsClauseEligibleSaop(Node *clause,
					   Expr *expr,
					   Oid opfamily,
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
	 */
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
	 * 5. Check operator (part 2).
	 *
	 * This is last as it is more expensive than the other checks.
	 */
	if (get_op_opfamily_strategy(oprid, opfamily) != BTEqualStrategyNumber)
		return false;

	/*
	 * 6. Checks passed.  Collect data.
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

	int			modulus = DatumGetInt32(const_node->constvalue);

	modulus = (modulus < 0) ? -modulus : modulus;

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
 * Whether this index column is able to be part of SAOP merge.  If true, find
 * the best SAOP (best meaning having the smallest cardinality), and fill
 * in/out params saop_merge_cardinality and saop_merge_saop_cols.
 */
bool
yb_indexcol_can_saop_merge(PlannerInfo *root,
						   IndexOptInfo *index,
						   Expr *expr,
						   int indexcol,
						   int *saop_merge_cardinality,
						   List **saop_merge_saop_cols)
{
	ListCell   *lc;
	int			best_num_elems = -1;
	ScalarArrayOpExpr *best_saop;
	YbSaopMergeSaopColInfo *saop_col_info;

	/*
	 * Abort if any of the following hold:
	 * - the caller disables SAOP merge (in/out param saop_merge_saop_cols is
	 *   NULL)
	 * - the session disables SAOP merge (GUC yb_max_saop_merge_streams is 0 or
	 *   yb_enable_base_scans_cost_model is false)
	 * - SAOP merge is not supported for this relation (not a YB relation)
	 */
	if (!(saop_merge_saop_cols &&
		  yb_max_saop_merge_streams > 0 && yb_enable_base_scans_cost_model &&
		  index->rel->is_yb_relation))
		return false;

	/* If same expr already used in saop_cols, then redundant */
	foreach(lc, *saop_merge_saop_cols)
	{
		YbSaopMergeSaopColInfo *old_saop_col_info =
			lfirst_node(YbSaopMergeSaopColInfo, lc);
		Expr	   *old_lhs = linitial(old_saop_col_info->saop->args);

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
		 * If this is an eligible SAOP index clause, keep track of it if it is
		 * better than the last one seen.
		 */
		if (ybIsClauseEligibleSaop((Node *) rinfo->clause, expr,
									   index->opfamily[indexcol],
									   &num_elems) &&
			(num_elems < best_num_elems || best_num_elems == -1))
		{
			best_num_elems = num_elems;
			best_saop = (ScalarArrayOpExpr *) rinfo->clause;

			/* Optimization when the cardinality can't get better than zero. */
			if (best_num_elems == 0 || *saop_merge_cardinality == 0)
				break;
		}
	}

	bool		derived = false;

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
	 * Fill out param saop_merge_cardinality.  Abort upon hitting the
	 * cardinality limit.
	 */
	if (unlikely(pg_mul_s32_overflow(*saop_merge_cardinality, best_num_elems,
									 saop_merge_cardinality)) ||
		*saop_merge_cardinality > yb_max_saop_merge_streams)
		return false;

	/* Fill out param saop_merge_saop_cols. */
	saop_col_info = makeNode(YbSaopMergeSaopColInfo);
	saop_col_info->saop = best_saop;
	saop_col_info->indexcol = indexcol;
	saop_col_info->num_elems = best_num_elems;
	saop_col_info->derived = derived;
	*saop_merge_saop_cols = lappend(*saop_merge_saop_cols, saop_col_info);
	return true;
}

/*
 * Get sort info for given pathkeys corresponding to tlist.  In case a pathkey
 * matches multiple columns in tlist, avoid the columns that are pinned as SAOP
 * columns.
 *
 * (Parts copied from prepare_sort_from_pathkeys.)
 */
void
yb_get_sort_info_from_pathkeys(List *tlist,
							   List *pathkeys,
							   Relids relids,
							   Bitmapset *saop_col_idxs,
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
				 * YB: skip over SAOP cols, which may be part of sort ECs in
				 * case they are equal to other columns part of sort.
				 */
				if (bms_is_member(indexcol++, saop_col_idxs))
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
