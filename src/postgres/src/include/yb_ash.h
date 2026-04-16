/*-------------------------------------------------------------------------
 *
 * yb_ash.h
 *    Utilities for Active Session History/Yugabyte (Postgres layer) integration
 *    that have to be defined on the PostgreSQL side.
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
 * src/include/yb_ash.h
 * ----------
 */

#ifndef YB_ASH_H
#define YB_ASH_H

#include "postgres.h"

#include "storage/proc.h"
#include "utils/guc.h"
#include "utils/timestamp.h"

#define YbAshIsClientAddrSet() \
	(yb_enable_ash && !IsBootstrapProcessingMode() && !YBIsInitDbModeEnvVarSet())

/*
 * query_id 0 is never produced by pg_stat_statements for a real query, so it
 * marks an invalid / uninitialized QP pair.
 */
#define YB_ASH_INVALID_QUERY_ID 0
/*
 * Default plan_id when QPM is disabled or for utility statements that have no
 * query plan.
 */
#define YB_ASH_DEFAULT_PLAN_ID 0
/*
 * TODO: plan_id is computed via hash_any_extended which can return 0, so a
 * plan_id of 0 doesn't reliably indicate "no plan". Consider reserving a
 * distinct invalid plan_id and checking it here as well.
 */
#define YbAshIsInvalidQpPair(pair) ((pair).query_id == YB_ASH_INVALID_QUERY_ID)

/* GUC variables */
extern bool yb_enable_ash;
extern int	yb_ash_circular_buffer_size;
extern int	yb_ash_sampling_interval_ms;
extern int	yb_ash_sample_size;

typedef bool (*YbAshTrackNestedQueries) (void);
extern YbAshTrackNestedQueries yb_ash_track_nested_queries;

extern Size YbAshShmemSize(void);
extern void YbAshShmemInit(void);

extern void YbAshRegister(void);
extern void YbAshMain(Datum main_arg);

extern void YbAshInit(void);
extern void YbAshSetDatabaseId(Oid database_id);
extern bool YbAshShouldIgnoreWaitEvent(uint32 wait_event_info);

extern void YbAshMaybeIncludeSample(PGPROC *proc, int num_procs,
									TimestampTz sample_time,
									int *samples_considered);
extern void YbAshStoreSample(PGPROC *proc, int num_procs,
							 TimestampTz sample_time,
							 int index);
extern void YbAshFillSampleWeight(int samples_considered);

extern bool yb_ash_circular_buffer_size_check_hook(int *newval,
												   void **extra,
												   GucSource source);

extern void YbAshSetMetadata(void);
extern void YbAshUnsetMetadata(void);

extern void YbAshSetOneTimeMetadata(void);
extern void YbAshSetMetadataForBgworkers(void);
extern uint64 YbAshGetConstQueryId(void);

#endif							/* YB_ASH_H */
