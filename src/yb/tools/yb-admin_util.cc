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
#include "yb/util/status.h"

namespace yb {
namespace tools {

using std::string;
using master::ListTabletServersResponsePB;

namespace {

std::vector<string> SplitOnUnderscore(const string& s) {
  std::vector<string> tokens;
  size_t start = 0;
  while (start <= s.size()) {
    auto end = s.find('_', start);
    if (end == string::npos) {
      end = s.size();
    }
    if (end > start) {
      tokens.push_back(s.substr(start, end - start));
    }
    start = end + 1;
  }
  return tokens;
}

bool EitherIsPrefix(const string& a, const string& b) {
  const auto& shorter = a.size() <= b.size() ? a : b;
  const auto& longer = a.size() <= b.size() ? b : a;
  return longer.compare(0, shorter.size(), shorter) == 0;
}

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

bool IsNoSuchMethodError(const Status& s) {
  // ERROR_NO_SUCH_METHOD (rpc_header.proto) is rendered as "rpc error 2" by outbound_call's
  // error category; the code is not carried structurally on Status, so the rendered text is
  // the only thing to match. The npos comparison is the point: find() returns npos — truthy —
  // on a miss, and treating it as a bool made every remote error look like a version mismatch
  // (#33434).
  return s.IsRemoteError() && s.ToString().find("rpc error 2") != std::string::npos;
}

std::vector<string> SuggestByNameTokens(
    const string& op, const std::vector<string>& names, size_t max_results) {
  const auto op_tokens = SplitOnUnderscore(op);
  if (op_tokens.empty()) {
    return {};
  }
  // Rank is the number of name tokens no typed token covers; sorting the (rank, name) pairs
  // orders the most fully covered names first and breaks ties alphabetically.
  std::vector<std::pair<size_t, string>> ranked;
  for (const auto& name : names) {
    const auto name_tokens = SplitOnUnderscore(name);
    std::vector<bool> covered(name_tokens.size(), false);
    bool all_op_tokens_covered = true;
    for (const auto& op_token : op_tokens) {
      bool op_token_covered = false;
      for (size_t i = 0; i < name_tokens.size(); ++i) {
        if (EitherIsPrefix(op_token, name_tokens[i])) {
          covered[i] = true;
          op_token_covered = true;
        }
      }
      if (!op_token_covered) {
        all_op_tokens_covered = false;
        break;
      }
    }
    if (!all_op_tokens_covered) {
      continue;
    }
    ranked.emplace_back(std::count(covered.begin(), covered.end(), false), name);
  }
  std::sort(ranked.begin(), ranked.end());
  if (ranked.size() > max_results) {
    ranked.resize(max_results);
  }
  std::vector<string> result;
  result.reserve(ranked.size());
  for (auto& [_, name] : ranked) {
    result.push_back(std::move(name));
  }
  return result;
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

}  // namespace tools
}  // namespace yb
