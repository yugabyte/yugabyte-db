/*-------------------------------------------------------------------------
 *
 * yb_internal_conn.h
 *    Registry of tserver-owned YSQL "internal" connection kinds.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/include/yb_internal_conn.h
 * ----------
 */

#ifndef YB_INTERNAL_CONN_H
#define YB_INTERNAL_CONN_H

#include "postgres.h"

#include "miscadmin.h"

/*
 * A YugabyteDB tserver opens internal libpq connections to its own postgres
 * for several purposes -- rebuilding the relcache init file, running
 * auto-analyze, draining the xCluster DDL queue, etc. Each kind has slightly
 * different desired behavior (e.g. only the relcache-init builder runs with
 * minimal catalog preload), shows up distinctly in pg_stat_activity, and
 * must be restricted to the yb-tserver-key authentication method.
 *
 * Rather than duplicating per-kind plumbing through libpq, the postmaster,
 * auth, and the pg_stat machinery, every kind is described by one entry in
 * YbInternalConnKindDescriptors. The startup parameter "yb_internal_conn_kind"
 * carries the kind's wire name; the registry maps that name to a BackendType
 * and to per-kind behavior bits.
 *
 * Adding a new kind:
 *   1. Append a value to enum YbInternalConnKind (before _MAX).
 *   2. Add the matching BackendType in src/include/miscadmin.h.
 *   3. Add a descriptor row in yb_internal_conn.c.
 *   4. Pass the new kind to CreateInternalPGConnBuilder at the call site.
 */

typedef enum YbInternalConnKind
{
	/*
	 * Sentinel value for "no yb_internal_conn_kind was set" -- i.e. the
	 * connection is a regular client backend (or some other non-YB backend
	 * type like a walsender).
	 */
	YB_INTERNAL_CONN_KIND_NONE = 0,
	YB_INTERNAL_CONN_KIND_RELCACHE_INIT,
	YB_INTERNAL_CONN_KIND_GLOBAL_VIEW,
	/* Future kinds (xCluster DDL queue, etc.) go above this line. */
	YB_INTERNAL_CONN_KIND_MAX
} YbInternalConnKind;

typedef struct YbInternalConnKindDescriptor
{
	/*
	 * Wire name passed in the libpq startup parameter "yb_internal_conn_kind".
	 * NULL for YB_INTERNAL_CONN_KIND_NONE (no name; the parameter is absent).
	 */
	const char *wire_name;

	/*
	 * BackendType assigned to MyBackendType when the kind matches. Each kind
	 * gets a distinct BackendType so it is individually visible in
	 * pg_stat_activity.backend_type.
	 */
	BackendType backend_type;

	/* Human-readable name used by pg_stat_activity.backend_type. */
	const char *backend_desc;

	/*
	 * If true, this kind runs with minimal catalog cache preload (i.e.
	 * YbUseMinimalCatalogCachesPreload returns true for this kind). Intended
	 * for short-lived backends with a narrow purpose, like the relcache-init
	 * builder.
	 */
	bool		use_minimal_preload;

	/*
	 * Effective only when use_minimal_preload is true. If true, this kind
	 * still preloads catcache LIST entries even though individual tuples are
	 * loaded in minimal mode. The relcache-init builder uses this so that
	 * list-keyed catcache lookups go through the populated list caches; other
	 * minimal-preload kinds leave lists to be built on demand. Has no effect
	 * when minimal preload is off, in which case lists are always preloaded.
	 */
	bool		preload_lists_in_minimal_mode;
} YbInternalConnKindDescriptor;

extern const YbInternalConnKindDescriptor
			YbInternalConnKindDescriptors[YB_INTERNAL_CONN_KIND_MAX];

/*
 * Look up a kind by the wire name received in the startup parameter. Returns
 * YB_INTERNAL_CONN_KIND_NONE for an unrecognized name; the caller should
 * report an error to the client.
 */
extern YbInternalConnKind YbLookupInternalConnKindByName(const char *name);

/*
 * Return the kind whose descriptor's backend_type matches the given type,
 * or YB_INTERNAL_CONN_KIND_NONE if no kind claims this type.
 */
extern YbInternalConnKind YbLookupInternalConnKindByBackendType(BackendType type);

/*
 * True iff backendType is the BackendType of some registered YB internal
 * connection kind. The pg_stat_activity equivalence-class checks and the
 * auth gate use this to treat every kind uniformly.
 */
extern bool YbIsInternalConnBackendType(BackendType backendType);

#endif							/* YB_INTERNAL_CONN_H */
