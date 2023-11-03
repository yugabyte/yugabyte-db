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

#include "yb/cdc/xrepl_stream_stats.h"

#include "yb/util/shared_lock.h"
#include "yb/util/size_literals.h"

namespace yb::xrepl {
bool StreamTabletStats::operator<(const StreamTabletStats& rhs) const {
  // Errors at top, followed by streams with most data.
  return status.ok() && mbs_sent < rhs.mbs_sent;
}

void StreamTabletStats::operator+=(const StreamTabletStats& rhs) {
  avg_poll_delay_ms += rhs.avg_poll_delay_ms;
  avg_get_changes_latency_ms += rhs.avg_get_changes_latency_ms;
  avg_throughput_kbps += rhs.avg_throughput_kbps;
  mbs_sent += rhs.mbs_sent;
  records_sent += rhs.records_sent;
  // Get the earliest poll time.
  if (!last_poll_time || (rhs.last_poll_time && rhs.last_poll_time < last_poll_time)) {
    last_poll_time = rhs.last_poll_time;
  }
  if (status.ok() && !rhs.status.ok()) {
    static const auto errors_found = STATUS(IllegalState, "Errors found");
    status = errors_found;
  }
}

void StreamTabletStats::operator/=(size_t divisor) {
  if (divisor <= 0) {
    return;
  }
  avg_poll_delay_ms /= divisor;
  avg_get_changes_latency_ms /= divisor;
}

void StreamTabletStatsHistory::UpdateStats(
    const MonoTime& start_time, const Status& status, int num_records, size_t bytes_sent,
    int64_t sent_index, int64_t latest_wal_index) {
  std::lock_guard l(mutex_);
  last_poll_time = start_time;
  last_status = status;
  latest_index = latest_wal_index;
  last_sent_index = sent_index;
  records_sent += num_records;

  StatsEntry entry;
  entry.start_time = start_time;
  entry.get_changes_latency = last_poll_time - start_time;
  if (!buffer.empty()) {
    entry.poll_delay = start_time - buffer.back().start_time;
  }

  if (status.ok()) {
    entry.kbs_sent = bytes_sent * 1.0 / 1_KB;
    mbs_sent += (bytes_sent * 1.0 / 1_MB);
  }

  buffer.push_back(std::move(entry));
}

void StreamTabletStatsHistory::PopulateStats(StreamTabletStats* stats) const {
  SharedLock l_metadata(mutex_);
  stats->mbs_sent = mbs_sent;
  stats->records_sent = records_sent;
  stats->sent_index = last_sent_index;
  stats->latest_index = latest_index;
  stats->status = last_status;
  stats->last_poll_time = last_poll_time;

  if (buffer.empty()) {
    return;
  }

  MonoDelta avg_poll_delay = MonoDelta::kZero;
  MonoDelta avg_get_changes_latency = MonoDelta::kZero;
  double kbs_sent = 0;
  for (const auto& entry : buffer) {
    avg_poll_delay += entry.poll_delay;
    avg_get_changes_latency += entry.get_changes_latency;
    kbs_sent += entry.kbs_sent;
  }
  avg_poll_delay /= buffer.size();
  avg_get_changes_latency /= buffer.size();

  const auto total_time = last_poll_time - buffer.front().start_time;
  const auto avg_throughput_kbps = kbs_sent / total_time.ToSeconds();

  stats->avg_poll_delay_ms = avg_poll_delay.ToMilliseconds();
  stats->avg_get_changes_latency_ms = avg_get_changes_latency.ToMilliseconds();
  stats->avg_throughput_kbps = avg_throughput_kbps;
}

}  // namespace yb::xrepl
