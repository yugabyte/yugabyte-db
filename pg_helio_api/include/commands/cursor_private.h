/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/cursor_private.h
 *
 * Private declarations of functions and types shared between
 * cursors.c and aggregation_cursors.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef CURSOR_PRIVATE_H
#define CURSOR_PRIVATE_H

bool DrainStreamingQuery(HTAB *cursorMap, Query *query, int batchSize,
						 int32_t *numIterations, uint32_t accumulatedSize,
						 pgbson_array_writer *arrayWriters);
bool DrainTailableQuery(HTAB *cursorMap, Query *query, int batchSize,
						int32_t *numIterations, uint32_t accumulatedSize,
						pgbson_array_writer *arrayWriter);
bool CreateAndDrainPersistedQuery(const char *cursorName, Query *query,
								  int batchSize, int32_t *numIterations, uint32_t
								  accumulatedSize,
								  pgbson_array_writer *arrayWriter, bool isHoldCursor,
								  bool closeCursor);
bool DrainPersistedCursor(const char *cursorName, int batchSize,
						  int32_t *numIterations, uint32_t accumulatedSize,
						  pgbson_array_writer *arrayWriter);

Datum PostProcessCursorPage(PG_FUNCTION_ARGS,
							pgbson_writer *cursorDoc,
							pgbson_array_writer *arrayWriter,
							pgbson_writer *topLevelWriter, int64_t cursorId,
							pgbson *continuation, bool persistConnection);

HTAB * CreateCursorHashSet(void);
HTAB * CreateTailableCursorHashSet(void);
void BuildContinuationMap(pgbson *continuationValue, HTAB *cursorMap);
void BuildTailableCursorContinuationMap(pgbson *continuationValue, HTAB *cursorMap);
void SerializeContinuationsToWriter(pgbson_writer *writer, HTAB *cursorMap);
void SerializeTailableContinuationsToWriter(pgbson_writer *writer, HTAB *cursorMap);

pgbson * DrainSingleResultQuery(Query *query);


void SetupCursorPagePreamble(pgbson_writer *topLevelWriter,
							 pgbson_writer *cursorDoc,
							 pgbson_array_writer *arrayWriter, int64_t cursorId,
							 const char *namespaceName, bool isFirstPage,
							 uint32_t *accumulatedLength);

#endif
