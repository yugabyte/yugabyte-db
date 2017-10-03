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

#ifndef KUDU_TABLET_TABLET_PEER_MM_OPS_H_
#define KUDU_TABLET_TABLET_PEER_MM_OPS_H_

#include "kudu/tablet/maintenance_manager.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/util/stopwatch.h"

namespace kudu {

class Histogram;
template<class T>
class AtomicGauge;

namespace tablet {

class FlushOpPerfImprovementPolicy {
 public:
  ~FlushOpPerfImprovementPolicy() {}

  // Sets the performance improvement based on the anchored ram if it's over the threshold,
  // else it will set it based on how long it has been since the last flush.
  static void SetPerfImprovementForFlush(MaintenanceOpStats* stats, double elapsed_ms);

 private:
  FlushOpPerfImprovementPolicy() {}
};

// Maintenance op for MRS flush. Only one can happen at a time.
class FlushMRSOp : public MaintenanceOp {
 public:
  explicit FlushMRSOp(TabletPeer* tablet_peer)
    : MaintenanceOp(StringPrintf("FlushMRSOp(%s)", tablet_peer->tablet()->tablet_id().c_str()),
                    MaintenanceOp::HIGH_IO_USAGE),
      tablet_peer_(tablet_peer) {
    time_since_flush_.start();
  }

  virtual void UpdateStats(MaintenanceOpStats* stats) OVERRIDE;

  virtual bool Prepare() OVERRIDE;

  virtual void Perform() OVERRIDE;

  virtual scoped_refptr<Histogram> DurationHistogram() const OVERRIDE;

  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const OVERRIDE;

 private:
  // Lock protecting time_since_flush_.
  mutable simple_spinlock lock_;
  Stopwatch time_since_flush_;

  TabletPeer *const tablet_peer_;
};

// Maintenance op for DMS flush.
// Reports stats for all the DMS this tablet contains but only flushes one in Perform().
class FlushDeltaMemStoresOp : public MaintenanceOp {
 public:
  explicit FlushDeltaMemStoresOp(TabletPeer* tablet_peer)
    : MaintenanceOp(StringPrintf("FlushDeltaMemStoresOp(%s)",
                                 tablet_peer->tablet()->tablet_id().c_str()),
                    MaintenanceOp::HIGH_IO_USAGE),
      tablet_peer_(tablet_peer) {
    time_since_flush_.start();
  }

  virtual void UpdateStats(MaintenanceOpStats* stats) OVERRIDE;

  virtual bool Prepare() OVERRIDE {
    return true;
  }

  virtual void Perform() OVERRIDE;

  virtual scoped_refptr<Histogram> DurationHistogram() const OVERRIDE;

  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const OVERRIDE;

 private:
  // Lock protecting time_since_flush_
  mutable simple_spinlock lock_;
  Stopwatch time_since_flush_;

  TabletPeer *const tablet_peer_;
};

// Maintenance task that runs log GC. Reports log retention that represents the amount of data
// that can be GC'd.
//
// Only one LogGC op can run at a time.
class LogGCOp : public MaintenanceOp {
 public:
  explicit LogGCOp(TabletPeer* tablet_peer);

  virtual void UpdateStats(MaintenanceOpStats* stats) OVERRIDE;

  virtual bool Prepare() OVERRIDE;

  virtual void Perform() OVERRIDE;

  virtual scoped_refptr<Histogram> DurationHistogram() const OVERRIDE;

  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const OVERRIDE;

 private:
  TabletPeer *const tablet_peer_;
  scoped_refptr<Histogram> log_gc_duration_;
  scoped_refptr<AtomicGauge<uint32_t> > log_gc_running_;
  mutable Semaphore sem_;
};

} // namespace tablet
} // namespace kudu

#endif /* KUDU_TABLET_TABLET_PEER_MM_OPS_H_ */
