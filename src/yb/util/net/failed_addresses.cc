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

#include "yb/util/net/failed_addresses.h"

#include "yb/util/flags.h"

using namespace std::literals;

DEFINE_UNKNOWN_int32(retry_failed_address_ms, 3600 * 1000,
    "Time in milliseconds to wait for before retrying an address of a node that could not be "
    "connected to, while the node's other addresses are used instead.");

namespace yb {

bool FailedAddresses::Failed(const HostPort& host_port) const {
  auto it = last_failed_.find(host_port);
  return it != last_failed_.end() &&
         (MonoTime::Now() - it->second) < FLAGS_retry_failed_address_ms * 1ms;
}

void FailedAddresses::MarkFailed(const HostPort& host_port) {
  last_failed_[host_port] = MonoTime::Now();
}

void FailedAddresses::Clear() {
  last_failed_.clear();
}

}  // namespace yb
