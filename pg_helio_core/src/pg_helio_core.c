/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/pg_helio_core.c
 *
 * Initialization of the shared library.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <utils/guc.h>

#include "bson_init.h"
#include "utils/type_cache.h"

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);

bool SkipHelioCoreLoad = false;
extern bool SkipDocumentDBCoreLoad;

/*
 * _PG_init gets called when the extension is loaded.
 */
void
_PG_init(void)
{
	if (SkipHelioCoreLoad)
	{
		return;
	}

	SkipDocumentDBCoreLoad = true;
	CoreSchemaName = "helio_core";
	CoreSchemaNameV2 = "helio_core";


	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"pg_helio_core can only be loaded via shared_preload_libraries"
							"Add pg_helio_core to shared_preload_libraries configuration "
							"variable in postgresql.conf. ")));
	}

	InstallBsonMemVTables();

	InitDocumentDBCoreConfigurations("helio_core");

	MarkGUCPrefixReserved("helio_core");
	ereport(LOG, (errmsg("Initialized pg_helio_core extension")));
}


/*
 * _PG_fini is called before the extension is reloaded.
 */
void
_PG_fini(void)
{ }
