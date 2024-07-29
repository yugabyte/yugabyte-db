/*--------------------------------------------------------------------------------------------------
 *
 * yb_uniqkeys.c
 *	  YugaByteDB distinct pushdown API
 *
 * Copyright (c) YugaByteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/backend/optimizer/path/yb_uniqkeys.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/stratnum.h"
#include "access/sysattr.h"
#include "nodes/nodes.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/paths.h"
#include "optimizer/tlist.h"
#include "optimizer/var.h"
#include "utils/lsyscache.h"

/*
 * YB: yb_reject_distinct_pushdown_walker
 *
 * Determine if the expression 'node' is compatible with distinct pushdown.
 * Distinct can seep past most simple operations, this function simply aims
 * to limit the applicability of distinct pushdown to well known operations
 * and then slowly extend to other operations as when required.
 * Distinct operation also does not distribute into volatile functions. This is
 * captured later on in yb_reject_distinct_pushdown.
 *
 * Returns false if 'node' is compatible with distinct pushdown.
 */
static bool
yb_reject_distinct_pushdown_walker(Node *node)
{
	if (node == NULL)
		return false;

	switch(nodeTag(node))
	{
		case T_Var:
		case T_Const:
		case T_CaseTestExpr:
			return false;
		case T_Param:
		case T_SQLValueFunction:
		case T_CoerceToDomainValue:
		case T_SetToDefault:
		case T_CurrentOfExpr:
		case T_NextValueExpr:
		case T_RangeTblRef:
		case T_SortGroupClause:
		case T_Aggref:
		case T_GroupingFunc:
		case T_WindowFunc:
		case T_WindowClause:
		case T_MinMaxExpr:
		case T_ArrayExpr:
		case T_ArrayCoerceExpr:
		case T_Query:
		case T_PlaceHolderVar:
			return true;
		default:
			break;
	}

	return expression_tree_walker(node, yb_reject_distinct_pushdown_walker,
								  NULL);
}

/*
 * YB: yb_reject_distinct_pushdown
 *
 * Determine if the expression 'expr' is incompatible with distinct pushdown.
 */
bool
yb_reject_distinct_pushdown(Node *expr)
{
	return yb_reject_distinct_pushdown_walker(expr) ||
		   contain_volatile_functions(expr);
}

/*
 * YB: yb_pull_varattnos_for_distinct_pushdown
 *
 * Extract Var references from the given expression 'node'.
 *
 * Say, we have a table with range columns r1, r2.
 * For a DISTINCT query with WHERE clause such as r1 * r2 = constant,
 * we wish to generate a distinct index scan that includes both r1 and r2.
 * Such paths are useful because the scan with DISTINCT r1, r2 has atleast
 * as many rows as those satisfying r1 * r2 = constant.
 *
 * However, not all expressions can do this.
 * Example: DISTINCT AVG(r1) must not fetch DISTINCT r1 since skipping
 * 			duplicate elements while computing the average is incorrect
 * 			without also fetching the exact counts of each distinct element.
 * Example: DISTINCT r1 * RANDOM() must not fetch DISTINCT r1 since
 * 			volatile functions must be run for each tuple because they have
 * 			side effects.
 *
 * Returns true iff Var references can be extracted from the 'node' expression.
 * Stores these Vars in '*colrefs'.
 */
static bool
yb_pull_varattnos_for_distinct_pushdown(Node *node, Index varno,
										Bitmapset **varattnos,
										AttrNumber min_attr)
{
	if (node == NULL)
		return false;

	/* Give up when we have volatile functions or aggregate clauses. */
	if (yb_reject_distinct_pushdown(node))
		return false;

	/*
	 * pull_varattnos_min_attr only appends the attnos.
	 * Hence, proper initialization is necessary at callsite.
	 */
	*varattnos = NULL;
	pull_varattnos_min_attr(node, varno, varattnos, min_attr);
	return true;
}

/*
 * YB: yb_get_colrefs_for_distinct_pushdown
 *
 * Determines the list of column references.
 * Column references determine the set of DISTINCT keys required from
 * the distinct index scan.
 */
static bool
yb_get_colrefs_for_distinct_pushdown(IndexOptInfo *index, List *index_clauses,
									 Bitmapset **colrefs)
{
	ListCell *lc;
	List	 *exprs;

	/*
	 * Collect all exprs that need to be DISTINCT.
	 * Includes both base relation targets and non-index predicates.
	 */

	/* Target List. */
	exprs = list_copy(index->rel->reltarget->exprs);

	/*
	 * Non-Index Predicates.
	 * Do not include index predicates since they seep past the DISTINCT
	 * operation.
	 * Example: consider an index with range columns r1, r2.
	 * The query SELECT DISTINCT r1 WHERE r2 < 10;
	 * only has r1 in the prefix since r2 < 10 is supported by the index and
	 * is done before skipping over duplicate values of r1.
	 */
	foreach(lc, index->indrestrictinfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		if (!list_member_ptr(index_clauses, rinfo))
			exprs = lappend(exprs, rinfo->clause);
	}

	/*
	 * Pull Var references from collected exprs.
	 */
	return yb_pull_varattnos_for_distinct_pushdown((Node *) exprs,
		index->rel->relid, colrefs, YBFirstLowInvalidAttributeNumber+1);
}

/*
 * YB: yb_is_const_clause_for_distinct_pushdown
 *
 * Determines whether the key at 'indexcol' is a constant as decided
 * by 'index_clauses'.
 *
 * Caveat: Do NOT use the match_clause_to_indexcol definition of a constant
 * since that definition is too permissive for our purposes.
 *
 * We wish to exploit the constantness of the index column to ignore the column
 * from the prefix. This can be done safely when we are guaranteed no more than
 * one unique element per scan.
 *
 * Caveat: Do NOT mark indexkeys as constant when the constantness is not
 * part of index conditions. We can NOT ignore index keys that are constant
 * but not pushed down beyond the DISTINCT operation to the index. This may
 * change in the future when we start supporting storage filters for DISTINCT.
 */
static bool
yb_is_const_clause_for_distinct_pushdown(IndexOptInfo *index,
										 List *index_clauses,
										 int indexcol,
				   						 Expr *indexkey)
{
	ListCell *lc;

	/* Boolean clauses. */
	if (indexcol_is_bool_constant_for_query(index, indexcol))
		return true;

	foreach(lc, index_clauses)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
		Expr		 *clause = rinfo->clause;
		Node		 *left_op,
					 *right_op;
		int			  op_strategy;

		/* Must be a binary operation. */
		if (!is_opclause(clause))
			continue;

		left_op = get_leftop(clause);
		right_op = get_rightop(clause);
		if (!left_op || !right_op)
			continue;

		op_strategy = get_op_opfamily_strategy(((OpExpr *) clause)->opno,
											   index->opfamily[indexcol]);
		if (op_strategy != BTEqualStrategyNumber)
			continue;

		/* Check whether the clause is of the form indexkey = constant. */
		if (equal(indexkey, left_op) &&
			rinfo->right_ec && EC_MUST_BE_REDUNDANT(rinfo->right_ec))
			return true;

		/* Check whether the indexkey is on the right. */
		if (equal(indexkey, right_op) &&
			rinfo->left_ec && EC_MUST_BE_REDUNDANT(rinfo->left_ec))
			return true;
	}

	return false;
}

/*
 * YB: yb_get_const_colrefs_for_distinct_pushdown
 *
 * Constant key columns are handled differently since these keys
 * need not be part of the prefix.
 *
 * Example: DISTINCT r1, r2 WHERE r2 = 1
 * Here, r2 need not be part of the prefix since there is at most one value of
 * r2 for each distinct value of r1. Moreover, since r2 = 1 is an index
 * condition, this filter is applied before skipping duplicate values of r1.
 *
 * On the other hand a query such as
 * DISTINCT r1, r2 WHERE r2 IN (1, 2)
 * cannot eliminate r2 from the prefix since there can be more than one value
 * of r2 for each distinct value of r1.
 *
 * Returns the set of 'index' key columns that are equal to a constant using
 * 'index_clauses' as the truth source.
 */
static Bitmapset *
yb_get_const_colrefs_for_distinct_pushdown(IndexOptInfo *index,
										   List *index_clauses)
{
	Bitmapset *const_colrefs;
	ListCell  *lc;
	int		   indexcol;

	const_colrefs = NULL;
	indexcol = 0;
	foreach(lc, index->indextlist)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lc);

		if (indexcol >= index->nkeycolumns)
			break;

		/*
		 * We only consider range columns since all hash columns need to be
		 * in the prefix anyway.
		 */
		if (indexcol >= index->nhashcolumns &&
			IsA(tle->expr, Var) &&
			yb_is_const_clause_for_distinct_pushdown(
				index, index_clauses, indexcol, tle->expr))
			const_colrefs = bms_add_member(const_colrefs,
				((Var *) tle->expr)->varattno -
				YBFirstLowInvalidAttributeNumber);
		indexcol++;
	}

	return const_colrefs;
}

/*
 * YB: yb_find_colref_in_index
 *
 * Returns the minimal ordinal position of the column reference 'target_colref'
 * in the 'index'. Ordinal position is the 1-based index.
 * Returns -1 if no match is found.
 */
static int
yb_find_colref_in_index(IndexOptInfo *index, int target_colref)
{
	ListCell *lc;
	int		  ordpos;

	ordpos = 0; /* Ordinal position. */
	foreach(lc, index->indextlist)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lc);
		int			 colref;

		ordpos++;

		if (!IsA(tle->expr, Var))
			continue;

		colref = ((Var *) tle->expr)->varattno -
					YBFirstLowInvalidAttributeNumber;

		/* Found a matching index column. */
		if (colref == target_colref)
		{
			return ordpos;
		}
	}

	return -1;
}

/*
 * YB: yb_get_distinct_prefixlen
 *
 * Returns the length of the minimal prefix encompassing all column references
 * that need to be distinct.
 *
 * Returns the shortest prefix that satisfies all the 'colrefs'.
 * Returns 0 when at most one row is DISTINCT.
 * Returns -1 when 'colrefs' cannot be satisfied by the 'index'.
 */
int
yb_calculate_distinct_prefixlen(IndexOptInfo *index, List *index_clauses)
{
	Bitmapset *colrefs;
	Bitmapset *const_colrefs;
	int		   prefixlen;
	int		   target_colref;

	if (!yb_get_colrefs_for_distinct_pushdown(index, index_clauses, &colrefs))
		return -1;

	const_colrefs = yb_get_const_colrefs_for_distinct_pushdown(index,
															   index_clauses);

	prefixlen = 0;
	target_colref = -1;
	while ((target_colref = bms_next_member(colrefs, target_colref)) >= 0)
	{
		/*
		 * ordpos represents the 1-based position of target_colref in the index.
		 */
		int ordpos = yb_find_colref_in_index(index, target_colref);
		if (ordpos < 0)
			return -1;

		/*
		 * Return -1 when an include column is referenced.
		 * Because include columns of an index are not sorted.
		 */
		if (ordpos > index->nkeycolumns)
			return -1;

		/*
		 * Do not include constants in the prefix since shorter prefixes are
		 * more efficient. This is only useful for trailing constants since
		 * variable, constant, variable still has a prefix length of 3.
		 * However, variable, variable, constant has a prefix length of 2.
		 */
		if (!bms_is_member(target_colref, const_colrefs) &&
			prefixlen < ordpos)
			prefixlen = ordpos;
	}

	/*
	 * Do not bother generating a distinct index path since we do not
	 * skip any rows when fetching all keys of a unique index.
	 */
	if (prefixlen == index->nkeycolumns && index->unique)
		return -1;

	return prefixlen;
}

/*
 * YB: yb_is_index_expr_referenced
 *
 * Returns true only when expr is a Var and is referenced in colrefs.
 */
static bool
yb_is_index_expr_referenced(Node *expr, Bitmapset *colrefs)
{
	if (expr == NULL)
		return false;

	if (IsA(expr, Var))
	{
		Var *var = (Var *) expr;

		return bms_is_member(var->varattno - YBFirstLowInvalidAttributeNumber,
							 colrefs);
	}

	return false;
}

/*
 * YB: yb_get_uniqkeys
 *
 * Returns target expressions from the 'index' that are distinct for the
 * distinct index scan with prefix length 'prefixlen'.
 */
List *
yb_get_uniqkeys(IndexOptInfo *index, int prefixlen)
{
	Bitmapset  *colrefs;
	ListCell   *lc;
	List	   *index_exprs = NIL;
	int			indexcol = 0;

	/*
	 * Also include target exprs that were not included in the prefix of the
	 * distinct index scan because they were determined constant.
	 */
	colrefs = NULL;
	pull_varattnos_min_attr((Node *) index->rel->reltarget->exprs,
							index->rel->relid, &colrefs,
							YBFirstLowInvalidAttributeNumber + 1);

	foreach(lc, index->indextlist)
	{
		TargetEntry *indextle = (TargetEntry *) lfirst(lc);

		/* INCLUDE columns are incapable of DISTINCT. */
		if (indexcol >= index->nkeycolumns)
			break;

		/*
		 * All items of the prefix must be part of uniqkeys.
		 * Better to include (constant) column references as well.
		 */
		if (indexcol < prefixlen ||
			yb_is_index_expr_referenced((Node *) indextle->expr, colrefs))
			index_exprs = lappend(index_exprs, indextle->expr);
		indexcol++;
	}

	bms_free(colrefs);
	return index_exprs;
}

/*
 * YB: yb_find_expr_in_uniqkeys
 *
 * Finds the position of the expression 'expr' in 'uniqkeys'.
 * Returns 0-based keycol position if found, -1 otherwise.
 */
static int
yb_find_expr_in_uniqkeys(Node *expr, List *uniqkeys)
{
	ListCell  *lc;
	int		   pos = 0;

	foreach(lc, uniqkeys)
	{
		Node *uniqkey = (Node *) lfirst(lc);

		if (exprCollation(expr) == exprCollation(uniqkey) &&
			exprType(expr) == exprType(uniqkey) &&
			equal(expr, uniqkey))
			return pos;

		pos++;
	}

	return -1;
}

/*
 * YB: yb_get_uniqkey_matches_ec
 *
 * Returns true only when 'uniqkey' matches some member of 'ec'.
 */
static bool
yb_uniqkey_matches_ec(Node *uniqkey, EquivalenceClass *ec)
{
	ListCell *lc;

	foreach(lc, ec->ec_members)
	{
		EquivalenceMember *em = (EquivalenceMember *) lfirst(lc);
		Node			  *em_expr;

		if (em->em_is_child)
			continue;

		em_expr = (Node *) em->em_expr;
		if (exprCollation(uniqkey) == exprCollation(em_expr) &&
			exprType(uniqkey) == exprType(em_expr) &&
			equal(uniqkey, em_expr))
			return true;
	}

	return false;
}

/*
 * YB: yb_uniqkey_is_const
 *
 * Returns true only when 'uniqkey' is a constant as determined by the query
 * 'root'.
 */
static bool
yb_uniqkey_is_const(PlannerInfo *root, Node *uniqkey)
{
	ListCell *lc;

	foreach(lc, root->eq_classes)
	{
		EquivalenceClass *ec = (EquivalenceClass *) lfirst(lc);

		if (!EC_MUST_BE_REDUNDANT(ec))
			continue;

		if (yb_uniqkey_matches_ec(uniqkey, ec))
			return true;
	}

	return false;
}

/*
 * YB: yb_does_uniqkey_equal_any_distinctkey
 *
 * Returns true iff the 'uniqkey' is equal to any of the keys proven to be
 * distinct. Typically, a path must be distinct on the same keys as the query.
 * However, the query also allows for additional keys to be distinct when they
 * are equal to already proven ones. See comment on yb_has_sufficient_uniqkeys.
 */
static bool
yb_does_uniqkey_equal_any_distinctkey(Node *uniqkey, List *query_ecs)
{
	ListCell *lc;

	foreach(lc, query_ecs)
	{
		EquivalenceClass *ec = (EquivalenceClass *) lfirst(lc);

		if (ec->ec_has_volatile)
			continue;

		if (yb_uniqkey_matches_ec(uniqkey, ec))
			return true;
	}

	return false;
}

/*
 * YB: yb_satisfies_distinct
 *
 * Returns true when the path 'pathnode' is DISTINCT enough to satisfy the
 * query 'root'. When true, 'pathnode' can be added directly when generating
 * distinct paths for the relation. This saves us additional computation/nodes
 * on top.
 *
 * Proving whether or not a path is DISTINCT enough is tricky.
 * We compare the query's distinctClause with path's uniqkeys for this
 * task. We use the help of examples to better explain how we prove that a
 * path is DISTINCT enough for the query.
 *
 * In the following examples, we have a table t, with columns k1, k2, k3.
 * For the purposes of the proof, it does not matter whether the columns are
 * range columns or not. We also assume here that 'uniqkeys' accurately captures
 * the set/tuple of keys that are DISTINCT in the path's output. In other words,
 * if uniqkeys = (k1, k2) then no two tuples in the output have the same value
 * of (k1, k2) pair.
 *
 * Example 1: SELECT DISTINCT k1, k2 FROM t;
 * In this simple case, assume that our path has uniqkeys (k1, k2). This path
 * is distinct enough for the query by definition of uniqkeys.
 *
 * Example 2: SELECT DISTINCT k2 FROM t;
 * We assume that the path has uniqkeys (k1, k2) in this case as well. This
 * path has more rows than necessary for the query. For example, there may be
 * only a single distinct value of k2 but many values of k1. This means we have
 * many values of (k1, k2) pairs but only a single value of k2. Additional
 * DISTINCTification is required on top of this path to satisfy the query.
 *
 * Example 3: SELECT DISTINCT k2, k1 FROM t;
 * Again, here we assume that the path has uniqkeys (k1, k2). In this case, the
 * path is distinct enough for the query. The reason is pretty intuitive but
 * let's use a more formal argument. We need to prove: (k1, k2) is distinct
 * implies that (k2, k1) is also distinct. On the contrary assume that the
 * path's output  has duplicate values (r2, r1) = (s2, s1). This implies that
 * r2 = s2 and r1 = s1. But this is a contradiction since (k1, k2) pair is
 * distinct.
 * Thus, distinctness is preserved through permutation of keys.
 *
 * XXX: We also need to prove that we collect all the distinct values of
 * k2, k1 in the output and not just prove that there are no duplicate values.
 * We skip such proofs here for brevity.
 *
 * Example 4: SeqScan(t) SELECT DISTINCT k2 FROM t WHERE k2 = 1;
 * Unlike the sort+unique node method of distinct, proving distinctness here
 * requires that we include constants as well in uniqkeys. In this example,
 * we cannot simply ignore k2 because it is equal to 1. The path's uniqkeys must
 * have k2. Otherwise, the path's output can have duplicate values of 1.
 *
 * Example 5: SELECT DISTINCT k2 FROM t WHERE k1 = 1;
 * Assume that the path has uniqkeys (k1, k2) then it is distinct enough for
 * the query. For that, we need to prove: (k1, k2) is distinct and k1 = 1
 * implies that (k2) is also distinct. On the contrary assume that there are
 * duplicate values of k2. This means there are two tuples (r2) = (s2) but this
 * implies that (1, r2) = (1, s2) and since r1 = 1, we have (r1, r2) = (s1, s2).
 * This is a contradiction since (k1, k2) is distinct.
 * Thus, excess keys equal to a constant do not affect the distinctness of the
 * the query. See Example 2 for why we need to check for excess keys.
 *
 * Example 6: SELECT DISTINCT k2 FROM t WHERE k1 = k2;
 * Assume that the path has uniqkeys (k1, k2). This path is distinct enough
 * because even though k1 is an excess key, k1 has the same values as k2.
 * Here, we need to prove: (k1, k2) is distinct and k1 = k2 implies that (k2)
 * is also distinct. On the contrary assume that there are duplicate values of
 * k2. This means there are two tuples (r2) = (s2). But this implies that
 * (r2, r2) = (s2, s2) and since r1 = r2, we have (r1, r2) = (s1, s2). This is
 * a contradiction since (k1, k2) is distinct.
 * Thus, excess columns equal to required columns do not impact the
 * distinctness of query, similar to Example 5.
 *
 * Example 7: SELECT DISTINCT k2 FROM t WHERE k1 = k2;
 * Instead, say the path only has (k1) in its uniqkeys. This path is NOT
 * distinct enough. For example, consider the tuples (1, 1), (2, 1). The path
 * outputs 1, 1 but the query only requires 1. In practice, this scenario
 * does not occur since we always include all the required columns in the
 * set of uniqkeys.
 *
 * In summary, we first check if all the distinct columns required by the query
 * are present in the path's uniqkeys. Next, we also check that the path is not
 * distinct on any excess columns.
 */
bool
yb_has_sufficient_uniqkeys(PlannerInfo *root, Path *pathnode)
{
	List	   *query_uniqkeys;
	List	   *path_uniqkeys;
	ListCell   *lc;
	Bitmapset  *matched_uniqkeys;
	List	   *query_ecs;
	int			pos;

	query_uniqkeys = get_sortgrouplist_exprs(root->parse->distinctClause,
											 root->processed_tlist);
	path_uniqkeys = pathnode->yb_path_info.yb_uniqkeys;

	/*
	 * Check that the pathnode's uniqkeys has at least as many keys as required
	 * by the query.
	 */
	if (list_length(path_uniqkeys) < list_length(query_uniqkeys))
		return false;

	matched_uniqkeys = NULL;
	foreach(lc, query_uniqkeys)
	{
		Node *query_uniqkey = (Node *) lfirst(lc);
		int  path_pos;

		path_pos = yb_find_expr_in_uniqkeys(query_uniqkey, path_uniqkeys);
		if (path_pos == -1)
			return false;
		matched_uniqkeys = bms_add_member(matched_uniqkeys, path_pos);
	}

	if (bms_num_members(matched_uniqkeys) == list_length(path_uniqkeys))
		return true;

	/*
	 * Check if pathnode is DISTINCT on more keys than requested by the query.
	 */
	query_ecs = yb_get_ecs_for_query_uniqkeys(root);
	pos = 0;
	foreach(lc, path_uniqkeys)
	{
		if (!bms_is_member(pos, matched_uniqkeys))
		{
			Node *path_uniqkey = (Node *) lfirst(lc);

			/*
			 * path_uniqkey is not required for the query but still alright if
			 * it is either a constant or equals some proven uniqkey.
			 */
			if (!yb_uniqkey_is_const(root, path_uniqkey) &&
				!yb_does_uniqkey_equal_any_distinctkey(path_uniqkey, query_ecs))
				return false;
		}

		pos++;
	}

	return true;
}
