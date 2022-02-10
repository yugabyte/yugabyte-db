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
#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#include "catalog/pg_proc.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "parser/parse_type.h"
#include "utils/lsyscache.h"
#include "commands/dbcommands.h"
#include "executor/tuptable.h"
#include "miscadmin.h"
#include "utils/syscache.h"
#include "utils/builtins.h"

#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"
#include "catalog/yb_type.h"

YBCPgExpr YBCNewColumnRef(YBCPgStatement ybc_stmt, int16_t attr_num,
						  int attr_typid, int attr_collation,
						  const YBCPgTypeAttrs *type_attrs) {
	YBCPgExpr expr = NULL;
	const YBCPgTypeEntity *type_entity = YbDataTypeFromOidMod(attr_num, attr_typid);
	YBCPgCollationInfo collation_info;
	YBGetCollationInfo(attr_collation, type_entity, 0 /* datum */, true /* is_null */,
					   &collation_info);
	HandleYBStatus(YBCPgNewColumnRef(ybc_stmt, attr_num, type_entity,
									 collation_info.collate_is_valid_non_c,
									 type_attrs, &expr));
	return expr;
}

YBCPgExpr YBCNewConstant(YBCPgStatement ybc_stmt, Oid type_id, Oid collation_id,
						 Datum datum, bool is_null) {
	YBCPgExpr expr = NULL;
	const YBCPgTypeEntity *type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);
	YBCPgCollationInfo collation_info;
	YBGetCollationInfo(collation_id, type_entity, datum, is_null, &collation_info);
	HandleYBStatus(YBCPgNewConstant(ybc_stmt, type_entity,
									collation_info.collate_is_valid_non_c,
									collation_info.sortkey,
									datum, is_null, &expr));
	return expr;
}

YBCPgExpr YBCNewConstantVirtual(YBCPgStatement ybc_stmt, Oid type_id, YBCPgDatumKind kind) {
	YBCPgExpr expr = NULL;
	const YBCPgTypeEntity *type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);
	HandleYBStatus(YBCPgNewConstantVirtual(ybc_stmt, type_entity, kind, &expr));
	return expr;
}

YBCPgExpr YBCNewEvalSingleParamExprCall(YBCPgStatement ybc_stmt,
                                        Expr *pg_expr,
                                        int32_t attno,
                                        int32_t typid,
                                        int32_t typmod,
                                        int32_t collid) {
	YBExprParamDesc params[1];
	params[0].attno = attno;
	params[0].typid = typid;
	params[0].typmod = typmod;
	params[0].collid = collid;
	return YBCNewEvalExprCall(ybc_stmt, pg_expr, params, 1);
}

/*
 * Assuming the first param is the target column, therefore representing both 
 * the first argument and return type.
 */
YBCPgExpr YBCNewEvalExprCall(YBCPgStatement ybc_stmt,
                             Expr *pg_expr,
                             YBExprParamDesc *params,
                             int num_params)
{
	YBCPgExpr ybc_expr = NULL;
	const YBCPgTypeEntity *type_ent = YbDataTypeFromOidMod(InvalidAttrNumber, params[0].typid);
	YBCPgCollationInfo collation_info;
	YBGetCollationInfo(params[0].collid, type_ent, 0 /* Datum */, true /* is_null */,
					   &collation_info);
	HandleYBStatus(YBCPgNewOperator(ybc_stmt, "eval_expr_call", type_ent,
					 collation_info.collate_is_valid_non_c, &ybc_expr));

	Datum expr_datum = CStringGetDatum(nodeToString(pg_expr));
	YBCPgExpr expr = YBCNewConstant(ybc_stmt, CSTRINGOID, C_COLLATION_OID,
									expr_datum , /* IsNull */ false);
	HandleYBStatus(YBCPgOperatorAppendArg(ybc_expr, expr));

	/*
	 * Adding the column type ids and mods to the message since we only have the YQL types in the
	 * DocDB Schema.
	 * TODO(mihnea): Eventually DocDB should know the full YSQL/PG types and we can remove this.
	 */
	for (int i = 0; i < num_params; i++) {
		Datum attno = Int32GetDatum(params[i].attno);
		YBCPgExpr attno_expr = YBCNewConstant(ybc_stmt, INT4OID, InvalidOid, attno, /* IsNull */ false);
		HandleYBStatus(YBCPgOperatorAppendArg(ybc_expr, attno_expr));

		Datum typid = Int32GetDatum(params[i].typid);
		YBCPgExpr typid_expr = YBCNewConstant(ybc_stmt, INT4OID, InvalidOid, typid, /* IsNull */ false);
		HandleYBStatus(YBCPgOperatorAppendArg(ybc_expr, typid_expr));
		
		Datum typmod = Int32GetDatum(params[i].typmod);
		YBCPgExpr typmod_expr = YBCNewConstant(ybc_stmt, INT4OID, InvalidOid, typmod, /* IsNull */ false);
		HandleYBStatus(YBCPgOperatorAppendArg(ybc_expr, typmod_expr));
	}
	return ybc_expr;
}

/* ------------------------------------------------------------------------- */
/*  Execution output parameter from Yugabyte */
YbPgExecOutParam *YbCreateExecOutParam()
{
	YbPgExecOutParam *param = makeNode(YbPgExecOutParam);
	param->bfoutput = makeStringInfo();

	/* Not yet used */
	param->status = makeStringInfo();
	param->status_code = 0;

	return param;
}

void YbWriteExecOutParam(YbPgExecOutParam *param, const YbcPgExecOutParamValue *value) {
	appendStringInfoString(param->bfoutput, value->bfoutput);

	/* Not yet used */
	if (value->status)
	{
		appendStringInfoString(param->status, value->status);
		param->status_code = value->status_code;
	}
}
