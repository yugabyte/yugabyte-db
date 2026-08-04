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

#include "yb/tserver/xcluster_poller_stats.h"

#include "yb/util/shared_lock.h"
#include "yb/util/size_literals.h"

namespace yb {

XClusterPollerStats::XClusterPollerStats(const std::string& replication_group_id)
    : replication_group_id(replication_group_id) {}

XClusterPollerStats::XClusterPollerStats(
    const xcluster::ProducerTabletInfo& producer_info,
    const xcluster::ConsumerTabletInfo& consumer_info)
    : replication_group_id(producer_info.replication_group_id),
      stream_id_str(producer_info.stream_id.ToString()),
      producer_tablet_id(producer_info.tablet_id),
      consumer_table_id(consumer_info.table_id),
      consumer_tablet_id(consumer_info.tablet_id) {}

bool XClusterPollerStats::operator<(const XClusterPollerStats& rhs) const {
  // Errors at top, followed by pollers receiving most data.
  return status.ok() && mbs_received < rhs.mbs_received;
}

void XClusterPollerStats::operator+=(const XClusterPollerStats& rhs) {
  avg_poll_delay_ms += rhs.avg_poll_delay_ms;
  avg_get_changes_latency_ms += rhs.avg_get_changes_latency_ms;
  avg_apply_latency_ms += rhs.avg_apply_latency_ms;
  avg_throughput_kbps += rhs.avg_throughput_kbps;
  mbs_received += rhs.mbs_received;
  records_received += rhs.records_received;
  // Get the earliest poll time.
  if (!last_poll_time || (rhs.last_poll_time && rhs.last_poll_time < last_poll_time)) {
    last_poll_time = rhs.last_poll_time;
  }
  if (status.ok() && !rhs.status.ok()) {
    static const auto errors_found = STATUS(IllegalState, "Errors found");
    status = errors_found;
  }
}

void XClusterPollerStats::operator/=(size_t divisor) {
  if (divisor <= 0) {
    return;
  }
  avg_poll_delay_ms /= divisor;
  avg_get_changes_latency_ms /= divisor;
  avg_apply_latency_ms /= divisor;
}

void PollStatsHistory::RecordBeginPoll() {
  std::lock_guard l(mutex_);
  current_poll_info_ = CurrentPollInfo();
  current_poll_info_.start_poll_time = MonoTime::Now();
}

void PollStatsHistory::RecordEndGetChanges() {
  std::lock_guard l(mutex_);
  current_poll_info_.get_changes_completion_time = MonoTime::Now();
}

void PollStatsHistory::RecordBeginApplyChanges() {
  std::lock_guard l(mutex_);
  // Only set once as we can retry this on failures.
  if (!current_poll_info_.start_apply_time) {
    current_poll_info_.start_apply_time = MonoTime::Now();
  }
}

void PollStatsHistory::RecordEndApplyChanges() {
  std::lock_guard l(mutex_);
  current_poll_info_.end_apply_time = MonoTime::Now();
}

void PollStatsHistory::SetError(Status&& error) {
  std::lock_guard l(mutex_);
  last_status_ = std::move(error);
}

void PollStatsHistory::RecordEndPoll(
    int num_records, int64_t received_index, size_t bytes_received) {
  std::lock_guard l(mutex_);
  last_end_poll_time_ = MonoTime::Now();
  last_status_ = Status::OK();
  received_index_ = received_index;

  if (num_records > 0) {
    records_received_ += num_records;
  }

  mbs_received_ += (bytes_received * 1.0 / 1_MB);

  if (!current_poll_info_.start_poll_time) {
    return;
  }

  PollStatEntry entry;
  entry.start_time = current_poll_info_.start_poll_time;
  entry.kbs_received = bytes_received * 1.0 / 1_KB;
  if (!buffer_.empty()) {
    entry.poll_delay = current_poll_info_.start_poll_time - buffer_.back().start_time;
  }

  if (current_poll_info_.get_changes_completion_time) {
    entry.get_changes_latency =
        current_poll_info_.get_changes_completion_time - current_poll_info_.start_poll_time;
  }

  if (current_poll_info_.start_apply_time && current_poll_info_.end_apply_time) {
    entry.apply_latency = current_poll_info_.end_apply_time - current_poll_info_.start_apply_time;
  }

  current_poll_info_ = CurrentPollInfo();
  buffer_.push_back(std::move(entry));
}

void PollStatsHistory::PopulateStats(XClusterPollerStats* stats) const {
  SharedLock l(mutex_);
  stats->mbs_received = mbs_received_;
  stats->records_received = records_received_;
  stats->received_index = received_index_;
  if (current_poll_info_.start_poll_time) {
    stats->last_poll_time = current_poll_info_.start_poll_time;
  } else if (!buffer_.empty()) {
    stats->last_poll_time = buffer_.back().start_time;
  }
  stats->status = last_status_;

  MonoDelta avg_poll_delay = MonoDelta::kZero;
  int32 avg_poll_delay_count = 0;
  MonoDelta avg_get_changes_latency = MonoDelta::kZero;
  int32 avg_get_changes_latency_count = 0;
  MonoDelta avg_apply_latency = MonoDelta::kZero;
  int32 avg_apply_latency_count = 0;
  double kbs_received = 0;
  if (buffer_.empty()) {
    return;
  }

  for (const auto& entry : buffer_) {
    if (entry.poll_delay) {
      avg_poll_delay_count++;
      avg_poll_delay += entry.poll_delay;
    }
    if (entry.get_changes_latency) {
      avg_get_changes_latency_count++;
      avg_get_changes_latency += entry.get_changes_latency;
    }
    if (entry.apply_latency) {
      avg_apply_latency_count++;
      avg_apply_latency += entry.apply_latency;
    }
    kbs_received += entry.kbs_received;
  }
  if (avg_poll_delay_count > 0) {
    avg_poll_delay /= avg_poll_delay_count;
  }
  if (avg_get_changes_latency_count > 0) {
    avg_get_changes_latency /= avg_get_changes_latency_count;
  }
  if (avg_apply_latency_count > 0) {
    avg_apply_latency /= avg_apply_latency_count;
  }

  const auto total_time = last_end_poll_time_ - buffer_.front().start_time;
  const auto avg_throughput_kbps = kbs_received / total_time.ToSeconds();

  stats->avg_poll_delay_ms = avg_poll_delay.ToMilliseconds();
  stats->avg_get_changes_latency_ms = avg_get_changes_latency.ToMilliseconds();
  stats->avg_apply_latency_ms = avg_apply_latency.ToMilliseconds();
  stats->avg_throughput_kbps = avg_throughput_kbps;
}
}  // namespace yb
