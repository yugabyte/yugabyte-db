/*-------------------------------------------------------------------------
 *
 * yb_cost.c
 *	  YB cost estimation functions.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/backend/access/common/yb_cost.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>

#include "access/nbtree.h"
#include "access/yb_cost.h"
#include "access/yb_scan_plan.h"
#include "catalog/catalog.h"
#include "catalog/pg_opfamily.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/cost.h"
#include "optimizer/optimizer.h"
#include "optimizer/paths.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "postmaster/bgworker_internals.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/spccache.h"
#include "yb/yql/pggate/ybc_gflags.h"

/*
 * ybcGetForeignRelSize
 *		Obtain relation size estimates for a foreign table
 */
void
ybcGetForeignRelSize(PlannerInfo *root,
					 RelOptInfo *baserel,
					 Oid foreigntableid)
{
	if (baserel->tuples < 0)
		baserel->tuples = YBC_DEFAULT_NUM_ROWS;

	/* Set the estimate for the total number of rows (tuples) in this table. */
	if (yb_enable_base_scans_cost_model ||
		yb_enable_optimizer_statistics)
	{
		set_baserel_size_estimates(root, baserel);
	}
	else
	{
		/*
		 * Initialize the estimate for the number of rows returned by this
		 * query.  This does not yet take into account the restriction clauses,
		 * but it will be updated later by ybcIndexCostEstimate once it
		 * inspects the clauses.
		 */
		baserel->rows = baserel->tuples;
	}

	/*
	 * Test any indexes of rel for applicability also.
	 */
	check_index_predicates(root, baserel);
}

void
ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
				bool is_backwards_scan, bool is_seq_scan,
				bool is_uncovered_idx_scan,
				Cost *startup_cost, Cost *total_cost,
				Oid index_tablespace_oid)
{
	if (is_seq_scan && !enable_seqscan)
		*startup_cost = disable_cost;
	else
		*startup_cost = (yb_enable_optimizer_statistics ?
						 yb_network_fetch_cost :
						 0);

	/*
	 * Yugabyte-specific per-tuple cost considerations:
	 *   - 10x the regular CPU cost to account for network/RPC + DocDB overhead.
	 *   - backwards scan scale factor as it will need that many more fetches
	 *     to get all rows/tuples.
	 *   - uncovered index scan is more costly than index-only or seq scan because
	 *     it requires extra request to the main table.
	 */
	double		tsp_cost = 0.0;
	bool		is_valid_tsp_cost = (!is_uncovered_idx_scan &&
									 get_yb_tablespace_cost(index_tablespace_oid,
															&tsp_cost));
	Cost		yb_per_tuple_cost_factor = YB_DEFAULT_PER_TUPLE_COST;

	if (is_valid_tsp_cost && yb_per_tuple_cost_factor > tsp_cost)
		yb_per_tuple_cost_factor = tsp_cost;

	Assert(!is_valid_tsp_cost || tsp_cost != 0);
	if (is_backwards_scan)
		yb_per_tuple_cost_factor *= YBC_BACKWARDS_SCAN_COST_FACTOR;
	if (is_uncovered_idx_scan)
		yb_per_tuple_cost_factor *= YBC_UNCOVERED_INDEX_COST_FACTOR;

	Cost		cost_per_tuple = (cpu_tuple_cost * yb_per_tuple_cost_factor +
								  baserel->baserestrictcost.per_tuple);

	*startup_cost += baserel->baserestrictcost.startup;

	*total_cost = (*startup_cost + cost_per_tuple * baserel->tuples *
				   selectivity);
}

/*
 * Evaluate the selectivity for yb_hash_code qualifiers.
 * Returns 1.0 if there are no yb_hash_code comparison expressions for this
 * index.
 */
static double
ybcEvalHashSelectivity(List *hashed_rinfos)
{
	bool		greatest_set = false;
	int			greatest = 0;

	bool		lowest_set = false;
	int			lowest = USHRT_MAX;
	double		selectivity;
	ListCell   *lc;

	foreach(lc, hashed_rinfos)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
		Expr	   *clause = rinfo->clause;

		Assert(IsA(clause, OpExpr));
		OpExpr	   *op = (OpExpr *) clause;
		Node	   *other_operand = (Node *) lsecond(op->args);

		if (!IsA(other_operand, Const))
			continue;

		int			strategy;
		Oid			lefttype;
		Oid			righttype;

		get_op_opfamily_properties(((OpExpr *) clause)->opno,
								   INTEGER_LSM_FAM_OID,
								   false,
								   &strategy,
								   &lefttype,
								   &righttype);

		int			signed_val = ((Const *) other_operand)->constvalue;

		signed_val = signed_val < 0 ? 0 : signed_val;
		uint32_t	val = signed_val > USHRT_MAX ? USHRT_MAX : signed_val;

		/*
		 * The goal here is to calculate selectivity based on qualifiers.
		 *
		 * 1. yb_hash_code(hash_col) -- Single Key selectivity
		 * 2. yb_hash_code(hash_col) >= ABC and yb_hash_code(hash_col) <= XYZ
		 *    This specifically means that we return all the hash codes between
		 *    ABC and XYZ. YBCEvalHashValueSelectivity takes in ABC and XYZ as
		 *    arguments and finds the number of buckets to search to return what
		 *    is required. If it needs to search 16 buckets out of 48 buckets
		 *    then the selectivity is 0.33 which YBCEvalHashValueSelectivity
		 *    returns.
		 */
		switch (strategy)
		{
			case BTLessStrategyNumber:
				yb_switch_fallthrough();
			case BTLessEqualStrategyNumber:
				greatest_set = true;
				greatest = val > greatest ? val : greatest;
				break;
			case BTGreaterEqualStrategyNumber:
				yb_switch_fallthrough();
			case BTGreaterStrategyNumber:
				lowest_set = true;
				lowest = val < lowest ? val : lowest;
				break;
			case BTEqualStrategyNumber:
				return YBC_SINGLE_KEY_SELECTIVITY;
			default:
				break;
		}

		if (greatest == lowest && greatest_set && lowest_set)
			break;
	}

	if (!greatest_set && !lowest_set)
		return 1.0;

	greatest = greatest_set ? greatest : INT32_MAX;
	lowest = lowest_set ? lowest : INT32_MIN;

	selectivity = YBCEvalHashValueSelectivity(lowest, greatest);
#ifdef SELECTIVITY_DEBUG
	elog(DEBUG4, "yb_hash_code selectivity is %f", selectivity);
#endif
	return selectivity;
}

/*
 * Evaluate the selectivity for some qualified cols given the hash and key cols.
 */
static double
ybcIndexEvalClauseSelectivity(double reltuples,
							  Bitmapset *qualified_scan_key_cols,
							  bool is_unique_idx,
							  Bitmapset *hash_key_cols,
							  Bitmapset *key_cols)
{
	/*
	 * If there is no search condition, or not all of the hash columns have
	 * search conditions, it will be a full-table scan.
	 */
	if (bms_is_empty(qualified_scan_key_cols) ||
		!bms_is_subset(hash_key_cols, qualified_scan_key_cols))
	{
		return YBC_FULL_SCAN_SELECTIVITY;
	}

	/*
	 * Otherwise, it will be either a full key lookup or range scan
	 * on a hash key.
	 */
	if (bms_is_subset(key_cols, qualified_scan_key_cols))
	{
		/* For unique indexes full key guarantees single row. */
		if (is_unique_idx)
			return ((reltuples == 0) ?
					YBC_SINGLE_ROW_SELECTIVITY :
					(double) (1.0 / reltuples));
		else
			return YBC_SINGLE_KEY_SELECTIVITY;
	}

	return YBC_HASH_SCAN_SELECTIVITY;
}

static void
ybcAddAttributeColumn(YbScanPlan scan_plan, AttrNumber attnum)
{
	const int	idx = YBAttnumToBmsIndex(scan_plan->target_relation, attnum);

	if (bms_is_member(idx, scan_plan->key_cols))
		scan_plan->qualified_scan_key_cols =
			bms_add_member(scan_plan->qualified_scan_key_cols, idx);
}

static bool
yb_is_hashed(Expr *clause, IndexOptInfo *index)
{
	bool		is_hashed = false;
	Node	   *leftop;

	if (IsA(clause, OpExpr))
	{
		leftop = get_leftop(clause);
		if (IsA(leftop, FuncExpr))
		{
			is_hashed = (((FuncExpr *) leftop)->funcid == F_YB_HASH_CODE);
			ListCell   *ls;

			if (is_hashed)
			{
				/*
				 * YB: We aren't going to push down a yb_hash_code call
				 * if we matched the call against an expression
				 */
				foreach(ls, index->indexprs)
				{
					Node	   *indexpr = (Node *) lfirst(ls);

					if (indexpr && IsA(indexpr, RelabelType))
						indexpr = (Node *) ((RelabelType *) indexpr)->arg;
					if (equal(indexpr, leftop))
					{
						is_hashed = false;
						break;
					}
				}
			}
		}
	}
	return is_hashed;
}

/*
 * Compute index access portion of IndexScan/IndexOnlyScan node.
 *   - Table row fetch costs are added by cost_index().
 *   - When yb_enable_optimizer_statistics is false, this function also updates
 *     baserel->rows if the current index qual is more selective than the ones
 *     seen so far.  i.e.: the table cardinality is determined by the most
 *     selective index qual regardless of the access path that is eventually
 *     chosen.
 */
void
ybcIndexCostEstimate(struct PlannerInfo *root, IndexPath *path,
					 Selectivity *selectivity, Cost *startup_cost,
					 Cost *total_cost)
{
	IndexOptInfo *indexinfo = path->indexinfo;
	bool		is_primary = false;
	RelOptInfo *baserel = path->path.parent;
	ListCell   *lc;
	bool		is_backwards_scan = path->indexscandir == BackwardScanDirection;
	bool		is_unique = indexinfo->unique;
	bool		is_partial_idx = (indexinfo->indpred != NIL &&
								  indexinfo->predOK);
	Bitmapset  *const_quals = NULL;
	List	   *hashed_rinfos = NIL;
	List	   *clauses = NIL;
	double		baserel_rows_estimate;

	if (!indexinfo->hypothetical)
	{
		/* Hypothetical index cannot be primary index */
		Relation	index = RelationIdGetRelation(indexinfo->indexoid);

		is_primary = index->rd_index->indisprimary;
		RelationClose(index);
	}

	/* Primary-index scans are always covered in Yugabyte (internally) */
	bool		is_uncovered_idx_scan = (!is_primary &&
										 path->path.pathtype != T_IndexOnlyScan);

	YbScanPlanData scan_plan;

	memset(&scan_plan, 0, sizeof(scan_plan));

	if (is_primary || indexinfo->hypothetical)
	{
		RangeTblEntry *rte = planner_rt_fetch(indexinfo->rel->relid, root);

		Assert(rte->rtekind == RTE_RELATION);
		Oid			baserel_oid = rte->relid;

		scan_plan.target_relation = RelationIdGetRelation(baserel_oid);
	}
	else
	{
		scan_plan.target_relation = RelationIdGetRelation(indexinfo->indexoid);
	}

	for (int i = 0; i < indexinfo->nkeycolumns; i++)
	{
		int			bms_idx;

		if (indexinfo->hypothetical)
			bms_idx = YBAttnumToBmsIndexWithMinAttr(YBFirstLowInvalidAttributeNumber,
													i + 1);
		else
		{
			if (is_primary)
				bms_idx = YBAttnumToBmsIndex(scan_plan.target_relation, indexinfo->indexkeys[i]);
			else
				bms_idx = YBAttnumToBmsIndex(scan_plan.target_relation, i + 1);
		}

		if (i < indexinfo->nhashcolumns)
		{
			scan_plan.hash_key_cols = bms_add_member(scan_plan.hash_key_cols, bms_idx);
		}
		scan_plan.key_cols = bms_add_member(scan_plan.key_cols, bms_idx);
	}

	/* Find out the search conditions on the key columns */
	foreach(lc, path->indexclauses)
	{
		IndexClause *iclause = lfirst_node(IndexClause, lc);
		int			indexcol = iclause->indexcol;
		ListCell   *lc2;

		foreach(lc2, iclause->indexquals)
		{
			RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc2);
			AttrNumber	attnum = (is_primary ?
								  path->indexinfo->indexkeys[indexcol] :
								  (indexcol + 1));
			Expr	   *clause = rinfo->clause;
			int			bms_idx = YBAttnumToBmsIndex(scan_plan.target_relation, attnum);

			if (IsA(clause, NullTest))
			{
				const_quals = bms_add_member(const_quals, bms_idx);
				ybcAddAttributeColumn(&scan_plan, attnum);
			}
			else
			{
				OpExpr	   *op = (OpExpr *) clause;
				Oid			clause_op = op->opno;
				Node	   *other_operand = (Node *) lsecond(op->args);
				Oid			opfamily = path->indexinfo->opfamily[indexcol];

				/*
				 * If specified, skip boolean index qual to avoid the row count
				 * estimate change, a side effect introduced by the fix for
				 * https://github.com/yugabyte/yugabyte-db/issues/26266
				 * for backward compatibility.  See the function header
				 * comment and around the lines updating baserel->rows, too.
				 */
				if (OidIsValid(clause_op) &&
					(!yb_ignore_bool_cond_for_legacy_estimate ||
					 !IsBooleanOpfamily(opfamily)))
				{
					ybcAddAttributeColumn(&scan_plan, attnum);
					if (other_operand && IsA(other_operand, Const))
						const_quals = bms_add_member(const_quals, bms_idx);
				}
			}

			if (yb_is_hashed(clause, path->indexinfo))
				hashed_rinfos = lappend(hashed_rinfos, rinfo);
			else
				clauses = lappend(clauses, rinfo);
		}
	}
	if (hashed_rinfos != NIL)
	{
		*selectivity = ybcEvalHashSelectivity(hashed_rinfos);
		baserel_rows_estimate = baserel->tuples * (*selectivity);
	}
	else
	{
		if (yb_enable_optimizer_statistics)
		{
			*selectivity = clauselist_selectivity(root /* PlannerInfo */ ,
												  clauses,
												  path->indexinfo->rel->relid /* varrelid */ ,
												  JOIN_INNER,
												  NULL /* SpecialJoinInfo */ );
			baserel_rows_estimate = (baserel->tuples * (*selectivity) >= 1 ?
									 baserel->tuples * (*selectivity) :
									 1);
		}
		else
		{
			*selectivity = ybcIndexEvalClauseSelectivity(baserel->tuples,
														 scan_plan.qualified_scan_key_cols,
														 is_unique,
														 scan_plan.hash_key_cols,
														 scan_plan.key_cols);
			baserel_rows_estimate = baserel->tuples * (*selectivity);
		}
	}

	path->path.rows = baserel_rows_estimate;

	/*
	 * For partial indexes, scale down the rows to account for the predicate.
	 * Do this after setting the baserel rows since this does not apply to base rel.
	 */
	if (!yb_enable_optimizer_statistics && is_partial_idx)
		*selectivity *= YBC_PARTIAL_IDX_PRED_SELECTIVITY;

	ybcCostEstimate(baserel, *selectivity, is_backwards_scan,
					false /* is_seq_scan */ , is_uncovered_idx_scan,
					startup_cost, total_cost,
					path->indexinfo->reltablespace);

	/* Merge scan should not be possible in non-CBO mode. */
	Assert(!path->yb_index_path_info.merge_scan_saop_cols);

	if (!yb_enable_optimizer_statistics)
	{
		/*
		 * Try to evaluate the number of rows this baserel might return.
		 * We cannot rely on the join conditions here (e.g. t1.c1 = t2.c2) because
		 * they may not be applied if another join path is chosen.
		 * So only use the t1.c1 = <const_value> quals (filtered above) for this.
		 */
		double		const_qual_selectivity = ybcIndexEvalClauseSelectivity(baserel->tuples,
																		   const_quals,
																		   is_unique,
																		   scan_plan.hash_key_cols,
																		   scan_plan.key_cols);

		baserel_rows_estimate = const_qual_selectivity * baserel->tuples;

		if (baserel_rows_estimate < baserel->rows)
			baserel->rows = baserel_rows_estimate;
	}

	RelationClose(scan_plan.target_relation);
}

/*
 * True if the scan involving table and index is such that the index data is
 * sharded together with the table.
 * TODO(#25940): this logic is not completely clean, as indicated by the
 * todos below.
 */
bool
YbIsScanningEmbeddedIdx(Relation table, Relation index)
{
	YbcTableProperties yb_table_properties_table;
	bool		is_embedded;

	yb_table_properties_table = YbGetTableProperties(table);

	/*
	 * There are a few cases where embedding happens.
	 * 1. System table: all system tables and indexes are specially colocated
	 *    to the sys catalog tablet.
	 * 2. Copartitioning: some indexes may use copartitioning, which shards the
	 *    table and index together.
	 */
	is_embedded = (IsSystemRelation(table) ||
				   (index && index->rd_indam->yb_amiscopartitioned));

	/*
	 * 3. Colocation: the table and index may be colocated to the same tablet:
	 *    - If ysql_enable_colocated_tables_with_tablespaces, check that the
	 *      table and index are in the same colocation tablet using
	 *      tablegroup_oid.
	 *      - TODO(#25940): index->rd_index->indisprimary seems irrelevant and
	 *        should not be a pass condition.
	 *    - Else, simply check for colocation of the table because the index
	 *      should follow the table.
	 *      - TODO(#25940): index being NULL or pk index should not be a pass
	 *        condition.
	 *    - TODO(#25940): the gflag could be turned on/off in the lifetime of
	 *      a cluster, so it shouldn't even be involved in this logic.
	 *      Everything should be validated, likely using the tablegroup_oid
	 *      check, assuming that holds even when indexes are created when the
	 *      flag is false.
	 */
	if (*YBCGetGFlags()->ysql_enable_colocated_tables_with_tablespaces)
		is_embedded |= (yb_table_properties_table->is_colocated &&
						((index && index->rd_index->indisprimary) ||
						 (index &&
						  (YbGetTableProperties(index)->tablegroup_oid ==
						   yb_table_properties_table->tablegroup_oid))));
	else
		is_embedded |= yb_table_properties_table->is_colocated;

	return is_embedded;
}

/*****************************************************************************
 *
 * Parallel scan support
 *
 * Currently not in use, but may be salvaged
 *****************************************************************************/

/*
 * Number of rows per planned parallel range.
 */
int			yb_parallel_range_rows = 0;

/*
 * ybParallelWorkers
 *
 * Estimate how many parallel workers are needed to scan a relation having
 * specified number of rows.
 */
int
ybParallelWorkers(double numrows)
{
	/* yb_parallel_range_rows set to 0 disables parallelizm */
	if (yb_parallel_range_rows <= 0)
		return 0;

	/* Estimate number of parallel workers */
	double		result = ceil(numrows / (double) yb_parallel_range_rows) - 1;

	/*
	 * Cap it at compile time limit for sanity, later on the value will be
	 * further capped accoding to the configuration.
	 */
	return (result > MAX_PARALLEL_WORKER_LIMIT ?
			MAX_PARALLEL_WORKER_LIMIT :
			(int) result);
}
