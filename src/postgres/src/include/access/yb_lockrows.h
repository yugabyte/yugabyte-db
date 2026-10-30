/*-------------------------------------------------------------------------
 *
 * yb_lockrows.h
 *	  YB row-locking and conflict-handling functions.
 *
 *	  Provides explicit row locking (SELECT FOR UPDATE/SHARE) and
 *	  conflict resolution for YB-backed relations.  Called from
 *	  nodeLockRows.c.
 *
 *	  Lifecycle: this is an interim executor bypass of the tableam
 *	  tuple_lock callback, which upstream ExecLockRows routes
 *	  through, and #36167 tracks routing YB locks the same way.
 *	  That needs design work first: tuple_lock identifies the row by
 *	  heap TID (YB locks by ybctid), returns the locked row version
 *	  and the TM_FailureData that EvalPlanQual follows (YBCLockTuple
 *	  returns only a TM_Result), and has no room for the batched
 *	  flush that YBCFlushTupleLocks provides.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/include/access/yb_lockrows.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/tableam.h"
#include "nodes/lockoptions.h"

struct EState;

extern TM_Result YBCLockTuple(Relation relation, Datum ybctid, RowMarkType mode,
							  LockWaitPolicy wait_policy, struct EState *estate,
							  YbcIsExplicitlyLockedRowSkippedCheckHandleOptional *handle);
extern void YBCFlushTupleLocks(void);
