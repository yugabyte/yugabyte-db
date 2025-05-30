/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/documentdb_init.c
 *
 * Initialization of the shared library initialization for documentdb.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include <limits.h>
#include <access/xact.h>
#include <postmaster/bgworker.h>
#include <storage/ipc.h>

#include "documentdb_api_init.h"
#include "metadata/metadata_guc.h"
#include "planner/documentdb_planner.h"
#include "customscan/custom_scan_registrations.h"
#include "commands/connection_management.h"
#include "utils/feature_counter.h"
#include "utils/version_utils.h"
#include "vector/vector_spec.h"
#include "commands/commands_common.h"
#include "configs/config_initialization.h"

/* --------------------------------------------------------- */
/* Data Types & Enum values */
/* --------------------------------------------------------- */

extern bool EnableBackgroundWorker;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

/* In single node mode, we always inline write operations */
bool DefaultInlineWriteOperations = true;
bool ShouldUpgradeDataTables = true;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */


/* callbacks for transaction management */
static void DocumentDBTransactionCallback(XactEvent event, void *arg);
static void DocumentDBSubTransactionCallback(SubXactEvent event, SubTransactionId mySubid,
											 SubTransactionId parentSubid, void *arg);
static void DocumentDBSharedMemoryInit(void);

/* --------------------------------------------------------- */
/* GUCs and default values */
/* --------------------------------------------------------- */


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


/*
 * Initializes core configurations pertaining to the bson type management.
 */
void
InitApiConfigurations(char *prefix, char *newGucPrefix)
{
	InitializeTestConfigurations(prefix, newGucPrefix);
	InitializeFeatureFlagConfigurations(prefix, newGucPrefix);
	InitializeBackgroundJobConfigurations(prefix, newGucPrefix);
	InitializeSystemConfigurations(prefix, newGucPrefix);
}


/*
 * Install custom hooks that Postgres exposes for DocumentDB API.
 */
void
InstallDocumentDBApiPostgresHooks(void)
{
	/* override planner to apply query transformations */
	ExtensionPreviousPlannerHook = planner_hook;
	planner_hook = DocumentDBApiPlanner;

	ExtensionPreviousIndexNameHook = explain_get_index_name_hook;
	explain_get_index_name_hook = ExtensionExplainGetIndexName;

	/* override planner paths hook for overriding indexed and non-indexed paths. */
	ExtensionPreviousSetRelPathlistHook = set_rel_pathlist_hook;
	set_rel_pathlist_hook = ExtensionRelPathlistHook;

	RegisterXactCallback(DocumentDBTransactionCallback, NULL);
	RegisterSubXactCallback(DocumentDBSubTransactionCallback, NULL);

	RegisterScanNodes();
	RegisterQueryScanNodes();
}


/* Initialized the background worker */
void
InitializeDocumentDBBackgroundWorker(char *libraryName, char *gucPrefix,
									 char *extensionObjectPrefix)
{
	/* Initialize GUCs */
	InitDocumentDBBackgroundWorkerGucs(gucPrefix);

	if (!EnableBackgroundWorker)
	{
		return;
	}

	BackgroundWorker worker;
	memset(&worker, 0, sizeof(worker));

	/* set up common data for the worker */
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = 10;
	worker.bgw_main_arg = Int32GetDatum(0);
	worker.bgw_notify_pid = 0;

	sprintf(worker.bgw_library_name, "%s", libraryName);
	sprintf(worker.bgw_function_name, "DocumentDBBackgroundWorkerMain");
	snprintf(worker.bgw_name, BGW_MAXLEN, "%s bg worker leader", extensionObjectPrefix);
	snprintf(worker.bgw_type, BGW_MAXLEN, "%s_bg_worker_leader", extensionObjectPrefix);

	RegisterBackgroundWorker(&worker);
}


/*
 * Uninstalls custom hooks that Postgres exposes for DocumentDB API.
 */
void
UninstallDocumentDBApiPostgresHooks(void)
{
	planner_hook = ExtensionPreviousPlannerHook;
	ExtensionPreviousPlannerHook = NULL;

	explain_get_index_name_hook = ExtensionPreviousIndexNameHook;
	ExtensionPreviousIndexNameHook = NULL;

	set_rel_pathlist_hook = ExtensionPreviousSetRelPathlistHook;
	ExtensionPreviousSetRelPathlistHook = NULL;

	UnregisterXactCallback(DocumentDBTransactionCallback, NULL);
	UnregisterSubXactCallback(DocumentDBSubTransactionCallback, NULL);
}


void
InitializeSharedMemoryHooks(void)
{
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = DocumentDBSharedMemoryInit;
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */


static void
DocumentDBSharedMemoryInit(void)
{
	SharedFeatureCounterShmemInit();
	InitializeVersionCache();

	if (prev_shmem_startup_hook != NULL)
	{
		prev_shmem_startup_hook();
	}
}


static void
DocumentDBTransactionCallback(XactEvent event, void *arg)
{
	switch (event)
	{
		case XACT_EVENT_ABORT:
		case XACT_EVENT_PARALLEL_ABORT:
		{
			ConnMgrTryCancelActiveConnection();
			break;
		}

		default:
		{
			break;
		}
	}
}


static void
DocumentDBSubTransactionCallback(SubXactEvent event, SubTransactionId mySubid,
								 SubTransactionId parentSubid, void *arg)
{
	switch (event)
	{
		case SUBXACT_EVENT_ABORT_SUB:
		{
			ConnMgrTryCancelActiveConnection();
			break;
		}

		default:
		{
			break;
		}
	}
}
