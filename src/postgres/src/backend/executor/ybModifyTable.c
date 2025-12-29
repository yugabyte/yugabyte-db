/*--------------------------------------------------------------------------------------------------
 *
 * ybModifyTable.c
 *        YB routines to stmt_handle ModifyTable nodes.
 *
 * Copyright (c) YugabyteDB, Inc.
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
 *        src/backend/executor/ybModifyTable.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include <execinfo.h>

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "access/yb_scan.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/indexing.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_auth_members_d.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting_d.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_replication_origin_d.h"
#include "catalog/pg_shseclabel_d.h"
#include "catalog/pg_tablespace_d.h"
#include "catalog/pg_trigger.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_role_profile.h"
#include "catalog/pg_yb_role_profile_d.h"
#include "catalog/yb_catalog_version.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/trigger.h"
#include "commands/yb_profile.h"
#include "executor/executor.h"
#include "executor/tuptable.h"
#include "executor/ybExpr.h"
#include "executor/ybModifyTable.h"
#include "miscadmin.h"
#include "nodes/execnodes.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/ybplan.h"
#include "pg_yb_utils.h"
#include "tcop/pquery.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "yb/yql/pggate/ybc_gflags.h"

bool		yb_disable_transactional_writes = false;
bool		yb_enable_upsert_mode = false;
bool		yb_fast_path_for_colocated_copy = false;

/*
 * Hack to ensure that the next CommandCounterIncrement() will call
 * CommandEndInvalidationMessages(). The result of this call is not
 * needed on the yb side, however the side effects are.
 */
void
MarkCurrentCommandUsed()
{
	(void) GetCurrentCommandId(true);
}

/*
 * Returns whether relation is capable of single row execution.
 */
bool
YBCIsSingleRowTxnCapableRel(ResultRelInfo *resultRelInfo)
{
	bool		has_triggers = resultRelInfo->ri_TrigDesc && resultRelInfo->ri_TrigDesc->numtriggers > 0;

	/*
	 * It would be nice to use YBCRelInfoHasSecondaryIndices(resultRelInfo)
	 * instead of the below, but that doesn't work because the callers (2 of 2
	 * at the time of writing) do not have that information populated yet.
	 */
	bool		has_indices = YBRelHasSecondaryIndices(resultRelInfo->ri_RelationDesc);

	return !has_indices && !has_triggers;
}

bool
YbIsSingleRowModifyTxnPlanned(PlannedStmt *pstmt, EState *estate)
{
	if (!IsYugaByteEnabled() || YbIsBatchedExecution() || IsTransactionBlock())
		return false;

	Assert(ActivePortal);

	return ActivePortal->stmts->length == 1 &&
		YBCIsSingleRowModify(pstmt) &&
		(pstmt->commandType == CMD_UPDATE ||
		 YBCIsSingleRowTxnCapableRel((ResultRelInfo *) linitial(estate->es_opened_result_relations)));
}

/*
 * Get the ybctid from a YB scan slot for UPDATE/DELETE.
 */
Datum
YBCGetYBTupleIdFromSlot(TupleTableSlot *slot)
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
Datum
YBCComputeYBTupleIdFromSlot(Relation rel, TupleTableSlot *slot)
{
	Oid			dboid = YBCGetDatabaseOid(rel);
	YbcPgTableDesc ybc_table_desc = NULL;

	HandleYBStatus(YBCPgGetTableDesc(dboid, YbGetRelfileNodeId(rel), &ybc_table_desc));
	Bitmapset  *pkey = YBGetTableFullPrimaryKeyBms(rel);
	AttrNumber	minattr = YBSystemFirstLowInvalidAttributeNumber + 1;
	YbcPgYBTupleIdDescriptor *descr = YBCCreateYBTupleIdDescriptor(dboid,
																   YbGetRelfileNodeId(rel),
																   bms_num_members(pkey));
	YbcPgAttrValueDescriptor *next_attr = descr->attrs;
	int			col = -1;

	while ((col = bms_next_member(pkey, col)) >= 0)
	{
		AttrNumber	attnum = col + minattr;

		next_attr->attr_num = attnum;
		/*
		 * Don't need to fill in for the DocDB RowId column, however we still
		 * need to add the column to the statement to construct the ybctid.
		 */
		if (attnum > 0)
		{
			Oid			type_id = TupleDescAttr(slot->tts_tupleDescriptor,
												attnum - 1)->atttypid;

			next_attr->type_entity = YbDataTypeFromOidMod(attnum, type_id);
			next_attr->collation_id = ybc_get_attcollation(RelationGetDescr(rel), attnum);
			next_attr->datum = slot_getattr(slot, attnum, &next_attr->is_null);
		}
		else if (attnum == YBRowIdAttributeNumber)
		{
			next_attr->datum = 0;
			next_attr->is_null = false;
			next_attr->type_entity = NULL;
			next_attr->collation_id = InvalidOid;
		}
		else
		{
			Oid			type_id = SystemAttributeDefinition(attnum)->atttypid;

			next_attr->type_entity = YbDataTypeFromOidMod(attnum, type_id);
			next_attr->collation_id = ybc_get_attcollation(RelationGetDescr(rel), attnum);
			next_attr->datum = slot_getsysattr(slot, attnum, &next_attr->is_null);
		}
		YbcPgColumnInfo column_info = {0};

		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
												   attnum,
												   &column_info), ybc_table_desc);
		YBSetupAttrCollationInfo(next_attr, &column_info);
		++next_attr;
	}
	uint64_t	tuple_id = 0;

	HandleYBStatus(YBCPgBuildYBTupleId(descr, &tuple_id));
	pfree(descr);
	return (Datum) tuple_id;
}

/*
 * Bind ybctid to the statement.
 */
static void
YBCBindTupleId(YbcPgStatement pg_stmt, Datum tuple_id)
{
	YbcPgExpr	ybc_expr = YBCNewConstant(pg_stmt, BYTEAOID, InvalidOid, tuple_id,
										  false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(pg_stmt, YBTupleIdAttributeNumber, ybc_expr));
}

/*
 * Utility method to execute a prepared write statement.
 * Will handle the case if the write changes the system catalogs meaning
 * we need to increment the catalog versions accordingly.
 */
static void
YBCExecWriteStmt(YbcPgStatement ybc_stmt,
				 Relation rel,
				 int *rows_affected_count)
{
	YbSetCatalogCacheVersion(ybc_stmt, YbGetCatalogCacheVersion());

	bool		is_syscatalog_version_inc = YbMarkStatementIfCatalogVersionIncrement(ybc_stmt, rel);

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
		/* TODO(shane) also update the shared memory catalog version here. */
		YbUpdateCatalogCacheVersion(YbGetCatalogCacheVersion() + 1);
	}

	if (YBIsDBCatalogVersionMode())
	{
		Oid			relid = RelationGetRelid(rel);

		if (RelationGetForm(rel)->relisshared &&
			(RelationSupportsSysCache(relid) ||
			 YbRelationIdIsInInitFileAndNotCached(relid) ||
			 YbSharedRelationIdNeedsGlobalImpact(relid)) &&
			!(*YBCGetGFlags()->ysql_disable_global_impact_ddl_statements))
		{
			/* NOTE: relisshared implies that rel is a system relation. */
			Assert(IsSystemRelation(rel));
			/*
			 * There are 3 sections in the next Assert. Relation ids
			 * in each section are grouped together and these sections
			 * are separated with an empty line.
			 *
			 * Section 1 contains relations in relcache init file that
			 * support sys cache. Should be kept in sync with shared
			 * relids in RelationSupportsSysCache.
			 *
			 * Section 2 contains relations in relcache init file but
			 * do not support sys cache. Should be kept in sync with
			 * YbRelationIdIsInInitFileAndNotCached.
			 *
			 * Section 3 contains relations that can be prefetched
			 * but do not have a PG catalog cache. Should be kept in
			 * sync with YbSharedRelationIdNeedsGlobalImpact.
			 */
			Assert(relid == AuthIdRelationId ||
				   relid == AuthIdRolnameIndexId ||
				   relid == AuthMemRelationId ||
				   relid == AuthMemRoleMemIndexId ||
				   relid == AuthMemMemRoleIndexId ||
				   relid == DatabaseRelationId ||
				   relid == TableSpaceRelationId ||
				   relid == ReplicationOriginRelationId ||
				   relid == ReplicationOriginIdentIndex ||
				   relid == ReplicationOriginNameIndex ||

				   relid == DatabaseNameIndexId ||
				   relid == SharedSecLabelRelationId ||
				   relid == SharedSecLabelObjectIndexId ||

				   relid == DbRoleSettingRelationId ||
				   relid == DbRoleSettingDatidRolidIndexId);

			YbSetIsGlobalDDL();
		}
	}
}

static void
YBCApplyInsertRow(YbcPgStatement insert_stmt,
				  Relation rel,
				  TupleTableSlot *slot,
				  OnConflictAction onConflictAction,
				  Datum *ybctid,
				  YbcPgTransactionSetting transaction_setting)
{
	Oid			relfileNodeId = YbGetRelfileNodeId(rel);
	AttrNumber	minattr = YBGetFirstLowInvalidAttributeNumber(rel);
	int			natts = RelationGetNumberOfAttributes(rel);
	Bitmapset  *pkey = YBGetTablePrimaryKeyBms(rel);
	TupleDesc	tupleDesc = RelationGetDescr(rel);

	/* Get the ybctid for the tuple and bind to statement */
	TABLETUPLE_YBCTID(slot) = ((ybctid != NULL && *ybctid != 0) ?
							   *ybctid :
							   YBCComputeYBTupleIdFromSlot(rel, slot));

	if (ybctid != NULL)
	{
		*ybctid = TABLETUPLE_YBCTID(slot);
	}
	int			buf_size = natts - minattr + 1;
	YbcBindColumn columns[buf_size];
	YbcBindColumn *column = columns;

	for (AttrNumber attnum = minattr; attnum <= natts; attnum++)
	{
		/* Skip virtual (system) and dropped columns */
		if (!IsRealYBColumn(rel, attnum) || bms_is_member(attnum - minattr, pkey))
		{
			continue;
		}

		column->attr_num = attnum;
		Oid			type_id = GetTypeId(attnum, tupleDesc);

		column->type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);

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
		Oid			collation_id = YBEncodingCollation(insert_stmt, attnum,
													   ybc_get_attcollation(RelationGetDescr(rel), attnum));

		column->datum = slot_getattr(slot, attnum, &column->is_null);
		YBGetCollationInfo(collation_id, column->type_entity, column->datum, column->is_null, &column->collation_info);

		/* Add the column value to the insert request */
		++column;
	}
	HandleYBStatus(YBCPgDmlBindRow(insert_stmt, TABLETUPLE_YBCTID(slot),
								   columns, column - columns));

	/*
	 * For system tables, mark tuple for invalidation from system caches
	 * at next command boundary.
	 *
	 * Do this now so if there is an error with insert we will re-query to get
	 * the correct state from the master.
	 */
	if (IsCatalogRelation(rel))
	{
		bool		shouldFree;
		HeapTuple	tuple = ExecFetchSlotHeapTuple(slot, false, &shouldFree);

		MarkCurrentCommandUsed();
		CacheInvalidateHeapTuple(rel, tuple, NULL);
		if (shouldFree)
			pfree(tuple);
	}

	if (onConflictAction == ONCONFLICT_YB_REPLACE || yb_enable_upsert_mode)
	{
		HandleYBStatus(YBCPgInsertStmtSetUpsertMode(insert_stmt));
	}

	/* Add row into foreign key cache */
	if (transaction_setting != YB_SINGLE_SHARD_TRANSACTION)
		YBCPgAddIntoForeignKeyReferenceCache(relfileNodeId,
											 TABLETUPLE_YBCTID(slot));

}

/*
 * Utility method to insert a tuple into the relation's backing YugaByte table.
 */
static void
YBCExecuteInsertInternal(Oid dboid,
						 Relation rel,
						 TupleTableSlot *slot,
						 OnConflictAction onConflictAction,
						 Datum *ybctid,
						 YbcPgTransactionSetting transaction_setting)
{
	YbcPgStatement insert_stmt = YbNewInsertForDb(dboid, rel, transaction_setting);

	YBCApplyInsertRow(insert_stmt, rel, slot, onConflictAction, ybctid, transaction_setting);
	/* Execute the insert */
	YBCApplyWriteStmt(insert_stmt, rel);
}

void
YBCExecuteInsert(Relation rel,
				 TupleTableSlot *slot,
				 OnConflictAction onConflictAction)
{
	YBCExecuteInsertForDb(YBCGetDatabaseOid(rel),
						  rel,
						  slot,
						  onConflictAction,
						  NULL /* ybctid */ ,
						  YB_TRANSACTIONAL);
}

void
YBCApplyWriteStmt(YbcPgStatement handle, Relation relation)
{
	/* Execute the insert */
	YBCExecWriteStmt(handle, relation, NULL /* rows_affected_count */ );

	/* Cleanup. */
	YBCPgDeleteStatement(handle);
}

static YbcPgTransactionSetting
YBCFixTransactionSetting(Relation rel,
						 YbcPgTransactionSetting transaction_setting)
{
	if ((transaction_setting == YB_TRANSACTIONAL) &&
		!IsSystemRelation(rel) && yb_disable_transactional_writes)
		return YB_NON_TRANSACTIONAL;
	return transaction_setting;
}

void
YBCExecuteInsertHeapTupleForDb(Oid dboid,
							   Relation rel,
							   HeapTuple tuple,
							   OnConflictAction onConflictAction,
							   Datum *ybctid,
							   YbcPgTransactionSetting transaction_setting)
{
	TupleTableSlot *slot = MakeSingleTupleTableSlot(RelationGetDescr(rel),
													&TTSOpsHeapTuple);

	ExecStoreHeapTuple(tuple, slot, false);
	YBCExecuteInsertForDb(dboid, rel, slot, onConflictAction, ybctid,
						  transaction_setting);
	HEAPTUPLE_YBCTID(tuple) = TABLETUPLE_YBCTID(slot);
	ExecDropSingleTupleTableSlot(slot);
}

void
YBCExecuteInsertForDb(Oid dboid,
					  Relation rel,
					  TupleTableSlot *slot,
					  OnConflictAction onConflictAction,
					  Datum *ybctid,
					  YbcPgTransactionSetting transaction_setting)
{
	YBCExecuteInsertInternal(dboid,
							 rel,
							 slot,
							 onConflictAction,
							 ybctid,
							 YBCFixTransactionSetting(rel, transaction_setting));
}

void
YBCExecuteNonTxnInsert(Relation rel,
					   TupleTableSlot *slot,
					   OnConflictAction onConflictAction)
{
	YBCExecuteNonTxnInsertForDb(YBCGetDatabaseOid(rel),
								rel,
								slot,
								onConflictAction,
								NULL /* ybctid */ );
}

void
YBCExecuteNonTxnInsertForDb(Oid dboid,
							Relation rel,
							TupleTableSlot *slot,
							OnConflictAction onConflictAction,
							Datum *ybctid)
{
	YBCExecuteInsertInternal(dboid,
							 rel,
							 slot,
							 onConflictAction,
							 ybctid,
							 YB_NON_TRANSACTIONAL);
}

void
YBCHeapInsert(ResultRelInfo *resultRelInfo,
			  TupleTableSlot *slot,
			  YbcPgStatement blockInsertStmt,
			  EState *estate)
{
	/*
	 * get information on the (current) result relation
	 */
	Relation	resultRelationDesc = resultRelInfo->ri_RelationDesc;

	slot->tts_tableOid = RelationGetRelid(resultRelationDesc);
	YbcPgTransactionSetting transaction_setting = (estate->yb_es_is_single_row_modify_txn ?
												   YB_SINGLE_SHARD_TRANSACTION :
												   YB_TRANSACTIONAL);

	if (blockInsertStmt)
	{
		YBCApplyInsertRow(blockInsertStmt, resultRelationDesc, slot,
						  ONCONFLICT_NONE, NULL /* ybctid */ ,
						  YBCFixTransactionSetting(resultRelationDesc,
												   transaction_setting));
		return;
	}

	Oid			dboid = YBCGetDatabaseOid(resultRelationDesc);

	/*
	 * If estate->yb_es_is_single_row_modify_txn is true, try to execute the
	 * statement as a single row transaction (rather than a distributed
	 * transaction) if it is safe to do so. I.e. if we are in a single-statement
	 * transaction that targets a single row (i.e. single-row-modify txn), and
	 * there are no indices or triggers on the target table.
	 */
	YBCExecuteInsertForDb(dboid, resultRelationDesc, slot, ONCONFLICT_NONE,
						  NULL /* ybctid */ , transaction_setting);
}

bool
YbIsInsertOnConflictReadBatchingPossible(ResultRelInfo *resultRelInfo)
{
	/*
	 * TODO(jason): figure out how to enable triggers.
	 */
	if (yb_insert_on_conflict_read_batch_size == 0 ||
		!IsYBRelation(resultRelInfo->ri_RelationDesc) ||
		IsCatalogRelation(resultRelInfo->ri_RelationDesc) ||
		(resultRelInfo->ri_TrigDesc &&
		 (resultRelInfo->ri_TrigDesc->trig_delete_before_row ||
		  resultRelInfo->ri_TrigDesc->trig_delete_instead_row ||
		  resultRelInfo->ri_TrigDesc->trig_insert_before_row ||
		  resultRelInfo->ri_TrigDesc->trig_insert_instead_row ||
		  resultRelInfo->ri_TrigDesc->trig_update_before_row ||
		  resultRelInfo->ri_TrigDesc->trig_update_instead_row)))
	{
		return false;
	}

	TriggerDesc *trigdesc = resultRelInfo->ri_TrigDesc;

	if (!(trigdesc && (trigdesc->trig_delete_after_row ||
					   trigdesc->trig_insert_after_row ||
					   trigdesc->trig_update_after_row)))
		return true;

	/* Any non-FK after row triggers make batching not supported. */
	for (int i = 0; i < trigdesc->numtriggers; i++)
	{
		Trigger    *trig = &trigdesc->triggers[i];

		if (TRIGGER_FOR_AFTER(trig->tgtype) &&
			TRIGGER_FOR_ROW(trig->tgtype) &&
			RI_FKey_trigger_type(trig->tgfoid) == RI_TRIGGER_NONE)
			return false;
	}
	return true;
}

/*
 * Utility method to build a ybctid descriptor for a unique index.
 * This method is invoked in conjunction with storing ybctids in pggate, and
 * building a descriptor rather the ybctid itself saves us a copy.
 *
 * Note that the unique index may refer to either the primary key or a unique,
 * secondary index.
 */
YbcPgYBTupleIdDescriptor *
YBCBuildUniqueIndexYBTupleId(Relation unique_index, Datum *values, bool *nulls)
{
	Assert(IsYBRelation(unique_index));

	const bool	is_pkey = unique_index->rd_index->indisprimary;
	Oid			dboid = YBCGetDatabaseOid(unique_index);
	Oid			relfileNodeId;
	YbcPgTableDesc ybc_table_desc = NULL;
	TupleDesc	tupdesc;

	if (is_pkey)
	{
		Relation	main_table = RelationIdGetRelation(unique_index->rd_index->indrelid);

		relfileNodeId = YbGetRelfileNodeId(main_table);
		tupdesc = RelationGetDescr(main_table);
		RelationClose(main_table);
	}
	else
	{
		relfileNodeId = YbGetRelfileNodeId(unique_index);
		tupdesc = RelationGetDescr(unique_index);
	}

	HandleYBStatus(YBCPgGetTableDesc(dboid, relfileNodeId, &ybc_table_desc));
	const int	nattrs = IndexRelationGetNumberOfKeyAttributes(unique_index);
	YbcPgYBTupleIdDescriptor *result = YBCCreateYBTupleIdDescriptor(dboid,
																	relfileNodeId, nattrs + (is_pkey ? 0 : 1));
	YbcPgAttrValueDescriptor *next_attr = result->attrs;
	bool		has_null = false;

	for (int i = 0; i < nattrs; ++i)
	{
		AttrNumber	attnum;

		if (is_pkey)
		{
			/*
			 * Since Yugabyte tables are clustered on the primary key, the
			 * primary key reuses the attribute numbers of the table. This leads
			 * to cases where the primary key attribute numbers are neither
			 * contiguous nor in the same order as that of the main table.
			 * For example:
			 *  CREATE TABLE foo (a int, b int, c int, PRIMARY KEY (c, a));
			 * The table 'foo' would have the attribute numbers:
			 * a: 1, b: 2, c: 3
			 * while its primary key would have the attribute numbers:
			 * a: 1, c: 3
			 * The array of index col values supplied to this function is in the
			 * order defined by the index (primary key). For the above example,
			 * this would be [c, a]. Therefore, the column values have to be
			 * explicitly mapped to the attribute numbers of the main table.
			 * That is, the attribute numbers would have to be [3, 1] rather
			 * than [1, 2] as in the case of secondary indexes.
			 */
			attnum = unique_index->rd_index->indkey.values[i];
		}
		else
			attnum = i + 1;

		Oid			type_id = GetTypeId(attnum, tupdesc);

		next_attr->type_entity = YbDataTypeFromOidMod(attnum, type_id);
		next_attr->collation_id = ybc_get_attcollation(tupdesc, attnum);
		next_attr->attr_num = attnum;
		next_attr->datum = values[i];
		next_attr->is_null = nulls == NULL ? false : nulls[i];
		has_null = has_null || next_attr->is_null;
		YbcPgColumnInfo column_info = {0};

		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
												   attnum,
												   &column_info), ybc_table_desc);
		YBSetupAttrCollationInfo(next_attr, &column_info);
		++next_attr;
	}

	/* Primary keys do not have the YBUniqueIdxKeySuffix attribute */
	if (!is_pkey)
	{
		/*
		 * If unique_index is not PK, this function should be used only for
		 * indexes that use nulls-not-distinct mode or for the index tuples with
		 * non-null values for all the index key columns. YBUniqueIdxKeySuffix
		 * is null in both these cases.
		 */
		Assert(unique_index->rd_index->indnullsnotdistinct || !has_null);
		YBCFillUniqueIndexNullAttribute(result);
	}

	return result;
}

static void
YBCForeignKeyReferenceCacheDeleteIndex(Relation index, Datum *values, bool *isnulls)
{
	Assert(index->rd_rel->relkind == RELKIND_INDEX);
	/* Only unique index can be used in foreign key constraints */
	if (!index->rd_index->indisprimary && index->rd_index->indisunique)
	{
		const int	nattrs = IndexRelationGetNumberOfKeyAttributes(index);

		/*
		 * Index with at least one NULL value can't be referenced by foreign key constraint,
		 * and can't be stored in cache, so ignore it.
		 */
		for (int i = 0; i < nattrs; ++i)
			if (isnulls[i])
				return;

		YbcPgYBTupleIdDescriptor *descr = YBCBuildUniqueIndexYBTupleId(index,
																	   values,
																	   NULL);

		HandleYBStatus(YBCPgForeignKeyReferenceCacheDelete(descr));
		pfree(descr);
	}
}

void
YBCExecuteInsertIndex(Relation index,
					  Datum *values,
					  bool *isnull,
					  Datum ybctid,
					  const uint64_t *backfill_write_time,
					  yb_bind_for_write_function callback,
					  void *indexstate)
{
	YBCExecuteInsertIndexForDb(YBCGetDatabaseOid(index),
							   index,
							   values,
							   isnull,
							   ybctid,
							   backfill_write_time,
							   callback,
							   indexstate);
}

void
YBCExecuteInsertIndexForDb(Oid dboid,
						   Relation index,
						   Datum *values,
						   bool *isnull,
						   Datum ybctid,
						   const uint64_t *backfill_write_time,
						   yb_bind_for_write_function callback,
						   void *indexstate)
{
	Assert(index->rd_rel->relkind == RELKIND_INDEX);
	Assert(ybctid != 0);

	const bool	is_backfill = (backfill_write_time != NULL);
	const bool	is_non_distributed_txn_write = (is_backfill ||
												(!IsSystemRelation(index) &&
												 yb_disable_transactional_writes));

	YbcPgStatement insert_stmt =
		YbNewInsertForDb(dboid, index,
						 is_non_distributed_txn_write ? YB_NON_TRANSACTIONAL : YB_TRANSACTIONAL);

	callback(insert_stmt, indexstate, index, values, isnull,
			 RelationGetNumberOfAttributes(index),
			 ybctid, true /* ybctid_as_value */ );

	/*
	 * For non-unique indexes the primary-key component (base tuple id) already
	 * guarantees uniqueness, so no need to read and check it in DocDB.
	 */
	if (!index->rd_index->indisunique)
		HandleYBStatus(YBCPgInsertStmtSetUpsertMode(insert_stmt));

	if (is_backfill)
	{
		HandleYBStatus(YBCPgInsertStmtSetIsBackfill(insert_stmt,
													true /* is_backfill */ ));
		/*
		 * For index backfill, set write hybrid time to a time in the past.
		 * This is to guarantee that backfilled writes are temporally before
		 * any online writes.
		 */
		HandleYBStatus(YBCPgInsertStmtSetWriteTime(insert_stmt,
												   *backfill_write_time));
	}

	/* Execute the insert and clean up. */
	YBCApplyWriteStmt(insert_stmt, index);
}

bool
YBCExecuteDelete(Relation rel,
				 TupleTableSlot *planSlot,
				 List *returning_columns,
				 bool target_tuple_fetched,
				 YbcPgTransactionSetting transaction_setting,
				 bool changingPart,
				 EState *estate)
{
	TupleDesc	tupleDesc = RelationGetDescr(rel);
	Datum		ybctid;

	/* YB_SINGLE_SHARD_TRANSACTION always implies target tuple wasn't fetched. */
	Assert((transaction_setting != YB_SINGLE_SHARD_TRANSACTION) || !target_tuple_fetched);

	YbcPgStatement delete_stmt = YbNewDelete(rel, transaction_setting);

	/*
	 * Look for ybctid. Raise error if ybctid is not found.
	 *
	 * Retrieve ybctid from the slot if possible, otherwise generate it
	 * from tuple values.
	 */
	if (target_tuple_fetched)
		ybctid = YBCGetYBTupleIdFromSlot(planSlot);
	else
		ybctid = YBCComputeYBTupleIdFromSlot(rel, planSlot);

	if (ybctid == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("missing column ybctid in DELETE request")));
	}

	TABLETUPLE_YBCTID(planSlot) = ybctid;
	MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(delete_stmt, BYTEAOID, InvalidOid, ybctid,
											 false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(delete_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	MemoryContextSwitchTo(oldContext);

	/* Delete row from foreign key cache */
	YBCPgDeleteFromForeignKeyReferenceCache(YbGetRelfileNodeId(rel), ybctid);

	/* Execute the statement. */
	int			rows_affected_count = 0;

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
		if (!target_tuple_fetched)
		{
			bool		shouldFree;
			HeapTuple	tuple = ExecFetchSlotHeapTuple(planSlot, false, &shouldFree);

			CacheInvalidateHeapTuple(rel, tuple, NULL);
			if (shouldFree)
				pfree(tuple);
		}
		else
		{
			/*
			 * We do not know what tuple to invalidate, and thus we have
			 * to invalidate the whole relation.
			 * This is a usual case when tuple is deleted due to an explicit
			 * DML, like DELETE FROM pg_xxx.
			 */
			CacheInvalidateCatalog(RelationGetRelid(rel));
		}
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
		bool		has_data = false;
		YbcPgSysColumns syscols;

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

void
YBCExecuteDeleteIndex(Relation index,
					  Datum *values,
					  bool *isnull,
					  Datum ybctid,
					  yb_bind_for_write_function callback,
					  void *indexstate)
{
	Assert(index->rd_rel->relkind == RELKIND_INDEX);

	YbcPgStatement delete_stmt = YbNewDelete(index, YB_TRANSACTIONAL);

	callback(delete_stmt, indexstate, index, values, isnull,
			 IndexRelationGetNumberOfKeyAttributes(index),
			 ybctid, false /* ybctid_as_value */ );

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
		HandleYBStatus(YBCPgDeleteStmtSetIsPersistNeeded(delete_stmt, true));

	YBCApplyWriteStmt(delete_stmt, index);
}

bool
YBCExecuteUpdate(ResultRelInfo *resultRelInfo,
				 TupleTableSlot *planSlot,
				 TupleTableSlot *slot,
				 HeapTuple oldtuple,
				 EState *estate,
				 ModifyTable *mt_plan,
				 bool target_tuple_fetched,
				 YbcPgTransactionSetting transaction_setting,
				 Bitmapset *updatedCols,
				 bool canSetTag)
{
	/* The input heap tuple's descriptor */
	Relation	rel = resultRelInfo->ri_RelationDesc;

	/* The target table tuple's descriptor */
	TupleDesc	outputTupleDesc = RelationGetDescr(rel);
	Oid			relid = RelationGetRelid(rel);
	Datum		ybctid;


	/* YB_SINGLE_SHARD_TRANSACTION always implies target tuple wasn't fetched. */
	Assert((transaction_setting != YB_SINGLE_SHARD_TRANSACTION) || !target_tuple_fetched);

	/* Update the tuple with table oid */
	slot->tts_tableOid = RelationGetRelid(rel);

	YbcPgStatement update_stmt = YbNewUpdate(rel, transaction_setting);

	/*
	 * Look for ybctid. Raise error if ybctid is not found.
	 *
	 * Retrieve ybctid from the slot if possible, otherwise generate it
	 * from tuple values.
	 */
	if (target_tuple_fetched)
		ybctid = YBCGetYBTupleIdFromSlot(planSlot);
	else
		ybctid = YBCComputeYBTupleIdFromSlot(rel, slot);

	if (ybctid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("missing column ybctid in UPDATE request")));

	TABLETUPLE_YBCTID(slot) = ybctid;
	YBCBindTupleId(update_stmt, ybctid);

	/* Assign new values to the updated columns for the current row. */
	AttrNumber	minattr = YBGetFirstLowInvalidAttributeNumber(rel);
	bool		whole_row = bms_is_member(InvalidAttrNumber, updatedCols);
	ListCell   *pushdown_lc = list_head(mt_plan->ybPushdownTlist);

	for (int idx = 0; idx < outputTupleDesc->natts; idx++)
	{
		FormData_pg_attribute *att_desc = TupleDescAttr(outputTupleDesc, idx);

		AttrNumber	attnum = att_desc->attnum;
		int32_t		type_id = att_desc->atttypid;

		/* Skip virtual (system) and dropped columns */
		if (!IsRealYBColumn(rel, attnum))
			continue;

		if (!whole_row && !bms_is_member(attnum - minattr, updatedCols))
			continue;

		/*
		 * Regular updates should not mention primary key columns, as they are
		 * supposed to go through YBCExecuteUpdateReplace routine.
		 */
		Assert(!bms_is_member(attnum - minattr, YBGetTablePrimaryKeyBms(rel)));

		/* Assign this attr's value, handle expression pushdown if needed. */
		if (pushdown_lc != NULL &&
			((TargetEntry *) lfirst(pushdown_lc))->resno == attnum)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(pushdown_lc);
			Expr	   *expr = YbExprInstantiateExprs(tle->expr, estate);
			MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));
			YbcPgExpr	ybc_expr = YBCNewEvalExprCall(update_stmt, expr);

			HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));

			pushdown_lc = lnext(mt_plan->ybPushdownTlist, pushdown_lc);
			MemoryContextSwitchTo(oldContext);
		}
		else
		{
			MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));
			bool		is_null = false;
			Datum		d = slot_getattr(slot, attnum, &is_null);

			/*
			 * For system relations, since we assign values to non-primary-key
			 * columns only, pass InvalidOid as collation_id to skip computing
			 * collation sortkeys.
			 */
			Oid			collation_id = (IsCatalogRelation(rel) ?
										InvalidOid :
										YBEncodingCollation(update_stmt,
															attnum,
															att_desc->attcollation));
			YbcPgExpr	ybc_expr = YBCNewConstant(update_stmt, type_id, collation_id, d, is_null);

			HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
			MemoryContextSwitchTo(oldContext);
		}
	}

	/*
	 * Instruct DocDB to return data from the columns required to evaluate
	 * returning clause expressions.
	 */
	YbDmlAppendTargets(mt_plan->ybReturningColumns, update_stmt);

	/* Column references to prepare data to evaluate pushed down expressions */
	YbAppendPrimaryColumnRefs(update_stmt, mt_plan->ybColumnRefs);

	/* Execute the statement. */

	/*
	 * If target tuple wasn't fetched, ybctid is constructed from values extracted
	 * from the where clause, so there is no guarantee that row exists and we
	 * need to retrieve this from the DocDB.
	 * Otherwise the ybctid was obtained from DocDB, and it is known beforehand
	 * thet row exists and will be affected by the operation.
	 */
	int			rows_affected_count = target_tuple_fetched ? 1 : 0;

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
	bool		can_batch_update = (target_tuple_fetched ||
									(!canSetTag &&
									 resultRelInfo->ri_returningList == NIL));

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
		{
			bool		shouldFree;

			/* tuple will be used transitorily, don't materialize it */
			HeapTuple	tuple = ExecFetchSlotHeapTuple(slot, false, &shouldFree);

			CacheInvalidateHeapTuple(rel, oldtuple, tuple);
			if (shouldFree)
				pfree(tuple);
		}
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
		Datum	   *values = planSlot->tts_values;
		bool	   *isnull = planSlot->tts_isnull;
		bool		has_data = false;
		YbcPgSysColumns syscols;

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

	/*
	 * For batched statements rows_affected_count remains at its initial value:
	 * 0 if a single row statement, 1 otherwise.
	 * Former would effectively break further evaluation, so there should be no
	 * secondary indexes, after row update triggers, nor returning clause.
	 */
	return rows_affected_count > 0;
}

void
YBCExecuteUpdateIndex(Relation index,
					  Datum *values,
					  bool *isnull,
					  Datum oldYbctid,
					  Datum newYbctid,
					  yb_assign_for_write_function callback)
{
	Assert(index->rd_rel->relkind == RELKIND_INDEX);

	YbcPgStatement update_stmt = YbNewUpdate(index, YB_TRANSACTIONAL);

	callback(update_stmt, index, values, isnull,
			 RelationGetNumberOfAttributes(index),
			 oldYbctid, newYbctid);

	YBCApplyWriteStmt(update_stmt, index);
}

bool
YBCExecuteUpdateLoginAttempts(Oid roleid,
							  int failed_attempts,
							  char rolprfstatus)
{
	Datum		ybctid;
	HeapTuple	tuple = yb_get_role_profile_tuple_by_role_oid(roleid);
	Relation	rel = relation_open(YbRoleProfileRelationId, AccessShareLock);
	TupleDesc	inputTupleDesc = rel->rd_att;
	TupleDesc	outputTupleDesc = RelationGetDescr(rel);
	TupleTableSlot *slot;

	/* Create update statement. */
	YbcPgStatement update_stmt = YbNewUpdate(rel, YB_SINGLE_SHARD_TRANSACTION);

	/*
	 * Look for ybctid. Raise error if ybctid is not found.
	 *
	 * Retrieve ybctid from the slot if possible, otherwise generate it
	 * from tuple values.
	 */
	slot = MakeSingleTupleTableSlot(inputTupleDesc, &TTSOpsHeapTuple);
	ExecStoreHeapTuple(tuple, slot, false);
	ybctid = TABLETUPLE_YBCTID(slot) = YBCComputeYBTupleIdFromSlot(rel, slot);
	ExecDropSingleTupleTableSlot(slot);

	if (ybctid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("missing column ybctid in UPDATE request")));

	YBCBindTupleId(update_stmt, ybctid);

	for (int idx = 0; idx < outputTupleDesc->natts; idx++)
	{
		FormData_pg_attribute *att_desc = TupleDescAttr(outputTupleDesc, idx);
		AttrNumber	attnum = att_desc->attnum;
		int32_t		type_id = att_desc->atttypid;
		Datum		d;

		if (attnum == Anum_pg_yb_role_profile_rolprffailedloginattempts)
			d = Int16GetDatum(failed_attempts);
		else if (attnum == Anum_pg_yb_role_profile_rolprfstatus)
			d = CharGetDatum(rolprfstatus);
		else
			continue;

		YbcPgExpr	ybc_expr = YBCNewConstant(update_stmt,
											  type_id,
											  InvalidOid,
											  d,
											  false);

		HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
	}

	/* Execute the statement. */
	int			rows_affected_count = 0;

	YBCExecWriteStmt(update_stmt,
					 rel,
					 &rows_affected_count);

	/* Cleanup. */
	YBCPgDeleteStatement(update_stmt);

	relation_close(rel, AccessShareLock);
	return rows_affected_count > 0;
}

void
YBCExecuteUpdateReplace(Relation rel,
						TupleTableSlot *planSlot,
						TupleTableSlot *slot,
						EState *estate)
{
	YBCExecuteDelete(rel,
					 planSlot,
					 NIL /* returning_columns */ ,
					 true /* target_tuple_fetched */ ,
					 YB_TRANSACTIONAL,
					 false /* changingPart */ ,
					 estate);

	/* Update the tuple with table oid */
	slot->tts_tableOid = RelationGetRelid(rel);

	YBCExecuteInsert(rel,
					 slot,
					 ONCONFLICT_NONE);
}

void
YBCDeleteSysCatalogTuple(Relation rel, HeapTuple tuple)
{
	if (HEAPTUPLE_YBCTID(tuple) == 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("missing column ybctid in DELETE request to Yugabyte database")));

	YbcPgStatement delete_stmt = YbNewDelete(rel, YB_TRANSACTIONAL);

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(delete_stmt, BYTEAOID, InvalidOid,
											 HEAPTUPLE_YBCTID(tuple), false /* is_null */ );

	/* Delete row from foreign key cache */
	YBCPgDeleteFromForeignKeyReferenceCache(YbGetRelfileNodeId(rel), HEAPTUPLE_YBCTID(tuple));

	HandleYBStatus(YBCPgDmlBindColumn(delete_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	/*
	 * Mark tuple for invalidation from system caches at next command
	 * boundary. Do this now so if there is an error with delete we will
	 * re-query to get the correct state from the master.
	 */
	MarkCurrentCommandUsed();
	CacheInvalidateHeapTuple(rel, tuple, NULL);

	YBCApplyWriteStmt(delete_stmt, rel);
}

void
YBCUpdateSysCatalogTuple(Relation rel, HeapTuple oldtuple, HeapTuple tuple)
{
	YBCUpdateSysCatalogTupleForDb(YBCGetDatabaseOid(rel), rel, oldtuple, tuple);
}

void
YBCUpdateSysCatalogTupleForDb(Oid dboid, Relation rel, HeapTuple oldtuple,
							  HeapTuple tuple)
{
	TupleDesc	tupleDesc = RelationGetDescr(rel);
	int			natts = RelationGetNumberOfAttributes(rel);
	YbcPgStatement update_stmt = YbNewUpdateForDb(dboid, rel, YB_TRANSACTIONAL);

	AttrNumber	minattr = YBGetFirstLowInvalidAttributeNumber(rel);
	Bitmapset  *pkey = YBGetTablePrimaryKeyBms(rel);

	/* Bind the ybctid to the statement. */
	Assert(HEAPTUPLE_YBCTID(tuple));
	YBCBindTupleId(update_stmt, HEAPTUPLE_YBCTID(tuple));

	/* Assign values to the non-primary-key columns to update the current row. */
	for (int idx = 0; idx < natts; idx++)
	{
		AttrNumber	attnum = TupleDescAttr(tupleDesc, idx)->attnum;

		/* Skip primary-key columns */
		if (bms_is_member(attnum - minattr, pkey))
		{
			continue;
		}

		bool		is_null = false;
		Datum		d = heap_getattr(tuple, attnum, tupleDesc, &is_null);

		/*
		 * Since we might get this tuple from the catcache, the attributes might
		 * be TOASTed. If so, we need to detoast them before sending them to DocDB.
		 */
		if (!is_null && TupleDescAttr(tupleDesc, idx)->attlen == -1)
		{
			d = PointerGetDatum(PG_DETOAST_DATUM(d));
		}
		/*
		 * Since we are assign values to non-primary-key columns, pass InvalidOid as
		 * collation_id to skip computing collation sortkeys.
		 */
		YbcPgExpr	ybc_expr = YBCNewConstant(update_stmt,
											  TupleDescAttr(tupleDesc, idx)->atttypid,
											  InvalidOid /* collation_id */ ,
											  d,
											  is_null);

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
	YBCApplyWriteStmt(update_stmt, rel);
}

bool
YBCRelInfoHasSecondaryIndices(ResultRelInfo *resultRelInfo)
{
	return (resultRelInfo->ri_NumIndices > 1 ||
			(resultRelInfo->ri_NumIndices == 1 &&
			 !resultRelInfo->ri_IndexRelationDescs[0]->rd_index->indisprimary));
}

int
YBCRelInfoGetSecondaryIndicesCount(ResultRelInfo *resultRelInfo)
{
	int			count = 0;

	for (int i = 0; i < resultRelInfo->ri_NumIndices; i++)
	{
		Relation	index = resultRelInfo->ri_IndexRelationDescs[i];

		if (index->rd_index->indisprimary)
		{
			continue;
		}

		++count;
	}

	return count;
}

/*
 * This code is authored by upstream PG and moved from ExecInsertIndexTuples in
 * order to be reused in various other places by YB.
 */
bool
YbIsPartialIndexPredicateSatisfied(IndexInfo *indexInfo, EState *estate)
{
	ExprState  *predicate;

	/*
	 * If predicate state not set up yet, create it (in the estate's
	 * per-query context)
	 */
	predicate = indexInfo->ii_PredicateState;
	if (predicate == NULL)
	{
		predicate = ExecPrepareQual(indexInfo->ii_Predicate, estate);
		indexInfo->ii_PredicateState = predicate;
	}

	/* Skip this index-update if the predicate isn't satisfied */
	return ExecQual(predicate, GetPerTupleExprContext(estate));
}
