/*
 * pg_partman_bgw.c
 *
 * A background worker process for the pg_partman extension to allow
 * partition maintenance to be scheduled and run within the database
 * itself without required a third-party scheduler (ex. cron)
 *
 */

#include "postgres.h"

/* These are always necessary for a bgworker */
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */
#include "access/xact.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "pgstat.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"
#include "tcop/utility.h"

#if (PG_VERSION_NUM >= 100000)
#include "utils/varlena.h"
#endif


PG_MODULE_MAGIC;

void        _PG_init(void);
void        pg_partman_bgw_main(Datum);
void        pg_partman_bgw_run_maint(Datum);

/* flags set by signal handlers */
static volatile sig_atomic_t got_sighup = false;
static volatile sig_atomic_t got_sigterm = false;

/* GUC variables */
static int pg_partman_bgw_interval = 3600; // Default hourly
static char *pg_partman_bgw_role = "postgres"; // Default to postgres role

// Do not analyze by default on PG11+
#if (PG_VERSION_NUM >= 110000)
static char *pg_partman_bgw_analyze = "off";
#else
static char *pg_partman_bgw_analyze = "on";
#endif

static char *pg_partman_bgw_jobmon = "on";
static char *pg_partman_bgw_dbname = NULL;

#if (PG_VERSION_NUM < 100500)
static bool (*split_function_ptr)(char *, char, List **) = &SplitIdentifierString;
#else
static bool (*split_function_ptr)(char *, char, List **) = &SplitGUCList;
#endif

/*
 * Signal handler for SIGTERM
 *      Set a flag to let the main loop to terminate, and set our latch to wake
 *      it up.
 */
static void
pg_partman_bgw_sigterm(SIGNAL_ARGS)
{
    int         save_errno = errno;

    got_sigterm = true;

    if (MyProc)
        SetLatch(&MyProc->procLatch);

    errno = save_errno;
}

/*
 * Signal handler for SIGHUP
 *      Set a flag to tell the main loop to reread the config file, and set
 *      our latch to wake it up.
 */
static void pg_partman_bgw_sighup(SIGNAL_ARGS) {
    int         save_errno = errno;

    got_sighup = true;

    if (MyProc)
        SetLatch(&MyProc->procLatch);

    errno = save_errno;
}

/*
 * Entrypoint of this module.
 */
void
_PG_init(void)
{
    BackgroundWorker worker;

    DefineCustomIntVariable("pg_partman_bgw.interval",
                            "How often run_maintenance() is called (in seconds).",
                            NULL,
                            &pg_partman_bgw_interval,
                            3600,
                            1,
                            INT_MAX,
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);

    #if (PG_VERSION_NUM >= 110000)
    DefineCustomStringVariable("pg_partman_bgw.analyze",
                            "Whether to run an analyze on a partition set whenever a new partition is created during run_maintenance(). Set to 'on' to send TRUE (default). Set to 'off' to send FALSE.",
                            NULL,
                            &pg_partman_bgw_analyze,
                            "off",
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);
    #else
    DefineCustomStringVariable("pg_partman_bgw.analyze",
                            "Whether to run an analyze on a partition set whenever a new partition is created during run_maintenance(). Set to 'on' to send TRUE (default). Set to 'off' to send FALSE.",
                            NULL,
                            &pg_partman_bgw_analyze,
                            "on",
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);
    #endif  

    DefineCustomStringVariable("pg_partman_bgw.dbname",
                            "CSV list of specific databases in the cluster to run pg_partman BGW on. This setting is not required and when not set, pg_partman will dynamically figure out which ones to run on. If set, forces the BGW to only run on these specific databases and never any others. Recommend leaving this unset unless necessary.",
                            NULL,
                            &pg_partman_bgw_dbname,
                            NULL,
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);

    DefineCustomStringVariable("pg_partman_bgw.jobmon",
                            "Whether to log run_maintenance() calls to pg_jobmon if it is installed. Set to 'on' to send TRUE (default). Set to 'off' to send FALSE.",
                            NULL,
                            &pg_partman_bgw_jobmon,
                            "on",
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);

    DefineCustomStringVariable("pg_partman_bgw.role",
                               "Role to be used by BGW. Must have execute permissions on run_maintenance()",
                               NULL,
                               &pg_partman_bgw_role,
                               "postgres",
                               PGC_SIGHUP,
                               0,
                               NULL,
                               NULL,
                               NULL);

/* Kept as comment for reference for future development
    DefineCustomStringVariable("pg_partman_bgw.maintenance_db",
                            "The BGW requires connecting to a local database for reading system catalogs. By default it uses template1. You can change that with this setting if needed.",
                            NULL,
                            &pg_partman_bgw_maint_db,
                            "template1",
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);
*/

    if (!process_shared_preload_libraries_in_progress)
        return;

    // Start BGW when database starts
    sprintf(worker.bgw_name, "pg_partman master background worker");
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
        BGWORKER_BACKEND_DATABASE_CONNECTION;
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
    worker.bgw_restart_time = 600;
    #if (PG_VERSION_NUM < 100000)
    worker.bgw_main = pg_partman_bgw_main;
    #endif
    #if (PG_VERSION_NUM >= 100000)
    sprintf(worker.bgw_library_name, "pg_partman_bgw");
    sprintf(worker.bgw_function_name, "pg_partman_bgw_main");
    #endif
    worker.bgw_main_arg = CStringGetDatum(pg_partman_bgw_dbname);
    worker.bgw_notify_pid = 0;
    RegisterBackgroundWorker(&worker);

}

void pg_partman_bgw_main(Datum main_arg) {
    StringInfoData buf;

    /* Establish signal handlers before unblocking signals. */
    pqsignal(SIGHUP, pg_partman_bgw_sighup);
    pqsignal(SIGTERM, pg_partman_bgw_sigterm);

    /* We're now ready to receive signals */
    BackgroundWorkerUnblockSignals();

/* Keep for when master requires persistent connection
   elog(LOG, "%s master process initialized with role %s on database %s"
            , MyBgworkerEntry->bgw_name
            , pg_partman_bgw_role
            , pg_partman_bgw_dbname);
*/
    elog(LOG, "%s master process initialized with role %s"
            , MyBgworkerEntry->bgw_name
            , pg_partman_bgw_role);

    initStringInfo(&buf);

    /*
     * Main loop: do this until the SIGTERM handler tells us to terminate
     */
    while (!got_sigterm) {
        BackgroundWorker        worker;
        BackgroundWorkerHandle  *handle;
        BgwHandleStatus         status;
        char                    *rawstring;
        int                     dbcounter;
        int                     rc;
        int                     full_string_length;
        List                    *elemlist;
        ListCell                *l;
        pid_t                   pid;

        /* Using Latch loop method suggested in latch.h
         * Uses timeout flag in WaitLatch() further below instead of sleep to allow clean shutdown */
        ResetLatch(&MyProc->procLatch);

        CHECK_FOR_INTERRUPTS();

        /* In case of a SIGHUP, just reload the configuration. */
        if (got_sighup) {
            got_sighup = false;
            ProcessConfigFile(PGC_SIGHUP);
        }
        elog(DEBUG1, "After sighup check (got_sighup: %d)", got_sighup);

        /* In case of a SIGTERM in middle of loop, stop all further processing and return from BGW function to allow it to exit cleanly. */
        if (got_sigterm) {
            elog(LOG, "pg_partman master BGW received SIGTERM. Shutting down. (got_sigterm: %d)", got_sigterm);
            return;
        }

        // Use method of shared_preload_libraries to split the pg_partman_bgw_dbname string found in src/backend/utils/init/miscinit.c 
        // Need a modifiable copy of string 
        if (pg_partman_bgw_dbname != NULL) {
            rawstring = pstrdup(pg_partman_bgw_dbname);
            // Parse string into list of identifiers 
            if (!(*split_function_ptr)(rawstring, ',', &elemlist)) {
                // syntax error in list 
                pfree(rawstring);
                list_free(elemlist);
                ereport(LOG,
                        (errcode(ERRCODE_SYNTAX_ERROR),
                         errmsg("invalid list syntax in parameter \"pg_partman_bgw.dbname\" in postgresql.conf")));
                return;
            }
            
            dbcounter = 0;
            foreach(l, elemlist) {

                char *dbname = (char *) lfirst(l);
                
                elog(DEBUG1, "Dynamic bgw launch begun for %s (%d)", dbname, dbcounter);
                worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
                    BGWORKER_BACKEND_DATABASE_CONNECTION;
                worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
                worker.bgw_restart_time = BGW_NEVER_RESTART;
                #if (PG_VERSION_NUM < 100000)
                worker.bgw_main = NULL;
                #endif
                sprintf(worker.bgw_library_name, "pg_partman_bgw");
                sprintf(worker.bgw_function_name, "pg_partman_bgw_run_maint");
                full_string_length = snprintf(worker.bgw_name, sizeof(worker.bgw_name),
                              "pg_partman dynamic background worker (dbname=%s)", dbname);
                if (full_string_length >= sizeof(worker.bgw_name)) {
                    /* dbname was truncated, add an ellipsis to denote it */
                    const char truncated_mark[] = "...)";
                    memcpy(worker.bgw_name + sizeof(worker.bgw_name) - sizeof(truncated_mark),
                           truncated_mark, sizeof(truncated_mark));
                }
                worker.bgw_main_arg = Int32GetDatum(dbcounter);
                worker.bgw_notify_pid = MyProcPid;

                dbcounter++;

                elog(DEBUG1, "Registering dynamic background worker...");
                if (!RegisterDynamicBackgroundWorker(&worker, &handle)) {
                    elog(ERROR, "Unable to register dynamic background worker for pg_partman. Consider increasing max_worker_processes if you see this frequently. Main background worker process will try restarting in 10 minutes.");
                }

                elog(DEBUG1, "Waiting for BGW startup...");
                status = WaitForBackgroundWorkerStartup(handle, &pid);
                elog(DEBUG1, "BGW startup status: %d", status);

                if (status == BGWH_STOPPED) {
                    ereport(ERROR,
                            (errcode(ERRCODE_INSUFFICIENT_RESOURCES),
                             errmsg("Could not start dynamic pg_partman background process"),
                           errhint("More details may be available in the server log.")));
                }

                if (status == BGWH_POSTMASTER_DIED) {
                    ereport(ERROR,
                            (errcode(ERRCODE_INSUFFICIENT_RESOURCES),
                          errmsg("Cannot start dynamic pg_partman background processes without postmaster"),
                             errhint("Kill all remaining database processes and restart the database.")));
                }
                Assert(status == BGWH_STARTED);

                #if (PG_VERSION_NUM >= 90500)
                // Shutdown wait function introduced in 9.5. The latch problems this wait fixes are only encountered in 
                // 9.6 and later. So this shouldn't be a problem for 9.4.
                elog(DEBUG1, "Waiting for BGW shutdown...");
                status = WaitForBackgroundWorkerShutdown(handle);
                elog(DEBUG1, "BGW shutdown status: %d", status);
                Assert(status == BGWH_STOPPED);
                #endif
            }

            pfree(rawstring);
            list_free(elemlist);
        } else { // pg_partman_bgw_dbname if null
            elog(DEBUG1, "pg_partman_bgw.dbname GUC is NULL. Nothing to do in main loop.");
        }


        elog(DEBUG1, "Latch status just before waitlatch call: %d", MyProc->procLatch.is_set);

        #if (PG_VERSION_NUM >= 100000)
        rc = WaitLatch(&MyProc->procLatch,
                       WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
                       pg_partman_bgw_interval * 1000L,
                       PG_WAIT_EXTENSION);
        #endif
        #if (PG_VERSION_NUM < 100000)
        rc = WaitLatch(&MyProc->procLatch,
                       WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
                       pg_partman_bgw_interval * 1000L);
        #endif
        /* emergency bailout if postmaster has died */
        if (rc & WL_POSTMASTER_DEATH) {
            proc_exit(1);
        }

        elog(DEBUG1, "Latch status after waitlatch call: %d", MyProc->procLatch.is_set);

    } // end sigterm while

} // end main

/*
 * Unable to pass the database name as a string argument (not sure why yet)
 * Instead, the GUC is parsed both in the main function and below and a counter integer 
 *  is passed to determine which database the BGW will run in.
 */
void pg_partman_bgw_run_maint(Datum arg) {

    char                *analyze;
    char                *dbname = "template1";
    char                *jobmon;
    char                *partman_schema;
    char                *rawstring;
    int                 db_main_counter = DatumGetInt32(arg);
    List                *elemlist;
    int                 ret;
    StringInfoData      buf;

    /* Establish signal handlers before unblocking signals. */
    pqsignal(SIGHUP, pg_partman_bgw_sighup);
    pqsignal(SIGTERM, pg_partman_bgw_sigterm);

    /* We're now ready to receive signals */
    BackgroundWorkerUnblockSignals();

    elog(DEBUG1, "Before parsing dbname GUC in dynamic main func: %s", pg_partman_bgw_dbname);
    rawstring = pstrdup(pg_partman_bgw_dbname);
    elog(DEBUG1, "GUC rawstring copy: %s", rawstring);
    // Parse string into list of identifiers 
    if (!(*split_function_ptr)(rawstring, ',', &elemlist)) {
        // syntax error in list 
        pfree(rawstring);
        list_free(elemlist);
        ereport(LOG,
                (errcode(ERRCODE_SYNTAX_ERROR),
                 errmsg("invalid list syntax in parameter \"pg_partman_bgw.dbname\" in postgresql.conf")));
        return;
    }

    dbname = list_nth(elemlist, db_main_counter);
    elog(DEBUG1, "Parsing dbname list: %s (%d)", dbname, db_main_counter);
    
    if (strcmp(dbname, "template1") == 0) {
        elog(DEBUG1, "Default database name found in dbname local variable (\"template1\").");
    }

    elog(DEBUG1, "Before run_maint initialize connection for db %s", dbname);

    #if (PG_VERSION_NUM < 110000)
    BackgroundWorkerInitializeConnection(dbname, pg_partman_bgw_role);
    #endif
    #if (PG_VERSION_NUM >= 110000)
    BackgroundWorkerInitializeConnection(dbname, pg_partman_bgw_role, 0);
    #endif
    
    elog(DEBUG1, "After run_maint initialize connection for db %s", dbname);

    initStringInfo(&buf);

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    SPI_connect();
    PushActiveSnapshot(GetTransactionSnapshot());
    pgstat_report_appname("pg_partman dynamic background worker");

    // First determine if pg_partman is even installed in this database
    appendStringInfo(&buf, "SELECT extname FROM pg_catalog.pg_extension WHERE extname = 'pg_partman'");
    pgstat_report_activity(STATE_RUNNING, buf.data);
    elog(DEBUG1, "Checking if pg_partman extension is installed in database: %s" , dbname);
    ret = SPI_execute(buf.data, true, 1);
    if (ret != SPI_OK_SELECT) {
        elog(FATAL, "Cannot determine if pg_partman is installed in database %s: error code %d", dbname, ret);
    }
    if (SPI_processed <= 0) {
        elog(DEBUG1, "pg_partman not installed in database %s. Nothing to do so dynamic worker exiting gracefully.", dbname);
        // Nothing left to do. Return end the run of BGW function.
        SPI_finish();
        PopActiveSnapshot();
        CommitTransactionCommand();
        pgstat_report_activity(STATE_IDLE, NULL);

        pfree(rawstring);
        list_free(elemlist);

        return;
    }

    // If so then actually log that it's started for that database. 
    elog(LOG, "%s dynamic background worker initialized with role %s on database %s"
            , MyBgworkerEntry->bgw_name
            , pg_partman_bgw_role
            , dbname);

    resetStringInfo(&buf);
    appendStringInfo(&buf, "SELECT n.nspname FROM pg_catalog.pg_extension e JOIN pg_catalog.pg_namespace n ON e.extnamespace = n.oid WHERE extname = 'pg_partman'");
    pgstat_report_activity(STATE_RUNNING, buf.data);
    ret = SPI_execute(buf.data, true, 1);

    if (ret != SPI_OK_SELECT) {
        elog(FATAL, "Cannot determine which schema pg_partman has been installed to: error code %d", ret);
    }

    if (SPI_processed > 0) {
        bool isnull;

        partman_schema = DatumGetCString(SPI_getbinval(SPI_tuptable->vals[0]
                , SPI_tuptable->tupdesc
                , 1
                , &isnull));

        if (isnull)
            elog(FATAL, "Query to determine pg_partman schema returned NULL.");

    } else {
        elog(FATAL, "Query to determine pg_partman schema returned zero rows.");
    }

    resetStringInfo(&buf);
    if (strcmp(pg_partman_bgw_analyze, "on") == 0) {
        analyze = "true";
    } else {
        analyze = "false";
    }
    if (strcmp(pg_partman_bgw_jobmon, "on") == 0) {
        jobmon = "true";
    } else {
        jobmon = "false";
    }
    appendStringInfo(&buf, "SELECT \"%s\".run_maintenance(p_analyze := %s, p_jobmon := %s)", partman_schema, analyze, jobmon);

    pgstat_report_activity(STATE_RUNNING, buf.data);

    ret = SPI_execute(buf.data, false, 0);

    if (ret != SPI_OK_SELECT)
        elog(FATAL, "Cannot call pg_partman run_maintenance() function: error code %d", ret);

    elog(LOG, "%s: %s called by role %s on database %s"
            , MyBgworkerEntry->bgw_name
            , buf.data
            , pg_partman_bgw_role
            , dbname);

    SPI_finish();
    PopActiveSnapshot();
    CommitTransactionCommand();
    pgstat_report_activity(STATE_IDLE, NULL);
    elog(DEBUG1, "pg_partman dynamic BGW shutting down gracefully for database %s.", dbname);

    pfree(rawstring);
    list_free(elemlist);

    return;
}

