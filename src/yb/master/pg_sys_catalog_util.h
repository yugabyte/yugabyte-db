// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
//

#ifndef YB_MASTER_PG_SYS_CATALOG_UTIL_H_
#define YB_MASTER_PG_SYS_CATALOG_UTIL_H_

#include "yb/common/ql_expr.h"

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/docdb/value_type.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_fwd.h"


namespace yb {
namespace master {

Result<QLTableRow> ExtractPgYbCatalogVersionRow(const tablet::Tablet& tablet) {
  const auto* meta = tablet.metadata();
  const std::shared_ptr<tablet::TableInfo> ysql_catalog_table_info =
      VERIFY_RESULT(meta->GetTableInfo(kPgYbCatalogVersionTableId));
  const Schema& schema = ysql_catalog_table_info->schema;
  QLTableRow row;
  auto iter = VERIFY_RESULT(tablet.NewRowIterator(schema.CopyWithoutColumnIds(),
                                                  {} /* read_hybrid_time */,
                                                  kPgYbCatalogVersionTableId));
  if (VERIFY_RESULT(iter->HasNext())) {
    RETURN_NOT_OK(iter->NextRow(&row));
    RSTATUS_DCHECK(!VERIFY_RESULT(iter->HasNext()), InternalError,
                   "pg_yb_catalog_version_table should not have more than one row.");
    return row;
  }
  return STATUS(NotFound, "Row not found");
}

Status FillPgCatalogRequest(const tablet::Tablet& tablet, const TableId& table_id,
                            const QLTableRow& row, PgsqlWriteRequestPB::PgsqlStmtType op_type,
                            PgsqlWriteRequestPB* write_request) {
  const auto& table_info = VERIFY_RESULT(
      tablet.metadata()->GetTableInfo(table_id));
  const Schema& schema = table_info->schema;
  const auto schema_version = table_info->schema_version;

  write_request->set_stmt_type(op_type);
  write_request->set_client(YQL_CLIENT_PGSQL);
  write_request->set_schema_version(schema_version);
  write_request->set_table_id(table_id);
  RSTATUS_DCHECK_EQ(schema.num_hash_key_columns(), 0, Corruption,
                    "pg_catalog tables should have 0 hash key columns.");

  for (int i = 0; i < schema.num_range_key_columns(); i++) {
    const auto& value = row.GetValue(schema.column_id(i));
    RSTATUS_DCHECK(value, Corruption,
                   "Value corresponding to a column not found in pg_catalog table.");
    write_request->add_range_column_values()->mutable_value()->CopyFrom(*value);
  }
  // TODO(Sanket): Need to verify if correct for PGSQL_TRUNCATE_COLOCATED.
  if (op_type == PgsqlWriteRequestPB::PGSQL_DELETE) {
    return Status::OK();
  }

  for (int i = schema.num_range_key_columns(); i < schema.num_columns(); i++) {
    const auto& value = row.GetValue(schema.column_id(i));
    RSTATUS_DCHECK(value, Corruption,
                   "Value corresponding to a column not found in pg_catalog table.");
    PgsqlColumnValuePB* column_value;
    if (op_type == PgsqlWriteRequestPB::PGSQL_UPDATE) {
      column_value = write_request->add_column_new_values();
    } else {
      column_value = write_request->add_column_values();
    }
    column_value->set_column_id(schema.column_id(i));
    column_value->mutable_expr()->mutable_value()->CopyFrom(*value);
  }

  return Status::OK();
}

bool ArePgsqlRowsSame(const QLTableRow& row1, const QLTableRow& row2, const Schema& schema) {
  for (int i = 0; i < schema.num_columns(); i++) {
    if (!row1.MatchColumn(schema.column_id(i), row2)) {
      return false;
    }
  }
  return true;
}

using Process = std::function<Status(const QLTableRow&, PgsqlWriteRequestPB_PgsqlStmtType)>;

Status ComputePgCatalogTableDifferenceBetweenCurrentAndPast(
    const tablet::Tablet& tablet, const TabletId& table_id, ReadHybridTime ht,
    SnapshotScheduleRestoration* restore, const Process& process) {
  const auto& table_info = VERIFY_RESULT(
      tablet.metadata()->GetTableInfo(table_id));
  const Schema& schema = table_info->schema;

  ModifiedPgCatalogTable details;
  details.name = table_info->table_name;

  auto curr_iter =  down_pointer_cast<docdb::DocRowwiseIterator>(
      VERIFY_RESULT(tablet.NewRowIterator(schema.CopyWithoutColumnIds(), {}, table_id)));

  auto restore_iter =  down_pointer_cast<docdb::DocRowwiseIterator>(
      VERIFY_RESULT(tablet.NewRowIterator(schema.CopyWithoutColumnIds(), ht, table_id)));

  char max_byte_value_type = docdb::ValueTypeAsChar::kMaxByte;
  Slice max_row_key(&max_byte_value_type, 1);

  QLTableRow curr_row, restore_row;
  Slice curr_rk = max_row_key, restore_rk = max_row_key;
  if (VERIFY_RESULT(curr_iter->HasNext())) {
    RETURN_NOT_OK(curr_iter->NextRow(&curr_row));
    curr_rk = curr_iter->row_key();
  }

  if (VERIFY_RESULT(restore_iter->HasNext())) {
    RETURN_NOT_OK(restore_iter->NextRow(&restore_row));
    restore_rk = restore_iter->row_key();
  }

  while (curr_rk != max_row_key || restore_rk != max_row_key) {
    if (curr_rk == restore_rk) {
      if (!ArePgsqlRowsSame(curr_row, restore_row, schema)) {
        details.num_updates++;
        RETURN_NOT_OK(process(restore_row, PgsqlWriteRequestPB::PGSQL_UPDATE));
      }
      curr_rk = max_row_key, restore_rk = max_row_key;
      if (VERIFY_RESULT(curr_iter->HasNext())) {
        RETURN_NOT_OK(curr_iter->NextRow(&curr_row));
        curr_rk = curr_iter->row_key();
      }
      if (VERIFY_RESULT(restore_iter->HasNext())) {
        RETURN_NOT_OK(restore_iter->NextRow(&restore_row));
        restore_rk = restore_iter->row_key();
      }
    } else if (!curr_rk.GreaterOrEqual(restore_rk)) {
      details.num_deletes++;
      RETURN_NOT_OK(process(curr_row, PgsqlWriteRequestPB::PGSQL_DELETE));
      curr_rk = max_row_key;
      if (VERIFY_RESULT(curr_iter->HasNext())) {
        RETURN_NOT_OK(curr_iter->NextRow(&curr_row));
        curr_rk = curr_iter->row_key();
      }
    } else {
      details.num_inserts++;
      RETURN_NOT_OK(process(restore_row, PgsqlWriteRequestPB::PGSQL_INSERT));
      restore_rk = max_row_key;
      if (VERIFY_RESULT(restore_iter->HasNext())) {
        RETURN_NOT_OK(restore_iter->NextRow(&restore_row));
        restore_rk = restore_iter->row_key();
      }
    }
  }

  restore->pg_catalog_modification_details.emplace(table_id, details);

  return Status::OK();
}

} // namespace master
} // namespace yb

#endif // YB_MASTER_PG_SYS_CATALOG_UTIL_H_
