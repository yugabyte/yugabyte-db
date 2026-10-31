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

#include "yb/master/ysql_db_pin_index.h"

#include <set>

#include "yb/util/shared_lock.h"

namespace yb {
namespace master {

void YsqlDbPinIndex::Update(
    const TabletServerId& ts, const OrderedDbOidToHybridTimeMap& old_pins,
    const OrderedDbOidToHybridTimeMap& new_pins) {
  struct Change {
    PgOid db_oid;
    HybridTime ht;
  };
  struct Moved {
    PgOid db_oid;
    HybridTime old_ht;
    HybridTime new_ht;
  };
  std::vector<Change> added, removed;
  std::vector<Moved> changed;
  // Both maps are ordered by db oid, so one merge pass yields every difference.
  auto o = old_pins.begin();
  auto n = new_pins.begin();
  while (o != old_pins.end() || n != new_pins.end()) {
    if (n == new_pins.end() || (o != old_pins.end() && o->first < n->first)) {
      removed.push_back({o->first, o->second});
      ++o;
    } else if (o == old_pins.end() || n->first < o->first) {
      added.push_back({n->first, n->second});
      ++n;
    } else {
      if (o->second != n->second) {
        changed.push_back({o->first, o->second, n->second});
      }
      ++o;
      ++n;
    }
  }
  if (added.empty() && removed.empty() && changed.empty()) {
    return;
  }

  std::lock_guard l(mutex_);
  for (const auto& c : removed) {
    pins_by_db_[c.db_oid].erase(Entry{c.ht, ts});
    RefreshDbUnlocked(c.db_oid);
  }
  for (const auto& c : added) {
    pins_by_db_[c.db_oid].emplace(c.ht, ts);
    RefreshDbUnlocked(c.db_oid);
  }
  for (const auto& c : changed) {
    auto& entries = pins_by_db_[c.db_oid];
    entries.erase(Entry{c.old_ht, ts});
    entries.emplace(c.new_ht, ts);
    RefreshDbUnlocked(c.db_oid);
  }
}

void YsqlDbPinIndex::Clear() {
  std::lock_guard l(mutex_);
  pins_by_db_.clear();
  cluster_pins_.clear();
}

OrderedDbOidToHybridTimeMap YsqlDbPinIndex::Get() const {
  SharedLock<decltype(mutex_)> l(mutex_);
  return cluster_pins_;
}

void YsqlDbPinIndex::RefreshDbUnlocked(PgOid db_oid) {
  auto it = pins_by_db_.find(db_oid);
  if (it->second.empty()) {
    pins_by_db_.erase(it);
    cluster_pins_.erase(db_oid);
  } else {
    cluster_pins_[db_oid] = it->second.begin()->first;
  }
}

}  // namespace master
}  // namespace yb
