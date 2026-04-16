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
 *	  src/backend/utils/misc/yb_index_check.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/relation.h"
#include "catalog/heap.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_operator.h"
#include "commands/defrem.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "executor/tuptable.h"
#include "executor/ybModifyTable.h"
#include "fmgr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "yb/yql/pggate/ybc_gflags.h"

#define IndRelDetail(indexrel)	\
	"index: '%s'", RelationGetRelationName(indexrel)

#define IndRowDetail(indexrel, ybbasectid_datum)	\
	"index: '%s', ybbasectid: '%s'", RelationGetRelationName(indexrel), YBDatumToString(ybbasectid_datum, BYTEAOID)

#define IndAttrDetail(indexrel, ybbasectid_datum, attnum)	\
	"index: '%s', ybbasectid: '%s', index attnum: %d", RelationGetRelationName(indexrel), YBDatumToString(ybbasectid_datum, BYTEAOID), attnum

typedef Plan *(*YbIssueDetectionPlan) (Relation baserel, Relation indexrel,
									   Datum lower_bound_ybctid,
									   bool multi_snapshot_mode);

typedef void (*YbIssueDetectionCheck) (TupleTableSlot *outslot,
									   Relation indexrel,
									   List *equality_opcodes);

int			yb_test_index_check_num_batches_per_snapshot = -1;
bool		yb_test_slowdown_index_check = false;

static void do_index_check(Oid indexoid, bool multi_snapshot_mode);
static void partitioned_index_check(Oid parentindexId,
									bool multi_snapshot_mode);

/* Detect inconsistent index rows. */
static size_t detect_inconsistent_rows(Relation baserel, Relation indexrel,
									   bool multi_snapshot_mode);
static Plan *inconsistent_row_detection_plan(Relation baserel,
											 Relation indexrel,
											 Datum lower_bound_ybctid,
											 bool multi_snapshot_mode);
static Plan *outer_indexrel_scan_plan(Relation indexrel,
									  Datum lower_bound_ybctid,
									  bool multi_snapshot_mode);
static Plan *inner_baserel_scan_plan(Relation baserel, Relation indexrel);
static void inconsistent_row_detection_check(TupleTableSlot *outslot,
											 Relation indexrel,
											 List *equality_opcodes);

/* Detect missing index rows. */
static void detect_missing_rows(Relation baserel, Relation indexrel,
								size_t actual_index_rowcount,
								bool multi_snapshot_mode);
static Plan *missing_row_detection_plan(Relation baserel, Relation indexrel,
										Datum lower_bound_ybctid,
										bool multi_snapshot_mode);
static Plan *outer_baserel_scan_plan(Relation baserel, Relation indexrel,
									 Datum lower_bound_ybctid,
									 bool multi_snapshot_mode);
static Plan *inner_indexrel_scan_plan(Relation indexrel);
static void missing_row_detection_check(TupleTableSlot *outslot,
										Relation indexrel,
										List *unsed_equality_opcodes);
static int64 get_expected_index_rowcount(Relation baserel, Relation indexrel);

static size_t detect_index_issues(Relation baserel, Relation indexrel,
								  YbIssueDetectionPlan issue_detection_plan,
								  YbIssueDetectionCheck issue_detection_check,
								  char *task_identifier,
								  bool multi_snapshot_mode);

/* Helper functions. */
static List *get_equality_opcodes(Relation indexrel);
static void init_estate(EState *estate, Relation baserel);
static void cleanup_estate(EState *estate);
static bool end_of_batch(size_t rowcount, time_t batch_start_time);
static IndexOnlyScan *make_indexonlyscan_plan(Relation indexrel,
											  List *plan_targetlist,
											  List *index_cols,
											  ScanDirection direction);
static IndexScan *make_basescan_plan(Relation baserel, Relation indexrel,
									 List *plan_targetlist, List *indextlist,
									 ScanDirection direction);
static Plan *make_bnl_plan(Plan *lefttree, Plan *righttree,
						   Var *join_clause_lhs, Var *join_clause_rhs,
						   List *plan_tlist);
static OpExpr *lower_bound_ybctid_indexqual(Expr *ybctid_expr,
											Datum lower_bound);
static ScalarArrayOpExpr *get_saop_expr(AttrNumber attnum, Expr *leftarg);
static Expr *get_index_attr_expr(Relation baserel, Relation indexrel,
								 int index_attnum, List **indexprs,
								 ListCell **next_expr);
static List *get_index_expressions(Relation indexrel);
static List *get_partial_index_predicate(Relation baserel, Relation indexrel,
										 List **partial_idx_colrefs,
										 bool *partial_idx_pushdown);

Datum
yb_index_check(PG_FUNCTION_ARGS)
{
	Oid			indexoid = PG_GETARG_OID(0);
	bool		multi_snapshot_mode = !PG_GETARG_BOOL(1);
	int			savedGUCLevel = -1;

	if (yb_test_index_check_num_batches_per_snapshot == 0)
		multi_snapshot_mode = false;

	uint64		original_read_point PG_USED_FOR_ASSERTS_ONLY =
		YBCPgGetCurrentReadPoint();

	bool		is_txn_block = IsTransactionBlock();

	if (is_txn_block)
		ereport(NOTICE,
				(errmsg("yb_index_check() is prone to 'Restart read required' "
						"erorrs when executed from within a transaction "
						"block.")));

	if (!is_txn_block)
	{
		savedGUCLevel = NewGUCNestLevel();

		(void) set_config_option("yb_read_after_commit_visibility", "relaxed",
								 PGC_USERSET, PGC_S_SESSION, GUC_ACTION_SAVE,
								 true, 0, false);
	}

	do_index_check(indexoid, multi_snapshot_mode);

	if (!is_txn_block)
		AtEOXact_GUC(false, savedGUCLevel);

	/*
	 * yb_index_check() uses multiple snapshots in multi_snapshot_mode. Verify
	 * that it restored the original read point at the end.
	 */
	Assert(original_read_point == YBCPgGetCurrentReadPoint());

	PG_RETURN_VOID();
}

static void
do_index_check(Oid indexoid, bool multi_snapshot_mode)
{
	/*
	 * Open the base and the index relation with AccessShareLock since we read
	 * both as of time.
	 */
	LOCKMODE	lockmode = AccessShareLock;
	Relation	indexrel = relation_open(indexoid, AccessShareLock);

	if (indexrel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
	{
		relation_close(indexrel, lockmode);
		return partitioned_index_check(indexoid, multi_snapshot_mode);
	}

	if (indexrel->rd_rel->relkind != RELKIND_INDEX)
		elog(ERROR, "Object is not an index");

	Assert(indexrel->rd_index);

	if (indexrel->rd_rel->relam != LSM_AM_OID)
		elog(ERROR,
			 "This operation is not supported for index with %s access method",
			 get_am_name(indexrel->rd_rel->relam));

	if (!indexrel->rd_index->indisvalid)
		elog(ERROR, "Index '%s' is marked invalid",
			 RelationGetRelationName(indexrel));

	/* YB doesn't have separate PK index, hence it is always consistent */
	if (indexrel->rd_index->indisprimary)
	{
		relation_close(indexrel, lockmode);
		return;
	}

	Relation	baserel = relation_open(indexrel->rd_index->indrelid, lockmode);

	PG_TRY();
	{
		size_t		actual_index_rowcount =
			detect_inconsistent_rows(baserel, indexrel, multi_snapshot_mode);

		detect_missing_rows(baserel, indexrel, actual_index_rowcount,
							multi_snapshot_mode);
	}
	PG_CATCH();
	{
		if (baserel->rd_index)
			yb_free_dummy_baserel_index(baserel);
		PG_RE_THROW();
	}
	PG_END_TRY();

	relation_close(indexrel, lockmode);
	relation_close(baserel, lockmode);
}

static void
partitioned_index_check(Oid parentindexId, bool multi_snapshot_mode)
{
	ListCell   *lc;

	foreach(lc, find_inheritance_children(parentindexId, AccessShareLock))
	{
		Oid			childindexId = ObjectIdGetDatum(lfirst_oid(lc));

		do_index_check(childindexId, multi_snapshot_mode);
	}
}

static size_t
detect_inconsistent_rows(Relation baserel, Relation indexrel,
						 bool multi_snapshot_mode)
{
	return detect_index_issues(baserel, indexrel,
							   inconsistent_row_detection_plan,
							   inconsistent_row_detection_check,
							   "detect_inconsistent_rows", multi_snapshot_mode);
}

/*
 * To check for spurious/inconsistent rows in the index, we perform a LEFT join
 * on the index relation (outer subplan) and the base relation (inner subplan)
 * on indexrel.ybbasectid == baserel.ybctid. Batched nested loop join is used
 * for optimal performance.
 */
static Plan *
inconsistent_row_detection_plan(Relation baserel, Relation indexrel,
								Datum lower_bound_ybctid,
								bool multi_snapshot_mode)
{
	/*
	 * Outer subplan: index relation scan
	 * Targetlist: ybbasectid, index_attributes, ybuniqueidxkeysuffix (if
	 * unique), index row ybctid.
	 */
	Plan	   *indexrel_scan = outer_indexrel_scan_plan(indexrel, lower_bound_ybctid,
														 multi_snapshot_mode);
	TupleDesc	indexrel_scan_desc = ExecTypeFromTL(indexrel_scan->targetlist);

	/*
	 * Inner subplan: base relation scan
	 * Targetlist: ybctid, index_attributes scanned/computed from baserel.
	 */
	Plan	   *baserel_scan = inner_baserel_scan_plan(baserel, indexrel);

	/*
	 * Join plan targetlist:
	 * - outer.ybbasectid, inner.ybctid
	 * - index attributes from the outer and the inner subplan such that
	 *   semantically same attributes are next to each other: (outer.att1,
	 *   inner.att1, outer.att2, inner.att2, ... and so on)
	 * - outer.ybuniqueidxkeysuffix (if index is uniue)
	 * - outer.ybctid
	 */
	Expr	   *expr;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	List	   *plan_tlist = NIL;
	int			i;
	int			resno = 0;

	/* outer.ybbasectid, inner.ybctid, index attributes */
	for (i = 0; i < RelationGetDescr(indexrel)->natts + 1; ++i)
	{
		attr = TupleDescAttr(indexrel_scan_desc, i);
		expr = (Expr *) makeVar(OUTER_VAR, i + 1, attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, ++resno, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);

		expr = (Expr *) makeVar(INNER_VAR, i + 1, attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, ++resno, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);
	}

	/* outer.ybuniqueidxkeysuffix (if index is uniue) */
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	if (indexrel->rd_index->indisunique)
	{
		expr = (Expr *) makeVar(OUTER_VAR, ++i, attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, ++resno, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);
	}

	/* outer.ybctid */
	expr = (Expr *) makeVar(OUTER_VAR, ++i, attr->atttypid, attr->atttypmod,
							attr->attcollation, 0);
	target_entry = makeTargetEntry(expr, ++resno, "", false);
	plan_tlist = lappend(plan_tlist, target_entry);

	/* Join claue */
	Var		   *join_clause_lhs = makeVar(OUTER_VAR, 1, attr->atttypid,
										  attr->atttypmod, attr->attcollation, 0);
	Var		   *join_clause_rhs = makeVar(INNER_VAR, 1, attr->atttypid,
										  attr->atttypmod, attr->attcollation, 0);

	return make_bnl_plan(indexrel_scan, baserel_scan, join_clause_lhs,
						 join_clause_rhs, plan_tlist);
}

/*
 * Generate plan corresponding to:
 *		SELECT ybbasectid, index attributes, ybuniqueidxkeysuffix if index is
 *		unique, index row ybctid from indexrel
 * In multi_snapshot_mode, also add the qual index row ybctid > lower_bound_ybctid.
 */
static Plan *
outer_indexrel_scan_plan(Relation indexrel, Datum lower_bound_ybctid,
						 bool multi_snapshot_mode)
{
	Expr	   *expr;
	List	   *index_cols = NIL;
	List	   *plan_targetlist = NIL;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	int			resno = 0;

	/* ybbasectid */
	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	expr = (Expr *) makeVar(INDEX_VAR, YBIdxBaseTupleIdAttributeNumber,
							attr->atttypid, attr->atttypmod, attr->attcollation,
							0);
	target_entry = makeTargetEntry(expr, ++resno, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);

	/* index attributes */
	TupleDesc	indexdesc = RelationGetDescr(indexrel);

	for (int i = 0; i < indexdesc->natts; ++i)
	{
		attr = TupleDescAttr(indexdesc, i);
		expr = (Expr *) makeVar(INDEX_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, ++resno, "", false);
		index_cols = lappend(index_cols, target_entry);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	/* ybuniqueidxkeysuffix (if index is unique) */
	if (indexrel->rd_index->indisunique)
	{
		attr = SystemAttributeDefinition(YBUniqueIdxKeySuffixAttributeNumber);
		expr = (Expr *) makeVar(INDEX_VAR, YBUniqueIdxKeySuffixAttributeNumber,
								attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, ++resno, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	/* ybctid (used in multi_snapshot_mode to set lower bound of next batch) */
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Expr	   *ybctid_from_index =
		(Expr *) makeVar(INDEX_VAR, YBTupleIdAttributeNumber, attr->atttypid,
						 attr->atttypmod, attr->attcollation, 0);

	target_entry = makeTargetEntry(ybctid_from_index, ++resno, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);

	/*
	 * Execute the index scan in forward direction in multi_snapshot_mode.
	 * This ensures the index rows are ordered by ybctid.
	 */
	ScanDirection direction = multi_snapshot_mode ? ForwardScanDirection :
		NoMovementScanDirection;

	IndexOnlyScan *scan_plan = make_indexonlyscan_plan(indexrel,
													   plan_targetlist,
													   index_cols, direction);

	if (lower_bound_ybctid)
	{
		Assert(multi_snapshot_mode);
		OpExpr	   *indexqual =
			lower_bound_ybctid_indexqual(ybctid_from_index, lower_bound_ybctid);

		scan_plan->indexqual = list_make1(indexqual);
	}
	return (Plan *) scan_plan;
}

/*
 * Generate plan corresponding to:
 *		SELECT ybctid, index attributes from baserel where ybctid IN (....)
 *		AND <partial index predicate, if any>
 * This makes the inner subplan of BNL. So this must be an IndexScan, but this
 * scans the base relation. To achive this, index scan is done an a dummy index
 * on the ybctid column. Under the hood, it works as an IndexOnlyScan on the
 * base relation. This is similair to how PK index scan works in YB.
 */
static Plan *
inner_baserel_scan_plan(Relation baserel, Relation indexrel)
{
	const FormData_pg_attribute *attr;
	List	   *plan_targetlist = NIL;
	int			resno = 0;

	/* Plan target list */

	/* ybctid */
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var		   *ybctid_expr = makeVar(1, YBTupleIdAttributeNumber, attr->atttypid,
									  attr->atttypmod, attr->attcollation, 0);
	TargetEntry *target_entry =
		makeTargetEntry((Expr *) ybctid_expr, ++resno, "", false);

	plan_targetlist = lappend(plan_targetlist, target_entry);

	/* index attributes */
	List	   *indexprs = NIL;
	ListCell   *next_expr = NULL;
	TupleDesc	indexdesc = RelationGetDescr(indexrel);

	for (int i = 0; i < indexdesc->natts; ++i)
	{
		Expr	   *expr = get_index_attr_expr(baserel, indexrel, i, &indexprs,
											   &next_expr);

		/* Assert that type of index attribute match base relation attribute. */
		Assert(exprType((Node *) expr) ==
			   TupleDescAttr(indexdesc, i)->atttypid);
		target_entry = makeTargetEntry(expr, ++resno, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	/* Index target list */
	Var		   *ybctid_from_index = (Var *) copyObject(ybctid_expr);

	ybctid_from_index->varno = INDEX_VAR;
	ybctid_from_index->varattno = 1;
	target_entry = makeTargetEntry((Expr *) ybctid_from_index, 1, "", false);
	List	   *indextlist = list_make1(target_entry);

	/* Index qual */
	ScalarArrayOpExpr *saop = get_saop_expr(YBIdxBaseTupleIdAttributeNumber,
											(Expr *) ybctid_from_index);

	/* Scan plan */
	IndexScan  *base_scan = make_basescan_plan(baserel, indexrel,
											   plan_targetlist, indextlist,
											   NoMovementScanDirection);

	base_scan->indexqual = list_make1(saop);
	return (Plan *) base_scan;
}

static void
inconsistent_row_detection_check(TupleTableSlot *outslot, Relation indexrel,
								 List *equality_opcodes)
{
	bool		indisunique = indexrel->rd_index->indisunique;
	bool		indnullsnotdistinct = indexrel->rd_index->indnullsnotdistinct;
	bool		indkeyhasnull = false;
	int			indnatts = indexrel->rd_index->indnatts;
	int			indnkeyatts = indexrel->rd_index->indnkeyatts;

	/*
	 * Assert that the slot (resulting from join) has expected number of
	 * attributes. The slot should have:
	 * - indexrel.ybbasectid and baserel.ybctid: (2)
	 * - two attributes for each index attribute - one from the index relation
	 *   and the other from the base relation: (2 * indnatts)
	 * - indexrel.ybuniqueidxkeysuffix if the index is
	 *   unique: (indisunique ? 1 : 0)
	 * - indexrel.ybctid (used in multi_snapshot_mode to set the lower bound of
	 * 	 the next batch): 1
	 */
	Assert(outslot->tts_tupleDescriptor->natts ==
		   2 * (indnatts + 1) + (indisunique ? 1 : 0) + 1);

	/* First, validate ybctid and ybbasectid. */
	int			attnum = 0;

	bool		ind_null;
	bool		base_null;
	Form_pg_attribute ind_att =
		TupleDescAttr(outslot->tts_tupleDescriptor, attnum);
	Datum		ybbasectid_datum = slot_getattr(outslot, ++attnum, &ind_null);
	Datum		ybctid_datum PG_USED_FOR_ASSERTS_ONLY =
		slot_getattr(outslot, ++attnum, &base_null);

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
	/* Assert the join condiition. */
	Assert(datum_image_eq(ybbasectid_datum, ybctid_datum, ind_att->attbyval,
						  ind_att->attlen));

	/* Validate the index attributes */
	for (int i = 0; i < indnatts; ++i)
	{
		ind_att = TupleDescAttr(outslot->tts_tupleDescriptor, attnum);

		Datum		ind_datum = slot_getattr(outslot, ++attnum, &ind_null);
		Datum		base_datum = slot_getattr(outslot, ++attnum, &base_null);

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
		ind_att = TupleDescAttr(outslot->tts_tupleDescriptor, attnum);
		Datum		ybuniqueidxkeysuffix_datum =
			slot_getattr(outslot, ++attnum, &ind_null);

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
			bool		equal = datum_image_eq(ybbasectid_datum,
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

static void
detect_missing_rows(Relation baserel, Relation indexrel,
					size_t actual_index_rowcount, bool multi_snapshot_mode)
{
	if (multi_snapshot_mode)
		detect_index_issues(baserel, indexrel, missing_row_detection_plan,
							missing_row_detection_check, "detect_missing_rows",
							multi_snapshot_mode);
	else
	{
		size_t		expected_index_rowcount =
			get_expected_index_rowcount(baserel, indexrel);

		/* We already verified that index doesn't contain spurious rows. */
		Assert(expected_index_rowcount >= actual_index_rowcount);
		if (actual_index_rowcount != expected_index_rowcount)
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					 errmsg("index is missing some rows: expected %ld, actual "
							"%ld",
							expected_index_rowcount, actual_index_rowcount),
					 errdetail(IndRelDetail(indexrel))));
	}
}

/*
 * To check for missing rows in the index, we perform a LEFT join on
 * the base relation (outer subplan) and the index relation (inner subplan)
 * using BNL.
 *
 * Join condition: baserel.computed_indexrow_ybctid == indexrel.ybctid.
 *
 * Procedure yb_compute_row_ybctid() is used to get the
 * computed_indexrow_ybctid.
 */
static Plan *
missing_row_detection_plan(Relation baserel, Relation indexrel,
						   Datum lower_bound_ybctid, bool multi_snapshot_mode)
{
	/*
	 * Outer subplan: base relation scan
	 * Targetlist: computed_indexrow_ybctid, ybctid
	 */
	Plan	   *baserel_scan = outer_baserel_scan_plan(baserel, indexrel,
													   lower_bound_ybctid,
													   multi_snapshot_mode);

	/*
	 * Inner subplan: index relation scan
	 * Targetlist: index row ybctid (not to be confused with ybbasectid)
	 */
	Plan	   *indexrel_scan = inner_indexrel_scan_plan(indexrel);

	/*
	 * Join plan targetlist: baserel.computed_indexrow_ybctid, indexrel.ybctid,
	 * baserel.ybctid.
	 */
	int			resno = 0;
	const FormData_pg_attribute *attr;

	/* baserel.computed_indexrow_ybctid */
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Expr	   *expr = (Expr *) makeVar(OUTER_VAR, 1, attr->atttypid, attr->atttypmod,
										attr->attcollation, 0);
	TargetEntry *target_entry = makeTargetEntry(expr, ++resno, "", false);
	List	   *plan_tlist = list_make1(target_entry);

	/* indexrel.ybctid */
	expr = (Expr *) makeVar(INNER_VAR, 1, attr->atttypid, attr->atttypmod,
							attr->attcollation, 0);
	target_entry = makeTargetEntry(expr, ++resno, "", false);
	plan_tlist = lappend(plan_tlist, target_entry);

	/* baserel.ybctid */
	expr = (Expr *) makeVar(OUTER_VAR, 2, attr->atttypid, attr->atttypmod,
							attr->attcollation, 0);
	target_entry = makeTargetEntry(expr, ++resno, "", false);
	plan_tlist = lappend(plan_tlist, target_entry);

	/* Join claue */
	Var		   *join_clause_lhs = makeVar(OUTER_VAR, 1, attr->atttypid,
										  attr->atttypmod, attr->attcollation, 0);
	Var		   *join_clause_rhs = makeVar(INNER_VAR, 1, attr->atttypid,
										  attr->atttypmod, attr->attcollation, 0);

	return make_bnl_plan(baserel_scan, indexrel_scan, join_clause_lhs,
						 join_clause_rhs, plan_tlist);
}

/*
 * Generate plan corresponding to:
 *		SELECT yb_compute_row_ybctid(indexreloid, keyatts, ybctid) AS
 *      computed_index_row_ybctid, ybctid from baserel where <partial index
 *      predicate, if any>.
 * In multi_snapshot_mode, also add the qual ybctid > lower_bound_ybctid.
 */
static Plan *
outer_baserel_scan_plan(Relation baserel, Relation indexrel,
						Datum lower_bound_ybctid, bool multi_snapshot_mode)
{
	List	   *plan_targetlist = NIL;
	const FormData_pg_attribute *attr;
	TargetEntry *target_entry;
	int			resno = 0;

	/* Plan targetlist */

	/*
	 * yb_compute_row_ybctid(indexreloid, keyatts, ybctid) AS
	 * computed_index_row_ybctid.
	 */
	List	   *keyatts = NIL;
	List	   *indexprs = NIL;
	ListCell   *next_expr = NULL;

	for (int i = 0; i < indexrel->rd_index->indnkeyatts; ++i)
	{
		Expr	   *expr =
			get_index_attr_expr(baserel, indexrel, i, &indexprs, &next_expr);

		keyatts = lappend(keyatts, expr);
	}

	RowExpr    *keyattsexpr = makeNode(RowExpr);

	keyattsexpr->args = keyatts;
	keyattsexpr->row_typeid = RECORDOID;

	Const	   *indexoidarg = makeConst(OIDOID, 0, InvalidOid, sizeof(Oid),
										(Datum) indexrel->rd_rel->oid, false, true);

	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var		   *ybctid_expr = makeVar(1, YBTupleIdAttributeNumber, attr->atttypid,
									  attr->atttypmod, attr->attcollation, 0);

	List	   *args = list_make3(indexoidarg, keyattsexpr, ybctid_expr);
	FuncExpr   *funcexpr = makeFuncExpr(F_YB_COMPUTE_ROW_YBCTID, BYTEAOID, args,
										InvalidOid, InvalidOid,
										COERCE_EXPLICIT_CALL);

	target_entry = makeTargetEntry((Expr *) funcexpr, ++resno,
								   "computed_indexrow_ybctid", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);

	/* ybctid (used in multi_snapshot_mode to set lower bound of next batch) */
	target_entry = makeTargetEntry((Expr *) ybctid_expr, ++resno, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);

	/* Index target list */
	Var		   *ybctid_from_index = (Var *) copyObject(ybctid_expr);

	ybctid_from_index->varno = INDEX_VAR;
	ybctid_from_index->varattno = 1;
	target_entry = makeTargetEntry((Expr *) ybctid_from_index, 1, "", false);
	List	   *indextlist = list_make1(target_entry);

	/*
	 * Execute the index scan in forward direction in multi_snapshot_mode.
	 * This ensures the index rows are ordered by ybctid.
	 */
	ScanDirection direction = multi_snapshot_mode ? ForwardScanDirection :
		NoMovementScanDirection;

	/* Scan plan */
	IndexScan  *scan_plan = make_basescan_plan(baserel, indexrel,
											   plan_targetlist, indextlist,
											   direction);

	if (lower_bound_ybctid)
	{
		Assert(multi_snapshot_mode);
		OpExpr	   *indexqual = lower_bound_ybctid_indexqual((Expr *) ybctid_from_index,
															 lower_bound_ybctid);

		scan_plan->indexqual = list_make1(indexqual);
	}
	return (Plan *) scan_plan;
}

/*
 * Generate plan corresponding to:
 *		SELECT index row ybctid from indexrel where index row ybctid IN (....)
 */
static Plan *
inner_indexrel_scan_plan(Relation indexrel)
{
	TupleDesc	indexdesc = RelationGetDescr(indexrel);
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;

	/* Plan target list */
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Expr	   *ybctid_expr = (Expr *) makeVar(INDEX_VAR, YBTupleIdAttributeNumber,
											   attr->atttypid, attr->atttypmod,
											   attr->attcollation, 0);

	target_entry = makeTargetEntry(ybctid_expr, 1, "", false);
	List	   *plan_targetlist = list_make1(target_entry);

	/* Index columns */
	List	   *index_cols = NIL;

	for (int j = 0; j < indexdesc->natts; ++j)
	{
		attr = TupleDescAttr(indexdesc, j);
		Expr	   *expr = (Expr *) makeVar(INDEX_VAR, j + 1, attr->atttypid,
											attr->atttypmod, attr->attcollation, 0);

		target_entry = makeTargetEntry(expr, j + 1, "", false);
		index_cols = lappend(index_cols, target_entry);
	}

	/* Index qual */
	ScalarArrayOpExpr *saop =
		get_saop_expr(YBTupleIdAttributeNumber, ybctid_expr);

	IndexOnlyScan *scan_plan = make_indexonlyscan_plan(indexrel,
													   plan_targetlist,
													   index_cols,
													   NoMovementScanDirection);

	scan_plan->indexqual = list_make1(saop);
	return (Plan *) scan_plan;
}

static void
missing_row_detection_check(TupleTableSlot *outslot, Relation indexrel,
							List *unsed_equality_opcodes)
{
	Assert(!TTS_EMPTY(outslot));
	bool		ind_null;
	bool		base_null;

	/*
	 * outslot attributes: baserel.computed_indexrow_ybctid, indexrel.ybctid,
	 * baserel.ybctid.
	 */
	Datum		computed_indexrow_ybctid PG_USED_FOR_ASSERTS_ONLY =
		slot_getattr(outslot, 1, &base_null);
	Datum		indexrow_ybctid PG_USED_FOR_ASSERTS_ONLY =
		slot_getattr(outslot, 2, &ind_null);
	const FormData_pg_attribute *ind_att PG_USED_FOR_ASSERTS_ONLY =
		SystemAttributeDefinition(YBTupleIdAttributeNumber);

	Assert(!base_null);
	if (ind_null)
	{
		Datum		ybctid = slot_getattr(outslot, 3, &base_null);

		Assert(!base_null);
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				 errmsg("index '%s' is missing row corresponding to ybctid "
						"'%s'", RelationGetRelationName(indexrel),
						YBDatumToString(ybctid, BYTEAOID))));
	}

	/*
	 * TODO: datumIsEqual() returns false due to header size mismatch for types
	 * with variable length. For instance, in the following case, ybbasectid is
	 * VARATT_IS_1B, whereas ybctid VARATT_IS_4B. Look into it.
	 */
	/* This should always pass because this was the join condition. */
	Assert(datum_image_eq(indexrow_ybctid, computed_indexrow_ybctid,
						  ind_att->attbyval, ind_att->attlen));
}

static int64
get_expected_index_rowcount(Relation baserel, Relation indexrel)
{
	StringInfoData querybuf;

	initStringInfo(&querybuf);
	appendStringInfo(&querybuf, "/*+SeqScan(%s)*/ SELECT count(*) from %s",
					 RelationGetRelationName(baserel),
					 RelationGetRelationName(baserel));

	bool		indpred_isnull;
	Datum		indpred_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
												Anum_pg_index_indpred,
												&indpred_isnull);

	if (!indpred_isnull)
	{
		Oid			basereloid = RelationGetRelid(baserel);
		char	   *indpred_clause =
			TextDatumGetCString(DirectFunctionCall2(pg_get_expr, indpred_datum,
													basereloid));

		appendStringInfo(&querybuf, " WHERE %s", indpred_clause);
	}

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	if (SPI_execute(querybuf.data, true, 0) != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed:");

	Assert(SPI_processed == 1);
	Assert(SPI_tuptable->tupdesc->natts == 1);

	bool		isnull;
	Datum		val =
		heap_getattr(SPI_tuptable->vals[0], 1, SPI_tuptable->tupdesc, &isnull);

	Assert(!isnull);
	int64		expected_rowcount = DatumGetInt64(val);

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");

	pfree(querybuf.data);
	return expected_rowcount;
}

/*
 * Common driver function that fetches and executes the plan and runs the output
 * through the check function. Returns the number of rows processed.
 */
static size_t
detect_index_issues(Relation baserel, Relation indexrel,
					YbIssueDetectionPlan issue_detection_plan,
					YbIssueDetectionCheck issue_detection_check, char *task_id,
					bool multi_snapshot_mode)
{
	Datum		lower_bound_ybctid = 0;
	bool		execution_complete = false;
	size_t		rowcount = 0;
	int			batchcount = 0;
	TupleTableSlot *outslot;
	time_t		batch_start_time;

	List	   *equality_opcodes = get_equality_opcodes(indexrel);

	while (!execution_complete)
	{
		bool		batch_complete = false;

		EState	   *estate = CreateExecutorState();
		MemoryContext oldctxt = MemoryContextSwitchTo(estate->es_query_cxt);

		init_estate(estate, baserel);

		Plan	   *plan = issue_detection_plan(baserel, indexrel, lower_bound_ybctid,
												multi_snapshot_mode);
		PlanState  *planstate = ExecInitNode((Plan *) plan, estate, 0);

		if (multi_snapshot_mode)
		{
			PushActiveSnapshot(GetLatestSnapshot());
			time(&batch_start_time);
		}

		while (!batch_complete && (outslot = ExecProcNode(planstate)))
		{
			issue_detection_check(outslot, indexrel, equality_opcodes);

			if (multi_snapshot_mode)
			{
				/* Update the  lower_bound_ybctid. */
				bool		null;
				Datum		ybctid = slot_getattr(outslot,
												  outslot->tts_tupleDescriptor->natts,
												  &null);

				if (null)
					elog(ERROR, "ybctid is unexpectedly null");

				if (!lower_bound_ybctid ||
					DirectFunctionCall2Coll(byteagt, DEFAULT_COLLATION_OID,
											ybctid, lower_bound_ybctid))
				{
					if (lower_bound_ybctid)
						pfree(DatumGetPointer(lower_bound_ybctid));

					/* lower_bound_ybctid needs to outlive the query context. */
					MemoryContextSwitchTo(oldctxt);
					COPY_YBCTID(ybctid, lower_bound_ybctid);
					MemoryContextSwitchTo(estate->es_query_cxt);
				}
			}

			if (yb_test_slowdown_index_check)
			{
				ereport(NOTICE,
						(errmsg("artificially slowing down yb_index_check(). It should be only used during tests.")));
				sleep(1);
			}

			++rowcount;
			if (multi_snapshot_mode)
				batch_complete = end_of_batch(rowcount, batch_start_time);
		}

		if (multi_snapshot_mode)
			PopActiveSnapshot();

		execution_complete = !outslot;
		ExecEndNode(planstate);
		MemoryContextSwitchTo(oldctxt);
		cleanup_estate(estate);
		++batchcount;
	}

	pfree(equality_opcodes);
	if (lower_bound_ybctid)
		pfree(DatumGetPointer(lower_bound_ybctid));

	elog(DEBUG1,
		 "%s processed %ld rows in %d batche(s) in "
		 "%s-snapshot-mode",
		 task_id, rowcount, batchcount,
		 multi_snapshot_mode ? "multi" : "single");
	return rowcount;
}

static List *
get_equality_opcodes(Relation indexrel)
{
	List	   *equality_opcodes = NIL;

	TupleDesc	indexdesc = RelationGetDescr(indexrel);

	for (int i = 0; i < indexdesc->natts; ++i)
	{
		const FormData_pg_attribute *attr = TupleDescAttr(indexdesc, i);
		Oid			operator_oid = OpernameGetOprid(list_make1(makeString("=")),
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

static void
init_estate(EState *estate, Relation baserel)
{
	estate->yb_exec_params.yb_index_check = true;

	RangeTblEntry *rte = makeNode(RangeTblEntry);

	rte->rtekind = RTE_RELATION;
	rte->relid = RelationGetRelid(baserel);
	rte->relkind = RELKIND_RELATION;
	ExecInitRangeTable(estate, list_make1(rte));

	estate->es_param_exec_vals =
		(ParamExecData *) palloc0(yb_bnl_batch_size * sizeof(ParamExecData));
}

static void
cleanup_estate(EState *estate)
{
	/*
	 * destroy the executor's tuple table.  Actually we only care about
	 * tupdesc refcounts; there's no need to pfree the TupleTableSlots, since
	 * the containing memory context is about to go away anyway.
	 */
	ExecResetTupleTable(estate->es_tupleTable, false /* shouldFree */ );
	ExecCloseResultRelations(estate);
	ExecCloseRangeTableRelations(estate);
	FreeExecutorState(estate);
}

static bool
end_of_batch(size_t rowcount, time_t batch_start_time)
{
	/*
	 * To ensure all the rows are processed, index batch should not end in the
	 * middle of a BNL batch.
	 */
	if (rowcount % yb_bnl_batch_size > 0)
		return false;

	/*
	 * For testing purposes, if yb_test_index_check_num_batches_per_snapshot
	 * > 0, use batch size = yb_bnl_batch_size *
	 * yb_test_index_check_num_batches_per_snapshot.
	 */
	if (yb_test_index_check_num_batches_per_snapshot > 0)
	{
		size_t		batch_size =
			yb_test_index_check_num_batches_per_snapshot * yb_bnl_batch_size;

		return rowcount % batch_size == 0;
	}

	time_t		current_time;

	time(&current_time);
	time_t		elapsed_time = difftime(current_time, batch_start_time);

	/*
	 * End the current batch if elapsed time > 70% of the
	 * timestamp_history_retention_interval_sec. This threshold of 70% is
	 * based on heuristic. The idea is to keep it closer to 100% so that as
	 * many rows as possible are processed within a single batch -- to avoid
	 * the overhead of creating too many batches. At the same time, keeping
	 * it too close to 100% risks running into Snapshot too old error in
	 * scenarios when elapsed time is marginally less than the threshold,
	 * and hence next batch of rows are processed using the same snapshot but
	 * that pushes the elapsed time beyond the
	 * timestamp_history_retention_interval_sec.
	 */
	return elapsed_time >
		(0.7 * *YBCGetGFlags()->timestamp_history_retention_interval_sec);
}

static IndexOnlyScan *
make_indexonlyscan_plan(Relation indexrel, List *plan_targetlist,
						List *index_cols, ScanDirection direction)
{
	IndexOnlyScan *index_only_scan = makeNode(IndexOnlyScan);
	Plan	   *plan = &index_only_scan->scan.plan;

	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	index_only_scan->scan.scanrelid = 1;	/* only one relation is involved */
	index_only_scan->indexid = RelationGetRelid(indexrel);
	index_only_scan->indextlist = index_cols;
	index_only_scan->indexorderdir = direction;
	return index_only_scan;
}

static IndexScan *
make_basescan_plan(Relation baserel, Relation indexrel, List *plan_targetlist,
				   List *indextlist, ScanDirection direction)
{
	/* Partial index predicate */
	List	   *partial_idx_colrefs = NIL;
	bool		partial_idx_pushdown = false;
	List	   *partial_idx_pred = get_partial_index_predicate(baserel, indexrel,
															   &partial_idx_colrefs,
															   &partial_idx_pushdown);

	/*
	 * TODO: TidScan, once supported, can be used here instead. With that,
	 * yb_dummy_baserel_index_open() and yb_free_dummy_baserel_index() will
	 * not be required.
	 */
	IndexScan  *base_scan = makeNode(IndexScan);
	Plan	   *plan = &base_scan->scan.plan;

	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	plan->qual = !partial_idx_pushdown ? partial_idx_pred : NIL;
	base_scan->scan.scanrelid = 1;	/* only one relation is involved */
	base_scan->indexid = RelationGetRelid(baserel);
	base_scan->indextlist = indextlist;
	base_scan->yb_rel_pushdown.quals = partial_idx_pushdown ? partial_idx_pred :
		NIL;
	base_scan->yb_rel_pushdown.colrefs =
		partial_idx_pushdown ? partial_idx_colrefs : NIL;
	base_scan->indexorderdir = direction;
	return base_scan;
}

static Plan *
make_bnl_plan(Plan *lefttree, Plan *righttree, Var *join_clause_lhs,
			  Var *join_clause_rhs, List *plan_tlist)
{
	OpExpr	   *join_clause = (OpExpr *) make_opclause(ByteaEqualOperator, BOOLOID,
													   false,	/* opretset */
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
	Plan	   *plan = &join_plan->nl.join.plan;

	plan->targetlist = plan_tlist;
	plan->lefttree = (Plan *) lefttree;
	plan->righttree = (Plan *) righttree;
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

static OpExpr *
lower_bound_ybctid_indexqual(Expr *ybctid_expr, Datum lower_bound)
{
	const FormData_pg_attribute *attr;

	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Const	   *lower_bound_expr =
		makeConst(attr->atttypid, attr->atttypmod, attr->attcollation,
				  attr->attlen, lower_bound, lower_bound == 0, attr->attbyval);

	/* OID 1959 corresponds to '>' operator on bytea type. */
	OpExpr	   *op = (OpExpr *) make_opclause(1959, BOOLOID, false, ybctid_expr,
											  (Expr *) lower_bound_expr, InvalidOid,
											  InvalidOid);

	op->opfuncid = get_opcode(1959);
	return op;
}

static ScalarArrayOpExpr *
get_saop_expr(AttrNumber attnum, Expr *leftarg)
{
	Bitmapset  *params_bms = NULL;
	List	   *params = NIL;
	const FormData_pg_attribute *attr;

	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	for (int i = 0; i < yb_bnl_batch_size; ++i)
	{
		params_bms = bms_add_member(params_bms, i);
		Param	   *param = makeNode(Param);

		param->paramkind = PARAM_EXEC;
		param->paramid = i;
		param->paramtype = attr->atttypid;
		param->paramtypmod = attr->atttypmod;
		param->paramcollid = attr->attcollation;
		param->location = -1;
		params = lappend(params, param);
	}
	ArrayExpr  *arrexpr = makeNode(ArrayExpr);

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
	saop->args = list_make2(leftarg, arrexpr);
	return saop;
}

static Expr *
get_index_attr_expr(Relation baserel, Relation indexrel, int index_attnum,
					List **indexprs, ListCell **next_expr)
{
	Expr	   *expr;
	const FormData_pg_attribute *attr;
	AttrNumber	baserel_attnum = indexrel->rd_index->indkey.values[index_attnum];

	if (baserel_attnum > 0)
	{
		/* regular index attribute */
		TupleDesc	base_desc = RelationGetDescr(baserel);

		attr = TupleDescAttr(base_desc, baserel_attnum - 1);
		expr = (Expr *) makeVar(1, baserel_attnum, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
	}
	else
	{
		/* expression index attribute */
		if (*next_expr == NULL)
		{
			/* Fetch expressions in the index */
			Assert(*indexprs == NIL);
			*indexprs = get_index_expressions(indexrel);
			*next_expr = list_head(*indexprs);
		}
		expr = (Expr *) lfirst(*next_expr);
		*next_expr = lnext(*indexprs, *next_expr);
	}
	return expr;
}

static List *
get_index_expressions(Relation indexrel)
{
	bool		isnull;
	List	   *indexprs = NIL;
	Datum		exprs_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
											  Anum_pg_index_indexprs, &isnull);

	if (!isnull)
		indexprs = (List *) stringToNode(TextDatumGetCString(exprs_datum));
	return indexprs;
}

static List *
get_partial_index_predicate(Relation baserel, Relation indexrel,
							List **partial_idx_colrefs,
							bool *partial_idx_pushdown)
{
	List	   *partial_idx_pred = NIL;
	bool		indpred_isnull;
	Datum		indpred_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
												Anum_pg_index_indpred,
												&indpred_isnull);

	if (!indpred_isnull)
	{
		Expr	   *indpred = stringToNode(TextDatumGetCString(indpred_datum));

		*partial_idx_pushdown =
			YbCanPushdownExpr(indpred, partial_idx_colrefs, baserel->rd_id);
		partial_idx_pred = list_make1(indpred);
	}
	return partial_idx_pred;
}

/*
 * Given a relation and the key attributes of a row, returns the row ybctid.
 * Arguments: relation oid, values of key attributes, ybidxbasectid (required
 * only if the relation is an index).
 */
Datum
yb_compute_row_ybctid(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);
	Relation	rel = RelationIdGetRelation(relid);
	TupleTableSlot *slot = MakeTupleTableSlot(RelationGetDescr(rel),
											  &TTSOpsVirtual);
	Form_pg_index index = rel->rd_index;

	ExecStoreHeapTupleDatum(PG_GETARG_DATUM(1), slot);

	if (index)
	{
		bool		has_null = PG_GETARG_HEAPTUPLEHEADER(1)->t_infomask & HEAP_HASNULL;
		bool		indisunique = index->indisunique;
		Datum		ybidxbasectid = PG_GETARG_DATUM(2);

		if (!DatumGetPointer(ybidxbasectid))
			elog(ERROR, "ybidxbasectid cannot be NULL for index relations");
		if (!indisunique)
			slot->tts_ybidxbasectid = ybidxbasectid;
		else if (!index->indnullsnotdistinct && has_null)
			slot->tts_ybuniqueidxkeysuffix = ybidxbasectid;
	}

	Datum		result = YBCComputeYBTupleIdFromSlot(rel, slot);

	ExecDropSingleTupleTableSlot(slot);
	RelationClose(rel);
	return result;
}
