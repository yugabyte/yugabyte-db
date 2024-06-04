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

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/util/monotime.h"

namespace yb {

template<class T>
class AtomicGauge;
class Counter;
class EventStats;
class MetricEntity;

namespace log {

struct LogMetrics {
  LogMetrics(const scoped_refptr<MetricEntity>& table_metric_entity,
             const scoped_refptr<MetricEntity>& tablet_metric_entity);

  // Global stats
  scoped_refptr<Counter> bytes_logged;
  scoped_refptr<AtomicGauge<uint64_t>> wal_size;

  // Per-group group commit stats
  scoped_refptr<EventStats> sync_latency;
  scoped_refptr<EventStats> append_latency;
  scoped_refptr<EventStats> group_commit_latency;
  scoped_refptr<EventStats> roll_latency;
  scoped_refptr<EventStats> entry_batches_per_group;
};

// TODO extract and generalize this for all histogram metrics
#define SCOPED_LATENCY_METRIC(_mtx, _h) \
  ScopedLatencyMetric<EventStats> _h##_metric(_mtx ? _mtx->_h.get() : nullptr)

} // namespace log
} // namespace yb
