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

#pragma once

#include <boost/optional/optional.hpp>

#include "yb/client/client_fwd.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/constants.h"
#include "yb/common/common_fwd.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/gutil/macros.h"

#include "yb/master/master_fwd.h"

#include "yb/util/monotime.h"

namespace yb {
struct TransactionMetadata;

namespace client {

// Creates a new table with the desired options.
class YBTableCreator {
 public:
  ~YBTableCreator();

  // Sets the name to give the table. It is copied. Required.
  YBTableCreator& table_name(const YBTableName& name);

  // Sets the type of the table.
  YBTableCreator& table_type(YBTableType table_type);

  // Sets the name of the role creating this table.
  YBTableCreator& creator_role_name(const RoleName& creator_role_name);

  // For Postgres: sets table id to assign, and whether the table is a sys catalog / shared table.
  YBTableCreator& table_id(const std::string& table_id);
  YBTableCreator& is_pg_catalog_table();
  YBTableCreator& is_pg_shared_table();

  // Sets the partition hash schema.
  YBTableCreator& hash_schema(dockv::YBHashSchema hash_schema);

  // Number of tablets that should be used for this table. If tablet_count is not given, YBClient
  // will calculate this value (num_shards_per_tserver * num_of_tservers).
  YBTableCreator& num_tablets(int32_t count);

  // Whether this table should be colocated due to being a part of colocated database.
  // Will be ignored by catalog manager if the database is not colocated.
  YBTableCreator& is_colocated_via_database(bool is_colocated_via_database);

  // Tablegroup ID - will be ignored by catalog manager if the table is not in a tablegroup.
  YBTableCreator& tablegroup_id(const std::string& tablegroup_id);

  YBTableCreator& colocation_id(ColocationId colocation_id);

  YBTableCreator& tablespace_id(const std::string& tablespace_id);

  YBTableCreator& is_matview(bool is_matview);

  YBTableCreator& pg_table_id(const std::string& pg_table_id);

  YBTableCreator& old_rewrite_table_id(const std::string& old_rewrite_table_id);

  YBTableCreator& is_truncate(bool is_truncate);

  YBTableCreator& xcluster_source_table_id(const TableId& source_table_id);

  // Sets the schema with which to create the table. Must remain valid for
  // the lifetime of the builder. Required.
  YBTableCreator& schema(const YBSchema* schema);

  // The creation of this table is dependent upon the success of this higher-level transaction.
  YBTableCreator& part_of_transaction(const TransactionMetadata* txn);

  // Adds a partitions to the table.
  YBTableCreator& add_partition(const dockv::Partition& partition);

  // Adds a set of hash partitions to the table.
  //
  // For each set of hash partitions added to the table, the total number of
  // table partitions is multiplied by the number of buckets. For example, if a
  // table is created with 3 split rows, and two hash partitions with 4 and 5
  // buckets respectively, the total number of table partitions will be 80
  // (4 range partitions * 4 hash buckets * 5 hash buckets).
  YBTableCreator& add_hash_partitions(const std::vector<std::string>& columns,
                                        int32_t num_buckets);

  // Adds a set of hash partitions to the table.
  //
  // This constructor takes a seed value, which can be used to randomize the
  // mapping of rows to hash buckets. Setting the seed may provide some
  // amount of protection against denial of service attacks when the hashed
  // columns contain user provided values.
  YBTableCreator& add_hash_partitions(const std::vector<std::string>& columns,
                                        int32_t num_buckets, int32_t seed);

  // Sets the columns on which the table will be range-partitioned.
  //
  // Every column must be a part of the table's primary key. If not set, the
  // table will be created with the primary-key columns as the range-partition
  // columns. If called with an empty vector, the table will be created without
  // range partitioning.
  //
  // Optional.
  YBTableCreator& set_range_partition_columns(
      const std::vector<std::string>& columns,
      const std::vector<std::string>& split_rows = {});

  // For index table: sets the indexed table id of this index.
  YBTableCreator& indexed_table_id(const std::string& id);

  // For index table: uses the old style request without index_info.
  YBTableCreator& TEST_use_old_style_create_request();

  // For index table: sets whether this is a local index.
  YBTableCreator& is_local_index(bool is_local_index);

  // For index table: sets whether this is a unique index.
  YBTableCreator& is_unique_index(bool is_unique_index);

  // For index table: should backfill be deferred for batching.
  YBTableCreator& is_backfill_deferred(bool is_backfill_deferred);

  // For index table: sets whether to do online schema migration when creating index.
  YBTableCreator& skip_index_backfill(const bool skip_index_backfill);

  // For vector index table: adds vector index-specific options.
  YBTableCreator& add_vector_options(const PgVectorIdxOptionsPB& vec_options);

  // For index table: indicates whether this index has mangled column name.
  // - Older index supports only ColumnRef, and its name is identical with colum name.
  // - Newer index supports expressions including ColumnRef, and its name is a mangled name of
  //   the expression. For example, ColumnRef name = "$C_" + escape_column_name.
  YBTableCreator& use_mangled_column_name(bool value);

  // Return index_info for caller to fill index information.
  IndexInfoPB* mutable_index_info() {
    return index_info_.get();
  }

  // Set the timeout for the operation. This includes any waiting
  // after the create has been submitted (i.e if the create is slow
  // to be performed for a large table, it may time out and then
  // later be successful).
  YBTableCreator& timeout(const MonoDelta& timeout);

  // Wait for the table to be fully created before returning.
  // Optional.
  //
  // If not provided, defaults to true.
  YBTableCreator& wait(bool wait);

  YBTableCreator& replication_info(const ReplicationInfoPB& ri);

  // Creates the table.
  //
  // The return value may indicate an error in the create table operation,
  // or a misuse of the builder; in the latter case, only the last error is
  // returned.
  Status Create();

  const std::string& get_table_id() const {
    return table_id_;
  }

 private:
  friend class YBClient;

  explicit YBTableCreator(YBClient* client);

  YBClient* const client_;

  YBTableName table_name_; // Required.

  TableType table_type_ = TableType::DEFAULT_TABLE_TYPE;

  RoleName creator_role_name_;

  // For Postgres: table id to assign, and whether the table is a sys catalog / shared table.
  // For all tables, table_id_ will contain the table id assigned after creation.
  std::string table_id_;
  boost::optional<bool> is_pg_catalog_table_;
  boost::optional<bool> is_pg_shared_table_;

  int32_t num_tablets_ = 0;

  const YBSchema* schema_ = nullptr;

  std::unique_ptr<PartitionSchemaPB> partition_schema_;

  std::vector<dockv::Partition> partitions_;

  int num_replicas_ = 0;

  std::unique_ptr<ReplicationInfoPB> replication_info_;

  // When creating index, proxy server construct index_info_, and master server will write it to
  // the data-table being indexed.
  std::unique_ptr<IndexInfoPB> index_info_;

  bool skip_index_backfill_ = false;

  bool TEST_use_old_style_create_request_ = false;

  MonoDelta timeout_;
  bool wait_ = true;

  bool is_colocated_via_database_ = true;

  // The tablegroup id to assign (if a table is in a tablegroup).
  std::string tablegroup_id_;

  // Colocation ID to distinguish a table within a colocation group.
  ColocationId colocation_id_ = kColocationIdNotSet;

  // The id of the tablespace to which this table is to be associated with.
  std::string tablespace_id_;

  boost::optional<bool> is_matview_;

  // In case the table was rewritten, explicitly store the TableId containing the PG table OID
  // (as the table's TableId no longer matches).
  TableId pg_table_id_;

  // Used during table rewrite - the TableId of the old DocDB table that is being rewritten.
  TableId old_rewrite_table_id_;

  // Set to true when the table is being re-written as part of a TRUNCATE operation.
  boost::optional<bool> is_truncate_;

  // Set by DDL Replication to link the table to the original table in the source cluster.
  TableId xcluster_source_table_id_;

  const TransactionMetadata* txn_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(YBTableCreator);
};

} // namespace client
} // namespace yb
