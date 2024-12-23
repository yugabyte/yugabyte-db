/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/geospatial/bson_extract_geospatial.c
 *
 * Geospatial data/extraction support for BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <math.h>
#include <miscadmin.h>
#include "utils/array.h"
#include "utils/builtins.h"

#include "utils/helio_errors.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_private.h"
#include "utils/list_utils.h"

PG_FUNCTION_INFO_V1(bson_extract_geometry);
PG_FUNCTION_INFO_V1(bson_extract_geometry_array);
PG_FUNCTION_INFO_V1(bson_extract_geometry_runtime);
PG_FUNCTION_INFO_V1(bson_validate_geometry);
PG_FUNCTION_INFO_V1(bson_validate_geography);

/*
 * A function that takes a pgbson, a dotted path into the
 * bson and creates a 'geometry' geospatial type based on the value found at the path
 *
 * This function uses a `strict` mode to do additional validations and throw error for cases
 * like generating geometry term for invalid input
 */
Datum
bson_extract_geometry(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	text *path = PG_GETARG_TEXT_P(1);
	StringView pathView = CreateStringViewFromText(path);
	Datum geometryDatum = BsonExtractGeometryStrict(document, &pathView);
	if (geometryDatum == (Datum) 0)
	{
		PG_RETURN_NULL();
	}

	PG_FREE_IF_COPY(document, 0);
	PG_RETURN_DATUM(geometryDatum);
}


Datum
bson_extract_geometry_array(PG_FUNCTION_ARGS)
{
	/* noop, To be removed when we are in 1.11 along with all the references in 1.9 sqls */
	PG_RETURN_ARRAYTYPE_P(construct_empty_array(GeometryTypeId()));
}


/*
 * A function that takes a pgbson, a dotted path into the
 * bson and returns the all the valid points found at the path as a geometry array.
 *
 * Note: For invalid case this function returns empty array,
 * this function currently serves as a test function which shows how non-strict
 * validation is treated
 */
Datum
bson_extract_geometry_runtime(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	text *path = PG_GETARG_TEXT_P(1);
	StringView pathView = CreateStringViewFromText(path);
	Datum geometryRuntimeDatum = BsonExtractGeometryRuntime(document, &pathView);
	if (geometryRuntimeDatum == (Datum) 0)
	{
		PG_RETURN_NULL();
	}
	PG_RETURN_DATUM(geometryRuntimeDatum);
}


/*
 * bson_validate_geometry is used to validate the geometries in the `document` at
 * the given path. This works like a bloom filter which either tells if geometry is not present
 * at all or any invalid/valid geometry is present.
 *
 * As of now there are 2 applications of this function:
 *
 * 1- Make the 2d index sparse
 * ===========================
 * This function is used as predicate in "2d index" CREATE INDEX statement like this:
 * CREATE INDEX ... WHERE bson_validate_geometry(document, 'a') IS NOT NULL;
 *
 * Anytime there is no valid geometry found at the given `path` then we should not create index terms for
 * the document.
 *
 * 2- Use with geospatial operators
 * ================================
 * This function is also used in geospatial query operators to match the index predicate.
 * e.g. $geoWithin mongo operator is converted like this in our planner
 * query => {a: {$geoWithin: {$box: [[10, 10], [20, 20]]}}}
 * planner => bson_validate_geometry(document, 'a') @|-| {a: {$box: [[10, 10], [20, 20]]}}
 *
 *
 * This function returns NULL when there is surely no geometries at path and the document is valid for insertion
 * even with index. e.g. {a: "No point"} or {a: {}} or {a: []}
 * otherwise it just returns the original document
 */
Datum
bson_validate_geometry(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	text *path = PG_GETARG_TEXT_P(1);
	StringView pathView = CreateStringViewFromText(path);

	/*
	 * There are couple of interesting cases with mongo
	 * e.g.
	 * doc -> { a: [[10, 10], ["invalid"], [20, 20]] }
	 * query -> {a: {$geoWithin: {$box: [[10, 10], [15, 15]]}}}
	 *
	 * Let's consider 2 cases where one case has 2d index on `a` and other doesn't
	 *
	 * 1- With index => This case will not allow the document to be inserted and throws an error
	 * at insert time.
	 *
	 * 2- Without index => This doesn't throw any error because there are no constraints plus the query
	 * actually matched the document, and it only considers whatever partial geometries it could find there
	 *
	 * So to match this we need to only fail when it is index case therefore we need to avoid throwing error from this
	 * function and that is delegated to `bson_gist_geometry_2d_compress` where actually the index terms are generated
	 * and document is processed with strict validation rules
	 */

	ProcessCommonGeospatialState state;
	InitProcessCommonGeospatialState(&state, GeospatialValidationLevel_BloomFilter,
									 GeospatialType_Geometry, NULL);
	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);

	BsonIterGetLegacyGeometryPoints(&documentIterator,
									&pathView,
									&state);

	/* When no error and no geometries this really means ignore this document for term generation but a valid insertion */
	if (state.isEmpty)
	{
		/* No valid regions found return NULL */
		PG_RETURN_NULL();
	}

	/* Otherwise return the original document */
	PG_RETURN_POINTER(document);
}


/*
 * A similar function to `bson_validate_geometry` for 2d sphere geography based indexes
 * which acts as a bloom filter for 2d sphere index
 */
Datum
bson_validate_geography(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	text *path = PG_GETARG_TEXT_P(1);
	StringView pathView = CreateStringViewFromText(path);

	ProcessCommonGeospatialState state;
	InitProcessCommonGeospatialState(&state, GeospatialValidationLevel_BloomFilter,
									 GeospatialType_Geography, NULL);
	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);

	BsonIterValidateGeographies(&documentIterator, &pathView, &state);

	/* When no error and no geometries this really means ignore this document for term generation but a valid insertion */
	if (state.isEmpty)
	{
		/* No valid regions found return NULL */
		PG_RETURN_NULL();
	}

	/* Otherwise return the original document */
	PG_RETURN_POINTER(document);
}
