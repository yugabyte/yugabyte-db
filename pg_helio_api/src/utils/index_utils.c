/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/index_utils.c
 *
 * Utilities for build/drop index.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <storage/proc.h>
#include <utils/snapmgr.h>

#include "utils/index_utils.h"

/*
 * set_indexsafe_procflags sets PROC_IN_SAFE_IC flag in MyProc->statusFlags.
 *
 * Copied from pg/src/backend/commands/indexcmds.c
 * Also see pg commit c98763bf51bf610b3ee7e209fc76c3ff9a6b3163.
 */
void
set_indexsafe_procflags(void)
{
	Assert(MyProc->xid == InvalidTransactionId &&
		   MyProc->xmin == InvalidTransactionId);

	LWLockAcquire(ProcArrayLock, LW_EXCLUSIVE);
	MyProc->statusFlags |= PROC_IN_SAFE_IC;
	ProcGlobal->statusFlags[MyProc->pgxactoff] = MyProc->statusFlags;
	LWLockRelease(ProcArrayLock);
}


/*
 * PopAllActiveSnapshots pops all currently active snapshots, which is typically
 * necessary before committing.
 */
void
PopAllActiveSnapshots(void)
{
	while (ActiveSnapshotSet())
	{
		PopActiveSnapshot();
	}
}
