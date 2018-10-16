/*--------------------------------------------------------------------------------------------------
 * ybcExpr.c
 *        Routines to construct YBC expression tree.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http: *www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/executor/ybcExpr.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "commands/dbcommands.h"
#include "executor/tuptable.h"
#include "miscadmin.h"

#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"

#include "utils/builtins.h"

static YBCPgExpr
YBCNewTextConstant(
		YBCPgStatement ybc_stmt,
		Oid type_id,
		Datum datum,
		bool is_null) {
	/* TODO: get rid of memory allocation here (in TextDatumGetCString). */
	char* text_str = is_null ? NULL : TextDatumGetCString(datum);
	YBCPgExpr expr = NULL;
	HandleYBStatus(YBCPgNewConstantText(ybc_stmt, text_str, is_null, &expr));
	if (text_str != NULL)
	{
		pfree(text_str);
	}
	return expr;
}

YBCPgExpr YBCNewConstant(YBCPgStatement ybc_stmt, Oid type_id, Datum datum, bool is_null) {
	YBCPgExpr expr = NULL;
	switch (type_id)
	{
		case BOOLOID:
		case BYTEAOID:
		case CHAROID:
		case NAMEOID:
			ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("YB Insert, type not yet supported: %d", type_id)));
			break;
		case INT8OID:
			HandleYBStatus(YBCPgNewConstantInt8(ybc_stmt, DatumGetInt64(datum), is_null, &expr));
			break;
		case INT2OID:
			HandleYBStatus(YBCPgNewConstantInt2(ybc_stmt, DatumGetInt16(datum), is_null, &expr));
			break;
		case INT2VECTOROID:
			ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("YB Insert, type not yet supported: %d", type_id)));
			break;
		case INT4OID:
			HandleYBStatus(YBCPgNewConstantInt4(ybc_stmt, DatumGetInt32(datum), is_null, &expr));
			break;
		case REGPROCOID:
			ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("YB Insert, type not yet supported: %d", type_id)));
			break;
		case TEXTOID:
			expr = YBCNewTextConstant(ybc_stmt, type_id, datum, is_null);
			break;
		case OIDOID:
		case TIDOID:
		case XIDOID:
		case CIDOID:
		case OIDVECTOROID:
		case POINTOID:
		case LSEGOID:
		case PATHOID:
		case BOXOID:
		case POLYGONOID:
		case LINEOID:
			ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("YB Insert, type not yet supported: %d", type_id)));
			break;
		case FLOAT4OID:
			HandleYBStatus(YBCPgNewConstantFloat4(ybc_stmt, DatumGetFloat4(datum), is_null, &expr));
			break;
		case FLOAT8OID:
			HandleYBStatus(YBCPgNewConstantFloat8(ybc_stmt, DatumGetFloat8(datum), is_null, &expr));
			break;
		case ABSTIMEOID:
		case RELTIMEOID:
		case TINTERVALOID:
		case UNKNOWNOID:
		case CIRCLEOID:
		case CASHOID:
		case INETOID:
		case CIDROID:
		case BPCHAROID:
			ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("YB Insert, type not yet supported: %d", type_id)));
			break;
		case VARCHAROID:
			expr = YBCNewTextConstant(ybc_stmt, type_id, datum, is_null);
			break;
		case DATEOID:
		case TIMEOID:
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
		case INTERVALOID:
		case TIMETZOID:
		case VARBITOID:
		case NUMERICOID:
		case REFCURSOROID:
		case REGPROCEDUREOID:
		case REGOPEROID:
		case REGOPERATOROID:
		case REGCLASSOID:
		case REGTYPEOID:
		case REGROLEOID:
		case REGNAMESPACEOID:
		case REGTYPEARRAYOID:
		case UUIDOID:
		case LSNOID:
		case TSVECTOROID:
		case GTSVECTOROID:
		case TSQUERYOID:
		case REGCONFIGOID:
		case REGDICTIONARYOID:
		case JSONBOID:
		case INT4RANGEOID:
		default:
			ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("Type not yet supported in YugaByte: %d", type_id)));
			break;
	}

	// Return the constructed expression.
	return expr;
}

YBCPgExpr YBCNewColumnRef(YBCPgStatement ybc_stmt, int16_t attr_num) {
	YBCPgExpr expr = NULL;
	HandleYBStatus(YBCPgNewColumnRef(ybc_stmt, attr_num, &expr));
	return expr;
}
