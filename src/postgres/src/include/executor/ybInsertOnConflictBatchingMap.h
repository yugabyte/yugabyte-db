/*-------------------------------------------------------------------------
 *
 * ybInsertOnConflictBatchingMap.c
 *	  Hashmap used during INSERT ON CONFLICT batching.
 *
 * Copyright (c) YugabyteDB, Inc.
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "executor/tuptable.h"

extern struct yb_insert_on_conflict_batching_hash *
YbInsertOnConflictBatchingMapCreate(MemoryContext metacxt,
									uint32 size,
									TupleDesc tupdesc);

extern void
YbInsertOnConflictBatchingMapInsert(struct yb_insert_on_conflict_batching_hash *tb,
									int nkeys,
									Datum *values,
									bool *isnull,
									TupleTableSlot *slot);

extern bool
YbInsertOnConflictBatchingMapLookup(struct yb_insert_on_conflict_batching_hash *tb,
									int nkeys,
									Datum *values,
									bool *isnull,
									TupleTableSlot **slot);

extern void
YbInsertOnConflictBatchingMapDelete(struct yb_insert_on_conflict_batching_hash *tb,
									int nkeys,
									Datum *values,
									bool *isnull);

void
YbInsertOnConflictBatchingMapDestroy(struct yb_insert_on_conflict_batching_hash *tb);
