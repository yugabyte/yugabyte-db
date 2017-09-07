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

#include "yb/rpc/service_pool.h"

#include <memory>
#include <string>
#include <vector>

#include <glog/logging.h>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/inbound_call.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/tasks_pool.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"
#include "yb/util/trace.h"

using std::shared_ptr;
using strings::Substitute;

METRIC_DEFINE_histogram(server, rpc_incoming_queue_time,
                        "RPC Queue Time",
                        yb::MetricUnit::kMicroseconds,
                        "Number of microseconds incoming RPC requests spend in the worker queue",
                        60000000LU, 3);

METRIC_DEFINE_counter(server, rpcs_timed_out_in_queue,
                      "RPC Queue Timeouts",
                      yb::MetricUnit::kRequests,
                      "Number of RPCs whose timeout elapsed while waiting "
                      "in the service queue, and thus were not processed.");

METRIC_DEFINE_counter(server, rpcs_queue_overflow,
                      "RPC Queue Overflows",
                      yb::MetricUnit::kRequests,
                      "Number of RPCs dropped because the service queue "
                      "was full.");

namespace yb {
namespace rpc {

namespace {

class InboundCallTask final {
 public:
  InboundCallTask(ServicePoolImpl* pool, InboundCallPtr call)
      : pool_(pool), call_(std::move(call)) {
  }

  void Run();
  void Done(const Status& status);

 private:
  ServicePoolImpl* pool_;
  InboundCallPtr call_;
};

} // namespace

class ServicePoolImpl {
 public:
  ServicePoolImpl(size_t max_tasks,
       ThreadPool* thread_pool,
       std::unique_ptr<ServiceIf> service,
       const scoped_refptr<MetricEntity>& entity)
      : thread_pool_(thread_pool),
        service_(std::move(service)),
        incoming_queue_time_(METRIC_rpc_incoming_queue_time.Instantiate(entity)),
        rpcs_timed_out_in_queue_(METRIC_rpcs_timed_out_in_queue.Instantiate(entity)),
        rpcs_queue_overflow_(METRIC_rpcs_queue_overflow.Instantiate(entity)),
        tasks_pool_(max_tasks) {
  }

  ~ServicePoolImpl() {
    Shutdown();
  }

  void Shutdown() {
    bool closing_state = false;
    if (closing_.compare_exchange_strong(closing_state, true)) {
      service_->Shutdown();
    }
  }

  void Enqueue(InboundCallPtr call) {
    TRACE_TO(call->trace(), "Inserting onto call queue");

    if (!tasks_pool_.Enqueue(thread_pool_, this, std::move(call))) {
      Overflow(call, "service", tasks_pool_.size());
    }
  }

  const Counter* RpcsTimedOutInQueueMetricForTests() const {
    return rpcs_timed_out_in_queue_.get();
  }

  const Counter* RpcsQueueOverflowMetric() const {
    return rpcs_queue_overflow_.get();
  }

  std::string service_name() const {
    return service_->service_name();
  }

  void Overflow(const InboundCallPtr& call, const char* type, size_t limit) {
    const auto err_msg =
        Substitute("$0 request on $1 from $2 dropped due to backpressure. "
                   "The $3 queue is full, it has $4 items.",
            call->method_name(),
            service_->service_name(),
            yb::ToString(call->remote_address()),
            type,
            limit);
    LOG(WARNING) << err_msg;
    const auto response_status = STATUS(ServiceUnavailable, err_msg);
    rpcs_queue_overflow_->Increment();
    call->RespondFailure(ErrorStatusPB::ERROR_SERVER_TOO_BUSY, response_status);
  }

  void Processed(const InboundCallPtr& call, const Status& status) {
    if (status.ok()) {
      return;
    }
    if (status.IsServiceUnavailable()) {
      Overflow(call, "global", thread_pool_->options().queue_limit);
      return;
    }
    LOG(WARNING) << call->method_name() << " request on "
                 << service_->service_name() << " from " << call->remote_address()
                 << " dropped because of: " << status.ToString();
    const auto response_status = STATUS(ServiceUnavailable, "Service is shutting down");
    call->RespondFailure(ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN, response_status);
  }

  void Handle(InboundCallPtr incoming) {
    incoming->RecordHandlingStarted(incoming_queue_time_);
    ADOPT_TRACE(incoming->trace());

    if (PREDICT_FALSE(incoming->ClientTimedOut())) {
      TRACE_TO(incoming->trace(), "Skipping call since client already timed out");
      rpcs_timed_out_in_queue_->Increment();

      // Respond as a failure, even though the client will probably ignore
      // the response anyway.
      incoming->RespondFailure(
          ErrorStatusPB::ERROR_SERVER_TOO_BUSY,
          STATUS(TimedOut, "Call waited in the queue past client deadline"));

      return;
    }

    TRACE_TO(incoming->trace(), "Handling call");

    service_->Handle(std::move(incoming));
  }

 private:
  ThreadPool* thread_pool_;
  std::unique_ptr<ServiceIf> service_;
  scoped_refptr<Histogram> incoming_queue_time_;
  scoped_refptr<Counter> rpcs_timed_out_in_queue_;
  scoped_refptr<Counter> rpcs_queue_overflow_;

  std::atomic<bool> closing_ = {false};
  TasksPool<InboundCallTask> tasks_pool_;
};

void InboundCallTask::Run() {
  pool_->Handle(call_);
}

void InboundCallTask::Done(const Status& status) {
  InboundCallPtr call = call_;
  pool_->Processed(call, status);
}

ServicePool::ServicePool(size_t max_tasks,
                         ThreadPool* thread_pool,
                         std::unique_ptr<ServiceIf> service,
                         const scoped_refptr<MetricEntity>& metric_entity)
    : impl_(new ServicePoolImpl(max_tasks, thread_pool, std::move(service), metric_entity)) {
}

ServicePool::~ServicePool() {
}

void ServicePool::Shutdown() {
  impl_->Shutdown();
}

void ServicePool::QueueInboundCall(InboundCallPtr call) {
  impl_->Enqueue(std::move(call));
}

const Counter* ServicePool::RpcsTimedOutInQueueMetricForTests() const {
  return impl_->RpcsTimedOutInQueueMetricForTests();
}

const Counter* ServicePool::RpcsQueueOverflowMetric() const {
  return impl_->RpcsQueueOverflowMetric();
}

std::string ServicePool::service_name() const {
  return impl_->service_name();
}

} // namespace rpc
} // namespace yb
