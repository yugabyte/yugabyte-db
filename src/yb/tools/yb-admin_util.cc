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

#include "yb/tools/yb-admin_util.h"

#include <algorithm>
#include <string_view>

#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"

#include "yb/rpc/outbound_call.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

DEFINE_NON_RUNTIME_bool(yb_admin_force_use_private_ip, false,
    "Prefer the private RPC address over the broadcast address when a server has registered "
    "both. The other address is still used when the preferred one is absent.");

namespace yb {
namespace tools {

using std::string;
using master::ListTabletServersResponsePB;

namespace {

int GetTabletServerAliveRank(const ListTabletServersResponsePB::Entry& server) {
  if (!server.has_alive()) {
    return 2;
  }
  return server.alive() ? 0 : 1;
}

bool CompareListTabletServersEntries(
    const ListTabletServersResponsePB::Entry& a,
    const ListTabletServersResponsePB::Entry& b) {
  const int a_alive_rank = GetTabletServerAliveRank(a);
  const int b_alive_rank = GetTabletServerAliveRank(b);
  if (a_alive_rank != b_alive_rank) {
    return a_alive_rank < b_alive_rank;
  }

  const auto& a_addresses = a.registration().common().private_rpc_addresses();
  const auto& b_addresses = b.registration().common().private_rpc_addresses();

  const std::string_view a_host =
      a_addresses.empty() ? std::string_view() : a_addresses.Get(0).host();
  const std::string_view b_host =
      b_addresses.empty() ? std::string_view() : b_addresses.Get(0).host();
  if (a_host != b_host) {
    return a_host < b_host;
  }

  const uint32_t a_port = a_addresses.empty() ? 0 : a_addresses.Get(0).port();
  const uint32_t b_port = b_addresses.empty() ? 0 : b_addresses.Get(0).port();
  if (a_port != b_port) {
    return a_port < b_port;
  }

  return a.instance_id().permanent_uuid() < b.instance_id().permanent_uuid();
}

}  // namespace

bool IsUnsupportedRpcError(const Status& s) {
  // Messenger::QueueInboundCall() answers a call it cannot route with ERROR_NO_SUCH_METHOD when the
  // service is registered but the method is not, and ERROR_NO_SUCH_SERVICE when the service itself
  // is absent. A cluster that predates the operation produces one or the other depending on whether
  // the RPC was added to an existing service, so both carry the framing RunCommand() applies.
  //
  // Read the code off the Status rather than matching Status::ToString(): OutboundCall::SetFailed()
  // attaches it with CloneAndAddErrorCode(RpcError(...)), which is how client.cc and
  // client_master_rpc.cc test the same condition. A status with no rpc code decodes to 0, so it
  // never matches.
  const auto rpc_error = rpc::RpcError(s);
  return rpc_error == rpc::ErrorStatusPB::ERROR_NO_SUCH_METHOD ||
         rpc_error == rpc::ErrorStatusPB::ERROR_NO_SUCH_SERVICE;
}

string SnapshotIdToString(const SnapshotId& snapshot_id) {
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(snapshot_id);
  return txn_snapshot_id ? txn_snapshot_id.ToString() : snapshot_id;
}

SnapshotId StringToSnapshotId(const string& str) {
  if (str.length() == TxnSnapshotId::StaticStringSize()) {
    auto txn_snapshot_id = TxnSnapshotIdFromString(str);
    if (txn_snapshot_id.ok()) {
      return SnapshotId(to_char_ptr(txn_snapshot_id->data()), txn_snapshot_id->size());
    }
  }
  // If conversion into TxnSnapshotId failed.
  return SnapshotId(str);
}

void SortListTabletServerEntries(
    google::protobuf::RepeatedPtrField<ListTabletServersResponsePB::Entry>& servers) {
  std::sort(servers.begin(), servers.end(), CompareListTabletServersEntries);
}

HostPortPB SelectTabletServerAddress(
    const google::protobuf::RepeatedPtrField<ListTabletServersResponsePB::Entry>& servers) {
  // Look for a live tablet server, but if the master does not report any of them as live, return
  // the first valid address, since the liveness reported by the master lags behind reality.
  HostPortPB any_tserver_address;
  for (const auto& server : servers) {
    if (!server.has_registration()) {
      continue;
    }
    auto address = SelectServerAddress(server.registration().common());
    if (address.host().empty()) {
      continue;
    }
    // A master that does not report liveness leaves the field unset, so treat it as live.
    if (!server.has_alive() || server.alive()) {
      return address;
    }
    if (any_tserver_address.host().empty()) {
      any_tserver_address = std::move(address);
    }
  }

  return any_tserver_address;
}

HostPortPB SelectServerAddress(
    const google::protobuf::RepeatedPtrField<HostPortPB>& broadcast_addresses,
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_rpc_addresses) {
  if (!broadcast_addresses.empty() && !private_rpc_addresses.empty() &&
      !FLAGS_yb_admin_force_use_private_ip) {
    YB_LOG_FIRST_N(INFO, 1) << "Server registered both a broadcast address "
                            << HostPortPBToString(broadcast_addresses.Get(0))
                            << " and a private RPC address "
                            << HostPortPBToString(private_rpc_addresses.Get(0))
                            << ", using the broadcast address. Pass "
                            << "--yb_admin_force_use_private_ip to use the private RPC address.";
  }
  const auto& preferred =
      FLAGS_yb_admin_force_use_private_ip ? private_rpc_addresses : broadcast_addresses;
  const auto& fallback =
      FLAGS_yb_admin_force_use_private_ip ? broadcast_addresses : private_rpc_addresses;
  if (!preferred.empty()) {
    return preferred.Get(0);
  }
  if (!fallback.empty()) {
    return fallback.Get(0);
  }
  return HostPortPB();
}

HostPortPB SelectServerAddress(const ServerRegistrationPB& registration) {
  return SelectServerAddress(
      registration.broadcast_addresses(), registration.private_rpc_addresses());
}

HostPortPB SelectServerAddress(const master::TSInfoPB& ts_info) {
  return SelectServerAddress(ts_info.broadcast_addresses(), ts_info.private_rpc_addresses());
}

}  // namespace tools
}  // namespace yb
