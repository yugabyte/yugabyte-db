/*-------------------------------------------------------------------------
 *
 * rumsharedmemutils.c
 *	  shared memory & vacuum utilities for the postgres RUM
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "miscadmin.h"

#include "time.h"
#include "commands/progress.h"
#include "commands/vacuum.h"
#include "postmaster/autovacuum.h"
#include "storage/indexfsm.h"
#include "storage/lmgr.h"
#include "storage/predicate.h"
#include "utils/backend_progress.h"
#include "storage/ipc.h"

#include "pg_documentdb_rum.h"

typedef struct RumSingleVacInfo
{
	LockRelId relid;             /* global identifier of an index */
	RumVacuumCycleId cycleid;    /* cycle ID for its active VACUUM */
} RumSingleVacInfo;

typedef struct RumSharedVacInfo
{
	RumVacuumCycleId cycle_ctr;         /* cycle ID most recently assigned */
	int num_vacuums;                    /* number of currently active VACUUMs */
	int max_vacuums;                    /* allocated length of vacuums[] array */
	RumSingleVacInfo vacuums[FLEXIBLE_ARRAY_MEMBER];
} RumSharedVacInfo;

PGDLLEXPORT RumSharedVacInfo *rumSharedVacInfo;
extern int32_t RumVacuumCycleIdOverride;

PGDLLEXPORT int RumParallelScanTrancheId = 0;
PGDLLEXPORT const char *RumParallelScanTrancheName = "RUM parallel scan Tranche";

static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

#if PG_VERSION_NUM >= 150000
static shmem_request_hook_type prev_shmem_request_hook = NULL;
#endif

static bool InitializeRumVacuumStateRun = false;


static Size
RumVacuumShmemSize(void)
{
	Size size;

	size = offsetof(RumSharedVacInfo, vacuums);
	size = add_size(size, mul_size(MaxBackends, sizeof(RumSingleVacInfo)));
	return size;
}


static void
RumVacuumShmemInit(void)
{
	bool found;

	rumSharedVacInfo = (RumSharedVacInfo *) ShmemInitStruct("RUM Shared Vacuum State",
															RumVacuumShmemSize(),
															&found);

	if (!IsUnderPostmaster)
	{
		/* Initialize shared memory area */
		Assert(!found);

		/*
		 * It doesn't really matter what the cycle counter starts at, but
		 * having it always start the same doesn't seem good.  Seed with
		 * low-order bits of time() instead.
		 */
		rumSharedVacInfo->cycle_ctr = (RumVacuumCycleId) time(NULL);

		rumSharedVacInfo->num_vacuums = 0;
		rumSharedVacInfo->max_vacuums = MaxBackends;
	}
	else
	{
		Assert(found);
	}
}


static void
InitializeRumParallelLWLock(void)
{
	if (RumParallelScanTrancheId == 0)
	{
#if PG_VERSION_NUMBER >= 180000
		RumParallelScanTrancheId = LWLockNewTrancheId(RumParallelScanTrancheName);
#else
		RumParallelScanTrancheId = LWLockNewTrancheId();
#endif
	}
}


static void
RumVacuumSharedMemoryRequest(void)
{
	if (prev_shmem_request_hook != NULL)
	{
		prev_shmem_request_hook();
	}

	/* Request ShMem from modules below */
	RequestAddinShmemSpace(RumVacuumShmemSize());
}


static void
RumVacuumSharedMemoryInit(void)
{
	/* CODESYNC: With Shmem request above */
	RumVacuumShmemInit();
	InitializeRumParallelLWLock();

	if (prev_shmem_startup_hook != NULL)
	{
		prev_shmem_startup_hook();
	}
}


PGDLLEXPORT void
InitializeRumVacuumState(void)
{
	if (InitializeRumVacuumStateRun)
	{
		return;
	}

#if PG_VERSION_NUM >= 150000
	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = RumVacuumSharedMemoryRequest;
#else
	RumVacuumSharedMemoryRequest();
#endif

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = RumVacuumSharedMemoryInit;

	InitializeRumVacuumStateRun = true;
}


PGDLLEXPORT RumVacuumCycleId
rum_start_vacuum_cycle_id(Relation rel)
{
	RumVacuumCycleId result;
	int i;
	RumSingleVacInfo *vac;

	if (RumVacuumCycleIdOverride > 0 && RumVacuumCycleIdOverride <= UINT16_MAX)
	{
		return (RumVacuumCycleId) RumVacuumCycleIdOverride;
	}

	LWLockAcquire(BtreeVacuumLock, LW_EXCLUSIVE);

	/*
	 * Assign the next cycle ID, being careful to avoid zero as well as the
	 * reserved high values.
	 */
	result = ++(rumSharedVacInfo->cycle_ctr);
	if (result == 0)
	{
		result = rumSharedVacInfo->cycle_ctr = 1;
	}

	/* Let's just make sure there's no entry already for this index */
	for (i = 0; i < rumSharedVacInfo->num_vacuums; i++)
	{
		vac = &rumSharedVacInfo->vacuums[i];
		if (vac->relid.relId == rel->rd_lockInfo.lockRelId.relId &&
			vac->relid.dbId == rel->rd_lockInfo.lockRelId.dbId)
		{
			/*
			 * Unlike most places in the backend, we have to explicitly
			 * release our LWLock before throwing an error.  This is because
			 * we expect rum_end_vacuum() to be called before transaction
			 * abort cleanup can run to release LWLocks.
			 */
			LWLockRelease(BtreeVacuumLock);
			elog(ERROR, "multiple active vacuums for index \"%s\"",
				 RelationGetRelationName(rel));
		}
	}

	/* OK, add an entry */
	if (rumSharedVacInfo->num_vacuums >= rumSharedVacInfo->max_vacuums)
	{
		LWLockRelease(BtreeVacuumLock);
		elog(ERROR, "out of rumvacuuminfo slots");
	}
	vac = &rumSharedVacInfo->vacuums[rumSharedVacInfo->num_vacuums];
	vac->relid = rel->rd_lockInfo.lockRelId;
	vac->cycleid = result;
	rumSharedVacInfo->num_vacuums++;

	LWLockRelease(BtreeVacuumLock);
	return result;
}


PGDLLEXPORT void
rum_end_vacuum_cycle_id(Relation rel)
{
	int i;

	/* Given how short we hold this lock and given that vacuums generally
	 * run with a lock on the table, we reuse the Btree vacuum lock.
	 */
	LWLockAcquire(BtreeVacuumLock, LW_EXCLUSIVE);

	/* Find the array entry */
	for (i = 0; i < rumSharedVacInfo->num_vacuums; i++)
	{
		RumSingleVacInfo *vac = &rumSharedVacInfo->vacuums[i];

		if (vac->relid.relId == rel->rd_lockInfo.lockRelId.relId &&
			vac->relid.dbId == rel->rd_lockInfo.lockRelId.dbId)
		{
			/* Remove it by shifting down the last entry */
			*vac = rumSharedVacInfo->vacuums[rumSharedVacInfo->num_vacuums - 1];
			rumSharedVacInfo->num_vacuums--;
			break;
		}
	}

	LWLockRelease(BtreeVacuumLock);
}


PGDLLEXPORT RumVacuumCycleId
rum_vacuum_get_cycleId(Relation rel)
{
	RumVacuumCycleId result = 0;
	int i;

	if (RumVacuumCycleIdOverride > 0 && RumVacuumCycleIdOverride <= UINT16_MAX)
	{
		return (RumVacuumCycleId) RumVacuumCycleIdOverride;
	}

	/* Share lock is enough since this is a read-only operation */
	LWLockAcquire(BtreeVacuumLock, LW_SHARED);

	for (i = 0; i < rumSharedVacInfo->num_vacuums; i++)
	{
		RumSingleVacInfo *vac = &rumSharedVacInfo->vacuums[i];

		if (vac->relid.relId == rel->rd_lockInfo.lockRelId.relId &&
			vac->relid.dbId == rel->rd_lockInfo.lockRelId.dbId)
		{
			result = vac->cycleid;
			break;
		}
	}

	LWLockRelease(BtreeVacuumLock);
	return result;
}
