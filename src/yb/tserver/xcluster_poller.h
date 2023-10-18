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

#include <stdlib.h>
#include <string>

#include "yb/cdc/cdc_types.h"
#include "yb/tserver/xcluster_output_client_interface.h"
#include "yb/common/hybrid_time.h"
#include "yb/rpc/rpc.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/locks.h"
#include "yb/util/status_fwd.h"

#pragma once

namespace yb {

class ThreadPool;

namespace rpc {

class RpcController;

} // namespace rpc

namespace cdc {

class CDCServiceProxy;

} // namespace cdc

namespace tserver {

class XClusterConsumer;

class XClusterPoller : public std::enable_shared_from_this<XClusterPoller> {
 public:
  XClusterPoller(
      const cdc::ProducerTabletInfo& producer_tablet_info,
      const cdc::ConsumerTabletInfo& consumer_tablet_info,
      ThreadPool* thread_pool,
      rpc::Rpcs* rpcs,
      const std::shared_ptr<XClusterClient>& local_client,
      const std::shared_ptr<XClusterClient>& producer_client,
      XClusterConsumer* xcluster_consumer,
      bool use_local_tserver,
      const std::vector<TabletId>& global_transaction_status_tablets,
      bool enable_replicate_transaction_status_table,
      SchemaVersion last_compatible_consumer_schema_version,
      rocksdb::RateLimiter* rate_limiter,
      std::function<int64_t(const TabletId&)>
          get_leader_term);
  ~XClusterPoller();

  void StartShutdown();
  void CompleteShutdown();

  bool IsFailed() const { return is_failed_.load(); }

  // Begins poll process for a producer tablet.
  void SchedulePoll();

  void ScheduleSetSchemaVersionIfNeeded(
      SchemaVersion cur_version, SchemaVersion last_compatible_consumer_schema_version);

  void UpdateSchemaVersions(const cdc::XClusterSchemaVersionMap& schema_versions)
      EXCLUDES(schema_version_lock_);

  void UpdateColocatedSchemaVersionMap(
      const cdc::ColocatedSchemaVersionMap& colocated_schema_version_map)
      EXCLUDES(schema_version_lock_);

  std::string LogPrefix() const;

  HybridTime GetSafeTime() const EXCLUDES(safe_time_lock_);

  cdc::ConsumerTabletInfo GetConsumerTabletInfo() const;

  bool TEST_Is_Sleeping() const { return ANNOTATE_UNPROTECTED_READ(TEST_is_sleeping_); }

 private:
  template <class F>
  void SubmitFunc(F&& f);

  bool CheckOffline();

  void DoSetSchemaVersion(SchemaVersion cur_version, SchemaVersion current_consumer_schema_version)
      EXCLUDES(data_mutex_);

  void DoPoll() EXCLUDES(data_mutex_);
  void GetChangesCallback(const Status& status, cdc::GetChangesResponsePB&& resp);
  void HandleGetChangesResponse(Status status, std::shared_ptr<cdc::GetChangesResponsePB> resp)
      EXCLUDES(data_mutex_);
  void ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response)
      EXCLUDES(data_mutex_);
  void ApplyChangesCallback(XClusterOutputClientResponse response);
  void HandleApplyChangesResponse(XClusterOutputClientResponse response) EXCLUDES(data_mutex_);
  void UpdateSafeTime(int64 new_time) EXCLUDES(safe_time_lock_);
  void UpdateSchemaVersionsForApply() EXCLUDES(schema_version_lock_);
  bool IsLeaderTermValid() REQUIRES(data_mutex_);

  const cdc::ProducerTabletInfo producer_tablet_info_;
  const cdc::ConsumerTabletInfo consumer_tablet_info_;

  mutable rw_spinlock schema_version_lock_;
  cdc::XClusterSchemaVersionMap schema_version_map_ GUARDED_BY(schema_version_lock_);
  cdc::ColocatedSchemaVersionMap colocated_schema_version_map_ GUARDED_BY(schema_version_lock_);

  // Although this is processing serially, it might be on a different thread in the ThreadPool.
  // Using mutex to guarantee cache flush, preventing TSAN warnings.
  // Recursive, since when we abort the CDCReadRpc, that will also call the callback within the
  // same thread (HandlePoll()) which needs to Unregister poll_handle_ from rpcs_.
  std::mutex data_mutex_;

  std::atomic<bool> shutdown_ = false;
  // In failed state we do not poll for changes and are awaiting shutdown.
  std::atomic<bool> is_failed_ = false;
  std::condition_variable shutdown_cv_;

  OpIdPB op_id_ GUARDED_BY(data_mutex_);
  std::atomic<SchemaVersion> validated_schema_version_;
  std::atomic<SchemaVersion> last_compatible_consumer_schema_version_;
  std::function<int64_t(const TabletId&)> get_leader_term_;

  Status status_ GUARDED_BY(data_mutex_);

  std::shared_ptr<XClusterOutputClientIf> output_client_;
  std::shared_ptr<XClusterClient> producer_client_;

  ThreadPool* thread_pool_;
  rpc::Rpcs* rpcs_;
  std::mutex poll_handle_mutex_;
  rpc::Rpcs::Handle poll_handle_ GUARDED_BY(poll_handle_mutex_);
  XClusterConsumer* xcluster_consumer_ GUARDED_BY(data_mutex_);

  mutable rw_spinlock safe_time_lock_;
  HybridTime producer_safe_time_ GUARDED_BY(safe_time_lock_);

  bool is_polling_ GUARDED_BY(data_mutex_) = true;
  int poll_failures_ GUARDED_BY(data_mutex_){0};
  int apply_failures_ GUARDED_BY(data_mutex_){0};
  int idle_polls_ GUARDED_BY(data_mutex_){0};

  int64_t leader_term_ GUARDED_BY(data_mutex_) = OpId::kUnknownTerm;

  bool TEST_is_sleeping_ = false;
};

} // namespace tserver
} // namespace yb
