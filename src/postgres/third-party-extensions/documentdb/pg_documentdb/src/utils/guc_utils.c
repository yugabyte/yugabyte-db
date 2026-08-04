/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/guc_utils.c
 *
 * Utilities for GUCs (Grand Unified Configuration)
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>

#include "utils/guc_utils.h"


/*
 * SetGUCLocally sets given GUC to given value locally. That means, any
 * changes done by this function will be automatically rollbacked at the end
 * of the current transaction even if it commits.
 *
 * This is effectively same as doing the following within the current
 * transaction:
 *   SET LOCAL my_guc TO new_value;
 *
 * To early rollback the changes done by this function (i.e.: before the
 * transaction commits/rollbacks), example usage is as follows:
 *
 *   int savedGUCLevel = NewGUCNestLevel();
 *   SetGUCLocally("my_guc", "new_value");
 *
 *    // perform the stuff that requires above GUC change
 *
 *   RollbackGUCChange(savedGUCLevel);
 *
 */
void
SetGUCLocally(const char *name, const char *value)
{
	set_config_option(name, value,
					  (superuser() ? PGC_SUSET : PGC_USERSET), PGC_S_SESSION,
					  GUC_ACTION_LOCAL, true, 0, false);
}
