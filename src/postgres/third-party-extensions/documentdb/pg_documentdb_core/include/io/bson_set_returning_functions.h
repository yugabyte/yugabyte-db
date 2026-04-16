/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_set_returning_functions.h
 *
 * Common declarations of functions for handling bson tuple store for functions that return SETOF bson.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_TUPLESTORE_H
#define BSON_TUPLESTORE_H

#include <postgres.h>
#include <utils/builtins.h>
#include <funcapi.h>
#include <miscadmin.h>

Tuplestorestate *SetupBsonTuplestore(PG_FUNCTION_ARGS,
									 TupleDesc *tupleDescriptor);
#endif
