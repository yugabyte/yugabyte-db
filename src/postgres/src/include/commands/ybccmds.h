/*--------------------------------------------------------------------------------------------------
 *
 * ybccmds.h
 *	  prototypes for ybccmds.c
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
 * src/include/commands/ybccmds.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#pragma once

#include "access/htup.h"
#include "catalog/dependency.h"
#include "catalog/objectaddress.h"
#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"
#include "storage/lock.h"
#include "utils/relcache.h"
#include "tcop/utility.h"

#include "yb/yql/pggate/ybc_pggate.h"

/*  Database Functions -------------------------------------------------------------------------- */

extern void YBCCreateDatabase(
	Oid dboid, const char *dbname, Oid src_dboid, Oid next_oid, bool colocated,
	bool *retry_on_oid_collision);

extern void YBCDropDatabase(Oid dboid, const char *dbname);

extern void YBCReserveOids(Oid dboid, Oid next_oid, uint32 count, Oid *begin_oid, Oid *end_oid);

/*  Tablegroup Functions ------------------------------------------------------------------------ */

extern void YBCCreateTablegroup(Oid grpoid, Oid tablespace_oid);

extern void YBCDropTablegroup(Oid grpoid);

/*  Table Functions ----------------------------------------------------------------------------- */

extern void YBCCreateTable(CreateStmt *stmt,
						   char relkind,
						   TupleDesc desc,
						   Oid relationId,
						   Oid namespaceId,
						   Oid tablegroupId,
						   Oid colocationId,
						   Oid tablespaceId,
						   Oid matviewPgTableId);

extern void YBCDropTable(Relation rel);

extern void YbTruncate(Relation rel);

extern void YBCCreateIndex(const char *indexName,
						   IndexInfo *indexInfo,
						   TupleDesc indexTupleDesc,
						   int16 *coloptions,
						   Datum reloptions,
						   Oid indexId,
						   Relation rel,
						   OptSplit *split_options,
						   const bool skip_index_backfill,
						   bool is_colocated,
						   Oid tablegroupId,
						   Oid colocationId,
						   Oid tablespaceId);

extern void YBCDropIndex(Relation index);

extern List* YBCPrepareAlterTable(List** subcmds,
										   int subcmds_size,
										   Oid relationId,
										   YBCPgStatement *rollbackHandle,
										   bool isPartitionOfAlteredTable);

extern void YBCExecAlterTable(YBCPgStatement handle, Oid relationId);

extern void YBCRename(RenameStmt* stmt, Oid relationId);

extern void YbBackfillIndex(BackfillIndexStmt *stmt, DestReceiver *dest);

extern TupleDesc YbBackfillIndexResultDesc(BackfillIndexStmt *stmt);

extern void YbDropAndRecreateIndex(Oid indexOid, Oid relId, Relation oldRel, AttrNumber *newToOldAttmap);

extern void YBCDropSequence(Oid sequence_oid);

/*  System Validation -------------------------------------------------------------------------- */
extern void YBCValidatePlacement(const char *placement_info);

/*  Replication Slot Functions ------------------------------------------------------------------ */

extern void YBCCreateReplicationSlot(const char *slot_name);

extern void
YBCListReplicationSlots(YBCReplicationSlotDescriptor **replication_slots,
						size_t *numreplicationslots);

extern void 
YBCGetReplicationSlotStatus(const char *slot_name, 
							bool *active);

extern void YBCDropReplicationSlot(const char *slot_name);
