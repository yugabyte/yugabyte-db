/*-------------------------------------------------------------------------
 *
 * yb_mem_report.h
 *	  Test-only report of the idle and peak memory of every postgres process.
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
 * src/include/yb_mem_report.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef YB_MEM_REPORT_H
#define YB_MEM_REPORT_H

#include "c.h"

extern bool yb_mem_report_enabled;

extern void YbMemReportPostmasterInit(void);
extern Size YbMemReportShmemSize(void);
extern void YbMemReportShmemInit(void);
extern void YbMemReportInitChild(void);
extern void YbMemReportAfterStatement(void);
extern int	YbMemReportWaitpid(int *exitstatus);

#endif							/* YB_MEM_REPORT_H */
