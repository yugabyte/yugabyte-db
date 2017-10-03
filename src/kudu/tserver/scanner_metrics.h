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
#ifndef KUDU_TSERVER_SCANNER_METRICS_H
#define KUDU_TSERVER_SCANNER_METRICS_H

#include "kudu/gutil/ref_counted.h"

namespace kudu {

class MetricEntity;
class Counter;
class Histogram;
class MonoTime;

namespace tserver {

// Keeps track of scanner related metrics for a given ScannerManager
// instance.
struct ScannerMetrics {
  explicit ScannerMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  // Adds the the number of microseconds that have passed since
  // 'time_started' to 'scanner_duration' histogram.
  void SubmitScannerDuration(const MonoTime& time_started);

  // Keeps track of the total number of scanners that have been
  // expired since the start of service.
  scoped_refptr<Counter> scanners_expired;

  // Keeps track of the duration of scanners.
  scoped_refptr<Histogram> scanner_duration;
};

} // namespace tserver
} // namespace kudu

#endif // KUDU_TSERVER_SCANNER_METRICS_H
