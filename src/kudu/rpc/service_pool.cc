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

#include "kudu/rpc/service_pool.h"

#include <glog/logging.h>
#include <memory>
#include <string>
#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/rpc/inbound_call.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/service_if.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/metrics.h"
#include "kudu/util/status.h"
#include "kudu/util/thread.h"
#include "kudu/util/trace.h"

using std::shared_ptr;
using strings::Substitute;

METRIC_DEFINE_histogram(server, rpc_incoming_queue_time,
                        "RPC Queue Time",
                        kudu::MetricUnit::kMicroseconds,
                        "Number of microseconds incoming RPC requests spend in the worker queue",
                        60000000LU, 3);

METRIC_DEFINE_counter(server, rpcs_timed_out_in_queue,
                      "RPC Queue Timeouts",
                      kudu::MetricUnit::kRequests,
                      "Number of RPCs whose timeout elapsed while waiting "
                      "in the service queue, and thus were not processed.");

METRIC_DEFINE_counter(server, rpcs_queue_overflow,
                      "RPC Queue Overflows",
                      kudu::MetricUnit::kRequests,
                      "Number of RPCs dropped because the service queue "
                      "was full.");

namespace kudu {
namespace rpc {

ServicePool::ServicePool(gscoped_ptr<ServiceIf> service,
                         const scoped_refptr<MetricEntity>& entity,
                         size_t service_queue_length)
  : service_(service.Pass()),
    service_queue_(service_queue_length),
    incoming_queue_time_(METRIC_rpc_incoming_queue_time.Instantiate(entity)),
    rpcs_timed_out_in_queue_(METRIC_rpcs_timed_out_in_queue.Instantiate(entity)),
    rpcs_queue_overflow_(METRIC_rpcs_queue_overflow.Instantiate(entity)),
    closing_(false) {
}

ServicePool::~ServicePool() {
  Shutdown();
}

Status ServicePool::Init(int num_threads) {
  for (int i = 0; i < num_threads; i++) {
    scoped_refptr<kudu::Thread> new_thread;
    CHECK_OK(kudu::Thread::Create("service pool", "rpc worker",
        &ServicePool::RunThread, this, &new_thread));
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
  for (scoped_refptr<kudu::Thread>& thread : threads_) {
    CHECK_OK(ThreadJoiner(thread.get()).Join());
  }

  // Now we must drain the service queue.
  Status status = Status::ServiceUnavailable("Service is shutting down");
  gscoped_ptr<InboundCall> incoming;
  while (service_queue_.BlockingGet(&incoming)) {
    incoming.release()->RespondFailure(ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN, status);
  }

  service_->Shutdown();
}

Status ServicePool::QueueInboundCall(gscoped_ptr<InboundCall> call) {
  InboundCall* c = call.release();

  TRACE_TO(c->trace(), "Inserting onto call queue");
  // Queue message on service queue
  QueueStatus queue_status = service_queue_.Put(c);
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
        c->remote_method().method_name(),
        service_->service_name(),
        c->remote_address().ToString(),
        service_queue_.max_size());
    status = Status::ServiceUnavailable(err_msg);
    rpcs_queue_overflow_->Increment();
    c->RespondFailure(ErrorStatusPB::ERROR_SERVER_TOO_BUSY, status);
    DLOG(INFO) << err_msg << " Contents of service queue:\n"
               << service_queue_.ToString();
  } else if (queue_status == QUEUE_SHUTDOWN) {
    status = Status::ServiceUnavailable("Service is shutting down");
    c->RespondFailure(ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN, status);
  } else {
    status = Status::RuntimeError(Substitute("Unknown error from BlockingQueue: $0", queue_status));
    c->RespondFailure(ErrorStatusPB::FATAL_UNKNOWN, status);
  }
  return status;
}

void ServicePool::RunThread() {
  while (true) {
    gscoped_ptr<InboundCall> incoming;
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
        Status::TimedOut("Call waited in the queue past client deadline"));

      // Must release since RespondFailure above ends up taking ownership
      // of the object.
      ignore_result(incoming.release());
      continue;
    }

    TRACE_TO(incoming->trace(), "Handling call");

    // Release the InboundCall pointer -- when the call is responded to,
    // it will get deleted at that point.
    service_->Handle(incoming.release());
  }
}

const string ServicePool::service_name() const {
  return service_->service_name();
}

} // namespace rpc
} // namespace kudu
