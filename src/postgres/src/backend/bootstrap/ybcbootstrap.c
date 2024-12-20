/*--------------------------------------------------------------------------------------------------
 *
 * ybcbootstrap.c
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
 *        src/backend/commands/ybcbootstrap.c
 *
 *------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"
#include "catalog/pg_attribute.h"
#include "access/sysattr.h"
#include "catalog/pg_class.h"
#include "catalog/pg_type.h"
#include "commands/dbcommands.h"
#include "catalog/pg_database.h"
#include "commands/ybccmds.h"
#include "catalog/yb_type.h"

#include "catalog/catalog.h"
#include "access/htup_details.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "executor/tuptable.h"
#include "executor/ybcExpr.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

#include "parser/parser.h"

static void YBCAddSysCatalogColumn(YBCPgStatement yb_stmt,
								   IndexStmt *pkey_idx,
								   const char *attname,
								   int attnum,
								   Oid type_id,
								   int32 typmod,
								   bool key)
{

	ListCell      *lc;
	bool          is_key    = false;
	const YBCPgTypeEntity *col_type  = YbDataTypeFromOidMod(attnum, type_id);

	if (pkey_idx)
	{
		foreach(lc, pkey_idx->indexParams)
		{
			IndexElem *elem = lfirst(lc);
			if (strcmp(elem->name, attname) == 0)
			{
				is_key = true;
			}
		}
	}

	/* We will call this twice, first for key columns, then for regular
	 * columns to handle any re-ordering.
	 * So only adding the if matching the is_key property.
	 */
	if (key == is_key)
	{
		HandleYBStatus(YBCPgCreateTableAddColumn(yb_stmt,
																						 attname,
																						 attnum,
																						 col_type,
																						 false /* is_hash */,
																						 is_key,
																						 false /* is_desc */,
																						 false /* is_nulls_first */));
	}
}

static void YBCAddSysCatalogColumns(YBCPgStatement yb_stmt,
									TupleDesc tupdesc,
									IndexStmt *pkey_idx,
									const bool key)
{
	for (int attno = 0; attno < tupdesc->natts; attno++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, attno);
		YBCAddSysCatalogColumn(yb_stmt,
							   pkey_idx,
							   attr->attname.data,
							   attr->attnum,
							   attr->atttypid,
							   attr->atttypmod,
							   key);
	}
}

void
YBCCreateSysCatalogTable(const char *table_name,
						 Oid table_oid,
						 TupleDesc tupdesc,
						 bool is_shared_relation,
						 IndexStmt *pkey_idx)
{
	/* Database and schema are fixed when running inidb. */
	Assert(IsBootstrapProcessingMode());
	char           *db_name     = "template1";
	char           *schema_name = "pg_catalog";
	YBCPgStatement yb_stmt      = NULL;
	YBCPgYbrowidMode ybrowid_mode = (pkey_idx == NULL
									 ? PG_YBROWID_MODE_RANGE
									 : PG_YBROWID_MODE_NONE);

	HandleYBStatus(YBCPgNewCreateTable(db_name,
									   schema_name,
									   table_name,
									   Template1DbOid,
									   table_oid,
									   is_shared_relation,
									   true /* is_sys_catalog_table */,
									   false, /* if_not_exists */
									   ybrowid_mode,
									   true, /* is_colocated_via_database */
									   InvalidOid /* tablegroup_oid */,
									   InvalidOid /* colocation_id */,
									   InvalidOid /* tablespace_oid */,
									   false /* is_matview */,
									   InvalidOid /* pg_table_oid */,
									   InvalidOid /* old_relfilenode_oid */,
									   false /* is_truncate */,
									   &yb_stmt));

	/* Add all key columns first, then the regular columns */
	if (pkey_idx != NULL)
	{
		YBCAddSysCatalogColumns(yb_stmt, tupdesc, pkey_idx, /* key */ true);
	}
	YBCAddSysCatalogColumns(yb_stmt, tupdesc, pkey_idx, /* key */ false);

	HandleYBStatus(YBCPgExecCreateTable(yb_stmt));
}
