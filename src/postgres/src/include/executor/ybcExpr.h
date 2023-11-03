/*--------------------------------------------------------------------------------------------------
 * ybcExpr.h
 *	  prototypes for ybcExpr.c
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
 * src/include/executor/ybcExpr.h
 *
 * NOTES:
 *   - For performance reasons, some expressions must be sent to YugaByte for execution.
 *   - This module constructs expression tree to be sent to YBC API.
 *   - In the future, we can move a portion of Postgres datum and engine to /yb/common such that
 *     DocDB can execute Postgres expression without constructing YBC tree.  That involves a lot
 *     more work, so we limit this work to construct a few simple expressions.
 *--------------------------------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"
#include "nodes/execnodes.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

#include "yb/yql/pggate/ybc_pggate.h"

// Construct column reference expression.
extern YBCPgExpr YBCNewColumnRef(YBCPgStatement ybc_stmt, int16_t attr_num,
								 int attr_typid, int attr_collation,
								 const YBCPgTypeAttrs *type_attrs);

// Construct constant expression using the given datatype "type_id" and value "datum".
extern YBCPgExpr YBCNewConstant(YBCPgStatement ybc_stmt, Oid type_id,
								Oid collation_id, Datum datum, bool is_null);

// Construct virtual constant expression using the given datatype "type_id" and virtual "datum".
extern YBCPgExpr YBCNewConstantVirtual(YBCPgStatement ybc_stmt, Oid type_id,
									   YBCPgDatumKind kind);
extern YBCPgExpr YBCNewTupleExpr(YBCPgStatement ybc_stmt, const YBCPgTypeAttrs *type_attrs,
								 int num_elems, YBCPgExpr *elems);

extern Expr *YbExprInstantiateParams(Expr* expr, EState *estate);
extern PushdownExprs *YbInstantiatePushdownParams(PushdownExprs *pushdown,
												  EState *estate);

extern bool YbCanPushdownExpr(Expr *pg_expr, List **params);

extern bool YbIsTransactionalExpr(Node *pg_expr);

YBCPgExpr YBCNewEvalExprCall(YBCPgStatement ybc_stmt, Expr *pg_expr);

extern YbPgExecOutParam *YbCreateExecOutParam();

extern void YbWriteExecOutParam(YbPgExecOutParam *out_param,
								const YbcPgExecOutParamValue *value);
