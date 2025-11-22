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
#include "access/transam.h"
#include "common/int.h"
#include "optimizer/paths.h"
#include "optimizer/yb_saop_merge.h"
#include "pg_yb_utils.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

/* GUC options */
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
 * Whether this index column is able to be part of SAOP merge.  If true, find
 * the best SAOP (best meaning having the smallest cardinality), and fill
 * in/out params saop_merge_cardinality and saop_merge_saop_cols.
 */
bool
yb_indexcol_can_saop_merge(IndexOptInfo *index,
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
