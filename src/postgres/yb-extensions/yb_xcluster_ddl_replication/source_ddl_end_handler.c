// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License.  You may obtain a copy
// of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
// License for the specific language governing permissions and limitations under
// the License.

#include "source_ddl_end_handler.h"

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
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_operator_d.h"
#include "catalog/pg_opclass_d.h"
#include "catalog/pg_opfamily_d.h"
#include "catalog/pg_policy_d.h"
#include "catalog/pg_proc_d.h"
#include "catalog/pg_rewrite_d.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_trigger_d.h"
#include "catalog/pg_ts_config_d.h"
#include "catalog/pg_ts_config_map_d.h"
#include "catalog/pg_ts_dict_d.h"
#include "catalog/pg_ts_parser_d.h"
#include "catalog/pg_ts_template_d.h"
#include "catalog/pg_type_d.h"
#include "catalog/pg_user_mapping_d.h"
#include "executor/spi.h"
#include "json_util.h"
#include "lib/stringinfo.h"

#include "extension_util.h"
#include "pg_yb_utils.h"
#include "tcop/cmdtag.h"
#include "utils/jsonb.h"
#include "utils/palloc.h"
#include "utils/rel.h"

#define DDL_END_OBJID_COLUMN_ID		  1
#define DDL_END_COMMAND_TAG_COLUMN_ID 2

#define SQL_DROP_CLASS_ID_COLUMN_ID	   1
#define SQL_DROP_IS_TEMP_COLUMN_ID	   2
#define SQL_DROP_OBJECT_TYPE_COLUMN_ID 3
#define SQL_DROP_SCHEMA_NAME_COLUMN_ID 4
#define SQL_DROP_OBJECT_NAME_COLUMN_ID 5

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
	X(CMDTAG_CREATE_TYPE) \
	X(CMDTAG_CREATE_USER_MAPPING) \
	X(CMDTAG_CREATE_VIEW) \
	X(CMDTAG_ALTER_AGGREGATE) \
	X(CMDTAG_ALTER_CAST) \
	X(CMDTAG_ALTER_COLLATION) \
	X(CMDTAG_ALTER_DOMAIN) \
	X(CMDTAG_ALTER_EXTENSION) \
	X(CMDTAG_ALTER_FUNCTION) \
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
	X(CMDTAG_ALTER_TYPE) \
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
	X(CMDTAG_DROP_POLICY) \
	X(CMDTAG_DROP_PROCEDURE) \
	X(CMDTAG_DROP_ROUTINE) \
	X(CMDTAG_DROP_RULE) \
	X(CMDTAG_DROP_SCHEMA) \
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

typedef struct NewRelMapEntry
{
	Oid relfile_oid;
	char *rel_name;
} NewRelMapEntry;

Oid
SPI_GetOid(HeapTuple spi_tuple, int column_id)
{
	bool is_null;
	Oid oid = DatumGetObjectId(
		SPI_getbinval(spi_tuple, SPI_tuptable->tupdesc, column_id, &is_null));
	if (is_null)
		elog(ERROR, "Found NULL value when parsing oid (column %d)", column_id);
	return oid;
}

const char *
SPI_GetText(HeapTuple spi_tuple, int column_id)
{
	return SPI_getvalue(spi_tuple, SPI_tuptable->tupdesc, column_id);
}

bool
SPI_GetBool(HeapTuple spi_tuple, int column_id)
{
	bool is_null;
	bool val = DatumGetBool(
		SPI_getbinval(spi_tuple, SPI_tuptable->tupdesc, column_id, &is_null));
	if (is_null)
		elog(ERROR, "Found NULL value when parsing bool (column %d)", column_id);
	return val;
}

bool
IsPrimaryIndex(Relation rel)
{
	return (rel->rd_rel->relkind == RELKIND_INDEX ||
			rel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX) &&
		   rel->rd_index && rel->rd_index->indisprimary;
}

bool
IsPassThroughDdlCommandSupported(CommandTag command_tag)
{
	switch (command_tag)
	{
		#define X(CMD_TAG_VALUE) case CMD_TAG_VALUE: return true;
		ALLOWED_DDL_LIST
		#undef X
		default: return false;
	}

	return false;
}

bool
IsPassThroughDdlSupported(const char *command_tag_name)
{
	if (command_tag_name == NULL || *command_tag_name == '\0')
		return false;

	CommandTag command_tag = GetCommandTagEnum(command_tag_name);
	return IsPassThroughDdlCommandSupported(command_tag);
}

bool
ShouldReplicateCreateRelation(Oid rel_oid, List **new_rel_list)
{
	Relation rel = RelationIdGetRelation(rel_oid);
	if (!rel)
		elog(ERROR, "Could not find relation with oid %d", rel_oid);
	// Ignore temporary tables.
	if (!IsYBBackedRelation(rel))
	{
		RelationClose(rel);
		return false;
	}
	// Primary indexes are YB-backed, but don't have table properties.
	if (IsPrimaryIndex(rel))
	{
		RelationClose(rel);
		return true;
	}

	// Also need to disallow colocated objects until that is supported.
	YbTableProperties table_props = YbGetTableProperties(rel);
	bool is_colocated = table_props->is_colocated;
	RelationClose(rel);
	if (is_colocated)
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("Colocated objects are not yet supported by "
							   "yb_xcluster_ddl_replication\n%s",
							   kManualReplicationErrorMsg)));

	// Add the new relation to the list of relations to replicate.
	NewRelMapEntry *new_rel_entry = palloc(sizeof(struct NewRelMapEntry));
	new_rel_entry->relfile_oid = YbGetRelfileNodeId(rel);
	new_rel_entry->rel_name = pstrdup(RelationGetRelationName(rel));

	*new_rel_list = lappend(*new_rel_list, new_rel_entry);

	return true;
}

bool
ShouldReplicateAlterReplication(Oid rel_oid)
{
	Relation rel = RelationIdGetRelation(rel_oid);
	if (!rel)
		elog(ERROR, "Could not find relation with oid %d", rel_oid);
	// Ignore temporary tables.
	if (!IsYBBackedRelation(rel))
	{
		RelationClose(rel);
		return false;
	}
	// Primary indexes are YB-backed, but don't have table properties.
	if (IsPrimaryIndex(rel))
	{
		RelationClose(rel);
		return true;
	}

	// Also need to disallow colocated objects until that is supported.
	YbTableProperties table_props = YbGetTableProperties(rel);
	bool is_colocated = table_props->is_colocated;
	RelationClose(rel);
	if (is_colocated && !TEST_AllowColocatedObjects)
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("Colocated objects are not yet supported by "
							   "yb_xcluster_ddl_replication\n%s",
							   kManualReplicationErrorMsg)));
	return true;
}

bool
ProcessSourceEventTriggerDDLCommands(JsonbParseState *state)
{
	StringInfoData query_buf;
	initStringInfo(&query_buf);
	appendStringInfo(&query_buf, "SELECT objid, command_tag FROM "
								 "pg_catalog.pg_event_trigger_ddl_commands()");
	int exec_res = SPI_execute(query_buf.data, /* readonly */ true, /* tcount */ 0);
	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	// As long as there is at least one command that needs to be replicated, we
	// will set this to true and replicate the entire query string.
	List *new_rel_list = NIL;
	bool should_replicate_ddl = false;
	for (int row = 0; row < SPI_processed; row++)
	{
		HeapTuple spi_tuple = SPI_tuptable->vals[row];
		Oid obj_id = SPI_GetOid(spi_tuple, DDL_END_OBJID_COLUMN_ID);
		const char *command_tag_name =
			SPI_GetText(spi_tuple, DDL_END_COMMAND_TAG_COLUMN_ID);
		CommandTag command_tag = GetCommandTagEnum(command_tag_name);

		if (command_tag == CMDTAG_CREATE_TABLE ||
			command_tag == CMDTAG_CREATE_INDEX)
		{
			should_replicate_ddl |=
				ShouldReplicateCreateRelation(obj_id, &new_rel_list);
		}
		else if (command_tag == CMDTAG_ALTER_TABLE ||
				 command_tag == CMDTAG_ALTER_INDEX)
		{
			// TODO(jhe): May need finer grained control over ALTER TABLE commands.
			should_replicate_ddl |= ShouldReplicateAlterReplication(obj_id);
		}
		else if (IsPassThroughDdlSupported(command_tag_name))
		{
			should_replicate_ddl = true;
		}
		else
		{
			elog(ERROR, "Unsupported DDL: %s\n%s", command_tag_name,
				 kManualReplicationErrorMsg);
		}
	}

	if (new_rel_list)
	{
		// Add the new_rel_map to the JSON output.
		AddJsonKey(state, "new_rel_map");
		(void) pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);

		ListCell *l;
		foreach (l, new_rel_list)
		{
			NewRelMapEntry *entry = (NewRelMapEntry *) lfirst(l);

			(void) pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
			AddNumericJsonEntry(state, "relfile_oid", entry->relfile_oid);
			AddStringJsonEntry(state, "rel_name", entry->rel_name);
			(void) pushJsonbValue(&state, WJB_END_OBJECT, NULL);

			pfree(entry->rel_name);
			pfree(entry);
		}

		(void) pushJsonbValue(&state, WJB_END_ARRAY, NULL);
	}

	return should_replicate_ddl;
}

bool
ProcessSourceEventTriggerDroppedObjects()
{
	StringInfoData query_buf;
	initStringInfo(&query_buf);
	appendStringInfo(&query_buf, "SELECT classid, is_temporary, "
								 "object_type, schema_name, object_name FROM "
								 "pg_catalog.pg_event_trigger_dropped_objects()");
	int exec_res = SPI_execute(query_buf.data, /* readonly */ true, /* tcount */ 0);
	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	// As long as there is at least one command that needs to be replicated, we
	// will set this to true and replicate the entire query string.
	bool should_replicate_ddl = false;
	bool found_temp = false;
	for (int row = 0; row < SPI_processed; row++)
	{
		HeapTuple spi_tuple = SPI_tuptable->vals[row];
		Oid class_id = SPI_GetOid(spi_tuple, SQL_DROP_CLASS_ID_COLUMN_ID);
		bool is_temp = SPI_GetBool(spi_tuple, SQL_DROP_IS_TEMP_COLUMN_ID);
		if (is_temp)
		{
			found_temp = true;
			continue;
		}

		switch (class_id)
		{
			case RelationRelationId:
				/*
				 * Since this trigger only happens after the objects are already
				 * deleted, there is not that much that we can validate here.
				 * If required for certain checks, we could:
				 * - make a call to yb-master for any docdb metadata via pggate.
				 * - or could modify pg_event_trigger_dropped_objects / create
				 *   yb_event_trigger_dropped_objects to provide the data we require.
				 */

				/*
				 * TODO(#22320) - For now we aggressively block any drops of
				 * relations in a colocated database, including non-colocated tables.
				 */
				if (MyDatabaseColocated)
					ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
									errmsg("Colocated objects are not yet "
										   "supported by yb_xcluster_ddl_replication\n%s",
										   kManualReplicationErrorMsg)));
				switch_fallthrough();
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
			default:
			{
				const char *object_type =
					SPI_GetText(spi_tuple, SQL_DROP_OBJECT_TYPE_COLUMN_ID);
				const char *schema_name =
					SPI_GetText(spi_tuple, SQL_DROP_SCHEMA_NAME_COLUMN_ID);
				const char *object_name =
					SPI_GetText(spi_tuple, SQL_DROP_OBJECT_NAME_COLUMN_ID);
				elog(ERROR,
					 "Unsupported Drop DDL for xCluster replicated DB: %s "
					 "(class_id: %d), object_name: %s.%s\n%s",
					 object_type, class_id, schema_name, object_name,
					 kManualReplicationErrorMsg);
			}
		}
	}

	if (found_temp && should_replicate_ddl)
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("Unsupported DROP command, found mix of "
							   "temporary and persisted objects in DDL command.\n%s",
							   kManualReplicationErrorMsg)));

	return should_replicate_ddl;
}
