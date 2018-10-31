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

	switch (type_id)
	{
		case BOOLOID:
			HandleYBStatus(YBCPgNewConstantBool(ybc_stmt, DatumGetInt64(datum), is_null, &expr));
			break;
		case CHAROID:
		case NAMEOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
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
		case REGPROCOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
			break;
		case OIDOID:
			HandleYBStatus(YBCPgNewConstantInt4(ybc_stmt, DatumGetInt32(datum), is_null, &expr));
			break;
		case TIDOID:
		case XIDOID:
		case CIDOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
			break;
		case POINTOID:
		case LSEGOID:
		case PATHOID:
		case BOXOID:
		case POLYGONOID:
		case LINEOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
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
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
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
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
		case UUIDOID:
		case LSNOID:
		case TSVECTOROID:
		case GTSVECTOROID:
		case TSQUERYOID:
		case REGCONFIGOID:
		case REGDICTIONARYOID:
		case INT4RANGEOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
		case INT4ARRAYOID:
			expr = YBCNewBinaryConstant(ybc_stmt, type_id, datum, is_null);
			break;
		default:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
	}

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
