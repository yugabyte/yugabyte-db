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
 *	  through.  Routing YB locks the same way needs design work
 *	  first: tuple_lock's contract is TID-shaped (YB locks by
 *	  ybctid), carries TM_Result/EvalPlanQual semantics that YB does
 *	  not map onto, and has no room for the batched flush that
 *	  YBCFlushTupleLocks provides.  Kept separate from
 *	  yb_special_scans.h, whose entry points have no tableam
 *	  callback to route through, and from yb_table_scan.h, whose
 *	  removal #32268 tracks without covering locking.
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
