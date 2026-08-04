/* ----------
 * wait_event.c
 *	  Wait event reporting infrastructure.
 *
 * Copyright (c) 2001-2026, PostgreSQL Global Development Group
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

#include "storage/lmgr.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "storage/subsystems.h"
#include "storage/spin.h"
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
#define YB_WAIT_EVENT_DESC_COLS_V3 6

static const char *pgstat_get_wait_activity(WaitEventActivity w);
static const char *pgstat_get_wait_buffer(WaitEventBuffer w);
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
static const char *yb_get_wait_event_aux_desc(uint32 wait_event_info);

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

#define WAIT_EVENT_CLASS_MASK	0xFF000000
#define WAIT_EVENT_ID_MASK		0x0000FFFF

/*
 * Hash tables for storing custom wait event ids and their names in
 * shared memory.
 *
 * WaitEventCustomHashByInfo is used to find the name from wait event
 * information.  Any backend can search it to find custom wait events.
 *
 * WaitEventCustomHashByName is used to find the wait event information from a
 * name.  It is used to ensure that no duplicated entries are registered.
 *
 * For simplicity, we use the same ID counter across types of custom events.
 * We could end that anytime the need arises.
 *
 * The size of the hash table is based on the assumption that usually only a
 * handful of entries are needed, but since it's small in absolute terms
 * anyway, we leave a generous amount of headroom.
 */
static HTAB *WaitEventCustomHashByInfo; /* find names from infos */
static HTAB *WaitEventCustomHashByName; /* find infos from names */

#define WAIT_EVENT_CUSTOM_HASH_SIZE	128

/* hash table entries */
typedef struct WaitEventCustomEntryByInfo
{
	uint32		wait_event_info;	/* hash key */
	char		wait_event_name[NAMEDATALEN];	/* custom wait event name */
} WaitEventCustomEntryByInfo;

typedef struct WaitEventCustomEntryByName
{
	char		wait_event_name[NAMEDATALEN];	/* hash key */
	uint32		wait_event_info;
} WaitEventCustomEntryByName;


/* dynamic allocation counter for custom wait events */
typedef struct WaitEventCustomCounterData
{
	int			nextId;			/* next ID to assign */
	slock_t		mutex;			/* protects the counter */
} WaitEventCustomCounterData;

/* pointer to the shared memory */
static WaitEventCustomCounterData *WaitEventCustomCounter;

/* first event ID of custom wait events */
#define WAIT_EVENT_CUSTOM_INITIAL_ID	1

static uint32 WaitEventCustomNew(uint32 classId, const char *wait_event_name);
static const char *GetWaitEventCustomIdentifier(uint32 wait_event_info);

static void WaitEventCustomShmemRequest(void *arg);
static void WaitEventCustomShmemInit(void *arg);

const ShmemCallbacks WaitEventCustomShmemCallbacks = {
	.request_fn = WaitEventCustomShmemRequest,
	.init_fn = WaitEventCustomShmemInit,
};

/*
 * Register shmem space for dynamic shared hash and dynamic allocation counter.
 */
static void
WaitEventCustomShmemRequest(void *arg)
{
	ShmemRequestStruct(.name = "WaitEventCustomCounterData",
					   .size = sizeof(WaitEventCustomCounterData),
					   .ptr = (void **) &WaitEventCustomCounter,
		);
	ShmemRequestHash(.name = "WaitEventCustom hash by wait event information",
					 .ptr = &WaitEventCustomHashByInfo,
					 .nelems = WAIT_EVENT_CUSTOM_HASH_SIZE,
					 .hash_info.keysize = sizeof(uint32),
					 .hash_info.entrysize = sizeof(WaitEventCustomEntryByInfo),
					 .hash_flags = HASH_ELEM | HASH_BLOBS,
		);
	ShmemRequestHash(.name = "WaitEventCustom hash by name",
					 .ptr = &WaitEventCustomHashByName,
					 .nelems = WAIT_EVENT_CUSTOM_HASH_SIZE,
	/* key is a NULL-terminated string */
					 .hash_info.keysize = sizeof(char[NAMEDATALEN]),
					 .hash_info.entrysize = sizeof(WaitEventCustomEntryByName),
					 .hash_flags = HASH_ELEM | HASH_STRINGS,
		);
}

static void
WaitEventCustomShmemInit(void *arg)
{
	/* initialize the allocation counter and its spinlock. */
	WaitEventCustomCounter->nextId = WAIT_EVENT_CUSTOM_INITIAL_ID;
	SpinLockInit(&WaitEventCustomCounter->mutex);
}

/*
 * Allocate a new event ID and return the wait event info.
 *
 * If the wait event name is already defined, this does not allocate a new
 * entry; it returns the wait event information associated to the name.
 */
uint32
WaitEventExtensionNew(const char *wait_event_name)
{
	return WaitEventCustomNew(PG_WAIT_EXTENSION, wait_event_name);
}

uint32
WaitEventInjectionPointNew(const char *wait_event_name)
{
	return WaitEventCustomNew(PG_WAIT_INJECTIONPOINT, wait_event_name);
}

static uint32
WaitEventCustomNew(uint32 classId, const char *wait_event_name)
{
	uint16		eventId;
	bool		found;
	WaitEventCustomEntryByName *entry_by_name;
	WaitEventCustomEntryByInfo *entry_by_info;
	uint32		wait_event_info;

	/* Check the limit of the length of the event name */
	if (strlen(wait_event_name) >= NAMEDATALEN)
		elog(ERROR,
			 "cannot use custom wait event string longer than %u characters",
			 NAMEDATALEN - 1);

	/*
	 * Check if the wait event info associated to the name is already defined,
	 * and return it if so.
	 */
	LWLockAcquire(WaitEventCustomLock, LW_SHARED);
	entry_by_name = (WaitEventCustomEntryByName *)
		hash_search(WaitEventCustomHashByName, wait_event_name,
					HASH_FIND, &found);
	LWLockRelease(WaitEventCustomLock);
	if (found)
	{
		uint32		oldClassId;

		oldClassId = entry_by_name->wait_event_info & WAIT_EVENT_CLASS_MASK;
		if (oldClassId != classId)
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_OBJECT),
					 errmsg("wait event \"%s\" already exists in type \"%s\"",
							wait_event_name,
							pgstat_get_wait_event_type(entry_by_name->wait_event_info))));
		return entry_by_name->wait_event_info;
	}

	/*
	 * Allocate and register a new wait event.  Recheck if the event name
	 * exists, as it could be possible that a concurrent process has inserted
	 * one with the same name since the LWLock acquired again here was
	 * previously released.
	 */
	LWLockAcquire(WaitEventCustomLock, LW_EXCLUSIVE);
	entry_by_name = (WaitEventCustomEntryByName *)
		hash_search(WaitEventCustomHashByName, wait_event_name,
					HASH_FIND, &found);
	if (found)
	{
		uint32		oldClassId;

		LWLockRelease(WaitEventCustomLock);
		oldClassId = entry_by_name->wait_event_info & WAIT_EVENT_CLASS_MASK;
		if (oldClassId != classId)
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_OBJECT),
					 errmsg("wait event \"%s\" already exists in type \"%s\"",
							wait_event_name,
							pgstat_get_wait_event_type(entry_by_name->wait_event_info))));
		return entry_by_name->wait_event_info;
	}

	/* Allocate a new event Id */
	SpinLockAcquire(&WaitEventCustomCounter->mutex);

	if (WaitEventCustomCounter->nextId >= WAIT_EVENT_CUSTOM_HASH_SIZE)
	{
		SpinLockRelease(&WaitEventCustomCounter->mutex);
		ereport(ERROR,
				errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				errmsg("too many custom wait events"));
	}

	eventId = WaitEventCustomCounter->nextId++;

	SpinLockRelease(&WaitEventCustomCounter->mutex);

	/* Register the new wait event */
	wait_event_info = classId | eventId;
	entry_by_info = (WaitEventCustomEntryByInfo *)
		hash_search(WaitEventCustomHashByInfo, &wait_event_info,
					HASH_ENTER, &found);
	Assert(!found);
	strlcpy(entry_by_info->wait_event_name, wait_event_name,
			sizeof(entry_by_info->wait_event_name));

	entry_by_name = (WaitEventCustomEntryByName *)
		hash_search(WaitEventCustomHashByName, wait_event_name,
					HASH_ENTER, &found);
	Assert(!found);
	entry_by_name->wait_event_info = wait_event_info;

	LWLockRelease(WaitEventCustomLock);

	return wait_event_info;
}

/*
 * Return the name of a custom wait event information.
 */
static const char *
GetWaitEventCustomIdentifier(uint32 wait_event_info)
{
	bool		found;
	WaitEventCustomEntryByInfo *entry;

	/* Built-in event? */
	if (wait_event_info == PG_WAIT_EXTENSION)
		return "Extension";

	/* It is a user-defined wait event, so lookup hash table. */
	LWLockAcquire(WaitEventCustomLock, LW_SHARED);
	entry = (WaitEventCustomEntryByInfo *)
		hash_search(WaitEventCustomHashByInfo, &wait_event_info,
					HASH_FIND, &found);
	LWLockRelease(WaitEventCustomLock);

	if (!entry)
		elog(ERROR,
			 "could not find custom name for wait event information %u",
			 wait_event_info);

	return entry->wait_event_name;
}


/*
 * Returns a list of currently defined custom wait event names.  The result is
 * a palloc'd array, with the number of elements saved in *nwaitevents.
 */
char	  **
GetWaitEventCustomNames(uint32 classId, int *nwaitevents)
{
	char	  **waiteventnames;
	WaitEventCustomEntryByName *hentry;
	HASH_SEQ_STATUS hash_seq;
	int			index;
	int			els;

	LWLockAcquire(WaitEventCustomLock, LW_SHARED);

	/* Now we can safely count the number of entries */
	els = hash_get_num_entries(WaitEventCustomHashByName);

	/* Allocate enough space for all entries */
	waiteventnames = palloc_array(char *, els);

	/* Now scan the hash table to copy the data */
	hash_seq_init(&hash_seq, WaitEventCustomHashByName);

	index = 0;
	while ((hentry = (WaitEventCustomEntryByName *) hash_seq_search(&hash_seq)) != NULL)
	{
		if ((hentry->wait_event_info & WAIT_EVENT_CLASS_MASK) != classId)
			continue;
		waiteventnames[index] = pstrdup(hentry->wait_event_name);
		index++;
	}

	LWLockRelease(WaitEventCustomLock);

	*nwaitevents = index;
	return waiteventnames;
}

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

	classId = wait_event_info & WAIT_EVENT_CLASS_MASK;

	switch (classId)
	{
		case PG_WAIT_LWLOCK:
			event_type = "LWLock";
			break;
		case PG_WAIT_LOCK:
			event_type = "Lock";
			break;
		case PG_WAIT_BUFFER:
			event_type = "Buffer";
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
		case PG_WAIT_INJECTIONPOINT:
			event_type = "InjectionPoint";
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

	classId = wait_event_info & WAIT_EVENT_CLASS_MASK;
	eventId = wait_event_info & WAIT_EVENT_ID_MASK;

	switch (classId)
	{
		case PG_WAIT_LWLOCK:
			event_name = GetLWLockIdentifier(classId, eventId);
			break;
		case PG_WAIT_LOCK:
			event_name = GetLockNameFromTagType(eventId);
			break;
		case PG_WAIT_EXTENSION:
		case PG_WAIT_INJECTIONPOINT:
			event_name = GetWaitEventCustomIdentifier(wait_event_info);
			break;
		case PG_WAIT_BUFFER:
			{
				WaitEventBuffer w = (WaitEventBuffer) wait_event_info;

				event_name = pgstat_get_wait_buffer(w);
				break;
			}
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

#include "utils/pgstat_wait_event.c"

static const char *
yb_get_wait_event_aux_desc(uint32 wait_event_info)
{
	uint32		classId;

	classId = wait_event_info & 0xFF000000;

	switch (classId)
	{
		case PG_WAIT_LWLOCK:
		case PG_WAIT_LOCK:
		case PG_WAIT_BUFFER:
		case PG_WAIT_ACTIVITY:
		case PG_WAIT_CLIENT:
		case PG_WAIT_EXTENSION:
		case PG_WAIT_IPC:
		case PG_WAIT_TIMEOUT:
		case PG_WAIT_IO:
			return "";
		default:
			return YBCGetWaitEventAuxDescription(wait_event_info);
	}
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
		case PG_WAIT_BUFFER:
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
		case WAIT_EVENT_CHECKPOINTER_SHUTDOWN:
		case WAIT_EVENT_IO_WORKER_MAIN:
		case WAIT_EVENT_LOGICAL_PARALLEL_APPLY_MAIN:
		case WAIT_EVENT_REPLICATION_SLOTSYNC_MAIN:
		case WAIT_EVENT_WAL_SUMMARIZER_WAL:
		case WAIT_EVENT_REPLICATION_SLOTSYNC_SHUTDOWN:
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
		case WAIT_EVENT_WAL_SENDER_WAIT_FOR_WAL:
		case WAIT_EVENT_LIBPQWALRECEIVER_CONNECT:
		case WAIT_EVENT_LIBPQWALRECEIVER_RECEIVE:
		case WAIT_EVENT_WAL_SENDER_WRITE_DATA:
		case WAIT_EVENT_YB_CLIENT_END:
		case WAIT_EVENT_WAIT_FOR_STANDBY_CONFIRMATION:
		case WAIT_EVENT_WAIT_FOR_WAL_FLUSH:
		case WAIT_EVENT_WAIT_FOR_WAL_REPLAY:
		case WAIT_EVENT_WAIT_FOR_WAL_WRITE:
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
		case WAIT_EVENT_HASH_GROW_BATCHES_REALLOCATE:
		case WAIT_EVENT_HASH_GROW_BATCHES_DECIDE:
		case WAIT_EVENT_HASH_GROW_BATCHES_ELECT:
		case WAIT_EVENT_HASH_GROW_BATCHES_FINISH:
		case WAIT_EVENT_HASH_GROW_BATCHES_REPARTITION:
		case WAIT_EVENT_HASH_GROW_BUCKETS_REALLOCATE:
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
		case WAIT_EVENT_CHECKPOINT_DELAY_COMPLETE:
		case WAIT_EVENT_CHECKPOINT_DELAY_START:
		case WAIT_EVENT_CHECKSUM_ENABLE_STARTCONDITION:
		case WAIT_EVENT_CHECKSUM_ENABLE_TEMPTABLE_WAIT:
		case WAIT_EVENT_LOGICAL_APPLY_SEND_DATA:
		case WAIT_EVENT_LOGICAL_PARALLEL_APPLY_STATE_CHANGE:
		case WAIT_EVENT_MESSAGE_QUEUE_INTERNAL:
		case WAIT_EVENT_MESSAGE_QUEUE_PUT_MESSAGE:
		case WAIT_EVENT_MESSAGE_QUEUE_RECEIVE:
		case WAIT_EVENT_MESSAGE_QUEUE_SEND:
		case WAIT_EVENT_MULTIXACT_CREATION:
		case WAIT_EVENT_REPACK_WORKER_EXPORT:
		case WAIT_EVENT_WAL_SUMMARY_READY:
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
		case WAIT_EVENT_COMMIT_DELAY:
		case WAIT_EVENT_SPIN_DELAY:
		case WAIT_EVENT_WAL_SUMMARIZER_ERROR:
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
		case WAIT_EVENT_RELATION_MAP_READ:
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
		case WAIT_EVENT_AIO_IO_COMPLETION:
		case WAIT_EVENT_AIO_IO_URING_EXECUTION:
		case WAIT_EVENT_AIO_IO_URING_SUBMIT:
		case WAIT_EVENT_COPY_FILE_COPY:
		case WAIT_EVENT_COPY_FROM_READ:
		case WAIT_EVENT_COPY_TO_WRITE:
		case WAIT_EVENT_DSM_ALLOCATE:
		case WAIT_EVENT_RELATION_MAP_REPLACE:
		case WAIT_EVENT_WAL_SUMMARY_READ:
		case WAIT_EVENT_WAL_SUMMARY_WRITE:
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
		case LOCKTAG_APPLY_TRANSACTION:
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
		case LWTRANCHE_BUFFER_MAPPING:
		case LWTRANCHE_REPLICATION_ORIGIN_STATE:
		case LWTRANCHE_REPLICATION_SLOT_IO:
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
		case LWTRANCHE_INVALID:
		case LWTRANCHE_NOTIFY_CHANNEL_HASH:
		case LWTRANCHE_PARALLEL_BTREE_SCAN:
		case LWTRANCHE_LAUNCHER_DSA:
		case LWTRANCHE_LAUNCHER_HASH:
		case LWTRANCHE_DSM_REGISTRY_DSA:
		case LWTRANCHE_DSM_REGISTRY_HASH:
		case LWTRANCHE_COMMITTS_SLRU:
		case LWTRANCHE_MULTIXACTOFFSET_SLRU:
		case LWTRANCHE_MULTIXACTMEMBER_SLRU:
		case LWTRANCHE_NOTIFY_SLRU:
		case LWTRANCHE_SERIAL_SLRU:
		case LWTRANCHE_SUBTRANS_SLRU:
		case LWTRANCHE_XACT_SLRU:
		case LWTRANCHE_PARALLEL_VACUUM_DSA:
		case LWTRANCHE_AIO_URING_COMPLETION:
		case LWTRANCHE_SHMEM_INDEX:
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
	if (ncols >= YB_WAIT_EVENT_DESC_COLS_V3)
		values[5] = CStringGetTextDatum(yb_get_wait_event_aux_desc(code));
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
	yb_insert_pg_events(PG_WAIT_BUFFER, tupdesc, tupstore);

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
