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

#ifndef KUDU_TABLET_TABLET_MM_OPS_H_
#define KUDU_TABLET_TABLET_MM_OPS_H_

#include "kudu/tablet/maintenance_manager.h"

namespace kudu {

class Histogram;
template<class T>
class AtomicGauge;

namespace tablet {

// MaintenanceOp for rowset compaction.
//
// This periodically invokes the tablet's CompactionPolicy to select a compaction.  The
// compaction policy's "quality" is used as a proxy for the performance improvement which
// is exposed back to the maintenance manager. As compactions become more fruitful (i.e.
// more overlapping rowsets), the perf_improvement score goes up, increasing priority
// with which a compaction on this tablet will be selected by the maintenance manager.
class CompactRowSetsOp : public MaintenanceOp {
 public:
  explicit CompactRowSetsOp(Tablet* tablet);

  virtual void UpdateStats(MaintenanceOpStats* stats) OVERRIDE;

  virtual bool Prepare() OVERRIDE;

  virtual void Perform() OVERRIDE;

  virtual scoped_refptr<Histogram> DurationHistogram() const OVERRIDE;

  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const OVERRIDE;

 private:
  mutable simple_spinlock lock_;
  MaintenanceOpStats prev_stats_;
  uint64_t last_num_mrs_flushed_;
  uint64_t last_num_rs_compacted_;
  Tablet* const tablet_;
};

// MaintenanceOp to run minor compaction on delta stores.
//
// There is only one MinorDeltaCompactionOp per tablet, so it picks the RowSet that needs the most
// work. The RS we end up compacting in Perform() can be different than the one reported in
// UpdateStats, we just pick the worst each time.
class MinorDeltaCompactionOp : public MaintenanceOp {
 public:
  explicit MinorDeltaCompactionOp(Tablet* tablet);

  virtual void UpdateStats(MaintenanceOpStats* stats) OVERRIDE;

  virtual bool Prepare() OVERRIDE;

  virtual void Perform() OVERRIDE;

  virtual scoped_refptr<Histogram> DurationHistogram() const OVERRIDE;

  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const OVERRIDE;

 private:
  mutable simple_spinlock lock_;
  MaintenanceOpStats prev_stats_;
  uint64_t last_num_mrs_flushed_;
  uint64_t last_num_dms_flushed_;
  uint64_t last_num_rs_compacted_;
  uint64_t last_num_rs_minor_delta_compacted_;
  Tablet* const tablet_;
};

// MaintenanceOp to run major compaction on delta stores.
//
// It functions just like MinorDeltaCompactionOp does, except it runs major compactions.
class MajorDeltaCompactionOp : public MaintenanceOp {
 public:
  explicit MajorDeltaCompactionOp(Tablet* tablet);

  virtual void UpdateStats(MaintenanceOpStats* stats) OVERRIDE;

  virtual bool Prepare() OVERRIDE;

  virtual void Perform() OVERRIDE;

  virtual scoped_refptr<Histogram> DurationHistogram() const OVERRIDE;

  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const OVERRIDE;

 private:
  mutable simple_spinlock lock_;
  MaintenanceOpStats prev_stats_;
  uint64_t last_num_mrs_flushed_;
  uint64_t last_num_dms_flushed_;
  uint64_t last_num_rs_compacted_;
  uint64_t last_num_rs_minor_delta_compacted_;
  uint64_t last_num_rs_major_delta_compacted_;
  Tablet* const tablet_;
};

} // namespace tablet
} // namespace kudu

#endif /* KUDU_TABLET_TABLET_MM_OPS_H_ */
