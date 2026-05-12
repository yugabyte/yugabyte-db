/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_query.c
 *
 * Implementation of bson query operation.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/builtins.h"

#include "collation/collation.h"
#include "io/bson_core.h"
#include "query/bson_compare.h"
#include "aggregation/bson_query.h"
#include "utils/documentdb_errors.h"


typedef struct
{
	/* found at least one _id */
	bool foundId;

	/* value of the _id we found */
	bson_value_t idValue;

	/* found multiple distinct _id values */
	bool foundMultipleIds;

	/* throw an error if _id is re-defined */
	bool errorOnConflict;

	/* other fields also present apart from _id */
	bool queryHasNonIdFilters;
} QueryIdContext;

static void ProcessIdInQuery(void *context, const char *path, const bson_value_t *value);
static void SetQueryHasNonIdFilters(void *context);

/*
 * Extract any _id values that uniquely identify a document from the filters.
 * If any such value exists, it is set in the idValue parameter and the function returns true.
 *
 * If errorOnConflict is set, an error is thrown when the query defines multiple distinct
 * _id values. Otherwise, if multiple _id values are found, the function returns false.
 */
bool
TraverseQueryDocumentAndGetId(bson_iter_t *queryDocument, bson_value_t *idValue,
							  bool errorOnConflict, bool *queryHasNonIdFilters,
							  bool *isIdValueCollationAware)
{
	QueryIdContext idContext;
	memset(&idContext, 0, sizeof(QueryIdContext));

	idContext.errorOnConflict = errorOnConflict;

	bool isUpsert = false;
	TraverseQueryDocumentAndProcess(queryDocument, (void *) &idContext,
									&ProcessIdInQuery, &SetQueryHasNonIdFilters,
									isUpsert);

	*queryHasNonIdFilters = idContext.queryHasNonIdFilters;
	if (idContext.foundId && !idContext.foundMultipleIds)
	{
		*idValue = idContext.idValue;
		*isIdValueCollationAware = IsBsonTypeCollationAware(idValue->value_type);
		return true;
	}
	else
	{
		return false;
	}
}


/*
 * Extract any projection values from a given query spec. For each leaf query value,
 * calls the ProcessQueryValueFunc with the value for that given query. Walks any
 * $and, and individual filters (which are implicitly $ands), and any $or that has
 * exactly one item (since a $or with 1 item can be used to infer fields).
 * if isUpsert is true and querySpec has $expr operator then will throw an error.
 *
 * processFilterFunc use to set set queryHasNonIdFilters to true for certain operators like $gt, $lt, $expr.
 * This ensures that we use the @@ operator to get an exact match later while building Update query.
 */
void
TraverseQueryDocumentAndProcess(bson_iter_t *queryDocument, void *context,
								ProcessQueryValueFunc processValueFunc,
								ProcessQueryFilterFunc processFilterFunc,
								bool isUpsert)
{
	/* for the query, walk all "$and", and "$eq" trying to find an _id. */
	while (bson_iter_next(queryDocument))
	{
		const char *key = bson_iter_key(queryDocument);
		if (strcmp(key, "$nor") == 0)
		{
			if (processFilterFunc)
			{
				processFilterFunc(context);
			}
			continue;
		}
		else if (strcmp(key, "$and") == 0)
		{
			bson_iter_t andIterator;
			if (!BSON_ITER_HOLDS_ARRAY(queryDocument) ||
				!bson_iter_recurse(queryDocument, &andIterator))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
									"Could not iterate through query document $and.")));
			}

			while (bson_iter_next(&andIterator))
			{
				bson_iter_t andElementIterator;
				if (!BSON_ITER_HOLDS_DOCUMENT(&andIterator) ||
					!bson_iter_recurse(&andIterator, &andElementIterator))
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
										"Could not iterate through elements within $and query.")));
				}

				TraverseQueryDocumentAndProcess(&andElementIterator, context,
												processValueFunc,
												processFilterFunc,
												isUpsert);
			}
		}
		else if (strcmp(key, "$or") == 0)
		{
			bson_iter_t orIterator;
			if (!BSON_ITER_HOLDS_ARRAY(queryDocument) ||
				!bson_iter_recurse(queryDocument, &orIterator))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
									"Could not iterate through query document $or.")));
			}

			/* the _id can be extracted iff the "or" is a single element array. */
			pgbsonelement orElement;
			if (TryGetSinglePgbsonElementFromBsonIterator(&orIterator, &orElement) &&
				orElement.bsonValue.value_type == BSON_TYPE_DOCUMENT)
			{
				/* a $or is only considered if it's a single element or. */
				bson_iter_t orElementIterator;
				bson_iter_init_from_data(&orElementIterator,
										 orElement.bsonValue.value.v_doc.data,
										 orElement.bsonValue.value.v_doc.data_len);
				TraverseQueryDocumentAndProcess(&orElementIterator, context,
												processValueFunc,
												processFilterFunc,
												isUpsert);
			}
			else if (isUpsert &&
					 bson_iter_recurse(queryDocument, &orIterator) &&
					 BsonIterSearchKeyRecursive(&orIterator, "$expr"))
			{
				/* Throw an error in case of upsert if querySpec holds $expr */
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_QUERYFEATURENOTALLOWED),
								errmsg(
									"Use of the $expr operator is not permitted within the query predicate for an upsert operation.")));
			}

			if (processFilterFunc)
			{
				processFilterFunc(context);
			}
		}
		else if (strcmp(key, "$expr") == 0)
		{
			if (isUpsert)
			{
				/* Throw an error in case of upsert if querySpec holds $expr */
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_QUERYFEATURENOTALLOWED),
								errmsg(
									"Use of the $expr operator is not permitted within the query predicate for an upsert operation.")));
			}

			if (processFilterFunc)
			{
				processFilterFunc(context);
			}
		}
		else
		{
			/* it's a field specification. consider it if it's a $eq, $all, $in operators or a document without any operator. */
			bson_iter_t idIterator;
			if (BSON_ITER_HOLDS_DOCUMENT(queryDocument) &&
				bson_iter_recurse(queryDocument, &idIterator))
			{
				/* When the document contains one or more operators (implicit AND) */
				/* e.g. { _id: {$eq: 10}                                 */
				/*      { _id: {$eq: 10, $ne: null} }    Implicit AND    */
				/*      { _id: {a: 10, b: 20} }          Object as value */
				/*      { _id: {$eq: 10, b: 20} }        Error Case      */
				/*      { _id: {a: 10, $eq: 20} }        Not Error Case  */
				/*      { $ref: "foo", $id: ObjectId("49d4j9jdjd949djd9449jd") } */
				bool isEmptyDoc = true;
				while (bson_iter_next(&idIterator))
				{
					isEmptyDoc = false;
					const char *op = bson_iter_key(&idIterator);
					const bson_value_t *opValue = bson_iter_value(&idIterator);

					if (strcmp(op, "$eq") == 0)
					{
						processValueFunc(context, key, opValue);
					}
					else if (strcmp(op, "$all") == 0)
					{
						if (opValue->value_type != BSON_TYPE_ARRAY)
						{
							ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
											errmsg("$all needs an array")));
						}

						bson_iter_t allIterator;
						BsonValueInitIterator(opValue, &allIterator);

						while (bson_iter_next(&allIterator))
						{
							processValueFunc(context, key, bson_iter_value(&allIterator));
						}
					}
					else if (strcmp(op, "$in") == 0)
					{
						bson_iter_t inIterator;
						if (opValue->value_type != BSON_TYPE_ARRAY)
						{
							ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
											errmsg(
												"Expected 'array' type for $in but found '%s' type",
												BsonTypeName(opValue->value_type))));
						}

						BsonValueInitIterator(opValue, &inIterator);

						if (bson_iter_next(&inIterator))
						{
							const bson_value_t *inValue = bson_iter_value(&inIterator);

							/* if $in has more than one element in array then ignore that field */
							if (!bson_iter_next(&inIterator))
							{
								processValueFunc(context, key, inValue);
							}
						}

						if (processFilterFunc)
						{
							processFilterFunc(context);
						}
					}
					else if (strlen(op) > 0 && op[0] != '$')
					{
						/* when operator does not start with $, its a regular value. Process entire document as single value */
						/* e.g. {_id: {a: 10}}            */
						/*      {_id: {a: 10, $eq: 20}}   */
						processValueFunc(context, key, bson_iter_value(queryDocument));
						break;
					}
					else if (isUpsert && ((strcmp(op, "$ref") == 0) ||
										  (strcmp(op, "$id") == 0)))
					{
						/* handle $ref scenario */
						/* { $ref: "foo", $id: ObjectId("49d4j9jdjd949djd9449jd") } */
						/* { $id: ObjectId("49d4j9jdjd949djd9449jd"), $ref: "foo" } */
						bson_iter_t refIterator = idIterator;

						if (!bson_iter_next(&refIterator))
						{
							ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
												"Unrecognized operator specified: %s",
												op),
											errdetail_log(
												"Unrecognized operator specified: %s",
												op)));
						}

						bool isRef = strcmp(op, "$ref") == 0;
						if ((isRef && strcmp(bson_iter_key(&refIterator), "$id") == 0) ||
							(!isRef && strcmp(bson_iter_key(&refIterator), "$ref") == 0))
						{
							processValueFunc(context, key, bson_iter_value(
												 queryDocument));
							break;
						}
					}
					else
					{
						/* Other operators like $gt, $lt, $ne etc. are ignored we don't call processValueFunc */
						if (processFilterFunc)
						{
							processFilterFunc(context);
						}
					}
				}
				if (isEmptyDoc)
				{
					/* empty document case:  {_id: {} } */
					processValueFunc(context, key, bson_iter_value(queryDocument));
				}
			}
			else if (!BSON_ITER_HOLDS_REGEX(queryDocument))
			{
				/* it's the form of "field": <value>   e.g. { _id: 10 } */
				/* however note that, "field" : /regex/ is not equality */
				processValueFunc(context, key, bson_iter_value(queryDocument));
			}
			else if (processFilterFunc)
			{
				/* { "_id" : { "$regularExpression" : { "pattern" : "abc", "options" : "i" } } } */
				processFilterFunc(context);
			}
		}
	}
}


/*
 * Given a leaf query field path, validates that the field is for _id, and sets the
 * value to the QueryIdContext structure. If the _id has already been set, fails
 * with an error.
 */
static void
ProcessIdInQuery(void *context, const char *path, const bson_value_t *value)
{
	QueryIdContext *idContext = (QueryIdContext *) context;

	if (strcmp(path, "_id") != 0)
	{
		idContext->queryHasNonIdFilters = true;
		return;
	}


	if (idContext->foundMultipleIds)
	{
		/* already found multiple values. We need to set it to true in case
		 * of multiple ID's as to match against multiple _id we need @@ filter */
		idContext->queryHasNonIdFilters = true;
		return;
	}

	bool isComparisonValid;

	if (idContext->foundId &&
		CompareBsonValueAndType(value, &(idContext->idValue), &isComparisonValid) != 0)
	{
		if (idContext->errorOnConflict)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_NOTSINGLEVALUEFIELD),
							errmsg(
								"Unable to determine query fields to assign because the path '_id' appears multiple times.")));
		}
		else
		{
			/* we need to set it to true in case of multiple ID's as to match against multiple _id we need @@ filter */
			idContext->queryHasNonIdFilters = true;
			idContext->foundMultipleIds = true;
		}
	}
	else
	{
		/* first _id value or same as the first */
		idContext->idValue = *value;
		idContext->foundId = true;
	}
}


/*
 * When traversing the query specification, we need to set queryHasNonIdFilters to true for certain
 * operators like $gt, $lt, $expr. This ensures that we use the @@ operator to get an exact match later
 * while building Update query.
 */
static void
SetQueryHasNonIdFilters(void *context)
{
	QueryIdContext *idContext = (QueryIdContext *) context;
	idContext->queryHasNonIdFilters = true;
}
