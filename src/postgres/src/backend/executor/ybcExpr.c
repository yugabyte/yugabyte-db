/*--------------------------------------------------------------------------------------------------
 * ybcExpr.c
 *        Routines to construct YBC expression tree.
 *
 * Copyright (c) YugaByte, Inc.
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
 * IDENTIFICATION
 *        src/backend/executor/ybcExpr.c
 *--------------------------------------------------------------------------------------------------
 */

#include <inttypes.h>

#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "commands/dbcommands.h"
#include "executor/tuptable.h"
#include "miscadmin.h"
#include "utils/builtins.h"

#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"
#include "commands/ybctype.h"

static YBCPgExpr
YBCNewBinaryConstant(
		YBCPgStatement ybc_stmt,
		Oid type_id,
		Datum datum,
		bool is_null) {
	char* data = NULL;
	int64_t size = 0;
	if (!is_null) {
		data = VARDATA_ANY(datum);
		size = VARSIZE_ANY_EXHDR(datum);
		const char* data_as_str = YBCFormatBytesAsStr(data, size);
		pfree((void*) data_as_str);

	}
	YBCPgExpr expr = NULL;
	HandleYBStatus(YBCPgNewConstantChar(ybc_stmt, data, size, is_null, &expr));
	return expr;
}

YBCPgExpr YBCNewConstant(YBCPgStatement ybc_stmt, Oid type_id, Datum datum, bool is_null) {
	YBCPgExpr expr = NULL;

	if (YBCIsPgBinarySerializedType(type_id)) {
		return YBCNewBinaryConstant(ybc_stmt, type_id, datum, is_null);
	}

#define REPORT_INVALID_TYPE_AND_BREAK() \
	YB_REPORT_TYPE_NOT_SUPPORTED(type_id); break

	switch (type_id)
	{
		case BOOLOID:
			HandleYBStatus(YBCPgNewConstantBool(ybc_stmt, DatumGetInt64(datum), is_null, &expr));
			break;
		case INT8OID:
			HandleYBStatus(YBCPgNewConstantInt8(ybc_stmt, DatumGetInt64(datum), is_null, &expr));
			break;
		case INT2OID:
			HandleYBStatus(YBCPgNewConstantInt2(ybc_stmt, DatumGetInt16(datum), is_null, &expr));
			break;
		case INT4OID:
			HandleYBStatus(YBCPgNewConstantInt4(ybc_stmt, DatumGetInt32(datum), is_null, &expr));
			break;
		case OIDOID:
			HandleYBStatus(YBCPgNewConstantInt4(ybc_stmt, DatumGetInt32(datum), is_null, &expr));
			break;
		case FLOAT4OID:
			HandleYBStatus(YBCPgNewConstantFloat4(ybc_stmt, DatumGetFloat4(datum), is_null, &expr));
			break;
		case FLOAT8OID:
			HandleYBStatus(YBCPgNewConstantFloat8(ybc_stmt, DatumGetFloat8(datum), is_null, &expr));
			break;
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
			HandleYBStatus(YBCPgNewConstantInt8(ybc_stmt, DatumGetInt64(datum), is_null, &expr));
			break;
		case INT4ARRAYOID:
			expr = YBCNewBinaryConstant(ybc_stmt, type_id, datum, is_null);
			break;
		case REGPROCOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CHAROID: REPORT_INVALID_TYPE_AND_BREAK();
		case NAMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case XIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case POINTOID: REPORT_INVALID_TYPE_AND_BREAK();
		case LSEGOID: REPORT_INVALID_TYPE_AND_BREAK();
		case PATHOID: REPORT_INVALID_TYPE_AND_BREAK();
		case BOXOID: REPORT_INVALID_TYPE_AND_BREAK();
		case POLYGONOID: REPORT_INVALID_TYPE_AND_BREAK();
		case LINEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case ABSTIMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case RELTIMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TINTERVALOID: REPORT_INVALID_TYPE_AND_BREAK();
		case UNKNOWNOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CIRCLEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CASHOID: REPORT_INVALID_TYPE_AND_BREAK();
		case INETOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CIDROID: REPORT_INVALID_TYPE_AND_BREAK();
		case DATEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TIMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case INTERVALOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TIMETZOID: REPORT_INVALID_TYPE_AND_BREAK();
		case VARBITOID: REPORT_INVALID_TYPE_AND_BREAK();
		case NUMERICOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REFCURSOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGPROCEDUREOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGOPEROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGOPERATOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGCLASSOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGTYPEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGROLEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGNAMESPACEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case UUIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case LSNOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TSVECTOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case GTSVECTOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case TSQUERYOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGCONFIGOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGDICTIONARYOID: REPORT_INVALID_TYPE_AND_BREAK();
		case INT4RANGEOID: REPORT_INVALID_TYPE_AND_BREAK();
		default: REPORT_INVALID_TYPE_AND_BREAK();
	}
#undef REPORT_INVALID_TYPE_AND_BREAK

	if (expr == NULL) {
		YBC_LOG_FATAL("Trying to return NULL from %s", __PRETTY_FUNCTION__);
	}
	// Return the constructed expression.
	return expr;
}

YBCPgExpr YBCNewColumnRef(YBCPgStatement ybc_stmt, int16_t attr_num) {
	YBCPgExpr expr = NULL;
	HandleYBStatus(YBCPgNewColumnRef(ybc_stmt, attr_num, &expr));
	return expr;
}
