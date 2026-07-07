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

#include "yb/util/result.h"

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

}  // namespace tools
}  // namespace yb
