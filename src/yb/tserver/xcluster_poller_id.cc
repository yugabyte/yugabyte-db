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
//

#include "yb/tserver/xcluster_poller_id.h"

namespace yb {

XClusterPollerId::XClusterPollerId(
    cdc::ReplicationGroupId replication_group_id, TableId consumer_table_id,
    TabletId producer_tablet_id, int64 leader_term)
    : replication_group_id(replication_group_id),
      consumer_table_id(consumer_table_id),
      producer_tablet_id(producer_tablet_id),
      leader_term(leader_term) {}

bool XClusterPollerId::operator==(const XClusterPollerId& other) const {
  return replication_group_id == other.replication_group_id &&
         consumer_table_id == other.consumer_table_id &&
         producer_tablet_id == other.producer_tablet_id && leader_term == other.leader_term;
}

std::string XClusterPollerId::ToString() const {
  return YB_STRUCT_TO_STRING(
      replication_group_id, consumer_table_id, producer_tablet_id, leader_term);
}

std::size_t XClusterPollerId::GetHash() const {
  std::size_t hash = 0;
  boost::hash_combine(hash, replication_group_id);
  boost::hash_combine(hash, consumer_table_id);
  boost::hash_combine(hash, producer_tablet_id);
  boost::hash_combine(hash, leader_term);

  return hash;
}

}  // namespace yb
