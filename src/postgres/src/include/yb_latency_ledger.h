/*-------------------------------------------------------------------------
 *
 * yb_latency_ledger.h
 *	  Per round trip latency accounting of a YSQL backend.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/include/yb_latency_ledger.h
 * ----------
 */

#ifndef YB_LATENCY_LEDGER_H
#define YB_LATENCY_LEDGER_H

#include "yb/yql/pggate/ybc_latency_ledger.h"

extern PGDLLIMPORT int yb_latency_ledger_buffer_size;
extern PGDLLIMPORT double yb_latency_ledger_sample_rate;
extern PGDLLIMPORT int yb_latency_ledger_min_duration_ms;

/* True while this backend accounts a round trip. */
extern PGDLLIMPORT bool yb_latency_ledger_active;

extern Size YbLatencyLedgerShmemSize(void);
extern void YbLatencyLedgerShmemInit(void);
extern YbcLedgerRing *YbLatencyLedgerRing(void);

extern void YbLatencyLedgerStartRoundTrip(void);
extern void YbLatencyLedgerEndRoundTrip(void);
extern void YbLatencyLedgerBeforeReadCommand(void);
extern void YbLatencyLedgerOnError(void);
extern void YbLatencyLedgerWaitEvent(uint32 wait_event_info);
extern void YbLatencyLedgerWaitEventName(uint32 wait_event_info, char *buf,
										 size_t buf_size);

#endif							/* YB_LATENCY_LEDGER_H */
