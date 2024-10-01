/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/helio_distributed_init.c
 *
 * Initialization of the shared library initialization for distribution for Hleio API.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>

#include "helio_distributed_init.h"


/* --------------------------------------------------------- */
/* GUCs and default values */
/* --------------------------------------------------------- */

#define DEFAULT_ENABLE_METADATA_REFERENCE_SYNC true
bool EnableMetadataReferenceTableSync = DEFAULT_ENABLE_METADATA_REFERENCE_SYNC;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/*
 * Initializes core configurations pertaining to helio core.
 */
void
InitHelioDistributedConfigurations(void)
{
	DefineCustomBoolVariable(
		"helio_api_distributed.enable_metadata_reference_table_sync",
		gettext_noop(
			"Determines whether or not to enable metadata reference table syncs."),
		NULL, &EnableMetadataReferenceTableSync, DEFAULT_ENABLE_METADATA_REFERENCE_SYNC,
		PGC_USERSET, 0, NULL, NULL, NULL);
}
