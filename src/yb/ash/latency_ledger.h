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
// A latency ledger is an exact, per-request account of where time went. It piggybacks on ASH:
// every wait state transition of a request's WaitStateInfo closes an interval and charges it to
// the wait state the request was in. When a request issues RPCs on its own behalf, the RPC that
// finished last (the critical path) is folded into the caller's ledger one hop deeper, together
// with the time the RPC spent outside the server that handled it (queueing on the client,
// transport, callback scheduling). The result is returned in rpc::ResponseHeader.latency_ledger.
//
// Intervals are measured on CLOCK_MONOTONIC, and only durations cross host boundaries.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include <boost/container/small_vector.hpp>

#include "yb/util/locks.h"

namespace yb {

class LatencyLedgerPB;

namespace ash {

// Ledger key layout (32 bits):
//   [31..28] hop depth: 0 = the process that owns the ledger, 1 = an RPC it issued, and so on.
//   [27..24] YSQL phase. Only the YSQL backend sets it; 0 elsewhere.
//   [23..0]  code: a wait state code (or a LedgerCode) with its top byte moved to bits 16..23.
constexpr uint32_t kLedgerDepthShift = 28;
constexpr uint32_t kLedgerPhaseShift = 24;
constexpr uint32_t kLedgerMaxDepth = 15;
constexpr uint32_t kLedgerMaxPhase = 15;

// Intervals that have no ASH wait state of their own. They use component 0xF, which ASH does not
// use, so they cannot collide with a WaitStateCode.
enum class LedgerCode : uint32_t {
  // An inbound call waited in the service queue for a handler thread.
  kServiceQueue = 0xF0000001U,
  // An outbound call was created but not yet written to its connection.
  kClientSend,
  // An outbound call was written to its connection until its response arrived, minus the time
  // the server reported in its ledger: network, reactors and (de)serialization on both sides.
  kTransport,
  // The response of an outbound call arrived until its callback started.
  kCallbackDelay,
  // A YSQL backend wrote a request into shared memory until the tserver started handling it.
  kShmemRequest,
  // The tserver finished a shared memory response until the YSQL backend read it.
  kShmemResponse,
  // Time charged to entries that did not fit into a fixed-size record.
  kOverflow,
  // Time the parent spent waiting on its children that no child ledger accounts for.
  kUnattributed,
  // A Raft operation was majority replicated but its apply had not started.
  kRaftApplyQueue,
};

constexpr uint32_t CompressLedgerCode(uint32_t code) {
  return ((code >> 24) << 16) | (code & 0xFFFFU);
}

constexpr uint32_t DecompressLedgerCode(uint32_t compressed) {
  return ((compressed >> 16) << 24) | (compressed & 0xFFFFU);
}

constexpr uint32_t MakeLedgerKey(uint32_t depth, uint32_t phase, uint32_t code) {
  return (std::min(depth, kLedgerMaxDepth) << kLedgerDepthShift) |
         ((phase & kLedgerMaxPhase) << kLedgerPhaseShift) |
         CompressLedgerCode(code);
}

constexpr uint32_t MakeLedgerKey(uint32_t depth, uint32_t phase, LedgerCode code) {
  return MakeLedgerKey(depth, phase, static_cast<uint32_t>(code));
}

constexpr uint32_t LedgerKeyDepth(uint32_t key) {
  return key >> kLedgerDepthShift;
}

constexpr uint32_t LedgerKeyPhase(uint32_t key) {
  return (key >> kLedgerPhaseShift) & kLedgerMaxPhase;
}

constexpr uint32_t LedgerKeyCode(uint32_t key) {
  return DecompressLedgerCode(key & 0xFFFFFFU);
}

// Moves key depth_offset hops deeper and, when phase is not 0, replaces its phase.
constexpr uint32_t ShiftLedgerKey(uint32_t key, uint32_t depth_offset, uint32_t phase = 0) {
  return MakeLedgerKey(
      LedgerKeyDepth(key) + depth_offset, phase ? phase : LedgerKeyPhase(key), LedgerKeyCode(key));
}

// CLOCK_MONOTONIC in nanoseconds. Comparable across processes on the same host.
int64_t LedgerNowNanos();

struct LedgerEntry {
  uint32_t key;
  int64_t nanos;
};

using LedgerEntries = boost::container::small_vector<LedgerEntry, 16>;

// Adds nanos to the entry with key, appending one if there is none.
void AddLedgerEntry(LedgerEntries* entries, uint32_t key, int64_t nanos);

// Keeps at most max_entries: when there are more, the smallest are folded into one kOverflow
// entry per depth and phase, so every depth still adds up. Returns whether it folded any.
bool FoldLedgerOverflow(LedgerEntries* entries, size_t max_entries);

// Appends the entries of pb, depth_offset hops deeper, with phase set when it is not 0.
void AppendLedgerEntries(
    const LatencyLedgerPB& pb, uint32_t depth_offset, uint32_t phase, LedgerEntries* out);

// Name of a wait state code or LedgerCode, e.g. "Raft_WaitingForReplication" or "Transport".
std::string LedgerCodeName(uint32_t code);

class LatencyLedger {
 public:
  LatencyLedger(int64_t start_nanos, uint32_t code);

  // Closes the current interval at now, charging it to the current code, and starts an interval
  // for code. Thread safe.
  void Transition(uint32_t code, int64_t now);
  void Transition(LedgerCode code, int64_t now) {
    Transition(static_cast<uint32_t>(code), now);
  }

  // Records a finished child: an RPC issued on behalf of this request. child_entries use depths
  // relative to the child: 0 for the time the caller observed around the RPC (client send,
  // transport, callback delay) and the server's own time, 1 for the RPCs that server issued, etc.
  // They are kept one hop deeper. Only the child that finished last is kept, since it is the one
  // that ended the parent's wait.
  void AddChild(int64_t end_nanos, LedgerEntries&& child_entries);

  // Closes the current interval at now and writes the ledger to out. The ledger stays usable;
  // later transitions start from now.
  void Finish(int64_t now, LatencyLedgerPB* out);

  int64_t start_nanos() const { return start_nanos_; }

 private:
  const int64_t start_nanos_;

  simple_spinlock mutex_;
  int64_t interval_start_ GUARDED_BY(mutex_);
  uint32_t current_code_ GUARDED_BY(mutex_);
  LedgerEntries entries_ GUARDED_BY(mutex_);
  int64_t child_end_nanos_ GUARDED_BY(mutex_) = 0;
  LedgerEntries child_entries_ GUARDED_BY(mutex_);
  uint32_t num_children_ GUARDED_BY(mutex_) = 0;
};

// Whether this YSQL backend asks for latency ledgers. Read by PgWaitStateInfo when it builds the
// ASH metadata of a request.
void SetPgLatencyLedgerRequested(bool requested);
bool IsPgLatencyLedgerRequested();

} // namespace ash
} // namespace yb
