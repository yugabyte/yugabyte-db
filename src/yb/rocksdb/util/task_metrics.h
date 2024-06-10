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

#pragma once

#include <memory>
#include <string>

#include "yb/rocksdb/listener.h"

#include "yb/util/metrics.h"

namespace rocksdb {

struct CompactionInfo {
  uint64_t input_files_count;
  uint64_t input_bytes_count;
  CompactionReason compaction_reason;
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

// Contains metrics related to compaction tasks started for particular compaction reasons.
struct RocksDBTaskStateMetrics {
  RocksDBTaskMetrics background;
  RocksDBTaskMetrics full;
  RocksDBTaskMetrics postsplit;
  RocksDBTaskMetrics total; // deprecated, TODO remove as part of GI-15048

  // Returns a pointer to the RocksDBTaskMetrics struct that corresponds to the
  // given CompactionReason.
  RocksDBTaskMetrics* TaskMetricsByCompactionReason(const CompactionReason reason) {
    switch (reason) {
      // Background compactions.
      case CompactionReason::kUnknown:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kLevelL0FilesNum:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kLevelMaxLevelSize:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kUniversalSizeAmplification:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kUniversalSizeRatio:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kUniversalSortedRunNum:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kUniversalDirectDeletion:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kFIFOMaxSize:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kFilesMarkedForCompaction:
        return &background;
      // Full compactions.
      case CompactionReason::kManualCompaction:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kAdminCompaction:
        FALLTHROUGH_INTENDED;
      case CompactionReason::kScheduledFullCompaction:
        return &full;
      // Post-split compactions.
      case CompactionReason::kPostSplitCompaction:
        return &postsplit;
    }
    FATAL_INVALID_ENUM_VALUE(CompactionReason, reason);
  }
};

// Contains metrics related to tasks of each particular state in a PriorityThreadPool.
struct RocksDBPriorityThreadPoolMetrics {
  RocksDBTaskStateMetrics queued; // deprecated, TODO remove as part of GI-15048
  RocksDBTaskStateMetrics paused; // deprecated, TODO remove as part of GI-15048
  RocksDBTaskStateMetrics active;
  RocksDBTaskStateMetrics nonactive;
};

// ROCKSDB_TASK_METRICS_DEFINE, ROCKSDB_COMPACTION_TASK_METRICS_TYPE_DEFINE,
// and ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE are helpers which
// define the metrics required for a PriorityTaskMetrics object. Example usage:
//
// At the top of the file:
// ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE(server);
#define ROCKSDB_TASK_METRICS_DEFINE(entity, name, label) \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _tasks_added), \
        label " Tasks Added", yb::MetricUnit::kTasks, \
        label " - compaction tasks added gauge."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _tasks_removed), \
        label " Tasks Removed", yb::MetricUnit::kTasks, \
        label " - compaction tasks removed gauge."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _input_files_added), \
        label " Total Files Added", yb::MetricUnit::kFiles, \
        label " - total files in added by compaction tasks."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _input_files_removed), \
        label " Total Files Removed", yb::MetricUnit::kFiles, \
        label " - total files in removed by compaction tasks."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _input_bytes_added), \
        label " Total Bytes Added", yb::MetricUnit::kBytes, \
        label " - total file size added by compaction tasks, bytes."); \
    METRIC_DEFINE_gauge_uint64(entity, BOOST_PP_CAT(name, _input_bytes_removed), \
        label " Total Bytes Removed", yb::MetricUnit::kBytes, \
        label " - total file size removed by compaction tasks, bytes.")

#define ROCKSDB_COMPACTION_TASK_METRICS_TYPE_DEFINE(entity, name, label) \
    ROCKSDB_TASK_METRICS_DEFINE(entity, BOOST_PP_CAT(name, _background_compaction), \
        label " RocksDB Background Compaction"); \
    ROCKSDB_TASK_METRICS_DEFINE(entity, BOOST_PP_CAT(name, _full_compaction), \
        label " RocksDB Full Compaction"); \
    ROCKSDB_TASK_METRICS_DEFINE(entity, BOOST_PP_CAT(name, _post_split_compaction), \
        label " RocksDB Post-Split Compaction"); \
    ROCKSDB_TASK_METRICS_DEFINE(entity, BOOST_PP_CAT(name, _task_metrics_compaction), \
        label " RocksDB total compactions (DEPRECATED)");

#define ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE(entity) \
    ROCKSDB_COMPACTION_TASK_METRICS_TYPE_DEFINE( \
        entity, queued, "Queued (DEPRECATED)"); \
    ROCKSDB_COMPACTION_TASK_METRICS_TYPE_DEFINE( \
        entity, paused, "Paused (DEPRECATED)"); \
    ROCKSDB_COMPACTION_TASK_METRICS_TYPE_DEFINE( \
        entity, active, "Active"); \
    ROCKSDB_COMPACTION_TASK_METRICS_TYPE_DEFINE( \
        entity, nonactive, "Non-Active");

// ROCKSDB_TASK_METRICS_INSTANCE, ROCKSDB_COMPACTION_TASK_METRICS_TYPE_INSTANCE,
// and ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE are helpers which
// instantiate the metrics required for a RocksDBPriorityThreadPoolMetrics object. Example usage:
//
// auto priority_thread_pool_metrics =
//     std::make_shared<rocksdb::RocksDBPriorityThreadPoolMetrics>(
//          ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE(metric_entity));
#define ROCKSDB_TASK_METRICS_INSTANCE(entity, name) { \
    entity->FindOrCreateMetric<AtomicGauge<uint64_t>>(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _tasks_added)), uint64_t(0)), \
    entity->FindOrCreateMetric<AtomicGauge<uint64_t>>(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _tasks_removed)), uint64_t(0)), \
    entity->FindOrCreateMetric<AtomicGauge<uint64_t>>(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _input_files_added)), uint64_t(0)), \
    entity->FindOrCreateMetric<AtomicGauge<uint64_t>>(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _input_files_removed)), uint64_t(0)), \
    entity->FindOrCreateMetric<AtomicGauge<uint64_t>>(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _input_bytes_added)), uint64_t(0)), \
    entity->FindOrCreateMetric<AtomicGauge<uint64_t>>(&BOOST_PP_CAT(METRIC_, \
        BOOST_PP_CAT(name, _input_bytes_removed)), uint64_t(0)) \
}

#define ROCKSDB_COMPACTION_TASK_METRICS_TYPE_INSTANCE(entity, name) \
  rocksdb::RocksDBTaskStateMetrics { \
    ROCKSDB_TASK_METRICS_INSTANCE(entity, BOOST_PP_CAT(name, _background_compaction)), \
    ROCKSDB_TASK_METRICS_INSTANCE(entity, BOOST_PP_CAT(name, _full_compaction)), \
    ROCKSDB_TASK_METRICS_INSTANCE(entity, BOOST_PP_CAT(name, _post_split_compaction)), \
    ROCKSDB_TASK_METRICS_INSTANCE(entity, BOOST_PP_CAT(name, _task_metrics_compaction)) \
  }


#define ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE(entity) \
    rocksdb::RocksDBPriorityThreadPoolMetrics { \
        ROCKSDB_COMPACTION_TASK_METRICS_TYPE_INSTANCE(entity, queued), \
        ROCKSDB_COMPACTION_TASK_METRICS_TYPE_INSTANCE(entity, paused), \
        ROCKSDB_COMPACTION_TASK_METRICS_TYPE_INSTANCE(entity, active), \
        ROCKSDB_COMPACTION_TASK_METRICS_TYPE_INSTANCE(entity, nonactive) \
    }

}  // namespace rocksdb
