/* ----------
 * yb_memory_watcher.h
 *
 * Background worker that reports PostgreSQL memory usage when the node or
 * cgroup is low on memory.
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
 * src/include/yb_memory_watcher.h
 * ----------
 */

#ifndef YB_MEMORY_WATCHER_H
#define YB_MEMORY_WATCHER_H

#include "postgres.h"

extern bool yb_enable_memory_watcher;
extern int	yb_memory_watcher_interval_ms;
extern int	yb_memory_watcher_threshold_pct;
extern int	yb_memory_watcher_report_interval_s;

extern void YbMemoryWatcherRegister(void);
extern void YbMemoryWatcherMain(Datum main_arg);

#endif							/* YB_MEMORY_WATCHER_H */
