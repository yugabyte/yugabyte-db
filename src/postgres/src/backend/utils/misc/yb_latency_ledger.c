/*-------------------------------------------------------------------------
 *
 * yb_latency_ledger.c
 *	  Per round trip latency accounting of a YSQL backend.
 *
 * A round trip runs from the first protocol message read after
 * ReadyForQuery to the next ReadyForQuery.  While one is accounted, pggate
 * charges every interval to the current YSQL phase (taken from the
 * distributed-tracing span stack) and wait event, asks the local tserver for
 * a latency ledger of every request, and folds those ledgers in one hop
 * deeper.  Finished round trips go to a ring buffer in shared memory, which
 * the YSQL webserver serves at /latency-ledger.
 *
 * A backend accounts its round trips when it is sampled
 * (yb_latency_ledger_sample_rate, drawn once per backend and rate) and then
 * records all of them, or when yb_latency_ledger_min_duration_ms is not
 * negative, and then records those that took at least that long.
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
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_latency_ledger.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/xact.h"
#include "common/pg_prng.h"
#include "miscadmin.h"
#include "storage/shmem.h"
#include "utils/backend_status.h"
#include "utils/wait_event.h"
#include "yb_latency_ledger.h"

int			yb_latency_ledger_buffer_size = 0;
double		yb_latency_ledger_sample_rate = 0.0;
int			yb_latency_ledger_min_duration_ms = -1;

bool		yb_latency_ledger_active = false;

static YbcLedgerRing *ledger_ring = NULL;

/* The sampling decision of this backend, and the rate it was drawn for. */
static bool session_sampled = false;
static double session_sampled_rate = -1.0;

/* Top-level transactions that finished in this backend. */
static uint32 finished_txns = 0;
static bool xact_callback_registered = false;

/* State of the round trip in progress. */
static uint32 round_trip_txn_seq = 0;
static uint16 round_trip_flags = 0;

Size
YbLatencyLedgerShmemSize(void)
{
	if (yb_latency_ledger_buffer_size <= 0)
		return 0;
	return YBCLedgerRingSize(yb_latency_ledger_buffer_size);
}

void
YbLatencyLedgerShmemInit(void)
{
	bool		found;

	if (yb_latency_ledger_buffer_size <= 0)
		return;

	ledger_ring = ShmemInitStruct("YB latency ledger",
								  YbLatencyLedgerShmemSize(), &found);
	if (!found)
	{
		memset(ledger_ring, 0, YbLatencyLedgerShmemSize());
		ledger_ring->num_slots = yb_latency_ledger_buffer_size;
	}
	YBCLedgerSetRing(ledger_ring);
}

YbcLedgerRing *
YbLatencyLedgerRing(void)
{
	return ledger_ring;
}

static void
yb_latency_ledger_xact_callback(XactEvent event, void *arg)
{
	switch (event)
	{
		case XACT_EVENT_COMMIT:
		case XACT_EVENT_PREPARE:
			++finished_txns;
			round_trip_flags |= YB_LEDGER_FLAG_COMMITTED;
			break;
		case XACT_EVENT_ABORT:
			++finished_txns;
			round_trip_flags |= YB_LEDGER_FLAG_ABORTED;
			break;
		default:
			break;
	}
}

/*
 * Called after each protocol message is read.  Starts a round trip unless
 * one is in progress.
 */
void
YbLatencyLedgerStartRoundTrip(void)
{
	if (!ledger_ring)
		return;

	if (yb_latency_ledger_active)
	{
		/* Further messages of the same round trip. */
		YBCLedgerSetPhase(YB_LEDGER_PHASE_OTHER);
		return;
	}

	if (yb_latency_ledger_sample_rate != session_sampled_rate)
	{
		session_sampled_rate = yb_latency_ledger_sample_rate;
		session_sampled =
			pg_prng_double(&pg_global_prng_state) < session_sampled_rate;
	}
	if (!session_sampled && yb_latency_ledger_min_duration_ms < 0)
		return;

	if (!xact_callback_registered)
	{
		RegisterXactCallback(yb_latency_ledger_xact_callback, NULL);
		xact_callback_registered = true;
	}

	round_trip_txn_seq = finished_txns;
	round_trip_flags = session_sampled ? YB_LEDGER_FLAG_SAMPLED_SESSION : 0;
	YBCLedgerStartRoundTrip();
	yb_latency_ledger_active = true;
}

/*
 * Called right before ReadyForQuery.
 */
void
YbLatencyLedgerEndRoundTrip(void)
{
	YbcLedgerRoundTripInfo info;

	if (!yb_latency_ledger_active)
		return;
	yb_latency_ledger_active = false;

	info.query_id = pgstat_get_my_query_id();
	info.pid = MyProcPid;
	info.txn_seq = round_trip_txn_seq;
	info.db_oid = MyDatabaseId;
	info.flags = round_trip_flags;
	if (IsTransactionBlock())
		info.flags |= YB_LEDGER_FLAG_IN_TXN_BLOCK;

	YBCLedgerEndRoundTrip(&info, session_sampled,
						  yb_latency_ledger_min_duration_ms >= 0 ?
						  (int64) yb_latency_ledger_min_duration_ms * 1000000 :
						  -1);
}

/*
 * Called before the backend reads the next protocol message.  Inside a
 * round trip, that is time spent waiting for the rest of the client's
 * messages.
 */
void
YbLatencyLedgerBeforeReadCommand(void)
{
	if (yb_latency_ledger_active)
		YBCLedgerSetPhase(YB_LEDGER_PHASE_CLIENT_IO);
}

/*
 * Called when the main loop catches an error, before the transaction is
 * aborted.
 */
void
YbLatencyLedgerOnError(void)
{
	if (!yb_latency_ledger_active)
		return;
	round_trip_flags |= YB_LEDGER_FLAG_ERROR;
	YBCLedgerSetPhase(YB_LEDGER_PHASE_ABORT);
}

void
YbLatencyLedgerWaitEvent(uint32 wait_event_info)
{
	YBCLedgerWaitEvent(wait_event_info);
}

/*
 * Names a wait event for the webserver, which runs in a backend of its own.
 */
void
YbLatencyLedgerWaitEventName(uint32 wait_event_info, char *buf,
							 size_t buf_size)
{
	const char *type = pgstat_get_wait_event_type(wait_event_info);
	const char *name = pgstat_get_wait_event(wait_event_info);

	snprintf(buf, buf_size, "%s:%s", type ? type : "?", name ? name : "?");
}
