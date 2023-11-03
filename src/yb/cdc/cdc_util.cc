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
//

#include "yb/cdc/cdc_util.h"

#include <boost/functional/hash.hpp>

#include "yb/gutil/strings/stringpiece.h"
#include "yb/util/format.h"

namespace yb::cdc {

std::string ProducerTabletInfo::ToString() const {
  return Format(
      "{ replication_group_id: $0 stream_id: $1 tablet_id: $2 }", replication_group_id, stream_id,
      tablet_id);
}

std::string ProducerTabletInfo::MetricsString() const {
  std::stringstream ss;
  ss << replication_group_id << ":" << stream_id << ":" << tablet_id;
  return ss.str();
}
std::size_t ProducerTabletInfo::Hash::operator()(const ProducerTabletInfo& p) const noexcept {
  std::size_t hash = 0;
  boost::hash_combine(hash, p.replication_group_id);
  boost::hash_combine(hash, p.stream_id);
  boost::hash_combine(hash, p.tablet_id);

  return hash;
}

bool IsAlterReplicationGroupId(const ReplicationGroupId& replication_group_id) {
  return GStringPiece(replication_group_id.ToString()).ends_with(kAlterReplicationGroupSuffix);
}

ReplicationGroupId GetOriginalReplicationGroupId(const ReplicationGroupId& replication_group_id) {
  // Remove the .ALTER suffix from universe_uuid if applicable.
  GStringPiece clean_id(replication_group_id.ToString());
  if (clean_id.ends_with(kAlterReplicationGroupSuffix)) {
    clean_id.remove_suffix(sizeof(kAlterReplicationGroupSuffix) - 1 /* exclude \0 ending */);
  }
  return ReplicationGroupId(clean_id.ToString());
}
}  // namespace yb::cdc
