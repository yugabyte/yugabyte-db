/*-------------------------------------------------------------------------
 *
 * xlogfuncs.c
 *
 * PostgreSQL write-ahead log manager user interface functions
 *
 * This file contains WAL control and information functions.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/backend/access/transam/xlogfuncs.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>

#include "access/htup_details.h"
#include "access/xlog_internal.h"
#include "access/xlogrecovery.h"
#include "access/xlogutils.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "replication/walreceiver.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/smgr.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/numeric.h"
#include "utils/pg_lsn.h"
#include "utils/timestamp.h"
#include "utils/tuplestore.h"

/*
 * Store label file and tablespace map during backups.
 */
static StringInfo label_file;
static StringInfo tblspc_map_file;

/*
 * pg_backup_start: set up for taking an on-line backup dump
 *
 * Essentially what this does is to create a backup label file in $PGDATA,
 * where it will be archived as part of the backup dump.  The label file
 * contains the user-supplied label string (typically this would be used
 * to tell where the backup dump will be stored) and the starting time and
 * starting WAL location for the dump.
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_backup_start(PG_FUNCTION_ARGS)
{
	text	   *backupid = PG_GETARG_TEXT_PP(0);
	bool		fast = PG_GETARG_BOOL(1);
	char	   *backupidstr;
	XLogRecPtr	startpoint;
	SessionBackupState status = get_backup_status();
	MemoryContext oldcontext;

	backupidstr = text_to_cstring(backupid);

	if (status == SESSION_BACKUP_RUNNING)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("a backup is already in progress in this session")));

	/*
	 * Label file and tablespace map file need to be long-lived, since they
	 * are read in pg_backup_stop.
	 */
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);
	label_file = makeStringInfo();
	tblspc_map_file = makeStringInfo();
	MemoryContextSwitchTo(oldcontext);

	register_persistent_abort_backup_handler();

	startpoint = do_pg_backup_start(backupidstr, fast, NULL, label_file,
									NULL, tblspc_map_file);

	PG_RETURN_LSN(startpoint);
}


/*
 * pg_backup_stop: finish taking an on-line backup.
 *
 * The first parameter (variable 'waitforarchive'), which is optional,
 * allows the user to choose if they want to wait for the WAL to be archived
 * or if we should just return as soon as the WAL record is written.
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_backup_stop(PG_FUNCTION_ARGS)
{
#define PG_STOP_BACKUP_V2_COLS 3
	TupleDesc	tupdesc;
	Datum		values[PG_STOP_BACKUP_V2_COLS];
	bool		nulls[PG_STOP_BACKUP_V2_COLS];

	bool		waitforarchive = PG_GETARG_BOOL(0);
	XLogRecPtr	stoppoint;
	SessionBackupState status = get_backup_status();

	/* Initialize attributes information in the tuple descriptor */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	MemSet(values, 0, sizeof(values));
	MemSet(nulls, 0, sizeof(nulls));

	if (status != SESSION_BACKUP_RUNNING)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("backup is not in progress"),
				 errhint("Did you call pg_backup_start()?")));

	/*
	 * Stop the backup. Return a copy of the backup label and tablespace map
	 * so they can be written to disk by the caller.
	 */
	stoppoint = do_pg_backup_stop(label_file->data, waitforarchive, NULL);

	values[0] = LSNGetDatum(stoppoint);
	values[1] = CStringGetTextDatum(label_file->data);
	values[2] = CStringGetTextDatum(tblspc_map_file->data);

	/* Free structures allocated in TopMemoryContext */
	pfree(label_file->data);
	pfree(label_file);
	label_file = NULL;
	pfree(tblspc_map_file->data);
	pfree(tblspc_map_file);
	tblspc_map_file = NULL;

	/* Returns the record as Datum */
	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

/*
 * pg_switch_wal: switch to next xlog file
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_switch_wal(PG_FUNCTION_ARGS)
{
	XLogRecPtr	switchpoint;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("WAL control functions cannot be executed during recovery.")));

	switchpoint = RequestXLogSwitch(false);

	/*
	 * As a convenience, return the WAL location of the switch record
	 */
	PG_RETURN_LSN(switchpoint);
}

/*
 * pg_create_restore_point: a named point for restore
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_create_restore_point(PG_FUNCTION_ARGS)
{
	text	   *restore_name = PG_GETARG_TEXT_PP(0);
	char	   *restore_name_str;
	XLogRecPtr	restorepoint;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("WAL control functions cannot be executed during recovery.")));

	if (!XLogIsNeeded())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("WAL level not sufficient for creating a restore point"),
				 errhint("wal_level must be set to \"replica\" or \"logical\" at server start.")));

	restore_name_str = text_to_cstring(restore_name);

	if (strlen(restore_name_str) >= MAXFNAMELEN)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("value too long for restore point (maximum %d characters)", MAXFNAMELEN - 1)));

	restorepoint = XLogRestorePoint(restore_name_str);

	/*
	 * As a convenience, return the WAL location of the restore point record
	 */
	PG_RETURN_LSN(restorepoint);
}

/*
 * Report the current WAL write location (same format as pg_backup_start etc)
 *
 * This is useful for determining how much of WAL is visible to an external
 * archiving process.  Note that the data before this point is written out
 * to the kernel, but is not necessarily synced to disk.
 */
Datum
pg_current_wal_lsn(PG_FUNCTION_ARGS)
{
	XLogRecPtr	current_recptr;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("WAL control functions cannot be executed during recovery.")));

	current_recptr = GetXLogWriteRecPtr();

	PG_RETURN_LSN(current_recptr);
}

/*
 * Report the current WAL insert location (same format as pg_backup_start etc)
 *
 * This function is mostly for debugging purposes.
 */
Datum
pg_current_wal_insert_lsn(PG_FUNCTION_ARGS)
{
	XLogRecPtr	current_recptr;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("WAL control functions cannot be executed during recovery.")));

	current_recptr = GetXLogInsertRecPtr();

	PG_RETURN_LSN(current_recptr);
}

/*
 * Report the current WAL flush location (same format as pg_backup_start etc)
 *
 * This function is mostly for debugging purposes.
 */
Datum
pg_current_wal_flush_lsn(PG_FUNCTION_ARGS)
{
	XLogRecPtr	current_recptr;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("WAL control functions cannot be executed during recovery.")));

	current_recptr = GetFlushRecPtr(NULL);

	PG_RETURN_LSN(current_recptr);
}

/*
 * Report the last WAL receive location (same format as pg_backup_start etc)
 *
 * This is useful for determining how much of WAL is guaranteed to be received
 * and synced to disk by walreceiver.
 */
Datum
pg_last_wal_receive_lsn(PG_FUNCTION_ARGS)
{
	XLogRecPtr	recptr;

	recptr = GetWalRcvFlushRecPtr(NULL, NULL);

	if (recptr == 0)
		PG_RETURN_NULL();

	PG_RETURN_LSN(recptr);
}

/*
 * Report the last WAL replay location (same format as pg_backup_start etc)
 *
 * This is useful for determining how much of WAL is visible to read-only
 * connections during recovery.
 */
Datum
pg_last_wal_replay_lsn(PG_FUNCTION_ARGS)
{
	XLogRecPtr	recptr;

	recptr = GetXLogReplayRecPtr(NULL);

	if (recptr == 0)
		PG_RETURN_NULL();

	PG_RETURN_LSN(recptr);
}

/*
 * Compute an xlog file name and decimal byte offset given a WAL location,
 * such as is returned by pg_backup_stop() or pg_switch_wal().
 *
 * Note that a location exactly at a segment boundary is taken to be in
 * the previous segment.  This is usually the right thing, since the
 * expected usage is to determine which xlog file(s) are ready to archive.
 */
Datum
pg_walfile_name_offset(PG_FUNCTION_ARGS)
{
	XLogSegNo	xlogsegno;
	uint32		xrecoff;
	XLogRecPtr	locationpoint = PG_GETARG_LSN(0);
	char		xlogfilename[MAXFNAMELEN];
	Datum		values[2];
	bool		isnull[2];
	TupleDesc	resultTupleDesc;
	HeapTuple	resultHeapTuple;
	Datum		result;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("%s cannot be executed during recovery.",
						 "pg_walfile_name_offset()")));

	/*
	 * Construct a tuple descriptor for the result row.  This must match this
	 * function's pg_proc entry!
	 */
	resultTupleDesc = CreateTemplateTupleDesc(2);
	TupleDescInitEntry(resultTupleDesc, (AttrNumber) 1, "file_name",
					   TEXTOID, -1, 0);
	TupleDescInitEntry(resultTupleDesc, (AttrNumber) 2, "file_offset",
					   INT4OID, -1, 0);

	resultTupleDesc = BlessTupleDesc(resultTupleDesc);

	/*
	 * xlogfilename
	 */
	XLByteToPrevSeg(locationpoint, xlogsegno, wal_segment_size);
	XLogFileName(xlogfilename, GetWALInsertionTimeLine(), xlogsegno,
				 wal_segment_size);

	values[0] = CStringGetTextDatum(xlogfilename);
	isnull[0] = false;

	/*
	 * offset
	 */
	xrecoff = XLogSegmentOffset(locationpoint, wal_segment_size);

	values[1] = UInt32GetDatum(xrecoff);
	isnull[1] = false;

	/*
	 * Tuple jam: Having first prepared your Datums, then squash together
	 */
	resultHeapTuple = heap_form_tuple(resultTupleDesc, values, isnull);

	result = HeapTupleGetDatum(resultHeapTuple);

	PG_RETURN_DATUM(result);
}

/*
 * Compute an xlog file name given a WAL location,
 * such as is returned by pg_backup_stop() or pg_switch_wal().
 */
Datum
pg_walfile_name(PG_FUNCTION_ARGS)
{
	XLogSegNo	xlogsegno;
	XLogRecPtr	locationpoint = PG_GETARG_LSN(0);
	char		xlogfilename[MAXFNAMELEN];

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("%s cannot be executed during recovery.",
						 "pg_walfile_name()")));

	XLByteToPrevSeg(locationpoint, xlogsegno, wal_segment_size);
	XLogFileName(xlogfilename, GetWALInsertionTimeLine(), xlogsegno,
				 wal_segment_size);

	PG_RETURN_TEXT_P(cstring_to_text(xlogfilename));
}

/*
 * pg_wal_replay_pause - Request to pause recovery
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_wal_replay_pause(PG_FUNCTION_ARGS)
{
	if (!RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is not in progress"),
				 errhint("Recovery control functions can only be executed during recovery.")));

	if (PromoteIsTriggered())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("standby promotion is ongoing"),
				 errhint("%s cannot be executed after promotion is triggered.",
						 "pg_wal_replay_pause()")));

	SetRecoveryPause(true);

	/* wake up the recovery process so that it can process the pause request */
	WakeupRecovery();

	PG_RETURN_VOID();
}

/*
 * pg_wal_replay_resume - resume recovery now
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_wal_replay_resume(PG_FUNCTION_ARGS)
{
	if (!RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is not in progress"),
				 errhint("Recovery control functions can only be executed during recovery.")));

	if (PromoteIsTriggered())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("standby promotion is ongoing"),
				 errhint("%s cannot be executed after promotion is triggered.",
						 "pg_wal_replay_resume()")));

	SetRecoveryPause(false);

	PG_RETURN_VOID();
}

/*
 * pg_is_wal_replay_paused
 */
Datum
pg_is_wal_replay_paused(PG_FUNCTION_ARGS)
{
	if (!RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is not in progress"),
				 errhint("Recovery control functions can only be executed during recovery.")));

	PG_RETURN_BOOL(GetRecoveryPauseState() != RECOVERY_NOT_PAUSED);
}

/*
 * pg_get_wal_replay_pause_state - Returns the recovery pause state.
 *
 * Returned values:
 *
 * 'not paused' - if pause is not requested
 * 'pause requested' - if pause is requested but recovery is not yet paused
 * 'paused' - if recovery is paused
 */
Datum
pg_get_wal_replay_pause_state(PG_FUNCTION_ARGS)
{
	char	   *statestr = NULL;

	if (!RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is not in progress"),
				 errhint("Recovery control functions can only be executed during recovery.")));

	/* get the recovery pause state */
	switch (GetRecoveryPauseState())
	{
		case RECOVERY_NOT_PAUSED:
			statestr = "not paused";
			break;
		case RECOVERY_PAUSE_REQUESTED:
			statestr = "pause requested";
			break;
		case RECOVERY_PAUSED:
			statestr = "paused";
			break;
	}

	Assert(statestr != NULL);
	PG_RETURN_TEXT_P(cstring_to_text(statestr));
}

/*
 * Returns timestamp of latest processed commit/abort record.
 *
 * When the server has been started normally without recovery the function
 * returns NULL.
 */
Datum
pg_last_xact_replay_timestamp(PG_FUNCTION_ARGS)
{
	TimestampTz xtime;

	xtime = GetLatestXTime();
	if (xtime == 0)
		PG_RETURN_NULL();

	PG_RETURN_TIMESTAMPTZ(xtime);
}

/*
 * Returns bool with current recovery mode, a global state.
 */
Datum
pg_is_in_recovery(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(RecoveryInProgress());
}

/*
 * Compute the difference in bytes between two WAL locations.
 */
Datum
pg_wal_lsn_diff(PG_FUNCTION_ARGS)
{
	Datum		result;

	result = DirectFunctionCall2(pg_lsn_mi,
								 PG_GETARG_DATUM(0),
								 PG_GETARG_DATUM(1));

	PG_RETURN_DATUM(result);
}

/*
 * Promotes a standby server.
 *
 * A result of "true" means that promotion has been completed if "wait" is
 * "true", or initiated if "wait" is false.
 */
Datum
pg_promote(PG_FUNCTION_ARGS)
{
	bool		wait = PG_GETARG_BOOL(0);
	int			wait_seconds = PG_GETARG_INT32(1);
	FILE	   *promote_file;
	int			i;

	if (!RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is not in progress"),
				 errhint("Recovery control functions can only be executed during recovery.")));

	if (wait_seconds <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("\"wait_seconds\" must not be negative or zero")));

	/* create the promote signal file */
	promote_file = AllocateFile(PROMOTE_SIGNAL_FILE, "w");
	if (!promote_file)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m",
						PROMOTE_SIGNAL_FILE)));

	if (FreeFile(promote_file))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write file \"%s\": %m",
						PROMOTE_SIGNAL_FILE)));

	/* signal the postmaster */
	if (kill(PostmasterPid, SIGUSR1) != 0)
	{
		ereport(WARNING,
				(errmsg("failed to send signal to postmaster: %m")));
		(void) unlink(PROMOTE_SIGNAL_FILE);
		PG_RETURN_BOOL(false);
	}

	/* return immediately if waiting was not requested */
	if (!wait)
		PG_RETURN_BOOL(true);

	/* wait for the amount of time wanted until promotion */
#define WAITS_PER_SECOND 10
	for (i = 0; i < WAITS_PER_SECOND * wait_seconds; i++)
	{
		int			rc;

		ResetLatch(MyLatch);

		if (!RecoveryInProgress())
			PG_RETURN_BOOL(true);

		CHECK_FOR_INTERRUPTS();

		rc = WaitLatch(MyLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   1000L / WAITS_PER_SECOND,
					   WAIT_EVENT_PROMOTE);

		/*
		 * Emergency bailout if postmaster has died.  This is to avoid the
		 * necessity for manual cleanup of all postmaster children.
		 */
		if (rc & WL_POSTMASTER_DEATH)
			PG_RETURN_BOOL(false);
	}

	ereport(WARNING,
			(errmsg_plural("server did not promote within %d second",
						   "server did not promote within %d seconds",
						   wait_seconds,
						   wait_seconds)));
	PG_RETURN_BOOL(false);
}
