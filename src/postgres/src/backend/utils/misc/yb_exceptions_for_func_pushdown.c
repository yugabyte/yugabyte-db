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
#include "catalog/pg_operator_d.h"
#include "utils/fmgroids.h"

const uint32 yb_funcs_safe_for_pushdown[] = {
	F_DRANDOM
};

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

#define COMPARISON_AND_ARITHMETIC_OPS(prefix) \
	COMPARISON_OPS(prefix), \
	ARITHMETIC_OPS(prefix)

#define COMPARISON_OPS(prefix) \
	EQUALITY_OPS(prefix), \
	INEQUALITY_OPS(prefix)

#define EQUALITY_OPS(prefix) \
	F_##prefix##EQ, \
	F_##prefix##NE

#define INEQUALITY_OPS(prefix) \
	F_##prefix##LT, \
	F_##prefix##LE, \
	F_##prefix##GE, \
	F_##prefix##GT

#define ARITHMETIC_OPS(prefix) \
	F_##prefix##MUL, \
	F_##prefix##DIV, \
	F_##prefix##PL, \
	F_##prefix##MI

#define UNARY_MINUS_AND_ABS(prefix) \
	F_##prefix##UM, \
	F_##prefix##ABS

const uint32 yb_funcs_safe_for_mixed_mode_pushdown[] = {
	COMPARISON_AND_ARITHMETIC_OPS(INT2),
	UNARY_MINUS_AND_ABS(INT2),
	COMPARISON_AND_ARITHMETIC_OPS(INT24),
	COMPARISON_AND_ARITHMETIC_OPS(INT28),
	COMPARISON_AND_ARITHMETIC_OPS(INT4),
	UNARY_MINUS_AND_ABS(INT4),
	COMPARISON_AND_ARITHMETIC_OPS(INT42),
	COMPARISON_AND_ARITHMETIC_OPS(INT48),
	COMPARISON_AND_ARITHMETIC_OPS(INT8),
	UNARY_MINUS_AND_ABS(INT8),
	COMPARISON_AND_ARITHMETIC_OPS(INT82),
	COMPARISON_AND_ARITHMETIC_OPS(INT84),

	COMPARISON_AND_ARITHMETIC_OPS(FLOAT4),
	UNARY_MINUS_AND_ABS(FLOAT4),
	COMPARISON_AND_ARITHMETIC_OPS(FLOAT48),
	COMPARISON_AND_ARITHMETIC_OPS(FLOAT8),
	UNARY_MINUS_AND_ABS(FLOAT8),
	COMPARISON_AND_ARITHMETIC_OPS(FLOAT84),

	F_I4TOI2,
	F_INT82,
	F_FTOI2,
	F_DTOI2,
	F_NUMERIC_INT2,
	F_I2TOI4,
	F_INT84,
	F_FTOI4,
	F_DTOI4,
	F_NUMERIC_INT4,
	F_INT28,
	F_INT48,
	F_FTOI8,
	F_DTOI8,
	F_NUMERIC_INT8,
	F_I2TOF,
	F_I4TOF,
	F_I8TOF,
	F_DTOF,
	F_NUMERIC_FLOAT4,
	F_I2TOD,
	F_I4TOD,
	F_I8TOD,
	F_FTOD,
	F_NUMERIC_FLOAT8,
	F_INT2_NUMERIC,
	F_INT4_NUMERIC,
	F_INT8_NUMERIC,
	F_FLOAT4_NUMERIC,
	F_FLOAT8_NUMERIC,

	F_TEXT_CHAR,
	F_CHAR_TEXT,
	F_RTRIM1,
	F_BPCHAR,

	COMPARISON_OPS(BPCHAR),

	F_INT4_BOOL,
	F_BOOL_INT4,

	COMPARISON_OPS(BOOL),

	COMPARISON_OPS(NUMERIC_),
	F_NUMERIC_ADD,
	F_NUMERIC_SUB,
	F_NUMERIC_MUL,
	F_NUMERIC_DIV,
	F_NUMERIC_UMINUS,
	F_NUMERIC_ABS,
	1705 /* numeric_abs */,

	EQUALITY_OPS(TEXT),
	INEQUALITY_OPS(TEXT_),

	1398 /* int2abs */,
	1397 /* int4abs */,
	1396 /* int8abs */,
	1395 /* float8abs */,
	1394 /* float4abs */,

	COMPARISON_OPS(UUID_),

	COMPARISON_OPS(TIME_),
	COMPARISON_OPS(TIMETZ_),
	1152, /* timestamptz_eq */
	1153, /* timestamptz_ne */
	1154, /* timestamptz_lt */
	1155, /* timestamptz_le */
	1156, /* timestamptz_ge */
	1157, /* timestamptz_gt */
	2052, /* timestamp_eq */
	2053, /* timestamp_ne */
	2054, /* timestamp_lt */
	2055, /* timestamp_le */
	2056, /* timestamp_ge */
	2057, /* timestamp_gt */
	COMPARISON_OPS(DATE_),
	COMPARISON_OPS(INTERVAL_),

	F_INT2MOD,
	F_INT4MOD,
	F_INT8MOD,
	1728, // F_NUMERIC_MOD,
	940, // F_MOD_INT2_INT2
	941, // F_MOD_INT4_INT4
	947, // F_MOD_INT8_INT8
	1729, // F_NUMERIC_MOD,

	F_TEXTLIKE,
	F_TEXTNLIKE,
	1569, // F_LIKE_TEXT_TEXT
	1570, // F_NOTLIKE_TEXT_TEXT
	1631, // F_BPCHARLIKE
	1632, // F_BPCHARNLIKE
	F_TEXTICLIKE,
	F_TEXTICNLIKE,
	1660, // F_BPCHARICLIKE
	1661, // F_BPCHARICNLIKE
	F_TEXTREGEXEQ,
	F_TEXTREGEXNE,

	F_ASCII,

	F_TEXTREGEXSUBSTR,
	2074, // F_SUBSTRING_TEXT_TEXT_TEXT
	937, // F_SUBSTRING_TEXT_INT4
	936, // F_SUBSTRING_TEXT_INT4_INT4
};

#define DEFINE_ARRAY_SIZE(array) const int array##_count = lengthof(array)

DEFINE_ARRAY_SIZE(yb_funcs_safe_for_pushdown);
DEFINE_ARRAY_SIZE(yb_funcs_unsafe_for_pushdown);
DEFINE_ARRAY_SIZE(yb_funcs_safe_for_mixed_mode_pushdown);
