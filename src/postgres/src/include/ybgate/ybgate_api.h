/*-------------------------------------------------------------------------
 *
 * ybgate_api.h
 *	  YbGate interface functions.
 *	  YbGate allows to execute Postgres code from DocDB
 *
 * Copyright (c) Yugabyte, Inc.
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
 *	  src/include/ybgate/ybgate_api.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "ybgate/ybgate_status.h"

#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------
// Error Reporting
//-----------------------------------------------------------------------------
// !!! IMPORTANT: All public functions in the ybgate directory MUST return a
// YbgStatus and call PG_SETUP_ERROR_REPORTING first, before any execution.
//
// This will ensure that any elog or ereport called internally by a YSQL/PG
// function will be caught/handled (and not crash the tserver process).
//-----------------------------------------------------------------------------

#ifdef __cplusplus
typedef void *YbgMemoryContext;
#else
typedef MemoryContext YbgMemoryContext;
#endif

//-----------------------------------------------------------------------------
// Memory Context
//-----------------------------------------------------------------------------

YbgMemoryContext YbgGetCurrentMemoryContext();

YbgMemoryContext YbgSetCurrentMemoryContext(YbgMemoryContext memctx);

YbgStatus YbgCreateMemoryContext(YbgMemoryContext parent,
								 const char *name,
								 YbgMemoryContext *memctx);

YbgStatus YbgPrepareMemoryContext();

YbgStatus YbgResetMemoryContext();

YbgStatus YbgDeleteMemoryContext();

YbgStatus YbgInit();

//-----------------------------------------------------------------------------
// Types
//-----------------------------------------------------------------------------

struct PgAttributeRow
{
	uint32_t attrelid;
	char	 attname[64];
	uint32_t atttypid;
	int32_t	 attstattarget;
	int16_t	 attlen;
	int16_t	 attnum;
	int32_t	 attndims;
	int32_t	 attcacheoff;
	int32_t	 atttypmod;
	bool	 attbyval;
	char	 attstorage;
	char	 attalign;
	bool	 attnotnull;
	bool	 atthasdef;
	bool	 atthasmissing;
	char	 attidentity;
	bool	 attisdropped;
	bool	 attislocal;
	int32_t	 attinhcount;
	uint32_t attcollation;
};

#ifndef __cplusplus
typedef struct PgAttributeRow PgAttributeRow;
#endif

struct YbgTypeDesc {
	int32_t type_id; /* type identifier */
	int32_t type_mod; /* type modifier */
};

#ifndef __cplusplus
typedef struct YbgTypeDesc YbgTypeDesc;
#endif

/*
 * Get the type entity table for the primtive YSQL/PG types.
 * Used for for converting between DocDB values and YSQL datums.
 */
YbgStatus YbgGetTypeTable(const YBCPgTypeEntity **type_table, int *count);

/*
 * For non-primitive types (the ones without a corresponding YBCPgTypeEntity),
 * get the corresponding primitive type's oid.
 */
YbgStatus YbgGetPrimitiveTypeOid(uint32_t type_oid, char typtype,
								 uint32_t typbasetype,
								 uint32_t *primitive_type_oid);

//-----------------------------------------------------------------------------
// Expression Evaluation
//-----------------------------------------------------------------------------

#ifdef __cplusplus
typedef void* YbgExprContext;
typedef void* YbgPreparedExpr;
#else
typedef struct YbgExprContextData* YbgExprContext;
typedef struct Expr* YbgPreparedExpr;
#endif

/*
 * Create an expression context to evaluate expressions against.
 */
YbgStatus YbgExprContextCreate(int32_t min_attno, int32_t max_attno, YbgExprContext *expr_ctx);

YbgStatus YbgExprContextReset(YbgExprContext expr_ctx);

/*
 * Add a column value from the table row.
 * Used by expression evaluation to resolve scan variables.
 */
YbgStatus YbgExprContextAddColValue(YbgExprContext expr_ctx, int32_t attno, uint64_t datum, bool is_null);

YbgStatus YbgPrepareExpr(char* expr_cstring, YbgPreparedExpr *expr);

YbgStatus YbgExprType(const YbgPreparedExpr expr, int32_t *typid);

YbgStatus YbgExprTypmod(const YbgPreparedExpr expr, int32_t *typmod);

YbgStatus YbgExprCollation(const YbgPreparedExpr expr, int32_t *collid);

/*
 * Evaluate an expression, using the expression context to resolve scan variables.
 * Will filling in datum and is_null with the result.
 */
YbgStatus YbgEvalExpr(YbgPreparedExpr expr, YbgExprContext expr_ctx, uint64_t *datum, bool *is_null);

/*
 * Given a 'datum' of array type, split datum into individual elements of type 'type' and store
 * the result in 'result_datum_array', with number of elements in 'nelems'. This will error out
 * if 'type' doesn't match the type of the individual elements in 'datum'. Memory for
 * 'result_datum_array' will be allocated in this function itself, pre-allocation is not needed.
 */
YbgStatus YbgSplitArrayDatum(uint64_t datum, int type, uint64_t **result_datum_array, int *nelems);

//-----------------------------------------------------------------------------
// Relation sampling
//-----------------------------------------------------------------------------

#ifdef __cplusplus
typedef void* YbgReservoirState;
#else
typedef struct YbgReservoirStateData* YbgReservoirState;
#endif

/*
 * Allocate and initialize a YbgReservoirState.
 */
YbgStatus YbgSamplerCreate(double rstate_w, uint64_t randstate_s0, uint64_t randstate_s1, YbgReservoirState *yb_rs);

/*
 * Allocate and initialize a YbgReservoirState.
 */
YbgStatus YbgSamplerGetState(YbgReservoirState yb_rs, double *rstate_w, uint64_t *randstate_s0, uint64_t *randstate_s1);

/*
 * Select a random value R uniformly distributed in (0 - 1)
 */
YbgStatus YbgSamplerRandomFract(YbgReservoirState yb_rs, double *value);

/*
 * Calculate next number of rows to skip based on current number of scanned rows
 * and requested sample size.
 */
YbgStatus YbgReservoirGetNextS(YbgReservoirState yb_rs, double t, int n, double *s);

char* DecodeDatum(char const* fn_name, uintptr_t datum);

char* DecodeTZDatum(char const* fn_name, uintptr_t datum, const char *timezone, bool from_YB);

char* DecodeArrayDatum(char const* arr_fn_name, uintptr_t datum,
		int16_t elem_len, bool elem_by_val, char elem_align, char elem_delim, bool from_YB,
		char const* fn_name, const char *timezone, char option);

char* DecodeRangeDatum(char const* range_fn_name, uintptr_t datum,
		int16_t elem_len, bool elem_by_val, char elem_align, char option, bool from_YB,
		char const* elem_fn_name, int range_type, const char *timezone);

char* DecodeRangeArrayDatum(char const* arr_fn_name, uintptr_t datum,
		int16_t elem_len, int16_t range_len, bool elem_by_val, bool range_by_val,
		char elem_align, char range_align, char elem_delim, char option, char range_option,
		bool from_YB, char const* elem_fn_name, char const* range_fn_name, int range_type,
		const char *timezone);

char *DecodeRecordDatum(uintptr_t datum, void *attrs, size_t natts);

char *GetOutFuncName(const int pg_data_type);

uint32_t GetRecordTypeId(uintptr_t datum);

uintptr_t HeapFormTuple(void *attrs, size_t natts, uintptr_t *values,
						bool *nulls);

void HeapDeformTuple(uintptr_t datum, void *attrs, size_t natts,
					 uintptr_t *values, bool *nulls);

//-----------------------------------------------------------------------------
// PG Version
//-----------------------------------------------------------------------------
YbgStatus YbgGetPgVersion(const char **version);

#ifdef __cplusplus
}
#endif
