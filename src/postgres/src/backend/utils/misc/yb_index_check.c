/*-------------------------------------------------------------------------
 *
 * yb_index_check.c
 * Utiity to check if a YB index is consistent with its base relation.
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
 *	  src/backend/utils/misc/yb_index_check.c.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "catalog/heap.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_operator.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "executor/tuptable.h"
#include "fmgr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

static void yb_index_check_internal(Oid indexoid);

#define IndRelDetail(indexrel)	\
	"index: '%s'", RelationGetRelationName(indexrel)

#define IndRowDetail(indexrel, ybbasectid_datum)	\
	"index: '%s', ybbasectid: '%s'", RelationGetRelationName(indexrel), YBDatumToString(ybbasectid_datum, BYTEAOID)

#define IndAttrDetail(indexrel, ybbasectid_datum, attnum)	\
	"index: '%s', ybbasectid: '%s', index attnum: %d", RelationGetRelationName(indexrel), YBDatumToString(ybbasectid_datum, BYTEAOID), attnum

static void
check_index_row_consistency(TupleTableSlot *slot, List *equality_opcodes,
							Relation indexrel)
{
	bool indisunique = indexrel->rd_index->indisunique;
	bool indnullsnotdistinct = indexrel->rd_index->indnullsnotdistinct;
	bool indkeyhasnull = false;
	int indnatts = indexrel->rd_index->indnatts;
	int indnkeyatts = indexrel->rd_index->indnkeyatts;

	Assert(slot->tts_tupleDescriptor->natts ==
		   2 * (indnatts + 1) + (indisunique ? 1 : 0));

	/* First, validate ybctid and ybbasectid. */
	int ind_attnum = 1;
	int base_attnum = ind_attnum + 1;

	bool ind_null;
	bool base_null;
	Datum ybbasectid_datum = slot_getattr(slot, ind_attnum, &ind_null);
	Datum ybctid_datum = slot_getattr(slot, base_attnum, &base_null);
	Form_pg_attribute ind_att = TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);

	if (ind_null)
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				 errmsg("index has row with ybbasectid == null"),
				 errdetail(IndRelDetail(indexrel))));

	if (base_null)
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				 errmsg("index contains spurious row"),
				 errdetail(IndRowDetail(indexrel, ybbasectid_datum))));

	/*
	 * TODO: datumIsEqual() returns false due to header size mismatch for types
	 * with variable length. For instance, in the following case, ybbasectid is
	 * VARATT_IS_1B, whereas ybctid VARATT_IS_4B. Look into it.
	 */
	/* This should never happen because this was the join condition */
	if (unlikely(!datum_image_eq(ybbasectid_datum, ybctid_datum,
								 ind_att->attbyval, ind_att->attlen)))
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("indexrow ybbasectid mismatch with baserow ybctid. The "
						"issue is likely with the checker, and not with the index"),
				 errdetail(IndRowDetail(indexrel, ybbasectid_datum))));

	ind_attnum += 2;
	base_attnum += 2;
	/* Validate the index attributes */
	for (int i = 0; i < indnatts; i++, ind_attnum += 2, base_attnum += 2)
	{
		Form_pg_attribute ind_att = TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);

		Datum ind_datum = slot_getattr(slot, ind_attnum, &ind_null);
		Datum base_datum = slot_getattr(slot, base_attnum, &base_null);

		if (ind_null && i < indnkeyatts)
			indkeyhasnull = true;

		if (ind_null || base_null)
		{
			if (ind_null && base_null)
				continue;

			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					 errmsg("inconsistent index row due to NULL mismatch"),
					 errdetail(IndAttrDetail(indexrel, ybbasectid_datum, i + 1))));
		}

		if (datum_image_eq(ind_datum, base_datum, ind_att->attbyval,
						   ind_att->attlen))
			continue;

		/* Index key should be binary equal to base relation counterpart. */
		if (i < indnkeyatts)
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					 errmsg("inconsistent index row due to binary mismatch of key attribute"),
					 errdetail(IndAttrDetail(indexrel, ybbasectid_datum, i + 1))));

		RegProcedure proc_oid = lfirst_int(list_nth_cell(equality_opcodes, i));
		if (proc_oid == InvalidOid)
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					 errmsg("inconsistent index row due to binary mismatch of non-key attribute"),
					 errdetail(IndAttrDetail(indexrel, ybbasectid_datum, i + 1))));

		if (!DatumGetBool(OidFunctionCall2Coll(proc_oid, DEFAULT_COLLATION_OID,
											   ind_datum, base_datum)))
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					 errmsg("inconsistent index row due to semantic mismatch of non-key attribute"),
					 errdetail(IndAttrDetail(indexrel, ybbasectid_datum, i + 1))));
	}

	if (indisunique)
	{
		/* Validate the ybuniqueidxkeysuffix */
		Datum ybuniqueidxkeysuffix_datum = slot_getattr(slot, ind_attnum, &ind_null);

		if (indnullsnotdistinct || !indkeyhasnull)
		{
			if (!ind_null)
				ereport(ERROR,
						(errcode(ERRCODE_INDEX_CORRUPTED),
						 errmsg("ybuniqueidxkeysuffix is (unexpectedly) not null"),
						 errdetail(IndRowDetail(indexrel, ybbasectid_datum))));
		}
		else
		{
			ind_att = TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);
			bool equal = datum_image_eq(ybbasectid_datum,
										ybuniqueidxkeysuffix_datum,
										ind_att->attbyval, ind_att->attlen);
			if (!equal)
				ereport(ERROR,
						(errcode(ERRCODE_INDEX_CORRUPTED),
						 errmsg("ybuniqueidxkeysuffix and ybbasectid mismatch"),
						 errdetail(IndRowDetail(indexrel, ybbasectid_datum))));
		}
	}
}

/*
 * Generate plan corresponding to:
 *		SELECT (ybbasectid, index attributes, ybuniqueidxkeysuffix if index is
 *		unique) from indexrel
 */
static Plan *
indexrel_scan_plan(Relation indexrel)
{
	TupleDesc indexdesc = RelationGetDescr(indexrel);

	Expr *expr;
	List *index_cols = NIL;
	List *plan_targetlist = NIL;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	int resno = 1;

	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	expr = (Expr *) makeVar(INDEX_VAR, YBIdxBaseTupleIdAttributeNumber,
							attr->atttypid, attr->atttypmod, attr->attcollation,
							0);
	target_entry = makeTargetEntry((Expr *) expr, resno, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);
	resno++;

	for (int i = 0; i < indexdesc->natts; i++, resno++)
	{
		attr = TupleDescAttr(indexdesc, i);
		expr = (Expr *) makeVar(INDEX_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, resno, "", false);
		index_cols = lappend(index_cols, target_entry);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	if (indexrel->rd_index->indisunique)
	{
		attr = SystemAttributeDefinition(YBUniqueIdxKeySuffixAttributeNumber);
		expr = (Expr *) makeVar(INDEX_VAR, YBUniqueIdxKeySuffixAttributeNumber,
								attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry((Expr *) expr, resno, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	IndexOnlyScan *index_scan = makeNode(IndexOnlyScan);
	Plan *plan = &index_scan->scan.plan;
	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	index_scan->scan.scanrelid = 1; /* only one relation is involved */
	index_scan->indexid = RelationGetRelid(indexrel);
	index_scan->indextlist = index_cols;
	return (Plan *) index_scan;
}

List *
fetch_index_expressions(Relation indexrel)
{
	bool isnull;
	List *indexprs = NIL;
	Datum exprs_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
										Anum_pg_index_indexprs, &isnull);
	if (!isnull)
		indexprs = (List *) stringToNode(TextDatumGetCString(exprs_datum));
	return indexprs;
}

/*
 * Generate plan corresponding to:
 *		SELECT (ybctid, index attributes) from baserel where ybctid IN (....)
 *		 AND <partial index predicate, if any>
 * This plan makes the inner subplan of BNL. So this must be an IndexScan, but
 * at the same time this should scan the base relation. To achive this, index
 * scan is done an a dummy index that is on the ybctid column. Under the hood,
 * it works as an index only scan on the base relation. This is similair to how
 * PK index scan works in YB.
 */
static Plan *
baserel_scan_plan(Relation baserel, Relation indexrel)
{
	TupleDesc indexdesc = RelationGetDescr(indexrel);
	TupleDesc base_desc = RelationGetDescr(baserel);

	List *plan_targetlist = NIL;

	int resno = 1;
	const FormData_pg_attribute *attr =
		SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *ybctid_expr = makeVar(1, YBTupleIdAttributeNumber, attr->atttypid,
							   attr->atttypmod, attr->attcollation, 0);
	TargetEntry *target_entry =
		makeTargetEntry((Expr *) ybctid_expr, resno, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);
	resno++;

	Expr *expr;
	List *indexprs = NIL;
	ListCell *next_expr = NULL;
	for (int i = 0; i < indexdesc->natts; i++, resno++)
	{
		AttrNumber attnum = indexrel->rd_index->indkey.values[i];
		if (attnum > 0)
		{
			/* regular index attribute */
			attr = TupleDescAttr(base_desc, attnum - 1);
			expr = (Expr *) makeVar(1, attnum, attr->atttypid, attr->atttypmod,
									attr->attcollation, 0);
		}
		else
		{
			/* expression index attribute */
			if (next_expr == NULL)
			{
				/* Fetch expressions in the index */
				Assert(indexprs == NIL);
				indexprs = fetch_index_expressions(indexrel);
				next_expr = list_head(indexprs);
			}
			expr = (Expr *) lfirst(next_expr);
			next_expr = lnext(indexprs, next_expr);
		}

		/* Assert that type of index attribute match base relation attribute. */
		Assert(exprType((Node *) expr) ==
			   TupleDescAttr(indexdesc, i)->atttypid);
		target_entry = makeTargetEntry(expr, resno, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	/* Index qual */
	/* LHS */
	Var *ybctid_from_index = (Var *) copyObject(ybctid_expr);
	ybctid_from_index->varno = INDEX_VAR;
	ybctid_from_index->varattno = 1;

	/* RHS */
	Bitmapset *params_bms = NULL;
	List *params = NIL;
	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	for (int i = 0; i < yb_bnl_batch_size; i++)
	{
		params_bms = bms_add_member(params_bms, i);
		Param *param = makeNode(Param);
		param->paramkind = PARAM_EXEC;
		param->paramid = i;
		param->paramtype = attr->atttypid;
		param->paramtypmod = attr->atttypmod;
		param->paramcollid = attr->attcollation;
		param->location = -1;
		params = lappend(params, param);
	}
	ArrayExpr *arrexpr = makeNode(ArrayExpr);
	arrexpr->array_typeid = BYTEAARRAYOID;
	arrexpr->element_typeid = BYTEAOID;
	arrexpr->multidims = false;
	arrexpr->array_collid = InvalidOid;
	arrexpr->location = -1;
	arrexpr->elements = params;

	ScalarArrayOpExpr *saop = makeNode(ScalarArrayOpExpr);
	saop->opno = ByteaEqualOperator;
	saop->opfuncid = get_opcode(ByteaEqualOperator);
	saop->useOr = true;
	saop->inputcollid = InvalidOid;
	saop->args = list_make2(ybctid_from_index, arrexpr);

	target_entry = makeTargetEntry((Expr *) ybctid_from_index, 1, "", false);
	List *indextlist = list_make1(target_entry);

	/* Partial index predicate */
	List *partial_idx_pred = NIL;
	List *partial_idx_colrefs = NIL;
	bool partial_idx_pushdown = false;
	bool indpred_isnull = false;
	Datum indpred_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
										  Anum_pg_index_indpred, &indpred_isnull);
	if (!indpred_isnull)
	{
		Expr *indpred = stringToNode(TextDatumGetCString(indpred_datum));
		partial_idx_pushdown = YbCanPushdownExpr(indpred, &partial_idx_colrefs);
		partial_idx_pred = lappend(partial_idx_pred, indpred);
	}

	/*
	 * TODO: TidScan, once supported, can be used here instead. With that,
	 * yb_dummy_baserel_index_open() and yb_free_dummy_baserel_index() should
	 * not be be required.
	 */
	IndexScan *base_scan = makeNode(IndexScan);
	Plan *plan = &base_scan->scan.plan;
	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	plan->qual = !partial_idx_pushdown ? partial_idx_pred : NIL;
	plan->extParam = params_bms;
	plan->allParam = params_bms;
	base_scan->scan.scanrelid = 1; /* only one relation is involved */
	base_scan->indexid = RelationGetRelid(baserel);
	base_scan->indextlist = indextlist;
	base_scan->indexqual = list_make1(saop);
	base_scan->yb_rel_pushdown.quals = partial_idx_pushdown ? partial_idx_pred :
															  NIL;
	base_scan->yb_rel_pushdown.colrefs =
		partial_idx_pushdown ? partial_idx_colrefs : NIL;
	return (Plan *) base_scan;
}

static Plan *
spurious_check_plan(Relation baserel, Relation indexrel)
{
	/*
	 * To check for spurious rows in index relation, we join the index relation
	 * with the base relation on indexrow.ybbasectid == baserow.ybctid. We use
	 * BNL for this purpose. To satisfy the BNL's join condition requirement,
	 * index relation is used as the outer (left) subplan.
	 */

	/* Outer subplan: index relation scan */
	Plan *indexrel_scan = indexrel_scan_plan(indexrel);
	TupleDesc indexrel_scan_desc = ExecTypeFromTL(indexrel_scan->targetlist);

	/* Inner subplan: base relation scan */
	Plan *baserel_scan = baserel_scan_plan(baserel, indexrel);

	/*
	 * Join plan targetlist
	 *
	 * Outer subplan tlist: ybbasectid, index_attributes , ybuniqueidxkeysuffix
	 * (if unique).
	 * Inner subplan tlist: ybctid, index_attributes (scanned/computed from
	 * baserel).
	 *
	 * Join plan tlist: union of the above such that semantically same entries
	 * are next to each other:
	 * (outer_attr1, inner_attr1, outer_attr2, inner_att2 ..*,
	 * ybuniqueidxkeysuffix if unqiue index)
	 *
	 */
	Expr *expr;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	List *plan_tlist = NIL;
	int i;
	for (i = 0; i < indexrel_scan_desc->natts; i++)
	{
		attr = TupleDescAttr(indexrel_scan_desc, i);
		expr = (Expr *) makeVar(OUTER_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 1, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);

		/*
		 * For unique index, the last tlist entry is ybuniqueidxkeysuffix, which
		 * doesn't have base rel counterpart.
		 */
		if (indexrel->rd_index->indisunique &&
			i == indexrel_scan_desc->natts - 1)
			continue;

		expr = (Expr *) makeVar(INNER_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 2, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);
	}

	/* Join claue */
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *join_clause_lhs = makeVar(OUTER_VAR, 1, attr->atttypid,
								   attr->atttypmod, attr->attcollation, 0);
	Var *join_clause_rhs = makeVar(INNER_VAR, 1, attr->atttypid,
								   attr->atttypmod, attr->attcollation, 0);
	OpExpr *join_clause = (OpExpr *) make_opclause(ByteaEqualOperator, BOOLOID,
												   false, /* opretset */
												   (Expr *) join_clause_lhs,
												   (Expr *) join_clause_rhs,
												   InvalidOid, InvalidOid);
	join_clause->opfuncid = get_opcode(ByteaEqualOperator);

	/* NestLoopParam */
	NestLoopParam *nlp = makeNode(NestLoopParam);
	nlp->paramno = 0;
	nlp->paramval = join_clause_lhs;
	nlp->yb_batch_size = yb_bnl_batch_size;

	/* BNL join plan */
	YbBatchedNestLoop *join_plan = makeNode(YbBatchedNestLoop);
	Plan *plan = &join_plan->nl.join.plan;
	plan->targetlist = plan_tlist;
	plan->lefttree = (Plan *) indexrel_scan;
	plan->righttree = (Plan *) baserel_scan;
	join_plan->nl.join.jointype = JOIN_LEFT;
	join_plan->nl.join.inner_unique = true;
	join_plan->nl.join.joinqual = list_make1(join_clause);
	join_plan->nl.nestParams = list_make1(nlp);
	join_plan->first_batch_factor = 1.0;
	join_plan->num_hashClauseInfos = 1;
	join_plan->hashClauseInfos = palloc0(sizeof(YbBNLHashClauseInfo));
	join_plan->hashClauseInfos->hashOp = ByteaEqualOperator;
	join_plan->hashClauseInfos->innerHashAttNo = join_clause_rhs->varattno;
	join_plan->hashClauseInfos->outerParamExpr = (Expr *) join_clause_lhs;
	join_plan->hashClauseInfos->orig_expr = (Expr *) join_clause;
	return (Plan *) join_plan;
}

List *
get_equality_opcodes(Relation indexrel)
{
	List *equality_opcodes = NIL;

	TupleDesc indexdesc = RelationGetDescr(indexrel);

	for (int i = 0; i < indexdesc->natts; i++)
	{
		const FormData_pg_attribute *attr = TupleDescAttr(indexdesc, i);
		Oid operator_oid = OpernameGetOprid(list_make1(makeString("=")),
											attr->atttypid, attr->atttypid);
		if (operator_oid == InvalidOid)
		{
			equality_opcodes = lappend_int(equality_opcodes, InvalidOid);
			continue;
		}
		RegProcedure proc_oid = get_opcode(operator_oid);
		equality_opcodes = lappend_int(equality_opcodes, proc_oid);
	}
	return equality_opcodes;
}

static int64
check_spurious_index_rows(Relation baserel, Relation indexrel)
{
	Plan *join_plan = spurious_check_plan(baserel, indexrel);

	/* Plan execution */
	EState *estate = CreateExecutorState();
	MemoryContext oldctxt = MemoryContextSwitchTo(estate->es_query_cxt);

	estate->yb_exec_params.yb_index_check = true;

	RangeTblEntry *rte1 = makeNode(RangeTblEntry);
	rte1->rtekind = RTE_RELATION;
	rte1->relid = RelationGetRelid(baserel);
	rte1->relkind = RELKIND_RELATION;
	ExecInitRangeTable(estate, list_make1(rte1));

	estate->es_param_exec_vals =
		(ParamExecData *) palloc0(yb_bnl_batch_size * sizeof(ParamExecData));

	PlanState *join_state = ExecInitNode((Plan *) join_plan, estate, 0);

	List *equality_opcodes = get_equality_opcodes(indexrel);

	int64 index_rowcount = 0;
	TupleTableSlot *output;
	while ((output = ExecProcNode(join_state)))
	{
		index_rowcount++;
		check_index_row_consistency(output, equality_opcodes, indexrel);
	}
	ExecEndNode(join_state);

	ExecResetTupleTable(estate->es_tupleTable, true);
	ExecCloseResultRelations(estate);
	ExecCloseRangeTableRelations(estate);
	MemoryContextSwitchTo(oldctxt);
	FreeExecutorState(estate);

	return index_rowcount;
}

static void
partitioned_index_check(Oid parentindexId)
{
	ListCell *lc;
	foreach (lc, find_inheritance_children(parentindexId, AccessShareLock))
	{
		Oid childindexId = ObjectIdGetDatum(lfirst_oid(lc));
		/* TODO: A new read time can be used for each partition. */
		yb_index_check_internal(childindexId);
	}
}

static int64
get_expected_index_rowcount(Relation baserel, Relation indexrel)
{
	StringInfoData querybuf;
	initStringInfo(&querybuf);
	appendStringInfo(&querybuf, "/*+SeqScan(%s)*/ SELECT count(*) from %s",
					 RelationGetRelationName(baserel),
					 RelationGetRelationName(baserel));

	bool indpred_isnull;
	Datum indpred_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
										  Anum_pg_index_indpred,
										  &indpred_isnull);
	if (!indpred_isnull)
	{
		Oid basereloid = RelationGetRelid(baserel);
		char *indpred_clause = TextDatumGetCString(DirectFunctionCall2(pg_get_expr,
																	   indpred_datum,
																	   basereloid));
		appendStringInfo(&querybuf, " WHERE %s", indpred_clause);
	}

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	if (SPI_execute(querybuf.data, true, 0) != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed:");

	Assert(SPI_processed == 1);
	Assert(SPI_tuptable->tupdesc->natts == 1);

	bool isnull;
	Datum val = heap_getattr(SPI_tuptable->vals[0], 1, SPI_tuptable->tupdesc, &isnull);
	Assert(!isnull);
	int64 expected_rowcount = DatumGetInt64(val);

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");

	return expected_rowcount;
}

static void
yb_index_check_internal(Oid indexoid)
{
	Relation indexrel = RelationIdGetRelation(indexoid);

	if (indexrel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
	{
		RelationClose(indexrel);
		return partitioned_index_check(indexoid);
	}

	if (indexrel->rd_rel->relkind != RELKIND_INDEX)
		elog(ERROR, "Object is not an index");

	Assert(indexrel->rd_index);

	if (indexrel->rd_rel->relam != LSM_AM_OID)
		elog(ERROR,
			 "This operation is not supported for index with access method %d",
			 indexrel->rd_rel->relam);

	if (!indexrel->rd_index->indisvalid)
		elog(ERROR, "Index '%s' is marked invalid", RelationGetRelationName(indexrel));

	/* YB doesn't have separate PK index, hence it is always consistent */
	if (indexrel->rd_index->indisprimary)
	{
		RelationClose(indexrel);
		return;
	}

	Relation baserel = RelationIdGetRelation(indexrel->rd_index->indrelid);

	/* Check for spurious index rows */
	int64 actual_index_rowcount = 0;
	PG_TRY();
	{
		actual_index_rowcount = check_spurious_index_rows(baserel, indexrel);
	}
	PG_CATCH();
	{
		if (baserel->rd_index)
			yb_free_dummy_baserel_index(baserel);
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Now, check for missing index rows */
	int64 expected_index_rowcount = get_expected_index_rowcount(baserel, indexrel);
	/* We already verified that index doesn't contain spurious rows. */
	Assert(expected_index_rowcount >= actual_index_rowcount);
	if (actual_index_rowcount != expected_index_rowcount)
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				 errmsg("index is missing some rows: expected %ld, actual %ld",
						expected_index_rowcount, actual_index_rowcount)));

	RelationClose(indexrel);
	RelationClose(baserel);
}

Datum
yb_index_check(PG_FUNCTION_ARGS)
{
	Oid indexoid = PG_GETARG_OID(0);
	yb_index_check_internal(indexoid);
	PG_RETURN_VOID();
}
