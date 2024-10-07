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
	F_RANDOM
};

const int yb_funcs_safe_for_pushdown_count =
	sizeof(yb_funcs_safe_for_pushdown) /
	sizeof(yb_funcs_safe_for_pushdown[0]);

const uint32 yb_funcs_unsafe_for_pushdown[] = {
	/* to_tsany.c */
	F_TO_TSVECTOR_TEXT,
	F_TO_TSVECTOR_REGCONFIG_TEXT,
	F_TO_TSVECTOR_JSONB,
	F_TO_TSVECTOR_REGCONFIG_JSONB,
	F_JSONB_TO_TSVECTOR_JSONB_JSONB,
	F_JSONB_TO_TSVECTOR_REGCONFIG_JSONB_JSONB,
	F_JSON_TO_TSVECTOR_JSON_JSONB,
	F_JSON_TO_TSVECTOR_REGCONFIG_JSON_JSONB,
	F_TO_TSVECTOR_JSON,
	F_TO_TSVECTOR_REGCONFIG_JSON,
	F_TO_TSQUERY_TEXT,
	F_TO_TSQUERY_REGCONFIG_TEXT,
	F_PLAINTO_TSQUERY_TEXT,
	F_PLAINTO_TSQUERY_REGCONFIG_TEXT,
	F_WEBSEARCH_TO_TSQUERY_TEXT,
	F_WEBSEARCH_TO_TSQUERY_REGCONFIG_TEXT,
	F_PHRASETO_TSQUERY_TEXT,
	F_PHRASETO_TSQUERY_REGCONFIG_TEXT,

	/* wparser.c */
	F_TS_HEADLINE_TEXT_TSQUERY,
	F_TS_HEADLINE_REGCONFIG_TEXT_TSQUERY,
	F_TS_HEADLINE_TEXT_TSQUERY_TEXT,
	F_TS_HEADLINE_REGCONFIG_TEXT_TSQUERY_TEXT,
	F_TS_HEADLINE_JSONB_TSQUERY,
	F_TS_HEADLINE_REGCONFIG_JSONB_TSQUERY,
	F_TS_HEADLINE_JSONB_TSQUERY_TEXT,
	F_TS_HEADLINE_REGCONFIG_JSONB_TSQUERY_TEXT,
	F_TS_HEADLINE_JSON_TSQUERY,
	F_TS_HEADLINE_REGCONFIG_JSON_TSQUERY,
	F_TS_HEADLINE_JSON_TSQUERY_TEXT,
	F_TS_HEADLINE_REGCONFIG_JSON_TSQUERY_TEXT,
	F_GET_CURRENT_TS_CONFIG,

	/* These call to_tsvector / to_tsquery */
	F_TS_MATCH_TT,
	F_TS_MATCH_TQ
};

const int yb_funcs_unsafe_for_pushdown_count =
	sizeof(yb_funcs_unsafe_for_pushdown) /
	sizeof(yb_funcs_unsafe_for_pushdown[0]);
