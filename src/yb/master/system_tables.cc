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

#include "yb/master/system_tables.h"

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"

#include "yb/dockv/partition.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/yql_virtual_table.h"

#include "yb/util/status_log.h"
#include "yb/master/catalog_manager_util.h"

#include "yb/util/flags.h"

DECLARE_bool(enable_table_owned_vector_reverse_mapping);
DECLARE_bool(TEST_create_table_with_empty_namespace_name);
DECLARE_bool(TEST_create_table_with_empty_pgschema_name);
DECLARE_int32(TEST_table_owned_vector_reverse_mapping);
DECLARE_bool(vector_index_store_payload);

namespace yb::master {

namespace {

// The schemas of the YCQL system tables. Their rows are written to the sys catalog, and
// UpdateSystemTableSchema bumps a table's version when its schema changes.

namespace peers {

const std::string kPeer = "peer";
const std::string kDataCenter = "data_center";
const std::string kHostId = "host_id";
const std::string kPreferredIp = "preferred_ip";
const std::string kRack = "rack";
const std::string kReleaseVersion = "release_version";
const std::string kRPCAddress = "rpc_address";
const std::string kSchemaVersion = "schema_version";
const std::string kTokens = "tokens";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kPeer, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kDataCenter, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kHostId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kPreferredIp, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kRack, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kReleaseVersion, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kRPCAddress, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSchemaVersion, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kTokens, QLType::CreateTypeSet(DataType::STRING)));
  return builder.Build();
}

}  // namespace peers

namespace local {

const std::string kSystemLocalKeyColumn = "key";
const std::string kSystemLocalBootstrappedColumn = "bootstrapped";
const std::string kSystemLocalBroadcastAddressColumn = "broadcast_address";
const std::string kSystemLocalClusterNameColumn = "cluster_name";
const std::string kSystemLocalCQLVersionColumn = "cql_version";
const std::string kSystemLocalDataCenterColumn = "data_center";
const std::string kSystemLocalGossipGenerationColumn = "gossip_generation";
const std::string kSystemLocalHostIdColumn = "host_id";
const std::string kSystemLocalListenAddressColumn = "listen_address";
const std::string kSystemLocalNativeProtocolVersionColumn =
    "native_protocol_version";
const std::string kSystemLocalPartitionerColumn = "partitioner";
const std::string kSystemLocalRackColumn = "rack";
const std::string kSystemLocalRpcAddressColumn = "rpc_address";
const std::string kSystemLocalSchemaVersionColumn = "schema_version";
const std::string kSystemLocalThriftVersionColumn = "thrift_version";
const std::string kSystemLocalTokensColumn = "tokens";
const std::string kSystemLocalTruncatedAtColumn = "truncated_at";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kSystemLocalKeyColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalBootstrappedColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalBroadcastAddressColumn, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalClusterNameColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalCQLVersionColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalDataCenterColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalGossipGenerationColumn, QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kSystemLocalHostIdColumn, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kSystemLocalListenAddressColumn, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalNativeProtocolVersionColumn,
                             QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalPartitionerColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalRackColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(yb::master::kSystemTablesReleaseVersionColumn,
                            QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalRpcAddressColumn, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalSchemaVersionColumn, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kSystemLocalThriftVersionColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalTokensColumn, QLType::CreateTypeSet(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalTruncatedAtColumn,
                             QLType::CreateTypeMap(DataType::UUID, DataType::BINARY)));
  return builder.Build();
}

}  // namespace local

namespace keyspaces {

constexpr const char* const kKeyspaceName = "keyspace_name";
constexpr const char* const kDurableWrites = "durable_writes";
constexpr const char* const kReplication = "replication";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kDurableWrites, QLType::Create(DataType::BOOL)));
  // TODO: replication needs to be a frozen map.
  CHECK_OK(builder.AddColumn(kReplication,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  return builder.Build();
}

}  // namespace keyspaces

namespace tables {

constexpr const char* const kKeyspaceName = "keyspace_name";
constexpr const char* const kTableName = "table_name";
constexpr const char* const kBloomFilterChance = "bloom_filter_fp_chance";
constexpr const char* const kCaching = "caching";
constexpr const char* const kCdc = "cdc";
constexpr const char* const kComment = "comment";
constexpr const char* const kCompaction = "compaction";
constexpr const char* const kCompression = "compression";
constexpr const char* const kCrcCheck = "crc_check_chance";
constexpr const char* const kLocalReadRepair = "dclocal_read_repair_chance";
constexpr const char* const kDefaultTimeToLive = "default_time_to_live";
constexpr const char* const kExtensions = "extensions";
constexpr const char* const kFlags = "flags";
constexpr const char* const kGcGraceSeconds = "gc_grace_seconds";
constexpr const char* const kId = "id";
constexpr const char* const kMaxIndexInterval = "max_index_interval";
constexpr const char* const kMemTableFlushPeriod = "memtable_flush_period_in_ms";
constexpr const char* const kMinIndexInterval = "min_index_interval";
constexpr const char* const kReadRepairChance = "read_repair_chance";
constexpr const char* const kSpeculativeRetry = "speculative_retry";
constexpr const char* const kTransactions = "transactions";
constexpr const char* const kNumTablets = "tablets";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kBloomFilterChance, QLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kCaching, QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCdc, QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn(kComment, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCompaction,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCompression,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCrcCheck, QLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kLocalReadRepair, QLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kDefaultTimeToLive, QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kExtensions,
                             QLType::CreateTypeMap(DataType::STRING, DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kFlags, QLType::CreateTypeSet(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kGcGraceSeconds, QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kMaxIndexInterval, QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kMemTableFlushPeriod, QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kMinIndexInterval, QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kReadRepairChance, QLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kSpeculativeRetry, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kTransactions,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kNumTablets, QLType::Create(DataType::INT32)));
  return builder.Build();
}

}  // namespace tables

namespace columns {

constexpr const char* const kKeyspaceName = "keyspace_name";
constexpr const char* const kTableName = "table_name";
constexpr const char* const kColumnName = "column_name";
constexpr const char* const kClusteringOrder = "clustering_order";
constexpr const char* const kColumnNameBytes = "column_name_bytes";
constexpr const char* const kKind = "kind";
constexpr const char* const kPosition = "position";
constexpr const char* const kType = "type";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kTableName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kColumnName, DataType::STRING));
  CHECK_OK(builder.AddColumn(kClusteringOrder, DataType::STRING));
  CHECK_OK(builder.AddColumn(kColumnNameBytes, DataType::BINARY));
  CHECK_OK(builder.AddColumn(kKind, DataType::STRING));
  CHECK_OK(builder.AddColumn(kPosition, DataType::INT32));
  CHECK_OK(builder.AddColumn(kType, DataType::STRING));
  return builder.Build();
}

}  // namespace columns

namespace size_estimates {

constexpr const char* const kKeyspaceName = "keyspace_name";
constexpr const char* const kTableName = "table_name";
constexpr const char* const kRangeStart = "range_start";
constexpr const char* const kRangeEnd = "range_end";
constexpr const char* const kMeanPartitionSize = "mean_partition_size";
constexpr const char* const kPartitionsCount = "partitions_count";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kTableName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kRangeStart, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kRangeEnd, DataType::STRING));
  CHECK_OK(builder.AddColumn(kMeanPartitionSize, DataType::INT64));
  CHECK_OK(builder.AddColumn(kPartitionsCount, DataType::INT64));
  return builder.Build();
}

}  // namespace size_estimates

namespace aggregates {

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("aggregate_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(
      "argument_types", QLType::CreateTypeFrozen(QLType::CreateTypeList(DataType::STRING))));
  CHECK_OK(builder.AddColumn("final_func", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("initcond", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("return_type", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("state_func", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("state_type", QLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace aggregates

namespace functions {

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("function_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(
      "argument_types", QLType::CreateTypeFrozen(QLType::CreateTypeList(DataType::STRING))));
  CHECK_OK(builder.AddColumn(
      "argument_names", QLType::CreateTypeFrozen(QLType::CreateTypeList(DataType::STRING))));
  CHECK_OK(builder.AddColumn("body", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("called_on_null_input", QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn("language", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("return_type", QLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace functions

namespace indexes {

constexpr const char* const kKeyspaceName = "keyspace_name";
constexpr const char* const kTableName = "table_name";
constexpr const char* const kIndexName = "index_name";
constexpr const char* const kKind = "kind";
constexpr const char* const kOptions = "options";
constexpr const char* const kTableId = "table_id";
constexpr const char* const kIndexId = "index_id";
constexpr const char* const kTransactions = "transactions";
constexpr const char* const kIsUnique = "is_unique";
constexpr const char* const kNumTablets = "tablets";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kIndexName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kKind, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kOptions,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kTableId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kIndexId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kTransactions,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kIsUnique, QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn(kNumTablets, QLType::Create(DataType::INT32)));

  return builder.Build();
}

}  // namespace indexes

namespace triggers {

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("table_name", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("trigger_name", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("options",
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  return builder.Build();
}

}  // namespace triggers

namespace views {

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("view_name", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("base_table_id", QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn("base_table_name", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("bloom_filter_fp_chance", QLType::Create(DataType::DOUBLE)));
  // TODO: caching needs to be a frozen map.
  CHECK_OK(builder.AddColumn("caching",
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn("cdc", QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn("comment", QLType::Create(DataType::STRING)));
  // TODO: compaction needs to be a frozen map.
  CHECK_OK(builder.AddColumn("compaction",
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  // TODO: compression needs to be a frozen map.
  CHECK_OK(builder.AddColumn("compression",
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn("crc_check_chance", QLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn("dclocal_read_repair_chance", QLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn("default_time_to_live", QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("extensions",
                             QLType::CreateTypeMap(DataType::STRING, DataType::BINARY)));
  CHECK_OK(builder.AddColumn("gc_grace_seconds", QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("id", QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn("include_all_columns", QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn("max_index_interval", QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("memtable_flush_period_in_ms", QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("min_index_interval", QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("read_repair_chance", QLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn("speculative_retry", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("where_clause", QLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace views

namespace types {

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("type_name", QLType::Create(DataType::STRING)));
  // TODO: field_names should be a frozen list.
  CHECK_OK(builder.AddColumn("field_names", QLType::CreateTypeList(DataType::STRING)));
  // TODO: field_types should be a frozen list.
  CHECK_OK(builder.AddColumn("field_types", QLType::CreateTypeList(DataType::STRING)));
  return builder.Build();
}

}  // namespace types

namespace partitions {

const std::string kKeyspaceName = "keyspace_name";
const std::string kTableName = "table_name";
const std::string kStartKey = "start_key";
const std::string kEndKey = "end_key";
const std::string kId = "id";
const std::string kReplicaAddresses = "replica_addresses";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kStartKey, QLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kEndKey, QLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kReplicaAddresses,
                             QLType::CreateTypeMap(DataType::INET, DataType::STRING)));
  return builder.Build();
}

}  // namespace partitions

namespace auth_roles {

constexpr const char* const kRole = "role";
constexpr const char* const kCanLogin = "can_login";
constexpr const char* const kIsSuperuser = "is_superuser";
constexpr const char* const kMemberOf = "member_of";
constexpr const char* const kSaltedHash = "salted_hash";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kRole, DataType::STRING));
  CHECK_OK(builder.AddColumn(kCanLogin, QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn(kIsSuperuser, QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn(kMemberOf, QLType::CreateTypeList(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSaltedHash, QLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace auth_roles

namespace auth_role_permissions {

constexpr const char* const kRole = "role";
constexpr const char* const kResource = "resource";
constexpr const char* const kPermissions = "permissions";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kRole, DataType::STRING));
  CHECK_OK(builder.AddColumn(kResource, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kPermissions, QLType::CreateTypeList(DataType::STRING)));
  return builder.Build();
}

}  // namespace auth_role_permissions

namespace auth_resource_role_permissions_index {

constexpr const char* const kResource = "resource";
constexpr const char* const kRole = "role";

Schema CreateSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kResource, DataType::STRING));
  CHECK_OK(builder.AddColumn(kRole, QLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace auth_resource_role_permissions_index

SystemTableDef Def(
    const char* table_name, const char* namespace_name, const char* namespace_id,
    Schema (*create_schema)()) {
  return SystemTableDef{
      .table_name = table_name,
      .namespace_name = namespace_name,
      .namespace_id = namespace_id,
      .create_schema = create_schema,
  };
}

Result<dockv::PartitionSchema> SinglePartitionSchema(
    const PartitionSchemaPB& pb, const Schema& schema) {
  dockv::PartitionSchema partition_schema;
  RETURN_NOT_OK(dockv::PartitionSchema::FromPB(pb, schema, &partition_schema));
  return partition_schema;
}

Result<PartitionPB> OnlyPartition(const dockv::PartitionSchema& partition_schema) {
  std::vector<dockv::Partition> partitions;
  RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));
  PartitionPB partition;
  partitions[0].ToPB(&partition);
  return partition;
}

bool EnableTableOwnedVectorReverseMapping() {
  return FLAGS_TEST_table_owned_vector_reverse_mapping != -1
      ? FLAGS_TEST_table_owned_vector_reverse_mapping != 0
      : FLAGS_enable_table_owned_vector_reverse_mapping;
}

}  // namespace

void FillNewTableEntry(
    const CreateTableRequestPB& req, const Schema& schema,
    const dockv::PartitionSchema& partition_schema,
    const NamespaceId& namespace_id, const NamespaceName& namespace_name, bool colocated,
    SysTablesEntryPB* metadata) {
  metadata->set_state(SysTablesEntryPB::PREPARING);
  metadata->set_name(req.name());
  metadata->set_table_type(req.table_type());
  metadata->set_namespace_id(namespace_id);
  if (!FLAGS_TEST_create_table_with_empty_namespace_name) {
    metadata->set_namespace_name(namespace_name);
  }
  metadata->set_version(0);
  metadata->set_next_column_id(ColumnId(schema.max_col_id() + 1));
  // No custom tablespace arrives as an absent or an empty id (CreateTablegroup sends "").
  if (req.has_tablespace_id() && !req.tablespace_id().empty()) {
    metadata->set_tablespace_id(req.tablespace_id());
  } else {
    // Sets the empty marker message; being in a oneof, this also clears tablespace_id.
    metadata->mutable_no_custom_tablespace();
  }
  *metadata->mutable_hosted_stateful_services() = req.hosted_stateful_services();

  if (req.has_replication_info()) {
    metadata->mutable_replication_info()->CopyFrom(req.replication_info());
  }
  // Use the Schema object passed in, since it has the column IDs already assigned,
  // whereas the user request PB does not.
  SchemaToPB(schema, metadata->mutable_schema());

  // Skipping the cases where the parameter is not required.
  if (req.table_type() == PGSQL_TABLE_TYPE && !req.is_pg_catalog_table() && !IsIndex(req)) {
    auto& table_properties = *metadata->mutable_schema()->mutable_table_properties();
    if (EnableTableOwnedVectorReverseMapping()) {
      table_properties.set_owns_vector_reverse_mapping(true);
    }
    // Fixed for the lifetime of the table: a table created while payloads are enabled never writes
    // the reverse mapping, so all its vector indexes resolve search results via the payload. This
    // keeps the reverse mapping entries and the chunks that replace them from ever mixing.
    table_properties.set_skip_vector_reverse_mapping(FLAGS_vector_index_store_payload);
  }

  if (FLAGS_TEST_create_table_with_empty_pgschema_name) {
    // Use empty string (default proto val) so that this passes has_pgschema_name() checks.
    metadata->mutable_schema()->set_deprecated_pgschema_name("");
  }

  partition_schema.ToPB(metadata->mutable_partition_schema());

  // For index table, set index details (indexed table id and whether the index is local).
  if (req.has_index_info()) {
    metadata->mutable_index_info()->CopyFrom(req.index_info());

    // Set the deprecated fields also for compatibility reasons.
    metadata->set_indexed_table_id(req.index_info().indexed_table_id());
    metadata->set_is_local_index(req.index_info().is_local());
    metadata->set_is_unique_index(req.index_info().is_unique());
  } else if (req.has_indexed_table_id()) {
    // Read data from the deprecated field and update the new fields.
    metadata->mutable_index_info()->set_indexed_table_id(req.indexed_table_id());
    metadata->mutable_index_info()->set_is_local(req.is_local_index());
    metadata->mutable_index_info()->set_is_unique(req.is_unique_index());

    // Set the deprecated fields also for compatibility reasons.
    metadata->set_indexed_table_id(req.indexed_table_id());
    metadata->set_is_local_index(req.is_local_index());
    metadata->set_is_unique_index(req.is_unique_index());
  }

  if (req.is_pg_shared_table()) {
    metadata->set_is_pg_shared_table(true);
  }

  if (req.is_matview()) {
    metadata->set_is_matview(true);
  }

  if (req.has_pg_table_id()) {
    metadata->set_pg_table_id(req.pg_table_id());
  }

  if (colocated) {
    metadata->set_colocated(true);
  }
}

const std::vector<SystemTableDef>& SystemTableDefs() {
  static const auto* const defs = new std::vector<SystemTableDef>{
      Def(kSystemPeersTableName, kSystemNamespaceName, kSystemNamespaceId, &peers::CreateSchema),
      Def(kSystemLocalTableName, kSystemNamespaceName, kSystemNamespaceId, &local::CreateSchema),
      Def(kSystemSchemaKeyspacesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &keyspaces::CreateSchema),
      Def(kSystemSchemaTablesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &tables::CreateSchema),
      Def(kSystemSchemaColumnsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &columns::CreateSchema),
      Def(kSystemSizeEstimatesTableName, kSystemNamespaceName, kSystemNamespaceId,
          &size_estimates::CreateSchema),
      // Empty tables.
      Def(kSystemSchemaAggregatesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &aggregates::CreateSchema),
      Def(kSystemSchemaFunctionsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &functions::CreateSchema),
      Def(kSystemSchemaIndexesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &indexes::CreateSchema),
      Def(kSystemSchemaTriggersTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &triggers::CreateSchema),
      Def(kSystemSchemaViewsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &views::CreateSchema),
      Def(kSystemSchemaTypesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
          &types::CreateSchema),
      Def(kSystemPartitionsTableName, kSystemNamespaceName, kSystemNamespaceId,
          &partitions::CreateSchema),
      // System auth tables.
      Def(kSystemAuthRolesTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId,
          &auth_roles::CreateSchema),
      Def(kSystemAuthRolePermissionsTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId,
          &auth_role_permissions::CreateSchema),
      Def(kSystemAuthResourceRolePermissionsIndexTableName, kSystemAuthNamespaceName,
          kSystemAuthNamespaceId, &auth_resource_role_permissions_index::CreateSchema),
  };
  return *defs;
}

SysTablesEntryPB SysCatalogTableEntry(const Schema& sys_catalog_schema) {
  SysTablesEntryPB metadata;
  metadata.set_state(SysTablesEntryPB::RUNNING);
  metadata.set_namespace_id(kSystemSchemaNamespaceId);
  metadata.set_namespace_name(kSystemSchemaNamespaceName);
  metadata.set_name(kSysCatalogTableName);
  metadata.set_table_type(TableType::YQL_TABLE_TYPE);
  SchemaToPB(sys_catalog_schema, metadata.mutable_schema());
  metadata.set_version(0);
  return metadata;
}

Result<SysTabletsEntryPB> SysCatalogTabletEntry(
    const Schema& sys_catalog_schema, const SysTablesEntryPB& table) {
  SysTabletsEntryPB metadata;
  metadata.set_state(SysTabletsEntryPB::RUNNING);
  *metadata.mutable_partition() = VERIFY_RESULT(OnlyPartition(
      VERIFY_RESULT(SinglePartitionSchema(table.partition_schema(), sys_catalog_schema))));
  metadata.set_table_id(kSysCatalogTableId);
  metadata.add_table_ids(kSysCatalogTableId);
  return metadata;
}

Result<bool> UpdateSystemTableSchema(const Schema& schema, SysTablesEntryPB* table) {
  Schema persisted;
  RETURN_NOT_OK(SchemaFromPB(table->schema(), &persisted));
  if (persisted.Equals(schema)) {
    return false;
  }
  SchemaToPB(schema, table->mutable_schema());
  table->set_version(table->version() + 1);
  table->set_updates_only_index_permissions(false);
  return true;
}

Result<SysTablesEntryPB> NewSystemTableEntry(const SystemTableDef& def) {
  const auto schema = def.create_schema();
  PartitionSchemaPB partition_schema_pb;
  partition_schema_pb.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
  const auto partition_schema = VERIFY_RESULT(SinglePartitionSchema(partition_schema_pb, schema));
  CreateTableRequestPB req;
  req.set_name(def.table_name);
  req.set_table_type(TableType::YQL_TABLE_TYPE);
  SysTablesEntryPB metadata;
  FillNewTableEntry(
      req, schema, partition_schema, def.namespace_id, def.namespace_name, /* colocated= */ false,
      &metadata);
  metadata.set_state(SysTablesEntryPB::RUNNING);
  return metadata;
}

Result<SysTabletsEntryPB> NewSystemTabletEntry(
    const TableId& table_id, const SysTablesEntryPB& table, const TabletId& tablet_id) {
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(table.schema(), &schema));
  const auto partition = VERIFY_RESULT(
      OnlyPartition(VERIFY_RESULT(SinglePartitionSchema(table.partition_schema(), schema))));
  auto table_info = make_scoped_refptr<TableInfo>(table_id, /* colocated= */ false);
  table_info->mutable_metadata()->StartMutation();
  table_info->mutable_metadata()->mutable_dirty()->pb = table;
  table_info->mutable_metadata()->CommitMutation();
  table_info->set_is_system();
  auto tablet = CreateTabletInfo(table_info, partition, SysTabletsEntryPB::RUNNING, tablet_id);
  auto entry = tablet->mutable_metadata()->mutable_dirty()->pb;
  tablet->mutable_metadata()->AbortMutation();
  return entry;
}

}  // namespace yb::master
