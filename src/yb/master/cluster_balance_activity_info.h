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

#include "yb/common/entity_ids_types.h"

#include "yb/master/async_rpc_tasks.h"

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

// Information representing the activity in one iteration of the cluster balancer.
class ClusterBalancerActivityInfo {
 public:
  bool IsIdle() const {
    return warnings_.size() == 0 && tasks_.empty() && !has_ongoing_remote_bootstraps_;
  }

  // Aggregate the tasks by {type, state}, and store the count and an example message for each.
  using TaskTypeAndState = std::pair<server::MonitoredTaskType, server::MonitoredTaskState>;
  struct TaskTypeAndStateHash {
    size_t operator()(const TaskTypeAndState& task_type_and_state) const {
      return boost::hash_value(task_type_and_state);
    }
  };
  struct AggregatedTaskDetails {
    int count = 0;
    std::string example_status;
    std::string example_description;
  };
  void AddTask(const std::shared_ptr<RetryingRpcTask>& task) { tasks_.push_back(task); }

  using TasksSummaryMap = std::unordered_map<
      TaskTypeAndState, AggregatedTaskDetails, TaskTypeAndStateHash>;
  TasksSummaryMap GetTasksSummary();

  struct WarningMessageCount {
    // Example warning message for one instance of this warning type.
    std::string example_message;
    int count = 0;
  };

  void CountWarning(ClusterBalancerWarningType type, const std::string& message) {
    const auto it = warnings_.find(type);
    if (it != warnings_.end()) {
      it->second.count++;
    } else {
      warnings_[type] = WarningMessageCount{ .example_message = message, .count = 1};
    }
  }

  std::vector<WarningMessageCount> GetWarningsSummary() const {
    std::vector<WarningMessageCount> warnings;
    for (const auto& entry : warnings_) {
      warnings.push_back(entry.second);
    }
    return warnings;
  }

  MonoTime run_end_time_ = MonoTime::Min();

  bool has_ongoing_remote_bootstraps_ = false;

 private:
  // List of warnings that might prevent the cluster balancer from making progress.
  std::unordered_map<ClusterBalancerWarningType, WarningMessageCount> warnings_;

  // List of cluster balancer tasks (both ongoing and newly-scheduled).
  std::vector<std::shared_ptr<RetryingRpcTask>> tasks_;
};

class ClusterBalancerActivityBuffer {
 public:
  explicit ClusterBalancerActivityBuffer(int capacity) : activities_(capacity) {}

  void RecordActivity(ClusterBalancerActivityInfo activity_info);
  ClusterBalancerActivityInfo GetLatestActivityInfo() const;

  bool IsIdle() const;

 private:
  mutable rw_spinlock mutex_;

  // Circular buffer of cluster balancer activity.
  boost::circular_buffer<ClusterBalancerActivityInfo> activities_ GUARDED_BY(mutex_);
};

} // namespace yb::master
