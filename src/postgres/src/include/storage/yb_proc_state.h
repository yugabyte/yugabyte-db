/*-------------------------------------------------------------------------
 *
 * yb_proc_state.h
 *	  YB per-process state that the postmaster reads after a backend dies, to
 *	  decide between cleaning up after it and a full restart.
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
 * src/include/storage/yb_proc_state.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_PROC_STATE_H
#define YB_PROC_STATE_H

typedef struct YbProcState
{
	/*
	 * We use this structure to keep track of locked LWLocks for release
	 * during error recovery.  Normally, only a few will be held at once, but
	 * occasionally the number can be much higher; for example, the
	 * pg_buffercache extension locks all buffer partitions simultaneously.
	 */
	bool		lwlock_acquired;
	int			spinlocks_acquired;
	/*
	 * Keep track of if the proc has been fully initialized. If a process
	 * that was not fully initialized is killed, we don't know how to clean up
	 * after it. Restart the postmaster in those cases.
	 */
	bool		initialization_completed;
	/*
	 * Keep track of if the proc has been terminated and is cleaning up
	 * after itself. If a process is killed while cleaning itself up, we don't
	 * know how to clean up after it. Restart the postmaster in those cases.
	 */
	bool		termination_started;

	/*
	 * True when we are in a critical section. Set by START_CRIT_SECTION
	 * and reset by END_CRIT_SECTION when we leave our last critical section.
	 *
	 * There may be cases where MyProc is NULL and we enter a critical section.
	 * These cases should be caught by initialization_completed and cause a
	 * postmaster restart anyway.
	 *
	 * In critical sections, ERRORs are escalated to PANICs, causing a
	 * postmaster restart. We also restart the postmaster if a process dies
	 * while in a critical section.
	 */
	bool		entered_crit_section;
} YbProcState;

extern PGDLLIMPORT YbProcState *YbMyProcState;

#endif							/* YB_PROC_STATE_H */
