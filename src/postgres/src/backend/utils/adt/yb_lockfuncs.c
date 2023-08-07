/*--------------------------------------------------------------------------------------------------
*
* yb_lockfuncs.c
*	  Functions for SQL access to YugabyteDB locking primitives
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
*		src/backend/utils/adt/yb_lockfuncs.c
*
*--------------------------------------------------------------------------------------------------
*/

#include "postgres.h"
#include "pg_yb_utils.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/ybcFunction.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/array.h"
#include "utils/builtins.h"

/* Number of columns in yb_lock_status output */
#define YB_NUM_LOCK_STATUS_COLUMNS 21

/*
 * yb_lock_status - produce a view with one row per held or awaited lock
 */
Datum
yb_lock_status(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	YbFuncCallContext yb_funcctx;

	if (!yb_enable_pg_locks)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("lock status is unavailable"),
						errdetail("yb_enable_pg_locks is false or a system "
								  "upgrade is in progress")));
	}

	/*
	 *  If this is not a superuser, do not return actual user data.
	 *  TODO: Remove this as soon as we mask out user data.
	 */
	if (!superuser_arg(GetUserId()) || !IsYbDbAdminUser(GetUserId()))
	{
		ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
						errmsg("permission denied: user must must be a "
							   "superuser or a member of the yb_db_admin role "
							   "to view lock status")));
	}

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	  tupdesc;
		MemoryContext oldcontext;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* build tupdesc for result tuples */
		/* this had better match function's declaration in pg_proc.h */
		tupdesc = CreateTemplateTupleDesc(YB_NUM_LOCK_STATUS_COLUMNS, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "locktype",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "database",
						   OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "relation",
						   OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "pid",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "mode",
						   TEXTARRAYOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "granted",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7, "fastpath",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 8, "waitstart",
						   TIMESTAMPTZOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 9, "waitend",
						   TIMESTAMPTZOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 10, "node",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 11, "tablet_id",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 12, "transaction_id",
						   UUIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 13, "subtransaction_id",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 14, "status_tablet_id",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 15, "is_explicit",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 16, "hash_cols",
						   TEXTARRAYOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 17, "range_cols",
						   TEXTARRAYOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 18, "attnum",
						   INT2OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 19, "column_id",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 20, "multiple_rows_locked",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 21, "blocked_by",
						   UUIDARRAYOID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		yb_funcctx = YbNewFuncCallContext(funcctx);

		HandleYBStatus(YBCNewGetLockStatusDataSRF(&yb_funcctx->handle));
		YbSetFunctionParam(yb_funcctx->handle, "relation", OIDOID,
						   (uint64_t) PG_GETARG_DATUM(0), PG_ARGISNULL(0));
		YbSetFunctionParam(yb_funcctx->handle, "database", OIDOID,
						   (uint64_t) ObjectIdGetDatum(MyDatabaseId), false);
		YbSetFunctionParam(yb_funcctx->handle, "transaction_id", UUIDOID,
						   (uint64_t) PG_GETARG_DATUM(1), PG_ARGISNULL(1));
		YbSetSRFTargets(yb_funcctx, tupdesc);

		funcctx->user_fctx = (void *) yb_funcctx;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	yb_funcctx = funcctx->user_fctx;

	Datum values[YB_NUM_LOCK_STATUS_COLUMNS];
	bool  nulls[YB_NUM_LOCK_STATUS_COLUMNS];

	while (YbSRFGetNext(yb_funcctx, (uint64_t *) values, nulls))
	{
		HeapTuple tuple;
		Datum	  result;

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}

	SRF_RETURN_DONE(funcctx);
}
