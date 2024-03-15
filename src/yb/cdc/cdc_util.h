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
#include "yb/cdc/xrepl_types.h"
#include "yb/common/entity_ids_types.h"

namespace yb::cdc {

struct TabletStreamInfo {
  // Unique ID on Producer, but not on Consumer.
  xrepl::StreamId stream_id;
  TabletId tablet_id;

  bool operator==(const TabletStreamInfo& other) const {
    return stream_id == other.stream_id && tablet_id == other.tablet_id;
  }

  std::string ToString() const;

  struct Hash {
    std::size_t operator()(const TabletStreamInfo& p) const noexcept;
  };
};


struct CDCCreationState {
  std::vector<xrepl::StreamId> created_cdc_streams;
  std::vector<TabletStreamInfo> producer_entries_modified;

  void Clear() {
    created_cdc_streams.clear();
    producer_entries_modified.clear();
  }
};

inline size_t hash_value(const TabletStreamInfo& p) noexcept { return TabletStreamInfo::Hash()(p); }

}  // namespace yb::cdc
