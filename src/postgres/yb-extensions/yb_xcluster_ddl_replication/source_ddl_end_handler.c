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

#include "catalog/pg_attrdef_d.h"
#include "catalog/pg_constraint_d.h"
#include "catalog/pg_type_d.h"
#include "executor/spi.h"
#include "json_util.h"
#include "lib/stringinfo.h"

#include "extension_util.h"
#include "pg_yb_utils.h"
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
AreCommandTagsEqual(const char *command_tag1, const char *command_tag2)
{
	size_t len = strlen(command_tag1);
	if (len != strlen(command_tag2))
		return false;
	return strncmp(command_tag1, command_tag2, len) == 0;
}

bool
ShouldReplicateCreateRelation(Oid rel_oid, List **new_rel_list)
{
	Relation rel = RelationIdGetRelation(rel_oid);
	if (!rel)
		elog(ERROR, "Could not find relation with oid %d", rel_oid);

	// Ignore temporary tables and primary indexes (same as main table).
	if (!IsYBBackedRelation(rel) ||
		(rel->rd_rel->relkind == RELKIND_INDEX && rel->rd_index->indisprimary))
	{
		RelationClose(rel);
		return false;
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
ProcessSourceEventTriggerDDLCommands(JsonbParseState *state)
{
	StringInfoData query_buf;
	initStringInfo(&query_buf);
	appendStringInfo(&query_buf, "SELECT objid, command_tag FROM "
								 "pg_catalog.pg_event_trigger_ddl_commands()");
	int exec_res = SPI_execute(query_buf.data, /*readonly*/ true, /*tcount*/ 0);
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
		const char *command_tag =
			SPI_GetText(spi_tuple, DDL_END_COMMAND_TAG_COLUMN_ID);

		/*
		 * TODO(jhe): Need a better way of handling all these DDL types. Perhaps
		 * can mimic CreateCommandTag and return a custom enum instead, thus
		 * allowing for switch cases here.
		 */
		if (AreCommandTagsEqual(command_tag, "CREATE TABLE") ||
			AreCommandTagsEqual(command_tag, "CREATE INDEX"))
		{
			should_replicate_ddl |=
				ShouldReplicateCreateRelation(obj_id, &new_rel_list);
		}
		else
		{
			elog(ERROR, "Unsupported DDL: %s\n%s", command_tag,
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
	int exec_res = SPI_execute(query_buf.data, /*readonly*/ true, /*tcount*/ 0);
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
			case AttrDefaultRelationId:
			case ConstraintRelationId:
			case TypeRelationId:
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
