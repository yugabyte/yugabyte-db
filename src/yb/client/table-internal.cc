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

#include "yb/client/table-internal.h"

#include <string>

#include "yb/client/client-internal.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/util/monotime.h"

namespace yb {

using master::GetTableLocationsRequestPB;
using master::GetTableLocationsResponsePB;
using rpc::RpcController;
using std::string;

namespace client {

using std::shared_ptr;

static Status PBToClientTableType(
    TableType table_type_from_pb,
    YBTableType* client_table_type) {
  switch (table_type_from_pb) {
    case TableType::KUDU_COLUMNAR_TABLE_TYPE:
      *client_table_type = YBTableType::KUDU_COLUMNAR_TABLE_TYPE;
      return Status::OK();
    case TableType::YQL_TABLE_TYPE:
      *client_table_type = YBTableType::YQL_TABLE_TYPE;
      return Status::OK();
    case TableType::REDIS_TABLE_TYPE:
      *client_table_type = YBTableType::REDIS_TABLE_TYPE;
      return Status::OK();
    default:
      *client_table_type = YBTableType::UNKNOWN_TABLE_TYPE;
      return STATUS(InvalidArgument, strings::Substitute(
        "Invalid table type from master response: $0", table_type_from_pb));
  }
}

YBTable::Data::Data(
    shared_ptr<YBClient> client,
    YBTableName name,
    string id,
    const YBSchema& schema,
    PartitionSchema partition_schema)
  : client_(std::move(client)),
    name_(std::move(name)),
    // The table type is set after the table is opened.
    table_type_(YBTableType::UNKNOWN_TABLE_TYPE),
    id_(std::move(id)),
    schema_(schema),
    partition_schema_(std::move(partition_schema)) {
}

YBTable::Data::~Data() {
}

Status YBTable::Data::Open() {
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
      return STATUS(TimedOut, msg);
    }

    // See YBClient::Data::SyncLeaderMasterRpc().
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


  RETURN_NOT_OK_PREPEND(PBToClientTableType(resp.table_type(), &table_type_),
    strings::Substitute("Invalid table type for table '$0'", name_.ToString()));

  VLOG(1) << "Open Table " << name_.ToString() << ", found "
          << resp.tablet_locations_size() << " tablets";
  return Status::OK();
}

}  // namespace client
}  // namespace yb
