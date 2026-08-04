/*-------------------------------------------------------------------------
 *
 * resowner_private.h
 *	  YB-only declarations for ResourceOwner per-resource helpers.
 *
 * YB_TODO_PG19MERGE: Upstream PG19 (commit 06a0f4d52be3a52a74725dd29c66cd486256a209) removed
 * this file; The YB ybinheritsref helpers below are not yet ported
 * to the new API (see the YB_TODO_PG19MERGE block in resowner.c). Once the
 * implementation is migrated onto a dedicated ResourceOwnerDesc, these
 * declarations and this file can be removed.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/resowner_private.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef RESOWNER_PRIVATE_H
#define RESOWNER_PRIVATE_H

#include "utils/resowner.h"

/* YB includes */
#include "utils/yb_inheritscache.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

/* YB */
extern void ResourceOwnerEnlargeYbPgInheritsRefs(ResourceOwner owner);
extern void ResourceOwnerRememberYbPgInheritsRef(ResourceOwner owner,
												 YbPgInheritsCacheEntry entry);
extern void ResourceOwnerForgetYbPgInheritsRef(ResourceOwner owner,
											   YbPgInheritsCacheEntry entry);

#endif							/* RESOWNER_PRIVATE_H */
