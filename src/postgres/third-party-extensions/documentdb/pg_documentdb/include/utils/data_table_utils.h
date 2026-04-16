/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/data_table_utils.h
 *
 * Utilities for mongo feature usage tracking.
 *
 *-------------------------------------------------------------------------
 */

 #ifndef DATA_TABLE_UTILS_H
 #define DATA_TABLE_UTILS_H

ArrayType * GetCollectionIds(void);
ArrayType * GetCollectionIdsStartingFrom(uint64 startCollectionId);
void AlterCreationTime(void);


#endif /* DATA_TABLE_UTILS_H */
