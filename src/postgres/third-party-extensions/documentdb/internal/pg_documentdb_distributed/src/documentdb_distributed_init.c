/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/documentdb_distributed_init.c
 *
 * Initialization of the shared library initialization for distribution for Hleio API.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>

#include "documentdb_distributed_init.h"


/* --------------------------------------------------------- */
/* GUCs and default values */
/* --------------------------------------------------------- */

#define DEFAULT_ENABLE_METADATA_REFERENCE_SYNC true
bool EnableMetadataReferenceTableSync = DEFAULT_ENABLE_METADATA_REFERENCE_SYNC;

#define DEFAULT_ENABLE_SHARD_REBALANCER false
bool EnableShardRebalancer = DEFAULT_ENABLE_SHARD_REBALANCER;

#define DEFAULT_CLUSTER_ADMIN_ROLE ""
char *ClusterAdminRole = DEFAULT_CLUSTER_ADMIN_ROLE;

#define DEFAULT_ENABLE_MOVE_COLLECTION true
bool EnableMoveCollection = DEFAULT_ENABLE_MOVE_COLLECTION;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/*
 * Initializes core configurations pertaining to documentdb distributed.
 */
void
InitDocumentDBDistributedConfigurations(const char *prefix)
{
	DefineCustomBoolVariable(
		psprintf("%s.enable_metadata_reference_table_sync", prefix),
		gettext_noop(
			"Determines whether or not to enable metadata reference table syncs."),
		NULL, &EnableMetadataReferenceTableSync, DEFAULT_ENABLE_METADATA_REFERENCE_SYNC,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_shard_rebalancer_apis", prefix),
		gettext_noop(
			"Determines whether or not to enable shard rebalancer APIs."),
		NULL, &EnableShardRebalancer, DEFAULT_ENABLE_SHARD_REBALANCER,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_move_collection", prefix),
		gettext_noop(
			"Determines whether or not to enable move collection."),
		NULL, &EnableMoveCollection, DEFAULT_ENABLE_MOVE_COLLECTION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomStringVariable(
		psprintf("%s.clusterAdminRole", prefix),
		gettext_noop(
			"The cluster admin role."),
		NULL, &ClusterAdminRole, DEFAULT_CLUSTER_ADMIN_ROLE,
		PGC_USERSET, 0, NULL, NULL, NULL);
}
