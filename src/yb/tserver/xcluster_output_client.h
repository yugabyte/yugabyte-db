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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/xcluster_types.h"
#include "yb/consensus/opid_util.h"
#include "yb/tserver/xcluster_async_executor.h"

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/xcluster_write_interface.h"

#pragma once

namespace rocksdb {

class RateLimiter;

}

namespace yb {

class ThreadPool;

namespace tserver {

struct XClusterOutputClientResponse {
  Status status;
  OpIdPB last_applied_op_id;
  uint32_t processed_record_count;
  std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response;
};

class XClusterPoller;

class XClusterOutputClient : public XClusterAsyncExecutor {
 public:
  XClusterOutputClient(
      XClusterPoller* xcluster_poller, const xcluster::ConsumerTabletInfo& consumer_tablet_info,
      const xcluster::ProducerTabletInfo& producer_tablet_info, client::YBClient& local_client,
      ThreadPool* thread_pool, rpc::Rpcs* rpcs, bool use_local_tserver, bool is_automatic_mode,
      rocksdb::RateLimiter* rate_limiter);
  ~XClusterOutputClient();
  void StartShutdown() override;
  void CompleteShutdown() override;

  // Async call for applying changes. Will invoke the apply_changes_clbk when the changes are
  // applied, or when any error occurs.
  void ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> resp);

  void UpdateSchemaVersionMappings(
      const cdc::XClusterSchemaVersionMap& schema_version_map,
      const cdc::ColocatedSchemaVersionMap& colocated_schema_version_map);

 private:
  std::string LogPrefix() const override {
    return Format(
        "P [$0:$1] C [$2:$3]: ", producer_tablet_info_.stream_id, producer_tablet_info_.tablet_id,
        consumer_tablet_info_.table_id, consumer_tablet_info_.tablet_id);
  }
  bool IsOffline() override { return shutdown_; }
  void MarkFailed(const std::string& reason, const Status& status = Status::OK()) override
      EXCLUDES(lock_);
  void MarkFailedUnlocked(const std::string& reason, const Status& status = Status::OK())
      REQUIRES(lock_);

  // Process all records in get_changes_resp_ starting from the start index. If we find a ddl
  // record, then we process the current changes first, wait for those to complete, then process
  // the ddl + other changes after.
  Status ProcessChangesStartingFromIndex(int start);

  Result<cdc::CDCRecordPB> TransformSequencesDataRecord(const cdc::CDCRecordPB& record);

  Status ProcessRecordForTablet(
      const cdc::CDCRecordPB& record, const Result<client::internal::RemoteTabletPtr>& tablet);

  Status ProcessRecordForLocalTablet(const cdc::CDCRecordPB& record);

  Status ProcessRecordForTabletRange(
      const cdc::CDCRecordPB& record,
      const Result<std::vector<client::internal::RemoteTabletPtr>>& tablets);

  bool IsValidMetaOp(const cdc::CDCRecordPB& record);
  Result<bool> ProcessMetaOp(const cdc::CDCRecordPB& record);
  Result<bool> ProcessChangeMetadataOp(const cdc::CDCRecordPB& record);

  // Gets the producer/consumer schema mapping for the record.
  Result<cdc::XClusterSchemaVersionMap> GetSchemaVersionMap(const cdc::CDCRecordPB& record)
      REQUIRES(lock_);

  // Processes the Record and sends the CDCWrite for it.
  Status ProcessRecord(const std::vector<std::string>& tablet_ids, const cdc::CDCRecordPB& record)
      EXCLUDES(lock_);

  Status SendUserTableWrites();

  void SendNextCDCWriteToTablet(std::unique_ptr<WriteRequestPB> write_request);
  void UpdateSchemaVersionMapping(tserver::GetCompatibleSchemaVersionRequestPB* req);

  void DoWriteCDCRecordDone(const Status& status, const WriteResponsePB& response);

  void DoSchemaVersionCheckDone(
      const Status& status, const GetCompatibleSchemaVersionRequestPB& req,
      const GetCompatibleSchemaVersionResponsePB& response);

  // Increment processed record count.
  // Returns true if all records are processed, false if there are still some pending records.
  bool IncProcessedRecordCount() REQUIRES(lock_);

  void HandleResponse() EXCLUDES(lock_);
  void HandleError(const Status& s) EXCLUDES(lock_);

  bool UseLocalTserver();

  bool IsSequencesDataTablet();

  // Even though this is a const we guard it with a lock, since it is unsafe to use after shutdown.
  // TODO: Once we move the async execution logic to the Poller, it will guarantee that our lifetime
  // is less than the pollers lifetime, making this always safe to use.
  XClusterPoller* const xcluster_poller_ GUARDED_BY(lock_);
  const xcluster::ConsumerTabletInfo consumer_tablet_info_;
  const xcluster::ProducerTabletInfo producer_tablet_info_;
  cdc::XClusterSchemaVersionMap schema_versions_ GUARDED_BY(lock_);
  cdc::ColocatedSchemaVersionMap colocated_schema_version_map_ GUARDED_BY(lock_);
  client::YBClient& local_client_;

  const bool use_local_tserver_;
  const bool is_automatic_mode_;

  std::shared_ptr<client::YBTable> table_;

  // Used to protect error_status_, op_id_, done_processing_, write_handle_ and record counts.
  mutable rw_spinlock lock_;
  Status error_status_ GUARDED_BY(lock_);
  OpIdPB op_id_ GUARDED_BY(lock_) = consensus::MinimumOpId();
  bool done_processing_ GUARDED_BY(lock_) = false;
  std::atomic<bool> shutdown_ = false;

  uint32_t processed_record_count_ GUARDED_BY(lock_) = 0;
  uint32_t record_count_ GUARDED_BY(lock_) = 0;

  SchemaVersion producer_schema_version_ GUARDED_BY(lock_) = 0;
  ColocationId colocation_id_ GUARDED_BY(lock_) = 0;

  // This will cache the response to an ApplyChanges() request.
  std::shared_ptr<cdc::GetChangesResponsePB> get_changes_resp_;

  // Store the result of the lookup for all the tablets.
  yb::Result<std::vector<scoped_refptr<yb::client::internal::RemoteTablet>>> all_tablets_result_;

  yb::MonoDelta timeout_ms_;

  std::unique_ptr<XClusterWriteInterface> write_strategy_ GUARDED_BY(lock_);

  rocksdb::RateLimiter* rate_limiter_;

  // Only non-optional for sequences_data tablets, in which case it is
  // the OID of the DB to write incoming sequence information to.
  std::optional<uint32_t> db_oid_write_sequences_to_{std::nullopt};
};

std::shared_ptr<XClusterOutputClient> CreateXClusterOutputClient(
    XClusterPoller* xcluster_poller, const xcluster::ConsumerTabletInfo& consumer_tablet_info,
    const xcluster::ProducerTabletInfo& producer_tablet_info, client::YBClient& local_client,
    ThreadPool* thread_pool, rpc::Rpcs* rpcs, bool use_local_tserver, bool is_automatic_mode,
    rocksdb::RateLimiter* rate_limiter);

} // namespace tserver
} // namespace yb
