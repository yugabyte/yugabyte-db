/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/guc_utils.h
 *
 * Utilities for GUCs (Grand Unified Configuration)
 *
 *-------------------------------------------------------------------------
 */

#ifndef GUC_UTILS_H
#define GUC_UTILS_H

#include "utils/guc.h"

void SetGUCLocally(const char *name, const char *value);


/*
 * Helper function to do an early rollback of a local GUC set with SetGUCLocally function.
 * Given the GUC level, it rollbacks the GUC change to its previous value in the current transcation.
 */
static inline void
RollbackGUCChange(int savedGUCLevel)
{
	bool commitGUCChanges = false;
	AtEOXact_GUC(commitGUCChanges, savedGUCLevel);
}


#endif
