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

#pragma once

#include <set>
#include <utility>

#include "yb/common/common_net.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace tools {

// Returns true when a command failed because the cluster does not implement the requested RPC —
// either the method (ERROR_NO_SUCH_METHOD) or the whole service (ERROR_NO_SUCH_SERVICE) is
// unknown to it, i.e. the cluster predates the operation.
bool IsUnsupportedRpcError(const Status& s);

std::string SnapshotIdToString(const SnapshotId& snapshot_id);

SnapshotId StringToSnapshotId(const std::string& str);

void SortListTabletServerEntries(
    google::protobuf::RepeatedPtrField<master::ListTabletServersResponsePB::Entry>& servers);

HostPortPB SelectTabletServerAddress(
    const google::protobuf::RepeatedPtrField<master::ListTabletServersResponsePB::Entry>& servers);

// Picks the address of a server to send an RPC to: the broadcast address when one is registered,
// the private RPC address otherwise. Setting --yb_admin_force_use_private_ip reverses the
// preference. Returns an empty HostPortPB when the server registered no address at all.
HostPortPB SelectServerAddress(
    const google::protobuf::RepeatedPtrField<HostPortPB>& broadcast_addresses,
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_rpc_addresses);

HostPortPB SelectServerAddress(const ServerRegistrationPB& registration);

HostPortPB SelectServerAddress(const master::TSInfoPB& ts_info);

}  // namespace tools
}  // namespace yb
