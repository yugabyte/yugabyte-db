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

#ifndef YB_TSERVER_PG_CREATE_TABLE_H
#define YB_TSERVER_PG_CREATE_TABLE_H

#include "yb/client/client_fwd.h"
#include "yb/client/schema.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_fwd.h"
#include "yb/common/partition.h"
#include "yb/common/pg_types.h"

#include "yb/tserver/pg_client.fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace tserver {

class PgCreateTable {
 public:
  explicit PgCreateTable(const PgCreateTableRequestPB& req);

  CHECKED_STATUS Prepare();
  CHECKED_STATUS Exec(
      client::YBClient* client, const TransactionMetadata* transaction_metadata,
      CoarseTimePoint deadline);

 private:
  CHECKED_STATUS AddColumn(const PgCreateColumnPB& req);
  void EnsureYBbasectidColumnCreated();
  Result<std::vector<std::string>> BuildSplitRows(const client::YBSchema& schema);

  size_t PrimaryKeyRangeColumnCount() const;

  const PgCreateTableRequestPB& req_;
  client::YBTableName table_name_;
  boost::optional<YBHashSchema> hash_schema_;
  std::vector<std::string> range_columns_;
  client::YBSchemaBuilder schema_builder_;
  PgObjectId indexed_table_id_;
  bool ybbasectid_added_ = false;
};

}  // namespace tserver
}  // namespace yb

#endif  // YB_TSERVER_PG_CREATE_TABLE_H
