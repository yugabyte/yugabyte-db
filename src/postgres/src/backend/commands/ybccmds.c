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
 *------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"
#include "catalog/pg_attribute.h"
#include "access/sysattr.h"
#include "catalog/pg_class.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_type.h"
#include "commands/dbcommands.h"
#include "catalog/pg_database.h"
#include "commands/ybccmds.h"
#include "commands/ybctype.h"

#include "catalog/catalog.h"
#include "access/htup_details.h"
#include "utils/lsyscache.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "executor/tuptable.h"
#include "executor/ybcExpr.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

#include "parser/parser.h"

/* -------------------------------------------------------------------------- */
/*  Database Functions. */

void
YBCCreateDatabase(Oid dboid, const char *dbname, Oid src_dboid, Oid next_oid)
{
	YBCPgStatement handle;

	HandleYBStatus(YBCPgNewCreateDatabase(ybc_pg_session,
										  dbname,
										  dboid,
										  src_dboid,
										  next_oid,
										  &handle));
	HandleYBStmtStatus(YBCPgExecCreateDatabase(handle), handle);
	HandleYBStatus(YBCPgDeleteStatement(handle));
}

void
YBCDropDatabase(Oid dboid, const char *dbname)
{
	YBCPgStatement handle;

	HandleYBStatus(YBCPgNewDropDatabase(ybc_pg_session,
	                                    dbname,
	                                    false,    /* if_exists */
	                                    &handle));
	HandleYBStmtStatus(YBCPgExecDropDatabase(handle), handle);
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

/* ---------------------------------------------------------------------------------------------- */
/*  Table Functions. */

/* Utility function to add columns to the YB create statement */
static void CreateTableAddColumns(YBCPgStatement handle,
								  TupleDesc desc,
								  Constraint *primary_key,
								  bool include_hash,
								  bool include_primary)
{
	int      i;

	for (i = 0; i < desc->natts; i++)
	{
		Form_pg_attribute att = desc->attrs[i];
		char              *attname = NameStr(att->attname);
		AttrNumber        attnum = att->attnum;			
		bool              is_primary = false;
		bool              is_first   = true;

		if (primary_key != NULL)
		{
			ListCell *cell;

			foreach(cell, primary_key->keys)
			{
				char *kattname = strVal(lfirst(cell));

				if (strcmp(attname, kattname) == 0)
				{
					is_primary = true;
					break;
				}
				if (is_first)
				{
					is_first = false;
				}
			}
		}

		/* TODO For now, assume the first primary key column is the hash. */
		bool is_hash = is_primary && is_first;
		if (include_hash == is_hash && include_primary == is_primary)
		{
			YBCPgDataType col_type = YBCDataTypeFromOidMod(att->atttypid,
														   att->atttypmod);

			HandleYBStmtStatus(YBCPgCreateTableAddColumn(handle,
			                                             attname,
			                                             attnum,
			                                             col_type,
			                                             is_hash,
			                                             is_primary), handle);
		}
	}
}

void
YBCCreateTable(CreateStmt *stmt, char relkind, TupleDesc desc, Oid relationId, Oid namespaceId)
{
	if (relkind != RELKIND_RELATION)
	{
		return;
	}

	if (stmt->relation->relpersistence == RELPERSISTENCE_TEMP)
	{
		return; /* Nothing to do. */
	}

	YBCPgStatement handle = NULL;
	ListCell       *listptr;

	char *db_name = get_database_name(MyDatabaseId);
	char *schema_name = stmt->relation->schemaname;
	if (schema_name == NULL)
	{
		schema_name = get_namespace_name(namespaceId);
	}
	if (!IsBootstrapProcessingMode())
		YBC_LOG_INFO("Creating Table %s.%s.%s",
					 db_name,
					 schema_name,
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

	HandleYBStatus(YBCPgNewCreateTable(ybc_pg_session,
	                                   db_name,
	                                   schema_name,
	                                   stmt->relation->relname,
	                                   MyDatabaseId,
	                                   relationId,
	                                   false, /* is_shared_table */
	                                   false, /* if_not_exists */
																		 primary_key == NULL /* add_primary_key */,
	                                   &handle));

	/*
	 * Process the table columns. They need to be sent in order, first hash
	 * columns, then rest of primary key columns, then regular columns. If
	 * no primary key is specified, an internal primary key is added above.
	 */
	if (primary_key != NULL) {
		CreateTableAddColumns(handle,
							  desc,
							  primary_key,
							  true /* is_hash */,
							  true /* is_primary */);

		CreateTableAddColumns(handle,
							  desc,
							  primary_key,
							  false /* is_hash */,
							  true /* is_primary */);
	}

    CreateTableAddColumns(handle,
                          desc,
                          primary_key,
                          false /* is_hash */,
                          false /* is_primary */);

	/* Create the table. */
	HandleYBStmtStatus(YBCPgExecCreateTable(handle), handle);

	HandleYBStatus(YBCPgDeleteStatement(handle));
}

void
YBCDropTable(Oid relationId)
{
	YBCPgStatement handle;

	HandleYBStatus(YBCPgNewDropTable(ybc_pg_session,
									 MyDatabaseId,
									 relationId,
	                                 false,    /* if_exists */
	                                 &handle));
	HandleYBStmtStatus(YBCPgExecDropTable(handle), handle);
	HandleYBStatus(YBCPgDeleteStatement(handle));
}

void
YBCTruncateTable(Relation rel) {
	YBCPgStatement handle;
	Oid relationId = RelationGetRelid(rel);

	HandleYBStatus(YBCPgNewTruncateTable(ybc_pg_session, MyDatabaseId, relationId, &handle));
	HandleYBStmtStatus(YBCPgExecTruncateTable(handle), handle);
	HandleYBStatus(YBCPgDeleteStatement(handle));
}

void
YBCCreateIndex(const char *indexName,
			   IndexInfo *indexInfo,			   
			   TupleDesc indexTupleDesc,
			   Oid indexId,
			   Relation rel)
{
	char *db_name	  = get_database_name(MyDatabaseId);
	char *schema_name = get_namespace_name(RelationGetNamespace(rel));

	if (!IsBootstrapProcessingMode())
		YBC_LOG_INFO("Creating index %s.%s.%s",
					 db_name,
					 schema_name,
					 indexName);

	YBCPgStatement handle = NULL;

	HandleYBStatus(YBCPgNewCreateIndex(ybc_pg_session,
									   db_name,
									   schema_name,
									   indexName,
									   MyDatabaseId,
									   indexId,
									   RelationGetRelid(rel),
									   rel->rd_rel->relisshared,
									   indexInfo->ii_Unique,
									   false, /* if_not_exists */
									   &handle));

	int	 i;
	bool is_hash = true;

	for (i = 0; i < indexTupleDesc->natts; i++)
	{
		Form_pg_attribute att	   = indexTupleDesc->attrs[i];
		char			  *attname = NameStr(att->attname);
		AttrNumber		  attnum   = att->attnum;			
		YBCPgDataType	  col_type = YBCDataTypeFromOidMod(att->atttypid,
														   att->atttypmod);

		HandleYBStmtStatus(YBCPgCreateIndexAddColumn(handle,
													 attname,
													 attnum,
													 col_type,
													 is_hash,
													 true /* is_range */), handle);

		is_hash = false;
	}

	/* Create the index. */
	HandleYBStmtStatus(YBCPgExecCreateIndex(handle), handle);

	HandleYBStatus(YBCPgDeleteStatement(handle));
}
