/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/insert.h
 *
 * Functions for inserting documents.
 *
 *-------------------------------------------------------------------------
 */
#ifndef COMMANDS_INSERT_H
#define COMMANDS_INSERT_H

#include <io/helio_bson_core.h>
#include "commands/commands_common.h"

MongoCollection * CreateCollectionForInsert(Datum databaseNameDatum,
											Datum collectionNameDatum);
bool InsertDocument(uint64 collectionId, const char *shardTableName, int64 shardKeyValue,
					pgbson *objectId, pgbson *document);

bool InsertDocumentToTempCollection(MongoCollection *collection, int64 shardKeyValue,
									pgbson *document);
bool TryInsertOne(MongoCollection *collection, pgbson *document, int64 shardKeyHash, bool
				  sameSourceAndTarget, WriteError *writeError);
#endif
