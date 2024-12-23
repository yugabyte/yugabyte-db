/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/pg_helio_api.c
 *
 * Initialization of the shared library for the Helio API.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <utils/guc.h>

#include "bson_init.h"
#include "utils/feature_counter.h"
#include "infrastructure/helio_external_configs.h"
#include "helio_api_init.h"

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);


bool SkipDocumentDBLoad = false;


/*
 * _PG_init gets called when the extension is loaded.
 */
void
_PG_init(void)
{
	if (SkipDocumentDBLoad)
	{
		return;
	}

	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"pg_documentdb can only be loaded via shared_preload_libraries"),
						errdetail_log(
							"Add pg_documentdb to shared_preload_libraries configuration "
							"variable in postgresql.conf. ")));
	}

	InstallBsonMemVTables();
	InitApiConfigurations("documentdb");
	InitializeExtensionExternalConfigs("documentdb");
	InitializeSharedMemoryHooks();
	MarkGUCPrefixReserved("documentdb");

	InstallHelioApiPostgresHooks();

	ereport(LOG, (errmsg("Initialized pg_documentdb extension")));
}


/*
 * _PG_fini is called before the extension is reloaded.
 */
void
_PG_fini(void)
{
	if (SkipDocumentDBLoad)
	{
		return;
	}

	UninstallHelioApiPostgresHooks();
}
