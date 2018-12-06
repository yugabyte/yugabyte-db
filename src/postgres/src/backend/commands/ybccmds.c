/*--------------------------------------------------------------------------------------------------
 *
 * ybccmds.c
 *        YB commands for creating and altering table structures and settings
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
 *        src/backend/commands/ybccmds.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"
#include "catalog/pg_class.h"
#include "catalog/pg_namespace.h"
#include "commands/dbcommands.h"
#include "commands/ybccmds.h"
#include "commands/ybctype.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

/* ---------------------------------------------------------------------------------------------- */
/*  Database Functions. */

void
YBCCreateDatabase(Oid dboid, const char *dbname, Oid src_dboid, Oid next_oid)
{
	YBCPgStatement handle;

	char	   *src_dbname = get_database_name(src_dboid);

	YBC_LOG_WARNING("Ignoring source database '%s' when creating database '%s'", src_dbname,
		dbname);
	PG_TRY();
	{
		HandleYBStatus(YBCPgNewCreateDatabase(ybc_pg_session,
											  dbname,
											  dboid,
											  InvalidOid,
											  next_oid,
											  &handle));
		HandleYBStatus(YBCPgExecCreateDatabase(handle));
	}
	PG_CATCH();
	{
		HandleYBStatus(YBCPgDeleteStatement(handle));
		PG_RE_THROW();
	}
	PG_END_TRY();
	HandleYBStatus(YBCPgDeleteStatement(handle));
}

void
YBCDropDatabase(Oid dboid, const char *dbname)
{
	YBCPgStatement handle;

	PG_TRY();
	{
		HandleYBStatus(YBCPgNewDropDatabase(ybc_pg_session,
											dbname,
											false,	/* if_exists */
											&handle));
		HandleYBStatus(YBCPgExecDropDatabase(handle));
	}
	PG_CATCH();
	{
		HandleYBStatus(YBCPgDeleteStatement(handle));
		PG_RE_THROW();
	}
	PG_END_TRY();
	HandleYBStatus(YBCPgDeleteStatement(handle));
}

void
YBCReserveOids(Oid dboid, Oid next_oid, uint32 count, Oid *begin_oid, Oid *end_oid)
{
	HandleYBStatus(YBCPgReserveOids(ybc_pg_session,
	                                dboid,
	                                next_oid,
	                                count,
	                                begin_oid,
	                                end_oid));
}

/* -------------------------------------------------------------------- */
/*  Table Functions. */

/* Utility function to add columns to the YB create statement */
static void CreateTableAddColumns(YBCPgStatement handle,
								  CreateStmt *stmt,
								  Oid namespaceId,
								  Constraint *primary_key,
								  bool include_hash,
								  bool include_primary)
{
	ListCell *lc;
	int      attnum = 1;
	foreach(lc, stmt->tableElts)
	{
		bool          is_primary = false;
		bool          is_first   = true;
		ColumnDef     *colDef    = lfirst(lc);
		YBCPgDataType col_type   = YBCDataTypeFromName(colDef->typeName);

		ListCell *cell;

		foreach(cell, primary_key->keys)
		{
			char *attname = strVal(lfirst(cell));

			if (strcmp(colDef->colname, attname) == 0)
			{
				is_primary = true;
				break;
			}
			if (is_first)
			{
				is_first = false;
			}
		}

		/* TODO For now, assume the first primary key column is the hash. */
		bool is_hash = is_primary && is_first;
		if (include_hash == is_hash && include_primary == is_primary)
		{
			HandleYBStmtStatus(YBCPgCreateTableAddColumn(handle,
			                                             colDef->colname,
			                                             attnum,
			                                             col_type,
			                                             is_hash,
			                                             is_primary), handle);
		}
		attnum++;
	}
}

void
YBCCreateTable(CreateStmt *stmt, char relkind, Oid namespaceId, Oid relationId)
{
	if (relkind != RELKIND_RELATION)
	{
		return;
	}

	YBCPgStatement handle = NULL;
	ListCell       *listptr;

	/*
	 * TODO The attnum from pg_attribute has not been created yet, we are
	 * making some assumptions below about how it will be assigned.
	 */
	char *db_name = get_database_name(MyDatabaseId);
	YBC_LOG_INFO("Creating Table %s, %s, %s",
							 db_name,
							 stmt->relation->schemaname,
							 stmt->relation->relname);

	Constraint *primary_key = NULL;

	foreach(listptr, stmt->constraints)
	{
		Constraint *constraint = lfirst(listptr);

		if (constraint->contype == CONSTR_PRIMARY)
		{
			primary_key = constraint;
		}
		else
		{
			ereport(ERROR,
			        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg(
					        "Only PRIMARY KEY constraints are currently supported.")));
		}
	}

	if (primary_key == NULL)
	{
		ereport(ERROR,
		        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg(
				        "Tables without a PRIMARY KEY are not yet supported.")));
	}

	HandleYBStatus(YBCPgNewCreateTable(ybc_pg_session,
	                                   db_name,
	                                   stmt->relation->schemaname,
	                                   stmt->relation->relname,
	                                   MyDatabaseId,
	                                   namespaceId,
	                                   relationId,
	                                   false, /* is_shared_table */
	                                   false, /* if_not_exists */
	                                   &handle));

	/*
	 * Process the table columns. They need to be sent in order, first hash
	 * columns, then rest of primary key columns, then regular columns
	 */
    CreateTableAddColumns(handle,
                          stmt,
                          namespaceId,
                          primary_key,
                          true /* is_hash */,
                          true /* is_primary */);

    CreateTableAddColumns(handle,
                          stmt,
                          namespaceId,
                          primary_key,
                          false /* is_hash */,
                          true /* is_primary */);

    CreateTableAddColumns(handle,
                          stmt,
                          namespaceId,
                          primary_key,
                          false /* is_hash */,
                          false /* is_primary */);

	/* Create the table. */
	HandleYBStmtStatus(YBCPgExecCreateTable(handle), handle);

	HandleYBStatus(YBCPgDeleteStatement(handle));
}

void
YBCDropTable(Oid relationId,
			 const char *relname,
			 const char *schemaname)
{
	YBCPgStatement handle;

	char *db_name = get_database_name(MyDatabaseId);

	HandleYBStatus(YBCPgNewDropTable(ybc_pg_session,
	                                 db_name,
	                                 schemaname,
	                                 relname,
	                                 false,    /* if_exists */
	                                 &handle));
	HandleYBStmtStatus(YBCPgExecDropTable(handle), handle);
	HandleYBStatus(YBCPgDeleteStatement(handle));
}
