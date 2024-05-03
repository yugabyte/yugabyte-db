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

#include "executor/spi.h"
#include "lib/stringinfo.h"

#include "extension_util.h"
#include "pg_yb_utils.h"

#define OBJID_COLUMN_ID		  1
#define COMMAND_TAG_COLUMN_ID 2

bool
ShouldReplicateCreateRelation(Oid rel_oid)
{
	Relation rel = RelationIdGetRelation(rel_oid);
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

	return true;
}

bool
ProcessSourceEventTriggerDDLCommands(JsonbParseState *state)
{
	StringInfoData query_buf;
	initStringInfo(&query_buf);
	appendStringInfo(&query_buf, "SELECT objid, command_tag FROM "
								 "pg_catalog.pg_event_trigger_ddl_commands()");
	int exec_res = SPI_execute(query_buf.data, true, 0);
	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	TupleDesc spiTupDesc = SPI_tuptable->tupdesc;

	// As long as there is at least one command that needs to be replicated, we
	// will set this to true and replicate the entire query string.
	bool should_replicate_ddl = false;
	for (int row = 0; row < SPI_processed; row++)
	{
		HeapTuple spiTuple = SPI_tuptable->vals[row];
		bool is_null;
		Oid objid = DatumGetObjectId(
			SPI_getbinval(spiTuple, spiTupDesc, OBJID_COLUMN_ID, &is_null));
		if (is_null)
			elog(ERROR, "Found NULL value when parsing objid");

		const char *command_tag =
			SPI_getvalue(spiTuple, spiTupDesc, COMMAND_TAG_COLUMN_ID);

		if (strncmp(command_tag, "CREATE TABLE", 12) == 0 ||
			strncmp(command_tag, "CREATE INDEX", 12) == 0)
		{
			should_replicate_ddl |= ShouldReplicateCreateRelation(objid);
		}
		else
		{
			elog(ERROR, "Unsupported DDL: %s\n%s", command_tag,
				 kManualReplicationErrorMsg);
		}
	}

	return should_replicate_ddl;
}
