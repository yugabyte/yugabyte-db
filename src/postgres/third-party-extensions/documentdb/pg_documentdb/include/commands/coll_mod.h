/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/coll_mod.h
 *
 * Exports around the functionality of collmod
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_COLL_MOD_H
#define DOCUMENTDB_COLL_MOD_H

typedef enum IndexMetadataUpdateOperation
{
	INDEX_METADATA_UPDATE_OPERATION_UNKNOWN = 0,
	INDEX_METADATA_UPDATE_OPERATION_HIDDEN = 1,
	INDEX_METADATA_UPDATE_OPERATION_PREPARE_UNIQUE = 2,
	INDEX_METADATA_UPDATE_OPERATION_UNIQUE = 3,
} IndexMetadataUpdateOperation;

void UpdatePostgresIndexCore(uint64_t collectionId, int indexId,
							 IndexMetadataUpdateOperation operation, bool value, bool
							 ignoreMissingShards);

#endif
