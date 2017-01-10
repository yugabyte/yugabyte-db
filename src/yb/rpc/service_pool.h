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

#ifndef YB_SERVICE_POOL_H
#define YB_SERVICE_POOL_H

#include <string>
#include <vector>

#include "yb/gutil/macros.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/rpc_service.h"
#include "yb/util/blocking_queue.h"
#include "yb/util/mutex.h"
#include "yb/util/thread.h"
#include "yb/util/status.h"

#define SERVICE_POOL_OPTIONS(name, short_name) \
  ServicePoolOptions(#name, \
                     #short_name, \
                     FLAGS_ ## name ## _num_threads, \
                     FLAGS_ ## name ## _queue_length)

namespace yb {

class Counter;
class Histogram;
class MetricEntity;
class Socket;

namespace rpc {

class Messenger;
class ServiceIf;

struct ServicePoolOptions {
  ServicePoolOptions(const std::string& p_name,
                     const std::string& p_short_name,
                     uint32_t p_num_threads,
                     size_t p_queue_length)
  : name(p_name),
    short_name(p_short_name),
    num_threads(p_num_threads),
    queue_length(p_queue_length) {}
  const std::string name;       // Used for categorizing threads in this pool
  const std::string short_name; // Used as prefix for thread names
  const uint32_t num_threads;   // Number of threads that service this pool
  const size_t queue_length;    // Max queue length allowed by this service
};

// A pool of threads that handle new incoming RPC calls.
// Also includes a queue that calls get pushed onto for handling by the pool.
class ServicePool : public RpcService {
 public:
  ServicePool(ServicePoolOptions opts,
              gscoped_ptr<ServiceIf> service,
              const scoped_refptr<MetricEntity>& metric_entity);
  virtual ~ServicePool();

  // Start up the thread pool.
  virtual CHECKED_STATUS Init();

  // Shut down the queue and the thread pool.
  virtual void Shutdown();

  virtual CHECKED_STATUS QueueInboundCall(gscoped_ptr<InboundCall> call) OVERRIDE;

  const Counter* RpcsTimedOutInQueueMetricForTests() const {
    return rpcs_timed_out_in_queue_.get();
  }

  const Counter* RpcsQueueOverflowMetric() const {
    return rpcs_queue_overflow_.get();
  }

  const std::string service_name() const;

 private:
  void RunThread();
  const ServicePoolOptions options_;
  gscoped_ptr<ServiceIf> service_;
  std::vector<scoped_refptr<yb::Thread> > threads_;
  BlockingQueue<InboundCall*> service_queue_;
  scoped_refptr<Histogram> incoming_queue_time_;
  scoped_refptr<Counter> rpcs_timed_out_in_queue_;
  scoped_refptr<Counter> rpcs_queue_overflow_;

  mutable Mutex shutdown_lock_;
  bool closing_;

  DISALLOW_COPY_AND_ASSIGN(ServicePool);
};

} // namespace rpc
} // namespace yb

#endif
