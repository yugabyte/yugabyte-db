
/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/drop_indexes.h
 *
 * Internal implementation of mongo_api_v1.drop_indexes.
 *
 *-------------------------------------------------------------------------
 */
#ifndef DROP_INDEXES_H
#define DROP_INDEXES_H

#include "metadata/index.h"

void DropPostgresIndex(uint64 collectionId, int indexId, bool unique,
					   bool concurrently, bool missingOk);
void DropPostgresIndexWithSuffix(uint64 collectionId,
								 IndexDetails *index,
								 bool concurrently,
								 bool missingOk,
								 const char *suffix);

#endif
