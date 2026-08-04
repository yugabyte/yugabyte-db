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
#include "yb/common/hybrid_time.h"
#include "yb/util/hash_util.h"

namespace yb::xcluster {

static const char* const kDDLQueuePgSchemaName = "yb_xcluster_ddl_replication";
static const char* const kDDLQueueTableName = "ddl_queue";
static const char* const kDDLReplicatedTableName = "replicated_ddls";
static const char* const kDDLQueueDDLEndTimeColumn = "ddl_end_time";
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
  TableId table_id;

  bool operator==(const ProducerTabletInfo& other) const = default;

  YB_STRUCT_DEFINE_HASH(ProducerTabletInfo, replication_group_id, stream_id, tablet_id, table_id);

  std::string ToString() const;
};

struct ConsumerTabletInfo {
  std::string tablet_id;
  TableId table_id;

  std::string ToString() const;
};

struct XClusterTabletInfo {
  ProducerTabletInfo producer_tablet_info;
  ConsumerTabletInfo consumer_tablet_info;
  // Whether or not replication has been paused for this tablet.
  bool disable_stream;
  // Whether or not we are using automatic DDL replication for this tablet.
  bool automatic_ddl_mode;

  const std::string& producer_tablet_id() const { return producer_tablet_info.tablet_id; }
};

// Contains information about commit times of the DDLs that we need to process on the xCluster
// target.
struct SafeTimeBatch {
  // Only commits earlier than this time can be processed. So this acts as a max time.
  HybridTime apply_safe_time;
  // Use set instead of unordered_set to ensure that the commit times are in order.
  std::set<HybridTime> commit_times;
  // Commits prior to this time have already been processed. So this acts as a min time.
  HybridTime last_commit_time_processed;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(apply_safe_time, commit_times, last_commit_time_processed);
  }

  bool IsComplete() const { return !apply_safe_time.is_special(); }

  bool operator==(const SafeTimeBatch& other) const {
    return apply_safe_time == other.apply_safe_time && commit_times == other.commit_times &&
           last_commit_time_processed == other.last_commit_time_processed;
  }
  bool operator!=(const SafeTimeBatch& other) const { return !(*this == other); }
};

}  // namespace yb::xcluster
