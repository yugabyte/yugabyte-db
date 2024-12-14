/*-------------------------------------------------------------------------
 *
 * postinit.c
 *	  postgres initialization utilities
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/init/postinit.c
 *
 * The following only applies to changes made to this file as part of
 * YugaByte development.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/session.h"
#include "access/sysattr.h"
#include "access/tableam.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "access/xloginsert.h"
#include "catalog/catalog.h"
#include "catalog/namespace.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/pg_tablespace.h"
#include "libpq/auth.h"
#include "libpq/libpq-be.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#include "postmaster/postmaster.h"
#include "replication/slot.h"
#include "replication/walsender.h"
#include "storage/bufmgr.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/lmgr.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/procsignal.h"
#include "storage/sinvaladt.h"
#include "storage/smgr.h"
#include "storage/sync.h"
#include "tcop/tcopprot.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"
#include "utils/portal.h"
#include "utils/ps_status.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/timeout.h"

/* Yugabyte includes */
#include "pg_yb_utils.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/pg_yb_logical_client_version.h"
#include "catalog/pg_yb_profile.h"
#include "catalog/pg_yb_role_profile.h"
#include "catalog/pg_yb_tablegroup.h"
#include "catalog/yb_catalog_version.h"
#include "catalog/yb_logical_client_version.h"
#include "utils/yb_inheritscache.h"

static HeapTuple GetDatabaseTuple(const char *dbname);
static HeapTuple GetDatabaseTupleByOid(Oid dboid);
static void PerformAuthentication(Port *port);
static void CheckMyDatabase(const char *name, bool am_superuser, bool override_allow_connections);
static void ShutdownPostgres(int code, Datum arg);
static void StatementTimeoutHandler(void);
static void LockTimeoutHandler(void);
static void IdleInTransactionSessionTimeoutHandler(void);
static void IdleSessionTimeoutHandler(void);
static void IdleStatsUpdateTimeoutHandler(void);
static void ClientCheckTimeoutHandler(void);
static bool ThereIsAtLeastOneRole(void);
static void process_startup_options(Port *port, bool am_superuser);
static void process_settings(Oid databaseid, Oid roleid);

static void InitPostgresImpl(const char *in_dbname, Oid dboid,
							 const char *username, Oid useroid,
							 bool load_session_libraries,
							 bool override_allow_connections,
							 char *out_dbname,
							 uint64_t *session_id,
							 bool* yb_sys_table_prefetching_started);
static void YbEnsureSysTablePrefetchingStopped();

/*** InitPostgres support ***/


/*
 * GetDatabaseTuple -- fetch the pg_database row for a database
 *
 * This is used during backend startup when we don't yet have any access to
 * system catalogs in general.  In the worst case, we can seqscan pg_database
 * using nothing but the hard-wired descriptor that relcache.c creates for
 * pg_database.  In more typical cases, relcache.c was able to load
 * descriptors for both pg_database and its indexes from the shared relcache
 * cache file, and so we can do an indexscan.  criticalSharedRelcachesBuilt
 * tells whether we got the cached descriptors.
 */
static HeapTuple
GetDatabaseTuple(const char *dbname)
{
	HeapTuple	tuple;
	Relation	relation;
	SysScanDesc scan;
	ScanKeyData key[1];

	/*
	 * form a scan key
	 */
	ScanKeyInit(&key[0],
				Anum_pg_database_datname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(dbname));

	/*
	 * Open pg_database and fetch a tuple.  Force heap scan if we haven't yet
	 * built the critical shared relcache entries (i.e., we're starting up
	 * without a shared relcache cache file).
	 */
	relation = table_open(DatabaseRelationId, AccessShareLock);
	scan = systable_beginscan(relation, DatabaseNameIndexId,
							  criticalSharedRelcachesBuilt,
							  NULL,
							  1, key);

	tuple = systable_getnext(scan);

	/* Must copy tuple before releasing buffer */
	if (HeapTupleIsValid(tuple))
		tuple = heap_copytuple(tuple);

	/* all done */
	systable_endscan(scan);
	table_close(relation, AccessShareLock);

	return tuple;
}

/*
 * GetDatabaseTupleByOid -- as above, but search by database OID
 */
static HeapTuple
GetDatabaseTupleByOid(Oid dboid)
{
	HeapTuple	tuple;
	Relation	relation;
	SysScanDesc scan;
	ScanKeyData key[1];

	/*
	 * form a scan key
	 */
	ScanKeyInit(&key[0],
				Anum_pg_database_oid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(dboid));

	/*
	 * Open pg_database and fetch a tuple.  Force heap scan if we haven't yet
	 * built the critical shared relcache entries (i.e., we're starting up
	 * without a shared relcache cache file).
	 */
	relation = table_open(DatabaseRelationId, AccessShareLock);
	scan = systable_beginscan(relation, DatabaseOidIndexId,
							  criticalSharedRelcachesBuilt,
							  NULL,
							  1, key);

	tuple = systable_getnext(scan);

	/* Must copy tuple before releasing buffer */
	if (HeapTupleIsValid(tuple))
		tuple = heap_copytuple(tuple);

	/* all done */
	systable_endscan(scan);
	table_close(relation, AccessShareLock);

	return tuple;
}


/*
 * PerformAuthentication -- authenticate a remote client
 *
 * returns: nothing.  Will not return at all if there's any failure.
 */
static void
PerformAuthentication(Port *port)
{
	/* This should be set already, but let's make sure */
	ClientAuthInProgress = true;	/* limit visibility of log messages */

	/*
	 * In EXEC_BACKEND case, we didn't inherit the contents of pg_hba.conf
	 * etcetera from the postmaster, and have to load them ourselves.
	 *
	 * FIXME: [fork/exec] Ugh.  Is there a way around this overhead?
	 */
#ifdef EXEC_BACKEND

	/*
	 * load_hba() and load_ident() want to work within the PostmasterContext,
	 * so create that if it doesn't exist (which it won't).  We'll delete it
	 * again later, in PostgresMain.
	 */
	if (PostmasterContext == NULL)
		PostmasterContext = AllocSetContextCreate(TopMemoryContext,
												  "Postmaster",
												  ALLOCSET_DEFAULT_SIZES);

	if (!load_hba())
	{
		/*
		 * It makes no sense to continue if we fail to load the HBA file,
		 * since there is no way to connect to the database in this case.
		 */
		ereport(FATAL,
				(errmsg("could not load pg_hba.conf")));
	}

	if (!load_ident())
	{
		/*
		 * It is ok to continue if we fail to load the IDENT file, although it
		 * means that you cannot log in using any of the authentication
		 * methods that need a user name mapping. load_ident() already logged
		 * the details of error to the log.
		 */
	}
#endif

	/*
	 * Set up a timeout in case a buggy or malicious client fails to respond
	 * during authentication.  Since we're inside a transaction and might do
	 * database access, we have to use the statement_timeout infrastructure.
	 */
	enable_timeout_after(STATEMENT_TIMEOUT, AuthenticationTimeout * 1000);

	/*
	 * Now perform authentication exchange.
	 */
	set_ps_display("authentication");
	ClientAuthentication(port); /* might not return, if failure */

	/*
	 * Done with authentication.  Disable the timeout, and log if needed.
	 */
	disable_timeout(STATEMENT_TIMEOUT, false);

	if (Log_connections)
	{
		StringInfoData logmsg;

		initStringInfo(&logmsg);
		if (am_walsender)
			appendStringInfo(&logmsg, _("replication connection authorized: user=%s"),
							 port->user_name);
		else
			appendStringInfo(&logmsg, _("connection authorized: user=%s"),
							 port->user_name);
		if (!am_walsender)
			appendStringInfo(&logmsg, _(" database=%s"), port->database_name);

		if (port->application_name != NULL)
			appendStringInfo(&logmsg, _(" application_name=%s"),
							 port->application_name);

#ifdef USE_SSL
		if (port->ssl_in_use)
			appendStringInfo(&logmsg, _(" SSL enabled (protocol=%s, cipher=%s, bits=%d)"),
							 be_tls_get_version(port),
							 be_tls_get_cipher(port),
							 be_tls_get_cipher_bits(port));
#endif
#ifdef ENABLE_GSS
		if (port->gss)
		{
			const char *princ = be_gssapi_get_princ(port);

			if (princ)
				appendStringInfo(&logmsg,
								 _(" GSS (authenticated=%s, encrypted=%s, principal=%s)"),
								 be_gssapi_get_auth(port) ? _("yes") : _("no"),
								 be_gssapi_get_enc(port) ? _("yes") : _("no"),
								 princ);
			else
				appendStringInfo(&logmsg,
								 _(" GSS (authenticated=%s, encrypted=%s)"),
								 be_gssapi_get_auth(port) ? _("yes") : _("no"),
								 be_gssapi_get_enc(port) ? _("yes") : _("no"));
		}
#endif

		ereport(LOG, errmsg_internal("%s", logmsg.data));
		pfree(logmsg.data);
	}

	set_ps_display("startup");

	ClientAuthInProgress = false;	/* client_min_messages is active now */
}


/*
 * CheckMyDatabase -- fetch information from the pg_database entry for our DB
 */
static void
CheckMyDatabase(const char *name, bool am_superuser, bool override_allow_connections)
{
	HeapTuple	tup;
	Form_pg_database dbform;
	Datum		datum;
	bool		isnull;
	char	   *collate;
	char	   *ctype;
	char	   *iculocale;

	/* Fetch our pg_database row normally, via syscache */
	tup = SearchSysCache1(DATABASEOID, ObjectIdGetDatum(MyDatabaseId));
	if (!HeapTupleIsValid(tup))
		elog(ERROR, "cache lookup failed for database %u", MyDatabaseId);
	dbform = (Form_pg_database) GETSTRUCT(tup);

	/* This recheck is strictly paranoia */
	if (strcmp(name, NameStr(dbform->datname)) != 0)
		ereport(FATAL,
				(errcode(ERRCODE_UNDEFINED_DATABASE),
				 errmsg("database \"%s\" has disappeared from pg_database",
						name),
				 errdetail("Database OID %u now seems to belong to \"%s\".",
						   MyDatabaseId, NameStr(dbform->datname))));

	/*
	 * Check permissions to connect to the database.
	 *
	 * These checks are not enforced when in standalone mode, so that there is
	 * a way to recover from disabling all access to all databases, for
	 * example "UPDATE pg_database SET datallowconn = false;".
	 *
	 * We do not enforce them for autovacuum worker processes either.
	 */
	if (IsUnderPostmaster && !IsAutoVacuumWorkerProcess())
	{
		/*
		 * Check that the database is currently allowing connections.
		 */
		if (!dbform->datallowconn && !override_allow_connections)
			ereport(FATAL,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("database \"%s\" is not currently accepting connections",
							name)));

		/*
		 * Check privilege to connect to the database.  (The am_superuser test
		 * is redundant, but since we have the flag, might as well check it
		 * and save a few cycles.)
		 */
		if (!am_superuser &&
			pg_database_aclcheck(MyDatabaseId, GetUserId(),
								 ACL_CONNECT) != ACLCHECK_OK)
			ereport(FATAL,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("permission denied for database \"%s\"", name),
					 errdetail("User does not have CONNECT privilege.")));

		/*
		 * Check connection limit for this database.
		 *
		 * There is a race condition here --- we create our PGPROC before
		 * checking for other PGPROCs.  If two backends did this at about the
		 * same time, they might both think they were over the limit, while
		 * ideally one should succeed and one fail.  Getting that to work
		 * exactly seems more trouble than it is worth, however; instead we
		 * just document that the connection limit is approximate.
		 */
		if (dbform->datconnlimit >= 0 &&
			!am_superuser &&
			CountDBConnections(MyDatabaseId) > dbform->datconnlimit)
			ereport(FATAL,
					(errcode(ERRCODE_TOO_MANY_CONNECTIONS),
					 errmsg("too many connections for database \"%s\"",
							name)));
	}

	/*
	 * OK, we're golden.  Next to-do item is to save the encoding info out of
	 * the pg_database tuple.
	 */
	SetDatabaseEncoding(dbform->encoding);
	/* Record it as a GUC internal option, too */
	SetConfigOption("server_encoding", GetDatabaseEncodingName(),
					PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
	/* If we have no other source of client_encoding, use server encoding */
	SetConfigOption("client_encoding", GetDatabaseEncodingName(),
					PGC_BACKEND, PGC_S_DYNAMIC_DEFAULT);

	/* assign locale variables */
	datum = SysCacheGetAttr(DATABASEOID, tup, Anum_pg_database_datcollate, &isnull);
	Assert(!isnull);
	collate = TextDatumGetCString(datum);
	datum = SysCacheGetAttr(DATABASEOID, tup, Anum_pg_database_datctype, &isnull);
	Assert(!isnull);
	ctype = TextDatumGetCString(datum);

	if (pg_perm_setlocale(LC_COLLATE, collate) == NULL)
		ereport(FATAL,
				(errmsg("database locale is incompatible with operating system"),
				 errdetail("The database was initialized with LC_COLLATE \"%s\", "
						   " which is not recognized by setlocale().", collate),
				 errhint("Recreate the database with another locale or install the missing locale.")));

	if (pg_perm_setlocale(LC_CTYPE, ctype) == NULL)
		ereport(FATAL,
				(errmsg("database locale is incompatible with operating system"),
				 errdetail("The database was initialized with LC_CTYPE \"%s\", "
						   " which is not recognized by setlocale().", ctype),
				 errhint("Recreate the database with another locale or install the missing locale.")));

	if (dbform->datlocprovider == COLLPROVIDER_ICU)
	{
		datum = SysCacheGetAttr(DATABASEOID, tup, Anum_pg_database_daticulocale, &isnull);
		Assert(!isnull);
		iculocale = TextDatumGetCString(datum);
		make_icu_collator(iculocale, &default_locale);
	}
	else
		iculocale = NULL;

	default_locale.provider = dbform->datlocprovider;

	/*
	 * Default locale is currently always deterministic.  Nondeterministic
	 * locales currently don't support pattern matching, which would break a
	 * lot of things if applied globally.
	 */
	default_locale.deterministic = true;

	/*
	 * Check collation version.  See similar code in
	 * pg_newlocale_from_collation().  Note that here we warn instead of error
	 * in any case, so that we don't prevent connecting.
	 */
	datum = SysCacheGetAttr(DATABASEOID, tup, Anum_pg_database_datcollversion,
							&isnull);
	if (!isnull)
	{
		char	   *actual_versionstr;
		char	   *collversionstr;

		collversionstr = TextDatumGetCString(datum);

		actual_versionstr = get_collation_actual_version(dbform->datlocprovider, dbform->datlocprovider == COLLPROVIDER_ICU ? iculocale : collate);
		if (!actual_versionstr)
			/* should not happen */
			elog(WARNING,
				 "database \"%s\" has no actual collation version, but a version was recorded",
				 name);
		else if (strcmp(actual_versionstr, collversionstr) != 0)
			ereport(WARNING,
					(errmsg("database \"%s\" has a collation version mismatch",
							name),
					 errdetail("The database was created using collation version %s, "
							   "but the operating system provides version %s.",
							   collversionstr, actual_versionstr),
					 errhint("Rebuild all objects in this database that use the default collation and run "
							 "ALTER DATABASE %s REFRESH COLLATION VERSION, "
							 "or build PostgreSQL with the right library version.",
							 quote_identifier(name))));
	}

	/* Make the locale settings visible as GUC variables, too */
	SetConfigOption("lc_collate", collate, PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
	SetConfigOption("lc_ctype", ctype, PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);

	check_strxfrm_bug();

	ReleaseSysCache(tup);
}


/*
 * pg_split_opts -- split a string of options and append it to an argv array
 *
 * The caller is responsible for ensuring the argv array is large enough.  The
 * maximum possible number of arguments added by this routine is
 * (strlen(optstr) + 1) / 2.
 *
 * Because some option values can contain spaces we allow escaping using
 * backslashes, with \\ representing a literal backslash.
 */
void
pg_split_opts(char **argv, int *argcp, const char *optstr)
{
	StringInfoData s;

	initStringInfo(&s);

	while (*optstr)
	{
		bool		last_was_escape = false;

		resetStringInfo(&s);

		/* skip over leading space */
		while (isspace((unsigned char) *optstr))
			optstr++;

		if (*optstr == '\0')
			break;

		/*
		 * Parse a single option, stopping at the first space, unless it's
		 * escaped.
		 */
		while (*optstr)
		{
			if (isspace((unsigned char) *optstr) && !last_was_escape)
				break;

			if (!last_was_escape && *optstr == '\\')
				last_was_escape = true;
			else
			{
				last_was_escape = false;
				appendStringInfoChar(&s, *optstr);
			}

			optstr++;
		}

		/* now store the option in the next argv[] position */
		argv[(*argcp)++] = pstrdup(s.data);
	}

	pfree(s.data);
}

/*
 * Initialize MaxBackends value from config options.
 *
 * This must be called after modules have had the chance to alter GUCs in
 * shared_preload_libraries and before shared memory size is determined.
 *
 * Note that in EXEC_BACKEND environment, the value is passed down from
 * postmaster to subprocesses via BackendParameters in SubPostmasterMain; only
 * postmaster itself and processes not under postmaster control should call
 * this.
 */
void
InitializeMaxBackends(void)
{
	Assert(MaxBackends == 0);

	/* the extra unit accounts for the autovacuum launcher */
	MaxBackends = MaxConnections + autovacuum_max_workers + 1 +
		max_worker_processes + max_wal_senders;

	/* internal error because the values were all checked previously */
	if (MaxBackends > MAX_BACKENDS)
		elog(ERROR, "too many backends configured");
}

/*
 * Early initialization of a backend (either standalone or under postmaster).
 * This happens even before InitPostgres.
 *
 * This is separate from InitPostgres because it is also called by auxiliary
 * processes, such as the background writer process, which may not call
 * InitPostgres at all.
 */
void
BaseInit(void)
{
	Assert(MyProc != NULL);

	/*
	 * Initialize our input/output/debugging file descriptors.
	 */
	DebugFileOpen();

	/*
	 * Initialize file access. Done early so other subsystems can access
	 * files.
	 */
	InitFileAccess();

	/*
	 * Initialize statistics reporting. This needs to happen early to ensure
	 * that pgstat's shutdown callback runs after the shutdown callbacks of
	 * all subsystems that can produce stats (like e.g. transaction commits
	 * can).
	 */
	pgstat_initialize();

	/* Do local initialization of storage and buffer managers */
	InitSync();
	smgrinit();
	InitBufferPoolAccess();

	/*
	 * Initialize temporary file access after pgstat, so that the temporary
	 * file shutdown hook can report temporary file statistics.
	 */
	InitTemporaryFileAccess();

	/*
	 * Initialize local buffers for WAL record construction, in case we ever
	 * try to insert XLOG.
	 */
	InitXLogInsert();

	/*
	 * Initialize replication slots after pgstat. The exit hook might need to
	 * drop ephemeral slots, which in turn triggers stats reporting.
	 */
	ReplicationSlotInitialize();
}


/* --------------------------------
 * InitPostgres
 *		Initialize POSTGRES.
 *
 * Parameters:
 *	in_dbname, dboid: specify database to connect to, as described below
 *	username, useroid: specify role to connect as, as described below
 *	load_session_libraries: TRUE to honor [session|local]_preload_libraries
 *	override_allow_connections: TRUE to connect despite !datallowconn
 *	out_dbname: optional output parameter, see below; pass NULL if not used
 *
 * The database can be specified by name, using the in_dbname parameter, or by
 * OID, using the dboid parameter.  Specify NULL or InvalidOid respectively
 * for the unused parameter.  If dboid is provided, the actual database
 * name can be returned to the caller in out_dbname.  If out_dbname isn't
 * NULL, it must point to a buffer of size NAMEDATALEN.
 *
 * Similarly, the role can be passed by name, using the username parameter,
 * or by OID using the useroid parameter.
 *
 * In bootstrap mode the database and username parameters are NULL/InvalidOid.
 * The autovacuum launcher process doesn't specify these parameters either,
 * because it only goes far enough to be able to read pg_database; it doesn't
 * connect to any particular database.  An autovacuum worker specifies a
 * database but not a username; conversely, a physical walsender specifies
 * username but not database.
 *
 * By convention, load_session_libraries should be passed as true in
 * "interactive" sessions (including standalone backends), but false in
 * background processes such as autovacuum.  Note in particular that it
 * shouldn't be true in parallel worker processes; those have another
 * mechanism for replicating their leader's set of loaded libraries.
 *
 * We expect that InitProcess() was already called, so we already have a
 * PGPROC struct ... but it's not completely filled in yet.
 *
 * YB extension: session_id. If greater than zero, connect local YbSession
 * to existing YBClientSession instance in TServer, rather than requesting new.
 * Helpful to initialize background worker backends that need to share state.
 *
 * Note:
 *		Be very careful with the order of calls in the InitPostgres function.
 * --------------------------------
 */
/* YB_TODO(neil) Double check the merged in this file.
 * Both Postgres and Yb refactor code, so merging mistakes are possible.
 * NOTE: Latest change in master might not be merged. Check "YB::master" code again for new changes.
 */
void
InitPostgres(const char *in_dbname, Oid dboid,
			 const char *username, Oid useroid,
			 bool load_session_libraries,
			 bool override_allow_connections,
			 char *out_dbname,
			 uint64_t *session_id)
{
	bool sys_table_prefetching_started = false;
	PG_TRY();
	{
		InitPostgresImpl(
			in_dbname, dboid, username, useroid, load_session_libraries,
			override_allow_connections, out_dbname, session_id,
			&sys_table_prefetching_started);
	}
	PG_CATCH();
	{
		YbEnsureSysTablePrefetchingStopped();
		PG_RE_THROW();
	}
	PG_END_TRY();
	YbEnsureSysTablePrefetchingStopped();
}

static void
InitPostgresImpl(const char *in_dbname, Oid dboid,
				 const char *username, Oid useroid,
				 bool load_session_libraries,
				 bool override_allow_connections,
				 char *out_dbname,
				 uint64_t *session_id,
				 bool* yb_sys_table_prefetching_started)
{
	bool		bootstrap = IsBootstrapProcessingMode();
	bool		am_superuser;
	char	   *fullpath;
	char		dbname[NAMEDATALEN];

	elog(DEBUG3, "InitPostgres");

	/*
	 * Add my PGPROC struct to the ProcArray.
	 *
	 * Once I have done this, I am visible to other backends!
	 */
	InitProcessPhase2();

	/*
	 * Initialize my entry in the shared-invalidation manager's array of
	 * per-backend data.
	 *
	 * Sets up MyBackendId, a unique backend identifier.
	 */
	MyBackendId = InvalidBackendId;

	SharedInvalBackendInit(false);

	if (MyBackendId > MaxBackends || MyBackendId <= 0)
		elog(FATAL, "bad backend ID: %d", MyBackendId);

	/* Now that we have a BackendId, we can participate in ProcSignal */
	ProcSignalInit(MyBackendId);

	/*
	 * Also set up timeout handlers needed for backend operation.  We need
	 * these in every case except bootstrap.
	 */
	if (!bootstrap)
	{
		RegisterTimeout(DEADLOCK_TIMEOUT, CheckDeadLockAlert);
		RegisterTimeout(STATEMENT_TIMEOUT, StatementTimeoutHandler);
		RegisterTimeout(LOCK_TIMEOUT, LockTimeoutHandler);
		RegisterTimeout(IDLE_IN_TRANSACTION_SESSION_TIMEOUT,
						IdleInTransactionSessionTimeoutHandler);
		RegisterTimeout(IDLE_SESSION_TIMEOUT, IdleSessionTimeoutHandler);
		RegisterTimeout(CLIENT_CONNECTION_CHECK_TIMEOUT, ClientCheckTimeoutHandler);
		RegisterTimeout(IDLE_STATS_UPDATE_TIMEOUT,
						IdleStatsUpdateTimeoutHandler);
	}

	MyProc->ybInitializationCompleted = true;

	/*
	 * If this is either a bootstrap process or a standalone backend, start up
	 * the XLOG machinery, and register to have it closed down at exit. In
	 * other cases, the startup process is responsible for starting up the
	 * XLOG machinery, and the checkpointer for closing it down.
	 */
	if (!IsUnderPostmaster)
	{
		/*
		 * We don't yet have an aux-process resource owner, but StartupXLOG
		 * and ShutdownXLOG will need one.  Hence, create said resource owner
		 * (and register a callback to clean it up after ShutdownXLOG runs).
		 */
		CreateAuxProcessResourceOwner();

		StartupXLOG();
		/* Release (and warn about) any buffer pins leaked in StartupXLOG */
		ReleaseAuxProcessResources(true);
		/* Reset CurrentResourceOwner to nothing for the moment */
		CurrentResourceOwner = NULL;

		/*
		 * Use before_shmem_exit() so that ShutdownXLOG() can rely on DSM
		 * segments etc to work (which in turn is required for pgstats).
		 */
		before_shmem_exit(pgstat_before_server_shutdown, 0);
		before_shmem_exit(ShutdownXLOG, 0);
	}

	/*
	 * Initialize the relation cache and the system catalog caches.  Note that
	 * no catalog access happens here; we only set up the hashtable structure.
	 * We must do this before starting a transaction because transaction abort
	 * would try to touch these hashtables.
	 */
	RelationCacheInitialize();
	InitCatalogCache();
	InitPlanCache();

	if (YBIsEnabledInPostgresEnvVar())
		YbInitPgInheritsCache();

	/* Initialize portal manager */
	EnablePortalManager();

	/* Initialize status reporting */
	pgstat_beinit();

	/*
	 * Set client_addr and client_host in ASH metadata which will remain
	 * constant throughout the session. We don't want to do this during
	 * bootstrap because it won't have client address anyway.
	 */
	if (YbAshIsClientAddrSet())
		YbAshSetOneTimeMetadata();

	/* Connect to YugaByte cluster. */
	if (bootstrap)
		YBInitPostgresBackend("postgres", "", username, session_id);
	else
		YBInitPostgresBackend("postgres", in_dbname, username, session_id);

	if (IsYugaByteEnabled() && !bootstrap)
	{
		HandleYBStatus(YBCPgTableExists(Template1DbOid,
										YbRoleProfileRelationId,
										&YbLoginProfileCatalogsExist));

		/* TODO (dmitry): Next call of the YBIsDBCatalogVersionMode function is
		 * kind of a hack and must be removed. This function is called before
		 * starting prefetching because for now switching into DB catalog
		 * version mode is impossible in case prefething is started.
		 */
		YBIsDBCatalogVersionMode();
		YBCStartSysTablePrefetchingNoCache();
		YbRegisterSysTableForPrefetching(AuthIdRelationId);   // pg_authid
		YbRegisterSysTableForPrefetching(DatabaseRelationId); // pg_database

		if (*YBCGetGFlags()->ysql_enable_profile && YbLoginProfileCatalogsExist)
		{
			YbRegisterSysTableForPrefetching(
				YbProfileRelationId);     // pg_yb_profile
			YbRegisterSysTableForPrefetching(
				YbRoleProfileRelationId); // pg_yb_role_profile
		}
		YbTryRegisterCatalogVersionTableForPrefetching();

		HandleYBStatus(YBCPrefetchRegisteredSysTables());
		/*
		 * If per database catalog version mode is enabled, this will load the
		 * catalog version of template1. It is fine because at this time we
		 * only read shared relations and therefore can use any database OID.
		 * We will update yb_catalog_cache_version to match MyDatabaseId once
		 * the latter is resolved so we will never use the catalog version of
		 * template1 to query relations that are private to MyDatabaseId.
		 */
		YbUpdateCatalogCacheVersion(YbGetMasterCatalogVersion());
	}
	/*
	 * Load relcache entries for the shared system catalogs.  This must create
	 * at least entries for pg_database and catalogs used for authentication.
	 */
	RelationCacheInitializePhase2();

	/*
	 * Set up process-exit callback to do pre-shutdown cleanup.  This is the
	 * one of the first before_shmem_exit callbacks we register; thus, this
	 * will be one the last things we do before low-level modules like the
	 * buffer manager begin to close down.  We need to have this in place
	 * before we begin our first transaction --- if we fail during the
	 * initialization transaction, as is entirely possible, we need the
	 * AbortTransaction call to clean up.
	 */
	before_shmem_exit(ShutdownPostgres, 0);

	/* The autovacuum launcher is done here */
	if (IsAutoVacuumLauncherProcess())
	{
		/* report this backend in the PgBackendStatus array */
		pgstat_bestart();

		return;
	}

	/*
	 * Start a new transaction here before first access to db, and get a
	 * snapshot.  We don't have a use for the snapshot itself, but we're
	 * interested in the secondary effect that it sets RecentGlobalXmin. (This
	 * is critical for anything that reads heap pages, because HOT may decide
	 * to prune them even if the process doesn't attempt to modify any
	 * tuples.)
	 *
	 * FIXME: This comment is inaccurate / the code buggy. A snapshot that is
	 * not pushed/active does not reliably prevent HOT pruning (->xmin could
	 * e.g. be cleared when cache invalidations are processed).
	 */
	if (!bootstrap)
	{
		/* statement_timestamp must be set for timeouts to work correctly */
		SetCurrentStatementStartTimestamp();
		StartTransactionCommand();

		/*
		 * transaction_isolation will have been set to the default by the
		 * above.  If the default is "serializable", and we are in hot
		 * standby, we will fail if we don't change it to something lower.
		 * Fortunately, "read committed" is plenty good enough.
		 */
		XactIsoLevel = XACT_READ_COMMITTED;

		(void) GetTransactionSnapshot();
	}

	/*
	 * Perform client authentication if necessary, then figure out our
	 * postgres user ID, and see if we are a superuser.
	 *
	 * In standalone mode and in autovacuum worker processes, we use a fixed
	 * ID, otherwise we figure it out from the authenticated user name.
	 */
	if (bootstrap || IsAutoVacuumWorkerProcess())
	{
		InitializeSessionUserIdStandalone();
		am_superuser = true;
	}
	else if (!IsUnderPostmaster)
	{
		InitializeSessionUserIdStandalone();
		am_superuser = true;
		if (!ThereIsAtLeastOneRole())
			ereport(WARNING,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("no roles are defined in this database system"),
					 errhint("You should immediately run CREATE USER \"%s\" SUPERUSER;.",
							 username != NULL ? username : "postgres")));
	}
	else if (IsBackgroundWorker)
	{
		if (username == NULL && !OidIsValid(useroid))
		{
			InitializeSessionUserIdStandalone();
			am_superuser = true;
		}
		else
		{
			InitializeSessionUserId(username, useroid);
			am_superuser = superuser();
		}
	}
	else
	{
		/* normal multiuser case */
		Assert(MyProcPort != NULL);
		PerformAuthentication(MyProcPort);
		InitializeSessionUserId(username, useroid);
		am_superuser = superuser();

		/*
		 * In YSQL upgrade mode (uses tserver auth method), we allow connecting to
		 * databases with disabled connections (normally it's just template0).
		 */
		override_allow_connections = override_allow_connections ||
									 MyProcPort->yb_is_tserver_auth_method;
	}

	/*
	 * Binary upgrades only allowed super-user connections
	 */
	if (IsBinaryUpgrade && !am_superuser)
	{
		ereport(FATAL,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to connect in binary upgrade mode")));
	}

	/*
	 * The last few connection slots are reserved for superusers.  Replication
	 * connections are drawn from slots reserved with max_wal_senders and not
	 * limited by max_connections or superuser_reserved_connections.
	 */
	if (!am_superuser && !am_walsender &&
		ReservedBackends > 0 &&
		!HaveNFreeProcs(ReservedBackends))
		ereport(FATAL,
				(errcode(ERRCODE_TOO_MANY_CONNECTIONS),
				 errmsg("remaining connection slots are reserved for non-replication superuser connections")));

	/* Check replication permissions needed for walsender processes. */
	if (am_walsender)
	{
		Assert(!bootstrap);

		if (!superuser() && !has_rolreplication(GetUserId()))
			ereport(FATAL,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser or replication role to start walsender")));
	}

	/*
	 * If this is a plain walsender only supporting physical replication, we
	 * don't want to connect to any particular database. Just finish the
	 * backend startup by processing any options from the startup packet, and
	 * we're done.
	 */
	if (am_walsender && !am_db_walsender)
	{
		/* process any options passed in the startup packet */
		if (MyProcPort != NULL)
			process_startup_options(MyProcPort, am_superuser);

		/* Apply PostAuthDelay as soon as we've read all options */
		if (PostAuthDelay > 0)
			pg_usleep(PostAuthDelay * 1000000L);

		/* initialize client encoding */
		InitializeClientEncoding();

		/* report this backend in the PgBackendStatus array */
		pgstat_bestart();

		/* close the transaction we started above */
		CommitTransactionCommand();

		return;
	}

	/*
	 * Set up the global variables holding database id and default tablespace.
	 * But note we won't actually try to touch the database just yet.
	 *
	 * We take a shortcut in the bootstrap case, otherwise we have to look up
	 * the db's entry in pg_database.
	 */
	if (bootstrap)
	{
		MyDatabaseId = Template1DbOid;
		MyDatabaseTableSpace = DEFAULTTABLESPACE_OID;
	}
	else if (in_dbname != NULL)
	{
		HeapTuple	tuple;
		Form_pg_database dbform;

		tuple = GetDatabaseTuple(in_dbname);
		if (!HeapTupleIsValid(tuple))
			ereport(FATAL,
					(errcode(ERRCODE_UNDEFINED_DATABASE),
					 errmsg("database \"%s\" does not exist", in_dbname)));
		dbform = (Form_pg_database) GETSTRUCT(tuple);
		MyDatabaseId = dbform->oid;
		MyDatabaseTableSpace = dbform->dattablespace;
		/* take database name from the caller, just for paranoia */
		strlcpy(dbname, in_dbname, sizeof(dbname));
		if (IsYugaByteEnabled())
			SetDatabaseEncoding(dbform->encoding);
	}
	else if (OidIsValid(dboid))
	{
		/* caller specified database by OID */
		HeapTuple	tuple;
		Form_pg_database dbform;

		tuple = GetDatabaseTupleByOid(dboid);
		if (!HeapTupleIsValid(tuple))
			ereport(FATAL,
					(errcode(ERRCODE_UNDEFINED_DATABASE),
					 errmsg("database %u does not exist", dboid)));
		dbform = (Form_pg_database) GETSTRUCT(tuple);
		MyDatabaseId = dbform->oid;
		MyDatabaseTableSpace = dbform->dattablespace;
		Assert(MyDatabaseId == dboid);
		strlcpy(dbname, NameStr(dbform->datname), sizeof(dbname));
		/* pass the database name back to the caller */
		if (out_dbname)
			strcpy(out_dbname, dbname);
		if (IsYugaByteEnabled())
			SetDatabaseEncoding(dbform->encoding);
	}
	else
	{
		/*
		 * If this is a background worker not bound to any particular
		 * database, we're done now.  Everything that follows only makes sense
		 * if we are bound to a specific database.  We do need to close the
		 * transaction we started before returning.
		 */
		if (!bootstrap)
		{
			pgstat_bestart();
			CommitTransactionCommand();
		}
		return;
	}

	if (MyDatabaseId != Template1DbOid && YBIsDBCatalogVersionMode())
	{
		/*
		 * Here we assume that the entire table pg_yb_catalog_version is
		 * prefetched. Note that in this case YbGetMasterCatalogVersion()
		 * returns the prefetched catalog version of MyDatabaseId which is
		 * consistent with all the other tables that are prefetched.
		 */
		uint64_t master_catalog_version = YbGetMasterCatalogVersion();
		Assert(master_catalog_version > YB_CATCACHE_VERSION_UNINITIALIZED);
		YbUpdateCatalogCacheVersion(master_catalog_version);
	}

	/*
	 * Now, take a writer's lock on the database we are trying to connect to.
	 * If there is a concurrently running DROP DATABASE on that database, this
	 * will block us until it finishes (and has committed its update of
	 * pg_database).
	 *
	 * Note that the lock is not held long, only until the end of this startup
	 * transaction.  This is OK since we will advertise our use of the
	 * database in the ProcArray before dropping the lock (in fact, that's the
	 * next thing to do).  Anyone trying a DROP DATABASE after this point will
	 * see us in the array once they have the lock.  Ordering is important for
	 * this because we don't want to advertise ourselves as being in this
	 * database until we have the lock; otherwise we create what amounts to a
	 * deadlock with CountOtherDBBackends().
	 *
	 * Note: use of RowExclusiveLock here is reasonable because we envision
	 * our session as being a concurrent writer of the database.  If we had a
	 * way of declaring a session as being guaranteed-read-only, we could use
	 * AccessShareLock for such sessions and thereby not conflict against
	 * CREATE DATABASE.
	 */
	if (!bootstrap)
		LockSharedObject(DatabaseRelationId, MyDatabaseId, 0,
						 RowExclusiveLock);

	/*
	 * Now we can mark our PGPROC entry with the database ID.
	 *
	 * We assume this is an atomic store so no lock is needed; though actually
	 * things would work fine even if it weren't atomic.  Anyone searching the
	 * ProcArray for this database's ID should hold the database lock, so they
	 * would not be executing concurrently with this store.  A process looking
	 * for another database's ID could in theory see a chance match if it read
	 * a partially-updated databaseId value; but as long as all such searches
	 * wait and retry, as in CountOtherDBBackends(), they will certainly see
	 * the correct value on their next try.
	 */
	MyProc->databaseId = MyDatabaseId;

	/* YB: Set the dbid in ASH metadata */
	if (IsYugaByteEnabled() && yb_enable_ash)
		YbAshSetDatabaseId(MyDatabaseId);

	/*
	 * We established a catalog snapshot while reading pg_authid and/or
	 * pg_database; but until we have set up MyDatabaseId, we won't react to
	 * incoming sinval messages for unshared catalogs, so we won't realize it
	 * if the snapshot has been invalidated.  Assume it's no good anymore.
	 */
	InvalidateCatalogSnapshot();
	if (IsYugaByteEnabled() && YBCIsSysTablePrefetchingStarted())
		YBCStopSysTablePrefetching();

	if (YBIsDBLogicalClientVersionMode())
	{
		int32_t logical_client_version = YbGetMasterLogicalClientVersion();
		elog(DEBUG1, "logical_client_version = %d", logical_client_version);
		YbSetLogicalClientCacheVersion(logical_client_version);
	}

	/*
	 * Recheck pg_database to make sure the target database hasn't gone away.
	 * If there was a concurrent DROP DATABASE, this ensures we will die
	 * cleanly without creating a mess.
	 * In YB mode DB existance is checked on cache load/refresh.
	 */
	if (!IsYugaByteEnabled() && !bootstrap)
	{
		HeapTuple	tuple;

		tuple = GetDatabaseTuple(dbname);
		if (!HeapTupleIsValid(tuple) ||
			MyDatabaseId != ((Form_pg_database) GETSTRUCT(tuple))->oid ||
			MyDatabaseTableSpace != ((Form_pg_database) GETSTRUCT(tuple))->dattablespace)
			ereport(FATAL,
					(errcode(ERRCODE_UNDEFINED_DATABASE),
					 errmsg("database \"%s\" does not exist", dbname),
					 errdetail("It seems to have just been dropped or renamed.")));
	}

	/* No local physical path for the database in YugaByte mode */
	if (!IsYugaByteEnabled())
	{
		/*
		 * Now we should be able to access the database directory safely. Verify
		 * it's there and looks reasonable.
		 */
		fullpath = GetDatabasePath(MyDatabaseId, MyDatabaseTableSpace);

		if (!bootstrap)
		{
			if (access(fullpath, F_OK) == -1)
			{
				if (errno == ENOENT)
					ereport(FATAL,
							(errcode(ERRCODE_UNDEFINED_DATABASE),
							 errmsg("database \"%s\" does not exist",
									dbname),
							 errdetail("The database subdirectory \"%s\" is missing.",
									   fullpath)));
				else
					ereport(FATAL,
							(errcode_for_file_access(),
							 errmsg("could not access directory \"%s\": %m",
									fullpath)));
			}

			ValidatePgVersion(fullpath);
		}

		SetDatabasePath(fullpath);
		pfree(fullpath);
	}

	/*
	 * It's now possible to do real access to the system catalogs.
	 *
	 * Load relcache entries for the system catalogs.  This must create at
	 * least the minimum set of "nailed-in" cache entries.
	 */
	// See if tablegroup catalog exists - needs to happen before cache fully initialized.
	if (IsYugaByteEnabled() && !bootstrap)
		HandleYBStatus(YBCPgTableExists(
			MyDatabaseId, YbTablegroupRelationId, &YbTablegroupCatalogExists));

	RelationCacheInitializePhase3();

	/*
	 * Also cache whether the database is colocated for optimization purposes.
	 */
	if (IsYugaByteEnabled() && !IsBootstrapProcessingMode())
	{
		MyDatabaseColocated = YbIsDatabaseColocated(MyDatabaseId, &MyColocatedDatabaseLegacy);
	}

	/* set up ACL framework (so CheckMyDatabase can check permissions) */
	initialize_acl();

	/*
	 * Re-read the pg_database row for our database, check permissions and set
	 * up database-specific GUC settings.  We can't do this until all the
	 * database-access infrastructure is up.  (Also, it wants to know if the
	 * user is a superuser, so the above stuff has to happen first.)
	 */
	if (!bootstrap)
		CheckMyDatabase(dbname, am_superuser, override_allow_connections);

	/*
	 * We are done with the authentication. Now we can send the db oid to the
	 * connection, process the startup options and return.
	 *
	 * This block of code must be after the values of global variables such as
	 * MyDatabaseId are set, since YbCreateClientIdWithDatabaseOid relies on it.
	 */
	if (yb_is_auth_backend)
	{
		/*
		 * Initialize the client id and also send the db oid back to the
		 * connection manager.
		 */
		YbCreateClientIdWithDatabaseOid(MyDatabaseId);

		/*
		 * Process any options passed in the startup packet. This is important
		 * to do here since this is what sets the GUC values sent to the client.
		 */
		if (MyProcPort != NULL)
			process_startup_options(MyProcPort, am_superuser);

		if (YBIsDBLogicalClientVersionMode())
			SendLogicalClientCacheVersionToFrontend();

		/* Process pg_db_role_setting options */
		process_settings(MyDatabaseId, GetSessionUserId());

		/* close the transaction we started above */
		CommitTransactionCommand();

		/*
		 * The auth-backend is only responsible for authentication, so we skip
		 * the remaining steps below.
		 */
		return;
	}

	/*
	 * Now process any command-line switches and any additional GUC variable
	 * settings passed in the startup packet.   We couldn't do this before
	 * because we didn't know if client is a superuser.
	 */
	if (MyProcPort != NULL)
		process_startup_options(MyProcPort, am_superuser);

	/* Process pg_db_role_setting options */
	process_settings(MyDatabaseId, GetSessionUserId());

	/* Apply PostAuthDelay as soon as we've read all options */
	if (PostAuthDelay > 0)
		pg_usleep(PostAuthDelay * 1000000L);

	/*
	 * Initialize various default states that can't be set up until we've
	 * selected the active user and gotten the right GUC settings.
	 */

	/* set default namespace search path */
	InitializeSearchPath();

	/* initialize client encoding */
	InitializeClientEncoding();

	/* Initialize this backend's session state. */
	InitializeSession();

	/*
	 * If this is an interactive session, load any libraries that should be
	 * preloaded at backend start.  Since those are determined by GUCs, this
	 * can't happen until GUC settings are complete, but we want it to happen
	 * during the initial transaction in case anything that requires database
	 * access needs to be done.
	 */
	if (load_session_libraries)
		process_session_preload_libraries();

	/* report this backend in the PgBackendStatus array */
	if (!bootstrap)
		pgstat_bestart();

	/* close the transaction we started above */
	if (!bootstrap)
		CommitTransactionCommand();
}

static void
YbEnsureSysTablePrefetchingStopped()
{
	if (IsYugaByteEnabled() && YBCIsSysTablePrefetchingStarted())
		YBCStopSysTablePrefetching();
}

/*
 * Process any command-line switches and any additional GUC variable
 * settings passed in the startup packet.
 */
static void
process_startup_options(Port *port, bool am_superuser)
{
	GucContext	gucctx;
	ListCell   *gucopts;

	gucctx = am_superuser ? PGC_SU_BACKEND : PGC_BACKEND;

	/*
	 * First process any command-line switches that were included in the
	 * startup packet, if we are in a regular backend.
	 */
	if (port->cmdline_options != NULL)
	{
		/*
		 * The maximum possible number of commandline arguments that could
		 * come from port->cmdline_options is (strlen + 1) / 2; see
		 * pg_split_opts().
		 */
		char	  **av;
		int			maxac;
		int			ac;

		maxac = 2 + (strlen(port->cmdline_options) + 1) / 2;

		av = (char **) palloc(maxac * sizeof(char *));
		ac = 0;

		av[ac++] = "postgres";

		pg_split_opts(av, &ac, port->cmdline_options);

		av[ac] = NULL;

		Assert(ac < maxac);

		(void) process_postgres_switches(ac, av, gucctx, NULL);
	}

	/*
	 * Process any additional GUC variable settings passed in startup packet.
	 * These are handled exactly like command-line variables.
	 */
	gucopts = list_head(port->guc_options);
	while (gucopts)
	{
		char	   *name;
		char	   *value;

		name = lfirst(gucopts);
		gucopts = lnext(port->guc_options, gucopts);

		value = lfirst(gucopts);
		gucopts = lnext(port->guc_options, gucopts);

		SetConfigOption(name, value, gucctx, PGC_S_CLIENT);
	}
}

/*
 * Load GUC settings from pg_db_role_setting.
 *
 * We try specific settings for the database/role combination, as well as
 * general for this database and for this user.
 */
static void
process_settings(Oid databaseid, Oid roleid)
{
	Relation	relsetting;
	Snapshot	snapshot;

	if (!IsUnderPostmaster)
		return;

	relsetting = table_open(DbRoleSettingRelationId, AccessShareLock);

	/* read all the settings under the same snapshot for efficiency */
	snapshot = RegisterSnapshot(GetCatalogSnapshot(DbRoleSettingRelationId));

	/* Later settings are ignored if set earlier. */
	ApplySetting(snapshot, databaseid, roleid, relsetting, PGC_S_DATABASE_USER);
	ApplySetting(snapshot, InvalidOid, roleid, relsetting, PGC_S_USER);
	ApplySetting(snapshot, databaseid, InvalidOid, relsetting, PGC_S_DATABASE);
	ApplySetting(snapshot, InvalidOid, InvalidOid, relsetting, PGC_S_GLOBAL);

	UnregisterSnapshot(snapshot);
	table_close(relsetting, AccessShareLock);
}

/*
 * Backend-shutdown callback.  Do cleanup that we want to be sure happens
 * before all the supporting modules begin to nail their doors shut via
 * their own callbacks.
 *
 * User-level cleanup, such as temp-relation removal and UNLISTEN, happens
 * via separate callbacks that execute before this one.  We don't combine the
 * callbacks because we still want this one to happen if the user-level
 * cleanup fails.
 */
static void
ShutdownPostgres(int code, Datum arg)
{
	/* Make sure we've killed any active transaction */
	AbortOutOfAnyTransaction();

	/*
	 * User locks are not released by transaction end, so be sure to release
	 * them explicitly.
	 */
	LockReleaseAll(USER_LOCKMETHOD, true);
}


/*
 * STATEMENT_TIMEOUT handler: trigger a query-cancel interrupt.
 */
static void
StatementTimeoutHandler(void)
{
	int			sig = SIGINT;

	/*
	 * During authentication the timeout is used to deal with
	 * authentication_timeout - we want to quit in response to such timeouts.
	 */
	if (ClientAuthInProgress)
		sig = SIGTERM;

#ifdef HAVE_SETSID
	/* try to signal whole process group */
	kill(-MyProcPid, sig);
#endif
	kill(MyProcPid, sig);
}

/*
 * LOCK_TIMEOUT handler: trigger a query-cancel interrupt.
 */
static void
LockTimeoutHandler(void)
{
#ifdef HAVE_SETSID
	/* try to signal whole process group */
	kill(-MyProcPid, SIGINT);
#endif
	kill(MyProcPid, SIGINT);
}

static void
IdleInTransactionSessionTimeoutHandler(void)
{
	IdleInTransactionSessionTimeoutPending = true;
	InterruptPending = true;
	SetLatch(MyLatch);
}

static void
IdleSessionTimeoutHandler(void)
{
	IdleSessionTimeoutPending = true;
	InterruptPending = true;
	SetLatch(MyLatch);
}

static void
IdleStatsUpdateTimeoutHandler(void)
{
	IdleStatsUpdateTimeoutPending = true;
	InterruptPending = true;
	SetLatch(MyLatch);
}

static void
ClientCheckTimeoutHandler(void)
{
	CheckClientConnectionPending = true;
	InterruptPending = true;
	SetLatch(MyLatch);
}

/*
 * Returns true if at least one role is defined in this database cluster.
 */
static bool
ThereIsAtLeastOneRole(void)
{
	Relation	pg_authid_rel;
	TableScanDesc scan;
	bool		result;

	pg_authid_rel = table_open(AuthIdRelationId, AccessShareLock);

	scan = table_beginscan_catalog(pg_authid_rel, 0, NULL);
	result = (heap_getnext(scan, ForwardScanDirection) != NULL);

	table_endscan(scan);
	table_close(pg_authid_rel, AccessShareLock);

	return result;
}
