/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/index.h
 *
 * Accessors around mongo_catalog.collection_indexes.
 *
 *-------------------------------------------------------------------------
 */
#ifndef METADATA_GUC_H
#define METADATA_GUC_H

#define NEXT_COLLECTION_ID_UNSET 0
extern int NextCollectionId;

#define NEXT_COLLECTION_INDEX_ID_UNSET 0
extern int NextCollectionIndexId;

#endif
