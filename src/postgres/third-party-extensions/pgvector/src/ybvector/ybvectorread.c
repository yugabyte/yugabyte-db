/*-------------------------------------------------------------------------
 *
 * ybvectorread.c
 *	  read routines for the Yugabyte vector index access method.
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
 *		third-party-extensions/pgvector/ybvector/ybvectorread.c
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/genam.h"
#include "access/relscan.h"
#include "access/stratnum.h"
#include "access/sysattr.h"
#include "access/yb_target.h"
#include "catalog/pg_am.h"
#include "catalog/pg_type.h"
#include "catalog/yb_type.h"
#include "commands/defrem.h"
#include "nodes/nodeFuncs.h"
#include "pg_yb_utils.h"
#include "pgstat.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "ybvector.h"

/*
 * Returns the btree equality operator of the column's type that an equality
 * on a primary key column has to use for its values to be encodable into the
 * key, as done for primary key lookups.  Sets *input_type to the operator's
 * input type, which can differ from the column type for binary-compatible
 * types (e.g. varchar uses text's operator).
 */
static Oid
getKeyEqualityOperator(Oid column_type, Oid *input_type)
{
	Oid			opclass = GetDefaultOpClass(column_type, BTREE_AM_OID);

	if (!OidIsValid(opclass))
		return InvalidOid;
	*input_type = get_opclass_input_type(opclass);
	return get_opfamily_member(get_opclass_family(opclass), *input_type,
							   *input_type, BTEqualStrategyNumber);
}

static Expr *
stripRelabel(Expr *expr)
{
	while (expr && IsA(expr, RelabelType))
		expr = ((RelabelType *) expr)->arg;
	return expr;
}

/*
 * If qual is "column = const" or "column = ANY(const array)" on a column of
 * the scanned relation, returns the column's attribute number and sets
 * *values / *nvalues to the compared values.  Returns InvalidAttrNumber for
 * any other qual, including one that has a NULL value.
 */
static AttrNumber
extractKeyColumnValues(Expr *qual, TupleDesc tupdesc, Datum **values,
					   int *nvalues)
{
	Oid			opno;
	Oid			inputcollid;
	Expr	   *lhs;
	Expr	   *rhs;
	bool		is_array;

	if (IsA(qual, OpExpr) && list_length(((OpExpr *) qual)->args) == 2)
	{
		OpExpr	   *op = (OpExpr *) qual;

		opno = op->opno;
		inputcollid = op->inputcollid;
		lhs = stripRelabel(linitial(op->args));
		rhs = stripRelabel(lsecond(op->args));
		/* Equality operators of a single type are their own commutators. */
		if (IsA(rhs, Var) && IsA(lhs, Const))
		{
			Expr	   *tmp = lhs;

			lhs = rhs;
			rhs = tmp;
		}
		is_array = false;
	}
	else if (IsA(qual, ScalarArrayOpExpr) && ((ScalarArrayOpExpr *) qual)->useOr)
	{
		ScalarArrayOpExpr *op = (ScalarArrayOpExpr *) qual;

		opno = op->opno;
		inputcollid = op->inputcollid;
		lhs = stripRelabel(linitial(op->args));
		rhs = stripRelabel(lsecond(op->args));
		is_array = true;
	}
	else
		return InvalidAttrNumber;

	if (!IsA(lhs, Var) || !IsA(rhs, Const))
		return InvalidAttrNumber;

	Var		   *var = (Var *) lhs;
	Const	   *value = (Const *) rhs;

	if (var->varlevelsup != 0 || var->varattno <= 0 ||
		var->varattno > tupdesc->natts || value->constisnull)
		return InvalidAttrNumber;

	Form_pg_attribute att = TupleDescAttr(tupdesc, var->varattno - 1);
	Oid			input_type = InvalidOid;

	if (opno != getKeyEqualityOperator(att->atttypid, &input_type) ||
		inputcollid != att->attcollation)
		return InvalidAttrNumber;

	if (!is_array)
	{
		if (value->consttype != input_type && value->consttype != att->atttypid)
			return InvalidAttrNumber;
		*values = palloc(sizeof(Datum));
		(*values)[0] = value->constvalue;
		*nvalues = 1;
		return var->varattno;
	}

	ArrayType  *array = DatumGetArrayTypeP(value->constvalue);
	Oid			elemtype = ARR_ELEMTYPE(array);
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	bool	   *nulls;

	if (elemtype != input_type && elemtype != att->atttypid)
		return InvalidAttrNumber;
	get_typlenbyvalalign(elemtype, &elmlen, &elmbyval, &elmalign);
	deconstruct_array(array, elemtype, elmlen, elmbyval, elmalign, values,
					  &nulls, nvalues);
	for (int i = 0; i < *nvalues; ++i)
	{
		if (nulls[i])
			return InvalidAttrNumber;
	}
	return var->varattno;
}

/*
 * The vector index of a table is stored in the table's own tablets, so a
 * search only has to visit the tablets that can hold rows matching an
 * equality on the primary key.  Find such equalities among the quals pushed
 * down to the table and pass them to pggate, which works out the ybctid
 * prefixes the search is restricted to.  The quals stay pushed down, so they
 * are still checked for every row.
 */
static void
bindKeyFilter(IndexScanDesc scan, YbOpaque yb_scan)
{
	Relation	rel = scan->heapRelation;
	TupleDesc	tupdesc = RelationGetDescr(rel);
	YbcPgTableDesc table_desc = NULL;
	YbcPgVectorKeyColumn *columns;
	int			ncolumns = 0;
	ListCell   *lc;

	if (!scan->yb_rel_pushdown || yb_scan->prepare_params.index_only_scan)
		return;

	HandleYBStatus(YBCPgGetTableDesc(YBCGetDatabaseOid(rel),
									 YbGetRelfileNodeId(rel), &table_desc));
	columns = palloc(sizeof(YbcPgVectorKeyColumn) *
					 list_length(scan->yb_rel_pushdown->quals));

	foreach(lc, scan->yb_rel_pushdown->quals)
	{
		Datum	   *values;
		int			nvalues;
		AttrNumber	attnum = extractKeyColumnValues(lfirst(lc), tupdesc,
													&values, &nvalues);
		YbcPgColumnInfo column_info = {0};
		bool		already_bound = false;

		if (attnum == InvalidAttrNumber)
			continue;

		HandleYBTableDescStatus(YBCPgGetColumnInfo(table_desc, attnum,
												   &column_info),
								table_desc);
		if (!column_info.is_key)
			continue;

		/*
		 * Matching rows satisfy every qual, so with several equalities on a
		 * column, any one of them bounds the tablets.
		 */
		for (int i = 0; i < ncolumns; ++i)
			already_bound |= (columns[i].attr_num == attnum);
		if (already_bound)
			continue;

		YbcPgAttrValueDescriptor *attrs =
			palloc(sizeof(YbcPgAttrValueDescriptor) * Max(nvalues, 1));
		Oid			type_id = TupleDescAttr(tupdesc, attnum - 1)->atttypid;

		for (int i = 0; i < nvalues; ++i)
		{
			attrs[i].attr_num = attnum;
			attrs[i].datum = values[i];
			attrs[i].is_null = false;
			attrs[i].type_entity = YbDataTypeFromOidMod(attnum, type_id);
			attrs[i].collation_id = ybc_get_attcollation(tupdesc, attnum);
			YBSetupAttrCollationInfo(&attrs[i], &column_info);
		}
		columns[ncolumns].attr_num = attnum;
		columns[ncolumns].nvalues = nvalues;
		columns[ncolumns].values = attrs;
		++ncolumns;
	}

	if (ncolumns > 0)
		HandleYBStatus(YBCPgDmlANNBindKeyFilter(yb_scan->handle, ncolumns,
												columns));
}

/*
 * Bind search keys to the ANN scan. These include
 * - the query vector
 * - the prefetch size (how many nearest neighbours we expect to return)
 */
static void bindAnnSearchKeys(YbOpaque yb_scan, IndexScanDesc scan,
							  Relation rel, int nkeys, int norderbys,
							  YbVectorScanOpaque so)
{
	if (scan->orderByData->sk_flags & SK_ISNULL)
	{
		yb_scan->quit_scan = true;
		return;
	}

	int ind_dim = TupleDescAttr(scan->indexRelation->rd_att, 0)->atttypmod;
	int vec_dim = ((Vector*) scan->orderByData->sk_argument)->dim;
	if (ind_dim != vec_dim)
		ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					errmsg("different vector dimensions %d and %d", ind_dim, vec_dim)));

	so->query_vector = scan->orderByData->sk_argument;
	YbcPgExpr vec_handle = YBCNewConstant(
		so->yb_scan_desc->handle, BYTEAOID, InvalidOid /* collation_id */,
		so->query_vector, false);

	YBCPgDmlANNBindVector(so->yb_scan_desc->handle, vec_handle);
	YBCPgDmlANNSetPrefetchSize(so->yb_scan_desc->handle, so->limit);
}

/*
 * ybvectorbeginscan
 *		Open the scan and initialize its opaque vector scan structure.
 */
IndexScanDesc
ybvectorbeginscan(Relation rel, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	YbVectorScanOpaque so;

	scan = RelationGetIndexScan(rel, nkeys, norderbys);

	/* allocate private workspace */
	so = (YbVectorScanOpaque) palloc(sizeof(YbVectorScanOpaqueData));
	so->limit = -1;

	so->first = true;
	scan->opaque = so;
	return scan;
}

/*
 * ybvectorrescan
 *		Reset temporary structures to prepare for rescan.
 */
void
ybvectorrescan(IndexScanDesc scan, ScanKey scankeys, int nscankeys,
			   ScanKey orderbys, int norderbys)
{
	YbVectorScanOpaque so = scan->opaque;
	if (!so->first)
		ybc_free_ybscan(so->yb_scan_desc);

	YbOpaque	ybScan = YbBeginScan(scan->heapRelation,
									 scan->indexRelation,
									 scan->xs_want_itup,
									 nscankeys,
									 scankeys,
									 scan->yb_scan_plan,
									 scan->yb_rel_pushdown,
									 scan->yb_idx_pushdown,
									 scan->yb_aggrefs,
									 scan->yb_distinct_prefixlen,
									 scan->yb_exec_params,
									 false,	/* is_internal_scan */
									 scan->fetch_ybctids_only);

	/* For vector indexes, we either recheck all rows or no rows. */
	scan->xs_recheck = YbNeedsPgRecheck(ybScan);

	so->yb_scan_desc = ybScan;
	if (scan->yb_exec_params->plan_limit > 0 && scan->yb_exec_params->plan_limit <= INT_MAX)
		so->limit = scan->yb_exec_params->plan_limit;

	if (scankeys && scan->numberOfKeys > 0)
		memmove(&scan->keyData, scankeys, scan->numberOfKeys * sizeof(ScanKeyData));

	if (orderbys && scan->numberOfOrderBys > 0)
		memmove(scan->orderByData, orderbys, scan->numberOfOrderBys * sizeof(ScanKeyData));

	if (norderbys > 0)
		bindAnnSearchKeys(ybScan, scan, scan->heapRelation, nscankeys,
						  norderbys, so);

	bindKeyFilter(scan, ybScan);

	so->first = true;
}

bool
ybvectorgettuple(IndexScanDesc scan, ScanDirection dir)
{
	YbVectorScanOpaque so = (YbVectorScanOpaque) scan->opaque;
	so->first = false;
	YbOpaque ybscan = so->yb_scan_desc;
	ybscan->exec_params = scan->yb_exec_params;
	Assert(ybscan->exec_params != NULL);
	ybscan->exec_params->work_mem = work_mem;

	if (!ybscan->is_exec_done)
		pgstat_count_index_scan(scan->indexRelation);

	/* Lifted from yb_lsm.c ybcingettuple. */
	bool has_tuple = false;
	if (ybscan->prepare_params.index_only_scan)
	{
		IndexTuple tuple = ybc_getnext_indextuple(ybscan, dir);
		if (tuple)
		{
			scan->xs_itup = tuple;
			scan->xs_itupdesc = RelationGetDescr(scan->indexRelation);
			has_tuple = true;
		}
	}
	else
	{
		HeapTuple tuple = ybc_getnext_heaptuple(ybscan, dir);
		if (tuple)
		{
			scan->xs_hitup = tuple;
			scan->xs_hitupdesc = RelationGetDescr(scan->heapRelation);
			has_tuple = true;
		}
	}

	scan->xs_recheckorderby = false;

	return has_tuple;
}

/*
 * ybvectorendscan
 *		Close the scan
 */
void
ybvectorendscan(IndexScanDesc scan)
{
	YbVectorScanOpaque so = scan->opaque;

	ybc_free_ybscan(so->yb_scan_desc);
}

/* ybvectormightrecheck
 *	 Assume we'll always recheck this scan.
 *	 TODO(tanuj): Make sure this is false and correct.
 */
bool
ybvectormightrecheck(Scan *scan, Relation heapRelation, Relation indexRelation,
					 bool xs_want_itup, ScanKey keys, int nkeys)
{
	return true;
}
