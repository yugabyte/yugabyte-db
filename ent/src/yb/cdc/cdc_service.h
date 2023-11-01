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

#ifndef ENT_SRC_YB_CDC_CDC_SERVICE_H
#define ENT_SRC_YB_CDC_CDC_SERVICE_H

#include <memory>

#include "yb/cdc/cdc_error.h"
#include "yb/cdc/cdc_metrics.h"
#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_service.service.h"
#include "yb/cdc/cdc_util.h"
#include "yb/client/async_initializer.h"

#include "yb/common/schema.h"

#include "yb/master/master_client.fwd.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/util/net/net_util.h"
#include "yb/util/service_util.h"
#include "yb/util/semaphore.h"

#include <boost/optional.hpp>

namespace yb {

class Thread;

namespace client {

class TableHandle;

}

namespace tserver {

class TSTabletManager;

}

namespace cdc {

typedef std::unordered_map<HostPort, std::shared_ptr<CDCServiceProxy>, HostPortHash>
  CDCServiceProxyMap;

YB_STRONGLY_TYPED_BOOL(CreateCDCMetricsEntity);

static const char* const kRecordType = "record_type";
static const char* const kRecordFormat = "record_format";
static const char* const kRetentionSec = "retention_sec";
static const char* const kSourceType = "source_type";
static const char* const kCheckpointType = "checkpoint_type";
static const char* const kIdType = "id_type";
static const char* const kNamespaceId = "NAMESPACEID";
static const char* const kTableId = "TABLEID";

struct TabletCheckpoint {
  OpId op_id;
  // Timestamp at which the op ID was last updated.
  CoarseTimePoint last_update_time;
  // Timestamp at which stream polling happen.
  int64_t last_active_time;

  bool ExpiredAt(std::chrono::milliseconds duration, std::chrono::time_point<CoarseMonoClock> now) {
    return !IsInitialized(last_update_time) || (now - last_update_time) >= duration;
  }
};

// Maintain each tablet minimum checkpoint info for
// log cache eviction as well as for intent cleanup.
struct TabletCDCCheckpointInfo {
  OpId cdc_op_id = OpId::Max();
  OpId cdc_sdk_op_id = OpId::Invalid();
  MonoDelta cdc_sdk_op_id_expiration = MonoDelta::kZero;
  int64_t cdc_sdk_latest_active_time = 0;
};

using TabletIdCDCCheckpointMap = std::unordered_map<TabletId, TabletCDCCheckpointInfo>;
using TabletIdStreamIdSet = std::set<pair<TabletId, CDCStreamId>>;

class CDCServiceImpl : public CDCServiceIf {
 public:
  CDCServiceImpl(tserver::TSTabletManager* tablet_manager,
                 const scoped_refptr<MetricEntity>& metric_entity_server,
                 MetricRegistry* metric_registry);

  CDCServiceImpl(const CDCServiceImpl&) = delete;
  void operator=(const CDCServiceImpl&) = delete;

  ~CDCServiceImpl();

  void CreateCDCStream(const CreateCDCStreamRequestPB* req,
                       CreateCDCStreamResponsePB* resp,
                       rpc::RpcContext rpc) override;
  void DeleteCDCStream(const DeleteCDCStreamRequestPB *req,
                       DeleteCDCStreamResponsePB* resp,
                       rpc::RpcContext rpc) override;
  void ListTablets(const ListTabletsRequestPB *req,
                   ListTabletsResponsePB* resp,
                   rpc::RpcContext rpc) override;
  void GetChanges(const GetChangesRequestPB* req,
                  GetChangesResponsePB* resp,
                  rpc::RpcContext rpc) override;
  void GetCheckpoint(const GetCheckpointRequestPB* req,
                     GetCheckpointResponsePB* resp,
                     rpc::RpcContext rpc) override;

  Result<TabletCheckpoint> TEST_GetTabletInfoFromCache(const ProducerTabletInfo& producer_tablet);

  // Update peers in other tablet servers about the latest minimum applied cdc index for a specific
  // tablet.
  void UpdateCdcReplicatedIndex(const UpdateCdcReplicatedIndexRequestPB* req,
                                UpdateCdcReplicatedIndexResponsePB* resp,
                                rpc::RpcContext rpc) override;

  void GetLatestEntryOpId(const GetLatestEntryOpIdRequestPB* req,
                          GetLatestEntryOpIdResponsePB* resp,
                          rpc::RpcContext context) override;

  void BootstrapProducer(const BootstrapProducerRequestPB* req,
                         BootstrapProducerResponsePB* resp,
                         rpc::RpcContext rpc) override;

  void GetCDCDBStreamInfo(const GetCDCDBStreamInfoRequestPB* req,
                          GetCDCDBStreamInfoResponsePB* resp,
                          rpc::RpcContext context) override;

  Status UpdateCdcReplicatedIndexEntry(
      const string& tablet_id, int64 replicated_index, boost::optional<int64> replicated_term,
      const OpId& cdc_sdk_replicated_op, const MonoDelta& cdc_sdk_op_id_expiration);

  Result<SetCDCCheckpointResponsePB> SetCDCCheckpoint(
      const SetCDCCheckpointRequestPB& req, CoarseTimePoint deadline) override;

  void GetTabletListToPollForCDC(
      const GetTabletListToPollForCDCRequestPB* req,
      GetTabletListToPollForCDCResponsePB* resp,
      rpc::RpcContext context) override;

  void IsBootstrapRequired(const IsBootstrapRequiredRequestPB* req,
                           IsBootstrapRequiredResponsePB* resp,
                           rpc::RpcContext rpc) override;

  void Shutdown() override;

  // Gets the associated metrics entity object stored in the additional metadata of the tablet.
  // If the metrics object is not present, then create it if create == true (eg if we have just
  // moved leaders) and not else (used to not recreate deleted metrics).
  std::shared_ptr<CDCTabletMetrics> GetCDCTabletMetrics(
      const ProducerTabletInfo& producer,
      std::shared_ptr<tablet::TabletPeer> tablet_peer = nullptr,
      CreateCDCMetricsEntity create = CreateCDCMetricsEntity::kTrue);

  void RemoveCDCTabletMetrics(
      const ProducerTabletInfo& producer,
      std::shared_ptr<tablet::TabletPeer> tablet_peer);

  void UpdateCDCTabletMetrics(const GetChangesResponsePB* resp,
                              const ProducerTabletInfo& producer_tablet,
                              const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                              const OpId& op_id,
                              int64_t last_readable_index);

  std::shared_ptr<CDCServerMetrics> GetCDCServerMetrics() {
    return server_metrics_;
  }

  // Returns true if this server has received a GetChanges call.
  bool CDCEnabled();

  void SetCDCServiceEnabled();

 private:
  FRIEND_TEST(CDCServiceTest, TestMetricsOnDeletedReplication);
  FRIEND_TEST(CDCServiceTestMultipleServersOneTablet, TestMetricsAfterServerFailure);
  FRIEND_TEST(CDCServiceTestMultipleServersOneTablet, TestMetricsUponRegainingLeadership);

  class Impl;

  template <class ReqType, class RespType>
  bool CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc);

  Status CheckStreamActive(
      const ProducerTabletInfo& producer_tablet, const client::YBSessionPtr& session,
      const int64_t& last_active_time_passed = 0);

  Result<int64_t> GetLastActiveTime(
      const ProducerTabletInfo& producer_tablet, const client::YBSessionPtr& session,
      bool ignore_cache = false);

  Result<OpId> GetLastCheckpoint(const ProducerTabletInfo& producer_tablet,
                                 const client::YBSessionPtr& session);

  Result<std::vector<pair<std::string, std::string>>> GetDBStreamInfo(
          const std::string& db_stream_id,
          const client::YBSessionPtr& session);

  Result<std::string> GetCdcStreamId(const ProducerTabletInfo& producer_tablet,
                                     const std::shared_ptr<client::YBSession>& session);

  Status UpdateCheckpointAndActiveTime(
      const ProducerTabletInfo& producer_tablet,
      const OpId& sent_op_id,
      const OpId& commit_op_id,
      const client::YBSessionPtr& session,
      uint64_t last_record_hybrid_time,
      const CDCRequestSource& request_source = CDCRequestSource::CDCSDK,
      bool force_update = false);

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> GetTablets(
      const CDCStreamId& stream_id);

  Status CreateCDCStreamForTable(
      const TableId& table_id,
      const std::unordered_map<std::string, std::string>& options,
      const CDCStreamId& stream_id);

  void RollbackPartialCreate(const CDCCreationState& creation_state);

  Result<NamespaceId> GetNamespaceId(const std::string& ns_name);

  Result<std::shared_ptr<StreamMetadata>> GetStream(
      const std::string& stream_id, bool ignore_cache = false);

  void RemoveStreamFromCache(const CDCStreamId& stream_id);

  std::shared_ptr<StreamMetadata> GetStreamMetadataFromCache(const std::string& stream_id);
  void AddStreamMetadataToCache(const std::string& stream_id,
                                const std::shared_ptr<StreamMetadata>& stream_metadata);

  Status CheckTabletValidForStream(const ProducerTabletInfo& producer_info);

  void TabletLeaderGetChanges(const GetChangesRequestPB* req,
                              GetChangesResponsePB* resp,
                              std::shared_ptr<rpc::RpcContext> context,
                              std::shared_ptr<tablet::TabletPeer> peer);

  void TabletLeaderGetCheckpoint(const GetCheckpointRequestPB* req,
                                 GetCheckpointResponsePB* resp,
                                 rpc::RpcContext* context,
                                 const std::shared_ptr<tablet::TabletPeer>& peer);

  void UpdateTabletPeersWithMaxCheckpoint(
      const std::unordered_set<TabletId>& tablet_ids_with_max_checkpoint,
      std::unordered_set<TabletId>* failed_tablet_ids);

  void UpdateTabletPeersWithMinReplicatedIndex(TabletIdCDCCheckpointMap* tablet_min_checkpoint_map);

  Status UpdateTabletPeerWithCheckpoint(
      const TabletId& tablet_id, TabletCDCCheckpointInfo* tablet_info,
      bool enable_update_local_peer_min_index, bool ignore_rpc_failures = true);

  Result<OpId> TabletLeaderLatestEntryOpId(const TabletId& tablet_id);

  void TabletLeaderIsBootstrapRequired(const IsBootstrapRequiredRequestPB* req,
                                       IsBootstrapRequiredResponsePB* resp,
                                       rpc::RpcContext* context,
                                       const std::shared_ptr<tablet::TabletPeer>& peer);

  Result<client::internal::RemoteTabletPtr> GetRemoteTablet(
      const TabletId& tablet_id, const bool use_cace = true);
  Result<client::internal::RemoteTabletServer*> GetLeaderTServer(
      const TabletId& tablet_id, const bool use_cache = true);
  Status GetTServers(const TabletId& tablet_id,
                             std::vector<client::internal::RemoteTabletServer*>* servers);

  std::shared_ptr<CDCServiceProxy> GetCDCServiceProxy(client::internal::RemoteTabletServer* ts);

  OpId GetMinSentCheckpointForTablet(const std::string& tablet_id);

  std::shared_ptr<MemTracker> GetMemTracker(
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
      const ProducerTabletInfo& producer_info);

  OpId GetMinAppliedCheckpointForTablet(const std::string& tablet_id,
                                        const client::YBSessionPtr& session);

  Status UpdatePeersCdcMinReplicatedIndex(
      const TabletId& tablet_id, const TabletCDCCheckpointInfo& cdc_checkpoint_min,
      bool ignore_failures = true);

  // Used as a callback function for parallelizing async cdc rpc calls.
  // Given a finished tasks counter, and the number of total rpc calls
  // in flight, the callback will increment the counter when called, and
  // set the promise to be fulfilled when all tasks have completed.
  void XClusterAsyncPromiseCallback(std::promise<void>* const promise,
                                    std::atomic<int>* const finished_tasks,
                                    int total_tasks);

  Status BootstrapProducerHelperParallelized(
    const BootstrapProducerRequestPB* req,
    BootstrapProducerResponsePB* resp,
    std::vector<client::YBOperationPtr>* ops,
    CDCCreationState* creation_state);

  Status BootstrapProducerHelper(
    const BootstrapProducerRequestPB* req,
    BootstrapProducerResponsePB* resp,
    std::vector<client::YBOperationPtr>* ops,
    CDCCreationState* creation_state);

  void ComputeLagMetric(int64_t last_replicated_micros, int64_t metric_last_timestamp_micros,
                        int64_t cdc_state_last_replication_time_micros,
                        scoped_refptr<AtomicGauge<int64_t>> metric);

  // Update metrics async_replication_sent_lag_micros and async_replication_committed_lag_micros.
  // Called periodically default 1s.
  void UpdateLagMetrics();

  // This method is used to read the cdc_state table to find the minimum replicated index for each
  // tablet and then update the peers' log objects. Also used to update lag metrics.
  void UpdatePeersAndMetrics();

  Status GetTabletIdsToPoll(
      const CDCStreamId stream_id,
      const std::set<TabletId>& active_or_hidden_tablets,
      const std::set<TabletId>& parent_tablets,
      const std::map<TabletId, TabletId>& child_to_parent_mapping,
      std::vector<std::pair<TabletId, OpId>>* result);

  // This method deletes entries from the cdc_state table that are contained in the set.
  Status DeleteCDCStateTableMetadata(
      const TabletIdStreamIdSet& cdc_state_entries_to_delete,
      const std::unordered_set<TabletId>& failed_tablet_ids);

  MicrosTime GetLastReplicatedTime(const std::shared_ptr<tablet::TabletPeer>& tablet_peer);

  bool ShouldUpdateLagMetrics(MonoTime time_since_update_metrics);

  Result<std::shared_ptr<client::TableHandle>> GetCdcStateTable() EXCLUDES(mutex_);

  void RefreshCdcStateTable() EXCLUDES(mutex_);

  Status RefreshCacheOnFail(const Status& s) EXCLUDES(mutex_);

  client::YBClient* client();

  void CreateEntryInCdcStateTable(
      const std::shared_ptr<client::TableHandle>& cdc_state_table,
      std::vector<ProducerTabletInfo>* producer_entries_modified,
      std::vector<client::YBOperationPtr>* ops,
      const CDCStreamId& stream_id,
      const TabletId& tablet_id,
      const OpId& op_id = OpId::Invalid());

  Status CreateCDCStreamForNamespace(
      const CreateCDCStreamRequestPB* req,
      CreateCDCStreamResponsePB* resp,
      CoarseTimePoint deadline);

  void FilterOutTabletsToBeDeletedByAllStreams(
      TabletIdCDCCheckpointMap* tablet_checkpoint_map,
      std::unordered_set<TabletId>* tablet_ids_with_max_checkpoint);

  Result<TabletIdCDCCheckpointMap> PopulateTabletCheckPointInfo(
      const TabletId& input_tablet_id = "",
      TabletIdStreamIdSet* tablet_stream_to_be_deleted = nullptr);

  Status SetInitialCheckPoint(
      const OpId& checkpoint, const string& tablet_id,
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer);

  Status UpdateChildrenTabletsOnSplitOp(
      const ProducerTabletInfo& producer_tablet,
      std::shared_ptr<yb::consensus::ReplicateMsg> split_op_msg,
      const client::YBSessionPtr& session);

  Status UpdateChildrenTabletsOnSplitOpForCDCSDK(const ProducerTabletInfo& info);

  // Get enum map from the cache.
  Result<EnumOidLabelMap> GetEnumMapFromCache(const NamespaceName& ns_name);

  // Update enum map in cache if required and get it.
  Result<EnumOidLabelMap> UpdateCacheAndGetEnumMap(const NamespaceName& ns_name);

  // Update enum map in cache.
  Result<EnumOidLabelMap> UpdateEnumMapInCacheUnlocked(const NamespaceName& ns_name)
      REQUIRES(mutex_);

  rpc::Rpcs rpcs_;

  tserver::TSTabletManager* tablet_manager_;

  MetricRegistry* metric_registry_;

  std::shared_ptr<CDCServerMetrics> server_metrics_;

  // Prevents GetChanges "storms" by rejecting when all permits have been acquired.
  Semaphore get_changes_rpc_sem_;

  // Used to protect tablet_checkpoints_ and stream_metadata_ maps.
  mutable rw_spinlock mutex_;

  std::unique_ptr<Impl> impl_;

  std::shared_ptr<client::TableHandle> cdc_state_table_ GUARDED_BY(mutex_);

  std::unordered_map<std::string, std::shared_ptr<StreamMetadata>> stream_metadata_
      GUARDED_BY(mutex_);

  // Map of namespace name to (map of enum oid to enumlabel).
  EnumLabelCache enumlabel_cache_ GUARDED_BY(mutex_);

  // Map of HostPort -> CDCServiceProxy. This is used to redirect requests to tablet leader's
  // CDC service proxy.
  CDCServiceProxyMap cdc_service_map_ GUARDED_BY(mutex_);

  // Thread with a few functions:
  //
  // Read the cdc_state table and get the minimum checkpoint for each tablet
  // and then, for each tablet this tserver is a leader, update the log minimum cdc replicated
  // index so we can use this information to correctly keep log files that are needed so we
  // can continue replicating cdc records. This runs periodically to handle
  // leadership changes (FLAGS_update_min_cdc_indices_interval_secs).
  // TODO(hector): It would be better to do this update only when a local peer becomes a leader.
  //
  // Periodically update lag metrics (FLAGS_update_metrics_interval_ms).
  scoped_refptr<Thread> update_peers_and_metrics_thread_;

  // True when this service is stopped. Used to inform
  // get_minimum_checkpoints_and_update_peers_thread_ that it should exit.
  bool cdc_service_stopped_ GUARDED_BY(mutex_){false};

  // True when this service has received a GetChanges request on a valid replication stream.
  std::atomic<bool> cdc_enabled_{false};
};

}  // namespace cdc
}  // namespace yb

#endif  // ENT_SRC_YB_CDC_CDC_SERVICE_H
