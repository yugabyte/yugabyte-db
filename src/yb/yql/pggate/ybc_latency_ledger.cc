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

#include "yb/yql/pggate/ybc_latency_ledger.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>

#include "yb/ash/latency_ledger.h"

#include "yb/common/common.pb.h"

#include "yb/util/locks.h"

#include "yb/yql/pggate/pg_latency_ledger.h"

namespace yb::pggate {

namespace {

YbcLedgerRing* ledger_ring = nullptr;

// Time this thread has spent on a CPU and waiting in a run queue, from the kernel's schedstat.
struct SchedStat {
  int64_t cpu_ns = 0;
  int64_t runqueue_ns = 0;
};

SchedStat ReadSchedStat() {
  // -1: not opened yet, -2: unavailable.
  static thread_local int fd = -1;
  if (fd == -1) {
    fd = open("/proc/thread-self/schedstat", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      fd = -2;
    }
  }
  if (fd < 0) {
    return {};
  }
  char buf[128];
  const auto n = pread(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0) {
    return {};
  }
  buf[n] = 0;
  unsigned long long cpu = 0, runqueue = 0;  // NOLINT(runtime/int)
  if (sscanf(buf, "%llu %llu", &cpu, &runqueue) != 2) {
    return {};
  }
  return SchedStat{.cpu_ns = static_cast<int64_t>(cpu),
                   .runqueue_ns = static_cast<int64_t>(runqueue)};
}

uint32_t ToRecordDuration(int64_t nanos) {
  const auto units = std::max<int64_t>(nanos, 0) >> YB_LEDGER_DURATION_SHIFT;
  return static_cast<uint32_t>(
      std::min<int64_t>(units, std::numeric_limits<uint32_t>::max()));
}

// Accounts the round trip in progress of this backend.
class PgLatencyLedger {
 public:
  void Start() {
    std::lock_guard lock(mutex_);
    active_.store(true, std::memory_order_relaxed);
    phase_stack_size_ = 0;
    start_nanos_ = interval_start_ = ash::LedgerNowNanos();
    start_time_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    phase_ = YB_LEDGER_PHASE_OTHER;
    wait_event_ = 0;
    entries_.clear();
    start_sched_ = ReadSchedStat();
    ash::SetPgLatencyLedgerRequested(true);
  }

  bool active() const {
    return active_.load(std::memory_order_relaxed);
  }

  uint8_t SetPhase(uint8_t phase) {
    std::lock_guard lock(mutex_);
    return DoSetPhase(phase);
  }

  void PushPhase(const char* span_name) {
    std::lock_guard lock(mutex_);
    const auto phase = SpanPhase(span_name);
    const auto previous = phase ? DoSetPhase(*phase) : phase_;
    if (phase_stack_size_ < kMaxPhaseStack) {
      phase_stack_[phase_stack_size_] = previous;
    }
    ++phase_stack_size_;
  }

  void PopPhase() {
    std::lock_guard lock(mutex_);
    if (phase_stack_size_ == 0) {
      return;
    }
    --phase_stack_size_;
    if (phase_stack_size_ < kMaxPhaseStack) {
      DoSetPhase(phase_stack_[phase_stack_size_]);
    }
  }

  void WaitEvent(uint32_t wait_event) {
    std::lock_guard lock(mutex_);
    if (active() && wait_event != wait_event_) {
      CloseInterval(ash::LedgerNowNanos());
    }
    wait_event_ = wait_event;
  }

  void AddRequest(
      int64_t sent_nanos, int64_t received_nanos, const LatencyLedgerPB& ledger,
      bool shared_memory) {
    std::lock_guard lock(mutex_);
    if (!active()) {
      return;
    }
    ash::AppendLedgerEntries(ledger, 1, phase_, &entries_);
    // The tserver stamps the response on the same host and clock, so the handoff back to this
    // backend is exact. Otherwise charge whatever its ledger does not cover.
    const auto finish = ledger.finish_mono_nanos();
    if (shared_memory && finish >= sent_nanos && finish <= received_nanos) {
      ash::AddLedgerEntry(
          &entries_, ash::MakeLedgerKey(1, phase_, ash::LedgerCode::kShmemResponse),
          received_nanos - finish);
    } else {
      ash::AddLedgerEntry(
          &entries_, ash::MakeLedgerKey(1, phase_, ash::LedgerCode::kTransport),
          std::max<int64_t>(received_nanos - sent_nanos - ledger.total_nanos(), 0));
    }
  }

  void End(const YbcLedgerRoundTripInfo& info, bool record, int64_t min_duration_ns) {
    std::lock_guard lock(mutex_);
    if (!active()) {
      return;
    }
    const auto now = ash::LedgerNowNanos();
    CloseInterval(now);
    active_.store(false, std::memory_order_relaxed);
    ash::SetPgLatencyLedgerRequested(false);
    const auto duration = now - start_nanos_;
    if (!ledger_ring ||
        !(record || (min_duration_ns >= 0 && duration >= min_duration_ns))) {
      return;
    }
    const auto end_sched = ReadSchedStat();
    Write(info, duration,
          SchedStat{.cpu_ns = end_sched.cpu_ns - start_sched_.cpu_ns,
                    .runqueue_ns = end_sched.runqueue_ns - start_sched_.runqueue_ns});
  }

  void Cancel() {
    std::lock_guard lock(mutex_);
    active_.store(false, std::memory_order_relaxed);
    ash::SetPgLatencyLedgerRequested(false);
  }

 private:
  static constexpr size_t kMaxPhaseStack = 32;

  // The phase of a distributed-tracing span, if it names one. See the YB_DIST_TRACE_START_SPAN
  // call sites in the YSQL backend.
  static std::optional<uint8_t> SpanPhase(const char* span_name) {
    std::string_view name(span_name);
    if (name.starts_with("ext.")) {
      name.remove_prefix(4);
    }
    if (name == "parse" || name == "rewrite") {
      return YB_LEDGER_PHASE_PARSE;
    }
    if (name == "plan") {
      return YB_LEDGER_PHASE_PLAN;
    }
    if (name == "bind") {
      return YB_LEDGER_PHASE_BIND;
    }
    if (name == "execute") {
      return YB_LEDGER_PHASE_EXECUTE;
    }
    if (name == "commit") {
      return YB_LEDGER_PHASE_COMMIT;
    }
    if (name == "abort") {
      return YB_LEDGER_PHASE_ABORT;
    }
    if (name == "describe") {
      return YB_LEDGER_PHASE_DESCRIBE;
    }
    return std::nullopt;
  }

  uint8_t DoSetPhase(uint8_t phase) REQUIRES(mutex_) {
    const auto previous = phase_;
    if (active_.load(std::memory_order_relaxed) && phase != phase_) {
      CloseInterval(ash::LedgerNowNanos());
    }
    phase_ = phase;
    return previous;
  }

  void CloseInterval(int64_t now) REQUIRES(mutex_) {
    if (now > interval_start_) {
      ash::AddLedgerEntry(
          &entries_, ash::MakeLedgerKey(0, phase_, wait_event_), now - interval_start_);
      interval_start_ = now;
    }
  }

  void Write(const YbcLedgerRoundTripInfo& info, int64_t duration, const SchedStat& sched)
      REQUIRES(mutex_) {
    auto flags = info.flags;
    if (ash::FoldLedgerOverflow(&entries_, YB_LEDGER_MAX_ENTRIES)) {
      flags |= YB_LEDGER_FLAG_OVERFLOW;
    }

    auto* ring = ledger_ring;
    const auto seq = __atomic_add_fetch(&ring->next_seq, 1, __ATOMIC_ACQ_REL);
    auto& slot = ring->slots[(seq - 1) % ring->num_slots];
    // Readers check that seq is the same before and after they copy a slot.
    __atomic_store_n(&slot.seq, 0, __ATOMIC_RELAXED);
    std::atomic_thread_fence(std::memory_order_release);
    slot.start_time_us = start_time_us_;
    slot.duration_ns = duration;
    slot.query_id = info.query_id;
    slot.pid = info.pid;
    slot.txn_seq = info.txn_seq;
    slot.db_oid = info.db_oid;
    slot.flags = flags;
    slot.cpu_ns = sched.cpu_ns;
    slot.runqueue_ns = sched.runqueue_ns;
    uint16_t num_entries = 0;
    for (const auto& entry : entries_) {
      slot.entries[num_entries++] = YbcLedgerEntry{
          .key = entry.key, .duration = ToRecordDuration(entry.nanos)};
    }
    slot.num_entries = num_entries;
    __atomic_store_n(&slot.seq, seq, __ATOMIC_RELEASE);
  }

  simple_spinlock mutex_;
  // Written under mutex_, read without it by YBCLedgerIsActive.
  std::atomic<bool> active_{false};
  uint8_t phase_stack_[kMaxPhaseStack] GUARDED_BY(mutex_);
  size_t phase_stack_size_ GUARDED_BY(mutex_) = 0;
  int64_t start_nanos_ GUARDED_BY(mutex_) = 0;
  int64_t start_time_us_ GUARDED_BY(mutex_) = 0;
  int64_t interval_start_ GUARDED_BY(mutex_) = 0;
  SchedStat start_sched_ GUARDED_BY(mutex_);
  uint8_t phase_ GUARDED_BY(mutex_) = YB_LEDGER_PHASE_OTHER;
  uint32_t wait_event_ GUARDED_BY(mutex_) = 0;
  ash::LedgerEntries entries_ GUARDED_BY(mutex_);
};

PgLatencyLedger& Ledger() {
  static PgLatencyLedger ledger;
  return ledger;
}

} // namespace

void PgLedgerAddRequest(
    int64_t sent_nanos, int64_t received_nanos, const LatencyLedgerPB& ledger,
    bool shared_memory) {
  Ledger().AddRequest(sent_nanos, received_nanos, ledger, shared_memory);
}

extern "C" {

size_t YBCLedgerRingSize(int num_slots) {
  return offsetof(YbcLedgerRing, slots) +
         static_cast<size_t>(std::max(num_slots, 0)) * sizeof(YbcLedgerRecord);
}

void YBCLedgerSetRing(YbcLedgerRing* ring) {
  ledger_ring = ring;
}

void YBCLedgerStartRoundTrip() {
  Ledger().Start();
}

bool YBCLedgerIsActive() {
  return Ledger().active();
}

uint8_t YBCLedgerSetPhase(uint8_t phase) {
  return Ledger().SetPhase(phase);
}

void YBCLedgerPushPhase(const char* span_name) {
  Ledger().PushPhase(span_name);
}

void YBCLedgerPopPhase() {
  Ledger().PopPhase();
}

void YBCLedgerWaitEvent(uint32_t wait_event_info) {
  Ledger().WaitEvent(wait_event_info);
}

void YBCLedgerEndRoundTrip(
    const YbcLedgerRoundTripInfo* info, bool record, int64_t min_duration_ns) {
  Ledger().End(*info, record, min_duration_ns);
}

void YBCLedgerCancelRoundTrip() {
  Ledger().Cancel();
}

} // extern "C"

} // namespace yb::pggate
