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

#include <vector>
#include <string>

#include "yb/common/entity_ids_types.h"

#include "yb/util/enums.h"

namespace yb::master {

YB_DEFINE_ENUM(
    ClusterBalancerWarningType,
    (kSkipTableLoadBalancing)
    (kTableRemoveReplicas)
    (kTableAddReplicas)
    (kTableLeaderMoves)
    (kTabletUnderReplicated)
    (kTabletWrongPlacement));

class ClusterBalancerWarnings {
 public:
  struct MessageCount {
    // Example warning message for one instance of this warning type.
    std::string example_message;
    int count = 0;
  };

  size_t size() const {
    size_t total_size = 0;
    for (const auto& [_, message_count] : warnings_) {
      total_size += message_count.count;
    }
    return total_size;
  }

  void CountWarning(ClusterBalancerWarningType type, const std::string& message) {
    const auto it = warnings_.find(type);
    if (it != warnings_.end()) {
      it->second.count++;
    } else {
      warnings_[type] = MessageCount{ .example_message = message, .count = 1};
    }
  }

  std::vector<MessageCount> GetWarningSummary() const {
    std::vector<MessageCount> warnings;
    for (const auto& entry : warnings_) {
      warnings.push_back(entry.second);
    }
    return warnings;
  }

 private:
  // Map of warning type to an example table/tablet that caused the warning.
  std::unordered_map<ClusterBalancerWarningType, MessageCount> warnings_;
};

} // namespace yb::master
