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
// YSQL latency ledger: for every protocol round trip of a YSQL backend (from the first message
// after ReadyForQuery to the next ReadyForQuery), an exact account of where the time went. Depth 0
// is the backend itself, by YSQL phase and wait event; deeper entries come from the ledgers the
// local tserver returns for each request (see yb/ash/latency_ledger.h). Finished round trips go
// to a ring buffer in shared memory, which the YSQL webserver serves at /latency-ledger.

#ifndef YB_YQL_PGGATE_YBC_LATENCY_LEDGER_H
#define YB_YQL_PGGATE_YBC_LATENCY_LEDGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YB_LEDGER_MAX_ENTRIES 48

// Entry durations are stored in units of 64 ns (up to ~274 s).
#define YB_LEDGER_DURATION_SHIFT 6

// YSQL phases of a round trip, stored in bits 24..27 of a ledger key.
typedef enum YbcLedgerPhase {
  YB_LEDGER_PHASE_OTHER = 0,
  YB_LEDGER_PHASE_PARSE = 1,      // Raw parsing, analysis and rewriting.
  YB_LEDGER_PHASE_PLAN = 2,       // Planning, including plan cache lookups.
  YB_LEDGER_PHASE_BIND = 3,       // Bind message processing other than planning.
  YB_LEDGER_PHASE_EXECUTE = 4,    // Running the portal.
  YB_LEDGER_PHASE_COMMIT = 5,     // Committing the transaction.
  YB_LEDGER_PHASE_ABORT = 6,      // Aborting the transaction, including error cleanup.
  YB_LEDGER_PHASE_CLIENT_IO = 7,  // Waiting for further messages of the round trip.
  YB_LEDGER_PHASE_DESCRIBE = 8,   // Describe message processing.
} YbcLedgerPhase;

// Record flags.
#define YB_LEDGER_FLAG_SAMPLED_SESSION 0x1  // Every round trip of this session is recorded.
#define YB_LEDGER_FLAG_COMMITTED 0x2        // A transaction committed in this round trip.
#define YB_LEDGER_FLAG_ABORTED 0x4          // A transaction aborted in this round trip.
#define YB_LEDGER_FLAG_IN_TXN_BLOCK 0x8     // The round trip ended inside a transaction block.
#define YB_LEDGER_FLAG_ERROR 0x10           // The round trip raised an error.
#define YB_LEDGER_FLAG_OVERFLOW 0x20        // Some entries were folded into an Overflow entry.

typedef struct YbcLedgerEntry {
  uint32_t key;
  uint32_t duration;  // In units of 1 << YB_LEDGER_DURATION_SHIFT nanoseconds.
} YbcLedgerEntry;

typedef struct YbcLedgerRecord {
  // Sequence number of the record; 0 while the slot is being written.
  uint64_t seq;
  // Wall clock time at which the round trip started, in microseconds since the Unix epoch.
  int64_t start_time_us;
  int64_t duration_ns;
  uint64_t query_id;
  int32_t pid;
  // Number of top-level transactions this backend had started when the round trip ended.
  uint32_t txn_seq;
  uint32_t db_oid;
  uint16_t num_entries;
  uint16_t flags;
  // Time the backend spent on a CPU and waiting for one during the round trip (from the kernel's
  // schedstat; 0 if unavailable).
  int64_t cpu_ns;
  int64_t runqueue_ns;
  YbcLedgerEntry entries[YB_LEDGER_MAX_ENTRIES];
} YbcLedgerRecord;

typedef struct YbcLedgerRing {
  uint64_t next_seq;
  uint64_t num_slots;
  YbcLedgerRecord slots[];
} YbcLedgerRing;

// Writes "<wait event type>:<wait event>" for a YSQL wait event into buf.
typedef void (*YbcLedgerWaitEventNameFunc)(uint32_t wait_event_info, char* buf, size_t buf_size);

typedef struct YbcLedgerRoundTripInfo {
  uint64_t query_id;
  int32_t pid;
  uint32_t txn_seq;
  uint32_t db_oid;
  uint16_t flags;
} YbcLedgerRoundTripInfo;

size_t YBCLedgerRingSize(int num_slots);

// Called once the ring is allocated in shared memory, before backends are forked.
void YBCLedgerSetRing(YbcLedgerRing* ring);

// Starts accounting a round trip, asking the tserver for ledgers of the requests it sends.
void YBCLedgerStartRoundTrip(void);

// Whether a round trip is being accounted.
bool YBCLedgerIsActive(void);

// Switches the current YSQL phase, returning the previous one.
uint8_t YBCLedgerSetPhase(uint8_t phase);

// Enters / leaves the phase of a distributed-tracing span (see YB_DIST_TRACE_START_SPAN). Spans
// that do not name a phase keep the current one.
void YBCLedgerPushPhase(const char* span_name);
void YBCLedgerPopPhase(void);

// Reports a change of the backend's wait event (0 when on CPU).
void YBCLedgerWaitEvent(uint32_t wait_event_info);

// Finishes the round trip. It is written to the ring if record is true or it took at least
// min_duration_ns (when that is not negative).
void YBCLedgerEndRoundTrip(
    const YbcLedgerRoundTripInfo* info, bool record, int64_t min_duration_ns);

// Abandons the current round trip without recording it.
void YBCLedgerCancelRoundTrip(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // YB_YQL_PGGATE_YBC_LATENCY_LEDGER_H
