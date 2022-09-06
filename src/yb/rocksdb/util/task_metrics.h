// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2013 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

#ifndef YB_ROCKSDB_UTIL_TASK_METRICS_H
#define YB_ROCKSDB_UTIL_TASK_METRICS_H

#include <memory>
#include <string>

#include "yb/util/metrics.h"

namespace rocksdb {

struct CompactionInfo {
  uint64_t input_files_count;
  uint64_t input_bytes_count;
};

// Contains metrics related to a particular task state in the priority thread pool.
struct RocksDBTaskMetrics {
  // Measures the number of tasks.
  scoped_refptr<yb::AtomicGauge<uint64_t>> compaction_tasks_added_;
  scoped_refptr<yb::AtomicGauge<uint64_t>> compaction_tasks_removed_;

  // Measures the total number of files in compaction tasks.
  scoped_refptr<yb::AtomicGauge<uint64_t>> compaction_input_files_added_;
  scoped_refptr<yb::AtomicGauge<uint64_t>> compaction_input_files_removed_;

  // Measures the total size of files for compaction tasks (in bytes).
  scoped_refptr<yb::AtomicGauge<uint64_t>> compaction_input_bytes_added_;
  scoped_refptr<yb::AtomicGauge<uint64_t>> compaction_input_bytes_removed_;

  void CompactionTaskAdded(const CompactionInfo& info) {
    if (compaction_tasks_added_) {
      compaction_tasks_added_->Increment();
    }
    if (compaction_input_files_added_) {
      compaction_input_files_added_->IncrementBy(info.input_files_count);
    }
    if (compaction_input_bytes_added_) {
      compaction_input_bytes_added_->IncrementBy(info.input_bytes_count);
    }
  }

  void CompactionTaskRemoved(const CompactionInfo& info) {
    if (compaction_tasks_removed_) {
      compaction_tasks_removed_->Increment();
    }
    if (compaction_input_files_removed_) {
      compaction_input_files_removed_->IncrementBy(info.input_files_count);
    }
    if (compaction_input_bytes_removed_) {
      compaction_input_bytes_removed_->IncrementBy(info.input_bytes_count);
    }
  }
};

// Contains metrics related to tasks of each particular state in a PriorityThreadPool.
struct RocksDBPriorityThreadPoolMetrics {
  RocksDBTaskMetrics queued;
  RocksDBTaskMetrics paused;
  RocksDBTaskMetrics active;
};

// ROCKSDB_TASK_METRICS_DEFINE / ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE are helpers which
// define the metrics required for a PriorityTaskMetrics object. Example usage:
// // At the top of the file:
// ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE(server);
#define ROCKSDB_TASK_METRICS_DEFINE(entity, name, label) \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _compaction_tasks_added), \
        label " Compaction Tasks Added", yb::MetricUnit::kTasks, \
        label " - compaction tasks added gauge."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _compaction_tasks_removed), \
        label " Compaction Tasks Removed", yb::MetricUnit::kTasks, \
        label " - compaction tasks removed gauge."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _compaction_input_files_added), \
        label " Total Compaction Files Added", yb::MetricUnit::kFiles, \
        label " - total files in added by compaction tasks."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _compaction_input_files_removed), \
        label " Total Compaction Files Removed", yb::MetricUnit::kFiles, \
        label " - total files in removed by compaction tasks."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _compaction_input_bytes_added), \
        label " Total Compaction Bytes Added", yb::MetricUnit::kBytes, \
        label " - total file size added by compaction tasks, bytes."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _compaction_input_bytes_removed), \
        label " Total Compaction Bytes Removed", yb::MetricUnit::kBytes, \
        label " - total file size removed by compaction tasks, bytes.")

#define ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE(entity) \
    ROCKSDB_TASK_METRICS_DEFINE( \
        entity, queued_task_metrics, "Queued RocksDB compactions in priority thread pool."); \
    ROCKSDB_TASK_METRICS_DEFINE( \
        entity, paused_task_metrics, "Paused RocksDB compactions in priority thread pool."); \
    ROCKSDB_TASK_METRICS_DEFINE( \
        entity, active_task_metrics, "Active RocksDB compactions in priority thread pool.");

// ROCKSDB_TASK_METRICS_INSTANCE / ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE are helpers which
// instantiate the metrics required for a RocksDBPriorityThreadPoolMetrics object. Example usage:
//
// auto priority_thread_pool_metrics =
//     std::make_shared<rocksdb::RocksDBPriorityThreadPoolMetrics>(
//          ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE(metric_entity));
#define ROCKSDB_TASK_METRICS_INSTANCE(entity, name) { \
    entity->FindOrCreateGauge(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _compaction_tasks_added)), uint64_t(0)), \
    entity->FindOrCreateGauge(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _compaction_tasks_removed)), uint64_t(0)), \
    entity->FindOrCreateGauge(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _compaction_input_files_added)), uint64_t(0)), \
    entity->FindOrCreateGauge(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _compaction_input_files_removed)), uint64_t(0)), \
    entity->FindOrCreateGauge(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _compaction_input_bytes_added)), uint64_t(0)), \
    entity->FindOrCreateGauge(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _compaction_input_bytes_removed)), uint64_t(0)) \
}

#define ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE(entity) \
    rocksdb::RocksDBPriorityThreadPoolMetrics { \
        ROCKSDB_TASK_METRICS_INSTANCE(entity, queued_task_metrics), \
        ROCKSDB_TASK_METRICS_INSTANCE(entity, paused_task_metrics), \
        ROCKSDB_TASK_METRICS_INSTANCE(entity, active_task_metrics) \
    }

}  // namespace rocksdb

#endif  // YB_ROCKSDB_UTIL_TASK_METRICS_H
