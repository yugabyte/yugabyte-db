/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/lock_tags.h
 *
 * Constants defined under PgmongoAdvisoryLockField4 are meant to be used for
 * "locktag_field4" field of "LOCKTAG" struct when acquiring an advisory lock
 * that is specific to pg_helio_api.
 *
 * In Postgres, "locktag_field4" is used to to discern between different kind of
 * advisory locks. 1 and 2 are used by Postgres and 4-12 are used by Citus; so
 * starting from 100 here is a good enough choice that wouldn't cause a conflict
 * with other known advisory locks that pg_helio_api should be compatible with.
 *
 * See AdvisoryLocktagClass at citus/src/include/distributed/resource_lock.h.
 *
 *-------------------------------------------------------------------------
 */
#ifndef LOCK_TAGS_H
#define LOCK_TAGS_H

#include <c.h>

typedef enum
{
	/* AcquireAdvisoryExclusiveLockForCreateIndexes */
	LT_FIELD4_EXCL_CREATE_INDEXES = 100,

	/* LockTagForInProgressIndexBuild */
	LT_FIELD4_IN_PROG_INDEX_BUILD,

	/* AcquireAdvisoryExclusiveSessionLockForCreateIndexBackground */
	LT_FIELD4_EXCL_CREATE_INDEX_BACKGROUND
} PgmongoAdvisoryLockField4;

#endif
