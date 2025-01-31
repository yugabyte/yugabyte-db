/*-------------------------------------------------------------------------
 *
 * procsignal.h
 *	  Routines for interprocess signaling
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/storage/procsignal.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PROCSIGNAL_H
#define PROCSIGNAL_H

#include "postgres.h"
#include "storage/backendid.h"
#include "storage/proc.h"


/*
 * Reasons for signaling a Postgres child process (a backend or an auxiliary
 * process, like checkpointer).  We can cope with concurrent signals for different
 * reasons.  However, if the same reason is signaled multiple times in quick
 * succession, the process is likely to observe only one notification of it.
 * This is okay for the present uses.
 *
 * Also, because of race conditions, it's important that all the signals be
 * defined so that no harm is done if a process mistakenly receives one.
 */
typedef enum
{
	PROCSIG_CATCHUP_INTERRUPT,	/* sinval catchup interrupt */
	PROCSIG_NOTIFY_INTERRUPT,	/* listen/notify interrupt */
	PROCSIG_PARALLEL_MESSAGE,	/* message from cooperating parallel backend */
	PROCSIG_WALSND_INIT_STOPPING,	/* ask walsenders to prepare for shutdown  */
	PROCSIG_BARRIER,			/* global barrier interrupt  */
	PROCSIG_LOG_MEMORY_CONTEXT, /* ask backend to log the memory contexts */

	/* Recovery conflict reasons */
	PROCSIG_RECOVERY_CONFLICT_DATABASE,
	PROCSIG_RECOVERY_CONFLICT_TABLESPACE,
	PROCSIG_RECOVERY_CONFLICT_LOCK,
	PROCSIG_RECOVERY_CONFLICT_SNAPSHOT,
	PROCSIG_RECOVERY_CONFLICT_BUFFERPIN,
	PROCSIG_RECOVERY_CONFLICT_STARTUP_DEADLOCK,

	NUM_PROCSIGNALS				/* Must be last! */
} ProcSignalReason;

typedef enum
{
	PROCSIGNAL_BARRIER_SMGRRELEASE	/* ask smgr to close files */
} ProcSignalBarrierType;

/*
 * prototypes for functions in procsignal.c
 */
extern Size ProcSignalShmemSize(void);
extern void ProcSignalShmemInit(void);

extern void ProcSignalInit(int pss_idx);
extern int	SendProcSignal(pid_t pid, ProcSignalReason reason,
						   BackendId backendId);

extern uint64 EmitProcSignalBarrier(ProcSignalBarrierType type);
extern void WaitForProcSignalBarrier(uint64 generation);
extern void ProcessProcSignalBarrier(void);

extern void procsignal_sigusr1_handler(SIGNAL_ARGS);

extern void CleanupProcSignalState(int status, Datum arg);
extern void CleanupProcSignalStateForProc(PGPROC *proc);

#endif							/* PROCSIGNAL_H */
