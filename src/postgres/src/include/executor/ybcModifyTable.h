/*--------------------------------------------------------------------------------------------------
 *
 * ybcModifyTable.h
 *	  prototypes for ybcModifyTable.c
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
 * src/include/executor/ybcModifyTable.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#pragma once

#include "nodes/execnodes.h"
#include "executor/tuptable.h"

/**
 * YSQL guc variables that can be used to enable non transactional writes.
 * e.g. 'SET yb_disable_transactional_writes=true'
 * See also the corresponding entries in guc.c.
 */
extern bool yb_disable_transactional_writes;

/**
 * YSQL guc variables that can be used to enable upsert mode for writes.
 * e.g. 'SET yb_enable_upsert_mode=true'
 * See also the corresponding entries in guc.c.
 */
extern bool yb_enable_upsert_mode;

//------------------------------------------------------------------------------
// YugaByte modify table API.

typedef void (*yb_bind_for_write_function) (YBCPgStatement stmt,
											void *indexstate,
											Relation index,
											Datum *values,
											bool *isnull,
											int natts,
											Datum ybbasectid,
											bool ybctid_as_value);

extern void YBCTupleTableInsert(ResultRelInfo  *resultRelInfo,
								TupleTableSlot *slot,
								YBCPgStatement blockInsertStmt, EState *estate);

/*
 * Insert data into YugaByte table.
 * This function is equivalent to "heap_insert", but it sends data to DocDB (YugaByte storage).
 *
 * ybctid argument can be supplied to keep it consistent across shared inserts.
 * If non-zero, it will be used instead of generation, otherwise it will be set
 * to the generated value.
 */
extern void YBCHeapInsert(ResultRelInfo *resultRelInfo,
						  TupleTableSlot *slot,
						  HeapTuple tuple,
						  YBCPgStatement blockInsertStmt,
						  EState *estate);

/*
 * Insert a tuple into a YugaByte table. Will execute within a distributed
 * transaction if the table is transactional (YSQL default).
 *
 * ybctid argument can be supplied to keep it consistent across shared inserts.
 * If non-zero, it will be used instead of generation, otherwise it will be set
 * to the generated value.
 */
extern void YBCExecuteInsert(Relation rel,
							 TupleDesc tupleDesc,
							 HeapTuple tuple,
							 OnConflictAction onConflictAction);
extern void YBCExecuteInsertForDb(Oid dboid,
								  Relation rel,
								  TupleDesc tupleDesc,
								  HeapTuple tuple,
								  OnConflictAction onConflictAction,
								  Datum *ybctid,
								  YBCPgTransactionSetting transaction_setting);

extern void YBCApplyWriteStmt(YBCPgStatement handle, Relation relation);

/*
 * Execute the insert outside of a transaction.
 * Assumes the caller checked that it is safe to do so.
 *
 * ybctid argument can be supplied to keep it consistent across shared inserts.
 * If non-zero, it will be used instead of generation, otherwise it will be set
 * to the generated value.
 */
extern void YBCExecuteNonTxnInsert(Relation rel,
								   TupleDesc tupleDesc,
								   HeapTuple tuple,
								   OnConflictAction onConflictAction);
extern void YBCExecuteNonTxnInsertForDb(Oid dboid,
										Relation rel,
										TupleDesc tupleDesc,
										HeapTuple tuple,
										OnConflictAction onConflictAction,
										Datum *ybctid);

/*
 * Insert a tuple into the an index's backing YugaByte index table.
 */
extern void YBCExecuteInsertIndex(Relation rel,
								  Datum *values,
								  bool *isnull,
								  ItemPointer tid,
								  const uint64_t* backfill_write_time,
								  yb_bind_for_write_function callback,
								  void *indexstate);
extern void YBCExecuteInsertIndexForDb(Oid dboid,
									   Relation rel,
									   Datum* values,
									   bool* isnull,
									   ItemPointer tid,
									   const uint64_t* backfill_write_time,
									   yb_bind_for_write_function callback,
									   void *indexstate);

/*
 * Delete a tuple (identified by ybctid) from a YugaByte table.
 * If this is a single row op we will return false in the case that there was
 * no row to delete. This can occur because we do not first perform a scan if
 * it is a single row op. 'changingPart' indicates if this delete is part of an
 * UPDATE operation on a partitioned table that moves a row from one partition
 * to anoter.
 */
extern bool YBCExecuteDelete(Relation rel,
							 TupleTableSlot *planSlot,
							 List *returning_columns,
							 bool target_tuple_fetched,
							 YBCPgTransactionSetting transaction_setting,
							 bool changingPart,
							 EState *estate);
/*
 * Delete a tuple (identified by index columns and base table ybctid) from an
 * index's backing YugaByte index table.
 */
extern void YBCExecuteDeleteIndex(Relation index,
                                  Datum *values,
                                  bool *isnull,
                                  Datum ybctid,
								  yb_bind_for_write_function callback,
								  void *indexstate);
/*
 * Update a row (identified by ybctid) in a YugaByte table.
 * If this is a single row op we will return false in the case that there was
 * no row to update. This can occur because we do not first perform a scan if
 * it is a single row op.
 */
extern bool YBCExecuteUpdate(ResultRelInfo *resultRelInfo,
							 TupleTableSlot *planSlot,
							 TupleTableSlot *slot,
							 HeapTuple oldtuple,
							 EState *estate,
							 ModifyTable *mt_plan,
							 bool target_tuple_fetched,
							 YBCPgTransactionSetting transaction_setting,
							 Bitmapset *updatedCols,
							 bool canSetTag);

/*
 * Update a row (identified by the roleid) in a pg_yb_role_profile. This is a
 * stripped down and specific version of YBCExecuteUpdate. It is used by
 * auth.c, since the typical method of writing does not work at that stage of
 * the DB initialization.
 *
 * Returns true if a row was updated.
 */
extern bool YBCExecuteUpdateLoginAttempts(Oid roleid,
										  int failed_attempts,
										  char rolprfstatus);
/*
 * Replace a row in a YugaByte table by first deleting an existing row
 * (identified by ybctid) and then inserting a tuple to replace it.
 * This allows us to update a row primary key.
 *
 * This will change ybctid of a row within a tuple.
 */
extern void YBCExecuteUpdateReplace(Relation rel,
								    TupleTableSlot *planSlot,
								    TupleTableSlot *slot,
								    EState *estate);

//------------------------------------------------------------------------------
// System tables modify-table API.
// For system tables we identify rows to update/delete directly by primary key
// and execute them directly (rather than needing to read ybctid first).
// TODO This should be used for regular tables whenever possible.

extern void YBCDeleteSysCatalogTuple(Relation rel, HeapTuple tuple);

extern void YBCUpdateSysCatalogTuple(Relation rel,
                                     HeapTuple oldtuple,
                                     HeapTuple tuple);
extern void YBCUpdateSysCatalogTupleForDb(Oid dboid,
                                          Relation rel,
                                          HeapTuple oldtuple,
                                          HeapTuple tuple);

//------------------------------------------------------------------------------
// Utility methods.

extern bool YBCIsSingleRowTxnCapableRel(ResultRelInfo *resultRelInfo);

/*
 * Checks if the given statement is planned to be executed as a single-row
 * modify transaction.
 */
extern bool YbIsSingleRowModifyTxnPlanned(PlannedStmt *pstmt, EState *estate);

extern Datum YBCGetYBTupleIdFromSlot(TupleTableSlot *slot);

extern Datum YBCGetYBTupleIdFromTuple(Relation rel,
									  HeapTuple tuple,
									  TupleDesc tupleDesc);

/*
 * Returns if a table has secondary indices.
 */
extern bool YBCRelInfoHasSecondaryIndices(ResultRelInfo *resultRelInfo);
