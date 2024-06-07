// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/util/test_graph.h"

#include <mutex>

#include "yb/util/logging.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/walltime.h"
#include "yb/util/locks.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

using std::shared_ptr;
using std::string;

namespace yb {

void TimeSeries::AddValue(double val) {
  std::lock_guard l(lock_);
  val_ += val;
}

void TimeSeries::SetValue(double val) {
  std::lock_guard l(lock_);
  val_ = val;
}

double TimeSeries::value() const {
  std::lock_guard l(lock_);
  return val_;
}

TimeSeriesCollector::TimeSeriesCollector(std::string scope)
    : scope_(std::move(scope)), exit_latch_(0), started_(false) {}

TimeSeriesCollector::~TimeSeriesCollector() {
  if (started_) {
    StopDumperThread();
  }
}

shared_ptr<TimeSeries> TimeSeriesCollector::GetTimeSeries(const string &key) {
  MutexLock l(series_lock_);
  SeriesMap::const_iterator it = series_map_.find(key);
  if (it == series_map_.end()) {
    shared_ptr<TimeSeries> ts(new TimeSeries());
    series_map_[key] = ts;
    return ts;
  } else {
    return (*it).second;
  }
}

void TimeSeriesCollector::StartDumperThread() {
  LOG(INFO) << "Starting metrics dumper";
  CHECK(!started_);
  exit_latch_.Reset(1);
  started_ = true;
  CHECK_OK(yb::Thread::Create("time series", "dumper",
      &TimeSeriesCollector::DumperThread, this, &dumper_thread_));
}

void TimeSeriesCollector::StopDumperThread() {
  CHECK(started_);
  exit_latch_.CountDown();
  CHECK_OK(ThreadJoiner(dumper_thread_.get()).Join());
  started_ = false;
}

void TimeSeriesCollector::DumperThread() {
  CHECK(started_);
  WallTime start_time = WallTime_Now();

  faststring metrics_str;
  while (true) {
    metrics_str.clear();
    metrics_str.append("metrics: ");
    BuildMetricsString(WallTime_Now() - start_time, &metrics_str);
    LOG(INFO) << metrics_str.ToString();

    // Sleep until next dump time, or return if we should exit
    if (exit_latch_.WaitFor(MonoDelta::FromMilliseconds(250))) {
      return;
    }
  }
}

void TimeSeriesCollector::BuildMetricsString(
  WallTime time_since_start, faststring *dst_buf) const {
  MutexLock l(series_lock_);

  dst_buf->append(StringPrintf("{ \"scope\": \"%s\", \"time\": %.3f",
                               scope_.c_str(), time_since_start));

  for (SeriesMap::const_reference entry : series_map_) {
    dst_buf->append(StringPrintf(", \"%s\": %.3f",
                                 entry.first.c_str(),  entry.second->value()));
  }
  dst_buf->append("}");
}


} // namespace yb
