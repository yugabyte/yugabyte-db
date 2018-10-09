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

Oid YBCExecuteInsert(Relation rel, TupleDesc tupleDesc, HeapTuple tuple)
{
	Bitmapset      *pkey         = NULL;
	char           *dbname       = get_database_name(MyDatabaseId);
	Oid            schemaoid     = rel->rd_rel->relnamespace;
	char           *schemaname   = get_namespace_name(schemaoid);
	char           *tablename    = NameStr(rel->rd_rel->relname);
	YBCPgStatement ybc_stmt      = NULL;
	YBCPgTableDesc ybc_tabledesc = NULL;

	/*
	 * Get the primary key columns 'pkey' from YugaByte. Used below to
	 * check that values for all primary key columns are given (not null)
	 */
	HandleYBStatus(YBCPgGetTableDesc(ybc_pg_session,
	                                 dbname,
	                                 tablename,
	                                 &ybc_tabledesc));

	for (AttrNumber attrNum = 1; attrNum <= rel->rd_att->natts; attrNum++)
	{
		bool is_primary = false;
		bool is_hash    = false;
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_tabledesc,
		                                           attrNum,
		                                           &is_primary,
		                                           &is_hash), ybc_tabledesc);
		if (is_primary)
		{
			pkey = bms_add_member(pkey, attrNum);
		}
	}
	HandleYBStatus(YBCPgDeleteTableDesc(ybc_tabledesc));
	ybc_tabledesc = NULL;

	/* Create the INSERT request and add the given values. */
	HandleYBStatus(YBCPgNewInsert(ybc_pg_session,
	                              dbname,
	                              schemaname,
	                              tablename,
	                              &ybc_stmt));
	bool     is_null = false;
	for (int i = 0; i < tupleDesc->natts; i++)
	{
		Oid       type_id  = tupleDesc->attrs[i]->atttypid;
		/* Attribute numbers start from 1 */
		int       attnum   = i + 1;
		Datum     datum    = heap_getattr(tuple, attnum, tupleDesc, &is_null);

		if (is_null && bms_is_member(attnum, pkey))
		{
			HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
			ereport(ERROR,
			        (errcode(ERRCODE_NOT_NULL_VIOLATION), errmsg(
					        "Missing/null value for primary key column")));
		}
		YBCPgExpr ybc_expr = YBCNewConstant(ybc_stmt, type_id, datum, is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(ybc_stmt, attnum, ybc_expr),
		                   ybc_stmt);
	}

	/* Execute the insert and clean up. */
	HandleYBStmtStatus(YBCPgExecInsert(ybc_stmt), ybc_stmt);
	HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
	ybc_stmt = NULL;

	/* YugaByte tables do not currently support Oids. */
	return InvalidOid;
}
