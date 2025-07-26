/*-------------------------------------------------------------------------
 *
 * ybgate_api.c
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
 *	  src/backend/ybgate/ybgate_api.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "catalog/pg_type_d.h"
#include "catalog/yb_type.h"
#include "common/int.h"
#include "executor/execExpr.h"
#include "executor/executor.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "nodes/execnodes.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/primnodes.h"
#include "pg_yb_utils.h"
#include "utils/acl.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/numeric.h"
#include "utils/sampling.h"
#include "utils/syscache.h"
#include "utils/yb_rowtypes.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "ybgate/ybgate_api.h"

YbgStatus
YbgInit()
{
	PG_SETUP_ERROR_REPORTING();

	SetDatabaseEncoding(PG_UTF8);

	PG_STATUS_OK();
}

/* ----------------------------------------------------------------------------- */
/*  Memory Context */
/* ----------------------------------------------------------------------------- */


YbgMemoryContext
YbgGetCurrentMemoryContext()
{
	return GetThreadLocalCurrentMemoryContext();
}

YbgMemoryContext
YbgSetCurrentMemoryContext(YbgMemoryContext memctx)
{
	return SetThreadLocalCurrentMemoryContext(memctx);
}

YbgStatus
YbgCreateMemoryContext(YbgMemoryContext parent,
					   const char *name,
					   YbgMemoryContext *memctx)
{
	PG_SETUP_ERROR_REPORTING();

	*memctx = CreateThreadLocalCurrentMemoryContext(parent, name);

	PG_STATUS_OK();
}

YbgStatus
YbgPrepareMemoryContext()
{
	PG_SETUP_ERROR_REPORTING();

	PrepareThreadLocalCurrentMemoryContext();

	PG_STATUS_OK();
}

YbgStatus
YbgResetMemoryContext()
{
	PG_SETUP_ERROR_REPORTING();

	ResetThreadLocalCurrentMemoryContext();

	PG_STATUS_OK();
}

YbgStatus
YbgDeleteMemoryContext()
{
	PG_SETUP_ERROR_REPORTING();

	DeleteThreadLocalCurrentMemoryContext();

	PG_STATUS_OK();
}

/* ----------------------------------------------------------------------------- */
/*  Types */
/* ----------------------------------------------------------------------------- */

YbgStatus
YbgGetTypeTable(YbcPgTypeEntities *types_entities)
{
	PG_SETUP_ERROR_REPORTING();

	*types_entities = YbGetTypeTable();

	PG_STATUS_OK();
}

YbgStatus
YbgGetPrimitiveTypeOid(uint32_t type_oid, char typtype, uint32_t typbasetype,
					   uint32_t *primitive_type_oid)
{
	PG_SETUP_ERROR_REPORTING();
	*primitive_type_oid = YbGetPrimitiveTypeOid(type_oid, typtype, typbasetype);
	PG_STATUS_OK();
}

/* ----------------------------------------------------------------------------- */
/*  Expression Evaluation */
/* ----------------------------------------------------------------------------- */

/*
 * Expression context for evaluating a YSQL expression from DocDB.
 * Currently includes the table row values to resolve scan variables.
 * TODO Eventually this should probably also have schema/type information.
 */
struct YbgExprContextData
{
	/* Values from table row. */
	int32_t		min_attno;
	int32_t		max_attno;
	Datum	   *attr_vals;
	Bitmapset  *attr_nulls;
};

/*
 * Evaluate an expression against an expression context.
 * Currently assumes the expression has been checked by the planner to only
 * allow immutable functions and the node types handled below.
 * TODO: this should use the general YSQL/PG expression evaluation framework, but
 * that requires syscaches and other dependencies to be fully initialized.
 */
static Datum
evalExpr(YbgExprContext ctx, Expr *expr, bool *is_null)
{
	switch (expr->type)
	{
		case T_FuncExpr:
		case T_OpExpr:
			{
				Oid			funcid;
				Oid			inputcollid;
				List	   *args;
				int			nargs;
				ListCell   *lc;

				/* Get the (underlying) function info. */
				if (IsA(expr, FuncExpr))
				{
					FuncExpr   *func_expr = castNode(FuncExpr, expr);

					args = func_expr->args;
					funcid = func_expr->funcid;
					inputcollid = func_expr->inputcollid;
				}
				else			/* (IsA(expr, OpExpr)) */
				{
					OpExpr	   *op_expr = castNode(OpExpr, expr);

					args = op_expr->args;
					funcid = op_expr->opfuncid;
					inputcollid = op_expr->inputcollid;
				}

				nargs = list_length(args);
				FmgrInfo   *flinfo = palloc0(sizeof(FmgrInfo));
				FunctionCallInfo fcinfo = palloc0(SizeForFunctionCallInfo(nargs));

				fmgr_info(funcid, flinfo);
				InitFunctionCallInfoData(*fcinfo,
										 flinfo,
										 nargs,
										 inputcollid,
										 NULL,
										 NULL);
				int			i = 0;

				foreach(lc, args)
				{
					Expr	   *arg = (Expr *) lfirst(lc);

					fcinfo->args[i].value = evalExpr(ctx, arg, &fcinfo->args[i].isnull);
					/*
					 * Strict functions are guaranteed to return NULL if any of
					 * their arguments are NULL.
					 */
					if (flinfo->fn_strict && fcinfo->args[i].isnull)
					{
						*is_null = true;
						return (Datum) 0;
					}
					i++;
				}
				Datum		result = FunctionCallInvoke(fcinfo);

				*is_null = fcinfo->isnull;
				return result;
			}
		case T_ScalarArrayOpExpr:
			{
				ScalarArrayOpExpr *saop_expr = castNode(ScalarArrayOpExpr, expr);
				bool		array_null;

				/*
				 * Get the array first. Null or empty array produces quick
				 * result
				 */
				Datum		array_arg = evalExpr(ctx, lsecond(saop_expr->args), &array_null);

				if (array_null)
				{
					*is_null = true;
					return (Datum) 0;
				}
				/* deconstruct_array inputs */
				ArrayType  *arr = (ArrayType *) DatumGetPointer(array_arg);
				Oid			elemtype = ARR_ELEMTYPE(arr);
				int16_t		elmlen;
				bool		elmbyval;
				char		elmalign;

				/* deconstruct_array outputs */
				Datum	   *elems;
				bool	   *nulls;
				int			array_len;

				/* planner must have checked that elemtype is supported */
				if (!YbTypeDetails(elemtype, &elmlen, &elmbyval, &elmalign))
					Assert(false);
				deconstruct_array(arr, elemtype, elmlen, elmbyval, elmalign,
								  &elems, &nulls, &array_len);
				if (array_len == 0)
				{
					return (Datum) !saop_expr->useOr;
				}
				/* Now get the operation function */
				FmgrInfo   *flinfo = palloc0(sizeof(FmgrInfo));
				FunctionCallInfo fcinfo = palloc0(SizeForFunctionCallInfo(2));

				fmgr_info(saop_expr->opfuncid, flinfo);
				InitFunctionCallInfoData(*fcinfo,
										 flinfo,
										 2 /* list_length(saop_expr->args) */ ,
										 saop_expr->inputcollid,
										 NULL,
										 NULL);
				fcinfo->args[0].value =
					evalExpr(ctx, linitial(saop_expr->args), &fcinfo->args[0].isnull);

				/*
				 * Strict function with null argument does not need to be
				 * evaluated
				 */
				if (flinfo->fn_strict && fcinfo->args[0].isnull)
				{
					*is_null = true;
					return (Datum) 0;
				}
				*is_null = false;
				for (int i = 0; i < array_len; i++)
				{
					fcinfo->args[1].value = elems[i];
					fcinfo->args[1].isnull = nulls[i];
					if (flinfo->fn_strict && fcinfo->args[1].isnull)
					{
						*is_null = true;
						continue;
					}
					bool		result = (bool) FunctionCallInvoke(fcinfo);

					if (fcinfo->isnull)
					{
						*is_null = true;
						continue;
					}
					if (saop_expr->useOr)
					{
						if (result)
						{
							*is_null = false;
							return (Datum) true;
						}
					}
					else
					{
						if (!result)
						{
							*is_null = false;
							return (Datum) false;
						}
					}
				}
				return *is_null ? (Datum) 0 : (Datum) !saop_expr->useOr;
			}
		case T_RelabelType:
			{
				RelabelType *rt = castNode(RelabelType, expr);

				return evalExpr(ctx, rt->arg, is_null);
			}
		case T_NullTest:
			{
				NullTest   *nt = castNode(NullTest, expr);
				bool		arg_is_null;

				evalExpr(ctx, nt->arg, &arg_is_null);
				*is_null = false;
				return (Datum) (nt->nulltesttype == IS_NULL) == arg_is_null;
			}
		case T_BoolExpr:
			{
				BoolExpr   *be = castNode(BoolExpr, expr);
				ListCell   *lc;
				Expr	   *arg;
				Datum		arg_value;
				bool		arg_is_null;

				switch (be->boolop)
				{
					case AND_EXPR:
						*is_null = false;
						foreach(lc, be->args)
						{
							arg = (Expr *) lfirst(lc);
							arg_value = evalExpr(ctx, arg, &arg_is_null);
							if (arg_is_null)
							{
								*is_null = true;
							}
							else if (!arg_value)
							{
								*is_null = false;
								return (Datum) false;
							}
						}
						return *is_null ? (Datum) 0 : (Datum) true;
					case OR_EXPR:
						*is_null = false;
						foreach(lc, be->args)
						{
							arg = (Expr *) lfirst(lc);
							arg_value = evalExpr(ctx, arg, &arg_is_null);
							if (arg_is_null)
							{
								*is_null = true;
							}
							else if (arg_value)
							{
								*is_null = false;
								return (Datum) true;
							}
						}
						return *is_null ? (Datum) 0 : (Datum) false;
					case NOT_EXPR:
						arg = (Expr *) linitial(be->args);
						arg_value = evalExpr(ctx, arg, is_null);
						return *is_null ? (Datum) 0 : (Datum) (!arg_value);
					default:
						/* Planner should ensure we never get here. */
						ereport(ERROR,
								(errcode(ERRCODE_INTERNAL_ERROR),
								 errmsg("unsupported boolop received by DocDB")));
						break;
				}
				return true;
			}
		case T_CaseExpr:
			{
				CaseExpr   *ce = castNode(CaseExpr, expr);
				ListCell   *lc;

				/*
				 * Support for implicit equality comparison would require catalog
				 * lookup to find equality operation for the argument data type.
				 */
				if (ce->arg)
					ereport(ERROR,
							(errcode(ERRCODE_INTERNAL_ERROR),
							 errmsg("unsupported CASE expression received by DocDB")));
				/*
				 * Evaluate WHEN clause expressions one by one, if any evaluation
				 * result is true, evaluate and return respective result expression
				 */
				foreach(lc, ce->args)
				{
					CaseWhen   *cw = castNode(CaseWhen, lfirst(lc));
					bool		arg_is_null;

					if (evalExpr(ctx, cw->expr, &arg_is_null))
						return evalExpr(ctx, cw->result, is_null);
				}
				/* None of the exprerssions was true, so evaluate the default. */
				if (ce->defresult)
					return evalExpr(ctx, ce->defresult, is_null);
				/* If default is not specified, return NULL */
				*is_null = true;
				return (Datum) 0;
			}
		case T_Const:
			{
				Const	   *const_expr = castNode(Const, expr);

				*is_null = const_expr->constisnull;
				return const_expr->constvalue;
			}
		case T_Var:
			{
				Var		   *var_expr = castNode(Var, expr);
				int32_t		att_idx = var_expr->varattno - ctx->min_attno;

				*is_null = bms_is_member(att_idx, ctx->attr_nulls);
				return ctx->attr_vals[att_idx];
			}
		default:
			/* Planner should ensure we never get here. */
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("unsupported YSQL expression received by DocDB")));
			break;
	}
	*is_null = true;
	return (Datum) 0;
}

YbgStatus
YbgExprContextCreate(int32_t min_attno, int32_t max_attno, YbgExprContext *expr_ctx)
{
	PG_SETUP_ERROR_REPORTING();

	YbgExprContext ctx = (YbgExprContext) palloc0(sizeof(struct YbgExprContextData));

	ctx->min_attno = min_attno;
	ctx->max_attno = max_attno;
	int32_t		num_attrs = max_attno - min_attno + 1;

	ctx->attr_vals = (Datum *) palloc0(sizeof(Datum) * num_attrs);
	ctx->attr_nulls = NULL;

	*expr_ctx = ctx;
	PG_STATUS_OK();
}

YbgStatus
YbgExprContextReset(YbgExprContext expr_ctx)
{
	PG_SETUP_ERROR_REPORTING();

	int32_t		num_attrs = expr_ctx->max_attno - expr_ctx->min_attno + 1;

	memset(expr_ctx->attr_vals, 0, sizeof(Datum) * num_attrs);
	expr_ctx->attr_nulls = NULL;

	PG_STATUS_OK();
}

YbgStatus
YbgExprContextAddColValue(YbgExprContext expr_ctx,
						  int32_t attno,
						  uint64_t datum,
						  bool is_null)
{
	PG_SETUP_ERROR_REPORTING();

	if (is_null)
	{
		expr_ctx->attr_nulls = bms_add_member(expr_ctx->attr_nulls, attno - expr_ctx->min_attno);
	}
	else
	{
		expr_ctx->attr_vals[attno - expr_ctx->min_attno] = (Datum) datum;
	}

	PG_STATUS_OK();
}

YbgStatus
YbgPrepareExpr(char *expr_cstring, YbgPreparedExpr *expr,
			   int yb_expression_version)
{
	PG_SETUP_ERROR_REPORTING();
	*expr = (YbgPreparedExpr) ybDeserializeNode(expr_cstring,
												yb_expression_version);
	PG_STATUS_OK();
}

YbgStatus
YbgExprType(const YbgPreparedExpr expr, int32_t *typid)
{
	PG_SETUP_ERROR_REPORTING();
	*typid = exprType((Node *) expr);
	PG_STATUS_OK();
}

YbgStatus
YbgExprTypmod(const YbgPreparedExpr expr, int32_t *typmod)
{
	PG_SETUP_ERROR_REPORTING();
	*typmod = exprTypmod((Node *) expr);
	PG_STATUS_OK();
}

YbgStatus
YbgExprCollation(const YbgPreparedExpr expr, int32_t *collid)
{
	PG_SETUP_ERROR_REPORTING();
	*collid = exprCollation((Node *) expr);
	PG_STATUS_OK();
}

YbgStatus
YbgEvalExpr(YbgPreparedExpr expr, YbgExprContext expr_ctx, uint64_t *datum, bool *is_null)
{
	PG_SETUP_ERROR_REPORTING();
	*datum = (uint64_t) evalExpr(expr_ctx, expr, is_null);
	PG_STATUS_OK();
}

/* YB_TODO(Deepthi@yugabyte)
 * - Postgres 13 has added some new types. Need to update this function accordingly.
 * - It'd be best if you use the table "static const YbcPgTypeEntity YbTypeEntityTable[]". Just
 *   as the attributes that you need, such as (elmlen, elmbyval, ...), and fill the table with
 *   their values. That way, when upgrading we don't have to seek for location of datatypes every
 *   where and update the info.
 */
YbgStatus
YbgSplitArrayDatum(uint64_t datum,
				   const int type,
				   uint64_t **result_datum_array,
				   int *const nelems)
{
	PG_SETUP_ERROR_REPORTING();
	ArrayType  *arr = DatumGetArrayTypeP((Datum) datum);

	if (ARR_NDIM(arr) != 1 || ARR_HASNULL(arr) || ARR_ELEMTYPE(arr) != type)
		return YbgStatusCreateError("Type of given datum array does not match the given type",
									__FILE__, __LINE__);

	int16_t		elmlen;
	bool		elmbyval;
	char		elmalign;

	/*
	 * Ideally this information should come from pg_type or from caller instead of hardcoding
	 * here. However this could be okay as PG also has this harcoded in few places.
	 */
	if (!YbTypeDetails(type, &elmlen, &elmbyval, &elmalign))
	{
		return YbgStatusCreateError("Only Text type supported for split of datum of array types",
									__FILE__, __LINE__);
	}
	deconstruct_array(arr, type, elmlen, elmbyval, elmalign,
					  (Datum **) result_datum_array, NULL /* nullsp */ , nelems);
	PG_STATUS_OK();
}

/* ----------------------------------------------------------------------------- */
/*  Relation sampling */
/* ----------------------------------------------------------------------------- */

struct YbgReservoirStateData
{
	ReservoirStateData rs;
};

YbgStatus
YbgSamplerCreate(double rstate_w, uint64_t randstate_s0, uint64_t randstate_s1, YbgReservoirState *yb_rs)
{
	PG_SETUP_ERROR_REPORTING();
	YbgReservoirState rstate = (YbgReservoirState) palloc0(sizeof(struct YbgReservoirStateData));

	rstate->rs.W = rstate_w;
	rstate->rs.randstate.s0 = randstate_s0;
	rstate->rs.randstate.s1 = randstate_s1;
	*yb_rs = rstate;
	PG_STATUS_OK();
}

YbgStatus
YbgSamplerGetState(YbgReservoirState yb_rs, double *rstate_w, uint64_t *randstate_s0, uint64_t *randstate_s1)
{
	PG_SETUP_ERROR_REPORTING();
	*rstate_w = yb_rs->rs.W;
	*randstate_s0 = yb_rs->rs.randstate.s0;
	*randstate_s1 = yb_rs->rs.randstate.s1;
	PG_STATUS_OK();
}

YbgStatus
YbgSamplerRandomFract(YbgReservoirState yb_rs, double *value)
{
	PG_SETUP_ERROR_REPORTING();
	ReservoirState rs = &yb_rs->rs;

	*value = sampler_random_fract(&rs->randstate);
	PG_STATUS_OK();
}

YbgStatus
YbgReservoirGetNextS(YbgReservoirState yb_rs, double t, int n, double *s)
{
	PG_SETUP_ERROR_REPORTING();
	*s = reservoir_get_next_S(&yb_rs->rs, t, n);
	PG_STATUS_OK();
}

char *
DecodeDatum(char const *fn_name, uintptr_t datum)
{
	FmgrInfo   *finfo;

	finfo = palloc0(sizeof(FmgrInfo));
	Oid			id = fmgr_internal_function(fn_name);

	fmgr_info(id, finfo);
	char	   *tmp = OutputFunctionCall(finfo, (uintptr_t) datum);

	return tmp;
}

char *
DecodeTZDatum(char const *fn_name, uintptr_t datum, const char *timezone, bool from_YB)
{
	FmgrInfo   *finfo;

	finfo = palloc0(sizeof(FmgrInfo));
	Oid			id = fmgr_internal_function(fn_name);

	fmgr_info(id, finfo);

	YbDatumDecodeOptions decodeOptions;

	decodeOptions.timezone = timezone;
	decodeOptions.from_YB = from_YB;
	decodeOptions.range_datum_decode_options = NULL;
	return DatumGetCString(FunctionCall2(finfo, (uintptr_t) datum,
										 PointerGetDatum(&decodeOptions)));
}

char *
DecodeArrayDatum(char const *arr_fn_name, uintptr_t datum,
				 int16_t elem_len, bool elem_by_val, char elem_align, char elem_delim, bool from_YB,
				 char const *fn_name, const char *timezone, char option)
{
	FmgrInfo   *arr_finfo;

	arr_finfo = palloc0(sizeof(FmgrInfo));
	Oid			arr_id = fmgr_internal_function(arr_fn_name);

	fmgr_info(arr_id, arr_finfo);

	FmgrInfo   *elem_finfo;

	elem_finfo = palloc0(sizeof(FmgrInfo));
	Oid			elem_id = fmgr_internal_function(fn_name);

	fmgr_info(elem_id, elem_finfo);

	YbDatumDecodeOptions decodeOptions;

	decodeOptions.is_array = true;
	decodeOptions.elem_by_val = elem_by_val;
	decodeOptions.from_YB = from_YB;
	decodeOptions.elem_align = elem_align;
	decodeOptions.elem_delim = elem_delim;
	decodeOptions.option = option;
	decodeOptions.elem_len = elem_len;
	/* decodeOptions.datum = datum; */
	decodeOptions.elem_finfo = elem_finfo;
	decodeOptions.timezone = timezone;
	decodeOptions.range_datum_decode_options = NULL;

	char	   *tmp = DatumGetCString(FunctionCall2(arr_finfo, (uintptr_t) datum,
													PointerGetDatum(&decodeOptions)));

	return tmp;
}

char *
DecodeRangeDatum(char const *range_fn_name, uintptr_t datum,
				 int16_t elem_len, bool elem_by_val, char elem_align, char option, bool from_YB,
				 char const *elem_fn_name, int range_type, const char *timezone)
{
	FmgrInfo   *range_finfo;

	range_finfo = palloc0(sizeof(FmgrInfo));
	Oid			range_id = fmgr_internal_function(range_fn_name);

	fmgr_info(range_id, range_finfo);

	FmgrInfo   *elem_finfo;

	elem_finfo = palloc0(sizeof(FmgrInfo));
	Oid			elem_id = fmgr_internal_function(elem_fn_name);

	fmgr_info(elem_id, elem_finfo);

	YbDatumDecodeOptions decodeOptions;

	decodeOptions.is_array = false;
	decodeOptions.elem_by_val = elem_by_val;
	decodeOptions.from_YB = from_YB;
	decodeOptions.elem_align = elem_align;
	decodeOptions.option = option;
	decodeOptions.elem_len = elem_len;
	decodeOptions.range_type = range_type;
	/* decodeOptions.datum = datum; */
	decodeOptions.elem_finfo = elem_finfo;
	decodeOptions.timezone = timezone;
	decodeOptions.range_datum_decode_options = NULL;

	char	   *tmp = DatumGetCString(FunctionCall2(range_finfo, (uintptr_t) datum,
													PointerGetDatum(&decodeOptions)));

	return tmp;
}

char *
DecodeRangeArrayDatum(char const *arr_fn_name, uintptr_t datum,
					  int16_t elem_len, int16_t range_len, bool elem_by_val, bool range_by_val,
					  char elem_align, char range_align, char elem_delim, char option, char range_option,
					  bool from_YB, char const *elem_fn_name, char const *range_fn_name, int range_type,
					  const char *timezone)
{
	FmgrInfo   *arr_finfo;

	arr_finfo = palloc0(sizeof(FmgrInfo));
	Oid			arr_id = fmgr_internal_function(arr_fn_name);

	fmgr_info(arr_id, arr_finfo);

	FmgrInfo   *range_finfo;

	range_finfo = palloc0(sizeof(FmgrInfo));
	Oid			range_id = fmgr_internal_function(range_fn_name);

	fmgr_info(range_id, range_finfo);

	FmgrInfo   *elem_finfo;

	elem_finfo = palloc0(sizeof(FmgrInfo));
	Oid			elem_id = fmgr_internal_function(elem_fn_name);

	fmgr_info(elem_id, elem_finfo);

	YbDatumDecodeOptions range_decodeOptions;

	range_decodeOptions.is_array = false;
	range_decodeOptions.elem_by_val = range_by_val;
	range_decodeOptions.from_YB = from_YB;
	range_decodeOptions.elem_align = range_align;
	range_decodeOptions.option = range_option;
	range_decodeOptions.elem_len = range_len;
	range_decodeOptions.range_type = range_type;
	range_decodeOptions.elem_finfo = range_finfo;
	range_decodeOptions.timezone = timezone;
	range_decodeOptions.range_datum_decode_options = NULL;

	YbDatumDecodeOptions arr_decodeOptions;

	arr_decodeOptions.is_array = true;
	arr_decodeOptions.elem_by_val = elem_by_val;
	arr_decodeOptions.from_YB = from_YB;
	arr_decodeOptions.elem_align = elem_align;
	arr_decodeOptions.elem_delim = elem_delim;
	arr_decodeOptions.option = option;
	arr_decodeOptions.elem_len = elem_len;
	arr_decodeOptions.elem_finfo = elem_finfo;
	arr_decodeOptions.timezone = timezone;
	arr_decodeOptions.range_datum_decode_options = &range_decodeOptions;

	char	   *tmp = DatumGetCString(FunctionCall2(arr_finfo, (uintptr_t) datum,
													PointerGetDatum(&arr_decodeOptions)));

	return tmp;
}

char *
DecodeRecordDatum(uintptr_t datum, void *attrs, size_t natts)
{
	HeapTupleHeader rec = DatumGetHeapTupleHeader(datum);
	TupleDesc	tupdesc = CreateTupleDesc(natts, attrs);

	return DatumGetCString(YbRecordOut(rec, tupdesc));
}

uint32_t
GetRecordTypeId(uintptr_t datum)
{
	HeapTupleHeader rec = DatumGetHeapTupleHeader(datum);

	return HeapTupleHeaderGetTypeId(rec);
}

uintptr_t
HeapFormTuple(void *attrs, size_t natts, uintptr_t *values, bool *nulls)
{
	TupleDesc	tupdesc = CreateTupleDesc(natts, attrs);

	PG_RETURN_HEAPTUPLEHEADER(heap_form_tuple(tupdesc, values, nulls)->t_data);
}

void
HeapDeformTuple(uintptr_t datum, void *attrs, size_t natts, uintptr_t *values,
				bool *nulls)
{
	HeapTupleHeader rec = DatumGetHeapTupleHeader(datum);
	HeapTupleData tuple;

	tuple.t_len = HeapTupleHeaderGetDatumLength(rec);
	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;
	tuple.t_data = rec;
	TupleDesc	tupdesc = CreateTupleDesc(natts, attrs);

	/* Break down the tuple into fields */
	heap_deform_tuple(&tuple, tupdesc, values, nulls);
}

int
YbgGetPgVersion()
{
	return PG_MAJORVERSION_NUM;
}
