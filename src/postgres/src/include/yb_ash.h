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

extern void GetAshDataForQueryDiagnosticsBundle(TimestampTz start_time, TimestampTz end_time,
												int64 query_id, StringInfo output_buffer,
												char *description);

#endif							/* YB_ASH_H */
