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
