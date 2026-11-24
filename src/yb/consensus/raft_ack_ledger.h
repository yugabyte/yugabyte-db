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

#include "yb/ash/latency_ledger.h"

namespace yb::consensus {

// How a Raft leader got an ack, from a follower or from its own WAL append. Built only while some
// operation keeps a latency ledger (see PeerMessageQueue::NoteLedgerOp), and handed to the
// operations that the ack made majority replicated, so that their ledgers can account for the time
// they waited for replication.
struct RaftAckLedger {
  // The ack came from the leader's own WAL append.
  bool local = false;
  // From a follower: when the request that carried the operations was sent.
  int64_t sent_nanos = 0;
  // When the leader got the ack: the WAL append finished, or the response callback started.
  int64_t acked_nanos = 0;
  // From a follower: the UpdateConsensus RPC as the leader saw it, including the follower's own
  // ledger, with depths relative to the RPC.
  ash::LedgerEntries rpc_entries;
  // Operations with an index in (from_index, to_index] became majority replicated with this ack.
  int64_t from_index = 0;
  int64_t to_index = 0;
};

} // namespace yb::consensus
