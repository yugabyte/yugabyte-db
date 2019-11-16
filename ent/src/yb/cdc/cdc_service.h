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

#include "yb/cdc/cdc_service.service.h"

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/cdc/cdc_metrics.h"
#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_util.h"

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

#include "yb/client/async_initializer.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/service_util.h"

namespace yb {

namespace tserver {

class TSTabletManager;

}

namespace cdc {

typedef std::unordered_map<HostPort, std::shared_ptr<CDCServiceProxy>, HostPortHash>
    CDCServiceProxyMap;

static const char* const kRecordType = "record_type";
static const char* const kRecordFormat = "record_format";
static const char* const kRetentionSec = "retention_sec";

struct TabletCheckpoint {
  OpId op_id;
  // Timestamp at which the op ID was last updated.
  CoarseTimePoint last_update_time;
};

class CDCServiceImpl : public CDCServiceIf {
 public:
  CDCServiceImpl(tserver::TSTabletManager* tablet_manager,
                 const scoped_refptr<MetricEntity>& metric_entity_server,
                 MetricRegistry* metric_registry);

  CDCServiceImpl(const CDCServiceImpl&) = delete;
  void operator=(const CDCServiceImpl&) = delete;

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

  void Shutdown() override;

  // Used in cdc_service-int-test.cc.
  scoped_refptr<CDCTabletMetrics> GetCDCTabletMetrics(const ProducerTabletInfo& producer,
      std::shared_ptr<tablet::TabletPeer> tablet_peer = nullptr);
  std::shared_ptr<CDCServerMetrics> GetCDCServerMetrics() {
    return server_metrics_;
  }

 private:
  template <class ReqType, class RespType>
  bool CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc);

  Result<OpId> GetLastCheckpoint(const ProducerTabletInfo& producer_tablet,
                                 const std::shared_ptr<client::YBSession>& session);

  CHECKED_STATUS UpdateCheckpoint(const ProducerTabletInfo& producer_tablet,
                                  const OpId& sent_op_id,
                                  const OpId& commit_op_id,
                                  const std::shared_ptr<client::YBSession>& session);

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> GetTablets(
      const CDCStreamId& stream_id);

  Result<std::shared_ptr<StreamMetadata>> GetStream(const std::string& stream_id);

  std::shared_ptr<StreamMetadata> GetStreamMetadataFromCache(const std::string& stream_id);
  void AddStreamMetadataToCache(const std::string& stream_id,
                                const std::shared_ptr<StreamMetadata>& stream_metadata);

  CHECKED_STATUS CheckTabletValidForStream(const ProducerTabletInfo& producer_info);

  void TabletLeaderGetChanges(const GetChangesRequestPB* req,
                              GetChangesResponsePB* resp,
                              std::shared_ptr<rpc::RpcContext> context,
                              std::shared_ptr<tablet::TabletPeer> peer);

  void TabletLeaderGetCheckpoint(const GetCheckpointRequestPB* req,
                                 GetCheckpointResponsePB* resp,
                                 rpc::RpcContext* context,
                                 const std::shared_ptr<tablet::TabletPeer>& peer);

  Result<client::internal::RemoteTabletServer *> GetLeaderTServer(const TabletId& tablet_id);
  std::shared_ptr<CDCServiceProxy> GetCDCServiceProxy(client::internal::RemoteTabletServer* ts);

  OpId GetMinSentCheckpointForTablet(const std::string& tablet_id);

  std::shared_ptr<MemTracker> GetMemTracker(
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
      const ProducerTabletInfo& producer_info);

  yb::rpc::Rpcs rpcs_;

  tserver::TSTabletManager* tablet_manager_;

  boost::optional<yb::client::AsyncClientInitialiser> async_client_init_;

  MetricRegistry* metric_registry_;
  std::shared_ptr<CDCServerMetrics> server_metrics_;

  // Used to protect tablet_checkpoints_ and stream_metadata_ maps.
  mutable rw_spinlock mutex_;

  // These are guarded by lock_.
  // Map of checkpoints that have been sent to CDC consumer and stored in cdc_state.
  struct TabletCheckpointInfo {
    ProducerTabletInfo producer_tablet_info;

    // Checkpoint stored in cdc_state table. This is the checkpoint that CDC consumer sends to CDC
    // producer as the last checkpoint that it has successfully applied.
    mutable TabletCheckpoint cdc_state_checkpoint;
    // Last checkpoint sent to CDC consumer. This will always be more than cdc_state_checkpoint.
    mutable TabletCheckpoint sent_checkpoint;

    std::shared_ptr<MemTracker> mem_tracker;

    TabletCheckpointInfo(
        const ProducerTabletInfo& producer_tablet_info_,
        const TabletCheckpoint& cdc_state_checkpoint_,
        const TabletCheckpoint& sent_checkpoint_)
        : producer_tablet_info(producer_tablet_info_), cdc_state_checkpoint(cdc_state_checkpoint_),
          sent_checkpoint(sent_checkpoint_) {
    }

    const std::string& tablet_id() const {
      return producer_tablet_info.tablet_id;
    }

    const std::string& stream_id() const {
      return producer_tablet_info.stream_id;
    }
  };

  class TabletTag;
  class StreamTag;

  typedef boost::multi_index_container <
    TabletCheckpointInfo,
    boost::multi_index::indexed_by <
      boost::multi_index::hashed_unique <
        boost::multi_index::member <
          TabletCheckpointInfo, ProducerTabletInfo, &TabletCheckpointInfo::producer_tablet_info>
      >,
      boost::multi_index::hashed_non_unique <
        boost::multi_index::tag <TabletTag>,
        boost::multi_index::const_mem_fun <
          TabletCheckpointInfo, const std::string&, &TabletCheckpointInfo::tablet_id
        >
      >,
      boost::multi_index::hashed_non_unique <
        boost::multi_index::tag <StreamTag>,
        boost::multi_index::const_mem_fun <
          TabletCheckpointInfo, const std::string&, &TabletCheckpointInfo::stream_id
        >
      >
    >
  > TabletCheckpoints;

  TabletCheckpoints tablet_checkpoints_;

  std::unordered_map<std::string, std::shared_ptr<StreamMetadata>> stream_metadata_;

  // Map of HostPort -> CDCServiceProxy. This is used to redirect requests to tablet leader's
  // CDC service proxy.
  CDCServiceProxyMap cdc_service_map_;
};

}  // namespace cdc
}  // namespace yb

#endif  // ENT_SRC_YB_CDC_CDC_SERVICE_H
