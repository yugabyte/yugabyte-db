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
#include "yb/consensus/opid_util.h"
#include "yb/tserver/xcluster_async_executor.h"
#include "yb/cdc/cdc_util.h"

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
  uint32_t wait_for_version{0};
  std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response;
};

class XClusterConsumer;
struct XClusterClient;

class XClusterOutputClient : public XClusterAsyncExecutor {
 public:
  XClusterOutputClient(
      XClusterConsumer* xcluster_consumer, const cdc::ConsumerTabletInfo& consumer_tablet_info,
      const cdc::ProducerTabletInfo& producer_tablet_info,
      const std::shared_ptr<XClusterClient>& local_client, ThreadPool* thread_pool, rpc::Rpcs* rpcs,
      std::function<void(const XClusterOutputClientResponse& response)> apply_changes_clbk,
      std::function<void(const std::string& reason, const Status& status)> mark_fail_clbk,
      bool use_local_tserver, rocksdb::RateLimiter* rate_limiter);
  ~XClusterOutputClient();
  void StartShutdown() override;
  void CompleteShutdown() override;

  // Sets the last compatible consumer schema version
  void SetLastCompatibleConsumerSchemaVersion(SchemaVersion schema_version);

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
  void MarkFailed(const std::string& reason, const Status& status = Status::OK()) override;

  // Process all records in get_changes_resp_ starting from the start index. If we find a ddl
  // record, then we process the current changes first, wait for those to complete, then process
  // the ddl + other changes after.
  Status ProcessChangesStartingFromIndex(int start);

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

  XClusterOutputClientResponse PrepareResponse() REQUIRES(lock_);
  void SendResponse(const XClusterOutputClientResponse& resp) EXCLUDES(lock_);

  void HandleResponse() EXCLUDES(lock_);
  void HandleError(const Status& s) EXCLUDES(lock_);

  bool UseLocalTserver();

  XClusterConsumer* xcluster_consumer_;
  const cdc::ConsumerTabletInfo consumer_tablet_info_;
  const cdc::ProducerTabletInfo producer_tablet_info_;
  cdc::XClusterSchemaVersionMap schema_versions_ GUARDED_BY(lock_);
  cdc::ColocatedSchemaVersionMap colocated_schema_version_map_ GUARDED_BY(lock_);
  std::shared_ptr<XClusterClient> local_client_;

  std::function<void(const XClusterOutputClientResponse& response)> apply_changes_clbk_;
  std::function<void(const std::string& reason, const Status& status)> mark_fail_clbk_;

  bool use_local_tserver_;

  std::shared_ptr<client::YBTable> table_;

  // Used to protect error_status_, op_id_, done_processing_, write_handle_ and record counts.
  mutable rw_spinlock lock_;
  Status error_status_ GUARDED_BY(lock_);
  OpIdPB op_id_ GUARDED_BY(lock_) = consensus::MinimumOpId();
  bool done_processing_ GUARDED_BY(lock_) = false;
  uint32_t wait_for_version_ GUARDED_BY(lock_) = 0;
  std::atomic<bool> shutdown_ = false;

  uint32_t processed_record_count_ GUARDED_BY(lock_) = 0;
  uint32_t record_count_ GUARDED_BY(lock_) = 0;

  SchemaVersion last_compatible_consumer_schema_version_ GUARDED_BY(lock_) = 0;
  SchemaVersion producer_schema_version_ GUARDED_BY(lock_) = 0;
  ColocationId colocation_id_ GUARDED_BY(lock_) = 0;

  // This will cache the response to an ApplyChanges() request.
  std::shared_ptr<cdc::GetChangesResponsePB> get_changes_resp_;

  // Store the result of the lookup for all the tablets.
  yb::Result<std::vector<scoped_refptr<yb::client::internal::RemoteTablet>>> all_tablets_result_;

  yb::MonoDelta timeout_ms_;

  std::unique_ptr<XClusterWriteInterface> write_strategy_ GUARDED_BY(lock_);

  rocksdb::RateLimiter* rate_limiter_;
};

std::shared_ptr<XClusterOutputClient> CreateXClusterOutputClient(
    XClusterConsumer* xcluster_consumer, const cdc::ConsumerTabletInfo& consumer_tablet_info,
    const cdc::ProducerTabletInfo& producer_tablet_info,
    const std::shared_ptr<XClusterClient>& local_client, ThreadPool* thread_pool, rpc::Rpcs* rpcs,
    std::function<void(const XClusterOutputClientResponse& response)> apply_changes_clbk,
    std::function<void(const std::string& reason, const Status& status)> mark_fail_clbk,
    bool use_local_tserver, rocksdb::RateLimiter* rate_limiter);

} // namespace tserver
} // namespace yb
