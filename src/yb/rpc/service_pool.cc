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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <pthread.h>
#include <sys/types.h>

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <boost/asio/strand.hpp>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/inbound_call.h"
#include "yb/rpc/scheduler.h"
#include "yb/rpc/service_if.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/high_water_mark.h"
#include "yb/util/lockfree.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status.h"
#include "yb/util/trace.h"

using namespace std::literals;
using namespace std::placeholders;
using std::string;

DEFINE_RUNTIME_int64(max_time_in_queue_ms, 6000,
    "Fail calls that get stuck in the queue longer than the specified amount of time (in ms)");
TAG_FLAG(max_time_in_queue_ms, advanced);
DEFINE_RUNTIME_int64(backpressure_recovery_period_ms, 600000,
    "Once we hit a backpressure/service-overflow we will consider dropping stale requests "
    "for this duration (in ms)");
TAG_FLAG(backpressure_recovery_period_ms, advanced);
DEFINE_test_flag(bool, enable_backpressure_mode_for_testing, false,
            "For testing purposes. Enables the rpc's to be considered timed out in the queue even "
            "when we have not had any backpressure in the recent past.");

DEFINE_RUNTIME_int32(rpc_queue_high_watermark_pct, 0,
    "Threshold (percentage of queue capacity) at which to start logging diagnostic "
    "warnings for high RPC queue depth. A value of 0 (default) disables the logging.");
TAG_FLAG(rpc_queue_high_watermark_pct, advanced);

DECLARE_bool(TEST_ash_debug_aux);

METRIC_DEFINE_event_stats(server, rpc_incoming_queue_time,
                        "RPC Queue Time",
                        yb::MetricUnit::kMicroseconds,
                        "Number of microseconds incoming RPC requests spend in the worker queue");

METRIC_DEFINE_counter(server, rpcs_timed_out_in_queue,
                      "RPC Queue Timeouts",
                      yb::MetricUnit::kRequests,
                      "Number of RPCs whose timeout elapsed while waiting "
                      "in the service queue, and thus were not processed. "
                      "Does not include calls that were expired before we tried to execute them.");

METRIC_DEFINE_counter(server, rpcs_timed_out_early_in_queue,
                      "RPC Queue Timeouts",
                      yb::MetricUnit::kRequests,
                      "Number of RPCs whose timeout elapsed while waiting "
                      "in the service queue, and thus were not processed. "
                      "Timeout for those calls were detected before the calls tried to execute.");

METRIC_DEFINE_counter(server, rpcs_queue_overflow,
                      "RPC Queue Overflows",
                      yb::MetricUnit::kRequests,
                      "Number of RPCs dropped because the service queue "
                      "was full.");

METRIC_DEFINE_counter(server, rpcs_added_to_queue,
                      "RPCs Added To Service Queue",
                      yb::MetricUnit::kRequests,
                      "Cumulative count of RPCs that were successfully placed onto a service "
                      "queue. Diff against rpcs_started_processing to tell whether the queue is "
                      "growing because of an arrival surge or because of slow draining.");

METRIC_DEFINE_counter(server, rpcs_dequeued,
                      "RPCs Removed From Service Queue",
                      yb::MetricUnit::kRequests,
                      "Cumulative count of RPCs removed from a service queue. Includes both "
                      "RPCs that were picked up for processing and RPCs that were popped off "
                      "the queue because they had already timed out.");

METRIC_DEFINE_counter(server, rpcs_started_processing,
                      "RPCs That Started Service Handler",
                      yb::MetricUnit::kRequests,
                      "Cumulative count of RPCs for which the service handler began executing. "
                      "rpcs_dequeued - rpcs_started_processing approximates the number of calls "
                      "discarded after dequeue (e.g. timed out in the queue).");

METRIC_DEFINE_gauge_int64(server, rpc_queue_max_size,
                           "RPC Queue Max Size",
                           yb::MetricUnit::kRequests,
                           "Maximum number of RPCs that can be queued before the service "
                           "queue is considered full.");

METRIC_DEFINE_gauge_int64(server, rpcs_queue_high_water_mark,
                           "RPC Queue High Water Mark",
                           yb::MetricUnit::kRequests,
                           "Highest number of RPCs concurrently queued in the service queue.");

namespace yb {
namespace rpc {

namespace {

const CoarseDuration kTimeoutCheckGranularity = 100ms;
const char* const kTimedOutInQueue = "Call waited in the queue past deadline";

// Hard cap on the number of distinct method names for which we emit per-method metrics.
// Real services declare ~10-30 methods, so this is effectively unreachable in normal
// operation. The cap exists purely as a safety net against pathological inputs that
// could otherwise grow per_method_counters_ without bound (e.g. an attacker spraying
// garbage method names in RPC headers, or a misconfigured / buggy peer that does the
// same accidentally). When the cap is reached we silently drop per-method metric
// granularity for new names; the aggregate counters keep working.
constexpr size_t kMaxPerMethodMetrics = 1000;

} // namespace

class ServicePoolImpl final : public InboundCallHandler {
 public:
  ServicePoolImpl(
      size_t max_tasks,
      ThreadPoolProvider thread_pool_provider,
      Scheduler* scheduler,
      ServiceIfPtr service,
      const scoped_refptr<MetricEntity>& entity)
      : max_queued_calls_(max_tasks),
        thread_pool_provider_(std::move(thread_pool_provider)),
        scheduler_(*scheduler),
        service_(std::move(service)),
        metric_entity_(entity),
        incoming_queue_time_(METRIC_rpc_incoming_queue_time.Instantiate(entity)),
        rpcs_timed_out_in_queue_(METRIC_rpcs_timed_out_in_queue.Instantiate(entity)),
        rpcs_timed_out_early_in_queue_(
            METRIC_rpcs_timed_out_early_in_queue.Instantiate(entity)),
        rpcs_queue_overflow_(METRIC_rpcs_queue_overflow.Instantiate(entity)),
        rpcs_added_to_queue_(METRIC_rpcs_added_to_queue.Instantiate(entity)),
        rpcs_dequeued_(METRIC_rpcs_dequeued.Instantiate(entity)),
        rpcs_started_processing_(METRIC_rpcs_started_processing.Instantiate(entity)),
        check_timeout_strand_(scheduler->io_service()),
        log_prefix_(Format("$0: ", service_->service_name())) {

          // Create per service counter for rpcs_in_queue_.
          auto id = Format("rpcs_in_queue_$0", service_->metric_name());
          EscapeMetricNameForPrometheus(&id);
          string description = id + " metric for ServicePoolImpl";
          rpcs_in_queue_ = entity->FindOrCreateMetric<AtomicGauge<int64_t>>(
              std::shared_ptr<GaugePrototype<int64_t>>(new OwningGaugePrototype<int64_t>(
                  entity->prototype().name(), std::move(id),
                  description, MetricUnit::kRequests, description, MetricLevel::kInfo)),
              static_cast<int64>(0) /* initial_value */);

          rpc_queue_max_size_ = METRIC_rpc_queue_max_size.Instantiate(entity, max_queued_calls_);
          rpcs_queue_high_water_mark_ = METRIC_rpcs_queue_high_water_mark.InstantiateFunctionGauge(
              entity, Bind(&ServicePoolImpl::GetQueueHighWaterMark, Unretained(this)));

          LOG_WITH_PREFIX(INFO) << "yb::rpc::ServicePoolImpl created at " << this;
  }

  ~ServicePoolImpl() {
    if (rpcs_queue_high_water_mark_) {
      rpcs_queue_high_water_mark_->DetachToCurrentValue();
    }
    StartShutdown();
    CompleteShutdown();
  }

  void CompleteShutdown() {
    shutdown_complete_latch_.Wait();
    while (scheduled_tasks_.load(std::memory_order_acquire) != 0) {
      std::this_thread::sleep_for(10ms);
    }
    LOG_IF(DFATAL, !pre_check_timeout_queue_.Empty())
        << "Entries pushed to pre_check_timeout_queue_ after shutdown";
  }

  int64_t GetQueueHighWaterMark() const {
    return queued_calls_.max_value();
  }

  void StartShutdown() {
    bool closing_state = false;
    if (closing_.compare_exchange_strong(closing_state, true)) {
      service_->Shutdown();

      auto check_timeout_task = check_timeout_task_.load(std::memory_order_acquire);
      if (check_timeout_task != kUninitializedScheduledTaskId) {
        scheduler_.Abort(check_timeout_task);
      }

      check_timeout_strand_.dispatch([this] {
        pre_check_timeout_queue_.Drain();
        shutdown_complete_latch_.CountDown();
      });
    }
  }

  void Process(InboundCallPtr call, Queue queue) {
    LOG_IF(DFATAL, closing_.load(std::memory_order_relaxed))
        << "Calling Process on closed service pool";
    auto thread_pool = thread_pool_provider_(call->pool_tag());
    if (!queue && thread_pool->OwnsThisThread()) {
      Handle(std::move(call));
      return;
    }

    TRACE_TO(call->trace(), "Inserting onto call queue");
    SET_WAIT_STATUS_TO(call->wait_state(), OnCpu_Passive);

    auto task = call->BindTask(this);
    if (!task) {
      Overflow(call, "service", queued_calls_.load(std::memory_order_relaxed));
      return;
    }

    // Note: rpcs_in_queue_ is already incremented inside CallQueued() (called via
    // BindTask above) before we get here. As a result, a Prometheus scrape that races
    // with us can momentarily observe rpcs_in_queue_ == rpcs_added_to_queue_ -
    // rpcs_dequeued_ + 1. Reordering would require pulling rpcs_in_queue_->Increment()
    // out of CallQueued(), which complicates the rollback-on-overflow path; the brief
    // skew is acceptable for monitoring purposes.
    rpcs_added_to_queue_->Increment();
    if (auto* per_method_counters = GetPerMethodCounters(call->method_name())) {
      per_method_counters->added->Increment();
    }

    auto call_deadline = call->GetClientDeadline();
    if (call_deadline != CoarseTimePoint::max()) {
      pre_check_timeout_queue_.Push(new WeakInboundCall(std::move(call)));
      ScheduleCheckTimeout(call_deadline);
    }

    thread_pool->Enqueue(task);
  }

  const Counter* RpcsTimedOutInQueueMetricForTests() const {
    return rpcs_timed_out_early_in_queue_.get();
  }

  const Counter* RpcsQueueOverflowMetric() const {
    return rpcs_queue_overflow_.get();
  }

  std::string service_name() const {
    return service_->service_name();
  }

  ServiceIfPtr TEST_get_service() const {
    return service_;
  }

  void Overflow(const InboundCallPtr& call, const char* type, size_t limit) {
    const auto err_msg =
        Format("$0 request on $1 from $2 dropped due to backpressure. "
                   "The $3 queue is full, it has $4 items.",
            call->method_name().ToBuffer(),
            service_->service_name(),
            call->remote_address(),
            type,
            limit);
    YB_LOG_EVERY_N_SECS(WARNING, 3) << LogPrefix() << err_msg;
    const auto response_status = STATUS(ServiceUnavailable, err_msg);
    rpcs_queue_overflow_->Increment();
    if (auto* per_method_counters = GetPerMethodCounters(call->method_name())) {
      per_method_counters->overflow->Increment();
    }
    call->RespondFailure(ErrorStatusPB::ERROR_SERVER_TOO_BUSY, response_status);
    last_backpressure_at_.store(
        CoarseMonoClock::Now().time_since_epoch(), std::memory_order_release);
  }

  void Failure(const InboundCallPtr& call, const Status& status) override {
    if (!call->TryStartProcessing()) {
      return;
    }

    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << LogPrefix()
        << call->method_name() << " request on " << service_->service_name() << " from "
        << call->remote_address() << " dropped because of: " << status.ToString();
    const auto response_status = STATUS(ServiceUnavailable, "Service is shutting down");
    call->RespondFailure(ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN, response_status);
  }

  void FillEndpoints(const RpcServicePtr& service, RpcEndpointMap* map) {
    service_->FillEndpoints(service, map);
  }

  void Handle(InboundCallPtr incoming) override {
    incoming->RecordHandlingStarted(incoming_queue_time_);
    ADOPT_TRACE(incoming->trace());
    if (FLAGS_TEST_ash_debug_aux && incoming->wait_state()) {
      incoming->wait_state()->UpdateAuxInfo(
          ash::AshAuxInfo{.method = incoming->method_name().ToBuffer()});
    }
    ADOPT_WAIT_STATE(incoming->wait_state());
    SCOPED_WAIT_STATUS(OnCpu_Active);

    const char* error_message;
    if (PREDICT_FALSE(incoming->ClientTimedOut())) {
      error_message = kTimedOutInQueue;
    } else if (PREDICT_FALSE(ShouldDropRequestDuringHighLoad(incoming))) {
      error_message = "The server is overloaded. Call waited in the queue past max_time_in_queue.";
    } else {
      if (incoming->TryStartProcessing()) {
        rpcs_started_processing_->Increment();
        TRACE_TO(incoming->trace(), "Handling call $0", AsString(incoming->method_name()));
        service_->Handle(std::move(incoming));
      }
      return;
    }

    TRACE_TO(incoming->trace(), error_message);
    VLOG_WITH_PREFIX(4)
        << "Timing out call " << incoming->ToString() << " due to: " << error_message;

    // Respond as a failure, even though the client will probably ignore
    // the response anyway.
    TimedOut(incoming.get(), error_message, rpcs_timed_out_in_queue_.get());
  }

 private:
  void TimedOut(InboundCall* call, const char* error_message, Counter* metric) {
    if (call->RespondTimedOutIfPending(error_message)) {
      metric->Increment();
    }
  }

  bool ShouldDropRequestDuringHighLoad(const InboundCallPtr& incoming) {
    CoarseTimePoint last_backpressure_at(last_backpressure_at_.load(std::memory_order_acquire));

    // For testing purposes.
    if (FLAGS_TEST_enable_backpressure_mode_for_testing) {
      last_backpressure_at = CoarseMonoClock::Now();
    }

    // Test for a sentinel value, to avoid reading the clock.
    if (last_backpressure_at == CoarseTimePoint()) {
      return false;
    }

    auto now = CoarseMonoClock::Now();
    if (now > last_backpressure_at + FLAGS_backpressure_recovery_period_ms * 1ms) {
      last_backpressure_at_.store(CoarseTimePoint().time_since_epoch(), std::memory_order_release);
      return false;
    }

    return incoming->GetTimeInQueue().ToMilliseconds() > FLAGS_max_time_in_queue_ms;
  }

  void CheckTimeout(ScheduledTaskId task_id, CoarseTimePoint time, const Status& status) {
    auto se = ScopeExit([this, task_id, time] {
      auto expected_duration = time.time_since_epoch();
      next_check_timeout_.compare_exchange_strong(
          expected_duration, CoarseTimePoint::max().time_since_epoch(),
          std::memory_order_acq_rel);
      auto expected_task_id = task_id;
      check_timeout_task_.compare_exchange_strong(
          expected_task_id, kUninitializedScheduledTaskId, std::memory_order_acq_rel);
      scheduled_tasks_.fetch_sub(1, std::memory_order_acq_rel);
    });
    if (!status.ok()) {
      return;
    }

    auto now = CoarseMonoClock::now();
    {
      while (auto raw_call = pre_check_timeout_queue_.Pop()) {
        std::unique_ptr<WeakInboundCall> call(raw_call);
        auto inbound_call = call->call.lock();
        if (!inbound_call) {
          continue;
        }
        if (now > inbound_call->GetClientDeadline()) {
          TimedOut(inbound_call.get(), kTimedOutInQueue, rpcs_timed_out_early_in_queue_.get());
        } else {
          check_timeout_queue_.emplace(inbound_call);
        }
      }
    }

    while (!check_timeout_queue_.empty() && now > check_timeout_queue_.top().time) {
      auto call = check_timeout_queue_.top().call.lock();
      if (call) {
        TimedOut(call.get(), kTimedOutInQueue, rpcs_timed_out_early_in_queue_.get());
      }
      check_timeout_queue_.pop();
    }

    if (!check_timeout_queue_.empty()) {
      ScheduleCheckTimeout(check_timeout_queue_.top().time);
    }
  }

  void ScheduleCheckTimeout(CoarseTimePoint time) {
    if (closing_.load(std::memory_order_acquire)) {
      return;
    }
    CoarseDuration next_check_timeout = next_check_timeout_.load(std::memory_order_acquire);
    time += kTimeoutCheckGranularity;
    while (CoarseTimePoint(next_check_timeout) > time) {
      if (next_check_timeout_.compare_exchange_weak(
              next_check_timeout, time.time_since_epoch(), std::memory_order_acq_rel)) {
        check_timeout_strand_.dispatch([this, time] {
          auto check_timeout_task = check_timeout_task_.load(std::memory_order_acquire);
          if (check_timeout_task != kUninitializedScheduledTaskId) {
            scheduler_.Abort(check_timeout_task);
          }
          scheduled_tasks_.fetch_add(1, std::memory_order_acq_rel);
          auto task_id = scheduler_.Schedule(
              [this, time](ScheduledTaskId task_id, const Status& status) {
                check_timeout_strand_.dispatch([this, time, task_id, status] {
                  CheckTimeout(task_id, time, status);
                });
              },
              ToSteady(time));
          check_timeout_task_.store(task_id, std::memory_order_release);
        });
        break;
      }
    }
  }

  const std::string& LogPrefix() const { return log_prefix_; }

  std::string DumpTopKMethods() {
    std::vector<std::pair<std::string, int64_t>> snapshot;
    {
      SharedLock<std::shared_mutex> lock(per_method_mutex_);
      snapshot.reserve(per_method_counters_.size());
      for (const auto& [method, counters] : per_method_counters_) {
        snapshot.emplace_back(method, counters.added->value());
      }
    }
    std::sort(snapshot.begin(), snapshot.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string result = "Top methods by added RPCs: ";
    int k = 0;
    for (const auto& [method, count] : snapshot) {
      if (k++ >= 5) break;
      result += Format("$0=$1, ", method, count);
    }
    return result;
  }

  std::optional<int64_t> CallQueued(int64_t rpc_queue_limit) override {
    auto queued_calls = queued_calls_.fetch_add(1, std::memory_order_acq_rel);
    if (queued_calls < 0) {
      YB_LOG_EVERY_N_SECS(DFATAL, 5) << "Negative number of queued calls: " << queued_calls;
    }

    size_t max_queued_calls = std::min(max_queued_calls_, implicit_cast<size_t>(rpc_queue_limit));
    if (implicit_cast<size_t>(queued_calls) >= max_queued_calls) {
      queued_calls_.fetch_sub(1, std::memory_order_relaxed);
      return std::nullopt;
    }

    int pct = FLAGS_rpc_queue_high_watermark_pct;
    if (pct > 0 && pct <= 100) {
      size_t threshold = (max_queued_calls_ * pct) / 100;
      if (implicit_cast<size_t>(queued_calls) >= threshold) {
        YB_LOG_EVERY_N_SECS(WARNING, 5) << LogPrefix()
            << "Queue depth " << queued_calls << " exceeds threshold of "
            << threshold << " (" << pct << "% of " << max_queued_calls_ << "). "
            << DumpTopKMethods();
      }
    }

    rpcs_in_queue_->Increment();
    return queued_calls;
  }

  void CallDequeued() override {
    queued_calls_.fetch_sub(1, std::memory_order_relaxed);
    rpcs_in_queue_->Decrement();
    rpcs_dequeued_->Increment();
  }

  // Per-RPC-method counters. Lazily registered on first occurrence of each method name.
  // In normal operation cardinality is the number of methods declared on the service
  // (typically ~10-30); kMaxPerMethodMetrics is the absolute upper bound enforced as a
  // safety net (see comment on the constant).
  struct PerMethodCounters {
    scoped_refptr<Counter> added;
    scoped_refptr<Counter> overflow;
  };

  // The hot (steady-state) lookup path does a heterogeneous std::string_view lookup
  // against per_method_counters_ under a SharedLock, so it does not allocate. We only
  // materialize a std::string when we are about to insert a new entry (i.e. the first
  // time we see a given method name on this service pool). See TransparentStringMap
  // alias below.
  //
  // TODO(#31673): once Abseil containers are available we should migrate
  // per_method_counters_ to absl::flat_hash_map; the transparent-hash lookup story is
  // already solved, so #31673 is now a cache-locality / open-addressing improvement
  // rather than a fix for any per-call allocation.
  PerMethodCounters* GetPerMethodCounters(Slice method_name) EXCLUDES(per_method_mutex_) {
    const std::string_view method_sv = method_name.AsStringView();
    {
      SharedLock<std::shared_mutex> lock(per_method_mutex_);
      auto it = per_method_counters_.find(method_sv);
      if (PREDICT_TRUE(it != per_method_counters_.end())) {
        return &it->second;
      }
    }
    std::lock_guard lock(per_method_mutex_);
    // Double-check after upgrading to the exclusive lock; someone else may have inserted
    // this method's entry between the shared-lock release and the exclusive-lock acquire.
    auto it = per_method_counters_.find(method_sv);
    if (it != per_method_counters_.end()) {
      return &it->second;
    }
    if (per_method_counters_.size() >= kMaxPerMethodMetrics) {
      // Log at most once every 60s so a runaway / hostile peer can't drown the log,
      // but visibility of "we are no longer creating per-method metrics" is preserved.
      YB_LOG_EVERY_N_SECS(WARNING, 60)
          << LogPrefix()
          << "Reached the per-service-pool cap of " << kMaxPerMethodMetrics
          << " distinct RPC method names; new method '" << method_sv
          << "' will not get its own per-method metrics. Aggregate counters are unaffected.";
      return nullptr;
    }
    // Miss path: allocate the std::string only now, for use as the map key.
    auto [new_it, inserted] = per_method_counters_.try_emplace(std::string(method_sv));
    if (inserted) {
      new_it->second = MakePerMethodCounters(new_it->first);
    }
    return &new_it->second;
  }

  PerMethodCounters MakePerMethodCounters(const std::string& method) REQUIRES(per_method_mutex_) {
    return {
        .added = MakeCounter("rpcs_added_to_queue", method,
                             "RPCs added to the service queue, by method"),
        .overflow = MakeCounter("rpcs_queue_overflow", method,
                                "RPCs dropped because the service queue was full, by method"),
    };
  }

  // MakeCounter assumes that this ServicePoolImpl is the sole owner of `metric_entity_`
  // for the synthesized metric name. MetricEntity::FindOrCreateMetric keys its dedup map
  // by prototype pointer (not by name), and we allocate a fresh OwningCounterPrototype on
  // every call, so two ServicePoolImpl instances sharing the same entity would register
  // two distinct Counter objects under the same Prometheus name. In current YB usage each
  // ServicePoolImpl is constructed with its own dedicated MetricEntity, so this is safe;
  // revisit if that invariant ever changes.
  scoped_refptr<Counter> MakeCounter(
      const std::string& base_name, const std::string& method, const std::string& description) {
    auto id = Format("$0_$1_$2", base_name, service_->metric_name(), method);
    EscapeMetricNameForPrometheus(&id);
    auto label = Format("$0 ($1::$2)", description, service_->service_name(), method);
    return metric_entity_->FindOrCreateMetric<Counter>(
        std::shared_ptr<CounterPrototype>(new OwningCounterPrototype(
            metric_entity_->prototype().name(), id, label, MetricUnit::kRequests, description,
            MetricLevel::kInfo)));
  }

  const size_t max_queued_calls_;
  ThreadPoolProvider thread_pool_provider_;
  Scheduler& scheduler_;
  ServiceIfPtr service_;
  scoped_refptr<MetricEntity> metric_entity_;
  scoped_refptr<EventStats> incoming_queue_time_;
  scoped_refptr<Counter> rpcs_timed_out_in_queue_;
  scoped_refptr<Counter> rpcs_timed_out_early_in_queue_;
  scoped_refptr<Counter> rpcs_queue_overflow_;
  scoped_refptr<Counter> rpcs_added_to_queue_;
  scoped_refptr<Counter> rpcs_dequeued_;
  scoped_refptr<Counter> rpcs_started_processing_;
  scoped_refptr<AtomicGauge<int64_t>> rpcs_in_queue_;
  scoped_refptr<AtomicGauge<int64_t>> rpc_queue_max_size_;
  scoped_refptr<FunctionGauge<int64_t>> rpcs_queue_high_water_mark_;
  std::shared_mutex per_method_mutex_;
  // UnorderedStringMap pairs StringHash (with is_transparent) and std::equal_to<void>
  // so that the steady-state .find() call can accept a std::string_view without
  // synthesizing a temporary std::string. See GetPerMethodCounters().
  UnorderedStringMap<std::string, PerMethodCounters> per_method_counters_
      GUARDED_BY(per_method_mutex_);
  // Have to use CoarseDuration here, since CoarseTimePoint does not work with clang + libstdc++
  std::atomic<CoarseDuration> last_backpressure_at_{CoarseTimePoint().time_since_epoch()};
  HighWaterMark queued_calls_{0};

  // It is too expensive to update timeout priority queue when each call is received.
  // So we are doing the following trick.
  // All calls are added to pre_check_timeout_queue_, w/o priority.
  // Then before timeout check we move calls from this queue to priority queue.
  struct WeakInboundCall : public MPSCQueueEntry<WeakInboundCall> {
    explicit WeakInboundCall(std::weak_ptr<InboundCall>&& call_) : call(std::move(call_)) {
    }

    std::weak_ptr<InboundCall> call;
  };
  MPSCQueue<WeakInboundCall> pre_check_timeout_queue_;

  // Used to track scheduled time, to avoid unnecessary rescheduling.
  std::atomic<CoarseDuration> next_check_timeout_{CoarseTimePoint::max().time_since_epoch()};

  // Last scheduled task, required to abort scheduled task during reschedule.
  std::atomic<ScheduledTaskId> check_timeout_task_{kUninitializedScheduledTaskId};

  std::atomic<int> scheduled_tasks_{0};

  // Timeout checking synchronization.
  IoService::strand check_timeout_strand_;

  struct QueuedCheckDeadline {
    CoarseTimePoint time;
    // We use weak pointer to avoid retaining call that was already processed.
    std::weak_ptr<InboundCall> call;

    explicit QueuedCheckDeadline(const InboundCallPtr& inp)
        : time(inp->GetClientDeadline()), call(inp) {
    }
  };

  // Priority queue puts the geatest value on top, so we invert comparison.
  friend bool operator<(const QueuedCheckDeadline& lhs, const QueuedCheckDeadline& rhs) {
    return lhs.time > rhs.time;
  }

  std::priority_queue<QueuedCheckDeadline> check_timeout_queue_;

  std::atomic<bool> closing_ = {false};
  CountDownLatch shutdown_complete_latch_{1};
  std::string log_prefix_;
};

ServicePool::ServicePool(
    size_t max_tasks,
    ThreadPoolProvider thread_pool_provider,
    Scheduler* scheduler,
    ServiceIfPtr service,
    const scoped_refptr<MetricEntity>& metric_entity)
    : impl_(new ServicePoolImpl(
        max_tasks, std::move(thread_pool_provider), scheduler, std::move(service), metric_entity)) {
}

ServicePool::~ServicePool() {
}

void ServicePool::StartShutdown() {
  impl_->StartShutdown();
}

void ServicePool::CompleteShutdown() {
  impl_->CompleteShutdown();
}

void ServicePool::Process(InboundCallPtr call, Queue queue) {
  impl_->Process(std::move(call), queue);
}

void ServicePool::FillEndpoints(RpcEndpointMap* map) {
  impl_->FillEndpoints(RpcServicePtr(this), map);
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

ServiceIfPtr ServicePool::TEST_get_service() const {
  return impl_->TEST_get_service();
}

} // namespace rpc
} // namespace yb
