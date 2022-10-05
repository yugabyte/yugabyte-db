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

#ifndef ENT_SRC_YB_CDC_CDC_UTIL_H
#define ENT_SRC_YB_CDC_CDC_UTIL_H

#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <boost/functional/hash.hpp>

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/util/format.h"
#include "yb/gutil/strings/stringpiece.h"

namespace yb {
namespace cdc {

// Maps a tablet id -> stream id -> replication error -> error detail.
typedef std::unordered_map<ReplicationErrorPb, std::string> ReplicationErrorMap;
typedef std::unordered_map<CDCStreamId, ReplicationErrorMap> StreamReplicationErrorMap;
typedef std::unordered_map<TabletId, StreamReplicationErrorMap> TabletReplicationErrorMap;

struct ConsumerTabletInfo {
  std::string tablet_id;
  TableId table_id;
};

struct ProducerTabletInfo {
  std::string universe_uuid; /* needed on Consumer side for uniqueness. Empty on Producer */
  CDCStreamId stream_id; /* unique ID on Producer, but not on Consumer. */
  std::string tablet_id;

  bool operator==(const ProducerTabletInfo& other) const {
    return universe_uuid == other.universe_uuid &&
           stream_id == other.stream_id &&
           tablet_id == other.tablet_id;
  }

  std::string ToString() const {
    return Format("{ universe_uuid: $0 stream_id: $1 tablet_id: $2 }",
                  universe_uuid, stream_id, tablet_id);
  }

  // String used as a descriptor id for metrics.
  std::string MetricsString() const {
    std::stringstream ss;
    ss << universe_uuid << ":" << stream_id << ":" << tablet_id;
    return ss.str();
  }

  struct Hash {
    std::size_t operator()(const ProducerTabletInfo& p) const noexcept {
      std::size_t hash = 0;
      boost::hash_combine(hash, p.universe_uuid);
      boost::hash_combine(hash, p.stream_id);
      boost::hash_combine(hash, p.tablet_id);

      return hash;
    }
  };
};

struct XClusterTabletInfo {
  ProducerTabletInfo producer_tablet_info;
  ConsumerTabletInfo consumer_tablet_info;

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

inline bool IsAlterReplicationUniverseId(const string& universe_uuid) {
  return GStringPiece(universe_uuid).ends_with(".ALTER");
}

inline string GetOriginalReplicationUniverseId(const string& universe_uuid) {
  // Remove the .ALTER suffix from universe_uuid if applicable.
  GStringPiece clean_universe_id(universe_uuid);
  if (clean_universe_id.ends_with(".ALTER")) {
    clean_universe_id.remove_suffix(sizeof(".ALTER")-1 /* exclude \0 ending */);
  }
  return clean_universe_id.ToString();
}

} // namespace cdc
} // namespace yb


#endif // ENT_SRC_YB_CDC_CDC_UTIL_H
