// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/master_fwd.h"

namespace yb {
namespace master {

// Per-database minimum of the pins reported by live tservers, maintained from each tserver's
// successive reports so that reading the cluster map never walks the descriptors.
class YsqlDbPinIndex {
 public:
  // old_pins and new_pins are the tserver's previous and current report.
  void Update(
      const TabletServerId& ts, const OrderedDbOidToHybridTimeMap& old_pins,
      const OrderedDbOidToHybridTimeMap& new_pins) EXCLUDES(mutex_);

  void Remove(
      const TabletServerId& ts, const OrderedDbOidToHybridTimeMap& old_pins) EXCLUDES(mutex_) {
    Update(ts, old_pins, {});
  }

  void Clear() EXCLUDES(mutex_);

  OrderedDbOidToHybridTimeMap Get() const EXCLUDES(mutex_);

 private:
  using Entry = std::pair<HybridTime, TabletServerId>;

  void RefreshDbUnlocked(PgOid db_oid) REQUIRES(mutex_);

  mutable rw_spinlock mutex_;
  std::unordered_map<PgOid, std::set<Entry>> pins_by_db_ GUARDED_BY(mutex_);
  OrderedDbOidToHybridTimeMap cluster_pins_ GUARDED_BY(mutex_);
};

}  // namespace master
}  // namespace yb
