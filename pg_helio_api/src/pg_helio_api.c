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
#include "infrastructure/bson_external_configs.h"
#include "documentdb_api_init.h"
#include "metadata/metadata_cache.h"

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);


extern bool SkipDocumentDBLoad;
bool SkipHelioApiLoad = false;
extern char *ApiExtensionName;


/*
 * _PG_init gets called when the extension is loaded.
 */
void
_PG_init(void)
{
	if (SkipHelioApiLoad)
	{
		return;
	}

	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"pg_helio_api can only be loaded via shared_preload_libraries"),
						errdetail_log(
							"Add pg_helio_api to shared_preload_libraries configuration "
							"variable in postgresql.conf. ")));
	}

	SkipDocumentDBLoad = true;

	ApiDataSchemaName = "helio_data";
	ApiAdminRole = "helio_admin_role";
	ApiReadOnlyRole = "helio_readonly_role";
	ApiSchemaName = "helio_api";
	ApiSchemaNameV2 = "helio_api";
	ApiInternalSchemaName = "helio_api_internal";
	ApiInternalSchemaNameV2 = "helio_api_internal";
	ApiCatalogSchemaName = "helio_api_catalog";
	ApiCatalogSchemaNameV2 = "helio_api_catalog";
	ExtensionObjectPrefix = "helio";
	FullBsonTypeName = "helio_core.bson";
	ApiExtensionName = "pg_helio_api";

	/* Schema functions migrated from a public API to an internal API schema
	 * (e.g. from helio_api -> helio_api_internal)
	 * TODO: These should be transition and removed in subsequent releases.
	 */
	ApiToApiInternalSchemaName = "helio_api_internal";
	ApiCatalogToApiInternalSchemaName = "helio_api_internal";
	DocumentDBApiInternalSchemaName = "helio_api_internal";
	ApiCatalogToCoreSchemaName = "helio_core";

	InstallBsonMemVTables();
	InitApiConfigurations("helio_api", "helio_api");
	InitializeExtensionExternalConfigs("helio_api");
	InitializeSharedMemoryHooks();
	MarkGUCPrefixReserved("helio_api");
	InitializeDocumentDBBackgroundWorker("pg_helio_api", "helio_api");

	InstallDocumentDBApiPostgresHooks();

	ereport(LOG, (errmsg("Initialized pg_helio_api extension")));
}


/*
 * _PG_fini is called before the extension is reloaded.
 */
void
_PG_fini(void)
{
	if (SkipHelioApiLoad)
	{
		return;
	}

	UninstallDocumentDBApiPostgresHooks();
}
