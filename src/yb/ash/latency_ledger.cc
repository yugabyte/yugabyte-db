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

#include "yb/ash/latency_ledger.h"

#include <time.h>

#include <algorithm>
#include <atomic>
#include <utility>

#include "yb/ash/wait_state.h"

#include "yb/common/common.pb.h"

#include "yb/util/format.h"

namespace yb::ash {

namespace {

// A child ledger carries the whole subtree of the RPCs below it. Bound what a parent keeps so a
// deep or wide tree cannot grow a ledger without limit; the rest is charged to kOverflow.
constexpr size_t kMaxChildEntries = 64;

std::atomic<bool> pg_latency_ledger_requested{false};

} // namespace

int64_t LedgerNowNanos() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void AddLedgerEntry(LedgerEntries* entries, uint32_t key, int64_t nanos) {
  for (auto& entry : *entries) {
    if (entry.key == key) {
      entry.nanos += nanos;
      return;
    }
  }
  entries->push_back(LedgerEntry{.key = key, .nanos = nanos});
}

bool FoldLedgerOverflow(LedgerEntries* entries, size_t max_entries) {
  if (entries->size() <= max_entries) {
    return false;
  }
  std::sort(entries->begin(), entries->end(), [](const auto& lhs, const auto& rhs) {
    return lhs.nanos > rhs.nanos;
  });
  // Depth and phase bits of a key.
  constexpr uint32_t kGroupMask = 0xFF000000U;
  size_t keep = max_entries;
  for (;;) {
    boost::container::small_vector<uint32_t, 16> groups;
    for (size_t i = keep; i != entries->size(); ++i) {
      const auto group = (*entries)[i].key & kGroupMask;
      if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
        groups.push_back(group);
      }
    }
    if (keep == 0 || keep + groups.size() <= max_entries) {
      break;
    }
    --keep;
  }
  LedgerEntries overflow;
  for (size_t i = keep; i != entries->size(); ++i) {
    const auto& entry = (*entries)[i];
    AddLedgerEntry(
        &overflow,
        (entry.key & kGroupMask) |
            CompressLedgerCode(std::to_underlying(LedgerCode::kOverflow)),
        entry.nanos);
  }
  entries->resize(keep);
  entries->insert(entries->end(), overflow.begin(), overflow.end());
  if (entries->size() > max_entries) {
    entries->resize(max_entries);
  }
  return true;
}

void AppendLedgerEntries(
    const LatencyLedgerPB& pb, uint32_t depth_offset, uint32_t phase, LedgerEntries* out) {
  const auto size = std::min(pb.keys_size(), pb.nanos_size());
  for (int i = 0; i != size; ++i) {
    AddLedgerEntry(out, ShiftLedgerKey(pb.keys(i), depth_offset, phase), pb.nanos(i));
  }
}

std::string LedgerCodeName(uint32_t code) {
  switch (static_cast<LedgerCode>(code)) {
    case LedgerCode::kServiceQueue: return "ServiceQueue";
    case LedgerCode::kClientSend: return "ClientSend";
    case LedgerCode::kTransport: return "Transport";
    case LedgerCode::kCallbackDelay: return "CallbackDelay";
    case LedgerCode::kShmemRequest: return "ShmemRequest";
    case LedgerCode::kShmemResponse: return "ShmemResponse";
    case LedgerCode::kOverflow: return "Overflow";
    case LedgerCode::kUnattributed: return "Unattributed";
    case LedgerCode::kRaftApplyQueue: return "RaftApplyQueue";
  }
  const char* name = ToCString(static_cast<WaitStateCode>(code));
  // Wait state names carry the enum's "k" prefix.
  return name ? std::string(name + 1) : Format("Code_$0", code);
}

LatencyLedger::LatencyLedger(int64_t start_nanos, uint32_t code)
    : start_nanos_(start_nanos), interval_start_(start_nanos), current_code_(code) {}

void LatencyLedger::Transition(uint32_t code, int64_t now) {
  std::lock_guard lock(mutex_);
  if (code == current_code_) {
    return;
  }
  if (now > interval_start_) {
    AddLedgerEntry(&entries_, MakeLedgerKey(0, 0, current_code_), now - interval_start_);
    interval_start_ = now;
  }
  current_code_ = code;
}

void LatencyLedger::AddChild(int64_t end_nanos, LedgerEntries&& child_entries) {
  std::lock_guard lock(mutex_);
  ++num_children_;
  if (end_nanos < child_end_nanos_) {
    return;
  }
  child_end_nanos_ = end_nanos;
  child_entries_ = std::move(child_entries);
}

void LatencyLedger::Finish(int64_t now, LatencyLedgerPB* out) {
  std::lock_guard lock(mutex_);
  if (now > interval_start_) {
    AddLedgerEntry(&entries_, MakeLedgerKey(0, 0, current_code_), now - interval_start_);
    interval_start_ = now;
  }
  out->Clear();
  out->set_total_nanos(now - start_nanos_);
  out->set_finish_mono_nanos(now);
  out->set_num_children(num_children_);
  for (const auto& entry : entries_) {
    out->add_keys(entry.key);
    out->add_nanos(entry.nanos);
  }
  LedgerEntries children;
  for (const auto& entry : child_entries_) {
    children.push_back(LedgerEntry{.key = ShiftLedgerKey(entry.key, 1), .nanos = entry.nanos});
  }
  FoldLedgerOverflow(&children, kMaxChildEntries);
  for (const auto& entry : children) {
    out->add_keys(entry.key);
    out->add_nanos(entry.nanos);
  }
}

void SetPgLatencyLedgerRequested(bool requested) {
  pg_latency_ledger_requested.store(requested, std::memory_order_relaxed);
}

bool IsPgLatencyLedgerRequested() {
  return pg_latency_ledger_requested.load(std::memory_order_relaxed);
}

} // namespace yb::ash
