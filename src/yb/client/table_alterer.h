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

#include <boost/optional.hpp>

#include "yb/client/client_fwd.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_fwd.h"

#include "yb/master/master_ddl.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb {
struct TransactionMetadata;

namespace client {

// Alters an existing table based on the provided steps.
//
// Sample usage:
//   YBTableAlterer* alterer = client->NewTableAlterer("table-name");
//   alterer->AddColumn("foo")->Type(DataType::INT32)->NotNull();
//   Status s = alterer->Alter();
//   delete alterer;
class YBTableAlterer {
 public:
  ~YBTableAlterer();

  // Renames the table. Options:
  // (1) Provided the new namespace + the new table name - changing the table name and
  //     moving it to the new namespace (keeping the PG schema name unchanged).
  // (2) Provided only the new table name - changing the table name only in the same namespace
  //     (keeping the namespace & the PG schema name unchanged).
  // (3) Provided only the new PG schema name - changing the table schema only
  //     (keeping the namespace & the table name unchanged).
  YBTableAlterer* RenameTo(const YBTableName& new_name);

  // Adds a new column to the table.
  //
  // When adding a column, you must specify the default value of the new
  // column using YBColumnSpec::DefaultValue(...).
  YBColumnSpec* AddColumn(const std::string& name);

  // Alter an existing column.
  YBColumnSpec* AlterColumn(const std::string& name);

  // Drops an existing column from the table.
  YBTableAlterer* DropColumn(const std::string& name);

  // Alter table properties.
  YBTableAlterer* SetTableProperties(const TableProperties& table_properties);

  YBTableAlterer* SetWalRetentionSecs(const uint32_t wal_retention_secs);

  // Set the timeout for the operation. This includes any waiting
  // after the alter has been submitted (i.e if the alter is slow
  // to be performed on a large table, it may time out and then
  // later be successful).
  YBTableAlterer* timeout(const MonoDelta& timeout);

  // Wait for the table to be fully altered before returning.
  //
  // If not provided, defaults to true.
  YBTableAlterer* wait(bool wait);

  // Set replication info for the table.
  YBTableAlterer* replication_info(const master::ReplicationInfoPB& ri);

  // The altering of this table is dependent upon the success of this higher-level transaction.
  YBTableAlterer* part_of_transaction(const TransactionMetadata* txn);

  // Set increment_schema_version to true.
  YBTableAlterer* set_increment_schema_version();

  // Alters the table.
  //
  // The return value may indicate an error in the alter operation, or a
  // misuse of the builder (e.g. add_column() with default_value=NULL); in
  // the latter case, only the last error is returned.
  Status Alter();

 private:
  friend class YBClient;

  YBTableAlterer(YBClient* client, const YBTableName& name);
  YBTableAlterer(YBClient* client, const std::string id);

  Status ToRequest(master::AlterTableRequestPB* req);

  YBClient* const client_;
  const YBTableName table_name_;
  const std::string table_id_;

  Status status_;

  struct Step;
  std::vector<Step> steps_;

  MonoDelta timeout_;

  bool wait_ = true;

  std::unique_ptr<YBTableName> rename_to_;

  std::unique_ptr<TableProperties> table_properties_;

  boost::optional<uint32_t> wal_retention_secs_;

  std::unique_ptr<master::ReplicationInfoPB> replication_info_;

  const TransactionMetadata* txn_ = nullptr;

  bool increment_schema_version_ = false;

  DISALLOW_COPY_AND_ASSIGN(YBTableAlterer);
};

} // namespace client
} // namespace yb
