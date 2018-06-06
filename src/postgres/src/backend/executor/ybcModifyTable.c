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

	YBCPgStatement stmt_handle;

	PG_TRY();
	{
		YBCLogInfo("Inserting into table: %s.%s.%s", dbname, schemaname, tablename);
		HandleYBStatus(YBCPgAllocInsert(ybc_pg_session, dbname, schemaname, tablename, &stmt_handle));

		bool		is_null = false;

		for (int i = 0; i < tupleDesc->natts; i++)
		{
			Oid			type_id = tupleDesc->attrs[i]->atttypid;
			/* Attribute numbers start from 1 */
			int			attnum = i + 1;

			Datum		datum = heap_getattr(tuple, attnum, tupleDesc, &is_null);

			if (is_null)
			{
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("Null values are not supported yet.")));
				return;
			}

			switch (type_id)
			{
				case INT2OID:
					{
						HandleYBStatus(YBCPgInsertSetColumnInt2(stmt_handle, attnum, DatumGetInt16(datum)));
						break;
					}
				case INT4OID:
					{
						HandleYBStatus(YBCPgInsertSetColumnInt4(stmt_handle, attnum, DatumGetInt32(datum)));
						break;
					}
				case INT8OID:
					{
						HandleYBStatus(YBCPgInsertSetColumnInt8(stmt_handle, attnum, DatumGetInt64(datum)));
						break;
					}
				case FLOAT4OID:
					{
						HandleYBStatus(YBCPgInsertSetColumnFloat4(stmt_handle, attnum, DatumGetFloat4(datum)));
						break;
					}
				case FLOAT8OID:
					{
						HandleYBStatus(YBCPgInsertSetColumnFloat8(stmt_handle, attnum, DatumGetFloat8(datum)));
						break;
					}
				default:
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("YB Insert, type not yet supported: %d", type_id)));
			}
		}
		YBCLogInfo("Prepared Insert.");
		HandleYBStatus(YBCPgExecInsert(stmt_handle));
		YBCLogInfo("Executed Insert");
	}
	PG_CATCH();
	{
		HandleYBStatus(YBCPgDeleteStatement(stmt_handle));
		PG_RE_THROW();
	}
	PG_END_TRY();
	HandleYBStatus(YBCPgDeleteStatement(stmt_handle));
}
