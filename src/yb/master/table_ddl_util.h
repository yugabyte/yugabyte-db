// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

// The schema and partition steps of CREATE TABLE and ALTER TABLE, shared by CatalogManager and the
// Rust master's host (src/yb/master_host), which runs them for a Rust leader. Errors of the CREATE
// TABLE steps carry the MasterError code the client gets.

#pragma once

#include <utility>
#include <vector>

#include "yb/common/common_types.pb.h"
#include "yb/common/schema.h"

#include "yb/dockv/partition.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/server/server_fwd.h"

#include "yb/util/result.h"

namespace yb::master {

// At least one key column, and only key column types that can be keys.
Status ValidateCreateTableSchema(const Schema& schema);

// The tablet count asked for: the request's num_tablets, else the schema's table property, else
// the default for `num_live_tservers` tservers in the table's placement.
int RequestedNumTablets(
    const CreateTableRequestPB& request, const Schema& schema, size_t num_live_tservers);

// The table's partition schema and partitions: the request's partitions when it lists them, else
// `num_tablets` computed ones (one for a colocated table). Sets request->num_tablets to the
// partition count and, for a colocated table, clears its partition schema.
Result<std::pair<dockv::PartitionSchema, std::vector<dockv::Partition>>> CreateTablePartitions(
    const Schema& schema, int num_tablets, bool colocated, CreateTableRequestPB* request);

// The new schema and next column id of `current_pb` after the request's alter steps and
// properties, with one DDL log entry per step.
Status ApplyAlterSteps(server::Clock* clock,
                       const TableId& table_id,
                       const SysTablesEntryPB& current_pb,
                       const AlterTableRequestPB* req,
                       Schema* new_schema,
                       ColumnId* next_col_id,
                       std::vector<DdlLogEntry>* ddl_log_entries);

}  // namespace yb::master
