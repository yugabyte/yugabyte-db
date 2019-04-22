/*--------------------------------------------------------------------------------------------------
 *
 * ybcModifyTable.c
 *        YB routines to stmt_handle ModifyTable nodes.
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
 *        src/backend/executor/ybcModifyTable.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "catalog/pg_type.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "nodes/execnodes.h"
#include "commands/dbcommands.h"
#include "executor/tuptable.h"
#include "executor/ybcExpr.h"
#include "executor/ybcModifyTable.h"
#include "miscadmin.h"
#include "catalog/catalog.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_database.h"
#include "utils/inval.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "executor/tuptable.h"
#include "executor/ybcExpr.h"

#include "utils/syscache.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

/*
 * Returns whether a relation's attribute is a real column in the backing
 * YugaByte table. (It implies we can both read from and write to it).
 */
bool IsRealYBColumn(Relation rel, int attrNum)
{
	return (attrNum > 0 && !TupleDescAttr(rel->rd_att, attrNum - 1)->attisdropped) ||
	       (rel->rd_rel->relhasoids && attrNum == ObjectIdAttributeNumber);
}

/*
 * Get the type ID of a real or virtual attribute (column).
 * Returns InvalidOid if the attribute number is invalid.
 */
static Oid GetTypeId(int attrNum, TupleDesc tupleDesc)
{
	switch (attrNum)
	{
		case SelfItemPointerAttributeNumber:
			return TIDOID;
		case ObjectIdAttributeNumber:
			return OIDOID;
		case MinTransactionIdAttributeNumber:
			return XIDOID;
		case MinCommandIdAttributeNumber:
			return CIDOID;
		case MaxTransactionIdAttributeNumber:
			return XIDOID;
		case MaxCommandIdAttributeNumber:
			return CIDOID;
		case TableOidAttributeNumber:
			return OIDOID;
		default:
			if (attrNum > 0 && attrNum <= tupleDesc->natts)
				return TupleDescAttr(tupleDesc, attrNum - 1)->atttypid;
			else
				return InvalidOid;
	}
}

/*
 * Get primary key columns as bitmap of a table.
 */
static Bitmapset *GetTablePrimaryKey(Relation rel)
{
	Oid            dboid         = YBCGetDatabaseOid(rel);
	Oid            relid         = RelationGetRelid(rel);
	AttrNumber     minattr       = FirstLowInvalidHeapAttributeNumber + 1;
	int            natts         = RelationGetNumberOfAttributes(rel);
	Bitmapset      *pkey         = NULL;
	YBCPgTableDesc ybc_tabledesc = NULL;

	/* Get the primary key columns 'pkey' from YugaByte. */
	HandleYBStatus(YBCPgGetTableDesc(ybc_pg_session, dboid, relid, &ybc_tabledesc));
	for (AttrNumber attnum = minattr; attnum <= natts; attnum++)
	{
		if (!IsRealYBColumn(rel, attnum))
		{
			continue;
		}

		bool is_primary = false;
		bool is_hash    = false;
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_tabledesc,
		                                           attnum,
		                                           &is_primary,
		                                           &is_hash), ybc_tabledesc);
		if (is_primary)
		{
			pkey = bms_add_member(pkey, attnum - minattr);
		}
	}
	HandleYBStatus(YBCPgDeleteTableDesc(ybc_tabledesc));

	return pkey;
}

/*
 * Get the ybctid from a YB scan slot for UPDATE/DELETE.
 */
Datum YBCGetYBTupleIdFromSlot(TupleTableSlot *slot)
{
	/*
	 * Look for ybctid in the tuple first if the slot contains a tuple packed with ybctid.
	 * Otherwise, look for it in the attribute list as a junk attribute.
	 */
	if (slot->tts_tuple != NULL && slot->tts_tuple->t_ybctid != 0)
	{
		return slot->tts_tuple->t_ybctid;
	}

	for (int idx = 0; idx < slot->tts_nvalid; idx++)
	{
		Form_pg_attribute att = TupleDescAttr(slot->tts_tupleDescriptor, idx);
		if (strcmp(NameStr(att->attname), "ybctid") == 0 && !slot->tts_isnull[idx])
		{
			Assert(att->atttypid == BYTEAOID);
			return slot->tts_values[idx];
		}
	}

	return 0;
}

/*
 * Check if operation changes a system table, ignore changes during
 * initialization (bootstrap mode).
 */
static bool IsSystemCatalogChange(Relation rel)
{
	return IsSystemRelation(rel) &&
	       !IsBootstrapProcessingMode();
}

/*
 * Utility method to execute a prepared write statement.
 * Will handle the case if the write changes the system catalogs meaning
 * we need to increment the catalog versions accordingly.
 */
static YBCStatus YBCExecWriteStmt(YBCPgStatement ybc_stmt, Relation rel)
{
	bool is_syscatalog_change = IsSystemCatalogChange(rel);
	bool is_syscatalog_version_change = false;

	/* Let the master know if this should increment the catalog version. */
	if (is_syscatalog_change)
	{
		YBCPgSetIfIsSysCatalogVersionChange(ybc_stmt,
		                                    &is_syscatalog_version_change);
	}

	HandleYBStmtStatus(YBCPgSetCatalogCacheVersion(ybc_stmt,
		                                           yb_catalog_cache_version),
		               ybc_stmt);

	/* Execute the insert. */
	YBCStatus status = YBCPgDmlExecWriteOp(ybc_stmt);

	/*
	 * Optimization to increment the catalog version for the local cache as
	 * this backend is already aware of this change and should update its
	 * catalog caches accordingly (without needing to ask the master).
	 * Note that, since the master catalog version should have been identically
	 * incremented, it will continue to match with the local cache version if
	 * and only if no other master changes occurred in the meantime (i.e. from
	 * other backends).
	 * If changes occurred, then a cache refresh will be needed as usual.
	 */
	if (!status && is_syscatalog_version_change)
	{
		yb_catalog_cache_version += 1;
	}

	return status;
}

/*
 * Utility method to handle the status of an insert statement to return unique
 * constraint violation error message due to duplicate key in primary key or
 * unique index / constraint.
 */
static void YBCHandleInsertStatus(YBCStatus status, Relation rel, YBCPgStatement stmt)
{
	if (!status)
		return;

	HandleYBStatus(YBCPgDeleteStatement(stmt));

	if (YBCStatusIsAlreadyPresent(status))
	{
		char *constraint;

		/*
		 * If this is the base table and there is a primary key, the primary key is
		 * the constraint. Otherwise, the rel is the unique index constraint.
		 */
		if (!rel->rd_index && rel->rd_pkindex != InvalidOid)
		{
			Relation pkey = RelationIdGetRelation(rel->rd_pkindex);

			constraint = pstrdup(RelationGetRelationName(pkey));

			RelationClose(pkey);
		}
		else
		{
			constraint = pstrdup(RelationGetRelationName(rel));
		}

		YBCFreeStatus(status);
		ereport(ERROR,
				(errcode(ERRCODE_UNIQUE_VIOLATION),
				 errmsg("duplicate key value violates unique constraint \"%s\"",
						constraint)));
	}
	else
	{
		HandleYBStatus(status);
	}
}

/*
 * Utility method to insert a tuple into the relation's backing YugaByte table.
 */
static Oid YBCExecuteInsertInternal(Relation rel,
                                    TupleDesc tupleDesc,
                                    HeapTuple tuple,
                                    bool is_single_row_txn)
{
	Oid            dboid    = YBCGetDatabaseOid(rel);
	Oid            relid    = RelationGetRelid(rel);
	AttrNumber     minattr  = FirstLowInvalidHeapAttributeNumber + 1;
	int            natts    = RelationGetNumberOfAttributes(rel);
	Bitmapset      *pkey    = GetTablePrimaryKey(rel);
	YBCPgStatement insert_stmt = NULL;
	bool           is_null  = false;

	/* Generate a new oid for this row if needed */
	if (rel->rd_rel->relhasoids)
	{
		if (!OidIsValid(HeapTupleGetOid(tuple)))
			HeapTupleSetOid(tuple, GetNewOid(rel));
	}

	/* Create the INSERT request and add the values from the tuple. */
	HandleYBStatus(YBCPgNewInsert(ybc_pg_session,
	                              dboid,
	                              relid,
	                              is_single_row_txn,
	                              &insert_stmt));

	for (AttrNumber attnum = minattr; attnum <= natts; attnum++)
	{
		/* Skip virtual (system) and dropped columns */
		if (!IsRealYBColumn(rel, attnum))
		{
			continue;
		}

		Oid   type_id = GetTypeId(attnum, tupleDesc);
		Datum datum   = heap_getattr(tuple, attnum, tupleDesc, &is_null);

		/* Check not-null constraint on primary key early */
		if (is_null && bms_is_member(attnum - minattr, pkey))
		{
			HandleYBStatus(YBCPgDeleteStatement(insert_stmt));
			ereport(ERROR,
			        (errcode(ERRCODE_NOT_NULL_VIOLATION), errmsg(
					        "Missing/null value for primary key column")));
		}

		/* Add the column value to the insert request */
		YBCPgExpr ybc_expr = YBCNewConstant(insert_stmt, type_id, datum, is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(insert_stmt, attnum, ybc_expr),
		                   insert_stmt);
	}

	/*
	 * If the relation has indexes, request ybctid of the inserted row to update
	 * the indexes.
	 */
	if (rel->rd_rel->relhasindex)
	{
		YBCPgTypeAttrs type_attrs = {0};
		YBCPgExpr      ybc_expr   = YBCNewColumnRef(insert_stmt,
		                                            YBTupleIdAttributeNumber,
		                                            InvalidOid,
		                                            &type_attrs);
		HandleYBStmtStatus(YBCPgDmlAppendTarget(insert_stmt, ybc_expr), insert_stmt);
	}

	/* Execute the insert */
	YBCHandleInsertStatus(YBCExecWriteStmt(insert_stmt, rel), rel, insert_stmt);

	/*
	 * If the relation has indexes, save ybctid to insert the new row into the
	 * indexes.
	 */
	if (rel->rd_rel->relhasindex)
	{
		YBCPgSysColumns syscols;
		bool            has_data = false;

		HandleYBStmtStatus(YBCPgDmlFetch(insert_stmt,
		                                 0 /* natts */,
		                                 NULL /* values */,
		                                 NULL /* isnulls */,
		                                 &syscols,
		                                 &has_data),
		                   insert_stmt);
		if (has_data && syscols.ybctid != NULL)
		{
			tuple->t_ybctid = PointerGetDatum(syscols.ybctid);
		}
	}

	/* Clean up */
	HandleYBStatus(YBCPgDeleteStatement(insert_stmt));
	insert_stmt = NULL;

	return HeapTupleGetOid(tuple);
}

Oid YBCExecuteInsert(Relation rel,
                     TupleDesc tupleDesc,
                     HeapTuple tuple)
{
	return YBCExecuteInsertInternal(rel,
	                                tupleDesc,
	                                tuple,
	                                false /* is_single_row_txn */);
}

Oid YBCExecuteSingleRowTxnInsert(Relation rel,
                                 TupleDesc tupleDesc,
                                 HeapTuple tuple)
{
	return YBCExecuteInsertInternal(rel,
	                                tupleDesc,
	                                tuple,
	                                true /* is_single_row_txn */);
}

void YBCExecuteInsertIndex(Relation index, Datum *values, bool *isnull, Datum ybctid)
{
	Oid            dboid    = YBCGetDatabaseOid(index);
	Oid            relid    = RelationGetRelid(index);
	TupleDesc      tupdesc  = RelationGetDescr(index);
	int            natts    = RelationGetNumberOfAttributes(index);
	YBCPgStatement insert_stmt = NULL;

	Assert(index->rd_rel->relkind == RELKIND_INDEX);

	/* Create the INSERT request and add the values from the tuple. */
	HandleYBStatus(YBCPgNewInsert(ybc_pg_session,
	                              dboid,
	                              relid,
	                              false /* is_single_row_txn */,
	                              &insert_stmt));

	for (AttrNumber attnum = 1; attnum <= natts; attnum++)
	{
		Oid   type_id = GetTypeId(attnum, tupdesc);
		Datum value   = values[attnum - 1];
		bool  is_null = isnull[attnum - 1];

		/* Add the column value to the insert request */
		YBCPgExpr ybc_expr = YBCNewConstant(insert_stmt,
		                                    type_id,
		                                    value,
		                                    is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(insert_stmt, attnum, ybc_expr),
		                   insert_stmt);
	}

	/*
	 * Bind the ybctid from the base table to the ybbasectid column.
	 */
	Assert(ybctid != 0);
	YBCPgExpr ybc_expr = YBCNewConstant(insert_stmt, BYTEAOID, ybctid, false /* is_null */);
	HandleYBStmtStatus(YBCPgDmlBindColumn(insert_stmt, YBBaseTupleIdAttributeNumber, ybc_expr),
					   insert_stmt);

	/* Execute the insert and clean up. */
	YBCHandleInsertStatus(YBCExecWriteStmt(insert_stmt, index), index, insert_stmt);
	HandleYBStatus(YBCPgDeleteStatement(insert_stmt));
	insert_stmt = NULL;
}

void YBCExecuteDelete(Relation rel, TupleTableSlot *slot)
{
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	YBCPgStatement delete_stmt = NULL;

	/* Find ybctid value. Raise error if ybctid is not found. */
	Datum  ybctid = YBCGetYBTupleIdFromSlot(slot);
	if (ybctid == 0)
	{
		ereport(ERROR,
		        (errcode(ERRCODE_UNDEFINED_COLUMN), errmsg(
				        "Missing column ybctid in DELETE request to YugaByte database")));
	}

	/* Execute DELETE. */
	HandleYBStatus(YBCPgNewDelete(ybc_pg_session, dboid, relid, &delete_stmt));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(delete_stmt, BYTEAOID, ybctid, false);
	HandleYBStmtStatus(YBCPgDmlBindColumn(delete_stmt,
										  YBTupleIdAttributeNumber,
										  ybctid_expr), delete_stmt);
	HandleYBStmtStatus(YBCExecWriteStmt(delete_stmt, rel), delete_stmt);

	/* Complete execution */
	HandleYBStatus(YBCPgDeleteStatement(delete_stmt));
	delete_stmt = NULL;
}

void YBCExecuteDeleteIndex(Relation index, Datum *values, bool *isnull, Datum ybctid)
{
	Oid            dboid    = YBCGetDatabaseOid(index);
	Oid            relid    = RelationGetRelid(index);
	TupleDesc      tupdesc  = RelationGetDescr(index);
	int            natts    = RelationGetNumberOfAttributes(index);
	YBCPgStatement ybc_stmt = NULL;

	Assert(index->rd_rel->relkind == RELKIND_INDEX);

	/* Create the DELETE request and add the values from the tuple. */
	HandleYBStatus(YBCPgNewDelete(ybc_pg_session, dboid, relid, &ybc_stmt));
	for (AttrNumber attnum = 1; attnum <= natts; attnum++)
	{
		Oid   type_id = GetTypeId(attnum, tupdesc);
		Datum value   = values[attnum - 1];
		bool  is_null = isnull[attnum - 1];

		/* Add the column value to the delete request */
		YBCPgExpr ybc_expr = YBCNewConstant(ybc_stmt, type_id, value, is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(ybc_stmt, attnum, ybc_expr), ybc_stmt);
	}

	if (!index->rd_index->indisunique)
	{
		/*
		 * Bind the ybctid from the base table to the ybbasectid column.
		 */
		if (ybctid == 0)
		{
			YBC_LOG_WARNING("Skipping index deletion in %s", RelationGetRelationName(index));

			HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
			return;
		}

		YBCPgExpr ybc_expr = YBCNewConstant(ybc_stmt, BYTEAOID, ybctid, false /* is_null */);
		HandleYBStmtStatus(YBCPgDmlBindColumn(ybc_stmt, YBBaseTupleIdAttributeNumber, ybc_expr),
						   ybc_stmt);
	}

	/* Execute the delete and clean up. */
	HandleYBStmtStatus(YBCExecWriteStmt(ybc_stmt, index), ybc_stmt);

	HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
	ybc_stmt = NULL;
}

void YBCExecuteUpdate(Relation rel, TupleTableSlot *slot, HeapTuple tuple)
{
	TupleDesc      tupleDesc   = slot->tts_tupleDescriptor;
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	YBCPgStatement update_stmt = NULL;

	/* Look for ybctid. Raise error if ybctid is not found. */
	Datum ybctid = YBCGetYBTupleIdFromSlot(slot);
	if (ybctid == 0)
	{
		ereport(ERROR,
		        (errcode(ERRCODE_UNDEFINED_COLUMN), errmsg(
				        "Missing column ybctid in UPDATE request to YugaByte database")));
	}

	/* Create update statement. */
	HandleYBStatus(YBCPgNewUpdate(ybc_pg_session, dboid, relid, &update_stmt));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(update_stmt, BYTEAOID, ybctid, false);
	HandleYBStmtStatus(YBCPgDmlBindColumn(update_stmt,
	                                      YBTupleIdAttributeNumber,
	                                      ybctid_expr), update_stmt);

	/* Assign new values to columns for updating the current row. */
	tupleDesc = RelationGetDescr(rel);
	for (int idx = 0; idx < tupleDesc->natts; idx++)
	{
		AttrNumber attnum = TupleDescAttr(tupleDesc, idx)->attnum;

		bool is_null = false;
		Datum d = heap_getattr(tuple, attnum, tupleDesc, &is_null);
		YBCPgExpr ybc_expr = YBCNewConstant(update_stmt, TupleDescAttr(tupleDesc, idx)->atttypid,
		                                    d, is_null);
		HandleYBStmtStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr), update_stmt);
	}

	/* Execute the statement and clean up */
	HandleYBStmtStatus(YBCExecWriteStmt(update_stmt, rel), update_stmt);
	HandleYBStatus(YBCPgDeleteStatement(update_stmt));
	update_stmt = NULL;

	/*
	 * If the relation has indexes, save the ybctid to insert the updated row into the indexes.
	 */
	if (rel->rd_rel->relhasindex)
	{
		tuple->t_ybctid = ybctid;
	}
}

void YBCDeleteSysCatalogTuple(Relation rel, HeapTuple tuple)
{
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	TupleDesc      tupdesc     = RelationGetDescr(rel);
	YBCPgStatement delete_stmt = NULL;

	Bitmapset *pkey = YBSysTablePrimaryKey(RelationGetRelid(rel));

	/* If we don't have a primary key for the sys catalog table, we use
 	* the ybctid.
 	*/
	if (!pkey) {
		pkey = bms_add_member(pkey, YBTupleIdAttributeNumber
							  - FirstLowInvalidHeapAttributeNumber);
	}

	// Execute DELETE.
	HandleYBStatus(YBCPgNewDelete(ybc_pg_session, dboid, relid, &delete_stmt));

	int col = -1;
	bool is_null = false;
	while ((col = bms_next_member(pkey, col)) >= 0)
	{
		AttrNumber attno = col + FirstLowInvalidHeapAttributeNumber;
		Oid        typid = OIDOID; // default;

		if (attno > 0)
			typid = TupleDescAttr(tupdesc, attno - 1)->atttypid;
		else if (attno == YBTupleIdAttributeNumber)
			typid = BYTEAOID;
		else if (attno == ObjectIdAttributeNumber)
			typid = OIDOID;
		else
			elog(ERROR, "Invalid primary key for table \"%s\"", RelationGetRelationName(rel));

		Datum      val   = heap_getattr(tuple, attno, tupdesc, &is_null);
		YBCPgExpr  expr  = YBCNewConstant(delete_stmt, typid, val, is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(delete_stmt, attno, expr),
		                   delete_stmt);
	}

	bms_free(pkey);

	/*
	 * Mark tuple for invalidation from system caches at next command
	 * boundary. Do this now so if there is an error with delete we will
	 * re-query to get the correct state from the master.
	 */
	CacheInvalidateHeapTuple(rel, tuple, NULL);

	HandleYBStmtStatus(YBCExecWriteStmt(delete_stmt, rel), delete_stmt);

	/* Complete execution */
	HandleYBStatus(YBCPgDeleteStatement(delete_stmt));
	delete_stmt = NULL;
}

void YBCUpdateSysCatalogTuple(Relation rel, HeapTuple oldtuple, HeapTuple tuple)
{
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	TupleDesc      tupleDesc   = RelationGetDescr(rel);
	YBCPgStatement update_stmt = NULL;

	/* Create update statement. */
	HandleYBStatus(YBCPgNewUpdate(ybc_pg_session, dboid, relid, &update_stmt));

	AttrNumber minattr = FirstLowInvalidHeapAttributeNumber + 1;
	int        natts   = RelationGetNumberOfAttributes(rel);
	Bitmapset  *pkey   = GetTablePrimaryKey(rel);

	for (AttrNumber attnum = minattr; attnum <= natts; attnum++)
	{
		/* Skip non-primary-key columns */
		if (!bms_is_member(attnum - minattr, pkey))
		{
			continue;
		}

		Oid   type_id = GetTypeId(attnum, tupleDesc);
		Datum datum   = 0;
		bool  is_null = false;

		datum = heap_getattr(tuple, attnum, tupleDesc, &is_null);

		/* Check not-null constraint on primary key early */
		if (is_null)
		{
			HandleYBStatus(YBCPgDeleteStatement(update_stmt));
			ereport(ERROR,
					(errcode(ERRCODE_NOT_NULL_VIOLATION), errmsg(
							"Missing/null value for primary key column")));
		}

		/* Add the column value to the update request */
		YBCPgExpr ybc_expr = YBCNewConstant(update_stmt, type_id, datum, is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(update_stmt, attnum, ybc_expr), update_stmt);
	}

	/* Assign new values to columns for updating the current row. */
	int idx;
	for (idx = 0; idx < tupleDesc->natts; idx++) {
		AttrNumber attnum = TupleDescAttr(tupleDesc, idx)->attnum;

		bool is_null = false;
		Datum d = heap_getattr(tuple, attnum, tupleDesc, &is_null);
		YBCPgExpr ybc_expr = YBCNewConstant(update_stmt, TupleDescAttr(tupleDesc, idx)->atttypid,
											d, is_null);
		HandleYBStmtStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr), update_stmt);
	}

	/*
	 * Mark old tuple for invalidation from system caches at next command
	 * boundary, and mark the new tuple for invalidation in case we abort.
	 * In case when there is no old tuple, we will invalidate with the
	 * new tuple at next command boundary instead. Do these now so if there
	 * is an error with update we will re-query to get the correct state
	 * from the master.
	 */
	if (oldtuple)
		CacheInvalidateHeapTuple(rel, oldtuple, tuple);
	else
		CacheInvalidateHeapTuple(rel, tuple, NULL);

	/* Execute the statement and clean up */
	HandleYBStmtStatus(YBCExecWriteStmt(update_stmt, rel), update_stmt);
	HandleYBStatus(YBCPgDeleteStatement(update_stmt));
	update_stmt = NULL;
}

void YBCStartBufferingWriteOperations()
{
	HandleYBStatus(YBCPgStartBufferingWriteOperations(ybc_pg_session));
}

void YBCFlushBufferedWriteOperations()
{
	HandleYBStatus(YBCPgFlushBufferedWriteOperations(ybc_pg_session));
}
