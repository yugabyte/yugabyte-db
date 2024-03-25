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

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/tserver/pg_client.pb.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

DECLARE_bool(TEST_duplicate_create_table_request);

namespace yb {
namespace tserver {

namespace {

//--------------------------------------------------------------------------------------------------
// Constants used for the sequences data table.
//--------------------------------------------------------------------------------------------------
static constexpr const char* const kPgSequencesNamespaceName = "system_postgres";
static constexpr const char* const kPgSequencesDataTableName = "sequences_data";

// Columns names and ids.
static constexpr const char* const kPgSequenceDbOidColName = "db_oid";

static constexpr const char* const kPgSequenceSeqOidColName = "seq_oid";

static constexpr const char* const kPgSequenceLastValueColName = "last_value";

static constexpr const char* const kPgSequenceIsCalledColName = "is_called";

} // namespace

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
      if (indexed_table_id_.IsValid()) {
        return STATUS(InvalidArgument,
                    "SPLIT AT option is not yet supported for hash partitioned indexes");
      } else {
        return STATUS(InvalidArgument,
                    "SPLIT AT option is not yet supported for hash partitioned tables");
      }
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
    if (req_.schema_name() == "pg_catalog") {
      table_properties.SetReplicaIdentity(PgReplicaIdentity::NOTHING);
    } else if (req_.schema_name() == "information_schema") {
      table_properties.SetReplicaIdentity(PgReplicaIdentity::DEFAULT);
    } else {
      table_properties.SetReplicaIdentity(PgReplicaIdentity::CHANGE);
    }
    schema_builder_.SetTableProperties(table_properties);
  }
  if (!req_.schema_name().empty()) {
    schema_builder_.SetSchemaName(req_.schema_name());
  }

  RETURN_NOT_OK(schema_builder_.Build(&schema));
  const auto split_rows = VERIFY_RESULT(BuildSplitRows(schema));

  // Create table.
  auto table_creator = client->NewTableCreator();
  table_creator->table_name(table_name_).table_type(client::YBTableType::PGSQL_TABLE_TYPE)
                .table_id(PgObjectId::GetYbTableIdFromPB(req_.table_id()))
                .schema(&schema)
                .is_colocated_via_database(req_.is_colocated_via_database())
                .is_matview(req_.is_matview())
                .is_truncate(req_.is_truncate());
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
    table_creator->tablegroup_id(tablegroup_oid.GetYbTablegroupId());
  }

  if (req_.optional_colocation_id_case() !=
      PgCreateTableRequestPB::OptionalColocationIdCase::OPTIONAL_COLOCATION_ID_NOT_SET) {
    table_creator->colocation_id(req_.colocation_id());
  }

  auto tablespace_oid = PgObjectId::FromPB(req_.tablespace_oid());
  if (tablespace_oid.IsValid()) {
    table_creator->tablespace_id(tablespace_oid.GetYbTablespaceId());
  }

  auto pg_table_oid = PgObjectId::FromPB(req_.pg_table_oid());
  if (pg_table_oid.IsValid()) {
    table_creator->pg_table_id(pg_table_oid.GetYbTableId());
  }

  auto old_relfilenode_id = PgObjectId::FromPB(req_.old_relfilenode_oid());
  if (old_relfilenode_id.IsValid()) {
    table_creator->old_rewrite_table_id(old_relfilenode_id.GetYbTableId());
  }

  // For index, set indexed (base) table id.
  if (indexed_table_id_.IsValid()) {
    table_creator->indexed_table_id(indexed_table_id_.GetYbTableId());
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
      // When FLAGS_TEST_duplicate_create_table_request is set to true, a table creator sends out
      // duplicate create table requests. The first one should succeed, and the subsequent one
      // should failed with AlreadyPresent error status. This is expected in tests, so return
      // an OK status when FLAGS_TEST_duplicate_create_table_request is true.
      if (req_.if_not_exist() || FLAGS_TEST_duplicate_create_table_request) {
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
  auto yb_type = QLType::Create(ToLW(static_cast<PersistentDataType>(req.attr_ybtype())));
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
    hash_schema_ = dockv::YBHashSchema::kPgsqlHash;
  } else if (req.is_range()) {
    col->PrimaryKey(sorting_type);
    range_columns_.emplace_back(req.attr_name());
  }
  col->PgTypeOid(req.attr_pgoid());
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
  dockv::DocKey prev_doc_key;
  for (const auto& bounds : req_.split_bounds()) {
    const auto& row = bounds.values();
    SCHECK_EQ(
        implicit_cast<size_t>(row.size()),
        PrimaryKeyRangeColumnCount() - (ybbasectid_added_ ? 1 : 0),
        IllegalState,
        "Number of split row values must be equal to number of primary key columns");

    // Keeping backward compatibility for old tables
    const auto partitioning_version = schema.table_properties().partitioning_version();
    const auto range_components_size = row.size() + (partitioning_version > 0 ? 1 : 0);

    dockv::KeyEntryValues range_components;
    range_components.reserve(range_components_size);
    bool compare_columns = true;
    for (const auto& row_value : row) {
      const auto column_index = range_components.size();
      if (partitioning_version > 0) {
        range_components.push_back(dockv::KeyEntryValue::FromQLValuePBForKey(
            row_value,
            schema.Column(schema.FindColumn(range_columns_[column_index])).sorting_type()));
      } else {
        range_components.push_back(row_value.value_case() == QLValuePB::VALUE_NOT_SET
            ? dockv::KeyEntryValue(dockv::KeyEntryType::kLowest)
            : dockv::KeyEntryValue::FromQLValuePB(
                row_value,
                schema.Column(schema.FindColumn(range_columns_[column_index])).sorting_type()));
      }

      // Validate that split rows respect column ordering.
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

    // If `ybuniqueidxkeysuffix` or `ybidxbasectid` are added to a range_columns, their value must
    // be explicitly specified with defaulted MINVALUE as this is being done for the columns that
    // are not assigned a value for range partitioning to make YBOperation.partition_key, tablet's
    // partition bounds and tablet's key_bounds match the same structure; for more details refer to
    // YBTransformPartitionSplitPoints() and https://github.com/yugabyte/yugabyte-db/issues/12191
    if ((partitioning_version > 0) && ybbasectid_added_) {
      range_components.push_back(
          dockv::KeyEntryValue::FromQLVirtualValue(QLVirtualValuePB::LIMIT_MIN));
    }
    prev_doc_key = dockv::DocKey(std::move(range_components));
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

Status CreateSequencesDataTable(client::YBClient* client, CoarseTimePoint deadline) {
  const client::YBTableName table_name(YQL_DATABASE_PGSQL,
                                       kPgSequencesDataNamespaceId,
                                       kPgSequencesNamespaceName,
                                       kPgSequencesDataTableName);
  RETURN_NOT_OK(client->CreateNamespaceIfNotExists(kPgSequencesNamespaceName,
                                                   YQLDatabase::YQL_DATABASE_PGSQL,
                                                   "" /* creator_role_name */,
                                                   kPgSequencesDataNamespaceId));

  // Set up the schema.
  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(kPgSequenceDbOidColName)->HashPrimaryKey()->Type(DataType::INT64);
  schema_builder.AddColumn(kPgSequenceSeqOidColName)->HashPrimaryKey()->Type(DataType::INT64);
  schema_builder.AddColumn(kPgSequenceLastValueColName)->Type(DataType::INT64)->NotNull();
  schema_builder.AddColumn(kPgSequenceIsCalledColName)->Type(DataType::BOOL)->NotNull();
  client::YBSchema schema;
  CHECK_OK(schema_builder.Build(&schema));

  // Generate the table id.
  PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);

  // Try to create the table.
  auto table_creator(client->NewTableCreator());

  auto status = table_creator->table_name(table_name)
      .schema(&schema)
      .table_type(client::YBTableType::PGSQL_TABLE_TYPE)
      .table_id(oid.GetYbTableId())
      .hash_schema(dockv::YBHashSchema::kPgsqlHash)
      .timeout(deadline - CoarseMonoClock::now())
      .Create();
  // If we could create it, then all good!
  if (status.ok()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' created.";
    // If the table was already there, also not an error...
  } else if (status.IsAlreadyPresent()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' already exists";
  } else {
    // If any other error, report that!
    LOG(ERROR) << "Error creating table '" << table_name.ToString() << "': " << status;
    return status;
  }
  return Status::OK();
}

}  // namespace tserver
}  // namespace yb
