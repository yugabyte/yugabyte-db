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

#include <stdlib.h>
#include <string>
#include <unordered_map>

#include <boost/functional/hash.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/common_types.fwd.h"
#include "yb/common/entity_ids_types.h"

#include "yb/cdc/cdc_consumer.pb.h"

#include "yb/client/client_fwd.h"

#include "yb/gutil/strings/stringpiece.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/format.h"
#include "yb/util/result.h"

namespace yb {
namespace cdc {

YB_STRONGLY_TYPED_STRING(ReplicationGroupId);

// Maps a tablet id -> stream id -> replication error -> error detail.
typedef std::unordered_map<ReplicationErrorPb, std::string> ReplicationErrorMap;
typedef std::unordered_map<CDCStreamId, ReplicationErrorMap> StreamReplicationErrorMap;
typedef std::unordered_map<TabletId, StreamReplicationErrorMap> TabletReplicationErrorMap;

typedef std::unordered_map<SchemaVersion, SchemaVersion> XClusterSchemaVersionMap;
typedef std::unordered_map<uint32_t, XClusterSchemaVersionMap> ColocatedSchemaVersionMap;
typedef std::unordered_map<CDCStreamId, XClusterSchemaVersionMap> StreamSchemaVersionMap;
typedef std::unordered_map<CDCStreamId, ColocatedSchemaVersionMap> StreamColocatedSchemaVersionMap;

constexpr uint32_t kInvalidSchemaVersion = std::numeric_limits<uint32_t>::max();

typedef std::pair<uint32_t, uint32_t> SchemaVersionMapping;

YB_STRONGLY_TYPED_BOOL(StreamModeTransactional);
YB_DEFINE_ENUM(RefreshStreamMapOption, (kNone)(kAlways)(kIfInitiatedState));

struct ConsumerTabletInfo {
  std::string tablet_id;
  TableId table_id;
};

struct ProducerTabletInfo {
  // Needed on Consumer side for uniqueness. Empty on Producer.
  ReplicationGroupId replication_group_id;
  // Unique ID on Producer, but not on Consumer.
  CDCStreamId stream_id;
  std::string tablet_id;

  bool operator==(const ProducerTabletInfo& other) const {
    return replication_group_id == other.replication_group_id && stream_id == other.stream_id &&
           tablet_id == other.tablet_id;
  }

  std::string ToString() const {
    return Format(
        "{ replication_group_id: $0 stream_id: $1 tablet_id: $2 }", replication_group_id, stream_id,
        tablet_id);
  }

  // String used as a descriptor id for metrics.
  std::string MetricsString() const {
    std::stringstream ss;
    ss << replication_group_id << ":" << stream_id << ":" << tablet_id;
    return ss.str();
  }

  struct Hash {
    std::size_t operator()(const ProducerTabletInfo& p) const noexcept {
      std::size_t hash = 0;
      boost::hash_combine(hash, p.replication_group_id);
      boost::hash_combine(hash, p.stream_id);
      boost::hash_combine(hash, p.tablet_id);

      return hash;
    }
  };
};

struct XClusterTabletInfo {
  ProducerTabletInfo producer_tablet_info;
  ConsumerTabletInfo consumer_tablet_info;
  // Whether or not replication has been paused for this tablet.
  bool disable_stream;

  const std::string& producer_tablet_id() const {
    return producer_tablet_info.tablet_id;
  }
};

struct CDCCreationState {
  std::vector<CDCStreamId> created_cdc_streams;
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

inline bool IsAlterReplicationGroupId(const ReplicationGroupId& replication_group_id) {
  return GStringPiece(replication_group_id.ToString()).ends_with(kAlterReplicationGroupSuffix);
}

inline ReplicationGroupId GetOriginalReplicationGroupId(
    const ReplicationGroupId& replication_group_id) {
  // Remove the .ALTER suffix from universe_uuid if applicable.
  GStringPiece clean_id(replication_group_id.ToString());
  if (clean_id.ends_with(kAlterReplicationGroupSuffix)) {
    clean_id.remove_suffix(sizeof(kAlterReplicationGroupSuffix) - 1 /* exclude \0 ending */);
  }
  return ReplicationGroupId(clean_id.ToString());
}

Result<std::optional<qlexpr::QLRow>> FetchOptionalCdcStreamInfo(
    client::TableHandle* table, client::YBSession* session, const TabletId& tablet_id,
    const CDCStreamId& stream_id, const std::vector<std::string>& columns);

Result<qlexpr::QLRow> FetchCdcStreamInfo(
    client::TableHandle* table, client::YBSession* session, const TabletId& tablet_id,
    const CDCStreamId& stream_id, const std::vector<std::string>& columns);

} // namespace cdc
} // namespace yb
