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

/* GUC variables */
extern bool yb_ash_enable_infra;
extern bool yb_enable_ash;
extern int yb_ash_circular_buffer_size;
extern int yb_ash_sampling_interval_ms;
extern int yb_ash_sample_size;

extern Size YbAshShmemSize(void);
extern void YbAshShmemInit(void);

extern void YbAshRegister(void);
extern void YbAshMain(Datum main_arg);

extern void YbAshInstallHooks(void);
extern void YbAshSetSessionId(uint64 session_id);

extern bool YbAshStoreSample(PGPROC *proc, int num_procs,
							 TimestampTz sample_time,
							 int *samples_stored);

extern bool yb_enable_ash_check_hook(bool *newval,
									 void **extra,
									 GucSource source);

#endif							/* YB_ASH_H */
