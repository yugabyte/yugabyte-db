/* ----------
 * wait_event.c
 *	  Wait event reporting infrastructure.
 *
 * Copyright (c) 2001-2022, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/activity/wait_event.c
 *
 * NOTES
 *
 * To make pgstat_report_wait_start() and pgstat_report_wait_end() as
 * lightweight as possible, they do not check if shared memory (MyProc
 * specifically, where the wait event is stored) is already available. Instead
 * we initially set my_wait_event_info to a process local variable, which then
 * is redirected to shared memory using pgstat_set_wait_event_storage(). For
 * the same reason pgstat_track_activities is not checked - the check adds
 * more work than it saves.
 *
 * ----------
 */
#include "postgres.h"

#include "storage/lmgr.h"		/* for GetLockNameFromTagType */
#include "storage/lwlock.h"		/* for GetLWLockIdentifier */
#include "utils/wait_event.h"

/* YB includes */
#include "funcapi.h"
#include "nodes/execnodes.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "yb/yql/pggate/util/ybc_util.h"

#define YB_WAIT_EVENT_DESC_COLS_V1 4
#define YB_WAIT_EVENT_DESC_COLS_V2 5

static const char *pgstat_get_wait_activity(WaitEventActivity w);
static const char *pgstat_get_wait_client(WaitEventClient w);
static const char *pgstat_get_wait_ipc(WaitEventIPC w);
static const char *pgstat_get_wait_timeout(WaitEventTimeout w);
static const char *pgstat_get_wait_io(WaitEventIO w);

static const char *yb_get_wait_event_desc(uint32 wait_event_info);
static const char *yb_get_wait_activity_desc(WaitEventActivity w);
static const char *yb_get_wait_client_desc(WaitEventClient w);
static const char *yb_get_wait_ipc_desc(WaitEventIPC w);
static const char *yb_get_wait_timeout_desc(WaitEventTimeout w);
static const char *yb_get_wait_io_desc(WaitEventIO w);
static const char *yb_get_wait_lock_desc(LockTagType lock_tag);
static const char *yb_get_wait_lwlock_desc(BuiltinTrancheIds tranche_id);

static uint32 local_my_wait_event_info;
uint32	   *my_wait_event_info = &local_my_wait_event_info;

static YbcWaitEventInfo yb_local_my_wait_event_info;
YbcWaitEventInfoPtr yb_my_wait_event_info = {
	&yb_local_my_wait_event_info.wait_event,
	&yb_local_my_wait_event_info.rpc_code,
};
static const char *yb_not_applicable =
"Inherited from PostgreSQL. Check "
"https://www.postgresql.org/docs/current/monitoring-stats.html "
"for description.";


/*
 * Configure wait event reporting to report wait events to *wait_event_info.
 * *wait_event_info needs to be valid until pgstat_reset_wait_event_storage()
 * is called.
 *
 * Expected to be called during backend startup, to point my_wait_event_info
 * into shared memory.
 */
void
pgstat_set_wait_event_storage(uint32 *wait_event_info)
{
	my_wait_event_info = wait_event_info;
}

/*
 * Reset wait event storage location.
 *
 * Expected to be called during backend shutdown, before the location set up
 * pgstat_set_wait_event_storage() becomes invalid.
 */
void
pgstat_reset_wait_event_storage(void)
{
	my_wait_event_info = &local_my_wait_event_info;
}

/*
 * Configure wait event reporting to report wait events to MyProc->wait_event_info,
 * and Pggate RPC enum reporting to report Pggate RPC enums to MyProc->yb_rpc_code
 * MyProc->wait_event_info and MyProc->yb_rpc_code needs to be valid until
 * yb_pgstat_reset_wait_event_storage() is called.
 *
 * Expected to be called during backend startup, to point my_wait_event_info
 * into shared memory.
 */
void
yb_pgstat_set_wait_event_storage(PGPROC *proc)
{
	yb_my_wait_event_info = (YbcWaitEventInfoPtr)
	{
		&proc->wait_event_info,
			&proc->yb_rpc_code,
	};

	/* pgstat_report_wait_start updates my_wait_event_info */
	my_wait_event_info = &proc->wait_event_info;
}

/*
 * Reset RPC enum storage location.
 *
 * Expected to be called during backend shutdown, before the location set up
 * yb_pgstat_set_wait_event_storage() becomes invalid.
 */
void
yb_pgstat_reset_wait_event_storage(void)
{
	yb_my_wait_event_info = (YbcWaitEventInfoPtr)
	{
		&yb_local_my_wait_event_info.wait_event,
			&yb_local_my_wait_event_info.rpc_code,
	};

	my_wait_event_info = &local_my_wait_event_info;
}

/* ----------
 * pgstat_get_wait_event_type() -
 *
 *	Return a string representing the current wait event type, backend is
 *	waiting on.
 */
const char *
pgstat_get_wait_event_type(uint32 wait_event_info)
{
	uint32		classId;
	const char *event_type;

	/* report process as not waiting. */
	if (wait_event_info == 0)
	{
		if (yb_enable_ash)
			return "Cpu";
		return NULL;
	}

	classId = wait_event_info & 0xFF000000;

	switch (classId)
	{
		case PG_WAIT_LWLOCK:
			event_type = "LWLock";
			break;
		case PG_WAIT_LOCK:
			event_type = "Lock";
			break;
		case PG_WAIT_BUFFER_PIN:
			event_type = "BufferPin";
			break;
		case PG_WAIT_ACTIVITY:
			event_type = "Activity";
			break;
		case PG_WAIT_CLIENT:
			event_type = "Client";
			break;
		case PG_WAIT_EXTENSION:
			event_type = "Extension";
			break;
		case PG_WAIT_IPC:
			event_type = "IPC";
			break;
		case PG_WAIT_TIMEOUT:
			event_type = "Timeout";
			break;
		case PG_WAIT_IO:
			event_type = "IO";
			break;
		default:
			event_type = "???";
			if (yb_enable_ash)
				event_type = YBCGetWaitEventType(wait_event_info);
			break;
	}

	return event_type;
}

/* ----------
 * pgstat_get_wait_event() -
 *
 *	Return a string representing the current wait event, backend is
 *	waiting on.
 */
const char *
pgstat_get_wait_event(uint32 wait_event_info)
{
	uint32		classId;
	uint16		eventId;
	const char *event_name;

	/* report process as not waiting. */
	if (wait_event_info == 0)
	{
		if (yb_enable_ash)
			return "OnCpu_Active";
		return NULL;
	}

	classId = wait_event_info & 0xFF000000;
	eventId = wait_event_info & 0x0000FFFF;

	switch (classId)
	{
		case PG_WAIT_LWLOCK:
			event_name = GetLWLockIdentifier(classId, eventId);
			break;
		case PG_WAIT_LOCK:
			event_name = GetLockNameFromTagType(eventId);
			break;
		case PG_WAIT_BUFFER_PIN:
			event_name = "BufferPin";
			break;
		case PG_WAIT_ACTIVITY:
			{
				WaitEventActivity w = (WaitEventActivity) wait_event_info;

				event_name = pgstat_get_wait_activity(w);
				break;
			}
		case PG_WAIT_CLIENT:
			{
				WaitEventClient w = (WaitEventClient) wait_event_info;

				event_name = pgstat_get_wait_client(w);
				break;
			}
		case PG_WAIT_EXTENSION:
			event_name = "Extension";
			break;
		case PG_WAIT_IPC:
			{
				WaitEventIPC w = (WaitEventIPC) wait_event_info;

				event_name = pgstat_get_wait_ipc(w);
				break;
			}
		case PG_WAIT_TIMEOUT:
			{
				WaitEventTimeout w = (WaitEventTimeout) wait_event_info;

				event_name = pgstat_get_wait_timeout(w);
				break;
			}
		case PG_WAIT_IO:
			{
				WaitEventIO w = (WaitEventIO) wait_event_info;

				event_name = pgstat_get_wait_io(w);
				break;
			}
		default:
			event_name = "unknown wait event";
			if (yb_enable_ash)
				event_name = YBCGetWaitEventName(wait_event_info);
			break;
	}

	return event_name;
}

/* ----------
 * pgstat_get_wait_activity() -
 *
 * Convert WaitEventActivity to string.
 * ----------
 */
static const char *
pgstat_get_wait_activity(WaitEventActivity w)
{
	const char *event_name = "unknown wait event";

	switch (w)
	{
		case WAIT_EVENT_ARCHIVER_MAIN:
			event_name = "ArchiverMain";
			break;
		case WAIT_EVENT_AUTOVACUUM_MAIN:
			event_name = "AutoVacuumMain";
			break;
		case WAIT_EVENT_BGWRITER_HIBERNATE:
			event_name = "BgWriterHibernate";
			break;
		case WAIT_EVENT_BGWRITER_MAIN:
			event_name = "BgWriterMain";
			break;
		case WAIT_EVENT_CHECKPOINTER_MAIN:
			event_name = "CheckpointerMain";
			break;
		case WAIT_EVENT_LOGICAL_APPLY_MAIN:
			event_name = "LogicalApplyMain";
			break;
		case WAIT_EVENT_LOGICAL_LAUNCHER_MAIN:
			event_name = "LogicalLauncherMain";
			break;
		case WAIT_EVENT_RECOVERY_WAL_STREAM:
			event_name = "RecoveryWalStream";
			break;
		case WAIT_EVENT_SYSLOGGER_MAIN:
			event_name = "SysLoggerMain";
			break;
		case WAIT_EVENT_WAL_RECEIVER_MAIN:
			event_name = "WalReceiverMain";
			break;
		case WAIT_EVENT_WAL_SENDER_MAIN:
			event_name = "WalSenderMain";
			break;
		case WAIT_EVENT_WAL_WRITER_MAIN:
			event_name = "WalWriterMain";
			break;
		case WAIT_EVENT_YB_QUERY_DIAGNOSTICS_MAIN:
			event_name = "QueryDiagnosticsMain";
			break;
		case WAIT_EVENT_YB_ASH_MAIN:
			event_name = "YbAshMain";
			break;
		case WAIT_EVENT_YB_IDLE_SLEEP:
			event_name = "YbIdleSleep";
			break;
		case WAIT_EVENT_YB_ACTIVITY_END:
			Assert(false);		/* should not be used to instrument */
			break;
			/* no default case, so that compiler will warn */
	}

	return event_name;
}

/* ----------
 * pgstat_get_wait_client() -
 *
 * Convert WaitEventClient to string.
 * ----------
 */
static const char *
pgstat_get_wait_client(WaitEventClient w)
{
	const char *event_name = "unknown wait event";

	switch (w)
	{
		case WAIT_EVENT_CLIENT_READ:
			event_name = "ClientRead";
			break;
		case WAIT_EVENT_CLIENT_WRITE:
			event_name = "ClientWrite";
			break;
		case WAIT_EVENT_GSS_OPEN_SERVER:
			event_name = "GSSOpenServer";
			break;
		case WAIT_EVENT_LIBPQWALRECEIVER_CONNECT:
			event_name = "LibPQWalReceiverConnect";
			break;
		case WAIT_EVENT_LIBPQWALRECEIVER_RECEIVE:
			event_name = "LibPQWalReceiverReceive";
			break;
		case WAIT_EVENT_SSL_OPEN_SERVER:
			event_name = "SSLOpenServer";
			break;
		case WAIT_EVENT_WAL_SENDER_WAIT_WAL:
			event_name = "WalSenderWaitForWAL";
			break;
		case WAIT_EVENT_WAL_SENDER_WRITE_DATA:
			event_name = "WalSenderWriteData";
			break;
		case WAIT_EVENT_YB_CLIENT_END:
			Assert(false);		/* should not be used to instrument */
			break;
			/* no default case, so that compiler will warn */
	}

	return event_name;
}

/* ----------
 * pgstat_get_wait_ipc() -
 *
 * Convert WaitEventIPC to string.
 * ----------
 */
static const char *
pgstat_get_wait_ipc(WaitEventIPC w)
{
	const char *event_name = "unknown wait event";

	switch (w)
	{
		case WAIT_EVENT_APPEND_READY:
			event_name = "AppendReady";
			break;
		case WAIT_EVENT_ARCHIVE_CLEANUP_COMMAND:
			event_name = "ArchiveCleanupCommand";
			break;
		case WAIT_EVENT_ARCHIVE_COMMAND:
			event_name = "ArchiveCommand";
			break;
		case WAIT_EVENT_BACKEND_TERMINATION:
			event_name = "BackendTermination";
			break;
		case WAIT_EVENT_BACKUP_WAIT_WAL_ARCHIVE:
			event_name = "BackupWaitWalArchive";
			break;
		case WAIT_EVENT_BGWORKER_SHUTDOWN:
			event_name = "BgWorkerShutdown";
			break;
		case WAIT_EVENT_BGWORKER_STARTUP:
			event_name = "BgWorkerStartup";
			break;
		case WAIT_EVENT_BTREE_PAGE:
			event_name = "BtreePage";
			break;
		case WAIT_EVENT_BUFFER_IO:
			event_name = "BufferIO";
			break;
		case WAIT_EVENT_CHECKPOINT_DONE:
			event_name = "CheckpointDone";
			break;
		case WAIT_EVENT_CHECKPOINT_START:
			event_name = "CheckpointStart";
			break;
		case WAIT_EVENT_EXECUTE_GATHER:
			event_name = "ExecuteGather";
			break;
		case WAIT_EVENT_HASH_BATCH_ALLOCATE:
			event_name = "HashBatchAllocate";
			break;
		case WAIT_EVENT_HASH_BATCH_ELECT:
			event_name = "HashBatchElect";
			break;
		case WAIT_EVENT_HASH_BATCH_LOAD:
			event_name = "HashBatchLoad";
			break;
		case WAIT_EVENT_HASH_BUILD_ALLOCATE:
			event_name = "HashBuildAllocate";
			break;
		case WAIT_EVENT_HASH_BUILD_ELECT:
			event_name = "HashBuildElect";
			break;
		case WAIT_EVENT_HASH_BUILD_HASH_INNER:
			event_name = "HashBuildHashInner";
			break;
		case WAIT_EVENT_HASH_BUILD_HASH_OUTER:
			event_name = "HashBuildHashOuter";
			break;
		case WAIT_EVENT_HASH_GROW_BATCHES_ALLOCATE:
			event_name = "HashGrowBatchesAllocate";
			break;
		case WAIT_EVENT_HASH_GROW_BATCHES_DECIDE:
			event_name = "HashGrowBatchesDecide";
			break;
		case WAIT_EVENT_HASH_GROW_BATCHES_ELECT:
			event_name = "HashGrowBatchesElect";
			break;
		case WAIT_EVENT_HASH_GROW_BATCHES_FINISH:
			event_name = "HashGrowBatchesFinish";
			break;
		case WAIT_EVENT_HASH_GROW_BATCHES_REPARTITION:
			event_name = "HashGrowBatchesRepartition";
			break;
		case WAIT_EVENT_HASH_GROW_BUCKETS_ALLOCATE:
			event_name = "HashGrowBucketsAllocate";
			break;
		case WAIT_EVENT_HASH_GROW_BUCKETS_ELECT:
			event_name = "HashGrowBucketsElect";
			break;
		case WAIT_EVENT_HASH_GROW_BUCKETS_REINSERT:
			event_name = "HashGrowBucketsReinsert";
			break;
		case WAIT_EVENT_LOGICAL_SYNC_DATA:
			event_name = "LogicalSyncData";
			break;
		case WAIT_EVENT_LOGICAL_SYNC_STATE_CHANGE:
			event_name = "LogicalSyncStateChange";
			break;
		case WAIT_EVENT_MQ_INTERNAL:
			event_name = "MessageQueueInternal";
			break;
		case WAIT_EVENT_MQ_PUT_MESSAGE:
			event_name = "MessageQueuePutMessage";
			break;
		case WAIT_EVENT_MQ_RECEIVE:
			event_name = "MessageQueueReceive";
			break;
		case WAIT_EVENT_MQ_SEND:
			event_name = "MessageQueueSend";
			break;
		case WAIT_EVENT_PARALLEL_BITMAP_SCAN:
			event_name = "ParallelBitmapScan";
			break;
		case WAIT_EVENT_PARALLEL_CREATE_INDEX_SCAN:
			event_name = "ParallelCreateIndexScan";
			break;
		case WAIT_EVENT_PARALLEL_FINISH:
			event_name = "ParallelFinish";
			break;
		case WAIT_EVENT_PROCARRAY_GROUP_UPDATE:
			event_name = "ProcArrayGroupUpdate";
			break;
		case WAIT_EVENT_PROC_SIGNAL_BARRIER:
			event_name = "ProcSignalBarrier";
			break;
		case WAIT_EVENT_PROMOTE:
			event_name = "Promote";
			break;
		case WAIT_EVENT_RECOVERY_CONFLICT_SNAPSHOT:
			event_name = "RecoveryConflictSnapshot";
			break;
		case WAIT_EVENT_RECOVERY_CONFLICT_TABLESPACE:
			event_name = "RecoveryConflictTablespace";
			break;
		case WAIT_EVENT_RECOVERY_END_COMMAND:
			event_name = "RecoveryEndCommand";
			break;
		case WAIT_EVENT_RECOVERY_PAUSE:
			event_name = "RecoveryPause";
			break;
		case WAIT_EVENT_REPLICATION_ORIGIN_DROP:
			event_name = "ReplicationOriginDrop";
			break;
		case WAIT_EVENT_REPLICATION_SLOT_DROP:
			event_name = "ReplicationSlotDrop";
			break;
		case WAIT_EVENT_RESTORE_COMMAND:
			event_name = "RestoreCommand";
			break;
		case WAIT_EVENT_SAFE_SNAPSHOT:
			event_name = "SafeSnapshot";
			break;
		case WAIT_EVENT_SYNC_REP:
			event_name = "SyncRep";
			break;
		case WAIT_EVENT_WAL_RECEIVER_EXIT:
			event_name = "WalReceiverExit";
			break;
		case WAIT_EVENT_WAL_RECEIVER_WAIT_START:
			event_name = "WalReceiverWaitStart";
			break;
		case WAIT_EVENT_XACT_GROUP_UPDATE:
			event_name = "XactGroupUpdate";
			break;
		case WAIT_EVENT_YB_PARALLEL_SCAN_EMPTY:
			event_name = "YBParallelScanEmpty";
			break;
		case WAIT_EVENT_YB_IPC_END:
			Assert(false);		/* should not be used to instrument */
			break;
			/* no default case, so that compiler will warn */
	}

	return event_name;
}

/* ----------
 * pgstat_get_wait_timeout() -
 *
 * Convert WaitEventTimeout to string.
 * ----------
 */
static const char *
pgstat_get_wait_timeout(WaitEventTimeout w)
{
	const char *event_name = "unknown wait event";

	switch (w)
	{
		case WAIT_EVENT_BASE_BACKUP_THROTTLE:
			event_name = "BaseBackupThrottle";
			break;
		case WAIT_EVENT_CHECKPOINT_WRITE_DELAY:
			event_name = "CheckpointWriteDelay";
			break;
		case WAIT_EVENT_PG_SLEEP:
			event_name = "PgSleep";
			break;
		case WAIT_EVENT_RECOVERY_APPLY_DELAY:
			event_name = "RecoveryApplyDelay";
			break;
		case WAIT_EVENT_RECOVERY_RETRIEVE_RETRY_INTERVAL:
			event_name = "RecoveryRetrieveRetryInterval";
			break;
		case WAIT_EVENT_REGISTER_SYNC_REQUEST:
			event_name = "RegisterSyncRequest";
			break;
		case WAIT_EVENT_VACUUM_DELAY:
			event_name = "VacuumDelay";
			break;
		case WAIT_EVENT_VACUUM_TRUNCATE:
			event_name = "VacuumTruncate";
			break;
		case WAIT_EVENT_YB_TXN_CONFLICT_BACKOFF:
			event_name = "YBTxnConflictBackoff";
			break;
		case WAIT_EVENT_YB_TIMEOUT_END:
			Assert(false);		/* should not be used to instrument */
			break;
			/* no default case, so that compiler will warn */
	}

	return event_name;
}

/* ----------
 * pgstat_get_wait_io() -
 *
 * Convert WaitEventIO to string.
 * ----------
 */
static const char *
pgstat_get_wait_io(WaitEventIO w)
{
	const char *event_name = "unknown wait event";

	switch (w)
	{
		case WAIT_EVENT_BASEBACKUP_READ:
			event_name = "BaseBackupRead";
			break;
		case WAIT_EVENT_BASEBACKUP_SYNC:
			event_name = "BaseBackupSync";
			break;
		case WAIT_EVENT_BASEBACKUP_WRITE:
			event_name = "BaseBackupWrite";
			break;
		case WAIT_EVENT_BUFFILE_READ:
			event_name = "BufFileRead";
			break;
		case WAIT_EVENT_BUFFILE_WRITE:
			event_name = "BufFileWrite";
			break;
		case WAIT_EVENT_BUFFILE_TRUNCATE:
			event_name = "BufFileTruncate";
			break;
		case WAIT_EVENT_CONTROL_FILE_READ:
			event_name = "ControlFileRead";
			break;
		case WAIT_EVENT_CONTROL_FILE_SYNC:
			event_name = "ControlFileSync";
			break;
		case WAIT_EVENT_CONTROL_FILE_SYNC_UPDATE:
			event_name = "ControlFileSyncUpdate";
			break;
		case WAIT_EVENT_CONTROL_FILE_WRITE:
			event_name = "ControlFileWrite";
			break;
		case WAIT_EVENT_CONTROL_FILE_WRITE_UPDATE:
			event_name = "ControlFileWriteUpdate";
			break;
		case WAIT_EVENT_COPY_FILE_READ:
			event_name = "CopyFileRead";
			break;
		case WAIT_EVENT_COPY_FILE_WRITE:
			event_name = "CopyFileWrite";
			break;
		case WAIT_EVENT_DATA_FILE_EXTEND:
			event_name = "DataFileExtend";
			break;
		case WAIT_EVENT_DATA_FILE_FLUSH:
			event_name = "DataFileFlush";
			break;
		case WAIT_EVENT_DATA_FILE_IMMEDIATE_SYNC:
			event_name = "DataFileImmediateSync";
			break;
		case WAIT_EVENT_DATA_FILE_PREFETCH:
			event_name = "DataFilePrefetch";
			break;
		case WAIT_EVENT_DATA_FILE_READ:
			event_name = "DataFileRead";
			break;
		case WAIT_EVENT_DATA_FILE_SYNC:
			event_name = "DataFileSync";
			break;
		case WAIT_EVENT_DATA_FILE_TRUNCATE:
			event_name = "DataFileTruncate";
			break;
		case WAIT_EVENT_DATA_FILE_WRITE:
			event_name = "DataFileWrite";
			break;
		case WAIT_EVENT_DSM_FILL_ZERO_WRITE:
			event_name = "DSMFillZeroWrite";
			break;
		case WAIT_EVENT_LOCK_FILE_ADDTODATADIR_READ:
			event_name = "LockFileAddToDataDirRead";
			break;
		case WAIT_EVENT_LOCK_FILE_ADDTODATADIR_SYNC:
			event_name = "LockFileAddToDataDirSync";
			break;
		case WAIT_EVENT_LOCK_FILE_ADDTODATADIR_WRITE:
			event_name = "LockFileAddToDataDirWrite";
			break;
		case WAIT_EVENT_LOCK_FILE_CREATE_READ:
			event_name = "LockFileCreateRead";
			break;
		case WAIT_EVENT_LOCK_FILE_CREATE_SYNC:
			event_name = "LockFileCreateSync";
			break;
		case WAIT_EVENT_LOCK_FILE_CREATE_WRITE:
			event_name = "LockFileCreateWrite";
			break;
		case WAIT_EVENT_LOCK_FILE_RECHECKDATADIR_READ:
			event_name = "LockFileReCheckDataDirRead";
			break;
		case WAIT_EVENT_LOGICAL_REWRITE_CHECKPOINT_SYNC:
			event_name = "LogicalRewriteCheckpointSync";
			break;
		case WAIT_EVENT_LOGICAL_REWRITE_MAPPING_SYNC:
			event_name = "LogicalRewriteMappingSync";
			break;
		case WAIT_EVENT_LOGICAL_REWRITE_MAPPING_WRITE:
			event_name = "LogicalRewriteMappingWrite";
			break;
		case WAIT_EVENT_LOGICAL_REWRITE_SYNC:
			event_name = "LogicalRewriteSync";
			break;
		case WAIT_EVENT_LOGICAL_REWRITE_TRUNCATE:
			event_name = "LogicalRewriteTruncate";
			break;
		case WAIT_EVENT_LOGICAL_REWRITE_WRITE:
			event_name = "LogicalRewriteWrite";
			break;
		case WAIT_EVENT_RELATION_MAP_READ:
			event_name = "RelationMapRead";
			break;
		case WAIT_EVENT_RELATION_MAP_SYNC:
			event_name = "RelationMapSync";
			break;
		case WAIT_EVENT_RELATION_MAP_WRITE:
			event_name = "RelationMapWrite";
			break;
		case WAIT_EVENT_REORDER_BUFFER_READ:
			event_name = "ReorderBufferRead";
			break;
		case WAIT_EVENT_REORDER_BUFFER_WRITE:
			event_name = "ReorderBufferWrite";
			break;
		case WAIT_EVENT_REORDER_LOGICAL_MAPPING_READ:
			event_name = "ReorderLogicalMappingRead";
			break;
		case WAIT_EVENT_REPLICATION_SLOT_READ:
			event_name = "ReplicationSlotRead";
			break;
		case WAIT_EVENT_REPLICATION_SLOT_RESTORE_SYNC:
			event_name = "ReplicationSlotRestoreSync";
			break;
		case WAIT_EVENT_REPLICATION_SLOT_SYNC:
			event_name = "ReplicationSlotSync";
			break;
		case WAIT_EVENT_REPLICATION_SLOT_WRITE:
			event_name = "ReplicationSlotWrite";
			break;
		case WAIT_EVENT_SLRU_FLUSH_SYNC:
			event_name = "SLRUFlushSync";
			break;
		case WAIT_EVENT_SLRU_READ:
			event_name = "SLRURead";
			break;
		case WAIT_EVENT_SLRU_SYNC:
			event_name = "SLRUSync";
			break;
		case WAIT_EVENT_SLRU_WRITE:
			event_name = "SLRUWrite";
			break;
		case WAIT_EVENT_SNAPBUILD_READ:
			event_name = "SnapbuildRead";
			break;
		case WAIT_EVENT_SNAPBUILD_SYNC:
			event_name = "SnapbuildSync";
			break;
		case WAIT_EVENT_SNAPBUILD_WRITE:
			event_name = "SnapbuildWrite";
			break;
		case WAIT_EVENT_TIMELINE_HISTORY_FILE_SYNC:
			event_name = "TimelineHistoryFileSync";
			break;
		case WAIT_EVENT_TIMELINE_HISTORY_FILE_WRITE:
			event_name = "TimelineHistoryFileWrite";
			break;
		case WAIT_EVENT_TIMELINE_HISTORY_READ:
			event_name = "TimelineHistoryRead";
			break;
		case WAIT_EVENT_TIMELINE_HISTORY_SYNC:
			event_name = "TimelineHistorySync";
			break;
		case WAIT_EVENT_TIMELINE_HISTORY_WRITE:
			event_name = "TimelineHistoryWrite";
			break;
		case WAIT_EVENT_TWOPHASE_FILE_READ:
			event_name = "TwophaseFileRead";
			break;
		case WAIT_EVENT_TWOPHASE_FILE_SYNC:
			event_name = "TwophaseFileSync";
			break;
		case WAIT_EVENT_TWOPHASE_FILE_WRITE:
			event_name = "TwophaseFileWrite";
			break;
		case WAIT_EVENT_VERSION_FILE_SYNC:
			event_name = "VersionFileSync";
			break;
		case WAIT_EVENT_VERSION_FILE_WRITE:
			event_name = "VersionFileWrite";
			break;
		case WAIT_EVENT_WALSENDER_TIMELINE_HISTORY_READ:
			event_name = "WALSenderTimelineHistoryRead";
			break;
		case WAIT_EVENT_WAL_BOOTSTRAP_SYNC:
			event_name = "WALBootstrapSync";
			break;
		case WAIT_EVENT_WAL_BOOTSTRAP_WRITE:
			event_name = "WALBootstrapWrite";
			break;
		case WAIT_EVENT_WAL_COPY_READ:
			event_name = "WALCopyRead";
			break;
		case WAIT_EVENT_WAL_COPY_SYNC:
			event_name = "WALCopySync";
			break;
		case WAIT_EVENT_WAL_COPY_WRITE:
			event_name = "WALCopyWrite";
			break;
		case WAIT_EVENT_WAL_INIT_SYNC:
			event_name = "WALInitSync";
			break;
		case WAIT_EVENT_WAL_INIT_WRITE:
			event_name = "WALInitWrite";
			break;
		case WAIT_EVENT_WAL_READ:
			event_name = "WALRead";
			break;
		case WAIT_EVENT_WAL_SYNC:
			event_name = "WALSync";
			break;
		case WAIT_EVENT_WAL_SYNC_METHOD_ASSIGN:
			event_name = "WALSyncMethodAssign";
			break;
		case WAIT_EVENT_WAL_WRITE:
			event_name = "WALWrite";
			break;

		case WAIT_EVENT_YB_COPY_COMMAND_STREAM_READ:
			event_name = "CopyCommandStreamRead";
			break;
		case WAIT_EVENT_YB_COPY_COMMAND_STREAM_WRITE:
			event_name = "CopyCommandStreamWrite";
			break;
		case WAIT_EVENT_YB_IO_END:
			Assert(false);		/* should not be used to instrument */
			break;
			/* no default case, so that compiler will warn */
	}

	return event_name;
}

static const char *
yb_get_wait_event_desc(uint32 wait_event_info)
{
	uint32		classId;
	uint16		eventId;
	const char *desc = yb_not_applicable;

	/* report process as not waiting. */
	if (wait_event_info == 0)
		return "A YSQL backend is doing CPU work.";

	classId = wait_event_info & 0xFF000000;
	eventId = wait_event_info & 0x0000FFFF;

	switch (classId)
	{
		case PG_WAIT_LWLOCK:
			if (eventId >= NUM_INDIVIDUAL_LWLOCKS)
				desc = yb_get_wait_lwlock_desc(eventId);
			break;
		case PG_WAIT_LOCK:
			desc = yb_get_wait_lock_desc(eventId);
			break;
		case PG_WAIT_ACTIVITY:
			desc = yb_get_wait_activity_desc(wait_event_info);
			break;
		case PG_WAIT_CLIENT:
			desc = yb_get_wait_client_desc(wait_event_info);
			break;
		case PG_WAIT_IPC:
			desc = yb_get_wait_ipc_desc(wait_event_info);
			break;
		case PG_WAIT_TIMEOUT:
			desc = yb_get_wait_timeout_desc(wait_event_info);
			break;
		case PG_WAIT_IO:
			desc = yb_get_wait_io_desc(wait_event_info);
			break;
		case PG_WAIT_BUFFER_PIN:
		case PG_WAIT_EXTENSION:
			break;
		default:
			break;
	}

	return desc;
}

static const char *
yb_get_wait_activity_desc(WaitEventActivity w)
{
	const char *desc = yb_not_applicable;

	switch (w)
	{
		case WAIT_EVENT_YB_QUERY_DIAGNOSTICS_MAIN:
			desc = "The YB Query Diagnostics background worker is waiting in the main loop.";
			break;
		case WAIT_EVENT_YB_ASH_MAIN:
			desc = "The YB ASH collector background worker is waiting in the main loop.";
			break;
		case WAIT_EVENT_CHECKPOINTER_MAIN:
		case WAIT_EVENT_WAL_SENDER_MAIN:
		case WAIT_EVENT_ARCHIVER_MAIN:
		case WAIT_EVENT_AUTOVACUUM_MAIN:
		case WAIT_EVENT_BGWRITER_HIBERNATE:
		case WAIT_EVENT_BGWRITER_MAIN:
		case WAIT_EVENT_LOGICAL_APPLY_MAIN:
		case WAIT_EVENT_LOGICAL_LAUNCHER_MAIN:
		case WAIT_EVENT_RECOVERY_WAL_STREAM:
		case WAIT_EVENT_SYSLOGGER_MAIN:
		case WAIT_EVENT_WAL_RECEIVER_MAIN:
		case WAIT_EVENT_WAL_WRITER_MAIN:
		case WAIT_EVENT_YB_IDLE_SLEEP:
		case WAIT_EVENT_YB_ACTIVITY_END:
			break;
			/* no default case, so that compiler will warn */
	}

	return desc;
}

static const char *
yb_get_wait_client_desc(WaitEventClient w)
{
	const char *desc = yb_not_applicable;

	switch (w)
	{
		case WAIT_EVENT_CLIENT_READ:
		case WAIT_EVENT_CLIENT_WRITE:
		case WAIT_EVENT_GSS_OPEN_SERVER:
		case WAIT_EVENT_SSL_OPEN_SERVER:
		case WAIT_EVENT_WAL_SENDER_WAIT_WAL:
		case WAIT_EVENT_LIBPQWALRECEIVER_CONNECT:
		case WAIT_EVENT_LIBPQWALRECEIVER_RECEIVE:
		case WAIT_EVENT_WAL_SENDER_WRITE_DATA:
		case WAIT_EVENT_YB_CLIENT_END:
			break;
			/* no default case, so that compiler will warn */
	}

	return desc;
}

static const char *
yb_get_wait_ipc_desc(WaitEventIPC w)
{
	const char *desc = yb_not_applicable;

	switch (w)
	{
		case WAIT_EVENT_YB_PARALLEL_SCAN_EMPTY:
			desc = "A YSQL backend is waiting on an empty queue while fetching parallel range keys.";
			break;
		case WAIT_EVENT_BACKEND_TERMINATION:
		case WAIT_EVENT_BGWORKER_SHUTDOWN:
		case WAIT_EVENT_BGWORKER_STARTUP:
		case WAIT_EVENT_BUFFER_IO:
		case WAIT_EVENT_CHECKPOINT_DONE:
		case WAIT_EVENT_CHECKPOINT_START:
		case WAIT_EVENT_MQ_INTERNAL:
		case WAIT_EVENT_MQ_PUT_MESSAGE:
		case WAIT_EVENT_MQ_RECEIVE:
		case WAIT_EVENT_MQ_SEND:
		case WAIT_EVENT_PARALLEL_FINISH:
		case WAIT_EVENT_PROCARRAY_GROUP_UPDATE:
		case WAIT_EVENT_PROC_SIGNAL_BARRIER:
		case WAIT_EVENT_REPLICATION_SLOT_DROP:
		case WAIT_EVENT_APPEND_READY:
		case WAIT_EVENT_ARCHIVE_CLEANUP_COMMAND:
		case WAIT_EVENT_ARCHIVE_COMMAND:
		case WAIT_EVENT_BACKUP_WAIT_WAL_ARCHIVE:
		case WAIT_EVENT_BTREE_PAGE:
		case WAIT_EVENT_EXECUTE_GATHER:
		case WAIT_EVENT_HASH_BATCH_ALLOCATE:
		case WAIT_EVENT_HASH_BATCH_ELECT:
		case WAIT_EVENT_HASH_BATCH_LOAD:
		case WAIT_EVENT_HASH_BUILD_ALLOCATE:
		case WAIT_EVENT_HASH_BUILD_ELECT:
		case WAIT_EVENT_HASH_BUILD_HASH_INNER:
		case WAIT_EVENT_HASH_BUILD_HASH_OUTER:
		case WAIT_EVENT_HASH_GROW_BATCHES_ALLOCATE:
		case WAIT_EVENT_HASH_GROW_BATCHES_DECIDE:
		case WAIT_EVENT_HASH_GROW_BATCHES_ELECT:
		case WAIT_EVENT_HASH_GROW_BATCHES_FINISH:
		case WAIT_EVENT_HASH_GROW_BATCHES_REPARTITION:
		case WAIT_EVENT_HASH_GROW_BUCKETS_ALLOCATE:
		case WAIT_EVENT_HASH_GROW_BUCKETS_ELECT:
		case WAIT_EVENT_HASH_GROW_BUCKETS_REINSERT:
		case WAIT_EVENT_LOGICAL_SYNC_DATA:
		case WAIT_EVENT_LOGICAL_SYNC_STATE_CHANGE:
		case WAIT_EVENT_PARALLEL_BITMAP_SCAN:
		case WAIT_EVENT_PARALLEL_CREATE_INDEX_SCAN:
		case WAIT_EVENT_PROMOTE:
		case WAIT_EVENT_RECOVERY_CONFLICT_SNAPSHOT:
		case WAIT_EVENT_RECOVERY_CONFLICT_TABLESPACE:
		case WAIT_EVENT_RECOVERY_END_COMMAND:
		case WAIT_EVENT_RECOVERY_PAUSE:
		case WAIT_EVENT_REPLICATION_ORIGIN_DROP:
		case WAIT_EVENT_RESTORE_COMMAND:
		case WAIT_EVENT_SAFE_SNAPSHOT:
		case WAIT_EVENT_SYNC_REP:
		case WAIT_EVENT_WAL_RECEIVER_EXIT:
		case WAIT_EVENT_WAL_RECEIVER_WAIT_START:
		case WAIT_EVENT_XACT_GROUP_UPDATE:
		case WAIT_EVENT_YB_IPC_END:
			break;
			/* no default case, so that compiler will warn */
	}

	return desc;
}

static const char *
yb_get_wait_timeout_desc(WaitEventTimeout w)
{
	const char *desc = yb_not_applicable;

	switch (w)
	{
		case WAIT_EVENT_YB_TXN_CONFLICT_BACKOFF:
			desc = "A YSQL backend is waiting for transaction conflict resolution with an exponential backoff.";
			break;
		case WAIT_EVENT_CHECKPOINT_WRITE_DELAY:
		case WAIT_EVENT_PG_SLEEP:
		case WAIT_EVENT_BASE_BACKUP_THROTTLE:
		case WAIT_EVENT_RECOVERY_APPLY_DELAY:
		case WAIT_EVENT_RECOVERY_RETRIEVE_RETRY_INTERVAL:
		case WAIT_EVENT_REGISTER_SYNC_REQUEST:
		case WAIT_EVENT_VACUUM_DELAY:
		case WAIT_EVENT_VACUUM_TRUNCATE:
		case WAIT_EVENT_YB_TIMEOUT_END:
			break;
			/* no default case, so that compiler will warn */
	}

	return desc;
}

static const char *
yb_get_wait_io_desc(WaitEventIO w)
{
	const char *desc = yb_not_applicable;

	switch (w)
	{
		case WAIT_EVENT_YB_COPY_COMMAND_STREAM_READ:
			desc = "A YSQL backend is waiting for a read from a file or program during COPY.";
			break;
		case WAIT_EVENT_YB_COPY_COMMAND_STREAM_WRITE:
			desc = "A YSQL backend is waiting for a write to a file or program during COPY.";
			break;
		case WAIT_EVENT_BUFFILE_READ:
		case WAIT_EVENT_BUFFILE_WRITE:
		case WAIT_EVENT_BUFFILE_TRUNCATE:
		case WAIT_EVENT_CONTROL_FILE_READ:
		case WAIT_EVENT_CONTROL_FILE_SYNC:
		case WAIT_EVENT_CONTROL_FILE_SYNC_UPDATE:
		case WAIT_EVENT_CONTROL_FILE_WRITE:
		case WAIT_EVENT_CONTROL_FILE_WRITE_UPDATE:
		case WAIT_EVENT_COPY_FILE_READ:
		case WAIT_EVENT_COPY_FILE_WRITE:
		case WAIT_EVENT_DSM_FILL_ZERO_WRITE:
		case WAIT_EVENT_LOCK_FILE_ADDTODATADIR_READ:
		case WAIT_EVENT_LOCK_FILE_ADDTODATADIR_SYNC:
		case WAIT_EVENT_LOCK_FILE_ADDTODATADIR_WRITE:
		case WAIT_EVENT_LOCK_FILE_CREATE_READ:
		case WAIT_EVENT_LOCK_FILE_CREATE_SYNC:
		case WAIT_EVENT_LOCK_FILE_CREATE_WRITE:
		case WAIT_EVENT_LOCK_FILE_RECHECKDATADIR_READ:
		case WAIT_EVENT_LOGICAL_REWRITE_CHECKPOINT_SYNC:
		case WAIT_EVENT_LOGICAL_REWRITE_MAPPING_SYNC:
		case WAIT_EVENT_LOGICAL_REWRITE_MAPPING_WRITE:
		case WAIT_EVENT_LOGICAL_REWRITE_SYNC:
		case WAIT_EVENT_LOGICAL_REWRITE_TRUNCATE:
		case WAIT_EVENT_LOGICAL_REWRITE_WRITE:
		case WAIT_EVENT_RELATION_MAP_SYNC:
		case WAIT_EVENT_RELATION_MAP_WRITE:
		case WAIT_EVENT_SLRU_READ:
		case WAIT_EVENT_SLRU_SYNC:
		case WAIT_EVENT_SLRU_WRITE:
		case WAIT_EVENT_WAL_BOOTSTRAP_SYNC:
		case WAIT_EVENT_WAL_BOOTSTRAP_WRITE:
		case WAIT_EVENT_WAL_INIT_SYNC:
		case WAIT_EVENT_WAL_INIT_WRITE:
		case WAIT_EVENT_WAL_READ:
		case WAIT_EVENT_WAL_SYNC:
		case WAIT_EVENT_WAL_WRITE:
		case WAIT_EVENT_BASEBACKUP_READ:
		case WAIT_EVENT_BASEBACKUP_SYNC:
		case WAIT_EVENT_BASEBACKUP_WRITE:
		case WAIT_EVENT_DATA_FILE_EXTEND:
		case WAIT_EVENT_DATA_FILE_FLUSH:
		case WAIT_EVENT_DATA_FILE_IMMEDIATE_SYNC:
		case WAIT_EVENT_DATA_FILE_PREFETCH:
		case WAIT_EVENT_DATA_FILE_READ:
		case WAIT_EVENT_DATA_FILE_SYNC:
		case WAIT_EVENT_DATA_FILE_TRUNCATE:
		case WAIT_EVENT_DATA_FILE_WRITE:
		case WAIT_EVENT_RELATION_MAP_READ:
		case WAIT_EVENT_REORDER_BUFFER_READ:
		case WAIT_EVENT_REORDER_BUFFER_WRITE:
		case WAIT_EVENT_REORDER_LOGICAL_MAPPING_READ:
		case WAIT_EVENT_REPLICATION_SLOT_READ:
		case WAIT_EVENT_REPLICATION_SLOT_RESTORE_SYNC:
		case WAIT_EVENT_REPLICATION_SLOT_SYNC:
		case WAIT_EVENT_REPLICATION_SLOT_WRITE:
		case WAIT_EVENT_SNAPBUILD_READ:
		case WAIT_EVENT_SNAPBUILD_SYNC:
		case WAIT_EVENT_SNAPBUILD_WRITE:
		case WAIT_EVENT_TIMELINE_HISTORY_FILE_SYNC:
		case WAIT_EVENT_TIMELINE_HISTORY_FILE_WRITE:
		case WAIT_EVENT_TIMELINE_HISTORY_READ:
		case WAIT_EVENT_TIMELINE_HISTORY_SYNC:
		case WAIT_EVENT_TIMELINE_HISTORY_WRITE:
		case WAIT_EVENT_TWOPHASE_FILE_READ:
		case WAIT_EVENT_TWOPHASE_FILE_SYNC:
		case WAIT_EVENT_TWOPHASE_FILE_WRITE:
		case WAIT_EVENT_VERSION_FILE_WRITE:
		case WAIT_EVENT_WALSENDER_TIMELINE_HISTORY_READ:
		case WAIT_EVENT_WAL_COPY_READ:
		case WAIT_EVENT_WAL_COPY_SYNC:
		case WAIT_EVENT_WAL_COPY_WRITE:
		case WAIT_EVENT_WAL_SYNC_METHOD_ASSIGN:
		case WAIT_EVENT_SLRU_FLUSH_SYNC:
		case WAIT_EVENT_VERSION_FILE_SYNC:
		case WAIT_EVENT_YB_IO_END:
			break;
			/* no default case, so that compiler will warn */
	}

	return desc;
}

static const char *
yb_get_wait_lock_desc(LockTagType lock_tag)
{
	const char *desc = yb_not_applicable;

	switch (lock_tag)
	{
		case LOCKTAG_RELATION:
		case LOCKTAG_RELATION_EXTEND:
		case LOCKTAG_DATABASE_FROZEN_IDS:
		case LOCKTAG_PAGE:
		case LOCKTAG_TUPLE:
		case LOCKTAG_TRANSACTION:
		case LOCKTAG_VIRTUALTRANSACTION:
		case LOCKTAG_SPECULATIVE_TOKEN:
		case LOCKTAG_OBJECT:
		case LOCKTAG_USERLOCK:
		case LOCKTAG_ADVISORY:
			break;
			/* no default case, so that compiler will warn */
	}

	return desc;
}

static const char *
yb_get_wait_lwlock_desc(BuiltinTrancheIds tranche_id)
{
	const char *desc = yb_not_applicable;

	switch (tranche_id)
	{
		case LWTRANCHE_YB_ASH_CIRCULAR_BUFFER:
			desc = "A YSQL backend is waiting for YB ASH circular buffer memory access.";
			break;
		case LWTRANCHE_YB_ASH_METADATA:
			desc = "A YSQL backend is waiting to update ASH metadata for a query.";
			break;
		case LWTRANCHE_YB_QUERY_DIAGNOSTICS:
			desc = "A YSQL backend is waiting for YB query diagnostics hash table memory access.";
			break;
		case LWTRANCHE_YB_QUERY_DIAGNOSTICS_CIRCULAR_BUFFER:
			desc = "A YSQL backend is waiting for YB query diagnostics circular buffer memory access.";
			break;
		case LWTRANCHE_YB_TERMINATED_QUERIES:
			desc = "A YSQL backend is waiting for YB terminated queries buffer memory access.";
			break;
		case LWTRANCHE_LOCK_FASTPATH:
		case LWTRANCHE_MULTIXACTMEMBER_BUFFER:
		case LWTRANCHE_MULTIXACTOFFSET_BUFFER:
		case LWTRANCHE_PGSTATS_DSA:
		case LWTRANCHE_PGSTATS_DATA:
		case LWTRANCHE_PGSTATS_HASH:
		case LWTRANCHE_SUBTRANS_BUFFER:
		case LWTRANCHE_WAL_INSERT:
		case LWTRANCHE_XACT_BUFFER:
		case LWTRANCHE_COMMITTS_BUFFER:
		case LWTRANCHE_NOTIFY_BUFFER:
		case LWTRANCHE_SERIAL_BUFFER:
		case LWTRANCHE_BUFFER_CONTENT:
		case LWTRANCHE_REPLICATION_ORIGIN_STATE:
		case LWTRANCHE_REPLICATION_SLOT_IO:
		case LWTRANCHE_BUFFER_MAPPING:
		case LWTRANCHE_LOCK_MANAGER:
		case LWTRANCHE_PREDICATE_LOCK_MANAGER:
		case LWTRANCHE_PARALLEL_HASH_JOIN:
		case LWTRANCHE_PARALLEL_QUERY_DSA:
		case LWTRANCHE_PER_SESSION_DSA:
		case LWTRANCHE_PER_SESSION_RECORD_TYPE:
		case LWTRANCHE_PER_SESSION_RECORD_TYPMOD:
		case LWTRANCHE_SHARED_TUPLESTORE:
		case LWTRANCHE_SHARED_TIDBITMAP:
		case LWTRANCHE_PARALLEL_APPEND:
		case LWTRANCHE_PER_XACT_PREDICATE_LIST:
		case LWTRANCHE_FIRST_USER_DEFINED:
			break;
			/* no default case, so that compiler will warn */
	}

	return desc;
}

static void
yb_insert_events_helper(uint32 code, const char *desc, TupleDesc tupdesc,
						Tuplestorestate *tupstore)
{
	int			ncols = YbGetNumberOfFunctionOutputColumns(F_YB_WAIT_EVENT_DESC);
	Datum		values[ncols];
	bool		nulls[ncols];

	MemSet(values, 0, sizeof(values));
	MemSet(nulls, 0, sizeof(nulls));

	values[0] = CStringGetTextDatum(YBCGetWaitEventClass(code));
	values[1] = CStringGetTextDatum(pgstat_get_wait_event_type(code));
	values[2] = CStringGetTextDatum(pgstat_get_wait_event(code));
	values[3] = CStringGetTextDatum(desc);
	if (ncols >= YB_WAIT_EVENT_DESC_COLS_V2)
		values[4] = UInt32GetDatum(code);

	tuplestore_putvalues(tupstore, tupdesc, values, nulls);
}

static void
yb_insert_pg_events(uint32 code, TupleDesc tupdesc, Tuplestorestate *tupstore)
{
	yb_insert_events_helper(code, yb_get_wait_event_desc(code), tupdesc, tupstore);
}

Datum
yb_wait_event_desc(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	uint32		i;

	/* ASH must be loaded first */
	if (!yb_enable_ash)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("ysql_yb_enable_ash gflag must be enabled")));

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));

	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	/* Switch context to construct returned data structures */
	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR,
				(errmsg_internal("return type must be a row type")));

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	/* for cpu event */
	yb_insert_pg_events(0, tupdesc, tupstore);

	/* wait events defined in wait_state.h */
	for (i = 0;; ++i)
	{
		YbcWaitEventDescriptor wait_event_desc = YBCGetWaitEventDescription(i);

		if (wait_event_desc.code == 0 && wait_event_desc.description == NULL)
			break;

		yb_insert_events_helper(wait_event_desc.code, wait_event_desc.description,
								tupdesc, tupstore);
	}

	/* for extension */
	yb_insert_pg_events(PG_WAIT_EXTENSION, tupdesc, tupstore);

	/* for buffer pin */
	yb_insert_pg_events(PG_WAIT_BUFFER_PIN, tupdesc, tupstore);

	/* description related to activity */
	for (i = WAIT_EVENT_ARCHIVER_MAIN; i < WAIT_EVENT_YB_ACTIVITY_END; ++i)
		yb_insert_pg_events(i, tupdesc, tupstore);

	/* description related to client */
	for (i = WAIT_EVENT_CLIENT_READ; i < WAIT_EVENT_YB_CLIENT_END; ++i)
		yb_insert_pg_events(i, tupdesc, tupstore);

	/* description related to IPC */
	for (i = WAIT_EVENT_APPEND_READY; i < WAIT_EVENT_YB_IPC_END; ++i)
		yb_insert_pg_events(i, tupdesc, tupstore);

	/* description related to timeout */
	for (i = WAIT_EVENT_BASE_BACKUP_THROTTLE; i < WAIT_EVENT_YB_TIMEOUT_END; ++i)
		yb_insert_pg_events(i, tupdesc, tupstore);

	/* description related to IO */
	for (i = WAIT_EVENT_BASEBACKUP_READ; i < WAIT_EVENT_YB_IO_END; ++i)
		yb_insert_pg_events(i, tupdesc, tupstore);

	/* description related to locks */
	for (i = LOCKTAG_RELATION; i <= LOCKTAG_LAST_TYPE; ++i)
		yb_insert_pg_events(PG_WAIT_LOCK | i, tupdesc, tupstore);

	/* description related to lwlocks */
	for (i = 0; i < LWTRANCHE_FIRST_USER_DEFINED; ++i)
	{
		if (i == 0 || i == 10 || i == 45)	/* deprecated, see lwlocknames.txt */
			continue;

		yb_insert_pg_events(PG_WAIT_LWLOCK | i, tupdesc, tupstore);
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);

	return (Datum) 0;
}

bool
YbIsIdleWaitEvent(uint32 wait_event_info)
{
	uint32		classId = wait_event_info & 0xFF000000;

	if (classId == PG_WAIT_ACTIVITY || classId == PG_WAIT_EXTENSION)
		return true;

	return false;
}
