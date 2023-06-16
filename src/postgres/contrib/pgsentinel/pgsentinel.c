/*
 * pgsentinel.c
 *   Track active session history.
 *
 * Copyright (c) 2018, PgSentinel
 *
 * IDENTIFICATION:
 * https://github.com/pgsentinel/pgsentinel
 *
 * LICENSE: GNU Affero General Public License v3.0
 */

#include "pgsentinel.h"
#include "postgres.h"
#include "fmgr.h"
#include "access/xact.h"
#include "lib/stringinfo.h"
#include "pgstat.h"
#include "executor/spi.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "utils/guc.h"
#include "utils/snapmgr.h"
#include "miscadmin.h"
#include "storage/spin.h"
#include "utils/date.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "funcapi.h"
#include "optimizer/planner.h"
#include "storage/shm_toc.h"
#include "access/twophase.h"
#include "parser/analyze.h"
#include "parser/scansup.h"
#include "access/hash.h"
#include "commands/extension.h"
#include "catalog/namespace.h"

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(pg_active_session_history);
PG_FUNCTION_INFO_V1(pg_stat_statements_history);

#define PG_ACTIVE_SESSION_HISTORY_COLS        28
#define PG_STAT_STATEMENTS_HISTORY_COLS       24
#define EXTENSION_NAME "pgsentinel"

/* Entry point of library loading */
void _PG_init(void);
void _PG_fini(void);
void pgsentinel_main(Datum);

/* Signal handling */
static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

/* Saved hook values in case of unload */
static shmem_startup_hook_type ash_prev_shmem_startup_hook = NULL;
#if (PG_VERSION_NUM >= 150000)
static shmem_request_hook_type ash_prev_shmem_request_hook = NULL;
static void ash_shmem_request(void);
#endif

/* Our hooks */
static void ash_shmem_startup(void);
static void ash_shmem_shutdown(int code, Datum arg);

/* GUC variables */
static int ash_sampling_period = 1;
static int ash_max_entries = 1000;
static int pgssh_max_entries = 10000;

static bool pgssh_enable = true;
static bool ash_track_idle_trans = true;
static int ash_restart_wait_time = 2;
char *pgsentinelDbName = "yugabyte";

/* Worker name */
static char *worker_name = "pgsentinel";

/* pg_stat_activity query */
static const char * const pgsa_query_no_track_idle=
#if (PG_VERSION_NUM / 100 ) == 906
"select act.datid, act.datname, act.pid, act.usesysid, act.usename, \
 act.application_name, text(act.client_addr), act.client_hostname, \
 act.client_port, act.backend_start, act.xact_start, act.query_start, \
 act.state_change, case when act.wait_event_type is null then 'CPU'  \
 else act.wait_event_type end as wait_event_type,case when act.wait_event is null \
 then 'CPU' else act.wait_event end as wait_event, act.state, act.backend_xid, \
 act.backend_xmin, act.query,(pg_blocking_pids(act.pid))[1], \
 cardinality(pg_blocking_pids(act.pid)),blk.state,gpi.* \
 from pg_stat_activity act left join pg_stat_activity blk  \
 on (pg_blocking_pids(act.pid))[1] = blk.pid,get_parsedinfo(act.pid) gpi \
 where act.state ='active' and act.pid != pg_backend_pid()";
#elif PG_VERSION_NUM < 130000
"select act.datid, act.datname, act.pid, act.usesysid, act.usename, \
 act.application_name, text(act.client_addr), act.client_hostname, \
 act.client_port, act.backend_start, act.xact_start, act.query_start,  \
 act.state_change, case when act.wait_event_type is null then 'CPU' \
 else act.wait_event_type end as wait_event_type,case when act.wait_event is null \
 then 'CPU' else act.wait_event end as wait_event, act.state, act.backend_xid, \
 act.backend_xmin, act.query, act.backend_type,(pg_blocking_pids(act.pid))[1], \
 cardinality(pg_blocking_pids(act.pid)),blk.state,gpi.* \
 from pg_stat_activity act left join pg_stat_activity blk  \
 on (pg_blocking_pids(act.pid))[1] = blk.pid,get_parsedinfo(act.pid) gpi \
 where act.state ='active' and act.pid != pg_backend_pid()";
#else
"select act.datid, act.datname, act.pid, act.usesysid, act.usename, \
 act.application_name, text(act.client_addr), act.client_hostname, \
 act.client_port, act.backend_start, act.xact_start, act.query_start,  \
 act.state_change, case when act.wait_event_type is null then 'CPU' \
 else act.wait_event_type end as wait_event_type,case when act.wait_event is null \
 then 'CPU' else act.wait_event end as wait_event, act.state, act.backend_xid, \
 act.backend_xmin, act.query, act.backend_type,(pg_blocking_pids(act.pid))[1], \
 cardinality(pg_blocking_pids(act.pid)),blk.state,gpi.*, act.leader_pid \
 from pg_stat_activity act left join pg_stat_activity blk  \
 on (pg_blocking_pids(act.pid))[1] = blk.pid,get_parsedinfo(act.pid) gpi \
 where act.state ='active' and act.pid != pg_backend_pid()";
#endif

static const char * const pgsa_query_track_idle=
#if (PG_VERSION_NUM / 100 ) == 906
"select act.datid, act.datname, act.pid, act.usesysid, act.usename, \
 act.application_name, text(act.client_addr), act.client_hostname, \
 act.client_port, act.backend_start, act.xact_start, act.query_start, \
 act.state_change, case when act.wait_event_type is null then 'CPU'  \
 else act.wait_event_type end as wait_event_type,case when act.wait_event is null \
 then 'CPU' else act.wait_event end as wait_event, act.state, act.backend_xid, \
 act.backend_xmin, act.query,(pg_blocking_pids(act.pid))[1], \
 cardinality(pg_blocking_pids(act.pid)),blk.state,gpi.* \
 from pg_stat_activity act left join pg_stat_activity blk  \
 on (pg_blocking_pids(act.pid))[1] = blk.pid,get_parsedinfo(act.pid) gpi \
 where act.state in ('active', 'idle in transaction') and act.pid != pg_backend_pid()";
#elif PG_VERSION_NUM < 130000
"select act.datid, act.datname, act.pid, act.usesysid, act.usename, \
 act.application_name, text(act.client_addr), act.client_hostname, \
 act.client_port, act.backend_start, act.xact_start, act.query_start,  \
 act.state_change, case when act.wait_event_type is null then 'CPU' \
 else act.wait_event_type end as wait_event_type,case when act.wait_event is null \
 then 'CPU' else act.wait_event end as wait_event, act.state, act.backend_xid, \
 act.backend_xmin, act.query, act.backend_type,(pg_blocking_pids(act.pid))[1], \
 cardinality(pg_blocking_pids(act.pid)),blk.state,gpi.* \
 from pg_stat_activity act left join pg_stat_activity blk  \
 on (pg_blocking_pids(act.pid))[1] = blk.pid,get_parsedinfo(act.pid) gpi \
 where act.state in ('active', 'idle in transaction') and act.pid != pg_backend_pid()";
#else
"select act.datid, act.datname, act.pid, act.usesysid, act.usename, \
 act.application_name, text(act.client_addr), act.client_hostname, \
 act.client_port, act.backend_start, act.xact_start, act.query_start,  \
 act.state_change, case when act.wait_event_type is null then 'CPU' \
 else act.wait_event_type end as wait_event_type,case when act.wait_event is null \
 then 'CPU' else act.wait_event end as wait_event, act.state, act.backend_xid, \
 act.backend_xmin, act.query, act.backend_type,(pg_blocking_pids(act.pid))[1], \
 cardinality(pg_blocking_pids(act.pid)),blk.state,gpi.*, act.leader_pid \
 from pg_stat_activity act left join pg_stat_activity blk  \
 on (pg_blocking_pids(act.pid))[1] = blk.pid,get_parsedinfo(act.pid) gpi \
 where act.state in ('active', 'idle in transaction') and act.pid != pg_backend_pid()";
#endif

static const char * const pg_stat_statements_query=
#if PG_VERSION_NUM < 130000
"select userid, dbid, queryid, calls, total_time, rows, shared_blks_hit, \
 shared_blks_read, shared_blks_dirtied, shared_blks_written, local_blks_hit, \
 local_blks_read, local_blks_dirtied, local_blks_written, temp_blks_read, \
 temp_blks_written, blk_read_time, blk_write_time from pg_stat_statements \
 where queryid in  (select queryid from pg_active_session_history  \
 where ash_time in (select ash_time from pg_active_session_history  \
 order by ash_time desc limit 1))";
#else
"select userid, dbid, queryid, calls, total_exec_time, rows, shared_blks_hit, \
 shared_blks_read, shared_blks_dirtied, shared_blks_written, local_blks_hit, \
 local_blks_read, local_blks_dirtied, local_blks_written, temp_blks_read, \
 temp_blks_written, blk_read_time, blk_write_time, \
 plans, total_plan_time, wal_records, wal_fpi, wal_bytes \
 from pg_stat_statements \
 where queryid in  (select queryid from pg_active_session_history  \
 where ash_time in (select ash_time from pg_active_session_history  \
 order by ash_time desc limit 1))";
#endif

static void pg_active_session_history_internal(FunctionCallInfo fcinfo);
static void pg_stat_statements_history_internal(FunctionCallInfo fcinfo);

procEntry *ProcEntryArray = NULL;
post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;

/* ash entry */
typedef struct ashEntry
{
	int pid;
#if PG_VERSION_NUM >= 130000
	int leader_pid;
#endif
	int client_port;
	uint64 queryid;
	TimestampTz ash_time;
	Oid datid;
	Oid usesysid;
	char *usename;
	char *datname;
	char *application_name;
	char *wait_event_type;
	char *wait_event;
	char *state;
	char *blocker_state;
	char *client_hostname;
	int blockers;
	int blockerpid;
	char *top_level_query;
	char *query;
	char *cmdtype;
	char *backend_type;
	char *client_addr;
	TransactionId backend_xmin;
	TransactionId backend_xid;
	TimestampTz backend_start;
	TimestampTz xact_start;
	TimestampTz query_start;
	TimestampTz state_change;
} ashEntry;

/* pg_stat_statement_history entry */
typedef struct pgsshEntry
{
	TimestampTz ash_time;
	Oid userid;
	Oid dbid;
	uint64 queryid;
	int64 calls;
	double total_time;
	int64 rows;
	int64 shared_blks_hit;
	int64 shared_blks_read;
	int64 shared_blks_dirtied;
	int64 shared_blks_written;
	int64 local_blks_hit;
	int64 local_blks_read;
	int64 local_blks_dirtied;
	int64 local_blks_written;
	int64 temp_blks_read;
	int64 temp_blks_written;
	double blk_read_time;
	double blk_write_time;
#if PG_VERSION_NUM >= 130000
	int64 plans;
	double total_plan_time;
	int64 wal_records;
	int64 wal_fpi;
	uint64 wal_bytes;
#endif
} pgsshEntry;

/* counters */
typedef struct intEntry
{
	int inserted;
	int pgsshinserted;
} intEntry;

/* For shared memory */
static char *AshEntryUsenameBuffer = NULL;
static char *AshEntryDatnameBuffer = NULL;
static char *AshEntryAppnameBuffer = NULL;
static char *AshEntryWaitEventTypeBuffer = NULL;
static char *AshEntryWaitEventBuffer = NULL;
static char *AshEntryClientHostnameBuffer = NULL;
static char *AshEntryTopLevelQueryBuffer = NULL;
static char *AshEntryQueryBuffer = NULL;
static char *AshEntryCmdTypeBuffer = NULL;
static char *AshEntryBackendTypeBuffer = NULL;
static char *AshEntryStateBuffer = NULL;
static char *AshEntryBlockerStateBuffer = NULL;
static char *AshEntryClientaddrBuffer = NULL;
static ashEntry *AshEntryArray = NULL;
static intEntry *IntEntryArray = NULL;
static pgsshEntry *PgsshEntryArray = NULL;
static char *ProcQueryBuffer = NULL;
static char *ProcCmdTypeBuffer = NULL;

/* Estimate amount of shared memory needed */
static Size ash_entry_memsize(void);

/* check extension is loaded/present */
static bool PgSentinelHasBeenLoaded(void);

/* store ash entry */
static void ash_entry_store(TimestampTz ash_time,const int pid,
#if PG_VERSION_NUM >= 130000
							int leader_pid,
#endif
							const char *usename, const int client_port,Oid datid,
							const char *datname, const char *application_name,
							const char *client_addr, TransactionId backend_xmin,
							TimestampTz backend_start, TimestampTz xact_start,
							TimestampTz query_start, TimestampTz state_change,
							const char *wait_event_type, const char *wait_event,
							const char *state, const char *client_hostname,
							const char *query, const char *backend_type,
							Oid usesysid, TransactionId backend_xid,
							int blockers, int blockerpid,
							const char *blocker_state, uint64 queryid,
							const char *gpi_query, const char *cmdtype);

/* prepare store ash */
static void ash_prepare_store(TimestampTz ash_time,const int pid,
#if PG_VERSION_NUM >= 130000
								int leader_pid,
#endif
								const char* usename,const int client_port,
								Oid datid, const char *datname,
								const char *application_name,
								const char *client_addr,
								TransactionId backend_xmin,
								TimestampTz backend_start,
								TimestampTz xact_start, TimestampTz query_start,
								TimestampTz state_change,
								const char *wait_event_type,
								const char *wait_event, const char *state,
								const char *client_hostname, const char *query,
								const char *backend_type, Oid usesysid,
								TransactionId backend_xid, int blockers,
								int blockerpid, const char *blocker_state,
								uint64 queryid, const char *gpi_query,
								const char *cmdtype);

/* Estimate amount of shared memory needed for ash entry */
static Size
ash_entry_memsize(void)
{
	Size            size;

	/* AshEntryArray */
	size = mul_size(sizeof(ashEntry), ash_max_entries);
	/* AshEntryUsenameBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryDatnameBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryAppnameBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryClientaddrBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryWaitEventTypeBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryWaitEventBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryStateBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryClientHostnameBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryQueryBuffer */
	size = add_size(size, mul_size(pgstat_track_activity_query_size,
															ash_max_entries));
	/* AshEntryCmdTypeBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryTopLevelQueryBuffer */
	size = add_size(size, mul_size(pgstat_track_activity_query_size,
															ash_max_entries));
	/* AshEntryBackendTypeBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));
	/* AshEntryBlockerStateBuffer */
	size = add_size(size, mul_size(NAMEDATALEN, ash_max_entries));

	return size;
}

/* Estimate amount of shared memory needed for int entry*/
static Size
int_entry_memsize(void)
{
	Size            size;
	/* IntEntryArray */
	size = mul_size(sizeof(intEntry), 1);
	return size;
}

/* Estimate amount of shared memory needed for pgssh entry*/
static Size
pgssh_entry_memsize(void)
{
	Size            size;
	/* PgsshEntryArray */
	size = mul_size(sizeof(pgsshEntry), pgssh_max_entries);
	return size;
}

static void
ash_shmem_startup(void)
{

	Size size;
	bool   found;
	char   *buffer;
	int    i;

	if (ash_prev_shmem_startup_hook)
		ash_prev_shmem_startup_hook();

	size = mul_size(sizeof(ashEntry), ash_max_entries);
	AshEntryArray = (ashEntry *) ShmemInitStruct("Ash Entry Array", size,
																		&found);

	if (!found)
		MemSet(AshEntryArray, 0, size);

	size = mul_size(sizeof(intEntry), 1);
	IntEntryArray = (intEntry *) ShmemInitStruct("int Entry Array", size,
																		&found);

	if (!found)
	{
		MemSet(IntEntryArray, 0, size);
		IntEntryArray[0].inserted=0;
		IntEntryArray[0].pgsshinserted=0;
	}

	if (pgssh_enable)
	{
		size = mul_size(sizeof(pgsshEntry), pgssh_max_entries);
		PgsshEntryArray = (pgsshEntry *) ShmemInitStruct("pgssh Entry Array",
																size, &found);

		if (!found)
			MemSet(PgsshEntryArray, 0, size);
	}

	size = mul_size(sizeof(procEntry), get_max_procs_count());
	ProcEntryArray = (procEntry *) ShmemInitStruct("Get_parsedinfo Proc Entry",
																size, &found);

	if (!found)
	{
		MemSet(ProcEntryArray, 0, size);
	}

	size = mul_size(pgstat_track_activity_query_size, get_max_procs_count());
	ProcQueryBuffer = (char *) ShmemInitStruct("Proc Query Buffer", size,
																	&found);

	if (!found)
	{
		MemSet(ProcQueryBuffer, 0, size);

		/* Initialize pointers. */
		buffer = ProcQueryBuffer;
		for (i = 0; i < get_max_procs_count(); i++)
		{
			ProcEntryArray[i].query= buffer;
			buffer += pgstat_track_activity_query_size;
		}
	}

	size = mul_size(NAMEDATALEN, get_max_procs_count());
	ProcCmdTypeBuffer = (char *) ShmemInitStruct("Proc CmdType Buffer", size,
																		&found);

	if (!found)
	{
		MemSet(ProcCmdTypeBuffer, 0, size);

		/* Initialize pointers. */
		buffer = ProcCmdTypeBuffer;
		for (i = 0; i < get_max_procs_count(); i++)
		{
			ProcEntryArray[i].cmdtype= buffer;
			buffer += NAMEDATALEN;
		}
	}

	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryUsenameBuffer = (char *)
		ShmemInitStruct("Ash Entry useName Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryUsenameBuffer, 0, size);

		/* Initialize usename pointers. */
		buffer = AshEntryUsenameBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].usename = buffer;
			buffer += NAMEDATALEN;
		}
	}


	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryDatnameBuffer = (char *)
		ShmemInitStruct("Ash Entry DatName Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryDatnameBuffer, 0, size);

		/* Initialize Datname pointers. */
		buffer = AshEntryDatnameBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].datname = buffer;
			buffer += NAMEDATALEN;
		}
	}


	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryAppnameBuffer = (char *)
		ShmemInitStruct("Ash Entry AppName Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryAppnameBuffer, 0, size);

		/* Initialize Appname pointers. */
		buffer = AshEntryAppnameBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].application_name = buffer;
			buffer += NAMEDATALEN;
		}
	}

	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryClientaddrBuffer = (char *)
		ShmemInitStruct("Ash Entry Client Addr Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryClientaddrBuffer, 0, size);

		/* Initialize Client Addr pointers. */
		buffer = AshEntryClientaddrBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].client_addr = buffer;
			buffer += NAMEDATALEN;
		}
	}

	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryWaitEventTypeBuffer = (char *)
		ShmemInitStruct("Ash Entry Wait event type Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryWaitEventTypeBuffer, 0, size);

		/* Initialize wait event type pointers. */
		buffer = AshEntryWaitEventTypeBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].wait_event_type = buffer;
			buffer += NAMEDATALEN;
		}
	}

	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryWaitEventBuffer = (char *)
		ShmemInitStruct("Ash Entry Wait Event Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryWaitEventBuffer, 0, size);

		/* Initialize wait event pointers. */
		buffer = AshEntryWaitEventBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].wait_event = buffer;
			buffer += NAMEDATALEN;
		}
	}


	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryStateBuffer = (char *)
		ShmemInitStruct("Ash Entry State Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryStateBuffer, 0, size);

		/* Initialize state pointers. */
		buffer = AshEntryStateBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].state = buffer;
			buffer += NAMEDATALEN;
		}
	}

	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryClientHostnameBuffer = (char *)
		ShmemInitStruct("Ash Entry Client Hostname Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryClientHostnameBuffer, 0, size);

		/* Initialize client hostname pointers. */
		buffer = AshEntryClientHostnameBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].client_hostname = buffer;
			buffer += NAMEDATALEN;
		}
	}

	size = mul_size(pgstat_track_activity_query_size, ash_max_entries);
	AshEntryTopLevelQueryBuffer = (char *)
		ShmemInitStruct("Ash Entry Top Level Query Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryTopLevelQueryBuffer, 0, size);

		/* Initialize top level query pointers. */
		buffer = AshEntryTopLevelQueryBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].top_level_query = buffer;
			buffer += pgstat_track_activity_query_size;
		}
	}

	size = mul_size(pgstat_track_activity_query_size, ash_max_entries);
	AshEntryQueryBuffer = (char *)
		ShmemInitStruct("Ash Entry Query Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryQueryBuffer, 0, size);

		/* Initialize query pointers. */
		buffer = AshEntryQueryBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].query = buffer;
			buffer += pgstat_track_activity_query_size;
		}
	}

	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryCmdTypeBuffer = (char *)
		ShmemInitStruct("Ash Entry CmdType Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryCmdTypeBuffer, 0, size);

		/* Initialize cmdtype pointers. */
		buffer = AshEntryCmdTypeBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].cmdtype = buffer;
			buffer += NAMEDATALEN;
		}
	}


	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryBackendTypeBuffer = (char *)
		ShmemInitStruct("Ash Entry Backend Type Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryBackendTypeBuffer, 0, size);

		/* Initialize backend type pointers. */
		buffer = AshEntryBackendTypeBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].backend_type = buffer;
			buffer += NAMEDATALEN;
		}
	}

	size = mul_size(NAMEDATALEN, ash_max_entries);
	AshEntryBlockerStateBuffer = (char *)
		ShmemInitStruct("Ash Entry Blocker State Buffer", size, &found);

	if (!found)
	{
		MemSet(AshEntryBlockerStateBuffer, 0, size);

		/* Initialize state pointers. */
		buffer = AshEntryBlockerStateBuffer;
		for (i = 0; i < ash_max_entries; i++)
		{
			AshEntryArray[i].blocker_state = buffer;
			buffer += NAMEDATALEN;
		}
	}

	/*
	 * set up a shmem exit hook to do whatever useful (dump to disk later on?).
	 */
	if (!IsUnderPostmaster)
		on_shmem_exit(ash_shmem_shutdown, (Datum) 0);


	if (found)
		return;
}

static void
ash_shmem_shutdown(int code, Datum arg)
{
	/* Don't try to dump during a crash. */
	if (code)
		return;

	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (!AshEntryArray)
		return;

	/* dump to disk ?*/
	/* This is the place */
}


static void
pgsentinel_sigterm(SIGNAL_ARGS)
{
	int save_errno = errno;
	got_sigterm = true;
#if PG_VERSION_NUM >= 100000
	SetLatch(MyLatch);
#else
	if (MyProc)
		SetLatch(&MyProc->procLatch);
#endif
	errno = save_errno;
}

static void
pgsentinel_sighup(SIGNAL_ARGS)
{
	int save_errno = errno;
	got_sighup = true;
#if PG_VERSION_NUM >= 100000
	SetLatch(MyLatch);
#else
	if (MyProc)
		SetLatch(&MyProc->procLatch);
#endif
	errno = save_errno;
}

static void
ash_entry_store(TimestampTz ash_time, const int pid,
#if PG_VERSION_NUM >= 130000
				int leader_pid,
#endif
				const char *usename,
				const int client_port, Oid datid, const char *datname,
				const char *application_name, const char *client_addr,
				TransactionId backend_xmin, TimestampTz backend_start,
				TimestampTz xact_start, TimestampTz query_start,
				TimestampTz state_change, const char *wait_event_type,
				const char *wait_event, const char *state,
				const char *client_hostname, const char *query,
				const char *backend_type, Oid usesysid,
				TransactionId backend_xid, int blockers, int blockerpid,
				const char *blocker_state, uint64 queryid,
				const char *gpi_query, const char *cmdtype)
{
	int inserted;
	inserted=IntEntryArray[0].inserted-1;
	memcpy(AshEntryArray[inserted].usename,usename,Min(strlen(usename)+1,
																NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].datname,datname,Min(strlen(datname)+1,
																NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].application_name,application_name,
								Min(strlen(application_name)+1,NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].wait_event_type,wait_event_type,
								Min(strlen(wait_event_type)+1,NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].wait_event,wait_event,
								Min(strlen(wait_event)+1,NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].state,state,Min(strlen(state)+1,
																NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].blocker_state,blocker_state,
								Min(strlen(blocker_state)+1,NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].client_hostname,client_hostname,
								Min(strlen(client_hostname)+1,NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].top_level_query,query,
					Min(strlen(query)+1,pgstat_track_activity_query_size-1));
	memcpy(AshEntryArray[inserted].backend_type,backend_type,
									Min(strlen(backend_type)+1,NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].client_addr,client_addr,
									Min(strlen(client_addr)+1,NAMEDATALEN-1));
	memcpy(AshEntryArray[inserted].query,gpi_query,Min(strlen(gpi_query)+1,
										pgstat_track_activity_query_size-1));
	memcpy(AshEntryArray[inserted].cmdtype,cmdtype,Min(strlen(cmdtype)+1,
																NAMEDATALEN-1));
	AshEntryArray[inserted].client_port=client_port;
	AshEntryArray[inserted].datid=datid;
	AshEntryArray[inserted].usesysid=usesysid;
	AshEntryArray[inserted].pid=pid;
#if PG_VERSION_NUM >= 130000
	AshEntryArray[inserted].leader_pid=leader_pid;
#endif
	AshEntryArray[inserted].backend_xmin=backend_xmin;
	AshEntryArray[inserted].backend_xid=backend_xid;
	AshEntryArray[inserted].backend_start=backend_start;
	AshEntryArray[inserted].xact_start=xact_start;
	AshEntryArray[inserted].query_start=query_start;
	AshEntryArray[inserted].state_change=state_change;
	AshEntryArray[inserted].ash_time=ash_time;
	AshEntryArray[inserted].blockers=blockers;
	AshEntryArray[inserted].blockerpid=blockerpid;
	AshEntryArray[inserted].queryid=queryid;
}

static void
ash_prepare_store(TimestampTz ash_time, const int pid,
#if PG_VERSION_NUM >= 130000
					int leader_pid,
#endif
					const char* usename,
					const int client_port, Oid datid, const char *datname,
					const char *application_name, const char *client_addr,
					TransactionId backend_xmin, TimestampTz backend_start,
					TimestampTz xact_start, TimestampTz query_start,
					TimestampTz state_change, const char *wait_event_type,
					const char *wait_event, const char *state,
					const char *client_hostname, const char *query,
					const char *backend_type, Oid usesysid,
					TransactionId backend_xid, int blockers, int blockerpid,
					const char *blocker_state, uint64 queryid,
					const char *gpi_query, const char *cmdtype)
{
	/* Safety check... */
	if (!AshEntryArray) { return; }

	IntEntryArray[0].inserted=(IntEntryArray[0].inserted % ash_max_entries) + 1;
	ash_entry_store(ash_time, pid,
#if PG_VERSION_NUM >= 130000
					leader_pid,
#endif
					usename, client_port, datid, datname,
					application_name, client_addr,backend_xmin, backend_start,
					xact_start, query_start, state_change, wait_event_type,
					wait_event, state, client_hostname, query, backend_type,
					usesysid, backend_xid, blockers, blockerpid, blocker_state,
					queryid, gpi_query, cmdtype);
}

void
pgsentinel_main(Datum main_arg)
{

	ereport(LOG, (errmsg("starting bgworker pgsentinel")));

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, pgsentinel_sighup);
	pqsignal(SIGTERM, pgsentinel_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* Connect to a database */
#if (PG_VERSION_NUM < 110000)
	BackgroundWorkerInitializeConnection(pgsentinelDbName, NULL);
#else
	BackgroundWorkerInitializeConnection(pgsentinelDbName, NULL, 0);
#endif

	while (!got_sigterm)
	{
		int rc, ret, i;
		bool gotactives;
		TimestampTz ash_time;
		MemoryContext uppercxt;
		gotactives=false; 

letswait:
		/* Wait necessary amount of time */
#if PG_VERSION_NUM >= 100000
		rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
								ash_sampling_period * 1000L,PG_WAIT_EXTENSION);
		ResetLatch(MyLatch);
#else
		rc = WaitLatch(&MyProc->procLatch, WL_LATCH_SET | WL_TIMEOUT |
							WL_POSTMASTER_DEATH, ash_sampling_period * 1000L);
		ResetLatch(&MyProc->procLatch);
#endif

		/* Emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		/* Process signals */
		if (got_sighup)
		{
			/* Process config file */
			got_sighup = false;
			ProcessConfigFile(PGC_SIGHUP);
			ereport(LOG, (errmsg("bgworker pgsentinel signal: processed SIGHUP")));
		}

		if (got_sigterm)
		{
			/* Simply exit */
			ereport(LOG, (errmsg("bgworker pgsentinel signal: processed SIGTERM")));
			proc_exit(0);
		}

		uppercxt = CurrentMemoryContext;
		SetCurrentStatementStartTimestamp();
		StartTransactionCommand();
		PushActiveSnapshot(GetTransactionSnapshot());

		if (!PgSentinelHasBeenLoaded()) {
			PopActiveSnapshot();
			CommitTransactionCommand();
			goto letswait;
		}

		SPI_connect();

		if (ash_track_idle_trans)
		{
			pgstat_report_activity(STATE_RUNNING, pgsa_query_track_idle);

			/* We can now execute queries via SPI */
			ret = SPI_execute(pgsa_query_track_idle, true, 0);
		}
		else
		{
			pgstat_report_activity(STATE_RUNNING, pgsa_query_no_track_idle);

			/* We can now execute queries via SPI */
			ret = SPI_execute(pgsa_query_no_track_idle, true, 0);
		}

		if (ret != SPI_OK_SELECT)
			elog(FATAL, "cannot select from pg_stat_activity: error code %d", ret);

		ash_time=GetCurrentTimestamp();
		/* Do some processing */

		if (SPI_processed > 0)
		{
			MemoryContext oldcxt = MemoryContextSwitchTo(uppercxt);
			gotactives=true;
			for (i = 0; i < SPI_processed; i++)
			{
				bool isnull;
				Datum data;
				char *usenamevalue=NULL;
				char *datnamevalue=NULL;
				char *appnamevalue=NULL;
				char *wait_event_typevalue=NULL;
				char *wait_eventvalue=NULL;
				char *client_hostnamevalue=NULL;
				char *queryvalue=NULL;
				char *backend_typevalue=NULL;
				char *statevalue=NULL;
				char *blockerstatevalue=NULL;
				char *clientaddrvalue=NULL;
				int pidvalue;
#if PG_VERSION_NUM >= 130000
				int leader_pidvalue;
#endif
				int client_portvalue;
				int blockersvalue;
				int blockerpidvalue;
				Oid datidvalue;
				Oid usesysidvalue;
				TransactionId backend_xminvalue;
				TransactionId backend_xidvalue;
				TimestampTz backend_startvalue;
				TimestampTz xact_startvalue;
				TimestampTz query_startvalue;
				TimestampTz state_changevalue;
				uint64 queryidvalue;
				char *gpi_queryvalue = NULL;
				char *cmdtypevalue = NULL;

				/* Fetch values */

				/* datid */
				datidvalue = DatumGetObjectId(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,1, &isnull));

				/* usesysid */
				usesysidvalue = DatumGetObjectId(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,4, &isnull));

				/* datname */
				datnamevalue = DatumGetCString(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,2, &isnull));

				/* pid */
				pidvalue = DatumGetInt32(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,3, &isnull));

#if PG_VERSION_NUM >= 100000
				/* blockerpid */
				blockerpidvalue = DatumGetInt32(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,21, &isnull));

				/* blockers */
				blockersvalue = DatumGetInt32(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,22, &isnull));
#else
				/* blockerpid */
				blockerpidvalue = DatumGetInt32(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,20, &isnull));

				/* blockers */
				blockersvalue = DatumGetInt32(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,21, &isnull));
#endif

				/* client_port */
				client_portvalue = DatumGetInt32(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,9, &isnull));

				/* usename */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																	5, &isnull);
				if (!isnull) {
					usenamevalue = DatumGetCString(data);
				}

				/* appname */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																	6, &isnull);
				if (!isnull) {
					appnamevalue = TextDatumGetCString(data);
				}

				/* wait_event_type */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																14, &isnull);
				if (!isnull) {
					wait_event_typevalue = TextDatumGetCString(data);
				}

				/* wait_event */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																15, &isnull);
				if (!isnull) {
					wait_eventvalue = TextDatumGetCString(data);
				}

				/* state */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																16, &isnull);
				if (!isnull) {
					statevalue = TextDatumGetCString(data);
				}

#if PG_VERSION_NUM >= 100000
				/* blocker state */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																23, &isnull);
				if (!isnull) {
					blockerstatevalue = TextDatumGetCString(data);
				}

				/* queryid */
				queryidvalue = DatumGetUInt64(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,25, &isnull));

				/* gpi query */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																26, &isnull);
				if (!isnull) {
					gpi_queryvalue = TextDatumGetCString(data);
				}

				/* cmdtype */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																27, &isnull);
				if (!isnull) {
					cmdtypevalue = TextDatumGetCString(data);
				}
#else
				/* blocker state */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																22, &isnull);
				if (!isnull) {
					blockerstatevalue = TextDatumGetCString(data);
				}

				/* queryid */
				queryidvalue = DatumGetUInt64(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,24, &isnull));

				/* gpi query */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																25, &isnull);
				if (!isnull) {
					gpi_queryvalue = TextDatumGetCString(data);
				}

				/* cmdtype */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																26, &isnull);
				if (!isnull) {
					cmdtypevalue = TextDatumGetCString(data);
				}
#endif

				/* client_hostname */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																	8, &isnull);
				if (!isnull) {
					client_hostnamevalue = TextDatumGetCString(data);
				}

				/* query */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																19, &isnull);
				if (!isnull) {
					queryvalue = TextDatumGetCString(data);
				}

#if PG_VERSION_NUM >= 100000
				/* backend_type */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																20, &isnull);
				if (!isnull) {
					backend_typevalue = TextDatumGetCString(data);
				}
#endif

				/* client addr */
				data=SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,
																7, &isnull);
				if (!isnull) {
					clientaddrvalue = TextDatumGetCString(data);
				}

				/* backend xid */
				backend_xidvalue = DatumGetTransactionId(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,17, &isnull));

				/* backedn xmin */
				backend_xminvalue = DatumGetTransactionId(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,18, &isnull));

				/* backend start */
				backend_startvalue = DatumGetTimestamp(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,10, &isnull));

				/* xact start */
				xact_startvalue = DatumGetTimestamp(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,11, &isnull));

				/* query start */
				query_startvalue = DatumGetTimestamp(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,12, &isnull));

				/* state change */
				state_changevalue = DatumGetTimestamp(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,13, &isnull));
#if PG_VERSION_NUM >= 130000
				/* leader pid */
				leader_pidvalue = DatumGetInt32(SPI_getbinval(
					SPI_tuptable->vals[i],SPI_tuptable->tupdesc,28, &isnull));
#endif

				/* prepare to store the entry */
				ash_prepare_store(ash_time, pidvalue,
#if PG_VERSION_NUM >= 130000
									leader_pidvalue,
#endif
									usenamevalue ? usenamevalue : "\0",
									client_portvalue, datidvalue,
									datnamevalue ? datnamevalue : "\0",
									appnamevalue ? appnamevalue : "\0",
									clientaddrvalue ? clientaddrvalue : "\0",
									backend_xminvalue, backend_startvalue,
									xact_startvalue,query_startvalue,
									state_changevalue,
									wait_event_typevalue ? wait_event_typevalue : "\0",
									wait_eventvalue ? wait_eventvalue : "\0",
									statevalue ? statevalue : "\0",
									client_hostnamevalue ? client_hostnamevalue : "\0",
									queryvalue ? queryvalue : "\0",
									backend_typevalue ? backend_typevalue : "\0",
									usesysidvalue, backend_xidvalue,
									blockersvalue, blockerpidvalue,
									blockerstatevalue ? blockerstatevalue : "\0",
									queryidvalue,
									gpi_queryvalue ? gpi_queryvalue : "\0",
									cmdtypevalue ? cmdtypevalue : "\0");
				if (appnamevalue != NULL) {
				   pfree(appnamevalue);
				}
				if (wait_event_typevalue != NULL) {
				   pfree(wait_event_typevalue);
				}
				if (wait_eventvalue != NULL) {
				   pfree(wait_eventvalue);
				}
				if (statevalue != NULL) {
				   pfree(statevalue);
				}
				if (blockerstatevalue != NULL) {
				   pfree(blockerstatevalue);
				}
				if (client_hostnamevalue != NULL) {
				   pfree(client_hostnamevalue);
				}
				if (queryvalue != NULL) {
				   pfree(queryvalue);
				}
				if (backend_typevalue != NULL) {
				   pfree(backend_typevalue);
				}
				if (clientaddrvalue != NULL) {
				   pfree(clientaddrvalue);
				}
				if (gpi_queryvalue != NULL) {
					pfree(gpi_queryvalue);
				}
				if (cmdtypevalue != NULL) {
					pfree(cmdtypevalue);
				}
			}
			MemoryContextSwitchTo(oldcxt);
		}
		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();
		pgstat_report_activity(STATE_IDLE, NULL);

		/* pg_stat_statement_history */
		if (gotactives && pgssh_enable) 
		{
			uppercxt = CurrentMemoryContext;

			SetCurrentStatementStartTimestamp();
			StartTransactionCommand();
			SPI_connect();
			PushActiveSnapshot(GetTransactionSnapshot());
			pgstat_report_activity(STATE_RUNNING, pg_stat_statements_query);

			/* We can now execute queries via SPI */
			ret = SPI_execute(pg_stat_statements_query,true, 0);

			if (ret != SPI_OK_SELECT)
				elog(FATAL, "cannot select from pg_stat_statements: error code %d", ret);

			/* Do some processing */
			if (SPI_processed > 0)
			{
				MemoryContext oldcxt = MemoryContextSwitchTo(uppercxt);
				for (i = 0; i < SPI_processed; i++)
				{
					bool isnull;
					IntEntryArray[0].pgsshinserted=(IntEntryArray[0].pgsshinserted % pgssh_max_entries) + 1;
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].ash_time=ash_time;
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].userid=DatumGetObjectId(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,1, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].dbid=DatumGetObjectId(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,2, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].queryid=DatumGetUInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,3, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].calls=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,4, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].total_time=DatumGetFloat8(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,5, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].rows=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,6, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].shared_blks_hit=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,7, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].shared_blks_read=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,8, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].shared_blks_dirtied=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,9, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].shared_blks_written=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,10, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].local_blks_hit=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,11, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].local_blks_read=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,12, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].local_blks_dirtied=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,13, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].local_blks_written=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,14, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].temp_blks_read=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,15, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].temp_blks_written=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,16, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].blk_read_time=DatumGetFloat8(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,17, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].blk_write_time=DatumGetFloat8(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,18, &isnull));
#if PG_VERSION_NUM >= 130000
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].plans=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,19, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].total_plan_time=DatumGetFloat8(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,20, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].wal_records=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,21, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].wal_fpi=DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,22, &isnull));
					PgsshEntryArray[IntEntryArray[0].pgsshinserted-1].wal_bytes=DatumGetUInt64(SPI_getbinval(SPI_tuptable->vals[i],SPI_tuptable->tupdesc,23, &isnull));
#endif
				}
				MemoryContextSwitchTo(oldcxt);
			}
			SPI_finish();
			PopActiveSnapshot();
			CommitTransactionCommand();
			pgstat_report_activity(STATE_IDLE, NULL);
		}
	}
	/* No problems, so clean exit */
	proc_exit(0);
}

static void
pgsentinel_load_params(void)
{

	DefineCustomIntVariable("pgsentinel_ash.sampling_period",
							"Duration between each pull (in seconds).",
							NULL,
							&ash_sampling_period,
							1,
							1,
							INT_MAX,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgsentinel_ash.track_idle_trans",
	                        "Track session in idle transaction state.",
							NULL,
							&ash_track_idle_trans,
							true,
							PGC_SIGHUP,
							0,
							NULL,
							NULL,
							NULL);

	if (!process_shared_preload_libraries_in_progress)
		return;

	/* can't define PGC_POSTMASTER variable after startup */
	DefineCustomIntVariable("pgsentinel_ash.max_entries",
							"Maximum number of ash entries.",
							NULL,
							&ash_max_entries,
							1000,
							1000,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	EmitWarningsOnPlaceholders("pgsentinel_ash");

	DefineCustomIntVariable("pgsentinel_pgssh.max_entries",
							"Maximum number of pgssh entries.",
							NULL,
							&pgssh_max_entries,
							10000,
							10000,
							INT_MAX,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	DefineCustomBoolVariable("pgsentinel_pgssh.enable",
	                        "Enable pg_stat_statements_history.",
							NULL,
							&pgssh_enable,
							true,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	EmitWarningsOnPlaceholders("pgsentinel_pgssh");

	DefineCustomStringVariable("pgsentinel.db_name",
							gettext_noop("Database on which the worker connect."),
							NULL,
							&pgsentinelDbName,
							"yugabyte",
							PGC_POSTMASTER,
							GUC_SUPERUSER_ONLY,
							NULL, NULL, NULL);
}

/*
 * Entry point for worker loading
 */
void
_PG_init(void)
{

	BackgroundWorker worker;

	/* Add parameters */
	pgsentinel_load_params();

	if (!process_shared_preload_libraries_in_progress)
		return;

#if PG_VERSION_NUM < 150000
	RequestAddinShmemSpace(ash_entry_memsize());
	RequestNamedLWLockTranche("Ash Entry Array", 1);

	RequestAddinShmemSpace(proc_entry_memsize());
	RequestNamedLWLockTranche("Get_parsedinfo Proc Entry Array", 1);

	RequestAddinShmemSpace(int_entry_memsize());
	RequestNamedLWLockTranche("Int Entry Array", 1);

	if (pgssh_enable)
	{
		RequestAddinShmemSpace(pgssh_entry_memsize());
		RequestNamedLWLockTranche("Pgssh Entry Array", 1);
	}
#endif

	/*
	 * Install hooks.
	 */
#if PG_VERSION_NUM >= 150000
	ash_prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = ash_shmem_request;
#endif
	ash_prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = ash_shmem_startup;
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = getparsedinfo_post_parse_analyze;

	/* Worker parameter and registration */
	memset(&worker, 0, sizeof(worker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
#if PG_VERSION_NUM >= 100000
	sprintf(worker.bgw_library_name, "pgsentinel");
	sprintf(worker.bgw_function_name, "pgsentinel_main");
#else
	worker.bgw_main = pgsentinel_main;
#endif
	snprintf(worker.bgw_name, BGW_MAXLEN, "%s", worker_name);
	/* Wait ash_restart_wait_time seconds for restart before crash */
	worker.bgw_restart_time = ash_restart_wait_time;
	worker.bgw_main_arg = (Datum) 0;
#if PG_VERSION_NUM >= 90400
	/*
	 * Notify PID is present since 9.4. If this is not initialized
	 * a static background worker cannot start properly.
	 */
	worker.bgw_notify_pid = 0;
#endif
	RegisterBackgroundWorker(&worker);
}

static void
pg_active_session_history_internal(FunctionCallInfo fcinfo)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc       tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int i;

	/* Entry array must exist already */
	if (!AshEntryArray)
		ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pg_active_session_history must be loaded via shared_preload_libraries")));

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
		elog(ERROR, "return type must be a row type");

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	for (i = 0; i < ash_max_entries; i++)
	{
		Datum           values[PG_ACTIVE_SESSION_HISTORY_COLS];
		bool            nulls[PG_ACTIVE_SESSION_HISTORY_COLS];
		int                     j = 0;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		// ash_time
		if (TimestampTzGetDatum(AshEntryArray[i].ash_time))
			values[j++] = TimestampTzGetDatum(AshEntryArray[i].ash_time);
		else
			break;

		// datid
		if (ObjectIdGetDatum(AshEntryArray[i].datid))
			values[j++] = ObjectIdGetDatum(AshEntryArray[i].datid);
		else
			nulls[j++] = true;

		// datname
		if (AshEntryArray[i].datname[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].datname);
		else
			nulls[j++] = true;

		// pid
		if (Int32GetDatum(AshEntryArray[i].pid))
			values[j++] = Int32GetDatum(AshEntryArray[i].pid);
		else
			nulls[j++] = true;

#if PG_VERSION_NUM >= 130000
		// leader_pid
		if (Int32GetDatum(AshEntryArray[i].leader_pid))
			values[j++] = Int32GetDatum(AshEntryArray[i].leader_pid);
		else
			nulls[j++] = true;
#else
			nulls[j++] = true;
#endif

		// usesysid
		if (ObjectIdGetDatum(AshEntryArray[i].usesysid))
			values[j++] = ObjectIdGetDatum(AshEntryArray[i].usesysid);
		else
			nulls[j++] = true;

		// usename
		if (AshEntryArray[i].usename[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].usename);
		else
			nulls[j++] = true;

		// application_name
		if (AshEntryArray[i].application_name[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].application_name);
		else
			nulls[j++] = true;

		// client_addr
		if (AshEntryArray[i].client_addr[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].client_addr);
		else
			nulls[j++] = true;

		// client_hostname
		if (AshEntryArray[i].client_hostname[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].client_hostname);
		else
			nulls[j++] = true;

		// client_port
		if (Int32GetDatum(AshEntryArray[i].client_port))
			values[j++] = Int32GetDatum(AshEntryArray[i].client_port);
		else
			nulls[j++] = true;

		// backend_start
		if (TimestampTzGetDatum(AshEntryArray[i].backend_start))
			values[j++] = TimestampTzGetDatum(AshEntryArray[i].backend_start);
		else
			nulls[j++] = true;

		// xact_start
		if (TimestampTzGetDatum(AshEntryArray[i].xact_start))
			values[j++] = TimestampTzGetDatum(AshEntryArray[i].xact_start);
		else
			nulls[j++] = true;

		// query_start
		if (TimestampTzGetDatum(AshEntryArray[i].query_start))
			values[j++] = TimestampTzGetDatum(AshEntryArray[i].query_start);
		else
			nulls[j++] = true;

		// state_change
		if (TimestampTzGetDatum(AshEntryArray[i].state_change))
			values[j++] = TimestampTzGetDatum(AshEntryArray[i].state_change);
		else
			nulls[j++] = true;

		// wait_event_type
		if (AshEntryArray[i].wait_event_type[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].wait_event_type);
		else
			nulls[j++] = true;

		// wait_event
		if (AshEntryArray[i].wait_event[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].wait_event);
		else
			nulls[j++] = true;

		// state
		if (AshEntryArray[i].state[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].state);
		else
			nulls[j++] = true;

		// backend_xid
		if (TransactionIdGetDatum(AshEntryArray[i].backend_xid))
			values[j++] = TransactionIdGetDatum(AshEntryArray[i].backend_xid);
		else
			nulls[j++] = true;

		// backend_xmin
		if (TransactionIdGetDatum(AshEntryArray[i].backend_xmin))
			values[j++] = TransactionIdGetDatum(AshEntryArray[i].backend_xmin);
		else
			nulls[j++] = true;

		// top_level_query
		if (AshEntryArray[i].top_level_query[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].top_level_query);
		else
			nulls[j++] = true;

		// query
		if (AshEntryArray[i].query[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].query);
		else
			nulls[j++] = true;

		// cmdtype
		if (AshEntryArray[i].cmdtype[0] != '\0')
                        values[j++] = CStringGetTextDatum(AshEntryArray[i].cmdtype);
		else
                        nulls[j++] = true;

		// query_id
		if (AshEntryArray[i].queryid)
			values[j++] = Int64GetDatum(AshEntryArray[i].queryid);
		else
			nulls[j++] = true;


		// backend_type
		if (AshEntryArray[i].backend_type[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].backend_type);
		else
			nulls[j++] = true;

		// blockers
		if (Int32GetDatum(AshEntryArray[i].blockers))
			values[j++] = Int32GetDatum(AshEntryArray[i].blockers);
		else
			nulls[j++] = true;

		// blockerspid
		if (Int32GetDatum(AshEntryArray[i].blockerpid))
			values[j++] = Int32GetDatum(AshEntryArray[i].blockerpid);
		else
			nulls[j++] = true;

		// blocker state
		if (AshEntryArray[i].blocker_state[0] != '\0')
			values[j++] = CStringGetTextDatum(AshEntryArray[i].blocker_state);
		else
			nulls[j++] = true;

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
}


static void
pg_stat_statements_history_internal(FunctionCallInfo fcinfo)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	TupleDesc       tupdesc;
	Tuplestorestate *tupstore;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	int i;

	if (!pgssh_enable)
		ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pg_stat_statements_history not enabled, set pgsentinel_pgssh.enable")));
	/* Entry array must exist already */
	if (!PgsshEntryArray)
		ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("pg_stat_statements_history must be loaded via shared_preload_libraries")));

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
		elog(ERROR, "return type must be a row type");

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	for (i = 0; i < pgssh_max_entries; i++)
	{
		Datum           values[PG_STAT_STATEMENTS_HISTORY_COLS];
		bool            nulls[PG_STAT_STATEMENTS_HISTORY_COLS];
		int             j = 0;
#if PG_VERSION_NUM >= 130000
		char        buf[256];
		Datum       wal_bytes;
#endif

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		// ash_time
		if (TimestampTzGetDatum(PgsshEntryArray[i].ash_time))
			values[j++] = TimestampTzGetDatum(PgsshEntryArray[i].ash_time);
		else
			break;

		// userid
		if (ObjectIdGetDatum(PgsshEntryArray[i].userid))
			values[j++] = ObjectIdGetDatum(PgsshEntryArray[i].userid);
		else
			nulls[j++] = true;

		// dbid
		if (ObjectIdGetDatum(PgsshEntryArray[i].dbid))
			values[j++] = ObjectIdGetDatum(PgsshEntryArray[i].dbid);
		else
			nulls[j++] = true;

		// query_id
		if (Int64GetDatum(PgsshEntryArray[i].queryid))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].queryid);
		else
			nulls[j++] = true;

		// calls
		if (Int64GetDatum(PgsshEntryArray[i].calls))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].calls);
		else
			values[j++] = 0;

		// total_time
		if (Float8GetDatum(PgsshEntryArray[i].total_time))
			values[j++] = Float8GetDatum(PgsshEntryArray[i].total_time);
		else
			values[j++] = 0;

		// rows
		if (Int64GetDatum(PgsshEntryArray[i].rows))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].rows);
		else
			values[j++] = 0;

		// shared_blks_hit
		if (Int64GetDatum(PgsshEntryArray[i].shared_blks_hit))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].shared_blks_hit);
		else
			values[j++] = 0;

		// shared_blks_read
		if (Int64GetDatum(PgsshEntryArray[i].shared_blks_read))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].shared_blks_read);
		else
			values[j++] = 0;

		// shared_blks_dirtied
		if (Int64GetDatum(PgsshEntryArray[i].shared_blks_dirtied))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].shared_blks_dirtied);
		else
			values[j++] = 0;

		// shared_blks_written
		if (Int64GetDatum(PgsshEntryArray[i].shared_blks_written))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].shared_blks_written);
		else
			values[j++] = 0;

		// local_blks_hit
		if (Int64GetDatum(PgsshEntryArray[i].local_blks_hit))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].local_blks_hit);
		else
			values[j++] = 0;

		// local_blks_read
		if (Int64GetDatum(PgsshEntryArray[i].local_blks_read))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].local_blks_read);
		else
			values[j++] = 0;

		// local_blks_dirtied
		if (Int64GetDatum(PgsshEntryArray[i].local_blks_dirtied))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].local_blks_dirtied);
		else
			values[j++] = 0;

		// local_blks_written
		if (Int64GetDatum(PgsshEntryArray[i].local_blks_written))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].local_blks_written);
		else
			values[j++] = 0;

		// temp_blks_read
		if (Int64GetDatum(PgsshEntryArray[i].temp_blks_read))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].temp_blks_read);
		else
			values[j++] = 0;

		// temp_blks_written
		if (Int64GetDatum(PgsshEntryArray[i].temp_blks_written))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].temp_blks_written);
		else
			values[j++] = 0;

		// blk_read_time
		if (Float8GetDatum(PgsshEntryArray[i].blk_read_time))
			values[j++] = Float8GetDatum(PgsshEntryArray[i].blk_read_time);
		else
			values[j++] = 0;

		// blk_write_time
		if (Float8GetDatum(PgsshEntryArray[i].blk_write_time))
			values[j++] = Float8GetDatum(PgsshEntryArray[i].blk_write_time);
		else
			values[j++] = 0;
#if PG_VERSION_NUM >= 130000
		// plans
		if (Int64GetDatum(PgsshEntryArray[i].plans))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].plans);
		else
			values[j++] = 0;

		// total_plan_time
		if (Float8GetDatum(PgsshEntryArray[i].total_plan_time))
			values[j++] = Float8GetDatum(PgsshEntryArray[i].total_plan_time);
		else
			values[j++] = 0;

		// wal_records
		if (Int64GetDatum(PgsshEntryArray[i].wal_records))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].wal_records);
		else
			values[j++] = 0;

		// wal_fpi
		if (Int64GetDatum(PgsshEntryArray[i].wal_fpi))
			values[j++] = Int64GetDatum(PgsshEntryArray[i].wal_fpi);
		else
			values[j++] = 0;

		// wal_bytes
		snprintf(buf, sizeof buf, UINT64_FORMAT, PgsshEntryArray[i].wal_bytes);
		/* Convert to numeric. */
		wal_bytes = DirectFunctionCall3(numeric_in,
										CStringGetDatum(buf),
										ObjectIdGetDatum(0),
										Int32GetDatum(-1));

		values[j++] = wal_bytes;
#else
		nulls[j++] = true;
		nulls[j++] = true;
		nulls[j++] = true;
		nulls[j++] = true;
		nulls[j++] = true;
#endif
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
        }
        /* clean up and return the tuplestore */
        tuplestore_donestoring(tupstore);
}

Datum
pg_active_session_history(PG_FUNCTION_ARGS)
{
	pg_active_session_history_internal(fcinfo);
	return (Datum) 0;
}

Datum
pg_stat_statements_history(PG_FUNCTION_ARGS)
{
	pg_stat_statements_history_internal(fcinfo);
	return (Datum) 0;
}

void
_PG_fini(void)
{
	/* Uninstall hooks. */
	shmem_startup_hook = ash_prev_shmem_startup_hook;
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
}

static bool
PgSentinelHasBeenLoaded(void)
{
	bool extensionLoaded = false;
	bool extensionPresent = false;
	bool extensionScriptExecuted = true;

	Oid extensionOid = get_extension_oid(EXTENSION_NAME, true);
	if (extensionOid != InvalidOid)
		extensionPresent = true;

	if (extensionPresent)
	{
		/* check if extension objects are still being created */
		if (creating_extension && CurrentExtensionObject == extensionOid)
			extensionScriptExecuted = false;
		else if (IsBinaryUpgrade)
			extensionScriptExecuted = false;
	}

	extensionLoaded = extensionPresent && extensionScriptExecuted;

	return extensionLoaded;
}

#if PG_VERSION_NUM >= 150000
/*
 * shmem_request hook: request additional shared resources.  We'll allocate or
 * attach to the shared resources in ash_shmem_startup().
 */
static void
ash_shmem_request(void)
{
    if (ash_prev_shmem_request_hook)
		ash_prev_shmem_request_hook();

	RequestAddinShmemSpace(ash_entry_memsize());
	RequestNamedLWLockTranche("Ash Entry Array", 1);

	RequestAddinShmemSpace(proc_entry_memsize());
	RequestNamedLWLockTranche("Get_parsedinfo Proc Entry Array", 1);

	RequestAddinShmemSpace(int_entry_memsize());
	RequestNamedLWLockTranche("Int Entry Array", 1);

	if (pgssh_enable)
	{
		RequestAddinShmemSpace(pgssh_entry_memsize());
		RequestNamedLWLockTranche("Pgssh Entry Array", 1);
	}
}
#endif
