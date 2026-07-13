/*-------------------------------------------------------------------------
 *
 * yb_dist_trace.h
 *	  Distributed tracing/Yugabyte (Postgres layer) executor hooks.
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
 * src/include/yb_dist_trace.h
 * ----------
 */

#ifndef YB_DIST_TRACE_H
#define YB_DIST_TRACE_H

struct PlanState;

extern void YbDistTraceInstallExecutorHooks(void);
extern void YbDistTraceEndNodeSpans(struct PlanState *planstate, bool errored);
extern void YbDistTraceEndOpenNodeSpans(void);

#endif							/* YB_DIST_TRACE_H */
