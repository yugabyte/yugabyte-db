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

#pragma once

#include "yb/cdc/cdc_types.h"

namespace yb {

// The minimum required information to uniquely identify a xCluster Poller instance across the
// universe. Each time the tablet leader peer changes the term is incremented. The Poller is tied to
// a single term.
// Each consumer table is linked to one producer table by one stream, so producer table id and
// stream id are redundant information that is ignored. When the tablet counts between the producer
// and consumer do not match, a single Poller can write to multiple consumer tablets, and multiple
// Pollers belonging to the same ReplicationGroup can write to the same consumer tablet.
struct XClusterPollerId {
  cdc::ReplicationGroupId replication_group_id;
  TableId consumer_table_id;
  TabletId producer_tablet_id;
  int64 leader_term;

  explicit XClusterPollerId(
      cdc::ReplicationGroupId replication_group_id, TableId consumer_table_id,
      TabletId producer_tablet_id, int64 leader_term);

  bool operator==(const XClusterPollerId& other) const;

  std::string ToString() const;

  std::size_t GetHash() const;
};

inline std::ostream& operator<<(std::ostream& out, const XClusterPollerId& poller_info) {
  return out << poller_info.ToString();
}

}  // namespace yb

namespace std {
template <>
struct hash<yb::XClusterPollerId> {
  size_t operator()(const yb::XClusterPollerId& poller_id) const { return poller_id.GetHash(); }
};
}  // namespace std
