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
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/walltime.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/faststring.h"
#include "yb/util/locks.h"

namespace yb {

class Thread;

class TimeSeries {
 public:
  void AddValue(double val);
  void SetValue(double val);

  double value() const;

 private:
  friend class TimeSeriesCollector;

  TimeSeries() :
    val_(0)
  {}

  mutable simple_spinlock lock_;
  double val_;

  DISALLOW_COPY_AND_ASSIGN(TimeSeries);
};

class TimeSeriesCollector {
 public:
  explicit TimeSeriesCollector(std::string scope);

  ~TimeSeriesCollector();

  std::shared_ptr<TimeSeries> GetTimeSeries(const std::string &key);
  void StartDumperThread();
  void StopDumperThread();

 private:
  void DumperThread();
  void BuildMetricsString(WallTime time_since_start, faststring *dst_buf) const;

  std::string scope_;

  typedef std::unordered_map<std::string, std::shared_ptr<TimeSeries> > SeriesMap;
  SeriesMap series_map_;
  mutable Mutex series_lock_;

  scoped_refptr<yb::Thread> dumper_thread_;

  // Latch used to stop the dumper_thread_. When the thread is started,
  // this is set to 1, and when the thread should exit, it is counted down.
  CountDownLatch exit_latch_;

  bool started_;

  DISALLOW_COPY_AND_ASSIGN(TimeSeriesCollector);
};

} // namespace yb
