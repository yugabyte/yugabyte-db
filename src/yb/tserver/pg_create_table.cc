// Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/pg_create_table.h"

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"

#include "yb/common/pg_system_attr.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/value_type.h"

#include "yb/tserver/pg_client.pb.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace tserver {

PgCreateTable::PgCreateTable(const PgCreateTableRequestPB& req) : req_(req) {
}

Status PgCreateTable::Prepare() {
  table_name_ = client::YBTableName(
    YQL_DATABASE_PGSQL, GetPgsqlNamespaceId(req_.table_id().database_oid()),
    req_.database_name(), req_.table_name());
  indexed_table_id_ = PgObjectId::FromPB(req_.base_table_id());

  for (const auto& create_column : req_.create_columns()) {
    RETURN_NOT_OK(AddColumn(create_column));
  }

  EnsureYBbasectidColumnCreated();

  if (!req_.split_bounds().empty()) {
    if (hash_schema_.is_initialized()) {
      return STATUS(InvalidArgument,
                    "SPLIT AT option is not yet supported for hash partitioned tables");
    }
  }

  return Status::OK();
}

Status PgCreateTable::Exec(
    client::YBClient* client, const TransactionMetadata* transaction_metadata,
    CoarseTimePoint deadline) {
  // Construct schema.
  client::YBSchema schema;

  const char* pg_txn_enabled_env_var = getenv("YB_PG_TRANSACTIONS_ENABLED");
  const bool transactional =
      !pg_txn_enabled_env_var || strcmp(pg_txn_enabled_env_var, "1") == 0;
  LOG(INFO) << Format(
      "PgCreateTable: creating a $0 $1: $2/$3",
      transactional ? "transactional" : "non-transactional",
      indexed_table_id_.IsValid() ? "index" : "table",
      table_name_, PgObjectId::FromPB(req_.table_id()));
  TableProperties table_properties;
  bool set_table_properties = false;
  if (transactional) {
    table_properties.SetTransactional(true);
    set_table_properties = true;
  }
  if (req_.num_tablets() > 0) {
    table_properties.SetNumTablets(req_.num_tablets());
    set_table_properties = true;
  }
  if (set_table_properties) {
    schema_builder_.SetTableProperties(table_properties);
  }

  RETURN_NOT_OK(schema_builder_.Build(&schema));
  const auto split_rows = VERIFY_RESULT(BuildSplitRows(schema));

  // Create table.
  auto table_creator = client->NewTableCreator();
  table_creator->table_name(table_name_).table_type(client::YBTableType::PGSQL_TABLE_TYPE)
                .table_id(PgObjectId::FromPB(req_.table_id()).GetYBTableId())
                .schema(&schema)
                .colocated(req_.colocated());
  if (req_.is_pg_catalog_table()) {
    table_creator->is_pg_catalog_table();
  }
  if (req_.is_shared_table()) {
    table_creator->is_pg_shared_table();
  }
  if (hash_schema_) {
    table_creator->hash_schema(*hash_schema_);
  } else if (!req_.is_pg_catalog_table()) {
    table_creator->set_range_partition_columns(range_columns_, split_rows);
  }

  auto tablegroup_oid = PgObjectId::FromPB(req_.tablegroup_oid());
  if (tablegroup_oid.IsValid()) {
    table_creator->tablegroup_id(tablegroup_oid.GetYBTablegroupId());
  }

  auto tablespace_oid = PgObjectId::FromPB(req_.tablespace_oid());
  if (tablespace_oid.IsValid()) {
    table_creator->tablespace_id(tablespace_oid.GetYBTablespaceId());
  }

  // For index, set indexed (base) table id.
  if (indexed_table_id_.IsValid()) {
    table_creator->indexed_table_id(indexed_table_id_.GetYBTableId());
    if (req_.is_unique_index()) {
      table_creator->is_unique_index(true);
    }
    if (req_.skip_index_backfill()) {
      table_creator->skip_index_backfill(true);
    }
  }

  if (transaction_metadata) {
    table_creator->part_of_transaction(transaction_metadata);
  }

  table_creator->timeout(deadline - CoarseMonoClock::now());

  const Status s = table_creator->Create();
  if (PREDICT_FALSE(!s.ok())) {
    if (s.IsAlreadyPresent()) {
      if (req_.if_not_exist()) {
        return Status::OK();
      }
      return STATUS(InvalidArgument, "Duplicate table");
    }
    if (s.IsNotFound()) {
      return STATUS(InvalidArgument, "Database not found", table_name_.namespace_name());
    }
    return STATUS_FORMAT(
        InvalidArgument, "Invalid table definition: $0",
        s.ToString(false /* include_file_and_line */, false /* include_code */));
  }

  return Status::OK();
}

Status PgCreateTable::AddColumn(const PgCreateColumnPB& req) {
  auto yb_type = QLType::Create(static_cast<DataType>(req.attr_ybtype()));
  if (!req.is_hash() && !req.is_range()) {
    EnsureYBbasectidColumnCreated();
  }
  client::YBColumnSpec* col = schema_builder_.AddColumn(req.attr_name())->Type(yb_type);
  col->Order(req.attr_num());
  auto sorting_type = static_cast<SortingType>(req.sorting_type());
  if (req.is_hash()) {
    if (!range_columns_.empty()) {
      return STATUS(InvalidArgument, "Hash column not allowed after an ASC/DESC column");
    }
    if (sorting_type != SortingType::kNotSpecified) {
      return STATUS(InvalidArgument, "Hash column can't have sorting order");
    }
    col->HashPrimaryKey();
    hash_schema_ = YBHashSchema::kPgsqlHash;
  } else if (req.is_range()) {
    col->PrimaryKey();
    range_columns_.emplace_back(req.attr_name());
  }
  col->SetSortingType(sorting_type);
  return Status::OK();
}

void PgCreateTable::EnsureYBbasectidColumnCreated() {
  if (ybbasectid_added_ || !indexed_table_id_.IsValid()) {
    return;
  }

  auto yb_type = QLType::Create(DataType::BINARY);

  // Add YBUniqueIdxKeySuffix column to store key suffix for handling multiple NULL values in
  // column with unique index.
  // Value of this column is set to ybctid (same as ybbasectid) for index row in case index
  // is unique and at least one of its key column is NULL.
  // In all other case value of this column is NULL.
  if (req_.is_unique_index()) {
    auto name = "ybuniqueidxkeysuffix";
    client::YBColumnSpec* col = schema_builder_.AddColumn(name)->Type(yb_type);
    col->Order(to_underlying(PgSystemAttrNum::kYBUniqueIdxKeySuffix));
    col->PrimaryKey();
    range_columns_.emplace_back(name);
  }

  // Add ybbasectid column to store the ybctid of the rows in the indexed table. It should be
  // added at the end of the primary key of the index, i.e. either before any non-primary-key
  // column if any or before exec() below.
  auto name = "ybidxbasectid";
  client::YBColumnSpec* col = schema_builder_.AddColumn(name)->Type(yb_type);
  col->Order(to_underlying(PgSystemAttrNum::kYBIdxBaseTupleId));
  if (!req_.is_unique_index()) {
    col->PrimaryKey();
    range_columns_.emplace_back(name);
  }

  ybbasectid_added_ = true;
}

Result<std::vector<std::string>> PgCreateTable::BuildSplitRows(const client::YBSchema& schema) {
  std::vector<std::string> rows;
  rows.reserve(req_.split_bounds().size());
  docdb::DocKey prev_doc_key;
  for (const auto& bounds : req_.split_bounds()) {
    const auto& row = bounds.values();
    SCHECK_EQ(
        row.size(), PrimaryKeyRangeColumnCount() - (ybbasectid_added_ ? 1 : 0),
        IllegalState,
        "Number of split row values must be equal to number of primary key columns");
    std::vector<docdb::PrimitiveValue> range_components;
    range_components.reserve(row.size());
    bool compare_columns = true;
    for (const auto& row_value : row) {
      const auto column_index = range_components.size();
      range_components.push_back(row_value.value_case() == QLValuePB::VALUE_NOT_SET
        ? docdb::PrimitiveValue(docdb::ValueType::kLowest)
        : docdb::PrimitiveValue::FromQLValuePB(
            row_value,
            schema.Column(schema.FindColumn(range_columns_[column_index])).sorting_type()));

      // Validate that split rows honor column ordering.
      if (compare_columns && !prev_doc_key.empty()) {
        const auto& prev_value = prev_doc_key.range_group()[column_index];
        const auto compare = prev_value.CompareTo(range_components.back());
        if (compare > 0) {
          return STATUS(InvalidArgument, "Split rows ordering does not match column ordering");
        } else if (compare < 0) {
          // Don't need to compare further columns
          compare_columns = false;
        }
      }
    }
    prev_doc_key = docdb::DocKey(std::move(range_components));
    const auto keybytes = prev_doc_key.Encode();

    // Validate that there are no duplicate split rows.
    if (rows.size() > 0 && keybytes.AsSlice() == Slice(rows.back())) {
      return STATUS(InvalidArgument, "Cannot have duplicate split rows");
    }
    rows.push_back(keybytes.ToStringBuffer());
  }
  return rows;
}

size_t PgCreateTable::PrimaryKeyRangeColumnCount() const {
  return range_columns_.size();
}

}  // namespace tserver
}  // namespace yb
