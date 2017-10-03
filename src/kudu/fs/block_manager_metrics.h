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
#ifndef KUDU_FS_BLOCK_MANAGER_METRICS_H
#define KUDU_FS_BLOCK_MANAGER_METRICS_H

#include <stdint.h>

#include "kudu/gutil/ref_counted.h"

namespace kudu {

class Counter;
template<class T>
class AtomicGauge;
class MetricEntity;

namespace fs {
namespace internal {

struct BlockManagerMetrics {
  explicit BlockManagerMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  scoped_refptr<AtomicGauge<uint64_t> > blocks_open_reading;
  scoped_refptr<AtomicGauge<uint64_t> > blocks_open_writing;

  scoped_refptr<Counter> total_readable_blocks;
  scoped_refptr<Counter> total_writable_blocks;
  scoped_refptr<Counter> total_bytes_read;
  scoped_refptr<Counter> total_bytes_written;
};

} // namespace internal
} // namespace fs
} // namespace kudu

#endif // KUDU_FS_BLOCK_MANAGER_METRICS_H
