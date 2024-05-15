/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_set_returning_functions.c
 *
 * Implementation to setup bson tuple store for functions that return SETOF bson.
 *
 *-------------------------------------------------------------------------
 */

#include "io/bson_set_returning_functions.h"
#include "utils/type_cache.h"

/*
 * SetupBsonTuplestore sets up a BSON tuple store for set-returning functions that return
 * SETOF bson.
 */
Tuplestorestate *
SetupBsonTuplestore(PG_FUNCTION_ARGS, TupleDesc *resultDescriptor)
{
	/* Define the type of the result tuplestore: a single bson column */
	TupleDesc tupleDescriptor = CreateTemplateTupleDesc(1);
	TupleDescInitEntry(tupleDescriptor,
					   (AttrNumber) 1,
					   NULL, /* Attribute Name */
					   BsonTypeId(),
					   -1, /* Attribute type modifier. Some data types need it, e.g., varchar to specify max length. */
					   0 /* Number of dimensions of the column. 0 for non-array */);

	ReturnSetInfo *resultSet = (ReturnSetInfo *) fcinfo->resultinfo;

	MemoryContext perQueryContext = resultSet->econtext->ecxt_per_query_memory;
	MemoryContext oldContext = MemoryContextSwitchTo(perQueryContext);

	bool randomAccess = false; /* if forward/backward access of the tupleStore allowed */
	bool interXact = false; /* Whether the files used for on-disk storage persist beyond the end of the current transaction. */
	Tuplestorestate *tupleStore = tuplestore_begin_heap(
		randomAccess,
		interXact,
		work_mem /*  how much data to store in memory in KBytes. work_mem is the default recommended by Postgres*/);
	resultSet->returnMode = SFRM_Materialize; /* result set instantiated in Tuplestore */
	resultSet->setResult = tupleStore;
	resultSet->setDesc = tupleDescriptor;

	*resultDescriptor = tupleDescriptor;

	MemoryContextSwitchTo(oldContext);
	return tupleStore;
}
