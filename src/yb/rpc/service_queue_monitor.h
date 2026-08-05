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

#include <memory>
#include <string>

#include "yb/rpc/rpc_fwd.h"

namespace yb::rpc {

// Watchdog that polls the queue depth of the RPC services registered with a messenger and, when
// any queue stays at or above a configured threshold for several consecutive polls, dumps the
// stacks of all threads to the log. This automates the manual /threadz capture that is otherwise
// needed to diagnose why RPC worker threads are not draining the service queues.
//
// Disabled unless FLAGS_rpc_queue_stack_dump_threshold > 0. All controlling flags are runtime
// flags defined in service_queue_monitor.cc, so the watchdog can be enabled on a live server.
// While the condition persists, subsequent dumps are suppressed with an exponential backoff.
class ServiceQueueMonitor {
 public:
  explicit ServiceQueueMonitor(const std::string& name);
  ~ServiceQueueMonitor();

  void Shutdown();

  // Starts monitoring the queue of the given service. The monitor thread is started on first use.
  void Track(const std::string& service_name, const RpcServicePtr& service);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::rpc
