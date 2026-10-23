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

#include <unordered_map>
#include <vector>

#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"

namespace yb {

// Which of a node's addresses a caller could not connect to, so it works through the rest
// before treating the node itself as unreachable. A record ages out after
// retry_failed_address_ms, for the same reason a failed replica is retried rather than
// written off: a transient failure would otherwise condemn an address for the life of the
// process, and the one that fails first is usually the one the deployment prefers.
//
// Failed applies that window itself, the way ConcurrentPod::Load applies its own, so that
// every caller reaches the same answer about the same address at the same moment.
//
// Dialers that reach the same nodes can share one record. A dialer that picks an address from
// it and moves off that address as one step holds its own lock around both.
class FailedAddresses {
 public:
  bool Failed(const HostPort& host_port) const EXCLUDES(mutex_);
  void MarkFailed(const HostPort& host_port) EXCLUDES(mutex_);
  void Clear() EXCLUDES(mutex_);

 private:
  mutable simple_spinlock mutex_;
  std::unordered_map<HostPort, MonoTime, HostPortHash> last_failed_ GUARDED_BY(mutex_);
};

// The addresses a dialer's pick passed over because they had failed, the ones ahead of it in
// the dialer's order. A dialer moves off an address only when that address fails, so one that
// fell back would stay on the fallback for as long as the fallback works; the pick is due again
// once an address it passed over no longer counts as failed, and the dialer picks again.
//
// Due reads the record the pick was made from rather than timing the pick, because the record
// can outlast the pick: a call that dialed the address before the dialer moved reports its
// failure afterwards, and another dialer sharing the record can mark the address again. A clock
// of the pick's own would come due while the address still counts as failed, and the pick made
// then would hold the fallback for another whole window. A pick that passed over nothing is
// never due, so a dialer on its preferred address pays nothing for this.
//
// The dialer holds the lock that guards its address.
class AddressPick {
 public:
  AddressPick() = default;
  explicit AddressPick(std::vector<HostPort> passed_over);

  bool Due(const FailedAddresses& failed) const;

 private:
  std::vector<HostPort> passed_over_;
};

}  // namespace yb
