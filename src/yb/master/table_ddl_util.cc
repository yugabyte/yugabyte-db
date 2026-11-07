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

#include "yb/master/table_ddl_util.h"

#include "yb/common/common_util.h"
#include "yb/common/key_encoder.h"
#include "yb/common/schema_pbutil.h"

#include "yb/master/master_error.h"

#include "yb/server/clock.h"

#include "yb/util/logging.h"
#include "yb/util/status_format.h"

namespace yb::master {

namespace {

// Given the SysTablesEntryPB for a table and 'col_name', return whether the column can be directly
// dropped or if it should be marked for deletion first and dropped later.
Result<bool> NeedTwoPhaseDeleteForColumn(const SysTablesEntryPB& pb, const std::string& col_name) {
  // Applicable only for YSQL tables.
  if (pb.table_type() != PGSQL_TABLE_TYPE) {
    return false;
  }

  // In general, mark the column for deletion first and drop it only after the transaction commits
  // to prevent data loss. However, if the table was created in the same transaction or if the
  // column was added in this transaction, then we can safely skip 2-phase deletion because the
  // column being dropped will not exist whether the transaction is a success or not, and no other
  // clients can see this column.
  if (pb.ysql_ddl_txn_verifier_state_size() == 0) {
    return true;
  }

  // ysql_ddl_txn_verifier_state is a repeated field with as many entries as number of savepoints
  // in the transaction so far. Since we don't support savepoints for DDL transactions today,
  // default to picking the first entry.
  const auto& state = pb.ysql_ddl_txn_verifier_state(0);
  if (state.contains_create_table_op()) {
    return false;
  }
  if (!state.contains_alter_table_op() || !state.has_previous_schema()) {
    return true;
  }

  // Find whether this column was added in this transaction. This should be done by comparing the
  // 'order' field which corresponds to postgres' attnum field. Comparing the name can result in
  // erroneous conditions if the column was renamed in this transaction.
  int order = -1;
  // Find the order of column to be deleted. We can find the column based on the name, as columns
  // are uniquely named.
  for (const auto& col : pb.schema().columns()) {
    if (col.name() == col_name) {
      order = col.order();
      break;
    }
  }

  SCHECK(order != -1, NotFound, "Column for deletion not found", col_name);

  // Find column with same order in previous schema.
  for (const auto& col : state.previous_schema().columns()) {
    if (col.order() == order) {
      // This column already existed before the current transaction. Therefore it is not being
      // added as part of this transaction and needs to be deleted using 2 phase deletion.
      return true;
    }
  }
  // This column was added as part of this transaction and need not be visible to any other
  // transactions. It is safe to drop right away.
  return false;
}

}  // namespace

Status ValidateCreateTableSchema(const Schema& schema) {
  if (schema.num_key_columns() <= 0) {
    return STATUS(InvalidArgument, "Must specify at least one key column",
                  MasterError(MasterErrorPB::INVALID_SCHEMA));
  }
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    if (!IsTypeAllowableInKey(schema.column(i).type_info())) {
      return STATUS(InvalidArgument, "Invalid datatype for primary key column",
                    MasterError(MasterErrorPB::INVALID_SCHEMA));
    }
  }
  return Status::OK();
}

int RequestedNumTablets(
    const CreateTableRequestPB& request, const Schema& schema, size_t num_live_tservers) {
  // Priorities: the internally specified CreateTableRequestPB::num_tablets, then the user's
  // TablePropertiesPB::num_tablets (kept in the stored schema either way), then the default.
  int num_tablets = 0;
  if (request.has_num_tablets()) {
    num_tablets = request.num_tablets();
  }
  if (num_tablets <= 0 && schema.table_properties().HasNumTablets()) {
    num_tablets = schema.table_properties().num_tablets();
  }
  if (num_tablets <= 0) {
    // The client may have read its default before any tserver heartbeated to a new leader.
    num_tablets = GetInitialNumTabletsPerTable(request.table_type(), num_live_tservers);
    LOG(INFO) << "Setting default tablets to " << num_tablets << " with " << num_live_tservers
              << " primary servers";
  }
  return num_tablets;
}

Result<std::pair<dockv::PartitionSchema, std::vector<dockv::Partition>>> CreateTablePartitions(
    const Schema& schema, int num_tablets, bool colocated, CreateTableRequestPB* request) {
  dockv::PartitionSchema partition_schema;
  std::vector<dockv::Partition> partitions;
  if (colocated) {
    RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));
    request->clear_partition_schema();
    num_tablets = 1;
  } else {
    RETURN_NOT_OK(
        dockv::PartitionSchema::FromPB(request->partition_schema(), schema, &partition_schema));
    if (request->partitions_size() > 0) {
      if (request->partitions_size() != num_tablets) {
        return STATUS(InvalidArgument, "Partitions are not defined for all tablets",
                      MasterError(MasterErrorPB::INVALID_SCHEMA));
      }
      std::string last;
      for (const auto& p : request->partitions()) {
        dockv::Partition np;
        dockv::Partition::FromPB(p, &np);
        if (np.partition_key_start() != last) {
          return STATUS(InvalidArgument, "Partitions does not cover the full partition keyspace",
                        MasterError(MasterErrorPB::INVALID_SCHEMA));
        }
        last = np.partition_key_end();
        partitions.push_back(std::move(np));
      }
    } else {
      // The tablet count is a suggestion; the partitioning decides the real count.
      RETURN_NOT_OK(partition_schema.CreatePartitions(num_tablets, &partitions));
    }
    num_tablets = narrow_cast<int>(partitions.size());
  }
  LOG(INFO) << "Set number of tablets: " << num_tablets;
  request->set_num_tablets(num_tablets);
  return std::pair{partition_schema, partitions};
}

Status ApplyAlterSteps(server::Clock* clock,
                       const TableId& table_id,
                       const SysTablesEntryPB& current_pb,
                       const AlterTableRequestPB* req,
                       Schema* new_schema,
                       ColumnId* next_col_id,
                       std::vector<DdlLogEntry>* ddl_log_entries) {
  const SchemaPB& current_schema_pb = current_pb.schema();
  Schema cur_schema;
  RETURN_NOT_OK(SchemaFromPB(current_schema_pb, &cur_schema));

  SchemaBuilder builder(cur_schema);
  if (current_pb.has_next_column_id()) {
    builder.set_next_column_id(ColumnId(current_pb.next_column_id()));
  }
  if (current_pb.has_colocated() && current_pb.colocated()) {
    if (current_schema_pb.table_properties().is_ysql_catalog_table()) {
      builder.set_cotable_id(VERIFY_RESULT(Uuid::FromHexString(req->table().table_id())));
    }
    // Colocation ID is set in schema and cannot be altered.
  }

  for (const AlterTableRequestPB::Step& step : req->alter_schema_steps()) {
    auto time = clock->Now();
    switch (step.type()) {
      case AlterTableRequestPB::ADD_COLUMN: {
        if (!step.has_add_column()) {
          return STATUS(InvalidArgument, "ADD_COLUMN missing column info");
        }

        // Verify that encoding is appropriate for the new column's type.
        ColumnSchemaPB new_col_pb = step.add_column().schema();
        if (new_col_pb.has_id()) {
          return STATUS_FORMAT(InvalidArgument,
              "column $0: client should not specify column id", new_col_pb.ShortDebugString());
        }
        ColumnSchema new_col = ColumnSchemaFromPB(new_col_pb);

        RETURN_NOT_OK(builder.AddColumn(new_col));
        ddl_log_entries->emplace_back(time, table_id, current_pb, Format("Add column $0", new_col));
        break;
      }

      case AlterTableRequestPB::DROP_COLUMN: {
        if (!step.has_drop_column()) {
          return STATUS(InvalidArgument, "DROP_COLUMN missing column info");
        }

        const std::string& col_name = step.drop_column().name();
        if (cur_schema.is_key_column(col_name)) {
          return STATUS(InvalidArgument, "cannot remove a key column");
        }

        if (req->ysql_yb_ddl_rollback_enabled() &&
            VERIFY_RESULT(NeedTwoPhaseDeleteForColumn(current_pb, col_name))) {
          RETURN_NOT_OK(builder.MarkColumnForDeletion(col_name));
          ddl_log_entries->emplace_back(
              time, table_id, current_pb,
              Format("Mark column $0 for deletion", col_name));
        } else {
          RETURN_NOT_OK(builder.RemoveColumn(col_name));
          ddl_log_entries->emplace_back(
              time, table_id, current_pb, Format("Drop column $0", col_name));
        }
        break;
      }

      case AlterTableRequestPB::RENAME_COLUMN: {
        if (!step.has_rename_column()) {
          return STATUS(InvalidArgument, "RENAME_COLUMN missing column info");
        }

        RETURN_NOT_OK(builder.RenameColumn(
            step.rename_column().old_name(),
            step.rename_column().new_name()));
        ddl_log_entries->emplace_back(
            time, table_id, current_pb,
            Format("Rename column $0 => $1", step.rename_column().old_name(),
                   step.rename_column().new_name()));
        break;
      }

      case AlterTableRequestPB::SET_COLUMN_PG_TYPE: {
        if (!step.has_set_column_pg_type()) {
          return STATUS(InvalidArgument, "SET_COLUMN_PG_TYPE missing column info");
        }

        RETURN_NOT_OK(builder.SetColumnPGType(
            step.set_column_pg_type().name(), step.set_column_pg_type().pg_type_oid()));
        ddl_log_entries->emplace_back(
            time, table_id, current_pb,
            Format(
                "Set pg_type_oid for column $0 => $1", step.set_column_pg_type().name(),
                step.set_column_pg_type().pg_type_oid()));
        break;
      }

        // TODO: EDIT_COLUMN.

      default: {
        return STATUS_FORMAT(InvalidArgument, "Invalid alter step type: $0", step.type());
      }
    }
  }

  if (req->has_alter_properties()) {
    RETURN_NOT_OK(builder.AlterProperties(req->alter_properties()));
  }

  *new_schema = builder.Build();
  *next_col_id = builder.next_column_id();
  return Status::OK();
}

}  // namespace yb::master
