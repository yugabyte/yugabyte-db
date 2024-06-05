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

#include <stdint.h>

#include <string>
#include <type_traits>
#include <vector>

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc_service.h"

#include "yb/util/status_fwd.h"
#include "yb/util/blocking_queue.h"
#include "yb/util/mutex.h"

namespace yb {

template<class T>
class AtomicGauge;

class Counter;
class EventStats;
class MetricEntity;
class Socket;

namespace rpc {

// A pool of threads that handle new incoming RPC calls.
// Also includes a queue that calls get pushed onto for handling by the pool.
class ServicePool : public RpcService {
 public:
  ServicePool(size_t max_tasks,
              ThreadPool* thread_pool,
              Scheduler* scheduler,
              ServiceIfPtr service,
              const scoped_refptr<MetricEntity>& metric_entity);
  virtual ~ServicePool();

  void StartShutdown() override;
  void CompleteShutdown() override;

  void FillEndpoints(RpcEndpointMap* map) override;
  void QueueInboundCall(InboundCallPtr call) override;
  void Handle(InboundCallPtr call) override;
  const Counter* RpcsTimedOutInQueueMetricForTests() const;
  const Counter* RpcsQueueOverflowMetric() const;
  std::string service_name() const;

  ServiceIfPtr TEST_get_service() const;
 private:
  std::unique_ptr<ServicePoolImpl> impl_;
};

} // namespace rpc
} // namespace yb
