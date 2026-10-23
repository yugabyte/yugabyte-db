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

}  // namespace yb
