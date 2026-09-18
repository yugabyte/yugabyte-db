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

#include <cstdint>
#include <memory>
#include <string>

#include "yb/rpc/rpc_fwd.h"

namespace yb::rpc {

// Process-wide watchdog that polls RPC service queue depths and, when any queue stays at or above
// a configured threshold, dumps the stacks of the affected RPC worker threads to the log. This
// automates the manual /threadz capture otherwise needed to diagnose why workers are not draining
// service queues.
//
// Disabled unless FLAGS_rpc_queue_stack_dump_threshold > 0. All controlling flags are runtime
// flags defined in service_queue_monitor.cc, so the watchdog can be enabled on a live server.
// While the condition persists, subsequent dumps are suppressed with an exponential backoff.
// Each instance is a lightweight registration for one messenger; all instances share one worker.
class ServiceQueueMonitor {
 public:
  explicit ServiceQueueMonitor(const std::string& name);
  ~ServiceQueueMonitor();

  ServiceQueueMonitor(const ServiceQueueMonitor&) = delete;
  ServiceQueueMonitor& operator=(const ServiceQueueMonitor&) = delete;

  void Shutdown();

  // Starts monitoring the queue of the given service. The process-wide worker starts on first use.
  void Track(
      const std::string& service_name, const RpcServicePtr& service,
      ServicePriority priority = ServicePriority::kNormal);

 private:
  class Impl;
  static std::shared_ptr<Impl> SharedImpl();

  const std::string name_;
  std::shared_ptr<Impl> impl_;
  const uint64_t owner_id_;
};

}  // namespace yb::rpc
