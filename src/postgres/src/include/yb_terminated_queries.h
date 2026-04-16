/* -------------------------------------------------------------------------
 *
 * yb_terminated_queries.h
 * 	  Declarations for YB Terminated Queries.
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
 * 	  src/include/yb_terminated_queries.h
 * -------------------------------------------------------------------------
 */

#ifndef YB_TERMINATED_QUERIES_H
#define YB_TERMINATED_QUERIES_H

#include "c.h"

/* Functions defined for yb_terminated_queries */
extern void yb_report_query_termination(char *message, int pid);
extern Size YbTerminatedQueriesShmemSize(void);
extern void YbTerminatedQueriesShmemInit(void);

#endif							/* YB_TERMINATED_QUERIES_H */
