/*-------------------------------------------------------------------------
 *
 * yb_lockrows.c
 *	  YB row-locking and conflict-handling functions.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/backend/access/common/yb_lockrows.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/sysattr.h"
#include "access/yb_lockrows.h"
#include "access/yb_scan_core.h"
#include "catalog/catalog.h"
#include "executor/executor.h"
#include "pg_yb_utils.h"
#include "utils/rel.h"
#include "utils/relcache.h"

/*
 * The return value of this function depends on whether we are batching or not.
 * Currently, batching is enabled if the GUC yb_explicit_row_locking_batch_size > 1
 * and the wait policy is not "SKIP LOCKED".
 * If we are batching, then the return value is just a placeholder, as we are not
 * acquiring the lock on the row before returning.
 * Otherwise, the returned TM_Result is adjusted in case of an error in acquiring the lock.
 */
TM_Result
YBCLockTuple(Relation relation, Datum ybctid, RowMarkType mode,
			 LockWaitPolicy pg_wait_policy, EState *estate,
			 YbcIsExplicitlyLockedRowSkippedCheckHandleOptional *handle)
{
	/*
	 * Lock catalog rows at the snapshot the scan read them at, which can
	 * differ from the read point current when the lock buffer is flushed.
	 */
	const bool	is_catalog = (IsCatalogRelation(relation) &&
							  !YBCIsLegacyModeForCatalogOps());
	const YbcPgExplicitRowLockParams lock_params = {
		.rowmark = mode,
		.pg_wait_policy = pg_wait_policy,
		.docdb_wait_policy = YBGetDocDBWaitPolicy(pg_wait_policy),
		.catalog_read_point = (is_catalog ?
							   YbGetCatalogReadPoint(estate->es_snapshot) :
							   YbcInvalidReadPointHandle)
	};

	const Oid	relfile_oid = YbGetRelfileNodeId(relation);
	const Oid	db_oid = YBCGetDatabaseOid(relation);

	if (yb_explicit_row_locking_batch_size > 1 &&
		(lock_params.pg_wait_policy != LockWaitSkip || handle))
	{
		HandleExplicitRowLockStatus(YBCAddExplicitRowLockIntent(relfile_oid,
																ybctid, db_oid,
																&lock_params,
																YbBuildTableLocalityInfo(relation),
																handle));
		return TM_Ok;
	}

	YbcPgStatement ybc_stmt = YbNewSelect(relation, NULL /* prepare_params */ );

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(ybc_stmt, BYTEAOID, InvalidOid, ybctid, false);

	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	if (lock_params.catalog_read_point != YbcInvalidReadPointHandle)
		HandleYBStatus(YBCPgDmlSetCatalogReadPoint(ybc_stmt,
												   lock_params.catalog_read_point));

	YbcPgExecParameters exec_params = {0};

	exec_params.plan_limit = 1;
	exec_params.rowmark = lock_params.rowmark;
	exec_params.pg_wait_policy = lock_params.pg_wait_policy;
	exec_params.docdb_wait_policy = lock_params.docdb_wait_policy;
	exec_params.stmt_in_txn_limit_ht_for_reads =
		estate->yb_exec_params.stmt_in_txn_limit_ht_for_reads;

	TM_Result	res = TM_Ok;
	bool		has_data = false;
	Datum	   *values = NULL;
	bool	   *nulls = NULL;
	YbcPgSysColumns syscols;

	HandleYBStatus(YBCPgExecSelect(ybc_stmt, &exec_params));
	YbcStatus status = YBCPgDmlFetch(ybc_stmt, 0, (uint64_t *) values, nulls, &syscols, &has_data);
	if (!status)
		YBCPgAddIntoForeignKeyReferenceCache(relfile_oid, ybctid);
	else
	{
		const uint32_t err_code = YBCStatusPgsqlError(status);
		elog(DEBUG2, "Error when trying to lock row. "
			 "pg_wait_policy=%d docdb_wait_policy=%d message=%s err_code=%d",
			 lock_params.pg_wait_policy, lock_params.docdb_wait_policy,
			 YBCStatusMessageBegin(status), err_code);

		switch(err_code)
		{
			case ERRCODE_YB_TXN_CONFLICT:
				res = TM_Updated;
				break;
			case ERRCODE_YB_TXN_SKIP_LOCKING:
				res = TM_WouldBlock;
				break;
			default:
				HandleYBStatus(status);
				break;
		}
		YBCFreeStatus(status);
	}
	YBCPgDeleteStatement(ybc_stmt);
	return res;
}

void
YBCFlushTupleLocks()
{
	HandleExplicitRowLockStatus(YBCFlushExplicitRowLockIntents());
}
