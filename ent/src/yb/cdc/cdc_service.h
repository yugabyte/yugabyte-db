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

#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/service_util.h"

namespace yb {
namespace cdc {

typedef std::unordered_map<HostPort, std::shared_ptr<CDCServiceProxy>, HostPortHash>
    CDCServiceProxyMap;

static const char* const kRecordType = "record_type";
static const char* const kRecordFormat = "record_format";
static const char* const kRetentionSec = "retention_sec";

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

  template <class RespType>
  Result<std::shared_ptr<tablet::TabletPeer>> GetTabletPeer(
      const std::string& tablet_id, RespType* resp, rpc::RpcContext* rpc);

  Result<OpIdPB> GetLastCheckpoint(const std::string& stream_id,
                                   const std::string& tablet_id,
                                   const std::shared_ptr<client::YBSession>& session);

  CHECKED_STATUS UpdateCheckpoint(const std::string& stream_id,
                                  const std::string& tablet_id,
                                  const OpIdPB& op_id,
                                  const std::shared_ptr<client::YBSession>& session);

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> GetTablets(
      const CDCStreamId& stream_id);

  std::shared_ptr<std::unordered_set<std::string>> GetTabletIdsForStream(
      const CDCStreamId& stream_id);

  Result<std::shared_ptr<StreamMetadata>> GetStream(const std::string& stream_id);

  std::shared_ptr<StreamMetadata> GetStreamMetadataFromCache(const std::string& stream_id);
  void AddStreamMetadataToCache(const std::string& stream_id,
                                const std::shared_ptr<StreamMetadata>& stream_metadata);

  CHECKED_STATUS CheckTabletValidForStream(const std::string& stream_id,
                                           const std::string& tablet_id);

  void TabletLeaderGetChanges(const GetChangesRequestPB* req,
                              GetChangesResponsePB* resp,
                              rpc::RpcContext* context);
  void TabletLeaderGetCheckpoint(const GetCheckpointRequestPB* req,
                                 GetCheckpointResponsePB* resp,
                                 rpc::RpcContext* context);

  Result<client::internal::RemoteTabletServer *> GetLeaderTServer(const TabletId& tablet_id);
  std::shared_ptr<CDCServiceProxy> GetCDCServiceProxy(client::internal::RemoteTabletServer* ts);

  tserver::TSTabletManager* tablet_manager_;

  boost::optional<yb::client::AsyncClientInitialiser> async_client_init_;

  // Used to protect tablet_checkpoints_ and stream_metadata_ maps.
  mutable rw_spinlock lock_;

  // These are guarded by lock_.
  std::unordered_map<std::string, OpIdPB> tablet_checkpoints_;
  std::unordered_map<std::string, std::shared_ptr<StreamMetadata>> stream_metadata_;

  // TODO: Add cache invalidation after tablet splitting is implemented (#1004).
  // Map of stream ID -> [tablet IDs].
  std::unordered_map<std::string, std::shared_ptr<std::unordered_set<std::string>>> stream_tablets_;

  // Map of HostPort -> CDCServiceProxy. This is used to redirect requests to tablet leader's
  // CDC service proxy.
  CDCServiceProxyMap cdc_service_map_;
};

}  // namespace cdc
}  // namespace yb

#endif  // ENT_SRC_YB_CDC_CDC_SERVICE_H
