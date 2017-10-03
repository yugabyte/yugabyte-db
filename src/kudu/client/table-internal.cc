// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/client/table-internal.h"

#include <string>

#include "kudu/client/client-internal.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/sysinfo.h"
#include "kudu/master/master.pb.h"
#include "kudu/master/master.proxy.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/util/monotime.h"

namespace kudu {

using master::GetTableLocationsRequestPB;
using master::GetTableLocationsResponsePB;
using rpc::RpcController;
using std::string;

namespace client {

using sp::shared_ptr;

KuduTable::Data::Data(shared_ptr<KuduClient> client,
                      string name,
                      string id,
                      const KuduSchema& schema,
                      PartitionSchema partition_schema)
    : client_(std::move(client)),
      name_(std::move(name)),
      id_(std::move(id)),
      schema_(schema),
      partition_schema_(std::move(partition_schema)) {
}

KuduTable::Data::~Data() {
}

Status KuduTable::Data::Open() {
  // TODO: fetch the schema from the master here once catalog is available.
  GetTableLocationsRequestPB req;
  GetTableLocationsResponsePB resp;

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(client_->default_admin_operation_timeout());

  req.mutable_table()->set_table_id(id_);
  Status s;
  // TODO: replace this with Async RPC-retrier based RPC in the next revision,
  // adding exponential backoff and allowing this to be used safely in a
  // a reactor thread.
  while (true) {
    RpcController rpc;

    // Have we already exceeded our deadline?
    MonoTime now = MonoTime::Now(MonoTime::FINE);
    if (deadline.ComesBefore(now)) {
      const char* msg = "OpenTable timed out after deadline expired";
      LOG(ERROR) << msg;
      return Status::TimedOut(msg);
    }

    // See KuduClient::Data::SyncLeaderMasterRpc().
    MonoTime rpc_deadline = now;
    rpc_deadline.AddDelta(client_->default_rpc_timeout());
    rpc.set_deadline(MonoTime::Earliest(rpc_deadline, deadline));

    s = client_->data_->master_proxy()->GetTableLocations(req, &resp, &rpc);
    if (!s.ok()) {
      // Various conditions cause us to look for the leader master again.
      // It's ok if that eventually fails; we'll retry over and over until
      // the deadline is reached.

      if (s.IsNetworkError()) {
        LOG(WARNING) << "Network error talking to the leader master ("
                     << client_->data_->leader_master_hostport().ToString() << "): "
                     << s.ToString();
        if (client_->IsMultiMaster()) {
          LOG(INFO) << "Determining the leader master again and retrying.";
          WARN_NOT_OK(client_->data_->SetMasterServerProxy(client_.get(), deadline),
                      "Failed to determine new Master");
          continue;
        }
      }

      if (s.IsTimedOut()
          && MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
        // If the RPC timed out and the operation deadline expired, we'll loop
        // again and time out for good above.
        LOG(WARNING) << "Timed out talking to the leader master ("
                     << client_->data_->leader_master_hostport().ToString() << "): "
                     << s.ToString();
        if (client_->IsMultiMaster()) {
          LOG(INFO) << "Determining the leader master again and retrying.";
          WARN_NOT_OK(client_->data_->SetMasterServerProxy(client_.get(), deadline),
                      "Failed to determine new Master");
          continue;
        }
      }
    }
    if (s.ok() && resp.has_error()) {
      if (resp.error().code() == master::MasterErrorPB::NOT_THE_LEADER ||
          resp.error().code() == master::MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
        LOG(WARNING) << "Master " << client_->data_->leader_master_hostport().ToString()
                     << " is no longer the leader master.";
        if (client_->IsMultiMaster()) {
          LOG(INFO) << "Determining the leader master again and retrying.";
          WARN_NOT_OK(client_->data_->SetMasterServerProxy(client_.get(), deadline),
                      "Failed to determine new Master");
          continue;
        }
      }
      if (s.ok()) {
        s = StatusFromPB(resp.error().status());
      }
    }
    if (!s.ok()) {
      LOG(WARNING) << "Error getting table locations: " << s.ToString() << ", retrying.";
      continue;
    }
    if (resp.tablet_locations_size() > 0) {
      break;
    }

    /* TODO: Use exponential backoff instead */
    base::SleepForMilliseconds(100);
  }

  VLOG(1) << "Open Table " << name_ << ", found " << resp.tablet_locations_size() << " tablets";
  return Status::OK();
}

} // namespace client
} // namespace kudu
