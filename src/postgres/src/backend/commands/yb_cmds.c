/*--------------------------------------------------------------------------------------------------
 *
 * yb_cmds.c
 *        YB commands for creating and altering table structures and settings
 *
 * Copyright (c) YugabyteDB, Inc.
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
 *        src/backend/commands/yb_cmds.c
 *
 *------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/nbtree.h"
#include "access/relation.h"
#include "access/sysattr.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/binary_upgrade.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/partition.h"
#include "catalog/pg_am.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_class.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_database.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_type.h"
#include "catalog/pg_type_d.h"
#include "catalog/pg_yb_tablegroup.h"
#include "catalog/yb_catalog_version.h"
#include "catalog/yb_logical_client_version.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/event_trigger.h"
#include "commands/tablecmds.h"
#include "commands/tablespace.h"
#include "commands/yb_cmds.h"
#include "commands/yb_tablegroup.h"
#include "executor/executor.h"
#include "executor/tuptable.h"
#include "executor/ybExpr.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/optimizer.h"
#include "parser/parse_coerce.h"
#include "parser/parse_relation.h"
#include "parser/parse_type.h"
#include "parser/parse_utilcmd.h"
#include "parser/parser.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "yb/yql/pggate/ybc_gflags.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

/* Utility function to calculate column sorting options */
static void
ColumnSortingOptions(SortByDir dir, SortByNulls nulls, bool *is_desc, bool *is_nulls_first)
{
	if (dir == SORTBY_DESC)
	{
		/*
		 * From postgres doc NULLS FIRST is the default for DESC order.
		 * So SORTBY_NULLS_DEFAULT is equal to SORTBY_NULLS_FIRST here.
		 */
		*is_desc = true;
		*is_nulls_first = (nulls != SORTBY_NULLS_LAST);
	}
	else
	{
		/*
		 * From postgres doc ASC is the default sort order and NULLS LAST is the default for it.
		 * So SORTBY_DEFAULT is equal to SORTBY_ASC and SORTBY_NULLS_DEFAULT is equal
		 * to SORTBY_NULLS_LAST here.
		 */
		*is_desc = false;
		*is_nulls_first = (nulls == SORTBY_NULLS_FIRST);
	}
}

/* -------------------------------------------------------------------------- */
/*  Database Functions. */

void
YBCCreateDatabase(Oid dboid, const char *dbname, Oid src_dboid, Oid next_oid, bool colocated,
				  bool *retry_on_oid_collision, YbcCloneInfo *yb_clone_info)
{
	if (YBIsDBCatalogVersionMode())
	{
		/*
		 * In per database catalog version mode, disallow create database
		 * if we come too close to the limit.
		 */
		int64_t		num_databases = YbGetNumberOfDatabases();
		int64_t		num_reserved =
			*YBCGetGFlags()->ysql_num_databases_reserved_in_db_catalog_version_mode;

		if (kYBCMaxNumDbCatalogVersions - num_databases <= num_reserved)
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("too many databases")));
	}

	YbcPgStatement handle;

	HandleYBStatus(YBCPgNewCreateDatabase(dbname,
										  dboid,
										  src_dboid,
										  next_oid,
										  colocated,
										  yb_clone_info,
										  &handle));

	YbcStatus	createdb_status = YBCPgExecCreateDatabase(handle);

	/*
	 * If OID collision happends for CREATE DATABASE, then we need to retry
	 * CREATE DATABASE.
	 */
	if (retry_on_oid_collision)
	{
		*retry_on_oid_collision =
			(createdb_status &&
			 YBCStatusPgsqlError(createdb_status) == ERRCODE_DUPLICATE_DATABASE &&
			 *YBCGetGFlags()->ysql_enable_create_database_oid_collision_retry);

		if (*retry_on_oid_collision)
		{
			YBCFreeStatus(createdb_status);
			return;
		}
	}

	HandleYBStatus(createdb_status);

	if (YBIsDBCatalogVersionMode())
		YbCreateMasterDBCatalogVersionTableEntry(dboid);

	if (YBIsDBLogicalClientVersionMode())
		YbCreateMasterDBLogicalClientVersionTableEntry(dboid);
}

static void
YBCDropDBSequences(Oid dboid)
{
	YbcPgStatement sequences_handle;

	/*
	 * We need to create the sequences handle in the CurTransactionContext
	 * because it is executed after the transaction commits.
	 * The PortalContext is reset by that time, hence we cannot use it.
	 */
	Assert(CurTransactionContext != NULL);
	MemoryContext oldcontext = MemoryContextSwitchTo(CurTransactionContext);

	HandleYBStatus(YBCPgNewDropDBSequences(dboid, &sequences_handle));
	YBSaveDdlHandle(sequences_handle);
	MemoryContextSwitchTo(oldcontext);
}

void
YBCDropDatabase(Oid dboid, const char *dbname)
{
	YbcPgStatement handle;

	HandleYBStatus(YBCPgNewDropDatabase(dbname,
										dboid,
										&handle));
	bool		not_found = false;

	HandleYBStatusIgnoreNotFound(YBCPgExecDropDatabase(handle), &not_found);
	if (not_found)
		return;

	/*
	 * Enqueue the DDL handle for the sequences of this database to be dropped
	 * after the transaction commits. As of 2023-09-21, since Drop Database is
	 * not atomic, this is pointless. However, with #16395, Drop Database will
	 * be atomic and this will be useful.
	*/
	YBCDropDBSequences(dboid);

	if (YBIsDBCatalogVersionMode())
		YbDeleteMasterDBCatalogVersionTableEntry(dboid);

	if (YBIsDBLogicalClientVersionMode())
		YbDeleteMasterDBLogicalClientVersionTableEntry(dboid);
}

void
YBCReserveOids(Oid dboid, Oid next_oid, uint32 count, Oid *begin_oid, Oid *end_oid)
{
	HandleYBStatus(YBCPgReserveOids(dboid,
									next_oid,
									count,
									begin_oid,
									end_oid));
}

/* ------------------------------------------------------------------------- */
/*  Tablegroup Functions. */
void
YBCCreateTablegroup(Oid grp_oid, Oid tablespace_oid)
{
	YbcPgStatement handle;
	char	   *db_name = get_database_name(MyDatabaseId);

	HandleYBStatus(YBCPgNewCreateTablegroup(db_name, MyDatabaseId,
											grp_oid, tablespace_oid, &handle));
	HandleYBStatus(YBCPgExecCreateTablegroup(handle));
}

void
YBCDropTablegroup(Oid grpoid)
{
	YbcPgStatement handle;

	HandleYBStatus(YBCPgNewDropTablegroup(MyDatabaseId, grpoid, &handle));
	if (yb_ddl_rollback_enabled)
	{
		/*
		 * The following function marks the tablegroup for deletion. YB-Master
		 * will delete the tablegroup after the transaction is successfully
		 * committed.
		 */
		HandleYBStatus(YBCPgExecDropTablegroup(handle));
		return;
	}
	/*
	 * YSQL DDL Rollback is disabled. Fall back to performing the YB-Master
	 * side deletion after the transaction commits.
	 */
	YBSaveDdlHandle(handle);
}


/* ------------------------------------------------------------------------- */
/*  Table Functions. */
static void
CreateTableAddColumn(YbcPgStatement handle,
					 Form_pg_attribute att,
					 bool is_hash,
					 bool is_primary,
					 bool is_desc,
					 bool is_nulls_first)
{
	const AttrNumber attnum = att->attnum;
	const YbcPgTypeEntity *col_type = YbDataTypeFromOidMod(attnum,
														   att->atttypid);

	if (att->atttypid == VECTOROID && !yb_enable_docdb_vector_type)
		elog(ERROR,
			 "all nodes in the cluster need to upgrade before creating "
			 "a vector table");

	HandleYBStatus(YBCPgCreateTableAddColumn(handle,
											 NameStr(att->attname),
											 attnum,
											 col_type,
											 is_hash,
											 is_primary,
											 is_desc,
											 is_nulls_first));
}

/*
 * Utility function to add columns to the YB create statement
 * Columns need to be sent in order first hash columns, then rest of primary
 * key columns, then regular columns.
 */
static void
CreateTableAddColumns(YbcPgStatement handle, TupleDesc desc,
					  Constraint *primary_key, const bool colocated,
					  const bool is_tablegroup)
{
	ListCell   *cell;
	IndexElem  *index_elem;

	if (primary_key != NULL)
	{
		/* Add all key columns first with respect to compound key order */
		foreach(cell, primary_key->yb_index_params)
		{
			index_elem = lfirst_node(IndexElem, cell);

			if (index_elem->ordering == SORTBY_HASH && colocated)
				ereport(ERROR, (errcode(ERRCODE_INVALID_TABLE_DEFINITION),
								errmsg("cannot colocate hash partitioned table")));

			bool		column_found = false;

			for (int i = 0; i < desc->natts; ++i)
			{
				Form_pg_attribute att = TupleDescAttr(desc, i);

				if (strcmp(NameStr(att->attname), index_elem->name) == 0)
				{
					if (!YbDataTypeIsValidForKey(att->atttypid))
						ereport(ERROR,
								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								 errmsg("PRIMARY KEY containing column of type"
										" '%s' not yet supported",
										YBPgTypeOidToStr(att->atttypid))));


					/*
					 * In YB mode, the first column defaults to HASH if it is
					 * not set and its table is not colocated
					 */
					const bool	is_first_key = cell == list_head(primary_key->yb_index_params);

					SortByDir	yb_order = YbSortOrdering(index_elem->ordering,
														  colocated,
														  is_tablegroup,
														  is_first_key);
					bool		is_desc = false;
					bool		is_nulls_first = false;

					ColumnSortingOptions(yb_order,
										 index_elem->nulls_ordering,
										 &is_desc,
										 &is_nulls_first);
					CreateTableAddColumn(handle, att,
										 (yb_order == SORTBY_HASH) /* is_hash */ ,
										 true /* is_primary */ , is_desc,
										 is_nulls_first);
					column_found = true;
					break;
				}
			}
			if (!column_found)
				ereport(FATAL,
						(errcode(ERRCODE_INTERNAL_ERROR),
						 errmsg("column '%s' not found in table",
								index_elem->name)));
		}
	}

	/* Add all non-key columns */
	for (int i = 0; i < desc->natts; ++i)
	{
		Form_pg_attribute att = TupleDescAttr(desc, i);

		/*
		 * We may be creating this table as part of table rewrite. Therefore,
		 * the table metadata may include the metadata of previously dropped
		 * attributes, which we should ignore.
		 */
		if (att->attisdropped)
			continue;
		bool		is_key = false;

		if (primary_key)
			foreach(cell, primary_key->yb_index_params)
		{
			IndexElem  *index_elem = (IndexElem *) lfirst(cell);

			if (strcmp(NameStr(att->attname), index_elem->name) == 0)
			{
				is_key = true;
				break;
			}
		}
		if (!is_key)
			CreateTableAddColumn(handle,
								 att,
								 false /* is_hash */ ,
								 false /* is_primary */ ,
								 false /* is_desc */ ,
								 false /* is_nulls_first */ );
	}
}

static void
YBTransformPartitionSplitPoints(YbcPgStatement yb_stmt,
								List *split_points,
								Form_pg_attribute *attrs,
								int attr_count)
{
	/* Parser state for type conversion and validation */
	ParseState *pstate = make_parsestate(NULL);

	/* Construct values */
	PartitionRangeDatum *datums[INDEX_MAX_KEYS];
	int			datum_count = 0;
	ListCell   *lc;

	foreach(lc, split_points)
	{
		YBTransformPartitionSplitValue(pstate, castNode(List, lfirst(lc)), attrs, attr_count,
									   datums, &datum_count);

		/* Convert the values to yugabyte format and bind to statement. */
		YbcPgExpr	exprs[INDEX_MAX_KEYS];
		int			idx;

		for (idx = 0; idx < datum_count; idx++)
		{
			switch (datums[idx]->kind)
			{
				case PARTITION_RANGE_DATUM_VALUE:
					{
						/*
						 * Given value is not null. Convert it to YugaByte
						 * format.
						 */
						Const	   *value = castNode(Const, datums[idx]->value);

						/*
						 * Use attr->attcollation because the split value will be compared against
						 * collation-encoded strings that are encoded using the column collation.
						 * We assume collation-encoding will likely to retain the similar key
						 * distribution as the original text values.
						 */
						Form_pg_attribute attr = attrs[idx];

						exprs[idx] = YBCNewConstant(yb_stmt, value->consttype, attr->attcollation,
													value->constvalue, false /* is_null */ );
						break;
					}

				case PARTITION_RANGE_DATUM_MAXVALUE:
					{
						/* Create MINVALUE in YugaByte format */
						Form_pg_attribute attr = attrs[idx];

						exprs[idx] = YBCNewConstantVirtual(yb_stmt, attr->atttypid,
														   YB_YQL_DATUM_LIMIT_MAX);
						break;
					}

				case PARTITION_RANGE_DATUM_MINVALUE:
					{
						/* Create MINVALUE in YugaByte format */
						Form_pg_attribute attr = attrs[idx];

						exprs[idx] = YBCNewConstantVirtual(yb_stmt, attr->atttypid,
														   YB_YQL_DATUM_LIMIT_MIN);
						break;
					}
			}
		}

		/*
		 * Defaulted to MINVALUE for the rest of the columns that are not
		 * assigned a value
		 */
		for (; idx < attr_count; idx++)
		{
			Form_pg_attribute attr = attrs[idx];

			exprs[idx] = YBCNewConstantVirtual(yb_stmt, attr->atttypid,
											   YB_YQL_DATUM_LIMIT_MIN);
		}

		/* Add the split boundary to CREATE statement */
		HandleYBStatus(YBCPgAddSplitBoundary(yb_stmt, exprs, attr_count));
	}
}

/* Utility function to handle split points */
static void
CreateTableHandleSplitOptions(YbcPgStatement handle, TupleDesc desc,
							  YbOptSplit *split_options, Constraint *primary_key,
							  const bool colocated, const bool is_tablegroup,
							  YbcPgYbrowidMode ybrowid_mode)
{
	/* Address both types of split options */
	switch (split_options->split_type)
	{
		case NUM_TABLETS:
			{
				/* Make sure we have HASH columns */
				bool		hashable = true;

				if (primary_key)
				{
					/*
					 * If a primary key exists, we utilize it to check its
					 * ordering
					 */
					ListCell   *head = list_head(primary_key->yb_index_params);
					IndexElem  *index_elem = (IndexElem *) lfirst(head);

					if (!index_elem ||
						YbSortOrdering(index_elem->ordering, colocated,
									   is_tablegroup,
									   true /* is_first_key */ ) != SORTBY_HASH)
						hashable = false;
				}
				else
					hashable = ybrowid_mode == PG_YBROWID_MODE_HASH;

				if (!hashable)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("HASH columns must be present to split by number of tablets")));
				/* Tell pggate about it */
				HandleYBStatus(YBCPgCreateTableSetNumTablets(handle, split_options->num_tablets));
				break;
			}

		case SPLIT_POINTS:
			{
				if (primary_key == NULL)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("cannot split table that does not have primary key")));

				/*
				 * Find the column descriptions for primary key (split
				 * columns).
				 */
				Form_pg_attribute attrs[INDEX_MAX_KEYS];
				ListCell   *lc;
				int			attr_count = 0;

				foreach(lc, primary_key->yb_index_params)
				{
					const char *col_name = castNode(IndexElem, lfirst(lc))->name;

					for (int i = 0; i < desc->natts; i++)
					{
						Form_pg_attribute att = TupleDescAttr(desc, i);

						if (strcmp(NameStr(att->attname), col_name) == 0)
						{
							attrs[attr_count++] = att;
							break;
						}
					}
				}

				/* Analyze split_points and add them to CREATE statement */
				YBTransformPartitionSplitPoints(handle, split_options->split_points, attrs, attr_count);
				break;
			}

		default:
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("invalid split options")));
	}
}

/*
 * The DocDB table is created using the relfileNodeId OID. The relationId is
 * sent to DocDB to be stored as the pg table id.
 * During table rewrite, oldRelfileNodeId is used to construct the old
 * DocDB table ID that corresponds to the rewritten table.
 * This is used to determine if the table being rewritten is a part of xCluster
 * replication (if it is, the rewrite operation must fail).
 * tableName is used to specify the name of the DocDB table. For a typical
 * CREATE TABLE command, the tableName is the same as stmt->relation->relname.
 * However, during table rewrites, stmt->relation points to the new transient
 * PG relation (named pg_temp_xxxx), but we want to create a DocDB table with
 * the same name as the original PG relation.
 */
void
YBCCreateTable(CreateStmt *stmt, char *tableName, char relkind, TupleDesc desc,
			   Oid relationId, Oid namespaceId, Oid tablegroupId,
			   Oid colocationId, Oid tablespaceId, Oid relfileNodeId,
			   Oid oldRelfileNodeId, bool isTruncate)
{
	bool		is_internal_rewrite = oldRelfileNodeId != InvalidOid;

	if (relkind != RELKIND_RELATION && relkind != RELKIND_PARTITIONED_TABLE &&
		relkind != RELKIND_MATVIEW)
	{
		return;
	}

	if (stmt->relation->relpersistence == RELPERSISTENCE_TEMP)
	{
		return;					/* Nothing to do. */
	}

	YbcPgStatement handle = NULL;
	ListCell   *listptr;
	bool		is_shared_relation = tablespaceId == GLOBALTABLESPACE_OID;
	Oid			databaseId = YBCGetDatabaseOidFromShared(is_shared_relation);
	bool		is_matview = relkind == RELKIND_MATVIEW;
	bool		is_colocated_tables_with_tablespace_enabled =
		*YBCGetGFlags()->ysql_enable_colocated_tables_with_tablespaces;

	char	   *db_name = get_database_name(databaseId);
	char	   *schema_name = stmt->relation->schemaname;

	if (schema_name == NULL)
	{
		schema_name = get_namespace_name(namespaceId);
	}
	if (!IsBootstrapProcessingMode())
		YBC_LOG_INFO("Creating Table %s.%s.%s with DocDB table name %s",
					 db_name,
					 schema_name,
					 stmt->relation->relname,
					 tableName);

	Constraint *primary_key = NULL;

	foreach(listptr, stmt->constraints)
	{
		Constraint *constraint = lfirst(listptr);

		if (constraint->contype == CONSTR_PRIMARY)
		{
			primary_key = constraint;
		}
	}

	/*
	 * If this is a partition table, check whether it needs to inherit the same
	 * primary key as the parent partitioned table.
	 */
	if (primary_key == NULL && stmt->partbound)
	{
		/*
		 * This relation is not created yet and not visible to other
		 * backends. It doesn't really matter what lock we take here.
		 */
		Relation	rel = relation_open(relationId, AccessShareLock);

		/* Find the parent partitioned table */
		RangeVar   *rv = (RangeVar *) lfirst(list_head(stmt->inhRelations));
		Oid			parentOid = RangeVarGetRelid(rv, NoLock, false);

		Relation	parentRel = table_open(parentOid, NoLock);

		if (!MyDatabaseColocated || MyColocatedDatabaseLegacy)
		{
			Assert(!OidIsValid(tablegroupId));
			tablegroupId = YbGetTableProperties(parentRel)->tablegroup_oid;
		}
		List	   *idxlist = RelationGetIndexList(parentRel);
		ListCell   *cell;

		foreach(cell, idxlist)
		{
			Relation	idxRel = index_open(lfirst_oid(cell), AccessShareLock);

			/* Fetch pg_index tuple for source index from relcache entry */
			if (!((Form_pg_index) GETSTRUCT(idxRel->rd_indextuple))->indisprimary)
			{
				/*
				 * This is not a primary key index so this doesn't matter.
				 */
				relation_close(idxRel, AccessShareLock);
				continue;
			}

			AttrMap    *attmap;
			IndexStmt  *idxstmt;
			Oid			constraintOid;

			attmap = build_attrmap_by_name(RelationGetDescr(rel), RelationGetDescr(parentRel),
										   false /* yb_ignore_type_mismatch */ );
			idxstmt = generateClonedIndexStmt(NULL, idxRel, attmap, &constraintOid);

			primary_key = makeNode(Constraint);
			primary_key->contype = CONSTR_PRIMARY;
			primary_key->conname = idxstmt->idxname;
			primary_key->options = idxstmt->options;
			primary_key->indexspace = idxstmt->tableSpace;

			ListCell   *idxcell;

			foreach(idxcell, idxstmt->indexParams)
			{
				IndexElem  *ielem = lfirst(idxcell);

				primary_key->keys =
					lappend(primary_key->keys, makeString(ielem->name));
				primary_key->yb_index_params =
					lappend(primary_key->yb_index_params, ielem);
			}

			relation_close(idxRel, AccessShareLock);
		}
		table_close(parentRel, NoLock);
		table_close(rel, AccessShareLock);
	}

	/* By default, inherit the colocated option from the database */
	bool		is_colocated_via_database = MyDatabaseColocated;

	/* Handle user-supplied colocated reloption */
	ListCell   *opt_cell;

	foreach(opt_cell, stmt->options)
	{
		DefElem    *def = (DefElem *) lfirst(opt_cell);

		/*
		 * A check in parse_utilcmd.c makes sure only one of these two options
		 * can be specified.
		 */
		if (strcmp(def->defname, "colocated") == 0 ||
			strcmp(def->defname, "colocation") == 0)
		{
			bool		colocated_relopt = defGetBoolean(def);

			if (MyDatabaseColocated)
				is_colocated_via_database = colocated_relopt;
			else if (colocated_relopt)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot set colocation true on a non-colocated"
								" database")));

			/*
			 * The following break is fine because there should only be one
			 * colocated reloption at this point due to checks in
			 * parseRelOptions
			 */
			break;
		}
	}

	if (OidIsValid(colocationId) && !is_colocated_via_database && !OidIsValid(tablegroupId))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot set colocation_id for non-colocated table")));

	/*
	 * Lazily create an underlying tablegroup in a colocated database if needed.
	 */
	if (is_colocated_via_database && !MyColocatedDatabaseLegacy
		&& !is_internal_rewrite)
	{
		char	   *tablegroup_name = NULL;

		/*
		 * If the default tablespace of the current database is explicitly
		 * mentioned in CREATE TABLE ... TABLESPACE, then use InvalidOid as
		 * tablespaceId instead.
		 * This prevents creating redundant default tablegroup.
		 * Postgres does the similar thing with its default tablespaces.
		 */
		if (tablespaceId == MyDatabaseTableSpace)
			tablespaceId = InvalidOid;

		if (is_colocated_tables_with_tablespace_enabled &&
			OidIsValid(tablespaceId))
		{
			/*
			 * We look in pg_shdepend rather than directly use the derived name,
			 * as later we might need to associate an existing implicit tablegroup to a tablespace
			 */

			shdepFindImplicitTablegroup(tablespaceId, &tablegroupId);

			/*
			 * If we do not find a tablegroup corresponding to the given tablespace, we
			 * would have to create one . We derive the name from tablespace OID.
			 */
			tablegroup_name = (OidIsValid(tablegroupId) ?
							   get_tablegroup_name(tablegroupId) :
							   get_implicit_tablegroup_name(tablespaceId));
		}
		else if (yb_binary_restore &&
				 is_colocated_tables_with_tablespace_enabled &&
				 OidIsValid(binary_upgrade_next_tablegroup_oid))
		{
			/*
			 * In yb_binary_restore if tablespaceId is not valid but
			 * binary_upgrade_next_tablegroup_oid is valid, that implies either:
			 * 1. it is a default tablespace.
			 * 2. we are restoring without tablespace information.
			 * In this case all tables are restored to default tablespace,
			 * while maintaining the colocation properties, and tablegroup's name
			 * will be colocation_restore_tablegroupId, while default tablegroup's
			 * name would still be default.
			 */
			tablegroup_name =
				(binary_upgrade_next_tablegroup_default ?
				 DEFAULT_TABLEGROUP_NAME :
				 get_restore_tablegroup_name(binary_upgrade_next_tablegroup_oid));
			binary_upgrade_next_tablegroup_default = false;
			tablegroupId = get_tablegroup_oid(tablegroup_name, true);
		}
		else
		{
			tablegroup_name = DEFAULT_TABLEGROUP_NAME;
			tablegroupId = get_tablegroup_oid(tablegroup_name, true);
		}

		char	   *tablespace_name = (OidIsValid(tablespaceId) ?
									   get_tablespace_name(tablespaceId) :
									   NULL);

		/* Tablegroup doesn't exist, so create it. */
		if (!OidIsValid(tablegroupId))
		{
			/*
			 * Regardless of the current user, let postgres be the owner of the
			 * default tablegroup in a colocated database.
			 */
			RoleSpec   *spec = makeNode(RoleSpec);

			spec->roletype = ROLESPEC_CSTRING;
			spec->rolename = pstrdup("postgres");

			YbCreateTableGroupStmt *tablegroup_stmt = makeNode(YbCreateTableGroupStmt);

			tablegroup_stmt->tablegroupname = tablegroup_name;
			tablegroup_stmt->tablespacename = tablespace_name;
			tablegroup_stmt->implicit = true;
			tablegroup_stmt->owner = spec;
			tablegroupId = CreateTableGroup(tablegroup_stmt);
			stmt->tablegroupname = pstrdup(tablegroup_name);
		}
		/*
		 * Reset the binary_upgrade params as these are not needed anymore (only
		 * required in CreateTableGroup), to ensure these parameter values are
		 * not reused in subsequent unrelated statements.
		 */
		binary_upgrade_next_tablegroup_oid = InvalidOid;
		binary_upgrade_next_tablegroup_default = false;

		/* Record dependency between the table and tablegroup. */
		ObjectAddress myself,
					tablegroup;

		myself.classId = RelationRelationId;
		myself.objectId = relationId;
		myself.objectSubId = 0;

		tablegroup.classId = YbTablegroupRelationId;
		tablegroup.objectId = tablegroupId;
		tablegroup.objectSubId = 0;

		recordDependencyOn(&myself, &tablegroup, DEPENDENCY_NORMAL);
	}

	YbOptSplit *split_options = stmt->split_options;
	bool		is_sys_catalog_table = YbIsSysCatalogTabletRelationByIds(relationId,
																		 namespaceId,
																		 schema_name);
	const bool	is_tablegroup = OidIsValid(tablegroupId);

	/*
	 * Generate ybrowid ASC sequentially if the flag is on unless a SPLIT INTO
	 * option is provided, in which case the author likely intended a HASH
	 * partitioned table.
	 */
	const bool	can_generate_ybrowid_sequentially = (*YBCGetGFlags()->TEST_generate_ybrowid_sequentially &&
													 !(split_options &&
													   split_options->split_type == NUM_TABLETS));

	/*
	 * The hidden ybrowid column is added when there is no primary key.  This
	 * column is HASH or ASC sorted depending on certain criteria.
	 */
	YbcPgYbrowidMode ybrowid_mode;

	if (primary_key)
		ybrowid_mode = PG_YBROWID_MODE_NONE;
	else if (is_colocated_via_database || is_tablegroup ||
			 is_sys_catalog_table || can_generate_ybrowid_sequentially)
		ybrowid_mode = PG_YBROWID_MODE_RANGE;
	else
		ybrowid_mode = PG_YBROWID_MODE_HASH;

	HandleYBStatus(YBCPgNewCreateTable(db_name,
									   schema_name,
									   tableName,
									   databaseId,
									   relfileNodeId,
									   is_shared_relation,
									   is_sys_catalog_table,
									   false,	/* if_not_exists */
									   ybrowid_mode,
									   is_colocated_via_database,
									   tablegroupId,
									   colocationId,
									   tablespaceId,
									   is_matview,
									   relationId,
									   oldRelfileNodeId,
									   isTruncate,
									   &handle));

	CreateTableAddColumns(handle, desc, primary_key, is_colocated_via_database,
						  is_tablegroup);

	if (stmt->partspec != NULL)
	{
		/* Parent partitions do not hold data, so 1 tablet is sufficient */
		HandleYBStatus(YBCPgCreateTableSetNumTablets(handle, 1));
	}

	/* Handle SPLIT statement, if present */
	if (split_options)
	{
		if (is_colocated_via_database)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot create colocated table with split option")));
		CreateTableHandleSplitOptions(handle, desc, split_options, primary_key,
									  is_colocated_via_database, is_tablegroup,
									  ybrowid_mode);
	}
	/* Create the table. */
	HandleYBStatus(YBCPgExecCreateTable(handle));
}

void
YBCDropTable(Relation relation)
{
	YbcTableProperties yb_props = YbTryGetTableProperties(relation);

	if (!yb_props)
	{
		/* Table was not found on YB side, nothing to do */
		return;
	}
	/*
	 * However, since we were likely hitting the cache, we still need to
	 * safeguard against NotFound errors.
	 */

	YbcPgStatement handle = NULL;
	Oid			databaseId = YBCGetDatabaseOid(relation);

	/*
	 * Create table-level tombstone for colocated (via DB or tablegroup)
	 * tables
	 */
	if (yb_props->is_colocated)
	{
		bool		not_found = false;

		HandleYBStatusIgnoreNotFound(YBCPgNewTruncateColocated(databaseId,
															   YbGetRelfileNodeId(relation),
															   false, &handle,
															   YB_TRANSACTIONAL),
									 &not_found);
		/*
		 * Since the creation of the handle could return a 'NotFound' error,
		 * execute the statement only if the handle is valid.
		 */
		const bool	valid_handle = !not_found;

		if (valid_handle)
		{
			HandleYBStatusIgnoreNotFound(YBCPgDmlBindTable(handle), &not_found);
			int			rows_affected_count = 0;

			HandleYBStatusIgnoreNotFound(YBCPgDmlExecWriteOp(handle, &rows_affected_count),
										 &not_found);
		}
	}

	/* Drop the table */
	{
		bool		not_found = false;

		HandleYBStatusIgnoreNotFound(YBCPgNewDropTable(databaseId,
													   YbGetRelfileNodeId(relation),
													   false /* if_exists */ ,
													   &handle),
									 &not_found);
		if (not_found)
		{
			return;
		}

		if (YbDdlRollbackEnabled())
		{
			/*
			 * The following issues a request to the YB-Master to drop the
			 * table once this transaction commits.
			 */
			HandleYBStatusIgnoreNotFound(YBCPgExecDropTable(handle),
										 &not_found);
			return;
		}
		/*
		 * YSQL DDL Rollback is disabled/unsupported. This means DocDB will not
		 * rollback the drop if the transaction ends up failing. We cannot
		 * abort drop in DocDB so postpone the execution until the rest of the
		 * statement/txn finishes executing.
		 */
		YBSaveDdlHandle(handle);
	}
}

void
YBCDropSequence(Oid sequence_oid)
{
	YbcPgStatement handle;

	/*
	 * We need to create the handle in the CurTransactionContext because it is
	 * executed after the transaction commits.
	 * The PortalContext is reset by that time, hence we cannot use it.
	 */
	Assert(CurTransactionContext != NULL);
	MemoryContext oldcontext = MemoryContextSwitchTo(CurTransactionContext);

	HandleYBStatus(YBCPgNewDropSequence(MyDatabaseId, sequence_oid, &handle));
	YBSaveDdlHandle(handle);
	MemoryContextSwitchTo(oldcontext);
}

/*
 * This function is inspired by RelationSetNewRelfilenode() in
 * backend/utils/cache/relcache.c It updates the tuple corresponding to the
 * truncated relation in pg_class in the sys cache.
 */
static void
YbOnTruncateUpdateCatalog(Relation rel)
{
	Relation	pg_class;
	HeapTuple	tuple;
	Form_pg_class classform;

	pg_class = table_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(RelationGetRelid(rel)));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "could not find tuple for relation %u", RelationGetRelid(rel));
	classform = (Form_pg_class) GETSTRUCT(tuple);

	if (rel->rd_rel->relkind != RELKIND_SEQUENCE)
	{
		classform->relpages = 0;
		classform->reltuples = -1;
		classform->relallvisible = 0;
	}

	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	heap_freetuple(tuple);
	table_close(pg_class, RowExclusiveLock);

	/* This makes the pg_class row change visible. */
	CommandCounterIncrement();
}

/*
 * Execute an unsafe YB truncate operation (without table rewrite):
 * For non-colocated tables: perform a tablet-level truncate.
 * For colocated tables: add a table-level tombstone.
 */
void
YbUnsafeTruncate(Relation rel)
{
	YbcPgStatement handle;
	Oid			relfileNodeId = YbGetRelfileNodeId(rel);
	Oid			databaseId = YBCGetDatabaseOid(rel);
	bool		isRegionLocal = YBCIsRegionLocal(rel);

	if (IsSystemRelation(rel) || YbGetTableProperties(rel)->is_colocated)
	{
		/*
		 * Create table-level tombstone for colocated/tablegroup/syscatalog
		 * relations.
		 */
		HandleYBStatus(YBCPgNewTruncateColocated(databaseId,
												 relfileNodeId,
												 isRegionLocal,
												 &handle,
												 YB_TRANSACTIONAL));
		HandleYBStatus(YBCPgDmlBindTable(handle));
		int			rows_affected_count = 0;

		HandleYBStatus(YBCPgDmlExecWriteOp(handle, &rows_affected_count));
	}
	else
	{
		/* Send truncate table RPC to master for non-colocated relations */
		HandleYBStatus(YBCPgNewTruncateTable(databaseId,
											 relfileNodeId,
											 &handle));
		HandleYBStatus(YBCPgExecTruncateTable(handle));
	}

	/* Update catalog metadata of the truncated table */
	YbOnTruncateUpdateCatalog(rel);

	if (!rel->rd_rel->relhasindex)
		return;

	/* Truncate the associated secondary indexes */
	List	   *indexlist = RelationGetIndexList(rel);
	ListCell   *lc;

	foreach(lc, indexlist)
	{
		Oid			indexId = lfirst_oid(lc);

		/*
		 * Lock level doesn't fully work in YB.  Since YB TRUNCATE is already
		 * considered to not be transaction-safe, it doesn't really matter.
		 */
		Relation	indexRel = index_open(indexId, AccessExclusiveLock);

		/* PK index is not secondary index, perform only catalog update */
		if (indexId == rel->rd_pkindex)
		{
			YbOnTruncateUpdateCatalog(indexRel);
			index_close(indexRel, AccessExclusiveLock);
			continue;
		}

		YbUnsafeTruncate(indexRel);
		index_close(indexRel, AccessExclusiveLock);
	}

	list_free(indexlist);
}

/* Utility function to handle split points */
static void
CreateIndexHandleSplitOptions(YbcPgStatement handle,
							  TupleDesc desc,
							  YbOptSplit *split_options,
							  int16 *coloptions,
							  int numIndexKeyAttrs)
{
	/* Address both types of split options */
	switch (split_options->split_type)
	{
		case NUM_TABLETS:
			/* Make sure we have HASH columns */
			if (!(coloptions[0] & INDOPTION_HASH))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("HASH columns must be present to split by number of tablets")));

			HandleYBStatus(YBCPgCreateIndexSetNumTablets(handle, split_options->num_tablets));
			break;

		case SPLIT_POINTS:
			{
				/* Construct array to SPLIT column datatypes */
				Form_pg_attribute attrs[INDEX_MAX_KEYS];
				int			attr_count;

				for (attr_count = 0; attr_count < numIndexKeyAttrs; ++attr_count)
				{
					attrs[attr_count] = TupleDescAttr(desc, attr_count);
				}

				/* Analyze split_points and add them to CREATE statement */
				YBTransformPartitionSplitPoints(handle, split_options->split_points, attrs, attr_count);
				break;
			}

		default:
			ereport(ERROR, (errmsg("illegal memory state for SPLIT options")));
			break;
	}
}

void
YBCBindCreateIndexColumns(YbcPgStatement handle,
						  IndexInfo *indexInfo,
						  TupleDesc indexTupleDesc,
						  int16 *coloptions,
						  int numIndexKeyAttrs)
{
	for (int i = 0; i < indexTupleDesc->natts; i++)
	{
		Form_pg_attribute att = TupleDescAttr(indexTupleDesc, i);
		char	   *attname = NameStr(att->attname);
		AttrNumber	attnum = att->attnum;
		const YbcPgTypeEntity *col_type = YbDataTypeFromOidMod(attnum, att->atttypid);

		const bool	is_key = (i < numIndexKeyAttrs);

		if (is_key)
		{
			if (!YbDataTypeIsValidForKey(att->atttypid))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("INDEX on column of type '%s' not yet supported",
								YBPgTypeOidToStr(att->atttypid))));
		}

		/*
		 * Non-key columns' options are always 0, and aren't explicitly stored
		 * in pg_index.indoptions. As they aren't stored, we may not have
		 * them in coloptions if the caller chose not to include them,
		 * so avoid checking coloptions if the column is a key column.
		 */
		const int16 options = is_key ? coloptions[i] : 0;
		const bool	is_hash = options & INDOPTION_HASH;
		const bool	is_desc = options & INDOPTION_DESC;
		const bool	is_nulls_first = options & INDOPTION_NULLS_FIRST;

		HandleYBStatus(YBCPgCreateIndexAddColumn(handle,
												 attname,
												 attnum,
												 col_type,
												 is_hash,
												 is_key,
												 is_desc,
												 is_nulls_first));
	}
}

/*
 * Similar to YBCCreateTable, the DocDB table for the index is created using
 * indexRelfileNodeId, indexId is sent to DocDB to be stored as the
 * pg table id, and oldRelfileNodeId is used during table rewrite.
 */
void
YBCCreateIndex(const char *indexName,
			   IndexInfo *indexInfo,
			   TupleDesc indexTupleDesc,
			   int16 *coloptions,
			   Datum reloptions,
			   Oid indexId,
			   Relation rel,
			   YbOptSplit *split_options,
			   const bool skip_index_backfill,
			   bool is_colocated,
			   Oid tablegroupId,
			   Oid colocationId,
			   Oid tablespaceId,
			   Oid indexRelfileNodeId,
			   Oid oldRelfileNodeId,
			   Oid *opclassOids)
{
	Oid			namespaceId = RelationGetNamespace(rel);
	char	   *db_name = get_database_name(YBCGetDatabaseOid(rel));
	char	   *schema_name = get_namespace_name(namespaceId);
	bool		is_sys_catalog_index = YbIsSysCatalogTabletRelationByIds(indexId,
																		 namespaceId,
																		 schema_name);

	if (!IsBootstrapProcessingMode())
		YBC_LOG_INFO("Creating index %s.%s.%s",
					 db_name,
					 schema_name,
					 indexName);

	YbcPgStatement handle = NULL;

	HandleYBStatus(YBCPgNewCreateIndex(db_name,
									   schema_name,
									   indexName,
									   YBCGetDatabaseOid(rel),
									   indexRelfileNodeId,
									   YbGetRelfileNodeId(rel),
									   rel->rd_rel->relisshared,
									   is_sys_catalog_index,
									   indexInfo->ii_Unique,
									   skip_index_backfill,
									   false,	/* if_not_exists */
									   MyDatabaseColocated && is_colocated,
									   tablegroupId,
									   colocationId,
									   tablespaceId,
									   indexId,
									   oldRelfileNodeId,
									   &handle));

	IndexAmRoutine *amroutine = GetIndexAmRoutineByAmId(indexInfo->ii_Am,
														true);

	Assert(amroutine != NULL && amroutine->yb_ambindschema != NULL);
	amroutine->yb_ambindschema(handle, indexInfo, indexTupleDesc, coloptions,
							   opclassOids, reloptions);

	/* Handle SPLIT statement, if present */
	if (split_options)
		CreateIndexHandleSplitOptions(handle, indexTupleDesc, split_options, coloptions,
									  indexInfo->ii_NumIndexKeyAttrs);

	/* Create the index. */
	HandleYBStatus(YBCPgExecCreateIndex(handle));
}

static Node *
ybFetchDefaultConstraintExpr(const ColumnDef *column, Relation rel)
{
	Node	   *result = NULL;
	ListCell   *clist;

	foreach(clist, column->constraints)
	{
		Constraint *constraint = lfirst_node(Constraint, clist);

		if (constraint->contype == CONSTR_DEFAULT)
		{
			if (result)
				ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
								errmsg("multiple default values specified for column \"%s\"  of table \"%s\"",
									   column->colname,
									   RelationGetRelationName(rel))));
			result = constraint->raw_expr;
			Assert(constraint->cooked_expr == NULL);
		}
	}
	return result;
}

static List *
YBCPrepareAlterTableCmd(AlterTableCmd *cmd, Relation rel, List *handles,
						int *col, bool *needsYBAlter,
						YbcPgStatement *rollbackHandle,
						bool isPartitionOfAlteredTable)
{
	Oid			relationId = RelationGetRelid(rel);
	Oid			relfileNodeId = YbGetRelfileNodeId(rel);

	if (isPartitionOfAlteredTable)
	{
		/*
		 * This function was invoked on a child partition table to reflect
		 * the effects of Alter on its parent.
		 */
		switch (cmd->subtype)
		{
			case AT_AddColumnRecurse:
			case AT_DropColumnRecurse:
			case AT_AddConstraintRecurse:
			case AT_DropConstraintRecurse:
			case AT_ValidateConstraintRecurse:
			case AT_DropExpression:
				break;
			default:
				/*
				 * This is not an alter command on a partitioned table that
				 * needs to trickle down to its child partitions. Nothing to
				 * do.
				 */
				return handles;
		}
	}
	switch (cmd->subtype)
	{
		case AT_AddColumn:
		case AT_AddColumnToView:
		case AT_AddColumnRecurse:
			{
				ColumnDef  *colDef = (ColumnDef *) cmd->def;
				Oid			typeOid;
				int32		typmod;
				HeapTuple	typeTuple;
				int			order;

				/* Skip yb alter for IF NOT EXISTS with existing column */
				if (cmd->missing_ok)
				{
					HeapTuple	tuple = SearchSysCacheAttName(relationId, colDef->colname);

					if (HeapTupleIsValid(tuple))
					{
						ReleaseSysCache(tuple);
						break;
					}
				}

				/*
				 * The typeOid for serial types is filled in during parse analysis
				 * for the ALTER TABLE ... ADD COLUMN operation, which is
				 * done at a later stage by PG (specifically, in ATExecAddColumn).
				 * So in case this is a serial type, we will have to do the
				 * conversion to the appropriate type ourselves.
				 */
				typeOid = YbGetSerialTypeOidFromColumnDef(colDef);
				if (typeOid == InvalidOid)
				{
					typeTuple = typenameType(NULL, colDef->typeName, &typmod);
					typeOid = ((Form_pg_type) GETSTRUCT(typeTuple))->oid;
					ReleaseSysCache(typeTuple);
				}

				order = RelationGetNumberOfAttributes(rel) + *col;
				const YbcPgTypeEntity *col_type = YbDataTypeFromOidMod(order, typeOid);

				Assert(list_length(handles) == 1);
				YbcPgStatement add_col_handle = (YbcPgStatement) lfirst(list_head(handles));

				YbcPgExpr	missing_value = NULL;
				Node	   *default_expr = ybFetchDefaultConstraintExpr(colDef, rel);

				if (default_expr && yb_enable_add_column_missing_default)
				{
					ParseState *pstate = make_parsestate(NULL);

					pstate->p_sourcetext = NULL;
					ParseNamespaceItem *nsitem = addRangeTableEntryForRelation(pstate,
																			   rel,
																			   AccessShareLock,
																			   NULL,
																			   false,
																			   true);

					addNSItemToQuery(pstate, nsitem, true, true, true);
					Expr	   *expr = (Expr *) cookDefault(pstate, default_expr, typeOid,
															typmod, colDef->colname,
															colDef->generated);

					/*
					 * Compute the missing default value if the default expression
					 * is non-volatile.
					 */
					if (!contain_volatile_functions((Node *) expr))
					{
						expr = expression_planner(expr);
						EState	   *estate = CreateExecutorState();
						ExprState  *exprState = ExecPrepareExpr(expr, estate);
						ExprContext *econtext = GetPerTupleExprContext(estate);
						bool		missingIsNull;
						Datum		missingValDatum = ExecEvalExpr(exprState, econtext,
																   &missingIsNull);

						missing_value = YBCNewConstant(add_col_handle, typeOid,
													   colDef->collOid,
													   missingValDatum,
													   missingIsNull);
						FreeExecutorState(estate);
					}
				}
				HandleYBStatus(YBCPgAlterTableAddColumn(add_col_handle,
														colDef->colname,
														order,
														col_type,
														missing_value));
				++(*col);
				*needsYBAlter = true;

				/*
				 * Prepare the handle that will be used to rollback
				 * this change at the DocDB side. This is an add column
				 * statement, thus the equivalent rollback operation
				 * will be to drop the column.
				 */
				if (*rollbackHandle == NULL)
				{
					HandleYBStatus(YBCPgNewAlterTable(YBCGetDatabaseOid(rel),
													  relfileNodeId,
													  rollbackHandle));
				}
				HandleYBStatus(YBCPgAlterTableDropColumn(*rollbackHandle,
														 colDef->colname));
				break;
			}

		case AT_DropColumn:
		case AT_DropColumnRecurse:
			{
				HeapTuple	tuple = SearchSysCacheAttName(relationId, cmd->name);

				/* Skip yb alter for non-existent column */
				if (!HeapTupleIsValid(tuple))
					break;
				AttrNumber	attnum = ((Form_pg_attribute) GETSTRUCT(tuple))->attnum;

				ReleaseSysCache(tuple);
				/*
				 * Skip yb alter for primary key columns (the table will be
				 * rewritten)
				 */
				if (YbIsAttrPrimaryKeyColumn(rel, attnum))
					break;
				Assert(list_length(handles) == 1);
				YbcPgStatement drop_col_handle = (YbcPgStatement) lfirst(list_head(handles));

				HandleYBStatus(YBCPgAlterTableDropColumn(drop_col_handle,
														 cmd->name));
				*needsYBAlter = true;

				break;
			}

		case AT_AddIndexConstraint:
			{
				IndexStmt  *index = (IndexStmt *) cmd->def;

				/*
				 * Only allow adding indexes when it is a unique or primary
				 * key constraint
				 */
				if (!(index->unique || index->primary) || !index->isconstraint)
				{
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("this ALTER TABLE command"
									" is not yet supported")));
				}

				break;
			}

		case AT_AddIndex:
		case AT_AddConstraint:
		case AT_AddConstraintRecurse:
		case AT_AlterColumnType:
		case AT_DropConstraint:
		case AT_DropConstraintRecurse:
		case AT_DropOids:
		case AT_EnableTrig:
		case AT_EnableAlwaysTrig:
		case AT_EnableReplicaTrig:
		case AT_EnableTrigAll:
		case AT_EnableTrigUser:
		case AT_DisableTrig:
		case AT_DisableTrigAll:
		case AT_DisableTrigUser:
		case AT_ChangeOwner:
		case AT_ColumnDefault:
		case AT_CookedColumnDefault:
		case AT_DropNotNull:
		case AT_SetNotNull:
		case AT_AddIdentity:
		case AT_SetIdentity:
		case AT_DropIdentity:
		case AT_EnableRowSecurity:
		case AT_DisableRowSecurity:
		case AT_SetStatistics:
		case AT_ForceRowSecurity:
		case AT_NoForceRowSecurity:
		case AT_AttachPartition:
		case AT_DetachPartition:
		case AT_SetTableSpace:
		case AT_ValidateConstraint:
		case AT_ValidateConstraintRecurse:
		case AT_DropExpression:
			{
				Assert(cmd->subtype != AT_DropConstraint);
				if (cmd->subtype == AT_AlterColumnType)
				{
					HeapTuple	typeTuple;

					/* Get current typid and typmod of the column. */
					typeTuple = SearchSysCacheAttName(relationId, cmd->name);
					if (!HeapTupleIsValid(typeTuple))
					{
						ereport(ERROR,
								(errcode(ERRCODE_UNDEFINED_COLUMN),
								 errmsg("column \"%s\" of relation \"%s\" does not exist",
										cmd->name, RelationGetRelationName(rel))));
					}
					ReleaseSysCache(typeTuple);
				}
				/*
				 * For these cases a YugaByte metadata does not need to be updated
				 * but we still need to increment the schema version.
				 */
				ListCell   *handle = NULL;

				foreach(handle, handles)
				{
					YbcPgStatement increment_schema_handle = (YbcPgStatement) lfirst(handle);

					HandleYBStatus(YBCPgAlterTableIncrementSchemaVersion(increment_schema_handle));
				}
				List	   *dependent_rels = NIL;

				/*
				 * For attach and detach partition cases, assigning
				 * the partition table as dependent relation.
				 */
				if (cmd->subtype == AT_AttachPartition ||
					cmd->subtype == AT_DetachPartition)
				{
					RangeVar   *partition_rv = ((PartitionCmd *) cmd->def)->name;
					Relation	r = relation_openrv(partition_rv, AccessExclusiveLock);
					char		relkind = r->rd_rel->relkind;

					relation_close(r, AccessExclusiveLock);
					/*
					 * If alter is performed on an index as opposed to a table
					 * skip schema version increment.
					 */
					if (relkind == RELKIND_INDEX ||
						relkind == RELKIND_PARTITIONED_INDEX)
					{
						return handles;
					}

					List	   *affectedPartitions = NIL;

					affectedPartitions = lappend(affectedPartitions,
												 table_openrv(partition_rv, AccessExclusiveLock));

					/*
					 * While attaching a partition to the parent partitioned table,
					 * additionally increment the schema version of the default
					 * partition as well. This will prevent any concurrent
					 * operations inserting data matching this new partition from
					 * being inserted into the default partition
					 */

					if (cmd->subtype == AT_AttachPartition)
					{
						Oid			defaultOid = get_default_partition_oid(rel->rd_id);

						if (OidIsValid(defaultOid))
						{
							Relation	defaultPartition = table_open(defaultOid, AccessExclusiveLock);

							affectedPartitions = lappend(affectedPartitions, defaultPartition);
						}
					}

					/*
					 * If the partition table is not YB supported table including
					 * foreign table, skip schema version increment.
					 */
					ListCell   *lc = NULL;

					foreach(lc, affectedPartitions)
					{
						Relation	partition = (Relation) lfirst(lc);

						if (!IsYBBackedRelation(partition) ||
							partition->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
						{
							table_close(partition, AccessExclusiveLock);
							continue;
						}
						dependent_rels = lappend(dependent_rels, partition);
					}
				}
				/*
				 * For add foreign key case, assigning the primary key table
				 * as dependent relation.
				 */
				else if (cmd->subtype == AT_AddConstraintRecurse &&
						 ((Constraint *) cmd->def)->contype == CONSTR_FOREIGN)
				{
					dependent_rels = lappend(dependent_rels,
											 table_openrv(((Constraint *) cmd->def)->pktable,
														  AccessExclusiveLock));
				}
				/*
				 * For drop foreign key case, assigning the primary key table
				 * as dependent relation.
				 * For partition and inheritance children, we do not need to identify foreign
				 * key dependent relations separately. For partition children, the foreign key
				 * dependent relation is the same as the parent. For inheritance, foreign key
				 * constraints do not recurse down to children.
				 */
				else if (cmd->subtype == AT_DropConstraintRecurse && !isPartitionOfAlteredTable)
				{
					Oid			constraint_oid = get_relation_constraint_oid(relationId,
																			 cmd->name,
																			 cmd->missing_ok);

					/*
					 * If the constraint doesn't exists and IF EXISTS is specified,
					 * A NOTICE will be reported later in ATExecDropConstraint.
					 */
					if (!OidIsValid(constraint_oid))
					{
						return handles;
					}
					HeapTuple	tuple = SearchSysCache1(CONSTROID,
														ObjectIdGetDatum(constraint_oid));

					if (!HeapTupleIsValid(tuple))
					{
						ereport(ERROR,
								(errcode(ERRCODE_SYSTEM_ERROR),
								 errmsg("cache lookup failed for constraint %u",
										constraint_oid)));
					}
					Form_pg_constraint con =
						(Form_pg_constraint) GETSTRUCT(tuple);

					ReleaseSysCache(tuple);
					if (con->contype == CONSTRAINT_FOREIGN &&
						relationId != con->confrelid)
					{
						dependent_rels = lappend(dependent_rels,
												 table_open(con->confrelid, AccessExclusiveLock));
					}
				}
				/*
				 * For add index case, assigning the table where index
				 * is built on as dependent relation.
				 */
				else if (cmd->subtype == AT_AddIndex)
				{
					IndexStmt  *index = (IndexStmt *) cmd->def;

					/*
					 * Only allow adding indexes when it is a unique
					 * or primary key constraint
					 */
					if (!(index->unique || index->primary) || !index->isconstraint)
					{
						ereport(ERROR,
								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								 errmsg("this ALTER TABLE command is"
										" not yet supported")));
					}
					dependent_rels = lappend(dependent_rels,
											 table_openrv(index->relation, AccessExclusiveLock));
				}

				/*
				 * During initdb, skip the schema version increment for ALTER
				 * TABLE ... ADD PRIMARY KEY/UNIQUE USING INDEX.
				 *
				 * Currently, CatalogManager::AlterTable() does not support
				 * altering catalog relations. During initdb there is a single
				 * connection, so we can skip the schema version increment and
				 * thus, avoid any YB metadata update. This allows us to
				 * execute this command without handling catalog relations in
				 * CatalogManager::AlterTable().
				 *
				 * This should be applicable to all ALTER TABLEs that reach this
				 * switch block. But, for now, only apply it to ALTER TABLE ...
				 * ADD PRIMARY KEY/UNIQUE USING INDEX.
				 */
				if (YBCIsInitDbModeEnvVarSet() &&
					cmd->subtype == AT_AddConstraintRecurse &&
					(((Constraint *) cmd->def)->contype == CONSTR_UNIQUE ||
					 ((Constraint *) cmd->def)->contype ==
					 CONSTR_PRIMARY) &&
					((Constraint *) cmd->def)->indexname != NULL)
				{
					Assert(YbIsSysCatalogTabletRelation(rel));
					Assert(dependent_rels == NIL);
					return handles;
				}

				/*
				 * If dependent relation exists, apply increment schema version
				 * operation on the dependent relation.
				 */
				ListCell   *lc = NULL;

				foreach(lc, dependent_rels)
				{
					Relation	dependent_rel = (Relation) lfirst(lc);

					Assert(dependent_rel != NULL);
					Oid			relationId = RelationGetRelid(dependent_rel);
					YbcPgStatement alter_cmd_handle = NULL;

					HandleYBStatus(YBCPgNewAlterTable(YBCGetDatabaseOidByRelid(relationId),
													  YbGetRelfileNodeId(dependent_rel),
													  &alter_cmd_handle));
					HandleYBStatus(YBCPgAlterTableIncrementSchemaVersion(alter_cmd_handle));
					handles = lappend(handles, alter_cmd_handle);
					YbTrackAlteredTableId(relationId);
					table_close(dependent_rel, AccessExclusiveLock);
				}
				*needsYBAlter = true;
				break;
			}

		case AT_ReplicaIdentity:
			{
				if (!yb_enable_replica_identity)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("replica identity is unavailable"),
							 errdetail("yb_enable_replica_identity is false or a "
									   "system upgrade is in progress.")));

				YbcPgStatement replica_identity_handle = (YbcPgStatement) lfirst(list_head(handles));
				ReplicaIdentityStmt *stmt = (ReplicaIdentityStmt *) cmd->def;

				HandleYBStatus(YBCPgAlterTableSetReplicaIdentity(replica_identity_handle,
																 stmt->identity_type));
				*needsYBAlter = true;
				break;
			}

		case AT_SetStorage:
			yb_switch_fallthrough();
		case AT_SetLogged:
			yb_switch_fallthrough();
		case AT_SetUnLogged:
			*needsYBAlter = false;
			break;

		case AT_SetRelOptions:
		case AT_ResetRelOptions:
			*needsYBAlter = false;
			ereport(NOTICE,
					(errmsg("storage parameters are currently ignored in YugabyteDB")));
			break;

		case AT_AddOf:
			yb_switch_fallthrough();
		case AT_DropOf:
			*needsYBAlter = false;
			break;

		case AT_DropInherit:
			yb_switch_fallthrough();
		case AT_AddInherit:
			/*
			 * Altering the inheritance should keep the docdb column list the same and not
			 * require an ALTER.
			 * This will need to be re-evaluated, if NULLability is propagated to docdb.
			 */
			*needsYBAlter = false;
			break;

		case AT_AlterConstraint:
			*needsYBAlter = false;
			break;

		default:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("this ALTER TABLE command is not yet supported")));
			break;
	}
	return handles;
}

List *
YBCPrepareAlterTable(List **subcmds,
					 int subcmds_size,
					 Oid relationId,
					 YbcPgStatement *rollbackHandle,
					 bool isPartitionOfAlteredTable)
{
	/* Appropriate lock was already taken */
	Relation	rel = relation_open(relationId, NoLock);

	if (!IsYBRelation(rel))
	{
		relation_close(rel, NoLock);
		return NULL;
	}

	List	   *handles = NIL;
	YbcPgStatement db_handle = NULL;

	HandleYBStatus(YBCPgNewAlterTable(YBCGetDatabaseOidByRelid(relationId),
									  YbGetRelfileNodeId(rel),
									  &db_handle));
	handles = lappend(handles, db_handle);
	ListCell   *lcmd;
	int			col = 1;
	bool		needs_yb_alter = false;

	for (int cmd_idx = 0; cmd_idx < subcmds_size; ++cmd_idx)
	{
		foreach(lcmd, subcmds[cmd_idx])
		{
			bool		subcmd_needs_yb_alter = false;

			handles = YBCPrepareAlterTableCmd((AlterTableCmd *) lfirst(lcmd),
											  rel, handles, &col,
											  &subcmd_needs_yb_alter, rollbackHandle,
											  isPartitionOfAlteredTable);
			needs_yb_alter |= subcmd_needs_yb_alter;
		}
	}
	relation_close(rel, NoLock);

	if (!needs_yb_alter)
	{
		return NULL;
	}

	YbTrackAlteredTableId(relationId);
	return handles;
}

void
YBCExecAlterTable(YbcPgStatement handle, Oid relationId)
{
	if (handle)
	{
		if (IsYBRelationById(relationId))
		{
			HandleYBStatus(YBCPgExecAlterTable(handle));
		}
	}
}

void
YBCRename(Oid relationId, ObjectType renameType, const char *relname,
		  const char *colname)
{
	YbcPgStatement handle = NULL;
	Oid			databaseId = YBCGetDatabaseOidByRelid(relationId);

	switch (renameType)
	{
		case OBJECT_MATVIEW:
		case OBJECT_TABLE:
		case OBJECT_INDEX:
			HandleYBStatus(YBCPgNewAlterTable(databaseId,
											  YbGetRelfileNodeIdFromRelId(relationId), &handle));
			HandleYBStatus(YBCPgAlterTableRenameTable(handle, relname));
			break;
		case OBJECT_COLUMN:
		case OBJECT_ATTRIBUTE:
			HandleYBStatus(YBCPgNewAlterTable(databaseId,
											  YbGetRelfileNodeIdFromRelId(relationId), &handle));

			HandleYBStatus(YBCPgAlterTableRenameColumn(handle, colname, relname));
			break;

		default:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("renaming this object is not yet supported")));

	}

	YBCExecAlterTable(handle, relationId);
}

void
YBCAlterTableNamespace(Form_pg_class classForm, Oid relationId)
{
	YbcPgStatement handle = NULL;
	Oid			databaseId = YBCGetDatabaseOidByRelid(relationId);

	switch (classForm->relkind)
	{
		case RELKIND_MATVIEW:	/* materialized view */
		case RELKIND_RELATION:	/* ordinary table */
		case RELKIND_INDEX:		/* secondary index */
		case RELKIND_PARTITIONED_TABLE: /* partitioned table */
		case RELKIND_PARTITIONED_INDEX: /* partitioned index */
			HandleYBStatus(YBCPgNewAlterTable(databaseId,
											  YbGetRelfileNodeIdFromRelId(relationId),
											  &handle));
			HandleYBStatus(YBCPgAlterTableSetSchema(handle,
													get_namespace_name(classForm->relnamespace)));
			break;

		default:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("schema altering for this object is not yet supported")));
	}

	YBCExecAlterTable(handle, relationId);
}

void
YBCDropIndex(Relation index)
{
	YbcTableProperties yb_props = YbTryGetTableProperties(index);

	if (!yb_props)
	{
		/* Index was not found on YB side, nothing to do */
		return;
	}
	/*
	 * However, since we were likely hitting the cache, we still need to
	 * safeguard against NotFound errors.
	 */

	YbcPgStatement handle;
	Oid			indexRelfileNodeId = YbGetRelfileNodeId(index);
	Oid			databaseId = YBCGetDatabaseOid(index);

	/*
	 * Create table-level tombstone for colocated (via DB or tablegroup)
	 * indexes
	 */
	if (yb_props->is_colocated)
	{
		bool		not_found = false;

		HandleYBStatusIgnoreNotFound(YBCPgNewTruncateColocated(databaseId,
															   indexRelfileNodeId,
															   false,
															   &handle,
															   YB_TRANSACTIONAL),
									 &not_found);
		const bool	valid_handle = !not_found;

		if (valid_handle)
		{
			HandleYBStatusIgnoreNotFound(YBCPgDmlBindTable(handle), &not_found);
			int			rows_affected_count = 0;

			HandleYBStatusIgnoreNotFound(YBCPgDmlExecWriteOp(handle, &rows_affected_count),
										 &not_found);
		}
	}

	/* Drop the index table */
	{
		bool		not_found = false;

		HandleYBStatusIgnoreNotFound(YBCPgNewDropIndex(databaseId,
													   indexRelfileNodeId,
													   false,	/* if_exists */
													   YbDdlRollbackEnabled(),	/* ddl_rollback_enabled */
													   &handle),
									 &not_found);
		if (not_found)
			return;

		if (YbDdlRollbackEnabled())
		{
			/*
			 * The following issues a request to the YB-Master to drop the
			 * index once this transaction commits.
			 */
			HandleYBStatusIgnoreNotFound(YBCPgExecDropIndex(handle),
										 &not_found);
			return;
		}
		/*
		 * YSQL DDL Rollback is disabled. This means DocDB will not rollback
		 * the drop if the transaction ends up failing. We cannot abort drop
		 * in DocDB so postpone the execution until the rest of the statement/
		 * txn finishes executing.
		 */
		YBSaveDdlHandle(handle);
	}
}

void
YbBackfillIndex(YbBackfillIndexStmt *stmt, DestReceiver *dest)
{
	IndexInfo  *indexInfo;
	ListCell   *cell;
	Oid			heapId;
	Oid			indexId;
	Relation	heapRel;
	Relation	indexRel;
	TupOutputState *tstate;
	YbPgExecOutParam *out_param;
	Oid			save_userid;
	int			save_sec_context;
	int			save_nestlevel;
	double		index_tuples;
	Datum		values[2];
	bool		nulls[2] = {0};

	if (*YBCGetGFlags()->ysql_disable_index_backfill)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("backfill is not enabled")));

	/*
	 * Examine oid list.  Currently, we only allow it to be a single oid, but
	 * later it should handle multiple oids of indexes on the same indexed
	 * table.
	 * TODO(jason): fix from here downwards for issue #4785.
	 */
	if (list_length(stmt->oid_list) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("only a single oid is allowed in BACKFILL INDEX (see"
						" issue #4785)")));

	foreach(cell, stmt->oid_list)
	{
		indexId = lfirst_oid(cell);
	}

	heapId = IndexGetRelation(indexId, false);
	/*
	 * The backend that initiated index backfill holds the following locks:
	 * 1. ShareUpdateExclusiveLock on the main table
	 * 2. RowExclusiveLock on the index table
	 *
	 * Theoretically, the child backfill jobs need not acquire any locks on
	 * either the main table or the index. Yet, we acquire relevant locks for
	 * reading the main table and inserting rows into the index for safety.
	 */
	heapRel = table_open(heapId, AccessShareLock);

	/*
	 * Switch to the table owner's userid, so that any index functions are run
	 * as that user.  Also lock down security-restricted operations and
	 * arrange to make GUC variable changes local to this command.
	 */
	GetUserIdAndSecContext(&save_userid, &save_sec_context);
	SetUserIdAndSecContext(heapRel->rd_rel->relowner,
						   save_sec_context | SECURITY_RESTRICTED_OPERATION);
	save_nestlevel = NewGUCNestLevel();

	indexRel = index_open(indexId, RowExclusiveLock);

	indexInfo = BuildIndexInfo(indexRel);
	/*
	 * The index should be ready for writes because it should be on the
	 * BACKFILLING permission.
	 */
	Assert(indexInfo->ii_ReadyForInserts);
	indexInfo->ii_Concurrent = true;
	indexInfo->ii_BrokenHotChain = false;

	out_param = YbCreateExecOutParam();
	index_tuples = yb_index_backfill(heapRel,
									 indexRel,
									 indexInfo,
									 false,
									 stmt->bfinfo,
									 out_param);

	index_close(indexRel, RowExclusiveLock);
	table_close(heapRel, AccessShareLock);

	/* Roll back any GUC changes executed by index functions */
	AtEOXact_GUC(false, save_nestlevel);

	/* Restore userid and security context */
	SetUserIdAndSecContext(save_userid, save_sec_context);

	/* output tuple */
	tstate = begin_tup_output_tupdesc(dest, YbBackfillIndexResultDesc(stmt),
									  &TTSOpsVirtual);

	values[0] = CStringGetTextDatum(out_param->bfoutput->data);
	values[1] = Float8GetDatum(index_tuples);

	/* send it to dest */
	do_tup_output(tstate, values, nulls);
	end_tup_output(tstate);
}

TupleDesc
YbBackfillIndexResultDesc(YbBackfillIndexStmt *stmt)
{
	TupleDesc	tupdesc;

	tupdesc = CreateTemplateTupleDesc(2);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "BACKFILL SPEC",
					   TEXTOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "ROWS INSERTED",
					   FLOAT8OID, -1, 0);
	return tupdesc;
}

void
YbDropAndRecreateIndex(Oid index_oid, Oid new_rel_id, Relation old_rel,
					   AttrMap *new_to_old_attmap)
{
	Relation	index_rel = index_open(index_oid, AccessExclusiveLock);

	/* Construct the new CREATE INDEX stmt */
	IndexStmt  *index_stmt = generateClonedIndexStmt(NULL,	/* heapRel, we provide
															 * an oid instead */
													 index_rel,
													 new_to_old_attmap,
													 NULL); /* parent constraint OID
															 * pointer */

	const char *index_name = RelationGetRelationName(index_rel);
	const char *index_namespace_name = get_namespace_name(index_rel->rd_rel->relnamespace);

	index_stmt->idxname = pstrdup(index_name);

	index_close(index_rel, AccessExclusiveLock);

	/* Drop old index */

	DropStmt   *stmt = makeNode(DropStmt);

	stmt->removeType = OBJECT_INDEX;
	stmt->missing_ok = false;
	stmt->objects = list_make1(list_make2(makeString(pstrdup(index_namespace_name)),
										  makeString(pstrdup(index_name))));
	stmt->behavior = DROP_CASCADE;
	stmt->concurrent = false;

	RemoveRelations(stmt);

	/* Create the new index */

	DefineIndex(new_rel_id,
				index_stmt,
				InvalidOid,		/* no predefined OID */
				InvalidOid,		/* no parent index */
				InvalidOid,		/* no parent constraint */
				false,			/* is_alter_table */
				false,			/* check_rights */
				false,			/* check_not_in_use */
				false,			/* skip_build */
				true /* quiet */ );
}

/* ------------------------------------------------------------------------- */
/*  System validation. */
void
YBCValidatePlacements(const char *live_placement_info,
					  const char *read_replica_placement_info,
					  bool check_satisfiable)
{
	HandleYBStatus(YBCPgValidatePlacements(live_placement_info,
										   read_replica_placement_info,
										   check_satisfiable));
}

/* ------------------------------------------------------------------------- */
/*  Replication Slot Functions. */

void
YBCCreateReplicationSlot(const char *slot_name,
						 const char *plugin_name,
						 CRSSnapshotAction snapshot_action,
						 uint64_t *consistent_snapshot_time,
						 YbCRSLsnType lsn_type,
						 YbCRSOrderingMode yb_ordering_mode)
{
	YbcPgStatement handle;

	YbcPgReplicationSlotSnapshotAction repl_slot_snapshot_action;

	switch (snapshot_action)
	{
		case CRS_NOEXPORT_SNAPSHOT:
			repl_slot_snapshot_action = YB_REPLICATION_SLOT_NOEXPORT_SNAPSHOT;
			break;
		case CRS_USE_SNAPSHOT:
			repl_slot_snapshot_action = YB_REPLICATION_SLOT_USE_SNAPSHOT;
			break;
		case CRS_EXPORT_SNAPSHOT:
			repl_slot_snapshot_action = YB_REPLICATION_SLOT_EXPORT_SNAPSHOT;
	}

	/*
	 * If lsn_type is specified as HYBRID_TIME, it would be handled in the if
	 * block, otherwise for the default case when nothing is specified or when
	 * SEQUENCE is specified, the value will stay the same.
	 */
	YbcLsnType	repl_slot_lsn_type = YB_REPLICATION_SLOT_LSN_TYPE_SEQUENCE;

	YbcOrderingMode repl_slot_ordering_mode =
		YB_REPLICATION_SLOT_ORDERING_MODE_TRANSACTION;

	if (lsn_type == CRS_HYBRID_TIME)
		repl_slot_lsn_type = YB_REPLICATION_SLOT_LSN_TYPE_HYBRID_TIME;

	if (yb_ordering_mode == YB_CRS_ROW)
		repl_slot_ordering_mode = YB_REPLICATION_SLOT_ORDERING_MODE_ROW;

	HandleYBStatus(YBCPgNewCreateReplicationSlot(slot_name,
												 plugin_name,
												 MyDatabaseId,
												 repl_slot_snapshot_action,
												 repl_slot_lsn_type,
												 repl_slot_ordering_mode,
												 &handle));

	YbcStatus	status = YBCPgExecCreateReplicationSlot(handle, consistent_snapshot_time);

	if (YBCStatusIsAlreadyPresent(status))
	{
		YBCFreeStatus(status);
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("replication slot \"%s\" already exists",
						slot_name)));
	}

	if (YBCStatusIsReplicationSlotLimitReached(status))
	{
		YBCFreeStatus(status);
		ereport(ERROR,
				(errcode(ERRCODE_CONFIGURATION_LIMIT_EXCEEDED),
				 errmsg("all replication slots are in use"),
				 errhint("Free one or increase max_replication_slots.")));
	}

	HandleYBStatus(status);
}

void
YBCListSlotEntries(YbcSlotEntryDescriptor **slot_entries,
				   size_t *num_slot_entries)
{
	HandleYBStatus(YBCPgListSlotEntries(slot_entries, num_slot_entries));
}

void
YBCListReplicationSlots(YbcReplicationSlotDescriptor **replication_slots,
						size_t *numreplicationslots)
{
	HandleYBStatus(YBCPgListReplicationSlots(replication_slots,
											 numreplicationslots));
}

void
YBCGetReplicationSlot(const char *slot_name,
					  YbcReplicationSlotDescriptor **replication_slot)
{
	char		error_message[NAMEDATALEN + 64] = "";

	snprintf(error_message, sizeof(error_message),
			 "replication slot \"%s\" does not exist", slot_name);

	HandleYBStatusWithCustomErrorForNotFound(YBCPgGetReplicationSlot(slot_name,
																	 replication_slot),
											 error_message);
}

void
YBCDropReplicationSlot(const char *slot_name)
{
	YbcPgStatement handle;
	char		error_message[NAMEDATALEN + 64] = "";

	snprintf(error_message, sizeof(error_message),
			 "replication slot \"%s\" does not exist", slot_name);

	HandleYBStatus(YBCPgNewDropReplicationSlot(slot_name,
											   &handle));
	HandleYBStatusWithCustomErrorForNotFound(YBCPgExecDropReplicationSlot(handle),
											 error_message);
}

/*
 * Returns an of relfilenodes corresponding to input table_oids array.
 * It is the responsibility of the caller to allocate and free the memory for relfilenodes.
 */
static void
YBCGetRelfileNodes(Oid *table_oids, size_t num_relations, Oid *relfilenodes)
{
	for (size_t table_idx = 0; table_idx < num_relations; table_idx++)
		relfilenodes[table_idx] = YbGetRelfileNodeIdFromRelId(table_oids[table_idx]);
}

void
YBCInitVirtualWalForCDC(const char *stream_id, Oid *relations,
						size_t numrelations,
						const YbcReplicationSlotHashRange *slot_hash_range,
						uint64_t active_pid,
						Oid *publications, size_t numpublications,
						bool yb_is_pub_all_tables)
{
	Assert(MyDatabaseId);

	Oid		   *relfilenodes;

	relfilenodes = palloc(sizeof(Oid) * numrelations);
	YBCGetRelfileNodes(relations, numrelations, relfilenodes);

	HandleYBStatus(YBCPgInitVirtualWalForCDC(stream_id, MyDatabaseId, relations,
											 relfilenodes, numrelations,
											 slot_hash_range, active_pid, publications,
											 numpublications, yb_is_pub_all_tables));

	pfree(relfilenodes);
}

void
YBCGetLagMetrics(const char *stream_id, int64_t *lag_metric)
{
	HandleYBStatus(YBCPgGetLagMetrics(stream_id, lag_metric));
}

void
YBCUpdatePublicationTableList(const char *stream_id, Oid *relations,
							  size_t numrelations)
{
	Assert(MyDatabaseId);

	Oid		   *relfilenodes;

	relfilenodes = palloc(sizeof(Oid) * numrelations);
	YBCGetRelfileNodes(relations, numrelations, relfilenodes);

	HandleYBStatus(YBCPgUpdatePublicationTableList(stream_id, MyDatabaseId,
												   relations, relfilenodes,
												   numrelations));

	pfree(relfilenodes);
}

void
YBCDestroyVirtualWalForCDC()
{
	/*
	 * This is executed as part of cleanup logic. So we just treat all errors as
	 * warning. Even if this fails, the cleanup will be done once the session is
	 * expired.
	 */
	HandleYBStatusAtErrorLevel(YBCPgDestroyVirtualWalForCDC(), WARNING);
}

void
YBCGetCDCConsistentChanges(const char *stream_id,
						   YbcPgChangeRecordBatch **record_batch,
						   YbcTypeEntityProvider type_entity_provider)
{
	HandleYBStatus(YBCPgGetCDCConsistentChanges(stream_id, record_batch, type_entity_provider));
}

void
YBCUpdateAndPersistLSN(const char *stream_id, XLogRecPtr restart_lsn_hint,
					   XLogRecPtr confirmed_flush, YbcPgXLogRecPtr *restart_lsn)
{
	HandleYBStatus(YBCPgUpdateAndPersistLSN(stream_id, restart_lsn_hint,
											confirmed_flush, restart_lsn));
}

void
YBCDropColumn(Relation rel, AttrNumber attnum)
{
	TupleDesc	tupleDesc = RelationGetDescr(rel);
	Form_pg_attribute attr = TupleDescAttr(tupleDesc, attnum - 1);

	if (YbIsAttrPrimaryKeyColumn(rel, attnum))
	{
		/*
		 * In YB, dropping a primary key involves a table
		 * rewrite, so invoke the entire ALTER TABLE logic.
		 */

		/* Construct a dummy query, as we don't have the original query. */
		char	   *query_str = psprintf("ALTER TABLE %s DROP COLUMN %s",
										 quote_qualified_identifier(get_namespace_name(rel->rd_rel->relnamespace),
																	RelationGetRelationName(rel)),
										 attr->attname.data);
		RawStmt    *rawstmt = (RawStmt *) linitial(raw_parser(query_str,
															  RAW_PARSE_DEFAULT));

		/* Construct the ALTER TABLE command. */
		AlterTableCmd *cmd = makeNode(AlterTableCmd);

		cmd->subtype = AT_DropColumn;
		cmd->name = attr->attname.data;
		List	   *cmds = list_make1(cmd);

		EventTriggerAlterTableStart((Node *) rawstmt->stmt);
		AlterTableInternal(RelationGetRelid(rel), cmds, true);
		EventTriggerAlterTableEnd();

		pfree(query_str);
	}
	else
	{
		YbcPgStatement handle = NULL;

		HandleYBStatus(YBCPgNewAlterTable(YBCGetDatabaseOidByRelid(RelationGetRelid(rel)),
										  YbGetRelfileNodeId(rel),
										  &handle));
		HandleYBStatus(YBCPgAlterTableDropColumn(handle, attr->attname.data));
		HandleYBStatus(YBCPgExecAlterTable(handle));
	}
}
