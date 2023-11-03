/*-------------------------------------------------------------------------
 *
 * yb_exceptions_for_func_pushdown.c
 *    List of non-immutable functions that do not perform any accesses to
 *    the database.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *    src/backend/utils/misc/yb_exceptions_for_func_pushdown.c
 *
 *-------------------------------------------------------------------------
 */

#include "c.h"
#include "utils/fmgroids.h"

const uint32 yb_funcs_safe_for_pushdown[] = {
	F_DRANDOM
};

const int yb_funcs_safe_for_pushdown_count =
	sizeof(yb_funcs_safe_for_pushdown) /
	sizeof(yb_funcs_safe_for_pushdown[0]);

const uint32 yb_funcs_unsafe_for_pushdown[] = {
	/* to_tsany.c */
	F_TO_TSVECTOR,
	F_TO_TSVECTOR_BYID,
	F_JSONB_STRING_TO_TSVECTOR,
	F_JSONB_STRING_TO_TSVECTOR_BYID,
	F_JSONB_TO_TSVECTOR,
	F_JSONB_TO_TSVECTOR_BYID,
	F_JSON_TO_TSVECTOR,
	F_JSON_TO_TSVECTOR_BYID,
	F_JSON_STRING_TO_TSVECTOR,
	F_JSON_STRING_TO_TSVECTOR_BYID,
	F_TO_TSQUERY,
	F_TO_TSQUERY_BYID,
	F_PLAINTO_TSQUERY,
	F_PLAINTO_TSQUERY_BYID,
	F_WEBSEARCH_TO_TSQUERY,
	F_WEBSEARCH_TO_TSQUERY_BYID,
	F_PHRASETO_TSQUERY,
	F_PHRASETO_TSQUERY_BYID,

	/* wparser.c */
	F_TS_HEADLINE,
	F_TS_HEADLINE_BYID,
	F_TS_HEADLINE_OPT,
	F_TS_HEADLINE_BYID_OPT,
	F_TS_HEADLINE_JSONB,
	F_TS_HEADLINE_JSONB_BYID,
	F_TS_HEADLINE_JSONB_OPT,
	F_TS_HEADLINE_JSONB_BYID_OPT,
	F_TS_HEADLINE_JSON,
	F_TS_HEADLINE_JSON_BYID,
	F_TS_HEADLINE_JSON_OPT,
	F_TS_HEADLINE_JSON_BYID_OPT,
	F_GET_CURRENT_TS_CONFIG,

	/* These call to_tsvector / to_tsquery */
	F_TS_MATCH_TT,
	F_TS_MATCH_TQ
};

const int yb_funcs_unsafe_for_pushdown_count =
	sizeof(yb_funcs_unsafe_for_pushdown) /
	sizeof(yb_funcs_unsafe_for_pushdown[0]);
