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

#include "yb/cdc/xrepl_types.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"

namespace yb::xcluster {

static const char* const kDDLQueuePgSchemaName = "yb_xcluster_ddl_replication";
static const char* const kDDLQueueTableName = "ddl_queue";
static const char* const kDDLReplicatedTableName = "replicated_ddls";
static const char* const kDDLQueueStartTimeColumn = "start_time";
static const char* const kDDLQueueQueryIdColumn = "query_id";
static const char* const kDDLQueueYbDataColumn = "yb_data";

YB_STRONGLY_TYPED_STRING(ReplicationGroupId);

struct TabletReplicationError {
  int64_t consumer_term = 0;
  ReplicationErrorPb error;
};

// Maps Consumer TableId, Producer TabletId to an error. This is per Replication Group.
using ReplicationGroupErrors =
    std::unordered_map<TableId, std::unordered_map<TabletId, TabletReplicationError>>;

struct ProducerTabletInfo {
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

inline size_t hash_value(const ProducerTabletInfo& p) noexcept {
  return ProducerTabletInfo::Hash()(p);
}

struct ConsumerTabletInfo {
  std::string tablet_id;
  TableId table_id;
};

struct XClusterTabletInfo {
  ProducerTabletInfo producer_tablet_info;
  ConsumerTabletInfo consumer_tablet_info;
  // Whether or not replication has been paused for this tablet.
  bool disable_stream;

  const std::string& producer_tablet_id() const { return producer_tablet_info.tablet_id; }
};

}  // namespace yb::xcluster
