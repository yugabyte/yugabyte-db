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

#include "yb/common/entity_ids_types.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb::xrepl {
struct StreamTabletStats {
  std::string stream_id_str;
  TabletId producer_tablet_id;
  std::string producer_table_id;
  std::string state;
  double avg_throughput_kbps = 0;
  // Amount of data sent. This can be higher than actual data size if receiver reties the
  // GetChanges call due to remote errors.
  double mbs_sent = 0;
  uint64_t records_sent = 0;
  // Time between polls.
  uint64_t avg_poll_delay_ms = 0;
  // Time taken to process the GetChanges request.
  uint64_t avg_get_changes_latency_ms = 0;
  int64_t sent_index = 0;
  int64_t latest_index = 0;
  MonoTime last_poll_time;
  Status status;

  bool operator<(const StreamTabletStats& rhs) const;
  void operator+=(const StreamTabletStats& rhs);
  void operator/=(size_t divisor);
};

struct StreamTabletStatsHistory {
  mutable std::shared_mutex mutex_;

  struct StatsEntry {
    MonoTime start_time;
    MonoDelta poll_delay = MonoDelta::kZero;
    MonoDelta get_changes_latency = MonoDelta::kZero;
    double kbs_sent = 0;
  };
  constexpr static int kCircularBufferSize = 100;
  boost::circular_buffer<StatsEntry> buffer GUARDED_BY(mutex_) =
      boost::circular_buffer<StatsEntry>(kCircularBufferSize);

  double mbs_sent GUARDED_BY(mutex_) = 0;
  uint64_t records_sent GUARDED_BY(mutex_) = 0;
  int64_t last_sent_index GUARDED_BY(mutex_) = 0;
  int64_t latest_index GUARDED_BY(mutex_) = 0;
  MonoTime last_poll_time GUARDED_BY(mutex_);
  Status last_status GUARDED_BY(mutex_);

  void UpdateStats(
      const MonoTime& start_time, const Status& status, int num_records, size_t bytes_sent,
      int64_t sent_index, int64_t latest_wal_index) EXCLUDES(mutex_);
  void PopulateStats(StreamTabletStats* stats) const EXCLUDES(mutex_);
};
}  // namespace yb::xrepl
