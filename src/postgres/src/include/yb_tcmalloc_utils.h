/* ----------
 * yb_tcmalloc_utils.h
 *
 * Utilities for TCMalloc heap profiling for Postgres backend processes.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/yb_tcmalloc_utils.h
 * ----------
 */

#ifndef YB_TCMALLOC_UTILS_H
#define YB_TCMALLOC_UTILS_H

extern int yb_log_heap_snapshot_on_exit_threshold;

/*
 * Handle and process logging of heap snapshot interrupt.
 */
extern void HandleLogHeapSnapshotInterrupt(void);
extern void HandleLogHeapSnapshotPeakInterrupt(void);
extern void ProcessLogHeapSnapshotInterrupt(void);

/*
 * Setup the hook to log the heap snapshot when a backend process exits.
 */
extern void YbSetupHeapSnapshotProcExit(void);

#endif							/* YB_TCMALLOC_UTILS_H */
