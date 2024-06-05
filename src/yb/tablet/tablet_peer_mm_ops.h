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

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/stopwatch.h"

namespace yb {

class EventStats;
template<class T>
class AtomicGauge;

namespace tablet {

// Maintenance task that runs log GC. Reports log retention that represents the amount of data
// that can be GC'd.
//
// Only one LogGC op can run at a time.
class LogGCOp : public MaintenanceOp {
 public:
  explicit LogGCOp(TabletPeer* tablet_peer, const TabletPtr& tablet);

  virtual void UpdateStats(MaintenanceOpStats* stats) override;

  virtual bool Prepare() override;

  virtual void Perform() override;

  virtual scoped_refptr<EventStats> DurationHistogram() const override;

  virtual scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const override;

 private:
  TabletPtr tablet_;
  TabletPeer *const tablet_peer_;
  scoped_refptr<EventStats> log_gc_duration_;
  scoped_refptr<AtomicGauge<uint32_t> > log_gc_running_;
  mutable Semaphore sem_;
};

} // namespace tablet
} // namespace yb
