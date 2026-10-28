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
 * Whether the btree equality of the type holds only for values with identical
 * key encodings, so the key encoded from a compared value is exactly the key
 * of every matching row.  It doesn't for float4/float8 (-0 = 0), numeric
 * (1.0 = 1.00), interval ('1 day' = '24 hours') or bpchar (trailing spaces),
 * so routing on them could skip the tablet holding a matching row.  Text and
 * varchar qualify because only quals under the C collation are pushed down.
 */
static bool
keyEqualityIsExact(Oid type)
{
	switch (type)
	{
		case BOOLOID:
		case CHAROID:
		case INT2OID:
		case INT4OID:
		case INT8OID:
		case OIDOID:
		case TEXTOID:
		case VARCHAROID:
		case BYTEAOID:
		case UUIDOID:
		case DATEOID:
		case TIMEOID:
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
			return true;
		default:
			return false;
	}
}

static bool
isIntegerType(Oid type)
{
	return type == INT2OID || type == INT4OID || type == INT8OID;
}

/*
 * Converts value, of integer type value_type, to integer type column_type.
 * Returns false when it is out of the column type's range: no row can match
 * it then.
 */
static bool
convertIntegerValue(Datum value, Oid value_type, Oid column_type, Datum *result)
{
	int64		v;

	switch (value_type)
	{
		case INT2OID:
			v = DatumGetInt16(value);
			break;
		case INT4OID:
			v = DatumGetInt32(value);
			break;
		default:
			v = DatumGetInt64(value);
			break;
	}
	switch (column_type)
	{
		case INT2OID:
			if (v < PG_INT16_MIN || v > PG_INT16_MAX)
				return false;
			*result = Int16GetDatum((int16) v);
			return true;
		case INT4OID:
			if (v < PG_INT32_MIN || v > PG_INT32_MAX)
				return false;
			*result = Int32GetDatum((int32) v);
			return true;
		default:
			*result = Int64GetDatum(v);
			return true;
	}
}

/*
 * Whether opno is an equality operator of the default btree operator family
 * of the column's type that routing can use, as primary key lookups do:
 * - an equality of a single type, which can differ from the column type for
 *   binary-compatible types (e.g. varchar uses text's operator);
 * - an integer cross-type equality such as int8 = int4 for "bigint_col = 3".
 *   The integer types share one family whose cross-type equality is exact,
 *   so the value can be converted to the column type.
 * Sets *value_type to the operator's input type on the value's side, and
 * *convert to whether the value has to be converted to the column type.
 */
static bool
isKeyEqualityOperator(Oid opno, Oid column_type, bool var_on_right,
					  Oid *value_type, bool *convert)
{
	Oid			opclass = GetDefaultOpClass(column_type, BTREE_AM_OID);
	Oid			left_type;
	Oid			right_type;
	Oid			column_side_type;

	if (!OidIsValid(opclass) ||
		get_op_opfamily_strategy(opno, get_opclass_family(opclass)) !=
		BTEqualStrategyNumber)
		return false;
	op_input_types(opno, &left_type, &right_type);
	column_side_type = var_on_right ? right_type : left_type;
	*value_type = var_on_right ? left_type : right_type;
	*convert = column_side_type != *value_type;
	return !*convert ||
		(column_side_type == column_type && isIntegerType(column_type) &&
		 isIntegerType(*value_type));
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
	bool		var_on_right = false;

	if (IsA(qual, OpExpr) && list_length(((OpExpr *) qual)->args) == 2)
	{
		OpExpr	   *op = (OpExpr *) qual;

		opno = op->opno;
		inputcollid = op->inputcollid;
		lhs = stripRelabel(linitial(op->args));
		rhs = stripRelabel(lsecond(op->args));
		if (IsA(rhs, Var) && IsA(lhs, Const))
		{
			Expr	   *tmp = lhs;

			lhs = rhs;
			rhs = tmp;
			var_on_right = true;
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
	Oid			value_type;
	bool		convert;

	/*
	 * Quals under a non-C collation aren't pushed down, so only the C
	 * collation can reach here; skip anything else rather than encode a
	 * collation sort key.
	 */
	if (!keyEqualityIsExact(att->atttypid) ||
		inputcollid != att->attcollation ||
		YBIsCollationValidNonC(att->attcollation) ||
		!isKeyEqualityOperator(opno, att->atttypid, var_on_right, &value_type,
							   &convert))
		return InvalidAttrNumber;

	/*
	 * A value of a single-type equality can also have the column's own type,
	 * relabeled to the operator's (e.g. a varchar value compared as text).
	 */
	if (!is_array)
	{
		if (value->consttype != value_type &&
			(convert || value->consttype != att->atttypid))
			return InvalidAttrNumber;
		*values = palloc(sizeof(Datum));
		*nvalues = 1;
		(*values)[0] = value->constvalue;
		return !convert ||
			convertIntegerValue(value->constvalue, value_type, att->atttypid,
								&(*values)[0]) ?
			var->varattno : InvalidAttrNumber;
	}

	ArrayType  *array = DatumGetArrayTypeP(value->constvalue);
	Oid			elemtype = ARR_ELEMTYPE(array);
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;
	bool	   *nulls;

	if (elemtype != value_type && (convert || elemtype != att->atttypid))
		return InvalidAttrNumber;
	get_typlenbyvalalign(elemtype, &elmlen, &elmbyval, &elmalign);
	deconstruct_array(array, elemtype, elmlen, elmbyval, elmalign, values,
					  &nulls, nvalues);
	for (int i = 0; i < *nvalues; ++i)
	{
		if (nulls[i])
			return InvalidAttrNumber;
		if (convert &&
			!convertIntegerValue((*values)[i], value_type, att->atttypid,
								 &(*values)[i]))
			return InvalidAttrNumber;
	}
	return var->varattno;
}

StaticAssertDecl(sizeof(Datum) == sizeof(uint64_t),
				 "YbcPgVectorKeyColumn.datums holds Datums");

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
		int			idx = 0;

		if (attnum == InvalidAttrNumber)
			continue;

		HandleYBTableDescStatus(YBCPgGetColumnInfo(table_desc, attnum,
												   &column_info),
								table_desc);
		if (!column_info.is_key)
			continue;

		/*
		 * Matching rows satisfy every qual, so with several equalities on a
		 * column, any one of them bounds the tablets: keep the one with the
		 * fewest values.
		 */
		while (idx < ncolumns && columns[idx].attr_num != attnum)
			++idx;
		if (idx < ncolumns && columns[idx].nvalues <= nvalues)
			continue;

		columns[idx].attr_num = attnum;
		columns[idx].type_entity =
			YbDataTypeFromOidMod(attnum,
								 TupleDescAttr(tupdesc, attnum - 1)->atttypid);
		columns[idx].nvalues = nvalues;
		columns[idx].datums = (const uint64_t *) values;
		if (idx == ncolumns)
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
