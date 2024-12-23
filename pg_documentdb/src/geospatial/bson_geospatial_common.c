/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/geospatial/bson_geospatial_common.c
 *
 * Implementation for method interacting with PostGIS
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "math.h"
#include "utils/builtins.h"

#include "utils/helio_errors.h"
#include "geospatial/bson_geojson_utils.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_private.h"
#include "utils/list_utils.h"
#include "io/bson_traversal.h"

#define IsIndexValidation(s) (s->validationLevel == GeospatialValidationLevel_Index)
#define IsBloomValidation(s) (s->validationLevel == GeospatialValidationLevel_BloomFilter)
#define IsRuntimeValidation(s) (s->validationLevel == GeospatialValidationLevel_Runtime)

#define IsIndexMultiKey(s) (IsIndexValidation(s) && s->isMultiKeyContext == true)


/*
 * Types used to denote point pair values as below while processing
 * this is helpful for error reporting
 */
typedef enum PointProcessType
{
	/*
	 * Denotes if the value is not a valid number to define a point.
	 * e.g. [10, 'invalid'] => [PointProcessType_Valid, PointProcessType_Invalid]
	 */
	PointProcessType_Invalid = 0,

	/*
	 * Denotes if the value is an empty object or array which defines the point.
	 * e.g. [10, []] or [10, {}] => [PointProcessType_Valid, PointProcessType_Empty]
	 */
	PointProcessType_Empty,

	/*
	 * Denotes if the value is a valid number to define a point.
	 * e.g. [10, 20] => [PointProcessType_Valid, PointProcessType_Valid]
	 */
	PointProcessType_Valid,
} PointProcessType;

/*
 *=====================================================
 * Forward declarations here
 * ====================================================
 */
static bool LegacyPointVisitTopLevelField(pgbsonelement *element, const
										  StringView *filterPath,
										  void *state);
static bool GeographyVisitTopLevelField(pgbsonelement *element, const
										StringView *filterPath,
										void *state);
static bool GeographyValidateTopLevelField(pgbsonelement *element, const
										   StringView *filterPath,
										   void *state);
static bool ContinueProcessIntermediateArray(void *state, const
											 bson_value_t *value);
static bool BsonValueAddLegacyPointDatum(const bson_value_t *value,
										 ProcessCommonGeospatialState *state,
										 bool *isNull);
static bool BsonValueParseAsLegacyPoint2d(const bson_value_t *value,
										  ProcessCommonGeospatialState *state);
static const char * _2dsphereIndexErrorPrefix(const pgbson *document);
static const char * _2dsphereIndexErrorHintPrefix(const pgbson *document);
static const char * _2dIndexNoPrefix(const pgbson *document);
static Datum BsonExtractGeospatialInternal(const pgbson *document,
										   const StringView *pathView,
										   GeospatialType type,
										   GeospatialValidationLevel level,
										   WKBGeometryType collectType,
										   GeospatialErrorContext *errCtxt);
static void SetQueryMatcherResult(ProcessCommonGeospatialState *state);

/*
 * Execution function while extracting the coordinates using legacy point system
 */
static const TraverseBsonExecutionFuncs ProcessLegacyCoordinates = {
	.ContinueProcessIntermediateArray = ContinueProcessIntermediateArray,
	.SetTraverseResult = NULL,
	.VisitArrayField = NULL,
	.VisitTopLevelField = LegacyPointVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL
};

/*
 * Execution function while extracting the geographies from document
 */
static const TraverseBsonExecutionFuncs ProcessGeography = {
	.ContinueProcessIntermediateArray = ContinueProcessIntermediateArray,
	.SetTraverseResult = NULL,
	.VisitArrayField = NULL,
	.VisitTopLevelField = GeographyVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL
};


/*
 * Execution function while extracting the geographies from document
 */
static const TraverseBsonExecutionFuncs ValidateGeography = {
	.ContinueProcessIntermediateArray = ContinueProcessIntermediateArray,
	.SetTraverseResult = NULL,
	.VisitArrayField = NULL,
	.VisitTopLevelField = GeographyValidateTopLevelField,
	.SetIntermediateArrayIndex = NULL
};


/*
 * Callers must ensure this is called only if a valid shape is found.
 * This function increments the total count of valid shapes found
 * and runs the matcher function in case of GeospatialValidationLevel_Runtime
 * and returns the result from the matcher function.
 * Note: For other GeospatialValidationLevel or when runtime matcher doesn't exist,
 * this function returns `false` which the caller should take into consideration
 */
static inline bool
UpdateStateAndRunMatcherIfValidPointFound(ProcessCommonGeospatialState *state)
{
	state->isEmpty = false;
	state->total++;
	if (IsRuntimeValidation(state) && state->runtimeMatcher.matcherFunc != NULL)
	{
		SetQueryMatcherResult(state);
		return state->runtimeMatcher.isMatched;
	}

	return false;
}


/*
 * BsonIterGetLegacyGeometryPoints function parses the values and given documentIter in
 * accordance with legacy coordinate point system and fills the list with all geometries found
 * in `state->regions` found at `keyPathView`
 *
 * According to the legacy coordinate system points can be defined as:
 * ==================================
 * 1- location: [longitude, latitude, ...]
 * where value is a <longitude, latitude> pair in an array.
 * Note* - More than 2 values are ignored
 *
 * =================================
 * 2- location: { field1: <long>, field2: <lat>, ... }
 * Object with first 2 fields containing <long, lat> pairs.
 * Note* - Fields names and more than 2 fields are ignored.
 *
 * ==================================
 * 3- location: [[long, lat, ...], ...] or [{field1: <long>, field2: <lat>, ...}, ...]
 * Array of multiple legacy coordinate points, the first 2 values of each point are used.
 * This represents a set of individual points.
 */
void
BsonIterGetLegacyGeometryPoints(bson_iter_t *documentIter,
								const StringView *keyPathView,
								ProcessCommonGeospatialState *state)
{
	Assert(documentIter != NULL && keyPathView != NULL &&
		   state != NULL && state->geospatialType == GeospatialType_Geometry &&
		   state->validationLevel != GeospatialValidationLevel_Unknown);
	TraverseBsonPathStringView(documentIter, keyPathView, state,
							   &ProcessLegacyCoordinates);
}


/*
 * BsonIterGetGeographies function parses the values and given documentIter and returns the
 * spherical geographies or multi key geographies from at "keyPathView" field.
 * The geographies are expected to be defined in GeoJSON format
 */
void
BsonIterGetGeographies(bson_iter_t *documentIter, const StringView *keyPathView,
					   ProcessCommonGeospatialState *state)
{
	Assert(documentIter != NULL && keyPathView != NULL &&
		   state != NULL && state->geospatialType == GeospatialType_Geography &&
		   (IsIndexValidation(state) || IsRuntimeValidation(state)));
	TraverseBsonPathStringView(documentIter, keyPathView, state,
							   &ProcessGeography);
}


/*
 * BsonIterValidateGeographies function validates the document at path "keyPathView" field
 * for potential geographies so that the document is further processed.
 *
 * The function is suppose to only look for any first valid geography (in case of multi key value)
 *  and returns after that
 */
void
BsonIterValidateGeographies(bson_iter_t *documentIter, const StringView *keyPathView,
							ProcessCommonGeospatialState *state)
{
	Assert(documentIter != NULL && keyPathView != NULL &&
		   state != NULL && state->geospatialType == GeospatialType_Geography &&
		   IsBloomValidation(state));
	TraverseBsonPathStringView(documentIter, keyPathView, state,
							   &ValidateGeography);
}


/*
 * Extracts the 2d geometry from the document at path `pathView` with strict
 * validation for GIST index term generation.
 *
 * Returns a single geometry always, if there are multi key geometries then
 * creates a super geometery as a `Collection` i.e. Multipoints
 *
 * Returns NULL Datum in case no valid geometries can be formed
 */
Datum
BsonExtractGeometryStrict(const pgbson *document, const StringView *pathView)
{
	GeospatialType type = GeospatialType_Geometry;
	GeospatialValidationLevel level = GeospatialValidationLevel_Index;
	WKBGeometryType collectType = WKBGeometryType_MultiPoint;

	GeospatialErrorContext errCtxt = {
		.document = document,
		.errCode = ERRCODE_HELIO_BADVALUE,
		.errPrefix = _2dIndexNoPrefix,
		.hintPrefix = _2dIndexNoPrefix
	};

	return BsonExtractGeospatialInternal(document, pathView, type, level, collectType,
										 &errCtxt);
}


/*
 * Extracts the 2d geometry from the document at path `pathView` runtime evaluation
 * of queries
 *
 * Returns a single geometry always, if there are multi key geometries then
 * creates a super geometery as a `Collection` i.e. Multipoints
 *
 * Returns NULL Datum in case no valid geometries can be formed
 */
Datum
BsonExtractGeometryRuntime(const pgbson *document, const StringView *pathView)
{
	GeospatialType type = GeospatialType_Geometry;
	GeospatialValidationLevel level = GeospatialValidationLevel_Runtime;
	WKBGeometryType collectType = WKBGeometryType_MultiPoint;

	/*
	 * For runtime the error ctxt is not used, we don't need to throw invalidation errors during runtime
	 */
	return BsonExtractGeospatialInternal(document, pathView, type, level, collectType,
										 NULL);
}


/*
 * Extracts the geographies from the document at path `pathView` with strict
 * validation for GIST index term generation.
 *
 * Returns a single geography always, if there are multi key geographies then
 * collects all the geographies as a single `GeometryCollection`
 *
 * Returns NULL Datum in case no valid geographies can be formed
 */
Datum
BsonExtractGeographyStrict(const pgbson *document, const StringView *pathView)
{
	GeospatialType type = GeospatialType_Geography;
	GeospatialValidationLevel level = GeospatialValidationLevel_Index;
	WKBGeometryType collectType = WKBGeometryType_GeometryCollection;

	GeospatialErrorContext errCtxt = {
		.document = document,
		.errCode = ERRCODE_HELIO_LOCATION16755,
		.errPrefix = _2dsphereIndexErrorPrefix,
		.hintPrefix = _2dsphereIndexErrorHintPrefix
	};

	return BsonExtractGeospatialInternal(document, pathView, type, level, collectType,
										 &errCtxt);
}


/*
 * Extracts the geographies from the document at path `pathView` runtime evaluation
 * of queries
 *
 * Returns a single geography always, if there are multi key geographies then
 * collects all the geographies as a single `GeometryCollection`
 *
 * Returns NULL Datum in case no valid geographies can be formed
 */
Datum
BsonExtractGeographyRuntime(const pgbson *document, const StringView *pathView)
{
	GeospatialType type = GeospatialType_Geography;
	GeospatialValidationLevel level = GeospatialValidationLevel_Runtime;
	WKBGeometryType collectType = WKBGeometryType_GeometryCollection;

	/*
	 * For runtime the error ctxt is not used, we don't need to throw invalidation errors during runtime
	 */
	return BsonExtractGeospatialInternal(document, pathView, type, level, collectType,
										 NULL);
}


/*==========================*/
/* Private Helper Utilities */
/*==========================*/

/*
 * Helper method to run a query matcher function against the values extracted from document and
 * a query, Set the result in the state
 */
static void
SetQueryMatcherResult(ProcessCommonGeospatialState *state)
{
	bool isMatched = state->runtimeMatcher.matcherFunc(state, state->WKBBuffer);
	state->runtimeMatcher.isMatched = isMatched;

	if (!isMatched)
	{
		/* Reset the buffer for next multikey value */
		DeepFreeWKB(state->WKBBuffer);
		state->WKBBuffer = makeStringInfo();
	}
}


/*
 * Process the top level field to extract 2d geometry points from the bson value
 * and add them to the state->points list.
 *
 * Returns whether or not we should continue after visiting top fields,
 * we return true so that traversal result is updated correctly.
 */
static bool
LegacyPointVisitTopLevelField(pgbsonelement *element, const StringView *filterPath,
							  void *state)
{
	ProcessCommonGeospatialState *processState = (ProcessCommonGeospatialState *) state;
	bson_value_t *value = &element->bsonValue;
	if (value->value_type != BSON_TYPE_ARRAY && value->value_type != BSON_TYPE_DOCUMENT)
	{
		/* Not an array or object, so we can't parse it as a point */
		return false;
	}
	else if (IsBsonValueEmptyArray(value) || IsBsonValueEmptyDocument(value))
	{
		/* Empty array or object */
		return false;
	}

	bool isValid = false;

	/* Check if first value is again array or object inside an array, then treat this as multipoint array */
	bson_iter_t iter;
	BsonValueInitIterator(value, &iter);
	bson_iter_next(&iter);
	const bson_value_t *firstValue = bson_iter_value(&iter);

	if (value->value_type == BSON_TYPE_ARRAY &&
		(firstValue->value_type == BSON_TYPE_ARRAY ||
		 firstValue->value_type == BSON_TYPE_DOCUMENT))
	{
		processState->isMultiKeyContext = true;
		BsonValueInitIterator(value, &iter);
		while (bson_iter_next(&iter))
		{
			const bson_value_t *nestedValue = bson_iter_value(&iter);
			isValid = BsonValueParseAsLegacyPoint2d(nestedValue, processState);

			/* If we have runtime matcher then run the match against each value individually */
			if (isValid)
			{
				/*
				 * Don't need to set total and run matcher in case of bloom filter.
				 * We can return as soon as 1 valid shape is found on path.
				 */
				if (IsBloomValidation(processState))
				{
					return false;
				}

				bool isMatched = UpdateStateAndRunMatcherIfValidPointFound(processState);
				if (isMatched)
				{
					return false;
				}
			}
		}
	}
	else
	{
		isValid = BsonValueParseAsLegacyPoint2d(value, processState);
		if (isValid)
		{
			UpdateStateAndRunMatcherIfValidPointFound(processState);
		}
	}

	return false;
}


/*
 * BsonValueParseAsLegacyPoint2d parses the given value to be legacy point following
 * 2d null rules. e.g. {a: "Text"} or {a: []} or {a: [{}, {}]} all are null geometry cases
 * (which is weird but need to follow the pattern).
 *
 * For non-index scenarios this method also considers GeoJSON points to be valid points.
 */
static bool
BsonValueParseAsLegacyPoint2d(const bson_value_t *value,
							  ProcessCommonGeospatialState *state)
{
	/* This is a single point case, parse these normally */
	bool isValid = false;
	bool throwError = IsIndexValidation(state);
	bool isNull = false;

	isValid = BsonValueAddLegacyPointDatum(value, state, &isNull);
	if (!isValid)
	{
		/* If not valid then try to parse this as GeoJson Point for runtime */
		if (!IsIndexValidation(state) && value->value_type == BSON_TYPE_DOCUMENT &&
			!isNull)
		{
			GeometryParseFlag parseFlag = ParseFlag_GeoJSON_Point;

			GeoJsonParseState parseState;
			memset(&parseState, 0, sizeof(GeoJsonParseState));

			parseState.shouldThrowValidityError = throwError;
			parseState.buffer = state->WKBBuffer;

			isValid = BsonValueGetGeometryWKB(value, parseFlag, &parseState);
		}

		if (IsBloomValidation(state) && !isValid && !isNull)
		{
			state->isEmpty = false;
			return false;
		}
	}

	return isValid;
}


/*
 * BsonValueAddLegacyPointDatum adds the WKB (if required ) of coordinate point from the given value
 * into the state buffer
 *
 * If the point is not a valid coordinate pair then throws an error for index.
 */
static bool
BsonValueAddLegacyPointDatum(const bson_value_t *value,
							 ProcessCommonGeospatialState *state,
							 bool *isNull)
{
	bool throwError = IsIndexValidation(state);

	if (value->value_type != BSON_TYPE_ARRAY &&
		value->value_type != BSON_TYPE_DOCUMENT)
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			throwError, (
				errcode(ERRCODE_HELIO_LOCATION16804),
				errmsg("location object expected, location array not in correct format"),
				errdetail_log(
					"location object expected, location array not in correct format")));
	}

	if (IsBsonValueEmptyArray(value) || IsBsonValueEmptyDocument(value))
	{
		/*
		 * Ignore these, without error, null cases again
		 * e.g. { loc: [] }, { loc: {} }
		 */
		*isNull = true;
		return false;
	}

	bson_iter_t valueIter;
	BsonValueInitIterator(value, &valueIter);

	int index = 0;
	Point point;
	memset(&point, 0, sizeof(Point));

	bool isValid = false;
	PointProcessType validCoordinates[2] = {
		PointProcessType_Empty, PointProcessType_Empty
	};

	/* Ignore values other than first 2 */
	while (index < 2 && bson_iter_next(&valueIter))
	{
		const bson_value_t *arrValue = bson_iter_value(&valueIter);
		if (IsBsonValueEmptyArray(arrValue) || IsBsonValueEmptyDocument(arrValue))
		{
			index++;
			continue;
		}
		if (!BsonTypeIsNumber(arrValue->value_type))
		{
			validCoordinates[index] = PointProcessType_Invalid;
			index++;
			continue;
		}

		double degrees = BsonValueAsDouble(arrValue);

		if (index == 0)
		{
			point.x = degrees;
		}
		else
		{
			point.y = degrees;
		}
		validCoordinates[index] = PointProcessType_Valid;
		index++;
	}

	/* Null point cases without error */
	/* 1. if single point with both values as empty array or object, e.g. { a: {f1: [], f2: {}}} */
	if (!state->isMultiKeyContext && validCoordinates[0] == PointProcessType_Empty &&
		validCoordinates[1] == PointProcessType_Empty)
	{
		*isNull = true;
		return false;
	}

	if (throwError)
	{
		/* Strictly validate here */
		if (index == 1 && validCoordinates[0] == PointProcessType_Valid)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION13068),
							errmsg("geo field only has 1 element"),
							errdetail_log("geo field only has 1 element")));
		}

		/* If any point is invalid do further checks */
		if (validCoordinates[0] != PointProcessType_Valid ||
			validCoordinates[1] != PointProcessType_Valid)
		{
			/* Throw this if invalid points for multipoint or only first point of single point pair is valid */
			if (state->isMultiKeyContext ||
				(!state->isMultiKeyContext &&
				 validCoordinates[0] == PointProcessType_Valid))
			{
				ereport(ERROR, (
							errcode(ERRCODE_HELIO_LOCATION13026),
							errmsg("geo values must be "
								   "'legacy coordinate pairs' for 2d indexes"),
							errdetail_log("geo values must be "
										  "'legacy coordinate pairs' for 2d indexes")));
			}

			/* For any other single point validation this error if any invalid point.
			 * e.g.
			 * {a : {loc1: {}, loc: 2}} => throws this
			 */
			if (!state->isMultiKeyContext &&
				(validCoordinates[0] != PointProcessType_Valid ||
				 validCoordinates[1] != PointProcessType_Valid))
			{
				ereport(ERROR, (
							errcode(ERRCODE_HELIO_LOCATION16804),
							errmsg("location object expected, location array "
								   "not in correct format"),
							errdetail_log("location object expected, location array "
										  "not in correct format")));
			}
		}
	}

	isValid = validCoordinates[0] == PointProcessType_Valid &&
			  validCoordinates[1] == PointProcessType_Valid;
	if (!isValid)
	{
		return false;
	}

	/* We made it this far means that there is atleast one non-null geometries */
	state->isEmpty = false;
	if (IsBloomValidation(state))
	{
		/* Don't convert to actual geometries if only validation is required */
		return true;
	}

	/* Write the point wkb buffer */
	WriteHeaderToWKBBuffer(state->WKBBuffer, WKBGeometryType_Point);
	WritePointToWKBBuffer(state->WKBBuffer, &point);
	return isValid;
}


/*
 * Continue always for intermediate arrays to find all possible points
 */
static bool
ContinueProcessIntermediateArray(void *state, const bson_value_t *value)
{
	ProcessCommonGeospatialState *processState = (ProcessCommonGeospatialState *) state;

	/* If we encounter in between array, assume it is multikey */
	processState->isMultiKeyContext = true;

	/* Also check if we need to continue in case of bloom validation */
	if (IsBloomValidation(processState) && !processState->isEmpty)
	{
		return false;
	}

	if (IsRuntimeValidation(processState) && processState->runtimeMatcher.isMatched)
	{
		return false;
	}

	return true;
}


/*
 * GeographyValidateTopLevelField validates if a value is a potential geography or not.
 * This is a separate function as opposed to `2d` or `geometry` because null cases are simpler here.
 * If the field is not present only in that case the geography is treated to be null.
 */
static bool
GeographyValidateTopLevelField(pgbsonelement *element, const
							   StringView *filterPath,
							   void *state)
{
	ProcessCommonGeospatialState *processState = (ProcessCommonGeospatialState *) state;
	bson_value_t *value = &element->bsonValue;

	if (value->value_type == BSON_TYPE_NULL || value->value_type == BSON_TYPE_UNDEFINED ||
		IsBsonValueEmptyArray(value) || IsBsonValueEmptyDocument(value))
	{
		/* These values are treated as sparse values
		 * { a: null}, {a: undefined}, {a: []}, {a: {}}
		 */
		return false;
	}

	/* Any other cases can be treated to be at least valid potential geography value */
	processState->isEmpty = false;

	return false;
}


/*
 *  GeographyVisitTopLevelField visits the "filterPath" in the document and tries
 * to parse it as known GeoJson format geodetic datum
 */
static bool
GeographyVisitTopLevelField(pgbsonelement *element, const
							StringView *filterPath,
							void *state)
{
	ProcessCommonGeospatialState *processState = (ProcessCommonGeospatialState *) state;

	bson_value_t *value = &element->bsonValue;
	bool throwError = IsIndexValidation(processState);

	/* If the top level field is not array or object, it's just not valid */
	if (value->value_type != BSON_TYPE_ARRAY && value->value_type != BSON_TYPE_DOCUMENT)
	{
		RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
			throwError, (
				errcode(GEO_ERROR_CODE(processState->errorCtxt)),
				errmsg("%sgeo element must be an array or object: %s : %s",
					   GEO_ERROR_PREFIX(processState->errorCtxt),
					   element->path, BsonValueToJsonForLogging(value)),
				errdetail_log("%sgeo element must be an array or object but found: %s",
							  GEO_HINT_PREFIX(processState->errorCtxt),
							  BsonTypeName(value->value_type))));
	}

	/*
	 * First parse this as Legacy
	 * Second if not successful Try to parse this as GeoJson, some mongo validations are just ignored by Postigs,
	 * we have our custom parser to accomodate these.
	 * e.g of such cases:
	 * 1- [10, "text"] - Point like this is treated as valid in Postgis after converting "text" to 0.
	 *    In this case JSON-C library sets the "errno" to EINVAL, but it all happens in Postgis space
	 *    and later overrided by other errors or no errors.
	 *    Ref: https://json-c.github.io/json-c/json-c-0.10/doc/html/json__object_8h.html
	 *
	 * 2- GeoJson type "Point" can also be defined as a document {x: 10, y: 10} which is not GeoJson standard
	 *    but mongo supports it
	 *
	 * 3- Bson document can have different number types e.g. Decimal which will not be recognized by Postgis in
	 *    Json format
	 */
	GeometryParseFlag parseFlag = ParseFlag_None;
	bool isValid = false;
	GeoJsonParseState parseState;
	memset(&parseState, 0, sizeof(GeoJsonParseState));
	parseState.shouldThrowValidityError = throwError;
	parseState.buffer = processState->WKBBuffer;
	parseState.errorCtxt = processState->errorCtxt;

	if (value->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t iter;
		BsonValueInitIterator(value, &iter);
		parseFlag = parseFlag | ParseFlag_Legacy;
		if (!bson_iter_next(&iter))
		{
			return false;
		}

		/*
		 * Check if first value is again array or object inside an array, then treat this as multipoint array.
		 * Querying documents containing array of legacy coordinate pairs ( for e.g. [ [ 1, 1 ], [ 2, 2 ], [ 3, 3 ] ] ) are supported with 2d index and in runtime but not with 2dsphere index.
		 * We only end up here in case of runtime or 2dsphere index validation. Hence adding this check here - throwError signifies 2dsphere index validation.
		 */
		const bson_value_t *firstValue = bson_iter_value(&iter);
		if ((firstValue->value_type == BSON_TYPE_ARRAY ||
			 firstValue->value_type == BSON_TYPE_DOCUMENT) &&
			!throwError)
		{
			processState->isMultiKeyContext = true;
			BsonValueInitIterator(value, &iter);
			while (bson_iter_next(&iter))
			{
				const bson_value_t *nestedValue = bson_iter_value(&iter);
				isValid = BsonValueGetGeometryWKB(nestedValue, parseFlag,
												  &parseState);

				if (isValid)
				{
					bool isMatched =
						UpdateStateAndRunMatcherIfValidPointFound(processState);

					if (isMatched)
					{
						return false;
					}
				}
			}
		}
		else
		{
			/* value must be a simple legacy coordinate pair, process it like that */
			isValid = BsonValueGetGeometryWKB(value, parseFlag, &parseState);

			if (isValid)
			{
				UpdateStateAndRunMatcherIfValidPointFound(processState);
			}
		}
	}
	else
	{
		/* This can either by legacy or GeoJSON, so try to parse this as legacy without error */
		parseFlag = parseFlag | ParseFlag_Legacy_NoError | ParseFlag_GeoJSON_All;
		isValid = BsonValueGetGeometryWKB(value, parseFlag, &parseState);

		if (parseState.crs != NULL &&
			strcmp(parseState.crs, GEOJSON_CRS_BIGPOLYGON) == 0)
		{
			/* Strict CRS are not valid for insertions with strict index validation */
			RETURN_FALSE_IF_ERROR_NOT_EXPECTED(
				throwError, (
					errcode(GEO_ERROR_CODE(processState->errorCtxt)),
					errmsg("%scan't index geometry with strict winding order",
						   GEO_ERROR_PREFIX(processState->errorCtxt)),
					errdetail_log("%scan't index geometry with strict winding order",
								  GEO_HINT_PREFIX(processState->errorCtxt))));
		}


		if (processState->opInfo != NULL &&
			IsGeoWithinQueryOperator(processState->opInfo->queryOperatorType) &&
			parseState.type == GeoJsonType_POLYGON && parseState.numOfRingsInPolygon > 1)
		{
			/* TODO: Fix polygon with holes geowithin comparision, for now we throw unsupported error because of
			 * Postgis matching difference for these cases
			 */
			ereport(ERROR, (
						errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
						errmsg("$geoWithin currently doesn't support polygons with holes")
						));
		}

		if (isValid)
		{
			UpdateStateAndRunMatcherIfValidPointFound(processState);
		}
	}

	return false;
}


/*
 * BsonExtractGeospatialInternal traverse the document and converts the document value to a geometry / geography
 * value, If multi key values are found in the document the `collectType` geometry / geography collection is created
 */
static Datum
BsonExtractGeospatialInternal(const pgbson *document, const StringView *pathView,
							  GeospatialType type, GeospatialValidationLevel level,
							  WKBGeometryType collectType,
							  GeospatialErrorContext *errCtxt)
{
	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);

	ProcessCommonGeospatialState state;
	InitProcessCommonGeospatialState(&state, level, type, errCtxt);

	if (type == GeospatialType_Geometry)
	{
		BsonIterGetLegacyGeometryPoints(&documentIterator, pathView,
										&state);
	}
	else
	{
		BsonIterGetGeographies(&documentIterator, pathView, &state);
	}

	if (state.isEmpty)
	{
		/*
		 * No values were found in this document, return NULL datum
		 */
		return (Datum) 0;
	}

	/* We made it this far and has a valid WKB buffer
	 * Write the size of varlena as the first if multikey or at 8th if not multikey
	 */

	bytea *wkbBytea = NULL;
	if (state.isMultiKeyContext && state.total > 1)
	{
		wkbBytea = WKBBufferGetCollectionByteaWithSRID(state.WKBBuffer, collectType,
													   state.total);
	}
	else
	{
		wkbBytea = WKBBufferGetByteaWithSRID(state.WKBBuffer);
	}

	Datum result = (type == GeospatialType_Geometry) ? GetGeometryFromWKB(wkbBytea) :
				   GetGeographyFromWKB(wkbBytea);

	/* We can free the buffers now */
	DeepFreeWKB(state.WKBBuffer);
	pfree(wkbBytea);

	return result;
}


/*
 * Returns error prefix for invalid documents during the 2dsphere index builds or inserts.
 */
static const char *
_2dsphereIndexErrorPrefix(const pgbson *document)
{
	bson_iter_t iterator;
	StringInfo prependErrorMsg = makeStringInfo();
	appendStringInfo(prependErrorMsg, "Can't extract geo keys ");
	if (PgbsonInitIteratorAtPath(document, "_id", &iterator))
	{
		/* Append only the _id so that in case of failure, clients can identify the errorneous documents */
		appendStringInfo(prependErrorMsg, "{ _id: %s } ",
						 BsonValueToJsonForLogging(bson_iter_value(&iterator)));
	}
	return prependErrorMsg->data;
}


/*
 * Returns error hint prefix for invalid documents during the 2dsphere index builds or inserts.
 * Please note that this function should not use document to log PII data
 */
static const char *
_2dsphereIndexErrorHintPrefix(const pgbson *ignore)
{
	return pstrdup("Can't extract geo keys ");
}


/*
 * Returns empty string as prefix
 */
static const char *
_2dIndexNoPrefix(const pgbson *document)
{
	return pstrdup(EMPTY_GEO_ERROR_PREFIX);
}
