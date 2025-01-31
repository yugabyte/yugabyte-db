/* ----------
 * backend_status.c
 *	  Backend status reporting infrastructure.
 *
 * Copyright (c) 2001-2022, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/activity/backend_status.c
 * ----------
 */
#include "postgres.h"

#include "access/xact.h"
#include "libpq/libpq.h"
#include "miscadmin.h"
#include "pg_trace.h"
#include "pgstat.h"
#include "port/atomics.h"		/* for memory barriers */
#include "storage/ipc.h"
#include "storage/proc.h"		/* for MyProc */
#include "storage/sinvaladt.h"
#include "utils/ascii.h"
#include "utils/backend_status.h"
#include "utils/guc.h"			/* for application_name */
#include "utils/memutils.h"

/* Yugabyte includes */
#include "pg_yb_utils.h"
#include "utils/syscache.h"

/* ----------
 * Total number of backends including auxiliary
 *
 * We reserve a slot for each possible BackendId, plus one for each
 * possible auxiliary process type.  (This scheme assumes there is not
 * more than one of any auxiliary process type at a time.) MaxBackends
 * includes autovacuum workers and background workers as well.
 * ----------
 */
#define NumBackendStatSlots (MaxBackends + NUM_AUXPROCTYPES)


/* ----------
 * GUC parameters
 * ----------
 */
bool		pgstat_track_activities = false;
int			pgstat_track_activity_query_size = 1024;


/* exposed so that backend_progress.c can access it */
PgBackendStatus *MyBEEntry = NULL;
static char *DatabaseNameBuffer = NULL;

static PgBackendStatus *BackendStatusArray = NULL;
static char *BackendAppnameBuffer = NULL;
static char *BackendClientHostnameBuffer = NULL;
static char *BackendActivityBuffer = NULL;
static Size BackendActivityBufferSize = 0;
#ifdef USE_SSL
static PgBackendSSLStatus *BackendSslStatusBuffer = NULL;
#endif
#ifdef ENABLE_GSS
static PgBackendGSSStatus *BackendGssStatusBuffer = NULL;
#endif

/* Status for backends including auxiliary */
static LocalPgBackendStatus *localBackendStatusTable = NULL;

/* Total number of backends including auxiliary */
static int	localNumBackends = 0;

static MemoryContext backendStatusSnapContext;


static void pgstat_beshutdown_hook(int code, Datum arg);
static void pgstat_read_current_status(void);
static void pgstat_setup_backend_status_context(void);


/*
 * Report shared-memory space needed by CreateSharedBackendStatus.
 */
Size
BackendStatusShmemSize(void)
{
	Size		size;

	/* BackendStatusArray: */
	size = mul_size(sizeof(PgBackendStatus), NumBackendStatSlots);
	/* BackendAppnameBuffer: */
	size = add_size(size,
					mul_size(NAMEDATALEN, NumBackendStatSlots));
	/* BackendClientHostnameBuffer: */
	size = add_size(size,
					mul_size(NAMEDATALEN, NumBackendStatSlots));
	/* BackendActivityBuffer: */
	size = add_size(size,
					mul_size(pgstat_track_activity_query_size, NumBackendStatSlots));
#ifdef USE_SSL
	/* BackendSslStatusBuffer: */
	size = add_size(size,
					mul_size(sizeof(PgBackendSSLStatus), NumBackendStatSlots));
#endif
#ifdef ENABLE_GSS
	/* BackendGssStatusBuffer: */
	size = add_size(size,
					mul_size(sizeof(PgBackendGSSStatus), NumBackendStatSlots));
#endif
	size = add_size(size, mul_size(NAMEDATALEN, NumBackendStatSlots));

	/* yb_new_conn metric */
	size = add_size(size, sizeof(uint64_t));

	return size;
}

/*
 * Initialize the shared status array and several string buffers
 * during postmaster startup.
 */
void
CreateSharedBackendStatus(void)
{
	Size		size;
	bool		found;
	int			i;
	char	   *buffer;

	/* Create or attach to the shared array */
	size = mul_size(sizeof(PgBackendStatus), NumBackendStatSlots);
	BackendStatusArray = (PgBackendStatus *)
		ShmemInitStruct("Backend Status Array", size, &found);

	if (!found)
	{
		/*
		 * We're the first - initialize.
		 */
		MemSet(BackendStatusArray, 0, size);
	}

	/* Create or attach to the shared appname buffer */
	size = mul_size(NAMEDATALEN, NumBackendStatSlots);
	BackendAppnameBuffer = (char *)
		ShmemInitStruct("Backend Application Name Buffer", size, &found);

	if (!found)
	{
		MemSet(BackendAppnameBuffer, 0, size);

		/* Initialize st_appname pointers. */
		buffer = BackendAppnameBuffer;
		for (i = 0; i < NumBackendStatSlots; i++)
		{
			BackendStatusArray[i].st_appname = buffer;
			buffer += NAMEDATALEN;
		}
	}

	/* Create or attach to the shared client hostname buffer */
	size = mul_size(NAMEDATALEN, NumBackendStatSlots);
	BackendClientHostnameBuffer = (char *)
		ShmemInitStruct("Backend Client Host Name Buffer", size, &found);

	if (!found)
	{
		MemSet(BackendClientHostnameBuffer, 0, size);

		/* Initialize st_clienthostname pointers. */
		buffer = BackendClientHostnameBuffer;
		for (i = 0; i < NumBackendStatSlots; i++)
		{
			BackendStatusArray[i].st_clienthostname = buffer;
			buffer += NAMEDATALEN;
		}
	}

	/* Create or attach to the shared activity buffer */
	BackendActivityBufferSize = mul_size(pgstat_track_activity_query_size,
										 NumBackendStatSlots);
	BackendActivityBuffer = (char *)
		ShmemInitStruct("Backend Activity Buffer",
						BackendActivityBufferSize,
						&found);

	if (!found)
	{
		MemSet(BackendActivityBuffer, 0, BackendActivityBufferSize);

		/* Initialize st_activity pointers. */
		buffer = BackendActivityBuffer;
		for (i = 0; i < NumBackendStatSlots; i++)
		{
			BackendStatusArray[i].st_activity_raw = buffer;
			buffer += pgstat_track_activity_query_size;
		}
	}

	if (YBIsEnabledInPostgresEnvVar())
	{
		Size		DatabaseNameBufferSize;

		DatabaseNameBufferSize = mul_size(NAMEDATALEN, NumBackendStatSlots);
		DatabaseNameBuffer = (char *)
			ShmemInitStruct("Database Name Buffer", DatabaseNameBufferSize, &found);

		if (!found)
		{
			MemSet(DatabaseNameBuffer, 0, DatabaseNameBufferSize);

			/* Initialize st_databasename pointers. */
			buffer = DatabaseNameBuffer;
			for (i = 0; i < NumBackendStatSlots; i++)
			{
				BackendStatusArray[i].st_databasename = buffer;
				buffer += NAMEDATALEN;
			}
		}
	}

#ifdef USE_SSL
	/* Create or attach to the shared SSL status buffer */
	size = mul_size(sizeof(PgBackendSSLStatus), NumBackendStatSlots);
	BackendSslStatusBuffer = (PgBackendSSLStatus *)
		ShmemInitStruct("Backend SSL Status Buffer", size, &found);

	if (!found)
	{
		PgBackendSSLStatus *ptr;

		MemSet(BackendSslStatusBuffer, 0, size);

		/* Initialize st_sslstatus pointers. */
		ptr = BackendSslStatusBuffer;
		for (i = 0; i < NumBackendStatSlots; i++)
		{
			BackendStatusArray[i].st_sslstatus = ptr;
			ptr++;
		}
	}
#endif

#ifdef ENABLE_GSS
	/* Create or attach to the shared GSSAPI status buffer */
	size = mul_size(sizeof(PgBackendGSSStatus), NumBackendStatSlots);
	BackendGssStatusBuffer = (PgBackendGSSStatus *)
		ShmemInitStruct("Backend GSS Status Buffer", size, &found);

	if (!found)
	{
		PgBackendGSSStatus *ptr;

		MemSet(BackendGssStatusBuffer, 0, size);

		/* Initialize st_gssstatus pointers. */
		ptr = BackendGssStatusBuffer;
		for (i = 0; i < NumBackendStatSlots; i++)
		{
			BackendStatusArray[i].st_gssstatus = ptr;
			ptr++;
		}
	}
#endif

	yb_new_conn = (uint64_t *) ShmemAlloc(sizeof(uint64_t));
	(*yb_new_conn) = 0;
}

/*
 * Initialize pgstats backend activity state, and set up our on-proc-exit
 * hook.  Called from InitPostgres and AuxiliaryProcessMain. For auxiliary
 * process, MyBackendId is invalid. Otherwise, MyBackendId must be set, but we
 * must not have started any transaction yet (since the exit hook must run
 * after the last transaction exit).
 *
 * NOTE: MyDatabaseId isn't set yet; so the shutdown hook has to be careful.
 */
void
pgstat_beinit(void)
{
	/* Initialize MyBEEntry */
	if (MyBackendId != InvalidBackendId)
	{
		Assert(MyBackendId >= 1 && MyBackendId <= MaxBackends);
		MyBEEntry = &BackendStatusArray[MyBackendId - 1];
	}
	else
	{
		/* Must be an auxiliary process */
		Assert(MyAuxProcType != NotAnAuxProcess);

		/*
		 * Assign the MyBEEntry for an auxiliary process.  Since it doesn't
		 * have a BackendId, the slot is statically allocated based on the
		 * auxiliary process type (MyAuxProcType).  Backends use slots indexed
		 * in the range from 1 to MaxBackends (inclusive), so we use
		 * MaxBackends + AuxBackendType + 1 as the index of the slot for an
		 * auxiliary process.
		 */
		MyBEEntry = &BackendStatusArray[MaxBackends + MyAuxProcType];
	}

	/* Set up a process-exit hook to clean up */
	on_shmem_exit(pgstat_beshutdown_hook, 0);
}


/* ----------
 * pgstat_bestart() -
 *
 *	Initialize this backend's entry in the PgBackendStatus array.
 *	Called from InitPostgres.
 *
 *	Apart from auxiliary processes, MyBackendId, MyDatabaseId,
 *	session userid, and application_name must be set for a
 *	backend (hence, this cannot be combined with pgbestat_beinit).
 *	Note also that we must be inside a transaction if this isn't an aux
 *	process, as we may need to do encoding conversion on some strings.
 * ----------
 */
void
pgstat_bestart(void)
{
	volatile PgBackendStatus *vbeentry = MyBEEntry;
	PgBackendStatus lbeentry;
#ifdef USE_SSL
	PgBackendSSLStatus lsslstatus;
#endif
#ifdef ENABLE_GSS
	PgBackendGSSStatus lgssstatus;
#endif

	/* pgstats state must be initialized from pgstat_beinit() */
	Assert(vbeentry != NULL);

	/*
	 * To minimize the time spent modifying the PgBackendStatus entry, and
	 * avoid risk of errors inside the critical section, we first copy the
	 * shared-memory struct to a local variable, then modify the data in the
	 * local variable, then copy the local variable back to shared memory.
	 * Only the last step has to be inside the critical section.
	 *
	 * Most of the data we copy from shared memory is just going to be
	 * overwritten, but the struct's not so large that it's worth the
	 * maintenance hassle to copy only the needful fields.
	 */
	memcpy(&lbeentry,
		   unvolatize(PgBackendStatus *, vbeentry),
		   sizeof(PgBackendStatus));

	/* These structs can just start from zeroes each time, though */
#ifdef USE_SSL
	memset(&lsslstatus, 0, sizeof(lsslstatus));
#endif
#ifdef ENABLE_GSS
	memset(&lgssstatus, 0, sizeof(lgssstatus));
#endif

	/*
	 * Now fill in all the fields of lbeentry, except for strings that are
	 * out-of-line data.  Those have to be handled separately, below.
	 */
	lbeentry.st_procpid = MyProcPid;
	lbeentry.st_backendType = MyBackendType;
	lbeentry.st_proc_start_timestamp = MyStartTimestamp;
	lbeentry.st_activity_start_timestamp = 0;
	lbeentry.st_state_start_timestamp = 0;
	lbeentry.st_xact_start_timestamp = 0;
	lbeentry.st_databaseid = MyDatabaseId;

	/* Increment the total connections counter */
	if (lbeentry.st_procpid > 0 &&
		(lbeentry.st_backendType == B_BACKEND ||
		 lbeentry.st_backendType == YB_YSQL_CONN_MGR))
		(*yb_new_conn)++;

	if (YBIsEnabledInPostgresEnvVar() && lbeentry.st_databaseid > 0)
	{
		HeapTuple	tuple;

		tuple = SearchSysCache1(DATABASEOID, ObjectIdGetDatum(lbeentry.st_databaseid));
		Form_pg_database dbForm;

		dbForm = (Form_pg_database) GETSTRUCT(tuple);
		strcpy(lbeentry.st_databasename, dbForm->datname.data);
		ReleaseSysCache(tuple);

		/* Initialization of allocated memory measurement value */
		lbeentry.yb_st_allocated_mem_bytes = PgMemTracker.backend_cur_allocated_mem_bytes;
	}

	/* We have userid for client-backends, wal-sender and bgworker processes */
	if (lbeentry.st_backendType == B_BACKEND
		|| lbeentry.st_backendType == B_WAL_SENDER
		|| lbeentry.st_backendType == B_BG_WORKER
		|| lbeentry.st_backendType == YB_YSQL_CONN_MGR)
		lbeentry.st_userid = GetSessionUserId();
	else
		lbeentry.st_userid = InvalidOid;

	/*
	 * We may not have a MyProcPort (eg, if this is the autovacuum process).
	 * If so, use all-zeroes client address, which is dealt with specially in
	 * pg_stat_get_backend_client_addr and pg_stat_get_backend_client_port.
	 */
	if (MyProcPort)
		memcpy(&lbeentry.st_clientaddr, &MyProcPort->raddr,
			   sizeof(lbeentry.st_clientaddr));
	else
		MemSet(&lbeentry.st_clientaddr, 0, sizeof(lbeentry.st_clientaddr));

#ifdef USE_SSL
	if (MyProcPort && MyProcPort->ssl_in_use)
	{
		lbeentry.st_ssl = true;
		lsslstatus.ssl_bits = be_tls_get_cipher_bits(MyProcPort);
		strlcpy(lsslstatus.ssl_version, be_tls_get_version(MyProcPort), NAMEDATALEN);
		strlcpy(lsslstatus.ssl_cipher, be_tls_get_cipher(MyProcPort), NAMEDATALEN);
		be_tls_get_peer_subject_name(MyProcPort, lsslstatus.ssl_client_dn, NAMEDATALEN);
		be_tls_get_peer_serial(MyProcPort, lsslstatus.ssl_client_serial, NAMEDATALEN);
		be_tls_get_peer_issuer_name(MyProcPort, lsslstatus.ssl_issuer_dn, NAMEDATALEN);
	}
	else
	{
		lbeentry.st_ssl = false;
	}
#else
	lbeentry.st_ssl = false;
#endif

#ifdef ENABLE_GSS
	if (MyProcPort && MyProcPort->gss != NULL)
	{
		const char *princ = be_gssapi_get_princ(MyProcPort);

		lbeentry.st_gss = true;
		lgssstatus.gss_auth = be_gssapi_get_auth(MyProcPort);
		lgssstatus.gss_enc = be_gssapi_get_enc(MyProcPort);
		if (princ)
			strlcpy(lgssstatus.gss_princ, princ, NAMEDATALEN);
	}
	else
	{
		lbeentry.st_gss = false;
	}
#else
	lbeentry.st_gss = false;
#endif

	lbeentry.st_state = STATE_UNDEFINED;
	lbeentry.st_progress_command = PROGRESS_COMMAND_INVALID;
	lbeentry.st_progress_command_target = InvalidOid;
	lbeentry.st_query_id = UINT64CONST(0);

	/*
	 * we don't zero st_progress_param here to save cycles; nobody should
	 * examine it until st_progress_command has been set to something other
	 * than PROGRESS_COMMAND_INVALID
	 */

	/*
	 * We're ready to enter the critical section that fills the shared-memory
	 * status entry.  We follow the protocol of bumping st_changecount before
	 * and after; and make sure it's even afterwards.  We use a volatile
	 * pointer here to ensure the compiler doesn't try to get cute.
	 */
	PGSTAT_BEGIN_WRITE_ACTIVITY(vbeentry);

	/* make sure we'll memcpy the same st_changecount back */
	lbeentry.st_changecount = vbeentry->st_changecount;

	memcpy(unvolatize(PgBackendStatus *, vbeentry),
		   &lbeentry,
		   sizeof(PgBackendStatus));

	/*
	 * We can write the out-of-line strings and structs using the pointers
	 * that are in lbeentry; this saves some de-volatilizing messiness.
	 */
	lbeentry.st_appname[0] = '\0';
	if (MyProcPort && MyProcPort->remote_hostname)
		strlcpy(lbeentry.st_clienthostname, MyProcPort->remote_hostname,
				NAMEDATALEN);
	else
		lbeentry.st_clienthostname[0] = '\0';
	lbeentry.st_activity_raw[0] = '\0';
	/* Also make sure the last byte in each string area is always 0 */
	lbeentry.st_appname[NAMEDATALEN - 1] = '\0';
	lbeentry.st_clienthostname[NAMEDATALEN - 1] = '\0';
	lbeentry.st_activity_raw[pgstat_track_activity_query_size - 1] = '\0';

#ifdef USE_SSL
	memcpy(lbeentry.st_sslstatus, &lsslstatus, sizeof(PgBackendSSLStatus));
#endif
#ifdef ENABLE_GSS
	memcpy(lbeentry.st_gssstatus, &lgssstatus, sizeof(PgBackendGSSStatus));
#endif

	PGSTAT_END_WRITE_ACTIVITY(vbeentry);

	/* Update app name to current GUC setting */
	if (application_name)
		pgstat_report_appname(application_name);
}

/*
 * Clear out our entry in the PgBackendStatus array.
 */
static void
pgstat_beshutdown_hook(int code, Datum arg)
{
	volatile PgBackendStatus *beentry = MyBEEntry;

	/*
	 * Clear my status entry, following the protocol of bumping st_changecount
	 * before and after.  We use a volatile pointer here to ensure the
	 * compiler doesn't try to get cute.
	 */
	PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);

	beentry->st_procpid = 0;	/* mark invalid */
	beentry->yb_session_id = 0;

	PGSTAT_END_WRITE_ACTIVITY(beentry);

	/* so that functions can check if backend_status.c is up via MyBEEntry */
	MyBEEntry = NULL;
}

/*
 * Discard any data collected in the current transaction.  Any subsequent
 * request will cause new snapshots to be read.
 *
 * This is also invoked during transaction commit or abort to discard the
 * no-longer-wanted snapshot.
 */
void
pgstat_clear_backend_activity_snapshot(void)
{
	/* Release memory, if any was allocated */
	if (backendStatusSnapContext)
	{
		MemoryContextDelete(backendStatusSnapContext);
		backendStatusSnapContext = NULL;
	}

	/* Reset variables */
	localBackendStatusTable = NULL;
	localNumBackends = 0;
}

static void
pgstat_setup_backend_status_context(void)
{
	if (!backendStatusSnapContext)
		backendStatusSnapContext = AllocSetContextCreate(TopMemoryContext,
														 "Backend Status Snapshot",
														 ALLOCSET_SMALL_SIZES);
}


/* ----------
 * pgstat_report_activity() -
 *
 *	Called from tcop/postgres.c to report what the backend is actually doing
 *	(but note cmd_str can be NULL for certain cases).
 *
 * All updates of the status entry follow the protocol of bumping
 * st_changecount before and after.  We use a volatile pointer here to
 * ensure the compiler doesn't try to get cute.
 * ----------
 */
void
pgstat_report_activity(BackendState state, const char *cmd_str)
{
	volatile PgBackendStatus *beentry = MyBEEntry;
	TimestampTz start_timestamp;
	TimestampTz current_timestamp;
	int			len = 0;

	TRACE_POSTGRESQL_STATEMENT_STATUS(cmd_str);

	if (!beentry)
		return;

	if (!pgstat_track_activities)
	{
		if (beentry->st_state != STATE_DISABLED)
		{
			volatile PGPROC *proc = MyProc;

			/*
			 * track_activities is disabled, but we last reported a
			 * non-disabled state.  As our final update, change the state and
			 * clear fields we will not be updating anymore.
			 */
			PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);
			beentry->st_state = STATE_DISABLED;
			beentry->st_state_start_timestamp = 0;
			beentry->st_activity_raw[0] = '\0';
			beentry->st_activity_start_timestamp = 0;
			/* st_xact_start_timestamp and wait_event_info are also disabled */
			beentry->st_xact_start_timestamp = 0;
			beentry->st_query_id = UINT64CONST(0);
			proc->wait_event_info = 0;
			PGSTAT_END_WRITE_ACTIVITY(beentry);
		}
		return;
	}

	/*
	 * To minimize the time spent modifying the entry, and avoid risk of
	 * errors inside the critical section, fetch all the needed data first.
	 */
	start_timestamp = GetCurrentStatementStartTimestamp();
	if (cmd_str != NULL)
	{
		/*
		 * Compute length of to-be-stored string unaware of multi-byte
		 * characters. For speed reasons that'll get corrected on read, rather
		 * than computed every write.
		 */
		len = Min(strlen(cmd_str), pgstat_track_activity_query_size - 1);
	}
	current_timestamp = GetCurrentTimestamp();

	/*
	 * If the state has changed from "active" or "idle in transaction",
	 * calculate the duration.
	 */
	if ((beentry->st_state == STATE_RUNNING ||
		 beentry->st_state == STATE_FASTPATH ||
		 beentry->st_state == STATE_IDLEINTRANSACTION ||
		 beentry->st_state == STATE_IDLEINTRANSACTION_ABORTED) &&
		state != beentry->st_state)
	{
		long		secs;
		int			usecs;

		TimestampDifference(beentry->st_state_start_timestamp,
							current_timestamp,
							&secs, &usecs);

		if (beentry->st_state == STATE_RUNNING ||
			beentry->st_state == STATE_FASTPATH)
			pgstat_count_conn_active_time((PgStat_Counter) secs * 1000000 + usecs);
		else
			pgstat_count_conn_txn_idle_time((PgStat_Counter) secs * 1000000 + usecs);
	}

	/*
	 * Now update the status entry
	 */
	PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);

	beentry->st_state = state;
	beentry->st_state_start_timestamp = current_timestamp;

	/*
	 * If a new query is started, we reset the query identifier as it'll only
	 * be known after parse analysis, to avoid reporting last query's
	 * identifier.
	 */
	if (state == STATE_RUNNING)
		beentry->st_query_id = UINT64CONST(0);

	if (cmd_str != NULL)
	{
		memcpy((char *) beentry->st_activity_raw, cmd_str, len);
		beentry->st_activity_raw[len] = '\0';
		beentry->st_activity_start_timestamp = start_timestamp;
	}

	PGSTAT_END_WRITE_ACTIVITY(beentry);
}

/* --------
 * pgstat_report_query_id() -
 *
 * Called to update top-level query identifier.
 * --------
 */
void
pgstat_report_query_id(uint64 query_id, bool force)
{
	volatile PgBackendStatus *beentry = MyBEEntry;

	/*
	 * if track_activities is disabled, st_query_id should already have been
	 * reset
	 */
	if (!beentry || !pgstat_track_activities)
		return;

	/*
	 * We only report the top-level query identifiers.  The stored query_id is
	 * reset when a backend calls pgstat_report_activity(STATE_RUNNING), or
	 * with an explicit call to this function using the force flag.  If the
	 * saved query identifier is not zero it means that it's not a top-level
	 * command, so ignore the one provided unless it's an explicit call to
	 * reset the identifier.
	 */
	if (beentry->st_query_id != 0 && !force)
		return;

	/*
	 * Update my status entry, following the protocol of bumping
	 * st_changecount before and after.  We use a volatile pointer here to
	 * ensure the compiler doesn't try to get cute.
	 */
	PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);
	beentry->st_query_id = query_id;
	PGSTAT_END_WRITE_ACTIVITY(beentry);
}


/* ----------
 * pgstat_report_appname() -
 *
 *	Called to update our application name.
 * ----------
 */
void
pgstat_report_appname(const char *appname)
{
	volatile PgBackendStatus *beentry = MyBEEntry;
	int			len;

	if (!beentry)
		return;

	/* This should be unnecessary if GUC did its job, but be safe */
	len = pg_mbcliplen(appname, strlen(appname), NAMEDATALEN - 1);

	/*
	 * Update my status entry, following the protocol of bumping
	 * st_changecount before and after.  We use a volatile pointer here to
	 * ensure the compiler doesn't try to get cute.
	 */
	PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);

	memcpy((char *) beentry->st_appname, appname, len);
	beentry->st_appname[len] = '\0';

	PGSTAT_END_WRITE_ACTIVITY(beentry);
}

/*
 * Report current transaction start timestamp as the specified value.
 * Zero means there is no active transaction.
 */
void
pgstat_report_xact_timestamp(TimestampTz tstamp)
{
	volatile PgBackendStatus *beentry = MyBEEntry;

	if (!pgstat_track_activities || !beentry)
		return;

	/*
	 * Update my status entry, following the protocol of bumping
	 * st_changecount before and after.  We use a volatile pointer here to
	 * ensure the compiler doesn't try to get cute.
	 */
	PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);

	beentry->st_xact_start_timestamp = tstamp;

	PGSTAT_END_WRITE_ACTIVITY(beentry);
}

/* ----------
 * pgstat_read_current_status() -
 *
 *	Copy the current contents of the PgBackendStatus array to local memory,
 *	if not already done in this transaction.
 * ----------
 */
static void
pgstat_read_current_status(void)
{
	volatile PgBackendStatus *beentry;
	LocalPgBackendStatus *localtable;
	LocalPgBackendStatus *localentry;
	char	   *localappname,
			   *localclienthostname,
			   *localactivity;
#ifdef USE_SSL
	PgBackendSSLStatus *localsslstatus;
#endif
#ifdef ENABLE_GSS
	PgBackendGSSStatus *localgssstatus;
#endif
	int			i;

	if (localBackendStatusTable)
		return;					/* already done */

	pgstat_setup_backend_status_context();

	/*
	 * Allocate storage for local copy of state data.  We can presume that
	 * none of these requests overflow size_t, because we already calculated
	 * the same values using mul_size during shmem setup.  However, with
	 * probably-silly values of pgstat_track_activity_query_size and
	 * max_connections, the localactivity buffer could exceed 1GB, so use
	 * "huge" allocation for that one.
	 */
	localtable = (LocalPgBackendStatus *)
		MemoryContextAlloc(backendStatusSnapContext,
						   sizeof(LocalPgBackendStatus) * NumBackendStatSlots);
	localappname = (char *)
		MemoryContextAlloc(backendStatusSnapContext,
						   NAMEDATALEN * NumBackendStatSlots);
	localclienthostname = (char *)
		MemoryContextAlloc(backendStatusSnapContext,
						   NAMEDATALEN * NumBackendStatSlots);
	localactivity = (char *)
		MemoryContextAllocHuge(backendStatusSnapContext,
							   pgstat_track_activity_query_size * NumBackendStatSlots);
#ifdef USE_SSL
	localsslstatus = (PgBackendSSLStatus *)
		MemoryContextAlloc(backendStatusSnapContext,
						   sizeof(PgBackendSSLStatus) * NumBackendStatSlots);
#endif
#ifdef ENABLE_GSS
	localgssstatus = (PgBackendGSSStatus *)
		MemoryContextAlloc(backendStatusSnapContext,
						   sizeof(PgBackendGSSStatus) * NumBackendStatSlots);
#endif

	localNumBackends = 0;

	beentry = BackendStatusArray;
	localentry = localtable;
	for (i = 1; i <= NumBackendStatSlots; i++)
	{
		/*
		 * Follow the protocol of retrying if st_changecount changes while we
		 * copy the entry, or if it's odd.  (The check for odd is needed to
		 * cover the case where we are able to completely copy the entry while
		 * the source backend is between increment steps.)	We use a volatile
		 * pointer here to ensure the compiler doesn't try to get cute.
		 */
		int			attempt = 1;

		while (yb_pgstat_log_read_activity(beentry, ++attempt))
		{
			int			before_changecount;
			int			after_changecount;

			pgstat_begin_read_activity(beentry, before_changecount);

			localentry->backendStatus.st_procpid = beentry->st_procpid;
			/* Skip all the data-copying work if entry is not in use */
			if (localentry->backendStatus.st_procpid > 0)
			{
				memcpy(&localentry->backendStatus, unvolatize(PgBackendStatus *, beentry), sizeof(PgBackendStatus));

				/*
				 * For each PgBackendStatus field that is a pointer, copy the
				 * pointed-to data, then adjust the local copy of the pointer
				 * field to point at the local copy of the data.
				 *
				 * strcpy is safe even if the string is modified concurrently,
				 * because there's always a \0 at the end of the buffer.
				 */
				strcpy(localappname, (char *) beentry->st_appname);
				localentry->backendStatus.st_appname = localappname;
				strcpy(localclienthostname, (char *) beentry->st_clienthostname);
				localentry->backendStatus.st_clienthostname = localclienthostname;
				strcpy(localactivity, (char *) beentry->st_activity_raw);
				localentry->backendStatus.st_activity_raw = localactivity;
#ifdef USE_SSL
				if (beentry->st_ssl)
				{
					memcpy(localsslstatus, beentry->st_sslstatus, sizeof(PgBackendSSLStatus));
					localentry->backendStatus.st_sslstatus = localsslstatus;
				}
#endif
#ifdef ENABLE_GSS
				if (beentry->st_gss)
				{
					memcpy(localgssstatus, beentry->st_gssstatus, sizeof(PgBackendGSSStatus));
					localentry->backendStatus.st_gssstatus = localgssstatus;
				}
#endif
			}

			pgstat_end_read_activity(beentry, after_changecount);

			if (pgstat_read_activity_complete(before_changecount,
											  after_changecount))
				break;

			/* Make sure we can break out of loop if stuck... */
			CHECK_FOR_INTERRUPTS();
		}

		beentry++;
		/* Only valid entries get included into the local array */
		if (localentry->backendStatus.st_procpid > 0)
		{
			BackendIdGetTransactionIds(i,
									   &localentry->backend_xid,
									   &localentry->backend_xmin);

			if (YBIsEnabledInPostgresEnvVar())
			{
				localentry->yb_backend_rss_mem_bytes =
					YbPgGetCurRSSMemUsage(localentry->backendStatus.st_procpid);
			}
			localentry++;
			localappname += NAMEDATALEN;
			localclienthostname += NAMEDATALEN;
			localactivity += pgstat_track_activity_query_size;
#ifdef USE_SSL
			localsslstatus++;
#endif
#ifdef ENABLE_GSS
			localgssstatus++;
#endif
			localNumBackends++;
		}
	}

	/* Set the pointer only after completion of a valid table */
	localBackendStatusTable = localtable;
}


/* ----------
 * pgstat_get_backend_current_activity() -
 *
 *	Return a string representing the current activity of the backend with
 *	the specified PID.  This looks directly at the BackendStatusArray,
 *	and so will provide current information regardless of the age of our
 *	transaction's snapshot of the status array.
 *
 *	It is the caller's responsibility to invoke this only for backends whose
 *	state is expected to remain stable while the result is in use.  The
 *	only current use is in deadlock reporting, where we can expect that
 *	the target backend is blocked on a lock.  (There are corner cases
 *	where the target's wait could get aborted while we are looking at it,
 *	but the very worst consequence is to return a pointer to a string
 *	that's been changed, so we won't worry too much.)
 *
 *	Note: return strings for special cases match pg_stat_get_backend_activity.
 * ----------
 */
const char *
pgstat_get_backend_current_activity(int pid, bool checkUser)
{
	PgBackendStatus *beentry;
	int			i;

	beentry = BackendStatusArray;
	for (i = 1; i <= MaxBackends; i++)
	{
		/*
		 * Although we expect the target backend's entry to be stable, that
		 * doesn't imply that anyone else's is.  To avoid identifying the
		 * wrong backend, while we check for a match to the desired PID we
		 * must follow the protocol of retrying if st_changecount changes
		 * while we examine the entry, or if it's odd.  (This might be
		 * unnecessary, since fetching or storing an int is almost certainly
		 * atomic, but let's play it safe.)  We use a volatile pointer here to
		 * ensure the compiler doesn't try to get cute.
		 */
		volatile PgBackendStatus *vbeentry = beentry;
		bool		found;

		int			attempt = 1;

		while (yb_pgstat_log_read_activity(vbeentry, ++attempt))
		{
			int			before_changecount;
			int			after_changecount;

			pgstat_begin_read_activity(vbeentry, before_changecount);

			found = (vbeentry->st_procpid == pid);

			pgstat_end_read_activity(vbeentry, after_changecount);

			if (pgstat_read_activity_complete(before_changecount,
											  after_changecount))
				break;

			/* Make sure we can break out of loop if stuck... */
			CHECK_FOR_INTERRUPTS();
		}

		if (found)
		{
			/* Now it is safe to use the non-volatile pointer */
			if (checkUser && !superuser() && beentry->st_userid != GetUserId())
				return "<insufficient privilege>";
			else if (*(beentry->st_activity_raw) == '\0')
				return "<command string not enabled>";
			else
			{
				/* this'll leak a bit of memory, but that seems acceptable */
				return pgstat_clip_activity(beentry->st_activity_raw);
			}
		}

		beentry++;
	}

	/* If we get here, caller is in error ... */
	return "<backend information not available>";
}

/* ----------
 * pgstat_get_crashed_backend_activity() -
 *
 *	Return a string representing the current activity of the backend with
 *	the specified PID.  Like the function above, but reads shared memory with
 *	the expectation that it may be corrupt.  On success, copy the string
 *	into the "buffer" argument and return that pointer.  On failure,
 *	return NULL.
 *
 *	This function is only intended to be used by the postmaster to report the
 *	query that crashed a backend.  In particular, no attempt is made to
 *	follow the correct concurrency protocol when accessing the
 *	BackendStatusArray.  But that's OK, in the worst case we'll return a
 *	corrupted message.  We also must take care not to trip on ereport(ERROR).
 * ----------
 */
const char *
pgstat_get_crashed_backend_activity(int pid, char *buffer, int buflen)
{
	volatile PgBackendStatus *beentry;
	int			i;

	beentry = BackendStatusArray;

	/*
	 * We probably shouldn't get here before shared memory has been set up,
	 * but be safe.
	 */
	if (beentry == NULL || BackendActivityBuffer == NULL)
		return NULL;

	for (i = 1; i <= MaxBackends; i++)
	{
		if (beentry->st_procpid == pid)
		{
			/* Read pointer just once, so it can't change after validation */
			const char *activity = beentry->st_activity_raw;
			const char *activity_last;

			/*
			 * We mustn't access activity string before we verify that it
			 * falls within the BackendActivityBuffer. To make sure that the
			 * entire string including its ending is contained within the
			 * buffer, subtract one activity length from the buffer size.
			 */
			activity_last = BackendActivityBuffer + BackendActivityBufferSize
				- pgstat_track_activity_query_size;

			if (activity < BackendActivityBuffer ||
				activity > activity_last)
				return NULL;

			/* If no string available, no point in a report */
			if (activity[0] == '\0')
				return NULL;

			/*
			 * Copy only ASCII-safe characters so we don't run into encoding
			 * problems when reporting the message; and be sure not to run off
			 * the end of memory.  As only ASCII characters are reported, it
			 * doesn't seem necessary to perform multibyte aware clipping.
			 */
			ascii_safe_strlcpy(buffer, activity,
							   Min(buflen, pgstat_track_activity_query_size));

			return buffer;
		}

		beentry++;
	}

	/* PID not found */
	return NULL;
}

/* ----------
 * pgstat_get_my_query_id() -
 *
 * Return current backend's query identifier.
 */
uint64
pgstat_get_my_query_id(void)
{
	if (!MyBEEntry)
		return 0;

	/*
	 * There's no need for a lock around pgstat_begin_read_activity /
	 * pgstat_end_read_activity here as it's only called from
	 * pg_stat_get_activity which is already protected, or from the same
	 * backend which means that there won't be concurrent writes.
	 */
	return MyBEEntry->st_query_id;
}


/* ----------
 * pgstat_fetch_stat_beentry() -
 *
 *	Support function for the SQL-callable pgstat* functions. Returns
 *	our local copy of the current-activity entry for one backend.
 *
 *	NB: caller is responsible for a check if the user is permitted to see
 *	this info (especially the querystring).
 * ----------
 */
PgBackendStatus *
pgstat_fetch_stat_beentry(int beid)
{
	pgstat_read_current_status();

	if (beid < 1 || beid > localNumBackends)
		return NULL;

	return &localBackendStatusTable[beid - 1].backendStatus;
}


/* ----------
 * pgstat_fetch_stat_local_beentry() -
 *
 *	Like pgstat_fetch_stat_beentry() but with locally computed additions (like
 *	xid and xmin values of the backend)
 *
 *	NB: caller is responsible for a check if the user is permitted to see
 *	this info (especially the querystring).
 * ----------
 */
LocalPgBackendStatus *
pgstat_fetch_stat_local_beentry(int beid)
{
	pgstat_read_current_status();

	if (beid < 1 || beid > localNumBackends)
		return NULL;

	return &localBackendStatusTable[beid - 1];
}


/* ----------
 * pgstat_fetch_stat_numbackends() -
 *
 *	Support function for the SQL-callable pgstat* functions. Returns
 *	the maximum current backend id.
 * ----------
 */
int
pgstat_fetch_stat_numbackends(void)
{
	pgstat_read_current_status();

	return localNumBackends;
}

/*
 * Convert a potentially unsafely truncated activity string (see
 * PgBackendStatus.st_activity_raw's documentation) into a correctly truncated
 * one.
 *
 * The returned string is allocated in the caller's memory context and may be
 * freed.
 */
char *
pgstat_clip_activity(const char *raw_activity)
{
	char	   *activity;
	int			rawlen;
	int			cliplen;

	/*
	 * Some callers, like pgstat_get_backend_current_activity(), do not
	 * guarantee that the buffer isn't concurrently modified. We try to take
	 * care that the buffer is always terminated by a NUL byte regardless, but
	 * let's still be paranoid about the string's length. In those cases the
	 * underlying buffer is guaranteed to be pgstat_track_activity_query_size
	 * large.
	 */
	activity = pnstrdup(raw_activity, pgstat_track_activity_query_size - 1);

	/* now double-guaranteed to be NUL terminated */
	rawlen = strlen(activity);

	/*
	 * All supported server-encodings make it possible to determine the length
	 * of a multi-byte character from its first byte (this is not the case for
	 * client encodings, see GB18030). As st_activity is always stored using
	 * server encoding, this allows us to perform multi-byte aware truncation,
	 * even if the string earlier was truncated in the middle of a multi-byte
	 * character.
	 */
	cliplen = pg_mbcliplen(activity, rawlen,
						   pgstat_track_activity_query_size - 1);

	activity[cliplen] = '\0';

	return activity;
}

/*
 * When backends die due to abnormal termination, cleanup is required.
 *
 * This function performs all the operations done by pgstat_beshutdown_hook,
 * which is executed during safe backend terminations. However, it does not
 * report the remaining stats to the pgstat collector as the backend has
 * already died.
 */
void
yb_pgstat_clear_entry_pid(int pid)
{
	PgBackendStatus *beentry;
	int			i;

	beentry = BackendStatusArray;
	for (i = 1; i <= MaxBackends; i++)
	{
		volatile PgBackendStatus *vbeentry = beentry;

		if (pid == vbeentry->st_procpid)
		{
			PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);
			beentry->st_procpid = 0;	/* mark invalid */
			beentry->yb_session_id = 0;
			PGSTAT_END_WRITE_ACTIVITY(beentry);
			return;
		}
		beentry++;
	}
}

/* ----------
 * yb_pgstat_report_allocated_mem_bytes() -
 *
 *	Called from utils/mmgr/mcxt.c to update our allocated memory measurement
 *	value
 * ----------
 */
void
yb_pgstat_report_allocated_mem_bytes(void)
{
	volatile PgBackendStatus *beentry = MyBEEntry;

	if (!beentry)
		return;

	PGSTAT_BEGIN_WRITE_ACTIVITY(beentry);

	beentry->yb_st_allocated_mem_bytes = PgMemTracker.backend_cur_allocated_mem_bytes;

	PGSTAT_END_WRITE_ACTIVITY(beentry);
}

/* ----------
 * yb_pgstat_set_catalog_version() -
 *
 *		Set yb_st_catalog_version.version for my backend
 * ----------
 */
void
yb_pgstat_set_catalog_version(uint64_t catalog_version)
{
	volatile PgBackendStatus *vbeentry = MyBEEntry;

	PGSTAT_BEGIN_WRITE_ACTIVITY(vbeentry);

	vbeentry->yb_st_catalog_version.version = catalog_version;

	PGSTAT_END_WRITE_ACTIVITY(vbeentry);
}

/* ----------
 * yb_pgstat_set_has_catalog_version() -
 *
 *		Set yb_st_catalog_version.has_version for my backend
 * ----------
 */
void
yb_pgstat_set_has_catalog_version(bool has_version)
{
	volatile PgBackendStatus *vbeentry = MyBEEntry;

	PGSTAT_BEGIN_WRITE_ACTIVITY(vbeentry);

	vbeentry->yb_st_catalog_version.has_version = has_version;

	PGSTAT_END_WRITE_ACTIVITY(vbeentry);
}

void
yb_pgstat_add_session_info(uint64_t session_id)
{
	volatile PgBackendStatus *vbeentry = NULL;

	/*
	 * This code could be invoked either in a regular backend or in an
	 * auxiliary process. In case of the latter, skip initializing shared
	 * memory context. See note in pgstat_initalize()
	 */
	if (MyBEEntry == NULL)
	{
		/* Must be an auxiliary process */
		Assert(MyAuxProcType != NotAnAuxProcess);
		return;
	}

	vbeentry = MyBEEntry;

	PGSTAT_BEGIN_WRITE_ACTIVITY(vbeentry);

	vbeentry->yb_session_id = session_id;

	PGSTAT_END_WRITE_ACTIVITY(vbeentry);
}

bool
yb_pgstat_log_read_activity(volatile PgBackendStatus *beentry, int attempt)
{
	if (attempt >= YB_MAX_BEENTRIES_ATTEMPTS)
	{
		elog(WARNING, "backend status entry for pid %d required %d "
			 "attempts, using inconsistent results",
			 (beentry)->st_procpid, attempt);
		return false;
	}
	if (attempt % YB_BEENTRY_LOGGING_INTERVAL == 0)
		elog(WARNING, "backend status entry for pid %d required %d "
			 "attempts, continuing to retry",
			 (beentry)->st_procpid, attempt);
	return true;
}

PgBackendStatus *
getBackendStatusArray(void)
{
	return BackendStatusArray;
}
