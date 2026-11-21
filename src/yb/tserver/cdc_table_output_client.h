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
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>

#include "yb/client/client_fwd.h"

#include "yb/common/entity_ids_types.h"

#include "yb/tserver/xcluster_output_client.h"

namespace yb::tserver {

// Writes the changes that a CdcTablePoller received from the CDCSDK GetChanges of one tablet into
// the CDC table (pg_yb_cdc_changes) of the tablet's database, one row per changed row.
//
// The writes are upserts that are not part of a transaction. The primary key of a row is derived
// from the change alone, so applying a batch again, in full or in part, leaves the table as if it
// had been applied once. The poller relies on that to retry a failed batch and to re-read changes
// after a leader change.
//
// The before and after columns hold the row images undecoded, for the reader of the table to
// convert to datums the way a walsender does with a GetChanges response:
//   {"<attnum>": [<type oid>, <value>], ...}
// The value is the protobuf JSON form of the column's QLValuePB with the field names of the
// proto, as in {"int32_value": 1}, so bytes are base64 and 64-bit integers are strings. It is
// null for NULL. A column that the image does not have is absent.
class CdcTableOutputClient : public XClusterOutputClientIf,
                             public std::enable_shared_from_this<CdcTableOutputClient> {
 public:
  // Changes of tables other than published_table_ids are dropped. nullopt keeps all of them.
  CdcTableOutputClient(
      XClusterPoller* poller, const TableId& cdc_table_id, client::YBClient& local_client,
      rocksdb::RateLimiter* rate_limiter,
      std::optional<std::unordered_set<TableId>> published_table_ids);

  void StartShutdown() override;
  void CompleteShutdown() override EXCLUDES(mutex_);

  void ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> resp) override;

  // Maps the schema versions of an xCluster source to those of its target, which do not exist here.
  void UpdateSchemaVersionMappings(
      const cdc::XClusterSchemaVersionMap& schema_version_map,
      const cdc::ColocatedSchemaVersionMap& colocated_schema_version_map) override {}

 private:
  struct Batch;

  Status OpenTable();

  // Adds one upsert to the batch for every row change in its GetChanges response.
  Status AddWrites(Batch& batch);

  void FlushDone(const std::shared_ptr<Batch>& batch, client::FlushStatus* flush_status);

  void Respond(const Status& status, const Batch& batch) EXCLUDES(mutex_);

  const TableId cdc_table_id_;
  client::YBClient& local_client_;
  rocksdb::RateLimiter* const rate_limiter_;
  const std::optional<std::unordered_set<TableId>> published_table_ids_;

  // Only accessed by ApplyChanges, which the poller never runs concurrently.
  client::YBTablePtr table_;
  int32_t origin_id_column_id_ = 0;
  int32_t op_column_id_ = 0;
  int32_t before_column_id_ = 0;
  int32_t after_column_id_ = 0;

  std::atomic<bool> shutdown_{false};
  // Held while calling the poller. CompleteShutdown takes it to wait for such a call to finish.
  std::mutex mutex_;
  // Unsafe to use after shutdown.
  XClusterPoller* const poller_ GUARDED_BY(mutex_);
};

}  // namespace yb::tserver
