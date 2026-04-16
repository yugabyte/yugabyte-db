/*-------------------------------------------------------------------------
 *
 * ybgate_api.h
 *	  YbGate interface functions.
 *	  YbGate allows to execute Postgres code from DocDB
 *
 * Copyright (c) YugabyteDB, Inc.
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
extern "C"
{
#endif

/*-----------------------------------------------------------------------------
 * Error Reporting
 *-----------------------------------------------------------------------------
 * !!! IMPORTANT: All public functions in the ybgate directory MUST return a
 * YbgStatus and call PG_SETUP_ERROR_REPORTING first, before any execution.
 *
 * This will ensure that any elog or ereport called internally by a YSQL/PG
 * function will be caught/handled (and not crash the tserver process).
 *-----------------------------------------------------------------------------
 */

#ifdef __cplusplus
typedef void *YbgMemoryContext;
#else
typedef MemoryContext YbgMemoryContext;
#endif

/*-----------------------------------------------------------------------------
 * Memory Context
 *-----------------------------------------------------------------------------
 */

extern YbgMemoryContext YbgGetCurrentMemoryContext();

extern YbgMemoryContext YbgSetCurrentMemoryContext(YbgMemoryContext memctx);

extern YbgStatus YbgCreateMemoryContext(YbgMemoryContext parent,
										const char *name,
										YbgMemoryContext *memctx);

extern YbgStatus YbgPrepareMemoryContext();

extern YbgStatus YbgResetMemoryContext();

extern YbgStatus YbgDeleteMemoryContext();

extern YbgStatus YbgInit();

/*-----------------------------------------------------------------------------
 * Types
 *-----------------------------------------------------------------------------
 */

struct YbPgAttributeRow
{
	uint32_t	attrelid;
	char		attname[64];
	uint32_t	atttypid;
	int32_t		attstattarget;
	int16_t		attlen;
	int16_t		attnum;
	int32_t		attndims;
	int32_t		attcacheoff;
	int32_t		atttypmod;
	bool		attbyval;
	char		attstorage;
	char		attalign;
	bool		attnotnull;
	bool		atthasdef;
	bool		atthasmissing;
	char		attidentity;
	bool		attisdropped;
	bool		attislocal;
	int32_t		attinhcount;
	uint32_t	attcollation;
};

#ifndef __cplusplus
typedef struct YbPgAttributeRow YbPgAttributeRow;
#endif

struct YbgTypeDesc
{
	int32_t		type_id;		/* type identifier */
	int32_t		type_mod;		/* type modifier */
};

#ifndef __cplusplus
typedef struct YbgTypeDesc YbgTypeDesc;
#endif

/*
 * Get the type entity table for the primitive YSQL/PG types.
 * Used for for converting between DocDB values and YSQL datums.
 */
extern YbgStatus YbgGetTypeTable(YbcPgTypeEntities *type_entities);

/*
 * For non-primitive types (the ones without a corresponding YbcPgTypeEntity),
 * get the corresponding primitive type's oid.
 */
extern YbgStatus YbgGetPrimitiveTypeOid(uint32_t type_oid, char typtype,
										uint32_t typbasetype,
										uint32_t *primitive_type_oid);

/*-----------------------------------------------------------------------------
 * Expression Evaluation
 *-----------------------------------------------------------------------------
 */

#ifdef __cplusplus
typedef void *YbgExprContext;
typedef void *YbgPreparedExpr;
#else
typedef struct YbgExprContextData *YbgExprContext;
typedef struct Expr *YbgPreparedExpr;
#endif

/*
 * Create an expression context to evaluate expressions against.
 */
extern YbgStatus YbgExprContextCreate(int32_t min_attno, int32_t max_attno,
									  YbgExprContext *expr_ctx);

extern YbgStatus YbgExprContextReset(YbgExprContext expr_ctx);

/*
 * Add a column value from the table row.
 * Used by expression evaluation to resolve scan variables.
 */
extern YbgStatus YbgExprContextAddColValue(YbgExprContext expr_ctx, int32_t attno,
										   uint64_t datum, bool is_null);

extern YbgStatus YbgPrepareExpr(char *expr_cstring, YbgPreparedExpr *expr,
								int yb_expression_version);

extern YbgStatus YbgExprType(const YbgPreparedExpr expr, int32_t *typid);

extern YbgStatus YbgExprTypmod(const YbgPreparedExpr expr, int32_t *typmod);

extern YbgStatus YbgExprCollation(const YbgPreparedExpr expr, int32_t *collid);

/*
 * Evaluate an expression, using the expression context to resolve scan variables.
 * Will filling in datum and is_null with the result.
 */
extern YbgStatus YbgEvalExpr(YbgPreparedExpr expr, YbgExprContext expr_ctx, uint64_t *datum,
							 bool *is_null);

/*
 * Given a 'datum' of array type, split datum into individual elements of type 'type' and store
 * the result in 'result_datum_array', with number of elements in 'nelems'. This will error out
 * if 'type' doesn't match the type of the individual elements in 'datum'. Memory for
 * 'result_datum_array' will be allocated in this function itself, pre-allocation is not needed.
 */
extern YbgStatus YbgSplitArrayDatum(uint64_t datum, int type, uint64_t **result_datum_array,
									int *nelems);

/*-----------------------------------------------------------------------------
 * Relation sampling
 *-----------------------------------------------------------------------------
 */

#ifdef __cplusplus
typedef void *YbgReservoirState;
#else
typedef struct YbgReservoirStateData *YbgReservoirState;
#endif

/*
 * Allocate and initialize a YbgReservoirState.
 */
extern YbgStatus YbgSamplerCreate(double rstate_w, uint64_t randstate_s0, uint64_t randstate_s1,
								  YbgReservoirState *yb_rs);

/*
 * Allocate and initialize a YbgReservoirState.
 */
extern YbgStatus YbgSamplerGetState(YbgReservoirState yb_rs, double *rstate_w,
									uint64_t *randstate_s0, uint64_t *randstate_s1);

/*
 * Select a random value R uniformly distributed in (0 - 1)
 */
extern YbgStatus YbgSamplerRandomFract(YbgReservoirState yb_rs, double *value);

/*
 * Calculate next number of rows to skip based on current number of scanned rows
 * and requested sample size.
 */
extern YbgStatus YbgReservoirGetNextS(YbgReservoirState yb_rs, double t, int n, double *s);

extern char *DecodeDatum(char const *fn_name, uintptr_t datum);

extern char *DecodeTZDatum(char const *fn_name, uintptr_t datum, const char *timezone,
						   bool from_YB);

extern char *DecodeArrayDatum(char const *arr_fn_name, uintptr_t datum, int16_t elem_len,
							  bool elem_by_val, char elem_align, char elem_delim, bool from_YB,
							  char const *fn_name, const char *timezone, char option);

extern char *DecodeRangeDatum(char const *range_fn_name, uintptr_t datum, int16_t elem_len,
							  bool elem_by_val, char elem_align, char option, bool from_YB,
							  char const *elem_fn_name, int range_type, const char *timezone);

extern char *DecodeRangeArrayDatum(char const *arr_fn_name, uintptr_t datum, int16_t elem_len,
								   int16_t range_len, bool elem_by_val, bool range_by_val,
								   char elem_align, char range_align, char elem_delim,
								   char option, char range_option, bool from_YB,
								   char const *elem_fn_name, char const *range_fn_name,
								   int range_type, const char *timezone);

extern char *DecodeRecordDatum(uintptr_t datum, void *attrs, size_t natts);

extern uint32_t GetRecordTypeId(uintptr_t datum);

extern uintptr_t HeapFormTuple(void *attrs, size_t natts, uintptr_t *values,
							   bool *nulls);

extern void HeapDeformTuple(uintptr_t datum, void *attrs, size_t natts,
							uintptr_t *values, bool *nulls);

/*-----------------------------------------------------------------------------
 * PG Version
 *-----------------------------------------------------------------------------
 */
extern int	YbgGetPgVersion();

/*-----------------------------------------------------------------------------
 * Ident Conf
 *-----------------------------------------------------------------------------
 */

/*
 * Read the ident config file and create a List of IdentLine records for
 * the contents.
 *
 * Serves the purpose of load_ident from hba.c but with more control over file
 * path and memory context so that this can be called from YCQL.
 *
 * Caller is responsible to free the memory context in case of errors.
 */
extern YbgStatus YbgLoadIdent(const char *ident_file_path,
							  YbgMemoryContext ident_context);

extern YbgStatus YbgCheckUsermap(const char *usermap_name, const char *pg_role,
								 const char *auth_user, bool case_insensitive,
								 bool *matched);

#ifdef __cplusplus
}
#endif
