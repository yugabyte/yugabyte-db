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

  ServicePool::ServicePool(ServicePoolOptions opts,
                           gscoped_ptr<ServiceIf> service,
                           const scoped_refptr<MetricEntity>& entity)
  : options_(opts),
    service_(service.Pass()),
    service_queue_(opts.queue_length),
    incoming_queue_time_(METRIC_rpc_incoming_queue_time.Instantiate(entity)),
    rpcs_timed_out_in_queue_(METRIC_rpcs_timed_out_in_queue.Instantiate(entity)),
    rpcs_queue_overflow_(METRIC_rpcs_queue_overflow.Instantiate(entity)),
    closing_(false) {
}

ServicePool::~ServicePool() {
  Shutdown();
}

Status ServicePool::Init() {
  LOG(INFO) << "Create ServicePool " << options_.name << ": "
            << options_.num_threads << " threads, queue size " << options_.queue_length;
  for (int i = 0; i < options_.num_threads; i++) {
    scoped_refptr<yb::Thread> new_thread;
    CHECK_OK(yb::Thread::Create(options_.name, options_.short_name, &ServicePool::RunThread, this,
                                &new_thread));
    threads_.push_back(new_thread);
  }
  return Status::OK();
}

void ServicePool::Shutdown() {
  service_queue_.Shutdown();

  MutexLock lock(shutdown_lock_);
  if (closing_) return;
  closing_ = true;
  // TODO: Use a proper thread pool implementation.
  for (scoped_refptr<yb::Thread>& thread : threads_) {
    CHECK_OK(ThreadJoiner(thread.get()).Join());
  }

  // Now we must drain the service queue.
  Status status = STATUS(ServiceUnavailable, "Service is shutting down");
  InboundCallPtr incoming;
  while (service_queue_.BlockingGet(&incoming)) {
    incoming->RespondFailure(ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN, status);
  }

  service_->Shutdown();
}

Status ServicePool::QueueInboundCall(InboundCallPtr call) {
  TRACE_TO(call->trace(), "Inserting onto call queue");
  // Queue message on service queue
  QueueStatus queue_status = service_queue_.Put(call);
  if (PREDICT_TRUE(queue_status == QUEUE_SUCCESS)) {
    // NB: do not do anything with 'c' after it is successfully queued --
    // a service thread may have already dequeued it, processed it, and
    // responded by this point, in which case the pointer would be invalid.
    return Status::OK();
  }

  Status status = Status::OK();
  if (queue_status == QUEUE_FULL) {
    string err_msg =
        Substitute("$0 request on $1 from $2 dropped due to backpressure. "
        "The service queue is full; it has $3 items.",
        call->remote_method().method_name(),
        service_->service_name(),
        call->remote_address().ToString(),
        service_queue_.max_size());
    status = STATUS(ServiceUnavailable, err_msg);
    rpcs_queue_overflow_->Increment();
    call->RespondFailure(ErrorStatusPB::ERROR_SERVER_TOO_BUSY, status);
    DLOG(INFO) << err_msg << " Contents of service queue:\n"
               << service_queue_.ToString();
  } else if (queue_status == QUEUE_SHUTDOWN) {
    status = STATUS(ServiceUnavailable, "Service is shutting down");
    call->RespondFailure(ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN, status);
  } else {
    status = STATUS(RuntimeError, Substitute("Unknown error from BlockingQueue: $0", queue_status));
    call->RespondFailure(ErrorStatusPB::FATAL_UNKNOWN, status);
  }
  return status;
}

void ServicePool::RunThread() {
  while (true) {
    InboundCallPtr incoming;
    if (!service_queue_.BlockingGet(&incoming)) {
      VLOG(1) << "ServicePool: messenger shutting down.";
      return;
    }

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

      // Must release since RespondFailure above ends up taking ownership
      // of the object.
      ignore_result(incoming);
      continue;
    }

    TRACE_TO(incoming->trace(), "Handling call");

    service_->Handle(incoming.get());
  }
}

const string ServicePool::service_name() const {
  return service_->service_name();
}

} // namespace rpc
} // namespace yb
