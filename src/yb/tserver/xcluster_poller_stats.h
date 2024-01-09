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

#include <shared_mutex>
#include <string>
#include <boost/circular_buffer.hpp>

#include "yb/cdc/cdc_types.h"
#include "yb/cdc/cdc_util.h"
#include "yb/common/entity_ids_types.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/util/monotime.h"

namespace yb {

struct XClusterPollerStats {
  explicit XClusterPollerStats(const std::string& replication_group_id);
  explicit XClusterPollerStats(
      const cdc::ProducerTabletInfo& producer_info, const cdc::ConsumerTabletInfo& consumer_info);

  cdc::ReplicationGroupId replication_group_id;
  std::string stream_id_str;
  TabletId producer_tablet_id;
  TableId consumer_table_id;
  TabletId consumer_tablet_id;
  std::string state;
  // Amount of data we received and processed per second, not accounting for delay between polls.
  double avg_throughput_kbps = 0;
  // Size of data that we received and processed successfully.
  double mbs_received = 0;
  uint64_t records_received = 0;
  uint64_t avg_poll_delay_ms = 0;
  uint64_t avg_get_changes_latency_ms = 0;
  uint64_t avg_apply_latency_ms = 0;
  int64_t received_index = 0;
  MonoTime last_poll_time;
  Status status;

  bool operator<(const XClusterPollerStats& rhs) const;
  void operator+=(const XClusterPollerStats& rhs);
  void operator/=(size_t divisor);
};

class PollStatsHistory {
 public:
  void RecordBeginPoll();
  void RecordEndGetChanges();
  void RecordBeginApplyChanges();
  void RecordEndApplyChanges();
  void RecordEndPoll(int num_records, int64_t received_index, size_t bytes_received);
  void SetError(Status&& error);
  void PopulateStats(XClusterPollerStats* stats) const;

 private:
  mutable std::shared_mutex mutex_;

  double mbs_received_ GUARDED_BY(mutex_) = 0;
  uint64_t records_received_ GUARDED_BY(mutex_) = 0;
  int64_t received_index_ GUARDED_BY(mutex_) = 0;
  MonoTime last_end_poll_time_ GUARDED_BY(mutex_);
  Status last_status_ GUARDED_BY(mutex_);

  struct CurrentPollInfo {
    MonoTime start_poll_time;
    MonoTime get_changes_completion_time;
    MonoTime start_apply_time;
    MonoTime end_apply_time;
  } current_poll_info_ GUARDED_BY(mutex_);

  struct PollStatEntry {
    MonoTime start_time;
    MonoDelta poll_delay;
    MonoDelta get_changes_latency;
    MonoDelta apply_latency;
    double kbs_received = 0;
  };

  constexpr static int kCircularBufferSize = 100;
  boost::circular_buffer<PollStatEntry> buffer_ GUARDED_BY(mutex_) =
      boost::circular_buffer<PollStatEntry>(kCircularBufferSize);
};

}  // namespace yb
