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

#ifndef YBCMODIFYTABLE_H
#define YBCMODIFYTABLE_H

#include "nodes/execnodes.h"
#include "executor/tuptable.h"

extern Oid YBCExecuteInsert(Relation rel, TupleDesc tupleDesc, HeapTuple tuple);

extern void YBCExecuteInsertIndex(Relation rel, Datum *values, bool *isnull, Datum ybctid);

extern void YBCExecuteDelete(Relation rel, ResultRelInfo *resultRelInfo, TupleTableSlot *slot);

extern void YBCExecuteDeleteIndex(Relation index, Datum *values, bool *isnull, Datum ybctid);

extern void YBCDeleteSysCatalogTuple(Relation rel, HeapTuple tuple, Bitmapset *pkey);

extern void YBCExecuteUpdate(Relation rel, ResultRelInfo *resultRelInfo, TupleTableSlot *slot,
							 HeapTuple tuple);

extern void YBCUpdateSysCatalogTuple(Relation rel, HeapTuple tuple);

extern Datum YBCGetYBTupleIdFromSlot(TupleTableSlot *slot);

#endif							/* YBCMODIFYTABLE_H */
