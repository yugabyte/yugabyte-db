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

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_util.h"

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
  OpIdPB op_id;
  // Timestamp at which the op ID was last updated.
  CoarseTimePoint last_update_time;
};

struct CDCTabletCheckpoints {
  // Checkpoint stored in cdc_state table. This is the checkpoint that CDC consumer sends to CDC
  // producer as the last checkpoint that it has successfully applied.
  TabletCheckpoint cdc_state_checkpoint;
  // Last checkpoint sent to CDC consumer. This will always be more than cdc_state_checkpoint.
  TabletCheckpoint sent_checkpoint;
};

class CDCServiceImpl : public CDCServiceIf {
 public:
  CDCServiceImpl(tserver::TSTabletManager* tablet_manager,
                 const scoped_refptr<MetricEntity>& metric_entity);

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

 private:
  template <class ReqType, class RespType>
  bool CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc);

  Result<OpIdPB> GetLastCheckpoint(const ProducerTabletInfo& producer_tablet,
                                   const std::shared_ptr<client::YBSession>& session);

  CHECKED_STATUS UpdateCheckpoint(const ProducerTabletInfo& producer_tablet,
                                  const OpIdPB& sent_op_id,
                                  const OpIdPB& commit_op_id,
                                  const std::shared_ptr<client::YBSession>& session);

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> GetTablets(
      const CDCStreamId& stream_id);

  Result<std::shared_ptr<StreamMetadata>> GetStream(const std::string& stream_id);

  std::shared_ptr<StreamMetadata> GetStreamMetadataFromCache(const std::string& stream_id);
  void AddStreamMetadataToCache(const std::string& stream_id,
                                const std::shared_ptr<StreamMetadata>& stream_metadata);

  CHECKED_STATUS CheckTabletValidForStream(const std::string& stream_id,
                                           const std::string& tablet_id);

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

  OpIdPB GetMinSentCheckpointForTablet(const std::string& tablet_id);

  yb::rpc::Rpcs rpcs_;

  tserver::TSTabletManager* tablet_manager_;

  boost::optional<yb::client::AsyncClientInitialiser> async_client_init_;

  // Used to protect tablet_checkpoints_ and stream_metadata_ maps.
  mutable rw_spinlock lock_;

  // These are guarded by lock_.
  // Map of checkpoints that have been sent to CDC consumer and stored in cdc_state.
  std::unordered_map<ProducerTabletInfo, CDCTabletCheckpoints, ProducerTabletInfo::Hash>
      tablet_checkpoints_;

  std::unordered_map<std::string, std::shared_ptr<StreamMetadata>> stream_metadata_;

  // TODO: Add cache invalidation after tablet splitting is implemented (#1004).
  // Map of stream IDs <-> tablet IDs.
  typedef boost::bimap<
      boost::bimaps::multiset_of<std::string>, boost::bimaps::multiset_of<std::string>>
      StreamTabletBiMap;
  typedef StreamTabletBiMap::value_type stream_tablet_value;

  StreamTabletBiMap stream_tablets_;

  // Map of HostPort -> CDCServiceProxy. This is used to redirect requests to tablet leader's
  // CDC service proxy.
  CDCServiceProxyMap cdc_service_map_;
};

}  // namespace cdc
}  // namespace yb

#endif  // ENT_SRC_YB_CDC_CDC_SERVICE_H
