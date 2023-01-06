/*--------------------------------------------------------------------------------------------------
 * ybcFunction.c
 *        Routines to construct a Postgres functions
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/executor/ybcFunction.c
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/tupdesc.h"
#include "utils/memutils.h"

#include "catalog/yb_type.h"
#include "executor/ybcFunction.h"
#include "pg_yb_utils.h"

YbFuncCallContext
YbNewFuncCallContext(FuncCallContext *funcctx)
{
	MemoryContext per_call_ctx;

	per_call_ctx = AllocSetContextCreate(funcctx->multi_call_memory_ctx,
										 "YB SRF per-call context",
										 ALLOCSET_SMALL_SIZES);

	YbFuncCallContext yb_funcctx = (YbFuncCallContext) MemoryContextAllocZero(
		funcctx->multi_call_memory_ctx, sizeof(FuncCallContext));

	yb_funcctx->per_call_ctx = per_call_ctx;

	return yb_funcctx;
}

void
YbSetFunctionParam(YBCPgFunction handle, const char *name, int attr_typid,
				   uint64_t datum, bool is_null)
{
	const YBCPgTypeEntity *type_entity =
		YbDataTypeFromOidMod(InvalidAttrNumber, attr_typid);
	HandleYBStatus(
		YBCAddFunctionParam(handle, name, type_entity, datum, is_null));
}

void
YbSetSRFTargets(YbFuncCallContext context, TupleDesc desc)
{
	for (int attr_num = 0; attr_num < desc->natts; ++attr_num)
	{
		FormData_pg_attribute *attr = TupleDescAttr(desc, attr_num);

		if (attr->attisdropped)
			continue;

		const char *attr_name = NameStr(attr->attname);
		const YBCPgTypeEntity *type_entity =
			YbDataTypeFromOidMod(attr->attnum, attr->atttypid);
		const YBCPgTypeAttrs type_attrs = {.typmod = attr->atttypmod};

		HandleYBStatus(YBCAddFunctionTarget(context->handle, attr_name,
											type_entity, type_attrs));
	}
	HandleYBStatus(YBCFinalizeFunctionTargets(context->handle));
}

bool
YbSRFGetNext(YbFuncCallContext context, uint64_t *values, bool *is_nulls)
{
	MemoryContext oldcontext;
	oldcontext = MemoryContextSwitchTo(context->per_call_ctx);

	MemoryContextReset(context->per_call_ctx);

	bool has_data = false;
	HandleYBStatus(YBCSRFGetNext(context->handle, values, is_nulls, &has_data));

	MemoryContextSwitchTo(oldcontext);

	return has_data;
}
