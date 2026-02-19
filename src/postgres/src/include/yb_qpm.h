/*-------------------------------------------------------------------------
 *
 * yb_qpm.h
 *.   Query Plan Management/Yugabyte (Postgres layer)
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
 * src/include/yb_qpm.h
 * ----------
 */

#ifndef YB_QPM_H
#define YB_QPM_H

#include "postgres.h"

#include "commands/explain.h"
#include "utils/guc.h"
#include "utils/timestamp.h"

#define QPM_HINT_TEXT_SIZE 2048
#define QPM_PLAN_TEXT_SIZE 4096
#define QPM_PARAM_TEXT_SIZE 256

#define YbQpmIsEnabled() (yb_qpm_configuration.track != YB_QPM_TRACK_NONE)

extern Size YbQpmShmemSize(void);
extern void YbQpmShmemInit(void);
extern void YbQpmInit(void);

extern char *YbQpmExplainPlan(QueryDesc *queryDesc, ExplainFormat format);

#endif							/* YB_QPM_H */
