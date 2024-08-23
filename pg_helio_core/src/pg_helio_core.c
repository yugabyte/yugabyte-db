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

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);

bool SkipHelioCoreLoad = false;

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

	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"pg_helio_core can only be loaded via shared_preload_libraries"),
						errhint(
							"Add pg_helio_core to shared_preload_libraries configuration "
							"variable in postgresql.conf. ")));
	}

	InstallBsonMemVTables();

	InitHelioCoreConfigurations();

	MarkGUCPrefixReserved("helio_core");
	ereport(LOG, (errmsg("Initialized pg_helio_core extension")));
}


/*
 * _PG_fini is called before the extension is reloaded.
 */
void
_PG_fini(void)
{ }
