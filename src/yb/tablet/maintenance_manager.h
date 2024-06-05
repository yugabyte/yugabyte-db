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

#include <stdint.h>

#include <condition_variable>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/tablet/tablet.pb.h"

#include "yb/util/condition_variable.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/threadpool.h"

namespace yb {

template<class T>
class AtomicGauge;
class EventStats;
class MaintenanceManager;
class MemTracker;

class MaintenanceOpStats {
 public:
  MaintenanceOpStats();

  // Zero all stats. They are invalid until the first setter is called.
  void Clear();

  bool runnable() const {
    DCHECK(valid_);
    return runnable_;
  }

  void set_runnable(bool runnable) {
    UpdateLastModified();
    runnable_ = runnable;
  }

  uint64_t ram_anchored() const {
    DCHECK(valid_);
    return ram_anchored_;
  }

  void set_ram_anchored(uint64_t ram_anchored) {
    UpdateLastModified();
    ram_anchored_ = ram_anchored;
  }

  int64_t logs_retained_bytes() const {
    DCHECK(valid_);
    return logs_retained_bytes_;
  }

  void set_logs_retained_bytes(int64_t logs_retained_bytes) {
    UpdateLastModified();
    logs_retained_bytes_ = logs_retained_bytes;
  }

  double perf_improvement() const {
    DCHECK(valid_);
    return perf_improvement_;
  }

  void set_perf_improvement(double perf_improvement) {
    UpdateLastModified();
    perf_improvement_ = perf_improvement;
  }

  const MonoTime& last_modified() const {
    DCHECK(valid_);
    return last_modified_;
  }

  bool valid() const {
    return valid_;
  }

 private:
  void UpdateLastModified() {
    valid_ = true;
    last_modified_ = MonoTime::Now();
  }

  // True if these stats are valid.
  bool valid_;

  // True if this op can be run now.
  bool runnable_;

  // The approximate amount of memory that not doing this operation keeps
  // around.  This number is used to decide when to start freeing memory, so it
  // should be fairly accurate.  May be 0.
  uint64_t ram_anchored_;

  // The approximate amount of disk space that not doing this operation keeps us from GCing from
  // the logs. May be 0.
  int64_t logs_retained_bytes_;

  // The estimated performance improvement-- how good it is to do this on some
  // absolute scale (yet TBD).
  double perf_improvement_;

  // The last time that the stats were modified.
  MonoTime last_modified_;
};

class ScopedMaintenanceOpRun;

// MaintenanceOp objects represent background operations that the
// MaintenanceManager can schedule.  Once a MaintenanceOp is registered, the
// manager will periodically poll it for statistics.  The registrant is
// responsible for managing the memory associated with the MaintenanceOp object.
// Op objects should be unregistered before being de-allocated.
class MaintenanceOp {
 public:
  friend class MaintenanceManager;

  // General indicator of how much IO the Op will use.
  enum IOUsage {
    LOW_IO_USAGE, // Low impact operations like removing a file, updating metadata.
    HIGH_IO_USAGE // Everything else.
  };

  explicit MaintenanceOp(std::string name, IOUsage io_usage);
  virtual ~MaintenanceOp();

  // Unregister this op, if it is currently registered.
  void Unregister();

  // Update the op statistics.  This will be called every scheduling period
  // (about a few times a second), so it should not be too expensive.  It's
  // possible for the returned statistics to be invalid; the caller should
  // call MaintenanceOpStats::valid() before using them.  This will be run
  // under the MaintenanceManager lock.
  virtual void UpdateStats(MaintenanceOpStats* stats) = 0;

  // Prepare to perform the operation.  This will be run without holding the
  // maintenance manager lock.  It should be short, since it is run from the
  // context of the maintenance op scheduler thread rather than a worker thread.
  // If this returns false, we will abort the operation.
  virtual bool Prepare() = 0;

  // Perform the operation.  This will be run without holding the maintenance
  // manager lock, and may take a long time.
  virtual void Perform() = 0;

  // Returns the histogram for this op that tracks duration. Cannot be NULL.
  virtual scoped_refptr<EventStats> DurationHistogram() const = 0;

  // Returns the gauge for this op that tracks when this op is running. Cannot be NULL.
  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const = 0;

  uint32_t running() { return running_; }

  std::string name() const { return name_; }

  IOUsage io_usage() const { return io_usage_; }

 private:
  friend class ScopedMaintenanceOpRun;

  // The name of the operation.  Op names must be unique.
  const std::string name_;

  // The number of times that this op is currently running.
  uint32_t running_ = 0;

  // Condition variable which the UnregisterOp function can wait on.
  //
  // Note: 'cond_' is used with the MaintenanceManager's mutex. As such,
  // it only exists when the op is registered.
  std::condition_variable cond_;

  // The MaintenanceManager with which this op is registered, or null
  // if it is not registered.
  std::shared_ptr<MaintenanceManager> manager_;

  IOUsage io_usage_;

  DISALLOW_COPY_AND_ASSIGN(MaintenanceOp);
};

struct MaintenanceOpComparator {
  bool operator() (const MaintenanceOp* lhs,
                   const MaintenanceOp* rhs) const {
    return lhs->name().compare(rhs->name()) < 0;
  }
};

// Holds the information regarding a recently completed operation.
struct CompletedOp {
  std::string name;
  MonoDelta duration;
  MonoTime start_mono_time;
};

// The MaintenanceManager manages the scheduling of background operations such
// as flushes or compactions.  It runs these operations in the background, in a
// thread pool.  It uses information provided in MaintenanceOpStats objects to
// decide which operations, if any, to run.
class MaintenanceManager : public std::enable_shared_from_this<MaintenanceManager> {
 public:
  struct Options {
    int32_t num_threads;
    int32_t polling_interval_ms;
    uint32_t history_size;
    std::shared_ptr<MemTracker> parent_mem_tracker;
  };

  explicit MaintenanceManager(const Options& options);
  ~MaintenanceManager();

  Status Init();
  void Shutdown();

  // Register an op with the manager.
  void RegisterOp(MaintenanceOp* op);

  // Unregister an op with the manager.
  // If the Op is currently running, it will not be interrupted.  However, this
  // function will block until the Op is finished.
  void UnregisterOp(MaintenanceOp* op);

  void GetMaintenanceManagerStatusDump(tablet::MaintenanceManagerStatusPB* out_pb);

  static const Options DEFAULT_OPTIONS;

 private:
  friend class ScopedMaintenanceOpRun;

  FRIEND_TEST(MaintenanceManagerTest, TestLogRetentionPrioritization);
  typedef std::map<MaintenanceOp*, MaintenanceOpStats, MaintenanceOpComparator> OpMapTy;

  void RunSchedulerThread();

  // find the best op, or null if there is nothing we want to run
  MaintenanceOp* FindBestOp() REQUIRES(mutex_);

  void LaunchOp(const ScopedMaintenanceOpRun& op);

  std::mutex mutex_;
  const int32_t num_threads_;
  const int32_t polling_interval_ms_;
  const std::shared_ptr<MemTracker> parent_mem_tracker_;

  OpMapTy ops_ GUARDED_BY(mutex_); // registered operations
  scoped_refptr<yb::Thread> monitor_thread_;
  std::unique_ptr<ThreadPool> thread_pool_;
  std::condition_variable cond_;
  bool shutdown_ GUARDED_BY(mutex_) = false;
  uint64_t running_ops_ = 0;
  // Vector used as a circular buffer for recently completed ops. Elements need to be added at
  // the completed_ops_count_ % the vector's size and then the count needs to be incremented.
  std::vector<CompletedOp> completed_ops_ GUARDED_BY(mutex_);
  int64_t completed_ops_count_ GUARDED_BY(mutex_) = 0;

  DISALLOW_COPY_AND_ASSIGN(MaintenanceManager);
};

class ScopedMaintenanceOpRun {
 public:
  explicit ScopedMaintenanceOpRun(MaintenanceOp* op);

  ScopedMaintenanceOpRun(const ScopedMaintenanceOpRun& rhs);
  void operator=(const ScopedMaintenanceOpRun& rhs);

  ScopedMaintenanceOpRun(ScopedMaintenanceOpRun&& rhs);
  void operator=(ScopedMaintenanceOpRun&& rhs);

  ~ScopedMaintenanceOpRun();

  void Reset();

  MaintenanceOp* get() const;

 private:
  void Assign(MaintenanceOp* op);

  MaintenanceOp* op_;
};

} // namespace yb
