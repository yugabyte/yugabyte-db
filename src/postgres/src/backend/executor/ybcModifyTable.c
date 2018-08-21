/*--------------------------------------------------------------------------------------------------
 *
 * ybcModifyTable.c
 *        YB routines to stmt_handle ModifyTable nodes.
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
 *        src/backend/executor/ybcModifyTable.c
 *
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
#include "executor/ybcExpr.h"
#include "executor/ybcModifyTable.h"
#include "miscadmin.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

void
YBCExecuteInsert(Relation relationDesc, TupleDesc tupleDesc, HeapTuple tuple)
{
	char	   *dbname = get_database_name(MyDatabaseId);

	char	   *schemaname = get_namespace_name(relationDesc->rd_rel->relnamespace);

	char	   *tablename = NameStr(relationDesc->rd_rel->relname);

	YBCPgStatement ybc_stmt;

	PG_TRY();
	{
		YBCLogInfo("Inserting into table: %s.%s.%s", dbname, schemaname, tablename);
		HandleYBStatus(YBCPgNewInsert(ybc_pg_session, dbname, schemaname, tablename, &ybc_stmt));

		bool		is_null = false;

		for (int i = 0; i < tupleDesc->natts; i++)
		{
			Oid			type_id = tupleDesc->attrs[i]->atttypid;
			/* Attribute numbers start from 1 */
			int			attnum = i + 1;
			Datum		datum = heap_getattr(tuple, attnum, tupleDesc, &is_null);
			YBCPgExpr ybc_expr = YBCNewConstant(ybc_stmt, type_id, datum, is_null);
			HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, attnum, ybc_expr));
		}
		YBCLogInfo("Prepared Insert.");
		HandleYBStatus(YBCPgExecInsert(ybc_stmt));
		YBCLogInfo("Executed Insert");
	}
	PG_CATCH();
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
		PG_RE_THROW();
	}
	PG_END_TRY();
	HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
}
