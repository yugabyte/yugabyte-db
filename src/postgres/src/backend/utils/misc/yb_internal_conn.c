/*-------------------------------------------------------------------------
 *
 * yb_internal_conn.c
 *    Registry of tserver-owned YSQL "internal" connection kinds. See
 *    yb_internal_conn.h for the framework rationale and for how to add
 *    a new kind.
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
 * src/backend/utils/misc/yb_internal_conn.c
 * ----------
 */

#include "postgres.h"

#include <string.h>

#include "yb_internal_conn.h"

const YbInternalConnKindDescriptor
			YbInternalConnKindDescriptors[YB_INTERNAL_CONN_KIND_MAX] = {
	[YB_INTERNAL_CONN_KIND_NONE] = {
		.wire_name = NULL,
		.backend_type = B_BACKEND,
		.backend_desc = NULL,	/* miscinit.c handles B_BACKEND directly */
		.use_minimal_preload = false,
		.preload_lists_in_minimal_mode = false,
	},
	[YB_INTERNAL_CONN_KIND_RELCACHE_INIT] = {
		.wire_name = "relcache_init",
		.backend_type = YB_RELCACHE_INIT_BACKEND,
		.backend_desc = "yb relcache init backend",
		.use_minimal_preload = true,
		.preload_lists_in_minimal_mode = true,
	},
	[YB_INTERNAL_CONN_KIND_AUTO_ANALYZE] = {
		.wire_name = "auto_analyze",
		.backend_type = YB_AUTO_ANALYZE_BACKEND,
		.backend_desc = "yb auto analyze backend",
		/*
		 * Auto-analyze is a long-lived backend running real ANALYZE work, so
		 * it preloads normally rather than minimally.
		 */
		.use_minimal_preload = false,
		.preload_lists_in_minimal_mode = false,
	},
	[YB_INTERNAL_CONN_KIND_GLOBAL_VIEW] = {
		.wire_name = "global_view",
		.backend_type = YB_GLOBAL_VIEW_BACKEND,
		.backend_desc = "yb global view backend",
		.use_minimal_preload = false,
		.preload_lists_in_minimal_mode = false,
	},
};

YbInternalConnKind
YbLookupInternalConnKindByName(const char *name)
{
	if (name == NULL)
		return YB_INTERNAL_CONN_KIND_NONE;

	for (int i = 1; i < YB_INTERNAL_CONN_KIND_MAX; ++i)
	{
		const char *wire_name = YbInternalConnKindDescriptors[i].wire_name;

		if (wire_name != NULL && strcmp(wire_name, name) == 0)
			return (YbInternalConnKind) i;
	}
	return YB_INTERNAL_CONN_KIND_NONE;
}

YbInternalConnKind
YbLookupInternalConnKindByBackendType(BackendType type)
{
	for (int i = 1; i < YB_INTERNAL_CONN_KIND_MAX; ++i)
	{
		if (YbInternalConnKindDescriptors[i].backend_type == type)
			return (YbInternalConnKind) i;
	}
	return YB_INTERNAL_CONN_KIND_NONE;
}

bool
YbIsInternalConnBackendType(BackendType backendType)
{
	return YbLookupInternalConnKindByBackendType(backendType) !=
		YB_INTERNAL_CONN_KIND_NONE;
}
