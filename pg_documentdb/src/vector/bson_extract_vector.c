/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/index/bson_extract_vector.c
 *
 * vector operator extensions on BSON.
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <access/reloptions.h>
#include <executor/executor.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#include <utils/array.h>
#include <utils/varlena.h>
#include <parser/parse_coerce.h>
#include <catalog/pg_type.h>
#include <funcapi.h>
#include <lib/stringinfo.h>

#include "utils/helio_errors.h"
#include "io/helio_bson_core.h"
#include "metadata/metadata_cache.h"
#include "vector/bson_extract_vector.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */


PG_FUNCTION_INFO_V1(command_bson_extract_vector);


/*
 * A function that takes a pgbson, a dotted path into the
 * bson and creates a 'vector' found at that path.
 * e.g. SELECT ApiCatalogSchemaName.bson_extract_vector('{ "a": [ 1, 2, 3 ]}', 'a')
 * will create a vector [1, 2, 3 ].
 * Ignores paths with intermediate arrays currently,
 * Ignores paths that do not contain vectors
 * Does not support multiple values or multiple embedded documents currently.
 */
Datum
command_bson_extract_vector(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	text *path = PG_GETARG_TEXT_P(1);

	bool isNull = false;
	Datum result = command_bson_extract_vector_base(document, text_to_cstring(path),
													&isNull);

	if (isNull)
	{
		PG_RETURN_NULL();
	}
	else
	{
		PG_RETURN_DATUM(result);
	}
}


Datum
command_bson_extract_vector_base(pgbson *document, char *pathStr, bool *isNull)
{
	bson_iter_t documentIter;
	if (!PgbsonInitIteratorAtPath(document, pathStr, &documentIter))
	{
		/* We ignore nested arrays for the path */
		*isNull = true;
		return (Datum) 0;
	}

	if (bson_iter_type(&documentIter) != BSON_TYPE_ARRAY)
	{
		/* We ignore non leaf arrays for the path */
		*isNull = true;
		return (Datum) 0;
	}

	bson_iter_recurse(&documentIter, &documentIter);

	bson_iter_t currentArrayIter = documentIter;

	/* First pass, count elements */
	int32_t numElements = 0;
	bool isAllNumeric = true;
	while (bson_iter_next(&currentArrayIter))
	{
		numElements++;
		if (!BsonValueIsNumber(bson_iter_value(&currentArrayIter)))
		{
			isAllNumeric = false;
		}
	}

	if (!isAllNumeric || numElements == 0)
	{
		/* We ignore invalid arrays for the path */
		*isNull = true;
		return (Datum) 0;
	}

	Datum *floatDatumArray = palloc(sizeof(Datum) * numElements);

	int i = 0;
	currentArrayIter = documentIter;
	while (bson_iter_next(&currentArrayIter))
	{
		floatDatumArray[i] = Float8GetDatum(BsonValueAsDouble(bson_iter_value(
																  &currentArrayIter)));
		i++;
	}

	bool elmbyval = true;
	ArrayType *array = construct_array(floatDatumArray, numElements, FLOAT8OID,
									   sizeof(double),
									   elmbyval, TYPALIGN_INT);

	return OidFunctionCall3Coll(
		PgDoubleToVectorFunctionOid(),
		InvalidOid, PointerGetDatum(array), Int32GetDatum(-1),
		BoolGetDatum(false));
}
