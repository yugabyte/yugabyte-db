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

#include "yb/tserver/remote_bootstrap_service.h"

#include "yb/tserver/remote_bootstrap_session.h"

namespace yb {
namespace tserver {
namespace enterprise {

using std::string;

RemoteBootstrapServiceImpl::RemoteBootstrapServiceImpl(
    FsManager* fs_manager,
    TabletPeerLookupIf* tablet_peer_lookup,
    const scoped_refptr<MetricEntity>& metric_entity)
    : super(fs_manager, tablet_peer_lookup, metric_entity) {
}

Status RemoteBootstrapServiceImpl::GetDataFilePiece(
    const DataIdPB& data_id,
    const scoped_refptr<RemoteBootstrapSessionClass>& session,
    uint64_t offset,
    int64_t client_maxlen,
    string* data,
    int64_t* total_data_length,
    RemoteBootstrapErrorPB::Code* error_code) {
  if (data_id.type() == DataIdPB::SNAPSHOT_FILE) {
    // Fetching a snapshot file chunk.
    const string file_name = data_id.file_name();
    const string snapshot_id = data_id.snapshot_id();
    RETURN_NOT_OK_PREPEND(
        session->GetSnapshotFilePiece(
            snapshot_id, file_name, offset, client_maxlen, data, total_data_length, error_code),
        "Unable to get piece of snapshot file");
    return Status::OK();
  }

  return super::GetDataFilePiece(
      data_id, session, offset, client_maxlen, data, total_data_length, error_code);
}

Status RemoteBootstrapServiceImpl::ValidateSnapshotFetchRequestDataId(
    const DataIdPB& data_id) const {
  if (data_id.snapshot_id().empty()) {
    return STATUS(InvalidArgument,
                  "snapshot id must be specified for type == SNAPSHOT_FILE",
                  data_id.ShortDebugString());
  }
  if (data_id.file_name().empty()) {
    return STATUS(InvalidArgument,
                  "file name must be specified for type == SNAPSHOT_FILE",
                  data_id.ShortDebugString());
  }
  return Status::OK();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
