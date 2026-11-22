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

#include <optional>
#include <unordered_set>

#include "yb/cdc/cdc_service.pb.h"

#include "yb/tserver/xcluster_poller.h"

namespace yb {

namespace cdc {
class CDCServiceProxy;
class CDCStateTable;
}  // namespace cdc

namespace tserver {

// Polls the CDCSDK changes of one tablet of this universe and writes them to the CDC table of
// the tablet's database (see CdcTableOutputClient).
//
// It reuses the loop of the xCluster poller: GetChanges, apply, and only then the next GetChanges,
// which acknowledges the applied changes to the CDC service. It differs in what it polls: a CDCSDK
// stream, through this tserver's own client. For a tablet whose leader is on this tserver, which
// is where the consumer runs the poller, GetChanges is an in-process call.
class CdcTablePoller : public XClusterPoller {
 public:
  CdcTablePoller(
      const xcluster::ProducerTabletInfo& producer_tablet_info,
      const xcluster::ConsumerTabletInfo& consumer_tablet_info, const NamespaceId& namespace_id,
      ThreadPool* thread_pool, rpc::Rpcs* rpcs, client::YBClient& local_client,
      XClusterConsumer* xcluster_consumer, int64_t leader_term,
      std::function<int64_t(const TabletId&)> get_leader_term, const std::string& ts_uuid,
      cdc::CDCStateTable& cdc_state_table);

  // Changes of tables other than published_table_ids are not written. nullopt writes all.
  void InitCdcTablePoller(
      rocksdb::RateLimiter* rate_limiter,
      std::optional<std::unordered_set<TableId>> published_table_ids);

  // The replication errors are reported to the master per xCluster replication group, and this
  // poller belongs to none.
  void StoreReplicationError(
      ReplicationErrorPb error, const std::string& error_detail = {}) override {}

  // Called by the idle poll timer once the backoff of this poller has passed.
  void PollAfterIdleDelay() { SchedulePollNow(); }

 protected:
  client::YBClient* GetChangesClient() override { return &local_client_; }

  Status PrepareGetChangesRequest(cdc::GetChangesRequestPB* req) override REQUIRES(data_mutex_);

  // Calls the CDC service of this tserver directly, without the tablet lookup of a GetChangesRpc.
  void SendGetChangesRequest(client::YBClient* client, cdc::GetChangesRequestPB* req) override
      REQUIRES(data_mutex_);

  // GetChanges then runs in this thread, a thread of the consumer's pool, instead of on the CDC
  // service's pool, and so does the handling of its response.
  bool ShouldSendGetChangesWithoutLock() override REQUIRES(data_mutex_);

  void SendGetChangesRequestWithoutLock(
      client::YBClient* client, cdc::GetChangesRequestPB* req) override EXCLUDES(data_mutex_);

  void SchedulePollAfter(int64_t delay_ms) override;

  Status ProcessGetChangesResponseError(const cdc::GetChangesResponsePB& resp) override;

  // True when the response has no row change for the CDC table, which is the case for every poll
  // of an idle tablet.
  bool CanSkipApply(const cdc::GetChangesResponsePB& resp) const override;

  void ChangesApplied(const XClusterOutputClientResponse& response) override
      REQUIRES(data_mutex_);

 private:
  // Where the next GetChanges continues from. All of it comes from the previous response.
  struct NextRequest {
    cdc::CDCSDKCheckpointPB checkpoint;
    // -1 when not known.
    int64_t safe_hybrid_time = -1;
    int32_t wal_segment_index = 0;
    int64_t max_index_in_sort_window = 0;
  };

  // The checkpoint the stream has in cdc_state for the tablet: where the last poller got to, or
  // where the stream starts.
  Result<NextRequest> ReadCheckpointFromCdcState();

  cdc::CDCStateTable& cdc_state_table_;

  const std::string stream_id_str_;

  // Calls the CDC service of this tserver in process.
  const std::shared_ptr<cdc::CDCServiceProxy> local_cdc_proxy_;

  // Unset until read from cdc_state by the first poll.
  std::optional<NextRequest> next_request_ GUARDED_BY(data_mutex_);
};

}  // namespace tserver
}  // namespace yb
