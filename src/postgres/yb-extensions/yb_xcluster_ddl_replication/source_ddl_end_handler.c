/* Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 *-----------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/partition.h"
#include "catalog/pg_am_d.h"
#include "catalog/pg_amop_d.h"
#include "catalog/pg_amproc_d.h"
#include "catalog/pg_attrdef_d.h"
#include "catalog/pg_cast_d.h"
#include "catalog/pg_collation_d.h"
#include "catalog/pg_constraint_d.h"
#include "catalog/pg_conversion_d.h"
#include "catalog/pg_extension_d.h"
#include "catalog/pg_foreign_data_wrapper_d.h"
#include "catalog/pg_foreign_server_d.h"
#include "catalog/pg_foreign_table_d.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_opclass_d.h"
#include "catalog/pg_operator_d.h"
#include "catalog/pg_opfamily_d.h"
#include "catalog/pg_policy_d.h"
#include "catalog/pg_proc_d.h"
#include "catalog/pg_publication_d.h"
#include "catalog/pg_publication_namespace_d.h"
#include "catalog/pg_publication_rel_d.h"
#include "catalog/pg_rewrite_d.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_subscription_d.h"
#include "catalog/pg_subscription_rel_d.h"
#include "catalog/pg_trigger_d.h"
#include "catalog/pg_ts_config_d.h"
#include "catalog/pg_ts_config_map_d.h"
#include "catalog/pg_ts_dict_d.h"
#include "catalog/pg_ts_parser_d.h"
#include "catalog/pg_ts_template_d.h"
#include "catalog/pg_type_d.h"
#include "catalog/pg_user_mapping_d.h"
#include "executor/spi.h"
#include "extension_util.h"
#include "json_util.h"
#include "lib/stringinfo.h"
#include "pg_yb_utils.h"
#include "source_ddl_end_handler.h"
#include "tcop/cmdtag.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/palloc.h"
#include "utils/rel.h"

#define DDL_END_CLASSID_COLUMN_ID	  1
#define DDL_END_OBJID_COLUMN_ID		  2
#define DDL_END_COMMAND_TAG_COLUMN_ID 3
#define DDL_END_SCHEMA_NAME_COLUMN_ID 4
#define DDL_END_COMMAND_COLUMN_ID     5

#define SQL_DROP_CLASS_ID_COLUMN_ID	     1
#define SQL_DROP_IS_TEMP_COLUMN_ID	     2
#define SQL_DROP_OBJECT_TYPE_COLUMN_ID   3
#define SQL_DROP_SCHEMA_NAME_COLUMN_ID   4
#define SQL_DROP_OBJECT_NAME_COLUMN_ID   5
#define SQL_DROP_ADDRESS_NAMES_COLUMN_ID 6

#define TABLE_REWRITE_OBJID_COLUMN_ID 1

static List *rewritten_table_oid_list = NIL;

/* DDLs ignored for replication. */
#define IGNORED_DDL_LIST \
	X(CMDTAG_ALTER_PUBLICATION) \
	X(CMDTAG_ALTER_SUBSCRIPTION) \
	X(CMDTAG_CREATE_PUBLICATION) \
	X(CMDTAG_CREATE_SUBSCRIPTION) \
	X(CMDTAG_DROP_PUBLICATION) \
	X(CMDTAG_DROP_SUBSCRIPTION)

#define ALLOWED_DDL_LIST \
	X(CMDTAG_COMMENT) \
	X(CMDTAG_CREATE_ACCESS_METHOD) \
	X(CMDTAG_CREATE_AGGREGATE) \
	X(CMDTAG_CREATE_CAST) \
	X(CMDTAG_CREATE_COLLATION) \
	X(CMDTAG_CREATE_DOMAIN) \
	X(CMDTAG_CREATE_EXTENSION) \
	X(CMDTAG_CREATE_FOREIGN_DATA_WRAPPER) \
	X(CMDTAG_CREATE_FOREIGN_TABLE) \
	X(CMDTAG_CREATE_FUNCTION) \
	X(CMDTAG_CREATE_OPERATOR) \
	X(CMDTAG_CREATE_OPERATOR_CLASS) \
	X(CMDTAG_CREATE_OPERATOR_FAMILY) \
	X(CMDTAG_CREATE_POLICY) \
	X(CMDTAG_CREATE_PROCEDURE) \
	X(CMDTAG_CREATE_ROUTINE) \
	X(CMDTAG_CREATE_RULE) \
	X(CMDTAG_CREATE_SCHEMA) \
	X(CMDTAG_CREATE_SERVER) \
	X(CMDTAG_CREATE_STATISTICS) \
	X(CMDTAG_CREATE_TEXT_SEARCH_CONFIGURATION) \
	X(CMDTAG_CREATE_TEXT_SEARCH_DICTIONARY) \
	X(CMDTAG_CREATE_TEXT_SEARCH_PARSER) \
	X(CMDTAG_CREATE_TEXT_SEARCH_TEMPLATE) \
	X(CMDTAG_CREATE_TRIGGER) \
	X(CMDTAG_CREATE_USER_MAPPING) \
	X(CMDTAG_CREATE_VIEW) \
	X(CMDTAG_ALTER_AGGREGATE) \
	X(CMDTAG_ALTER_CAST) \
	X(CMDTAG_ALTER_COLLATION) \
	X(CMDTAG_ALTER_DEFAULT_PRIVILEGES) \
	X(CMDTAG_ALTER_DOMAIN) \
	X(CMDTAG_ALTER_EXTENSION) \
	X(CMDTAG_ALTER_FUNCTION) \
	X(CMDTAG_ALTER_MATERIALIZED_VIEW) \
	X(CMDTAG_ALTER_OPERATOR) \
	X(CMDTAG_ALTER_OPERATOR_CLASS) \
	X(CMDTAG_ALTER_OPERATOR_FAMILY) \
	X(CMDTAG_ALTER_POLICY) \
	X(CMDTAG_ALTER_PROCEDURE) \
	X(CMDTAG_ALTER_ROUTINE) \
	X(CMDTAG_ALTER_RULE) \
	X(CMDTAG_ALTER_SCHEMA) \
	X(CMDTAG_ALTER_STATISTICS) \
	X(CMDTAG_ALTER_TEXT_SEARCH_CONFIGURATION) \
	X(CMDTAG_ALTER_TEXT_SEARCH_DICTIONARY) \
	X(CMDTAG_ALTER_TEXT_SEARCH_PARSER) \
	X(CMDTAG_ALTER_TEXT_SEARCH_TEMPLATE) \
	X(CMDTAG_ALTER_TRIGGER) \
	X(CMDTAG_ALTER_VIEW) \
	X(CMDTAG_DROP_ACCESS_METHOD) \
	X(CMDTAG_DROP_AGGREGATE) \
	X(CMDTAG_DROP_CAST) \
	X(CMDTAG_DROP_COLLATION) \
	X(CMDTAG_DROP_DOMAIN) \
	X(CMDTAG_DROP_EXTENSION) \
	X(CMDTAG_DROP_FOREIGN_DATA_WRAPPER) \
	X(CMDTAG_DROP_FOREIGN_TABLE) \
	X(CMDTAG_DROP_FUNCTION) \
	X(CMDTAG_DROP_OPERATOR) \
	X(CMDTAG_DROP_OPERATOR_CLASS) \
	X(CMDTAG_DROP_OPERATOR_FAMILY) \
	X(CMDTAG_DROP_OWNED) \
	X(CMDTAG_DROP_POLICY) \
	X(CMDTAG_DROP_PROCEDURE) \
	X(CMDTAG_DROP_ROUTINE) \
	X(CMDTAG_DROP_RULE) \
	X(CMDTAG_DROP_SCHEMA) \
	X(CMDTAG_DROP_SEQUENCE) \
	X(CMDTAG_DROP_SERVER) \
	X(CMDTAG_DROP_STATISTICS) \
	X(CMDTAG_DROP_TEXT_SEARCH_CONFIGURATION) \
	X(CMDTAG_DROP_TEXT_SEARCH_DICTIONARY) \
	X(CMDTAG_DROP_TEXT_SEARCH_PARSER) \
	X(CMDTAG_DROP_TEXT_SEARCH_TEMPLATE) \
	X(CMDTAG_DROP_TRIGGER) \
	X(CMDTAG_DROP_TYPE) \
	X(CMDTAG_DROP_USER_MAPPING) \
	X(CMDTAG_DROP_VIEW) \
	X(CMDTAG_GRANT) \
	X(CMDTAG_IMPORT_FOREIGN_SCHEMA) \
	X(CMDTAG_REVOKE) \
	X(CMDTAG_SECURITY_LABEL)

/* Struct Definitions. */
typedef struct YbNewRelMapEntry
{
	char	   *name;
	char	   *namespace;
	Oid			relfile_oid;
	Oid			colocation_id;
	bool		is_index;
} YbNewRelMapEntry;

typedef struct YbEnumLabelMapEntry
{
	Oid			enum_oid;
	Oid			label_oid;
	char	   *label_name;
} YbEnumLabelMapEntry;

typedef struct YbNameToOidMapEntry
{
	char	   *schema;
	char	   *name;
	Oid			oid;
} YbNameToOidMapEntry;

/* Forward Declarations. */
static bool ShouldReplicateNewRelation(Oid rel_oid, List **new_rel_list, bool is_table_rewrite);
static bool ShouldReplicateTruncatedRelation(Oid rel_oid, List **new_rel_list);

bool
IsIndex(Relation rel)
{
	return (rel->rd_rel->relkind == RELKIND_INDEX ||
			rel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX);
}

bool
IsPrimaryIndex(Relation rel)
{
	return (IsIndex(rel) && rel->rd_index && rel->rd_index->indisprimary);
}

bool
IsPassThroughDdlCommandSupported(CommandTag command_tag)
{
	switch (command_tag)
	{
#define X(CMD_TAG_VALUE) case CMD_TAG_VALUE: return true;
			ALLOWED_DDL_LIST
#undef X
		default:
			return false;
	}

	return false;
}

static bool
IsIgnoredDdlCommand(CommandTag command_tag)
{
	switch (command_tag)
	{
#define X(CMD_TAG_VALUE) case CMD_TAG_VALUE: return true;
			IGNORED_DDL_LIST
#undef X
		default:
			return false;
	}

	return false;
}

bool
IsPassThroughDdlSupported(const char *command_tag_name)
{
	if (command_tag_name == NULL || *command_tag_name == '\0')
		return false;

	CommandTag	command_tag = GetCommandTagEnum(command_tag_name);

	return IsPassThroughDdlCommandSupported(command_tag);
}

static bool
IsSequence(Oid rel_oid)
{
	Relation	rel = RelationIdGetRelation(rel_oid);

	if (!rel)
		elog(ERROR, "Could not find relation with OID %d", rel_oid);

	const char	relkind = rel->rd_rel->relkind;

	RelationClose(rel);
	return relkind == RELKIND_SEQUENCE;
}

void
ReplicateAssociatedIndexes(Oid rel_oid, List **new_rel_list, bool is_table_rewrite)
{
	Relation	rel = RelationIdGetRelation(rel_oid);
	if (!rel)
		elog(ERROR, "Could not find relation with OID %u", rel_oid);
	if (IsIndex(rel))
	{
		/* Avoid recursion by only going table->index not also index->table. */
		RelationClose(rel);
		return;
	}

	List	   *indexes = RelationGetIndexList(rel);
	ListCell   *cell;

	foreach(cell, indexes)
	{
		Oid index_oid = lfirst_oid(cell);

		ShouldReplicateNewRelation(index_oid, new_rel_list, is_table_rewrite);
	}

	RelationClose(rel);
}

void
ReplicateInheritedRelations(Oid rel_oid, List **new_rel_list, bool is_table_rewrite)
{
	List	   *children = find_inheritance_children(rel_oid, NoLock);
	ListCell   *cell;

	foreach(cell, children)
	{
		Oid			child_oid = lfirst_oid(cell);

		ShouldReplicateNewRelation(child_oid, new_rel_list, is_table_rewrite);
	}
}

bool
ShouldReplicateRelationHelper(Oid rel_oid, List **new_rel_list, bool is_table_rewrite,
							  bool include_inheritance_children)
{
	char       *name;
	Oid         namespace_oid;
	char       *namespace;
	Relation	rel = RelationIdGetRelation(rel_oid);
	if (!rel)
		elog(ERROR, "Could not find relation with OID %u", rel_oid);
	name = RelationGetRelationName(rel);
	namespace_oid = RelationGetNamespace(rel);
	namespace = get_namespace_name(namespace_oid);

	/* Ignore temporary tables. */
	if (!IsYBBackedRelation(rel))
	{
		RelationClose(rel);
		return false;
	}
	/* Primary indexes are YB-backed, but don't have table properties. */
	if (IsPrimaryIndex(rel))
	{
		RelationClose(rel);
		return true;
	}

	/*
	 * Add the new relation to the list of relations to replicate if not already
	 * present.
	 */
	ListCell *cell;
	bool found = false;
	foreach (cell, *new_rel_list)
	{
		YbNewRelMapEntry *existing_entry = (YbNewRelMapEntry *) lfirst(cell);
		if (!strcmp(existing_entry->name, name) &&
			!strcmp(existing_entry->namespace, namespace))
		{
			found = true;
			break;
		}
	}
	if (!found)
	{
		YbNewRelMapEntry *new_rel_entry = palloc(sizeof(struct YbNewRelMapEntry));

		new_rel_entry->name = pstrdup(RelationGetRelationName(rel));
		new_rel_entry->namespace = pstrdup(namespace);
		new_rel_entry->relfile_oid = YbGetRelfileNodeId(rel);
		new_rel_entry->colocation_id = GetColocationIdFromRelation(&rel, is_table_rewrite);
		new_rel_entry->is_index = IsIndex(rel);

		*new_rel_list = lappend(*new_rel_list, new_rel_entry);
	}

	RelationClose(rel);

	ReplicateAssociatedIndexes(rel_oid, new_rel_list, is_table_rewrite);

	if (include_inheritance_children)
		ReplicateInheritedRelations(rel_oid, new_rel_list, is_table_rewrite);

	return true;
}

/*
 * This function handles both new relation from create table/index,
 * and also new relations as a result of table rewrites.
 * Returns whether the relation should be replicated (eg false if
 * table is a temp table or a primary key index).
 *
 * This function does not handle sequences.
 */
bool
ShouldReplicateNewRelation(Oid rel_oid, List **new_rel_list,
						   bool is_table_rewrite)
{
	return ShouldReplicateRelationHelper(rel_oid, new_rel_list, is_table_rewrite,
										 true /* include_inheritance_children */ );
}

/*
 * This function handles TRUNCATE TABLE.
 * Returns whether the relation should be replicated (eg false if
 * table is a temp table or a primary key index).
 * Child relations are explicitly included in the relation list of a
 * TRUNCATE DDL, so do not include them again.
 */
bool
ShouldReplicateTruncatedRelation(Oid rel_oid, List **new_rel_list)
{
	return ShouldReplicateRelationHelper(rel_oid, new_rel_list,
										 true /* is_table_rewrite */ ,
										 false /* include_inheritance_children */ );
}

void
HandleAlterColumnAddIndex(AlterTableCmd *subcmd, Oid rel_oid,
						  List **new_rel_list)
{
	/* Need to fetch the index oid from the index name. */
	IndexStmt  *index = (IndexStmt *) subcmd->def;

	/* Skip primary keys for YB. */
	if (index->primary)
		return;

	if (index->idxname == NULL)
	{
		/*
		 * Default name is being used. It is difficult for us to get the name
		 * that did end up getting picked for this index. In this case, we
		 * will just collect info for all indexes on the table.
		 */
		Relation	rel = RelationIdGetRelation(rel_oid);
		List	   *indexes = RelationGetIndexList(rel);

		RelationClose(rel);
		ListCell   *cell;

		foreach(cell, indexes)
		{
			Oid			index_oid = lfirst_oid(cell);

			ShouldReplicateNewRelation(index_oid, new_rel_list, /* is_table_rewrite= */ false);
		}

		list_free(indexes);
		return;
	}

	/* We do have the index name, so can use that. */
	Relation	rel = RelationIdGetRelation(rel_oid);
	Oid			index_oid = get_relname_relid(index->idxname,
											  RelationGetNamespace(rel));

	RelationClose(rel);

	if (index_oid == InvalidOid)
		elog(ERROR,
			 "Could not find index with name %s for parent relation %d",
			 index->idxname,
			 rel_oid);

	/* Once we have the OID, we can capture its info. */
	ShouldReplicateNewRelation(index_oid, new_rel_list, /* is_table_rewrite= */ false);
}

void
CheckAlterColumnTypeDDL(Oid rel_oid, CollectedCommand *cmd, List **new_rel_list,
						bool is_table_rewrite, bool is_temporary_object)
{
	/* Ignore temp objects. */
	if (is_temporary_object)
		return;

	if (cmd && cmd->type == SCT_AlterTable && cmd->d.alterTable.subcmds)
	{
		ListCell   *cell;

		foreach(cell, cmd->d.alterTable.subcmds)
		{
			AlterTableCmd *subcmd = castNode(AlterTableCmd,
											 ((CollectedATSubcmd *) lfirst(cell))->parsetree);

			switch (subcmd->subtype)
			{
				case AT_AddIndex:
				case AT_ReAddIndex:
					{
						HandleAlterColumnAddIndex(subcmd, rel_oid, new_rel_list);
						break;
					}
				default:
					break;
			}
		}
	}
}

void
ProcessRewrittenIndexes(Oid rel_oid, const char *schema_name, List **new_rel_list)
{
	Relation	rewritten_table = RelationIdGetRelation(rel_oid);

	if (!rewritten_table)
		elog(ERROR, "Could not find relation with oid %d", rel_oid);

	char	   *rewritten_table_name = pstrdup(RelationGetRelationName(rewritten_table));

	RelationClose(rewritten_table);

	StringInfoData query_buf;

	initStringInfo(&query_buf);
	appendStringInfo(&query_buf,
					 "SELECT c.oid FROM pg_class c "
					 "JOIN pg_indexes i ON c.relname = i.indexname "
					 "WHERE i.tablename = '%s' AND i.schemaname = '%s';",
					 rewritten_table_name, schema_name);

	/*
	 * Preserve current state of SPI_processed and SPI_tuptable because they
	 * will be overwritten by the next SPI_execute call. This ensures that
	 * caller's expected state remains unchanged.
	 */
	int			saved_processed = SPI_processed;
	SPITupleTable *saved_tuptable = SPI_tuptable;

	int			exec_res = SPI_execute(query_buf.data, /* readonly */ true, /* tcount */ 0);

	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	for (int i = 0; i < SPI_processed; i++)
	{
		HeapTuple	spi_tuple = SPI_tuptable->vals[i];
		Oid			rewritten_index_oid = SPI_GetOid(spi_tuple, 1);

		ShouldReplicateNewRelation(rewritten_index_oid, new_rel_list, /* is_table_rewrite */ true);
	}

	SPI_processed = saved_processed;
	SPI_tuptable = saved_tuptable;
}

void
ProcessNewRelationsList(JsonbParseState *state, List **rel_list)
{
	if (!*rel_list)
		return;

	/* Add the extra context to the JSON output. */
	AddJsonKey(state, "new_rel_map");
	(void) pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);

	ListCell   *l;

	foreach(l, *rel_list)
	{
		YbNewRelMapEntry *entry = (YbNewRelMapEntry *) lfirst(l);

		(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
		AddStringJsonEntry(state, "rel_name", entry->name);
		AddStringJsonEntry(state, "rel_namespace", entry->namespace);
		AddNumericJsonEntry(state, "relfile_oid", entry->relfile_oid);
		if (entry->colocation_id)
			AddNumericJsonEntry(state, "colocation_id", entry->colocation_id);
		if (entry->is_index)
			AddBoolJsonEntry(state, "is_index", true);
		(void) pushJsonbValue(&state, WJB_END_OBJECT, NULL);

		pfree(entry->name);
		pfree(entry);
	}

	(void) pushJsonbValue(&state, WJB_END_ARRAY, NULL);
}

bool
ShouldReplicateAlterReplication(Oid rel_oid)
{
	Relation	rel = RelationIdGetRelation(rel_oid);

	if (!rel)
		elog(ERROR, "Could not find relation with oid %d", rel_oid);
	/* Ignore temporary tables. */
	if (!IsYBBackedRelation(rel))
	{
		RelationClose(rel);
		return false;
	}

	RelationClose(rel);
	return true;
}

static void
GetEnumLabels(Oid enum_oid, List **enum_label_list)
{
	StringInfoData query;

	initStringInfo(&query);
	appendStringInfo(&query,
					 "SELECT enumlabel, oid FROM pg_catalog.pg_enum WHERE "
					 "enumtypid = %u",
					 enum_oid);
	int			exec_result = SPI_execute(query.data, /* readonly */ true, /* tcount */ 0);

	if (exec_result != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_result, query.data);
	pfree(query.data);

	for (int i = 0; i < SPI_processed; i++)
	{
		HeapTuple	tuple = SPI_tuptable->vals[i];
		TupleDesc	tupdesc = SPI_tuptable->tupdesc;

		YbEnumLabelMapEntry *enum_label_entry =
			palloc(sizeof(YbEnumLabelMapEntry));

		enum_label_entry->enum_oid = enum_oid;
		enum_label_entry->label_name =
			SPI_GetText(tuple, SPI_fnumber(tupdesc, "enumlabel"));
		enum_label_entry->label_oid =
			SPI_GetOid(tuple, SPI_fnumber(tupdesc, "oid"));
		*enum_label_list = lappend(*enum_label_list, enum_label_entry);
	}
}

static void
AddNameToOidInfo(char *schema, char *name, Oid oid,
				 List **name_to_oid_info_list)
{
	YbNameToOidMapEntry *name_to_oid_info_entry =
		palloc(sizeof(YbNameToOidMapEntry));

	name_to_oid_info_entry->name = name;
	name_to_oid_info_entry->schema = pstrdup(schema);
	name_to_oid_info_entry->oid = oid;
	*name_to_oid_info_list = lappend(*name_to_oid_info_list,
									 name_to_oid_info_entry);
}

static void
AddSequenceInfo(Oid pg_class_oid, char *schema, List **sequence_info_list)
{
	char	   *name = get_rel_name(pg_class_oid);

	if (!name)
		elog(ERROR, "Unable to find name of sequence with pg_class OID %u",
			 pg_class_oid);
	if (!schema)
		elog(ERROR, "Schema of sequence with pg_class OID %u unknown",
			 pg_class_oid);
	AddNameToOidInfo(schema, name, pg_class_oid, sequence_info_list);
}

static void
AddTypeInfo(Oid pg_type_oid, char *schema, List **type_info_list)
{
	char	   *name = get_typname(pg_type_oid);

	if (!name)
		elog(ERROR, "Unable to find name of type with pg_type OID %u",
			 pg_type_oid);
	if (!schema)
		elog(ERROR, "Schema of type with pg_type OID %u unknown",
			 pg_type_oid);
	AddNameToOidInfo(schema, name, pg_type_oid, type_info_list);
}

typedef struct YbCommandInfo
{
	Oid			class_id;
	Oid			oid;
	char	   *command_tag_name;
	char	   *schema;			/* may be NULL */
	CollectedCommand *command;
} YbCommandInfo;

static int
GetSourceEventTriggerDDLCommands(YbCommandInfo **info_array_out)
{
	StringInfoData query_buf;

	initStringInfo(&query_buf);
	appendStringInfo(&query_buf,
					 "SELECT classid, objid, command_tag, schema_name, command "
					 "FROM pg_catalog.pg_event_trigger_ddl_commands()");
	int			exec_res = SPI_execute(query_buf.data,
									   true,	/* readonly */
									   0);	/* tcount */

	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	YbCommandInfo *info_array = palloc(SPI_processed * sizeof(YbCommandInfo));
	int			num_of_rows = SPI_processed;

	for (int row = 0; row < num_of_rows; row++)
	{
		HeapTuple	spi_tuple = SPI_tuptable->vals[row];
		YbCommandInfo *info = &info_array[row];

		info->command_tag_name =
			SPI_GetText(spi_tuple, DDL_END_COMMAND_TAG_COLUMN_ID);
		CommandTag	command_tag = GetCommandTagEnum(info->command_tag_name);

		/*
		 * Only commands that don't have an oid are GRANT, REVOKE, and
		 * ALTER DEFAULT PRIVILEGES.
		 */
		if (command_tag != CMDTAG_GRANT && command_tag != CMDTAG_REVOKE
			&& command_tag != CMDTAG_ALTER_DEFAULT_PRIVILEGES)
		{
			info->class_id = SPI_GetOid(spi_tuple, DDL_END_CLASSID_COLUMN_ID);
			info->oid = SPI_GetOid(spi_tuple, DDL_END_OBJID_COLUMN_ID);
		}
		else
		{
			info->class_id = InvalidOid;
			info->oid = InvalidOid;
		}
		info->schema = SPI_GetText(spi_tuple, DDL_END_SCHEMA_NAME_COLUMN_ID);
		info->command = GetCollectedCommand(spi_tuple, DDL_END_COMMAND_COLUMN_ID);
	}

	*info_array_out = info_array;
	return num_of_rows;
}

void
PushEnumLabelMap(JsonbParseState *state, char *map_key,
				 List *enum_label_list)
{
	if (!enum_label_list)
		return;

	/*----------
	 * Add the enum_label_list to the JSON output.  We use a flat array of
	 * entries because JSON doesn't allow maps on composite values.
	 *
	 * If two entries have the same enum and label OIDs, then the
	 * remaining fields are guaranteed to be the same.
	 *----------
	 */
	AddJsonKey(state, map_key);
	(void) pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);

	ListCell   *l;

	foreach(l, enum_label_list)
	{
		YbEnumLabelMapEntry *entry = (YbEnumLabelMapEntry *) lfirst(l);

		(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
		AddNumericJsonEntry(state, "enum_oid", entry->enum_oid);
		AddStringJsonEntry(state, "label", entry->label_name);
		AddNumericJsonEntry(state, "label_oid", entry->label_oid);
		(void) pushJsonbValue(&state, WJB_END_OBJECT, NULL);

		pfree(entry->label_name);
		pfree(entry);
	}

	(void) pushJsonbValue(&state, WJB_END_ARRAY, NULL);
}

void
PushNameToOidMap(JsonbParseState *state, char *map_key,
				 List *name_to_oid_info_list)
{
	if (!name_to_oid_info_list)
		return;

	/*----------
	 * Add the name_to_oid_info_list to the JSON output.  We use a flat array
	 * of entries because JSON doesn't allow maps on composite values.
	 *
	 * If two entries have the same schema and name, then the oid field is
	 * guaranteed to be the same.
	 *----------
	 */
	AddJsonKey(state, map_key);
	(void) pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);

	ListCell   *l;

	foreach(l, name_to_oid_info_list)
	{
		YbNameToOidMapEntry *entry = (YbNameToOidMapEntry *) lfirst(l);

		(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
		AddStringJsonEntry(state, "schema", entry->schema);
		AddStringJsonEntry(state, "name", entry->name);
		AddNumericJsonEntry(state, "oid", entry->oid);
		(void) pushJsonbValue(&state, WJB_END_OBJECT, NULL);

		pfree(entry->schema);
		pfree(entry->name);
		pfree(entry);
	}

	(void) pushJsonbValue(&state, WJB_END_ARRAY, NULL);
}

void
PushVariable(JsonbParseState *state, char *guc_name)
{
	char *value = GetConfigOptionByName(guc_name, NULL, false);
	if (!value)
		return;

	AddStringJsonEntry(state, guc_name, value);
}

void
PushVariableMap(JsonbParseState *state)
{
	AddJsonKey(state, "variables");
	(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);

	/*----------
	 * To maximize safety, we always record the session variables.
	 *
	 * This protects us against the source and target clusters having
	 * different defaults and changes of which DDLs use these variables.
	 *----------
	 */

	/* This affects what tablespace a table is created in. */
	PushVariable(state, "default_tablespace");
	/* This affects whether a table is range or hash started. */
	PushVariable(state, "yb_use_hash_splitting_by_default");

	/* Settings that affect parsing of string literals. */
	PushVariable(state, "standard_conforming_strings");
	/* Settings that affect parsing of dates, times, and intervals. */
	PushVariable(state, "DateStyle");
	PushVariable(state, "TimeZone");
	PushVariable(state, "IntervalStyle");
	/* Settings that affect parsing of function bodies. */
	PushVariable(state, "check_function_bodies");

	(void) pushJsonbValue(&state, WJB_END_OBJECT, NULL);
}

bool
ProcessSourceEventTriggerDDLCommands(JsonbParseState *state)
{
	/*
	 * First copy the command information that we get by using SPI_execute so
	 * we can use SPI_execute while processing each command.  (Each
	 * SPI_execute call invalidates the results of the previous one; trying to
	 * avoid this by using SPI_connect would force us into a different memory
	 * context, which would be inconvenient .)
	 */
	YbCommandInfo *info_array;
	int			num_of_rows = GetSourceEventTriggerDDLCommands(&info_array);

	List	   *new_rel_list = NIL;
	List	   *enum_label_list = NIL;
	List	   *sequence_info_list = NIL;
	List	   *type_info_list = NIL;
	bool		found_temp = false;

	/*
	 * As long as there is at least one command that needs to be replicated, we
	 * will set this to true and replicate the entire query string.
	 */
	bool		should_replicate_ddl = false;

	for (int row = 0; row < num_of_rows; row++)
	{
		YbCommandInfo *info = &info_array[row];
		Oid			obj_id = info->oid;
		const char *command_tag_name = info->command_tag_name;
		CommandTag	command_tag = GetCommandTagEnum(info->command_tag_name);
		char	   *schema = info->schema;

		bool		is_temporary_object = IsTempSchema(schema);
		/*
		 * The above works for most objects, but in a few cases Postgres does
		 * not provide the schema; we thus have to handle those separately here.
		 */
		if (info->class_id == PolicyRelationId)
			is_temporary_object = IsTemporaryPolicy(info->oid);
		else if (info->class_id == TriggerRelationId)
			is_temporary_object = IsTemporaryTrigger(info->oid);
		else if (info->class_id == RewriteRelationId)
			is_temporary_object = IsTemporaryRule(info->oid);
		found_temp |= is_temporary_object;

		if (command_tag == CMDTAG_CREATE_TABLE ||
			command_tag == CMDTAG_CREATE_INDEX ||
			command_tag == CMDTAG_CREATE_TABLE_AS ||
			command_tag == CMDTAG_SELECT_INTO ||
			command_tag == CMDTAG_CREATE_MATERIALIZED_VIEW)
		{
			should_replicate_ddl |=
				ShouldReplicateNewRelation(obj_id, &new_rel_list, /* is_table_rewrite */ false);
		}
		else if (command_tag == CMDTAG_CREATE_TYPE ||
				 command_tag == CMDTAG_ALTER_TYPE)
		{
			if (type_is_enum(obj_id))
				GetEnumLabels(obj_id, &enum_label_list);
			AddTypeInfo(obj_id, schema, &type_info_list);
			should_replicate_ddl |= true;
		}
		else if (command_tag == CMDTAG_CREATE_SEQUENCE ||
				 command_tag == CMDTAG_ALTER_SEQUENCE)
		{
			AddSequenceInfo(obj_id, schema, &sequence_info_list);
			should_replicate_ddl |= !is_temporary_object;
		}
		else if (command_tag == CMDTAG_ALTER_TABLE &&
				 IsSequence(obj_id))
		{
			/*
			 * This is one of:
			 * - ALTER TABLE <sequence> OWNER TO ...
			 * - ALTER TABLE <sequence> RENAME TO ...
			 * - ALTER TABLE <sequence> SET SCHEMA ...
			 *
			 * None of which change sequence metadata (i.e., pg_sequence) or
			 * data.
			 */
			should_replicate_ddl |= !is_temporary_object;
		}
		else if (command_tag == CMDTAG_ALTER_TABLE &&
				 list_member_oid(rewritten_table_oid_list, obj_id))
		{
			rewritten_table_oid_list = list_delete_oid(rewritten_table_oid_list, obj_id);

			/*
			 * We need to also add the rewritten table to the new_rel_list,
			 * because in case of a table rewrite, the associated old DocDB
			 * table is discarded and a new one is created, and the relfile_oid
			 * of the table is updated to reference this new DocDB table,
			 */
			should_replicate_ddl |=
				ShouldReplicateNewRelation(obj_id, &new_rel_list, /* is_table_rewrite */ true);

			/*
			 * Add all indexes that are associated with this table, as table
			 * rewrite will also rewrite all dependent index tables.
			 */
			const char *schema_name = info->schema;

			ProcessRewrittenIndexes(obj_id, schema_name, &new_rel_list);
		}
		else if (command_tag == CMDTAG_ALTER_TABLE ||
				 command_tag == CMDTAG_ALTER_INDEX)
		{
			/* Perform additional checks on subcommands. */
			CollectedCommand *cmd = info->command;

			CheckAlterColumnTypeDDL(obj_id, cmd, &new_rel_list,
									 /* is_table_rewrite */ false,
									is_temporary_object);

			should_replicate_ddl |= ShouldReplicateAlterReplication(obj_id);
		}
		else if (command_tag == CMDTAG_TRUNCATE_TABLE)
		{
			if (!is_temporary_object)
				should_replicate_ddl |=
					ShouldReplicateTruncatedRelation(obj_id, &new_rel_list);
		}
		else if (command_tag == CMDTAG_REFRESH_MATERIALIZED_VIEW)
		{
			/* Refresh is a table rewrite. */
			should_replicate_ddl |=
				ShouldReplicateNewRelation(obj_id, &new_rel_list,
										   /* is_table_rewrite= */ true);
			const char *schema_name = info->schema;
			ProcessRewrittenIndexes(obj_id, schema_name, &new_rel_list);
		}
		else if (IsPassThroughDdlSupported(command_tag_name))
		{
			should_replicate_ddl = !is_temporary_object;
		}
		else if (IsIgnoredDdlCommand(command_tag))
		{
			continue;
		}
		else
		{
			elog(ERROR, "Unsupported DDL: %s\n%s", command_tag_name,
				 kManualReplicationErrorMsg);
		}
	}

	if (should_replicate_ddl && found_temp)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("unsupported mix of temporary and permanent objects"),
				 errdetail("This database is being replicated using xCluster "
							 "automatic mode, which does not support DDLs "
							 "that simultaneously use both temporary and "
							 "permanent objects."),
				 errhint("Using multiple commands, manipulate the temporary "
						 "objects separately from the permanent ones.")));
	}

	ProcessNewRelationsList(state, &new_rel_list);

	/*
	 * Add non-empty OID assignment maps to JSON.
	 */
	PushEnumLabelMap(state, "enum_label_info", enum_label_list);
	PushNameToOidMap(state, "sequence_info", sequence_info_list);
	PushNameToOidMap(state, "type_info", type_info_list);

	/*
	 * Record session variables that are known to meaningfully affect the DDL
	 * execution.
	 */
	PushVariableMap(state);

	return should_replicate_ddl;
}

void
ProcessSourceEventTriggerTableRewrite()
{
	StringInfoData query_buf;

	initStringInfo(&query_buf);
	appendStringInfo(&query_buf, "SELECT pg_event_trigger_table_rewrite_oid()");
	int			exec_res = SPI_execute(query_buf.data, /* readonly */ true, /* tcount */ 0);

	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	HeapTuple	spi_tuple = SPI_tuptable->vals[0];
	Oid			rewritten_table_oid = SPI_GetOid(spi_tuple, TABLE_REWRITE_OBJID_COLUMN_ID);

	/* This event trigger doesn't trigger on parent tables, so need to check. */
	Oid			parent_table_oid = InvalidOid;

	if (has_superclass(rewritten_table_oid))
		parent_table_oid = get_partition_parent(rewritten_table_oid,
												 /* even_if_detached= */ true);

	/*
	 * Add the rewritten table to the list of rewritten tables, so that ddl end
	 * event triggers can process the rewritten table.
	 */
	MemoryContext oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	rewritten_table_oid_list = lappend_oid(rewritten_table_oid_list, rewritten_table_oid);
	if (parent_table_oid != InvalidOid)
		rewritten_table_oid_list = lappend_oid(rewritten_table_oid_list, parent_table_oid);
	MemoryContextSwitchTo(oldcontext);
}

bool
ProcessSourceEventTriggerDroppedObjects(CommandTag tag)
{
	StringInfoData query_buf;

	initStringInfo(&query_buf);
	appendStringInfo(&query_buf, "SELECT classid, is_temporary, object_type, "
					 "schema_name, object_name, address_names FROM "
					 "pg_catalog.pg_event_trigger_dropped_objects()");
	int			exec_res =
		SPI_execute(query_buf.data, /* readonly */ true, /* tcount */ 0);

	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	bool		found_temp = false;
	/*
	 * As long as there is at least one command that needs to be replicated, we
	 * will set this to true and replicate the entire query string.
	 */
	bool		should_replicate_ddl = false;

	for (int row = 0; row < SPI_processed; row++)
	{
		HeapTuple	spi_tuple = SPI_tuptable->vals[row];
		Oid			class_id = SPI_GetOid(spi_tuple, SQL_DROP_CLASS_ID_COLUMN_ID);
		char	   *first_address_name = SPI_TextArrayGetElement(spi_tuple,
																 SQL_DROP_ADDRESS_NAMES_COLUMN_ID, 0);

		/* The following works for named objects */
		bool		is_temp = SPI_GetBool(spi_tuple, SQL_DROP_IS_TEMP_COLUMN_ID);

		/* And following works for unnamed objects */
		if (first_address_name && IsTempSchema(first_address_name))
			is_temp = true;

		if (is_temp)
		{
			found_temp = true;
			/* Postgres does not support temporary materialized views. */
			continue;
		}

		switch (class_id)
		{
			case AccessMethodRelationId:
			case AccessMethodOperatorRelationId:
			case AccessMethodProcedureRelationId:
			case AttrDefaultRelationId:
			case CastRelationId:
			case CollationRelationId:
			case ConstraintRelationId:
			case ConversionRelationId:
			case ExtensionRelationId:
			case ForeignDataWrapperRelationId:
			case ForeignServerRelationId:
			case ForeignTableRelationId:
			case NamespaceRelationId:
			case OperatorClassRelationId:
			case OperatorFamilyRelationId:
			case OperatorRelationId:
			case PolicyRelationId:
			case ProcedureRelationId:
			case RelationRelationId:
			case RewriteRelationId:
			case StatisticExtRelationId:
			case TriggerRelationId:
			case TSConfigRelationId:
			case TSConfigMapRelationId:
			case TSDictionaryRelationId:
			case TSParserRelationId:
			case TSTemplateRelationId:
			case TypeRelationId:
			case UserMappingRelationId:
				should_replicate_ddl = true;
				break;
			/*
			 * Ignore logical replication objects (not replicated via xCluster).
			 * Note: Schema-level publications (PublicationNamespaceRelationId)
			 * and SUBSCRIPTION are not supported yet in YB.
			 */
			case PublicationRelationId:
			case PublicationRelRelationId:
			case PublicationNamespaceRelationId:
			case SubscriptionRelationId:
			case SubscriptionRelRelationId:
				continue;
			default:
				{
					const char *object_type = SPI_GetText(spi_tuple,
														  SQL_DROP_OBJECT_TYPE_COLUMN_ID);
					const char *schema_name = SPI_GetText(spi_tuple,
														  SQL_DROP_SCHEMA_NAME_COLUMN_ID);
					const char *object_name = SPI_GetText(spi_tuple,
														  SQL_DROP_OBJECT_NAME_COLUMN_ID);

					elog(ERROR,
						 "unsupported Drop DDL for xCluster replicated DB: %s "
						 "(class_id: %d), object_name: %s.%s\n%s",
						 object_type, class_id, schema_name, object_name,
						 kManualReplicationErrorMsg);
				}
		}
	}

	if (found_temp && should_replicate_ddl)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot drop temporary and permanent objects in one command"),
				 errdetail("This database is being replicated using xCluster "
						   "automatic mode, which does not support DDLs "
						   "that simultaneously drop both temporary and "
						   "permanent objects."),
				 errhint("Drop the temporary objects separately from the "
						 "permanent ones using multiple commands.")));

	return should_replicate_ddl;
}

void
ClearRewrittenTableOidList()
{
	list_free(rewritten_table_oid_list);
	rewritten_table_oid_list = NIL;
}
