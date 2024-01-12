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
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_auth_members_d.h"
#include "catalog/pg_tablespace_d.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_role_profile.h"
#include "catalog/pg_yb_role_profile_d.h"
#include "catalog/yb_type.h"
#include "commands/yb_profile.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "nodes/execnodes.h"
#include "nodes/nodeFuncs.h"
#include "commands/dbcommands.h"
#include "executor/executor.h"
#include "executor/tuptable.h"
#include "executor/ybcExpr.h"
#include "executor/ybcModifyTable.h"
#include "miscadmin.h"
#include "catalog/catalog.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_database.h"
#include "catalog/yb_catalog_version.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/relcache.h"
#include "executor/tuptable.h"
#include "executor/ybcExpr.h"
#include "optimizer/ybcplan.h"

#include "utils/syscache.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "access/yb_scan.h"

#include <execinfo.h>

/* Yugabyte includes */
#include "utils/builtins.h"

bool yb_disable_transactional_writes = false;
bool yb_enable_upsert_mode = false;

/*
 * Hack to ensure that the next CommandCounterIncrement() will call
 * CommandEndInvalidationMessages(). The result of this call is not
 * needed on the yb side, however the side effects are.
 */
void MarkCurrentCommandUsed() {
	(void) GetCurrentCommandId(true);
}

/*
 * Returns whether relation is capable of single row execution.
 */
bool YBCIsSingleRowTxnCapableRel(ResultRelInfo *resultRelInfo)
{
	bool has_triggers = resultRelInfo->ri_TrigDesc && resultRelInfo->ri_TrigDesc->numtriggers > 0;
	bool has_indices = YBCRelInfoHasSecondaryIndices(resultRelInfo);
	return !has_indices && !has_triggers;
}

/*
 * Get the ybctid from a YB scan slot for UPDATE/DELETE.
 */
Datum YBCGetYBTupleIdFromSlot(TupleTableSlot *slot)
{
	/*
	 * Look for ybctid in the slot's item pointer field first.
	 * Otherwise, look for it in the attribute list as a junk attribute.
	 */
	if (TABLETUPLE_YBCTID(slot) != 0)
	{
		return TABLETUPLE_YBCTID(slot);
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
 * Get the ybctid from a tuple.
 *
 * Note that if the relation has a DocDB RowId attribute, this will generate a new RowId value
 * meaning the ybctid will be unique. Therefore you should only use this if the relation has
 * a primary key or you're doing an insert.
 */
Datum YBCGetYBTupleIdFromTuple(Relation rel,
							   HeapTuple tuple,
							   TupleDesc tupleDesc) {
	Oid dboid = YBCGetDatabaseOid(rel);
	YBCPgTableDesc ybc_table_desc = NULL;
	HandleYBStatus(YBCPgGetTableDesc(dboid, YbGetStorageRelid(rel), &ybc_table_desc));
	Bitmapset *pkey    = YBGetTableFullPrimaryKeyBms(rel);
	AttrNumber minattr = YBSystemFirstLowInvalidAttributeNumber + 1;
	YBCPgYBTupleIdDescriptor *descr = YBCCreateYBTupleIdDescriptor(
		dboid, YbGetStorageRelid(rel), bms_num_members(pkey));
	YBCPgAttrValueDescriptor *next_attr = descr->attrs;
	int col = -1;
	while ((col = bms_next_member(pkey, col)) >= 0) {
		AttrNumber attnum = col + minattr;
		next_attr->attr_num = attnum;
		/*
		 * Don't need to fill in for the DocDB RowId column, however we still
		 * need to add the column to the statement to construct the ybctid.
		 */
		if (attnum != YBRowIdAttributeNumber) {
			Oid	type_id = (attnum > 0) ?
					TupleDescAttr(tupleDesc, attnum - 1)->atttypid : InvalidOid;

			next_attr->type_entity = YbDataTypeFromOidMod(attnum, type_id);
			next_attr->collation_id = ybc_get_attcollation(RelationGetDescr(rel), attnum);
			next_attr->datum = heap_getattr(tuple, attnum, tupleDesc, &next_attr->is_null);
		} else {
			next_attr->datum = 0;
			next_attr->is_null = false;
			next_attr->type_entity = NULL;
			next_attr->collation_id = InvalidOid;
		}
		YBCPgColumnInfo column_info = {0};
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
												   attnum,
												   &column_info), ybc_table_desc);
		YBSetupAttrCollationInfo(next_attr, &column_info);
		++next_attr;
	}
	bms_free(pkey);
	uint64_t tuple_id = 0;
	HandleYBStatus(YBCPgBuildYBTupleId(descr, &tuple_id));
	pfree(descr);
	return (Datum)tuple_id;
}

/*
 * Bind ybctid to the statement.
 */
static void YBCBindTupleId(YBCPgStatement pg_stmt, Datum tuple_id) {
	YBCPgExpr ybc_expr = YBCNewConstant(pg_stmt, BYTEAOID, InvalidOid, tuple_id,
										false /* is_null */);
	HandleYBStatus(YBCPgDmlBindColumn(pg_stmt, YBTupleIdAttributeNumber, ybc_expr));
}

/*
 * Utility method to execute a prepared write statement.
 * Will handle the case if the write changes the system catalogs meaning
 * we need to increment the catalog versions accordingly.
 */
static void YBCExecWriteStmt(YBCPgStatement ybc_stmt,
							 Relation rel,
							 int *rows_affected_count)
{
	YbSetCatalogCacheVersion(ybc_stmt, YbGetCatalogCacheVersion());

	bool is_syscatalog_version_inc = YbMarkStatementIfCatalogVersionIncrement(ybc_stmt, rel);

	/* Execute the insert. */
	HandleYBStatus(YBCPgDmlExecWriteOp(ybc_stmt, rows_affected_count));

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
	if (is_syscatalog_version_inc)
	{
		// TODO(shane) also update the shared memory catalog version here.
		YbUpdateCatalogCacheVersion(YbGetCatalogCacheVersion() + 1);
	}

	if (YBIsDBCatalogVersionMode() &&
		RelationGetForm(rel)->relisshared &&
		RelationSupportsSysCache(RelationGetRelid(rel)) &&
		!(*YBCGetGFlags()->ysql_disable_global_impact_ddl_statements))
	{
		/* NOTE: relisshared implies that rel is a system relation. */
		Assert(IsSystemRelation(rel));
		Assert(/* pg_authid */
			   RelationGetRelid(rel) == AuthIdRelationId ||
			   RelationGetRelid(rel) == AuthIdRolnameIndexId ||

			   /* pg_auth_members */
			   RelationGetRelid(rel) == AuthMemRelationId ||
			   RelationGetRelid(rel) == AuthMemRoleMemIndexId ||
			   RelationGetRelid(rel) == AuthMemMemRoleIndexId ||

			   /* pg_database */
			   RelationGetRelid(rel) == DatabaseRelationId ||

			   /* pg_tablespace */
			   RelationGetRelid(rel) == TableSpaceRelationId);
		YbSetIsGlobalDDL();
	}
}

/*
 * Utility method to insert a tuple into the relation's backing YugaByte table.
 */
static void YBCExecuteInsertInternal(Oid dboid,
									 Relation rel,
									 TupleDesc tupleDesc,
									 HeapTuple tuple,
									 bool is_single_row_txn,
									 OnConflictAction onConflictAction,
									 Datum *ybctid)
{
	Oid            relid    = RelationGetRelid(rel);
	AttrNumber     minattr  = YBGetFirstLowInvalidAttributeNumber(rel);
	int            natts    = RelationGetNumberOfAttributes(rel);
	Bitmapset      *pkey    = YBGetTablePrimaryKeyBms(rel);
	YBCPgStatement insert_stmt = NULL;
	bool           is_null  = false;

	/* Create the INSERT request and add the values from the tuple. */
	HandleYBStatus(YBCPgNewInsert(dboid,
	                              YbGetStorageRelid(rel),
	                              is_single_row_txn,
	                              YBCIsRegionLocal(rel),
	                              &insert_stmt));

	/* Get the ybctid for the tuple and bind to statement */
	HEAPTUPLE_YBCTID(tuple) =
		ybctid != NULL && *ybctid != 0 ? *ybctid
		                               : YBCGetYBTupleIdFromTuple(rel, tuple, tupleDesc);
	YBCBindTupleId(insert_stmt, HEAPTUPLE_YBCTID(tuple));

	if (ybctid != NULL)
	{
		*ybctid = HEAPTUPLE_YBCTID(tuple);
	}

	for (AttrNumber attnum = minattr; attnum <= natts; attnum++)
	{
		/* Skip virtual (system) and dropped columns */
		if (!IsRealYBColumn(rel, attnum))
		{
			continue;
		}

		Oid   type_id = GetTypeId(attnum, tupleDesc);
		/*
		 * Postgres does not populate the column collation in tupleDesc but
		 * we must use the column collation in order to correctly compute the
		 * collation sortkey which later can be stored in DocDB. For example,
		 *   create table foo(id text collate "en-US-x-icu" primary key);
		 *   insert into foo values (1024);
		 *   insert into foo values ('2048' collate "C");
		 * Postgres will convert the integer 1024 to a text constant '1024'
		 * with the default collation. The text constant '2048' will retain
		 * its explicit collate "C". In both cases, in order to correctly
		 * compute collation sortkey, we must use the column collation
		 * "en-US-x-icu". When those two text constants are stored in the
		 * column, they will have the column collation when read out later.
		 * Postgres could have also converted both collations to the column
		 * collation but it appears that collation is not part of a type.
		 */
		Oid   collation_id = YBEncodingCollation(insert_stmt, attnum,
			ybc_get_attcollation(RelationGetDescr(rel), attnum));
		Datum datum   = heap_getattr(tuple, attnum, tupleDesc, &is_null);

		/* Check not-null constraint on primary key early */
		if (is_null && bms_is_member(attnum - minattr, pkey))
		{
			ereport(ERROR,
			        (errcode(ERRCODE_NOT_NULL_VIOLATION), errmsg(
					        "Missing/null value for primary key column")));
		}

		/* Add the column value to the insert request */
		YBCPgExpr ybc_expr = YBCNewConstant(insert_stmt, type_id, collation_id, datum, is_null);
		HandleYBStatus(YBCPgDmlBindColumn(insert_stmt, attnum, ybc_expr));
	}

	/*
	 * For system tables, mark tuple for invalidation from system caches
	 * at next command boundary.
	 *
	 * Do this now so if there is an error with insert we will re-query to get
	 * the correct state from the master.
	 */
	if (IsCatalogRelation(rel))
	{
		MarkCurrentCommandUsed();
		CacheInvalidateHeapTuple(rel, tuple, NULL);
	}

	if (onConflictAction == ONCONFLICT_YB_REPLACE || yb_enable_upsert_mode)
	{
		HandleYBStatus(YBCPgInsertStmtSetUpsertMode(insert_stmt));
	}

	/* Execute the insert */
	YBCExecWriteStmt(insert_stmt, rel, NULL /* rows_affected_count */);

	/* Cleanup. */
	YBCPgDeleteStatement(insert_stmt);
	/* Add row into foreign key cache */
	if (!is_single_row_txn)
		YBCPgAddIntoForeignKeyReferenceCache(relid, HEAPTUPLE_YBCTID(tuple));

	bms_free(pkey);
}

void YBCExecuteInsert(Relation rel,
					  TupleDesc tupleDesc,
					  HeapTuple tuple,
					  OnConflictAction onConflictAction)
{
	YBCExecuteInsertForDb(YBCGetDatabaseOid(rel),
						  rel,
						  tupleDesc,
						  tuple,
						  onConflictAction,
						  NULL /* ybctid */);
}

void YBCExecuteInsertForDb(Oid dboid,
						   Relation rel,
						   TupleDesc tupleDesc,
						   HeapTuple tuple,
						   OnConflictAction onConflictAction,
						   Datum *ybctid)
{
	bool non_transactional = !IsSystemRelation(rel) && yb_disable_transactional_writes;
	YBCExecuteInsertInternal(dboid,
							 rel,
							 tupleDesc,
							 tuple,
							 non_transactional,
							 onConflictAction,
							 ybctid);
}

void YBCExecuteNonTxnInsert(Relation rel,
							TupleDesc tupleDesc,
							HeapTuple tuple,
							OnConflictAction onConflictAction)
{
	YBCExecuteNonTxnInsertForDb(YBCGetDatabaseOid(rel),
								rel,
								tupleDesc,
								tuple,
								onConflictAction,
								NULL /* ybctid */);
}

void YBCExecuteNonTxnInsertForDb(Oid dboid,
								 Relation rel,
								 TupleDesc tupleDesc,
								 HeapTuple tuple,
								 OnConflictAction onConflictAction,
								 Datum *ybctid)
{
	YBCExecuteInsertInternal(dboid,
							 rel,
							 tupleDesc,
							 tuple,
							 true /* is_single_row_txn */,
							 onConflictAction,
							 ybctid);
}

/* YB_REVIEW(neil) Revisit later. */
/* YB_TODO: Is there a better way to do multi tuple insert? */
void
YBCTupleTableMultiInsert(ResultRelInfo *resultRelInfo, TupleTableSlot **slots,
						 int num, EState *estate)
{
	for (int i = 0; i < num; i++)
		YBCTupleTableInsert(resultRelInfo, slots[i], estate);
}

void
YBCTupleTableInsert(ResultRelInfo *resultRelInfo, TupleTableSlot *slot,
					EState *estate)
{
	bool	  shouldFree = true;
	HeapTuple tuple = ExecFetchSlotHeapTuple(slot, true, &shouldFree);

	/* Update the tuple with table oid */
	slot->tts_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);
	tuple->t_tableOid = slot->tts_tableOid;

	/* Perform the insertion, and copy the resulting ItemPointer */
	YBCHeapInsert(resultRelInfo, slot, tuple, estate);
	ItemPointerCopy(&tuple->t_self, &slot->tts_tid);

	if (shouldFree)
		pfree(tuple);
}

void YBCHeapInsert(ResultRelInfo *resultRelInfo,
				   TupleTableSlot *slot,
				   HeapTuple tuple,
				   EState *estate)
{
	Oid dboid = YBCGetDatabaseOid(resultRelInfo->ri_RelationDesc);
	YBCHeapInsertForDb(resultRelInfo, dboid, slot, tuple, estate, NULL /* ybctid */);
}

void YBCHeapInsertForDb(ResultRelInfo *resultRelInfo,
						Oid dboid,
						TupleTableSlot* slot,
						HeapTuple tuple,
						EState* estate,
						Datum* ybctid)
{
	/*
	 * get information on the (current) result relation
	 */
	Relation resultRelationDesc = resultRelInfo->ri_RelationDesc;

	if (estate->yb_es_is_single_row_modify_txn)
	{
		/*
		 * Try to execute the statement as a single row transaction (rather
		 * than a distributed transaction) if it is safe to do so.
		 * I.e. if we are in a single-statement transaction that targets a
		 * single row (i.e. single-row-modify txn), and there are no indices
		 * or triggers on the target table.
		 */
		YBCExecuteNonTxnInsertForDb(dboid,
									resultRelationDesc,
									slot->tts_tupleDescriptor,
									tuple,
									ONCONFLICT_NONE,
									ybctid);
	}
	else
	{
		YBCExecuteInsertForDb(dboid,
							  resultRelationDesc,
							  slot->tts_tupleDescriptor,
							  tuple,
							  ONCONFLICT_NONE,
							  ybctid);
	}
}

static YBCPgYBTupleIdDescriptor*
YBCBuildNonNullUniqueIndexYBTupleId(Relation unique_index, Datum *values)
{
	Oid dboid = YBCGetDatabaseOid(unique_index);
	Oid relid = RelationGetRelid(unique_index);
	YBCPgTableDesc ybc_table_desc = NULL;
	HandleYBStatus(YBCPgGetTableDesc(dboid, relid, &ybc_table_desc));
	TupleDesc tupdesc = RelationGetDescr(unique_index);
	const int nattrs = IndexRelationGetNumberOfKeyAttributes(unique_index);
	YBCPgYBTupleIdDescriptor* result = YBCCreateYBTupleIdDescriptor(dboid, relid, nattrs + 1);
	YBCPgAttrValueDescriptor *next_attr = result->attrs;
	for (AttrNumber attnum = 1; attnum <= nattrs; ++attnum)
	{
		Oid type_id = GetTypeId(attnum, tupdesc);
		next_attr->type_entity = YbDataTypeFromOidMod(attnum, type_id);
		next_attr->collation_id = ybc_get_attcollation(tupdesc, attnum);
		next_attr->attr_num = attnum;
		next_attr->datum = values[attnum - 1];
		next_attr->is_null = false;
		YBCPgColumnInfo column_info = {0};
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
												   attnum,
												   &column_info), ybc_table_desc);
		YBSetupAttrCollationInfo(next_attr, &column_info);
		++next_attr;
	}
	YBCFillUniqueIndexNullAttribute(result);
	return result;
}

static void
YBCForeignKeyReferenceCacheDeleteIndex(Relation index, Datum *values, bool *isnulls)
{
	Assert(index->rd_rel->relkind == RELKIND_INDEX);
	/* Only unique index can be used in foreign key constraints */
	if (!index->rd_index->indisprimary && index->rd_index->indisunique)
	{
		const int nattrs = IndexRelationGetNumberOfKeyAttributes(index);
		/*
		 * Index with at least one NULL value can't be referenced by foreign key constraint,
		 * and can't be stored in cache, so ignore it.
		 */
		for (int i = 0; i < nattrs; ++i)
			if (isnulls[i])
				return;

		YBCPgYBTupleIdDescriptor* descr = YBCBuildNonNullUniqueIndexYBTupleId(index, values);
		HandleYBStatus(YBCPgForeignKeyReferenceCacheDelete(descr));
		pfree(descr);
	}
}

void YBCExecuteInsertIndex(Relation index,
						   Datum *values,
						   bool *isnull,
						   ItemPointer tid,
						   const uint64_t *backfill_write_time,
						   yb_bind_for_write_function callback,
						   void *indexstate)
{
	YBCExecuteInsertIndexForDb(YBCGetDatabaseOid(index),
							   index,
							   values,
							   isnull,
							   tid,
							   backfill_write_time,
							   callback,
							   indexstate);
}

void YBCExecuteInsertIndexForDb(Oid dboid,
								Relation index,
								Datum* values,
								bool* isnull,
								ItemPointer tid,
								const uint64_t* backfill_write_time,
								yb_bind_for_write_function callback,
								void *indexstate)
{
	Assert(index->rd_rel->relkind == RELKIND_INDEX);
	Assert(tid != 0 && YbItemPointerYbctid(tid));

	Oid            relid    = RelationGetRelid(index);
	YBCPgStatement insert_stmt = NULL;

	/* Create the INSERT request and add the values from the tuple. */
	/*
	 * TODO(jason): rename `is_single_row_txn` to something like
	 * `non_distributed_txn` when closing issue #4906.
	 */
	const bool is_backfill = (backfill_write_time != NULL);
	const bool is_non_distributed_txn_write =
		is_backfill || (!IsSystemRelation(index) && yb_disable_transactional_writes);
	HandleYBStatus(YBCPgNewInsert(dboid,
								  relid,
								  is_non_distributed_txn_write,
								  YBCIsRegionLocal(index),
								  &insert_stmt));

	callback(insert_stmt, indexstate, index, values, isnull,
			 RelationGetNumberOfAttributes(index),
			 YbItemPointerYbctid(tid), true /* ybctid_as_value */);

	/*
	 * For non-unique indexes the primary-key component (base tuple id) already
	 * guarantees uniqueness, so no need to read and check it in DocDB.
	 */
	if (!index->rd_index->indisunique) {
		HandleYBStatus(YBCPgInsertStmtSetUpsertMode(insert_stmt));
	}

	if (is_backfill)
	{
		HandleYBStatus(YBCPgInsertStmtSetIsBackfill(insert_stmt,
													true /* is_backfill */));
		/*
		 * For index backfill, set write hybrid time to a time in the past.
		 * This is to guarantee that backfilled writes are temporally before
		 * any online writes.
		 */
		HandleYBStatus(YBCPgInsertStmtSetWriteTime(insert_stmt,
												   *backfill_write_time));
	}

	/* Execute the insert and clean up. */
	YBCExecWriteStmt(insert_stmt,
					 index,
					 NULL /* rows_affected_count */);

	/* Cleanup. */
	YBCPgDeleteStatement(insert_stmt);
}

bool YBCExecuteDelete(Relation rel,
					  TupleTableSlot *planSlot,
					  List *returning_columns,
					  bool target_tuple_fetched,
					  bool is_single_row_txn,
					  bool changingPart,
					  EState *estate)
{
	TupleDesc		tupleDesc = RelationGetDescr(rel);
	Oid				dboid = YBCGetDatabaseOid(rel);
	Oid				relid = RelationGetRelid(rel);
	YBCPgStatement	delete_stmt = NULL;
	Datum			ybctid;

	/* is_single_row_txn always implies target tuple wasn't fetched. */
	Assert(!is_single_row_txn || !target_tuple_fetched);

	/* Create DELETE request. */
	HandleYBStatus(YBCPgNewDelete(dboid,
								  YbGetStorageRelid(rel),
								  is_single_row_txn,
								  YBCIsRegionLocal(rel),
								  &delete_stmt));

	/*
	 * Look for ybctid. Raise error if ybctid is not found.
	 *
	 * Retrieve ybctid from the slot if possible, otherwise generate it
	 * from tuple values.
	 */
	if (target_tuple_fetched)
		ybctid = YBCGetYBTupleIdFromSlot(planSlot);
	else
	{
		bool shouldFree = true;
		HeapTuple tuple = ExecFetchSlotHeapTuple(planSlot, true, &shouldFree);
		ybctid = YBCGetYBTupleIdFromTuple(rel, tuple, planSlot->tts_tupleDescriptor);
		if (shouldFree)
			pfree(tuple);
	}

	if (ybctid == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("Missing column ybctid in DELETE request")));
	}

	MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(delete_stmt, BYTEAOID, InvalidOid, ybctid,
										   false /* is_null */);
	HandleYBStatus(YBCPgDmlBindColumn(delete_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	MemoryContextSwitchTo(oldContext);

	/* Delete row from foreign key cache */
	YBCPgDeleteFromForeignKeyReferenceCache(relid, ybctid);

	/* Execute the statement. */
	int rows_affected_count = 0;

	/*
	 * TODO (deepthi): Remove this hacky fix once #9592 is fixed
	 */
	if (changingPart)
	{
		/*
		 * This delete is part of the DELETE+INSERT done while UPDATing the
		 * partition key of a row such that it moves from one partition to
		 * another. Only if the DELETE actually removes a row, should the
		 * corresponding INSERT take place. In case of #9592 we cannot assume
		 * that a non-single row transaction always deleted an existing
		 * value. Hence until #9592 is fixed, if the delete is part of moving
		 * a row across partitions, pass &rows_affected_count even if this
		 * is not a single row transaction.
		 */
		YBCExecWriteStmt(delete_stmt, rel, &rows_affected_count);
		/* Cleanup. */
		YBCPgDeleteStatement(delete_stmt);
		return rows_affected_count > 0;
	}

	/*
	 * Instruct DocDB to return data from the columns required to evaluate
	 * returning clause expressions.
	 */
	YbDmlAppendTargets(returning_columns, delete_stmt);

	/*
	 * For system tables, mark tuple for invalidation from system caches
	 * at next command boundary.
	 *
	 * Do this now so if there is an error with delete we will re-query to get
	 * the correct state from the master.
	 */
	if (IsCatalogRelation(rel))
	{
		MarkCurrentCommandUsed();
#ifdef YB_TODO
		/* Need rework for pg15. Interface is changed for slot and tuple */
		if (slot->tts_tuple)
			CacheInvalidateHeapTuple(rel, slot->tts_tuple, NULL);
		else
		{
			/*
			 * We do not know what tuple to invalidate, and thus we have
			 * to invalidate the whole relation.
			 * This is a usual case when tuple is deleted due to an explicit
			 * DML, like DELETE FROM pg_xxx.
			 */
			CacheInvalidateCatalog(relid);
		}
#endif
	}

	YBCExecWriteStmt(delete_stmt,
					 rel,
					 target_tuple_fetched ? NULL : &rows_affected_count);

	/*
	 * Fetch values of the columns required to evaluate returning clause
	 * expressions. They are put into the slot Postgres uses to evaluate
	 * the RETURNING clause later on.
	 */
	if (returning_columns && rows_affected_count > 0)
	{
		bool			has_data   = false;
		YBCPgSysColumns	syscols;

		/*
		 * TODO Currently all delete requests sent to DocDB have ybctid and
		 * hence affect at most one row. It does not have to be that way,
		 * if the WHERE expressions all are pushed down, DocDB can iterate over
		 * the table and delete the rows satisfying the condition.
		 * Once implemented, there will be the case when we need to fetch
		 * multiple rows here. The problem is that by protocol ExecuteDelete
		 * returns one tuple at a time, and getting called again.
		 * That problem can be addressed by storing fetch state with the
		 * statement state and shortcut to emitting another tuple when the
		 * function is called again.
		 */
		Assert(rows_affected_count == 1);
		HandleYBStatus(YBCPgDmlFetch(delete_stmt,
									 tupleDesc->natts,
									 (uint64_t *) planSlot->tts_values,
									 planSlot->tts_isnull,
									 &syscols,
									 &has_data));
		Assert(has_data);
		/*
		 * The YBCPgDmlFetch function does not necessarily fetch all the
		 * attributes, only those we requested. This is planner's responsibility
		 * to ensure that returning_columns contains all the
		 * attributes that may be referenced during subsequent evaluations.
		 */
		planSlot->tts_nvalid = tupleDesc->natts;
		planSlot->tts_flags &= ~TTS_FLAG_EMPTY;

		/*
		 * The Result is getting dummy TLEs in place of missing attributes,
		 * so we should fix the tuple table slot's descriptor before
		 * the RETURNING clause expressions are evaluated.
		 */
		planSlot->tts_tupleDescriptor = CreateTupleDescCopyConstr(tupleDesc);
	}

	/* Cleanup. */
	YBCPgDeleteStatement(delete_stmt);

	return target_tuple_fetched || rows_affected_count > 0;
}

void YBCExecuteDeleteIndex(Relation index,
						   Datum *values,
						   bool *isnull,
						   Datum ybctid,
						   yb_bind_for_write_function callback,
						   void *indexstate)
{
	Assert(index->rd_rel->relkind == RELKIND_INDEX);

	Oid            dboid    = YBCGetDatabaseOid(index);
	Oid            relid    = RelationGetRelid(index);
	YBCPgStatement delete_stmt = NULL;

	/* Create the DELETE request and add the values from the tuple. */
	HandleYBStatus(YBCPgNewDelete(dboid,
								  relid,
								  false /* is_single_row_txn */,
								  YBCIsRegionLocal(index),
								  &delete_stmt));

	callback(delete_stmt, indexstate, index, values, isnull,
			 IndexRelationGetNumberOfKeyAttributes(index),
			 ybctid, false /* ybctid_as_value */);

	YBCForeignKeyReferenceCacheDeleteIndex(index, values, isnull);

	/*
	 * If index backfill hasn't finished yet, deletes to the index should be
	 * persisted.  Normally, deletes aren't persisted when they can be
	 * optimized out, but that breaks correctness if there's a pending
	 * backfill.
	 * TODO(jason): consider issue #6812.  We may be able to avoid persisting
	 * deletes when indisready is false.
	 * TODO(jason): consider how this will unnecessarily cause deletes to be
	 * persisted when online dropping an index (issue #4936).
	 */
	if (!*YBCGetGFlags()->ysql_disable_index_backfill && !index->rd_index->indisvalid)
		HandleYBStatus(YBCPgDeleteStmtSetIsPersistNeeded(delete_stmt,
														 true));

	YBCExecWriteStmt(delete_stmt, index, NULL /* rows_affected_count */);
	YBCPgDeleteStatement(delete_stmt);
}

bool YBCExecuteUpdate(ResultRelInfo *resultRelInfo,
					  TupleTableSlot *planSlot,
					  TupleTableSlot *slot,
					  HeapTuple oldtuple,
					  EState *estate,
					  ModifyTable *mt_plan,
					  bool target_tuple_fetched,
					  bool is_single_row_txn,
					  Bitmapset *updatedCols,
					  bool canSetTag)
{
	// The input heap tuple's descriptor
	Relation rel = resultRelInfo->ri_RelationDesc;
	TupleDesc		inputTupleDesc = slot->tts_tupleDescriptor;
	// The target table tuple's descriptor
	TupleDesc		outputTupleDesc = RelationGetDescr(rel);
	Oid				dboid = YBCGetDatabaseOid(rel);
	Oid				relid = RelationGetRelid(rel);
	YBCPgStatement	update_stmt = NULL;
	Datum			ybctid;

	/* is_single_row_txn always implies target tuple wasn't fetched. */
	Assert(!is_single_row_txn || !target_tuple_fetched);

	/* YB_TODO: Should materialize arg be true - check other usages as well that you have introduced? */
	bool	  shouldFree = true;
	HeapTuple tuple = ExecFetchSlotHeapTuple(slot, true, &shouldFree);

	/* Update the tuple with table oid */
	slot->tts_tableOid = RelationGetRelid(rel);
	tuple->t_tableOid = slot->tts_tableOid;

	/* Create update statement. */
	HandleYBStatus(YBCPgNewUpdate(dboid,
								  relid,
								  is_single_row_txn,
								  YBCIsRegionLocal(rel),
								  &update_stmt));

	/*
	 * Look for ybctid. Raise error if ybctid is not found.
	 *
	 * Retrieve ybctid from the slot if possible, otherwise generate it
	 * from tuple values.
	 */
	if (target_tuple_fetched)
		ybctid = YBCGetYBTupleIdFromSlot(planSlot);
	else
		ybctid = YBCGetYBTupleIdFromTuple(rel, tuple, inputTupleDesc);

	if (ybctid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("Missing column ybctid in UPDATE request")));

	YBCBindTupleId(update_stmt, ybctid);

	/* Assign new values to the updated columns for the current row. */
	AttrNumber	minattr		= YBGetFirstLowInvalidAttributeNumber(rel);
	bool		whole_row	= bms_is_member(InvalidAttrNumber, updatedCols);
	ListCell   *pushdown_lc	= list_head(mt_plan->ybPushdownTlist);

	Bitmapset *pkey = YBGetTablePrimaryKeyBms(rel);

	for (int idx = 0; idx < outputTupleDesc->natts; idx++)
	{
		FormData_pg_attribute *att_desc = TupleDescAttr(outputTupleDesc, idx);

		AttrNumber attnum = att_desc->attnum;
		int32_t type_id = att_desc->atttypid;

		/* Skip virtual (system) and dropped columns */
		if (!IsRealYBColumn(rel, attnum))
			continue;

		if (!whole_row && !bms_is_member(attnum - minattr, updatedCols))
			continue;

		/*
		 * Regular updates should not mention primary key columns, as they are
		 * supposed to go through YBCExecuteUpdateReplace routine.
		 */
		Assert(!bms_is_member(attnum - minattr, pkey));

		MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));
		/* Assign this attr's value, handle expression pushdown if needed. */
		if (pushdown_lc != NULL &&
			((TargetEntry *) lfirst(pushdown_lc))->resno == attnum)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(pushdown_lc);
			Expr *expr = YbExprInstantiateParams(tle->expr, estate);
			YBCPgExpr ybc_expr = YBCNewEvalExprCall(update_stmt, expr);
			HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));

			pushdown_lc = lnext(mt_plan->ybPushdownTlist, pushdown_lc);
		}
		else
		{
			bool is_null = false;
			Datum d = heap_getattr(tuple, attnum, inputTupleDesc, &is_null);
			/*
			 * For system relations, since we assign values to non-primary-key
			 * columns only, pass InvalidOid as collation_id to skip computing
			 * collation sortkeys.
			 */
			Oid collation_id = IsCatalogRelation(rel)
				? InvalidOid
				: YBEncodingCollation(update_stmt, attnum, att_desc->attcollation);
			YBCPgExpr ybc_expr = YBCNewConstant(update_stmt, type_id, collation_id, d, is_null);

			HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
		}
		MemoryContextSwitchTo(oldContext);
	}
	bms_free(pkey);

	/*
	 * Instruct DocDB to return data from the columns required to evaluate
	 * returning clause expressions.
	 */
	YbDmlAppendTargets(mt_plan->ybReturningColumns, update_stmt);

	/* Column references to prepare data to evaluate pushed down expressions */
	YbDmlAppendColumnRefs(mt_plan->ybColumnRefs, true /* is_primary */,
						  update_stmt);

	/* Execute the statement. */

	/*
	 * If target tuple wasn't fetched, ybctid is constructed from values extracted
	 * from the where clause, so there is no guarantee that row exists and we
	 * need to retrieve this from the DocDB.
	 * Otherwise the ybctid was obtained from DocDB, and it is known beforehand
	 * thet row exists and will be affected by the operation.
	 */
	int rows_affected_count = target_tuple_fetched ? 1 : 0;

	/*
	 * Check if the statement can be batched.
	 *
	 * In general, it can not if we need any information from the response to
	 * finish update processing.
	 *
	 * A number of thing are to be done after the modification is applied:
	 * increment the number of rows afecteded by the statement; update the
	 * secondary indexes, run after row update triggers, evaluate the returning
	 * clause.
	 *
	 * If the statement has fetched target tuple, we already have the
	 * information: as explained above, number of rows affected is 1, and the
	 * tuple needed to accomplish the rest of the tasks is the one that has been
	 * emitted by the subplan.
	 *
	 * But if the statement did not fetch target tuple, we can only batch if the
	 * statement does not change the number of rows affected (the case if the
	 * canSetTag flag is false) AND the statement updates no indexed columns AND
	 * table has no AFTER ROW UPDATE triggers AND there is no RETURNING clause.
	 *
	 * Currently we always fetch target tuple if the statement affects
	 * indexed columns or table has AFTER ROW UPDATE triggers, so only
	 * the first and the last conditions are checked here.
	 */
	bool can_batch_update = target_tuple_fetched ||
		(!canSetTag && resultRelInfo->ri_returningList == NIL);

	/*
	 * For system tables, mark tuple pair for invalidation from system caches
	 * at next command boundary.
	 *
	 * Some system updates are marked as in-place update (i.e. overwrite), for
	 * them we will invalidate the new tuple at next command boundary instead.
	 * See heap_inplace_update().
	 *
	 * Do these now so if there is an error with update we will re-query to get
	 * the correct state from the master.
	 */
	if (IsCatalogRelation(rel))
	{
		MarkCurrentCommandUsed();
		if (oldtuple)
			CacheInvalidateHeapTuple(rel, oldtuple, tuple);
		else
		{
			/*
			 * We do not know what tuple to invalidate, and thus we have
			 * to invalidate the whole relation.
			 * This is a sometimes the case when tuple is updated due to an explicit
			 * DML, like UPDATE pg_xxx SET ...
			 */
			CacheInvalidateCatalog(relid);
		}
	}

	/* If update batching is allowed, then ignore rows_affected_count. */
	YBCExecWriteStmt(update_stmt,
					 rel,
					 can_batch_update ? NULL : &rows_affected_count);

	/*
	 * Fetch values of the columns required to evaluate returning clause
	 * expressions. They are put into the slot Postgres uses to evaluate
	 * the RETURNING clause later on.
	 */
	if (mt_plan->ybReturningColumns && rows_affected_count > 0)
	{
		Datum		   *values    = planSlot->tts_values;
		bool		   *isnull    = planSlot->tts_isnull;
		bool			has_data   = false;
		YBCPgSysColumns	syscols;

		/*
		 * TODO Currently all update requests sent to DocDB have ybctid and
		 * hence affect at most one row. It does not have to be that way,
		 * if the SET and WHERE expressions all are pushed down, DocDB can
		 * iterate over the table and update the rows satisfying the condition.
		 * Once implemented, there will be the case when we need to fetch
		 * multiple rows here. The problem is that by protocol ExecuteUpdate
		 * returns one tuple at a time, and getting called again.
		 * That problem can be addressed by storing fetch state with the
		 * statement state and shortcut to emitting another tuple when the
		 * function is called again.
		 */
		Assert(rows_affected_count == 1);
		HandleYBStatus(YBCPgDmlFetch(update_stmt,
									 outputTupleDesc->natts,
								 	 (uint64_t *) values,
									 isnull,
									 &syscols,
									 &has_data));

		Assert(has_data);
		/*
		 * The YBCPgDmlFetch function does not necessarily fetch all the
		 * attributes, only those we requested. This is planner's responsibility
		 * to ensure that mt_plan->ybReturningColumns contains all the
		 * attributes that may be referenced during subsequent evaluations.
		 */
		planSlot->tts_nvalid = outputTupleDesc->natts;
		planSlot->tts_flags &= ~TTS_FLAG_EMPTY;

		/*
		 * The Result is getting dummy TLEs in place of missing attributes,
		 * so we should fix the tuple table slot's descriptor before
		 * the RETURNING clause expressions are evaluated.
		 */
		planSlot->tts_tupleDescriptor = CreateTupleDescCopyConstr(outputTupleDesc);
	}

	/* Cleanup. */
	YBCPgDeleteStatement(update_stmt);

	/*
	 * If the relation has indexes, save the ybctid to insert the updated row into the indexes.
	 */
	if (YBRelHasSecondaryIndices(rel))
	{
		TABLETUPLE_YBCTID(slot) = ybctid;
	}

	if (shouldFree)
		pfree(tuple);

	/*
	 * For batched statements rows_affected_count remains at its initial value:
	 * 0 if a single row statement, 1 otherwise.
	 * Former would effectively break further evaluation, so there should be no
	 * secondary indexes, after row update triggers, nor returning clause.
	 */
	return rows_affected_count > 0;
}

bool
YBCExecuteUpdateLoginAttempts(Oid roleid,
							  int failed_attempts,
							  char rolprfstatus)
{
	YBCPgStatement	update_stmt = NULL;
	Datum			ybctid;
	HeapTuple	 	tuple = yb_get_role_profile_tuple_by_role_oid(roleid);
	Relation 		rel = relation_open(YbRoleProfileRelationId, AccessShareLock);
	TupleDesc 		inputTupleDesc = rel->rd_att;
	TupleDesc		outputTupleDesc = RelationGetDescr(rel);
	Oid				dboid = YBCGetDatabaseOid(rel);

	/* Create update statement. */
	HandleYBStatus(YBCPgNewUpdate(dboid,
				   YbRoleProfileRelationId,
				   true,
				   YBCIsRegionLocal(rel),
				   &update_stmt));

	/*
	 * Look for ybctid. Raise error if ybctid is not found.
	 *
	 * Retrieve ybctid from the slot if possible, otherwise generate it
	 * from tuple values.
	 */
	ybctid = YBCGetYBTupleIdFromTuple(rel, tuple, inputTupleDesc);

	if (ybctid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("Missing column ybctid in UPDATE request")));

	YBCBindTupleId(update_stmt, ybctid);

	for (int idx = 0; idx < outputTupleDesc->natts; idx++)
	{
		FormData_pg_attribute  *att_desc = TupleDescAttr(outputTupleDesc, idx);
		AttrNumber 				attnum = att_desc->attnum;
		int32_t 				type_id = att_desc->atttypid;
		Datum 					d;

		if (attnum == Anum_pg_yb_role_profile_rolprffailedloginattempts)
			d = Int16GetDatum(failed_attempts);
		else if (attnum == Anum_pg_yb_role_profile_rolprfstatus)
			d = CharGetDatum(rolprfstatus);
		else
			continue;

		YBCPgExpr ybc_expr = YBCNewConstant(update_stmt,
											type_id,
											InvalidOid,
											d,
											false);

		HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
	}

	/* Execute the statement. */
	int rows_affected_count = 0;
	YBCExecWriteStmt(update_stmt,
					 rel,
					 &rows_affected_count);

	/* Cleanup. */
	YBCPgDeleteStatement(update_stmt);

	relation_close(rel, AccessShareLock);
	return rows_affected_count > 0;
}
void YBCExecuteUpdateReplace(Relation rel,
							 TupleTableSlot *planSlot,
							 TupleTableSlot *slot,
							 EState *estate)
{
	YBCExecuteDelete(rel,
					 planSlot,
					 NIL /* returning_columns */,
					 true /* target_tuple_fetched */,
					 false /* is_single_row_txn */,
					 false /* changingPart */,
					 estate);
	bool	  shouldFree = true;
	HeapTuple tuple = ExecFetchSlotHeapTuple(slot, true, &shouldFree);

	/* Update the tuple with table oid */
	slot->tts_tableOid = RelationGetRelid(rel);
	tuple->t_tableOid = slot->tts_tableOid;

	YBCExecuteInsert(rel,
					 RelationGetDescr(rel),
					 tuple,
					 ONCONFLICT_NONE);
	ItemPointerCopy(&tuple->t_self, &slot->tts_tid);
	if (shouldFree)
		pfree(tuple);
}

void YBCDeleteSysCatalogTuple(Relation rel, HeapTuple tuple)
{
	Oid            dboid       = YBCGetDatabaseOid(rel);
	Oid            relid       = RelationGetRelid(rel);
	YBCPgStatement delete_stmt = NULL;

	if (HEAPTUPLE_YBCTID(tuple) == 0)
		ereport(ERROR,
		        (errcode(ERRCODE_UNDEFINED_COLUMN), errmsg(
				        "Missing column ybctid in DELETE request to YugaByte database")));

	/* Prepare DELETE statement. */
	HandleYBStatus(YBCPgNewDelete(dboid,
								  relid,
								  false /* is_single_row_txn */,
								  YBCIsRegionLocal(rel),
								  &delete_stmt));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(delete_stmt, BYTEAOID, InvalidOid,
										   HEAPTUPLE_YBCTID(tuple), false /* is_null */);

	/* Delete row from foreign key cache */
	YBCPgDeleteFromForeignKeyReferenceCache(relid, HEAPTUPLE_YBCTID(tuple));

	HandleYBStatus(YBCPgDmlBindColumn(delete_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	/*
	 * Mark tuple for invalidation from system caches at next command
	 * boundary. Do this now so if there is an error with delete we will
	 * re-query to get the correct state from the master.
	 */
	MarkCurrentCommandUsed();
	CacheInvalidateHeapTuple(rel, tuple, NULL);

	YBCExecWriteStmt(delete_stmt, rel, NULL /* rows_affected_count */);

	/* Cleanup. */
	YBCPgDeleteStatement(delete_stmt);
}

void YBCUpdateSysCatalogTuple(Relation rel, HeapTuple oldtuple, HeapTuple tuple)
{
	YBCUpdateSysCatalogTupleForDb(YBCGetDatabaseOid(rel), rel, oldtuple, tuple);
}

void YBCUpdateSysCatalogTupleForDb(Oid dboid, Relation rel, HeapTuple oldtuple, HeapTuple tuple)
{
	Oid            relid       = RelationGetRelid(rel);
	TupleDesc      tupleDesc   = RelationGetDescr(rel);
	int            natts       = RelationGetNumberOfAttributes(rel);
	YBCPgStatement update_stmt = NULL;

	/* Create update statement. */
	HandleYBStatus(YBCPgNewUpdate(dboid,
								  relid,
								  false /* is_single_row_txn */,
								  YBCIsRegionLocal(rel),
								  &update_stmt));

	AttrNumber minattr = YBGetFirstLowInvalidAttributeNumber(rel);
	Bitmapset  *pkey   = YBGetTablePrimaryKeyBms(rel);

	/* Bind the ybctid to the statement. */
	YBCBindTupleId(update_stmt, HEAPTUPLE_YBCTID(tuple));

	/* Assign values to the non-primary-key columns to update the current row. */
	for (int idx = 0; idx < natts; idx++)
	{
		AttrNumber attnum = TupleDescAttr(tupleDesc, idx)->attnum;

		/* Skip primary-key columns */
		if (bms_is_member(attnum - minattr, pkey))
		{
			continue;
		}

		bool is_null = false;
		Datum d = heap_getattr(tuple, attnum, tupleDesc, &is_null);
		/*
		 * Since we are assign values to non-primary-key columns, pass InvalidOid as
		 * collation_id to skip computing collation sortkeys.
		 */
		YBCPgExpr ybc_expr = YBCNewConstant(
			update_stmt, TupleDescAttr(tupleDesc, idx)->atttypid, InvalidOid /* collation_id */,
			d, is_null);
		HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
	}

	/*
	 * Mark old tuple for invalidation from system caches at next command
	 * boundary, and mark the new tuple for invalidation in case we abort.
	 * In case when there is no old tuple, we will invalidate with the
	 * new tuple at next command boundary instead. Do these now so if there
	 * is an error with update we will re-query to get the correct state
	 * from the master.
	 */
	MarkCurrentCommandUsed();
	if (oldtuple)
		CacheInvalidateHeapTuple(rel, oldtuple, tuple);
	else
		CacheInvalidateHeapTuple(rel, tuple, NULL);

	/* Execute the statement and clean up */
	YBCExecWriteStmt(update_stmt, rel, NULL /* rows_affected_count */);

	/* Cleanup. */
	YBCPgDeleteStatement(update_stmt);;
}

bool
YBCRelInfoHasSecondaryIndices(ResultRelInfo *resultRelInfo)
{
	return resultRelInfo->ri_NumIndices > 1 ||
			(resultRelInfo->ri_NumIndices == 1 &&
			 !resultRelInfo->ri_IndexRelationDescs[0]->rd_index->indisprimary);
}
