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
#include <string>
#include <utility>
#include <vector>

#include "yb/common/entity_ids_types.h"
#include "yb/master/master_cluster.pb.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace tools {

// Returns true when a command failed because the server does not implement the requested RPC
// (ERROR_NO_SUCH_METHOD) — i.e. the cluster predates the operation.
bool IsNoSuchMethodError(const Status& s);

// Suggestions for an abbreviated operation name (#32640): the names whose '_'-separated tokens
// cover every token of op, ranked by fewest uncovered name tokens (the closest command first),
// then alphabetically, capped at max_results. A token covers another when either is a prefix of
// the other, so "server" finds "servers" and vice versa. Requiring every typed token to be
// covered is the precision guard: the alternative — widening the edit-distance tolerance of the
// fuzzy tier — makes arbitrary garbage match random commands.
std::vector<std::string> SuggestByNameTokens(
    const std::string& op, const std::vector<std::string>& names, size_t max_results);

std::string SnapshotIdToString(const SnapshotId& snapshot_id);

SnapshotId StringToSnapshotId(const std::string& str);

void SortListTabletServerEntries(
    google::protobuf::RepeatedPtrField<master::ListTabletServersResponsePB::Entry>& servers);

}  // namespace tools
}  // namespace yb
