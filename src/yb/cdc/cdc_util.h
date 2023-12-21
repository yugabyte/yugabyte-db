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

#pragma once

#include <string>
#include "yb/cdc/cdc_types.h"
#include "yb/common/entity_ids_types.h"

namespace yb::cdc {
struct ConsumerTabletInfo {
  std::string tablet_id;
  TableId table_id;
};

struct ProducerTabletInfo {
  // Needed on Consumer side for uniqueness. Empty on Producer.
  ReplicationGroupId replication_group_id;
  // Unique ID on Producer, but not on Consumer.
  xrepl::StreamId stream_id;
  TabletId tablet_id;

  bool operator==(const ProducerTabletInfo& other) const {
    return replication_group_id == other.replication_group_id && stream_id == other.stream_id &&
           tablet_id == other.tablet_id;
  }

  std::string ToString() const;

  struct Hash {
    std::size_t operator()(const ProducerTabletInfo& p) const noexcept;
  };
};

struct XClusterTabletInfo {
  ProducerTabletInfo producer_tablet_info;
  ConsumerTabletInfo consumer_tablet_info;
  // Whether or not replication has been paused for this tablet.
  bool disable_stream;

  const std::string& producer_tablet_id() const { return producer_tablet_info.tablet_id; }
};

struct CDCCreationState {
  std::vector<xrepl::StreamId> created_cdc_streams;
  std::vector<ProducerTabletInfo> producer_entries_modified;

  void Clear() {
    created_cdc_streams.clear();
    producer_entries_modified.clear();
  }
};

inline size_t hash_value(const ProducerTabletInfo& p) noexcept {
  return ProducerTabletInfo::Hash()(p);
}

constexpr char kAlterReplicationGroupSuffix[] = ".ALTER";

inline ReplicationGroupId GetAlterReplicationGroupId(const std::string& replication_group_id) {
  return ReplicationGroupId(replication_group_id + kAlterReplicationGroupSuffix);
}

inline ReplicationGroupId GetAlterReplicationGroupId(
    const ReplicationGroupId& replication_group_id) {
  return GetAlterReplicationGroupId(replication_group_id.ToString());
}

bool IsAlterReplicationGroupId(const ReplicationGroupId& replication_group_id);

ReplicationGroupId GetOriginalReplicationGroupId(const ReplicationGroupId& replication_group_id);

}  // namespace yb::cdc
