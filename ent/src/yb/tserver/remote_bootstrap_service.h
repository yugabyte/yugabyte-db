// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_
#define ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_

#include "../../../../src/yb/tserver/remote_bootstrap_service.h"

namespace yb {
namespace tserver {
namespace enterprise {

class RemoteBootstrapServiceImpl : public yb::tserver::RemoteBootstrapServiceImpl {
  typedef yb::tserver::RemoteBootstrapServiceImpl super;
 public:
  RemoteBootstrapServiceImpl(FsManager* fs_manager,
                             TabletPeerLookupIf* tablet_peer_lookup,
                             const scoped_refptr<MetricEntity>& metric_entity);

 protected:
  CHECKED_STATUS GetDataFilePiece(
      const DataIdPB& data_id, const scoped_refptr<RemoteBootstrapSessionClass>& session,
      uint64_t offset, int64_t client_maxlen, std::string* data,
      int64_t* total_data_length, RemoteBootstrapErrorPB::Code* error_code) override;

  CHECKED_STATUS ValidateSnapshotFetchRequestDataId(const DataIdPB& data_id) const override;
};

}  // namespace enterprise
}  // namespace tserver
}  // namespace yb

#endif // ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_
