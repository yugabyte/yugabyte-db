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

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

/*
 * Returns whether a relation's attribute is a real column in the backing
 * YugaByte table. (It implies we can both read from and write to it).
 */
bool IsRealYBColumn(Relation rel, int attrNum)
{
	return attrNum > 0 ||
	       (rel->rd_rel->relhasoids && attrNum == ObjectIdAttributeNumber);
}

/*
 * Get the type ID of a real or virtual attribute (column).
 * Returns InvalidOid if the attribute number is invalid.
 */
Oid GetTypeId(int attrNum, TupleDesc tupleDesc)
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
				return tupleDesc->attrs[attrNum - 1]->atttypid;
			else
				return InvalidOid;
	}
}

/*
 * Insert a tuple into the a relation's backing YugaByte table.
 */
Oid YBCExecuteInsert(Relation rel, TupleDesc tupleDesc, HeapTuple tuple)
{
	Oid            dboid         = YBCGetDatabaseOid(rel);
	Oid            relid         = RelationGetRelid(rel);
	AttrNumber     minattr       = FirstLowInvalidHeapAttributeNumber + 1;
	int            natts         = RelationGetNumberOfAttributes(rel);
	Bitmapset      *pkey         = NULL;
	YBCPgStatement ybc_stmt      = NULL;
	YBCPgTableDesc ybc_tabledesc = NULL;

	/* Generate a new oid for this row if needed */
	if (rel->rd_rel->relhasoids)
	{
		if (!OidIsValid(HeapTupleGetOid(tuple)))
			HeapTupleSetOid(tuple, GetNewOid(rel));
	}

	/*
	 * Get the primary key columns 'pkey' from YugaByte. Used below to
	 * check that values for all primary key columns are given (not null)
	 */
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
	ybc_tabledesc = NULL;

	/* Create the INSERT request and add the values from the tuple. */
	HandleYBStatus(YBCPgNewInsert(ybc_pg_session, dboid, relid, &ybc_stmt));
	bool is_null = false;
	for (AttrNumber attnum  = minattr; attnum <= natts; attnum++)
	{
		/* Skip virtual (system) columns */
		if (!IsRealYBColumn(rel, attnum))
		{
			continue;
		}

		Oid   type_id = GetTypeId(attnum, tupleDesc);
		Datum datum   = heap_getattr(tuple, attnum, tupleDesc, &is_null);

		/* Check not-null constraint on primary key early */
		if (is_null && bms_is_member(attnum - minattr, pkey))
		{
			HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
			ereport(ERROR,
			        (errcode(ERRCODE_NOT_NULL_VIOLATION), errmsg(
					        "Missing/null value for primary key column")));
		}

		/* Add the column value to the insert request */
		YBCPgExpr ybc_expr = YBCNewConstant(ybc_stmt, type_id, datum, is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(ybc_stmt, attnum, ybc_expr),
		                   ybc_stmt);
	}

	/* Execute the insert and clean up. */
	HandleYBStmtStatus(YBCPgExecInsert(ybc_stmt), ybc_stmt);
	HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
	ybc_stmt = NULL;

	return HeapTupleGetOid(tuple);
}

void YBCExecuteDelete(Relation rel, ResultRelInfo *resultRelInfo, TupleTableSlot *slot)
{
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	YBCPgStatement delete_stmt = NULL;

	// Find ybctid value.
	int               idx;
	Datum             ybctid   = 0;
	Form_pg_attribute *attrs   = slot->tts_tupleDescriptor->attrs;
	for (idx = 0; idx < slot->tts_nvalid; idx++)
	{
		if (strcmp(NameStr(attrs[idx]->attname), "ybctid") == 0 &&
		    !slot->tts_isnull[idx])
		{
			Assert(attrs[idx]->atttypid == BYTEAOID);
			ybctid = slot->tts_values[idx];
		}
	}

	// Raise error if ybctid is not found.
	if (ybctid == 0)
	{
		ereport(ERROR,
		        (errcode(ERRCODE_UNDEFINED_COLUMN), errmsg(
				        "Missing column ybctid in DELETE request to YugaByte database")));
	}

	// Execute DELETE.
	HandleYBStatus(YBCPgNewDelete(ybc_pg_session, dboid, relid, &delete_stmt));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(delete_stmt,
										   BYTEAOID,
										   ybctid,
										   false);
	HandleYBStmtStatus(YBCPgDmlBindColumn(delete_stmt,
										  YBTupleIdAttributeNumber,
										  ybctid_expr), delete_stmt);
	HandleYBStmtStatus(YBCPgExecDelete(delete_stmt), delete_stmt);

	/* Complete execution */
	HandleYBStatus(YBCPgDeleteStatement(delete_stmt));
	delete_stmt = NULL;
}

void YBCDeleteSysCatalogTuple(Relation rel, HeapTuple tuple, Bitmapset *pkey)
{
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	TupleDesc      tupdesc     = RelationGetDescr(rel);
	YBCPgStatement delete_stmt = NULL;

	// Execute DELETE.
	HandleYBStatus(YBCPgNewDelete(ybc_pg_session, dboid, relid, &delete_stmt));

	int col = -1;
	bool is_null = false;
	while ((col = bms_next_member(pkey, col)) >= 0)
	{
		AttrNumber attno = col + FirstLowInvalidHeapAttributeNumber;
		Oid        typid = OIDOID; // default;
		if (attno > 0)
			typid = tupdesc->attrs[attno - 1]->atttypid;
		Datum      val   = heap_getattr(tuple, attno, tupdesc, &is_null);
		YBCPgExpr  expr  = YBCNewConstant(delete_stmt, typid, val, is_null);
		HandleYBStmtStatus(YBCPgDmlBindColumn(delete_stmt, attno, expr),
		                   delete_stmt);
	}

	/*
	 * Invalidate the cache now so if there is an error with delete we will
	 * re-query to get the correct state from the master.
	 */
	CacheInvalidateHeapTuple(rel, tuple, NULL);

	HandleYBStmtStatus(YBCPgExecDelete(delete_stmt), delete_stmt);

	/* Complete execution */
	HandleYBStatus(YBCPgDeleteStatement(delete_stmt));
	delete_stmt = NULL;
}

void YBCExecuteUpdate(Relation rel,
					  ResultRelInfo *resultRelInfo,
					  TupleTableSlot *slot,
					  HeapTuple tuple)
{
	TupleDesc      tupleDesc   = slot->tts_tupleDescriptor;
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	YBCPgStatement update_stmt = NULL;

	/* Look for ybctid. */
	int idx;
	Datum ybctid = 0;
	Form_pg_attribute *attrs = slot->tts_tupleDescriptor->attrs;
	for (idx = 0; idx < slot->tts_nvalid; idx++)
	{
		if (strcmp(NameStr(attrs[idx]->attname), "ybctid") == 0 &&	!slot->tts_isnull[idx])
		{
			Assert(attrs[idx]->atttypid == BYTEAOID);
			ybctid = slot->tts_values[idx];
		}
	}

	/* Raise error if ybctid is not found. */
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
	Form_pg_attribute *table_attrs = rel->rd_att->attrs;
	for (idx = 0; idx < rel->rd_att->natts; idx++) {
		AttrNumber attnum = table_attrs[idx]->attnum;

		bool is_null = false;
		Datum d = heap_getattr(tuple, attnum, tupleDesc, &is_null);
		YBCPgExpr ybc_expr = YBCNewConstant(update_stmt, table_attrs[idx]->atttypid, d, is_null);
		HandleYBStmtStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr), update_stmt);
	}

	/* Execute the statement */
	HandleYBStmtStatus(YBCPgExecUpdate(update_stmt), update_stmt);
	HandleYBStatus(YBCPgDeleteStatement(update_stmt));
	update_stmt = NULL;
}
