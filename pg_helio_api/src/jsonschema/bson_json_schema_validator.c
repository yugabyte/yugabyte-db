/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_json_schema_validator.c
 *
 * Implementation of $jsonSchema query operator.
 * Generates a Json Schema tree from given Json Schema and validates given document against the tree.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <funcapi.h>
#include <miscadmin.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "jsonschema/bson_json_schema_tree.h"
#include "utils/mongo_errors.h"
#include "metadata/metadata_cache.h"
#include "utils/fmgr_utils.h"
#include "utils/hashset_utils.h"

/* --------------------------------------------------------- */
/* Forward Declerations */
/* --------------------------------------------------------- */

typedef struct BsonKeyValuePair
{
	const char *key;
	bson_value_t *value;
	List *subDocKvList;
} BsonKeyValuePair;

static bool ValidateBsonValueAgainstSchemaTree(const bson_value_t *value,
											   const SchemaNode *node);

static bool ValidateBsonValueCommon(const bson_value_t *value,
									const SchemaNode *node);
static bool ValidateBsonValueObject(const bson_value_t *value,
									const SchemaNode *node);
static bool ValidateBsonValueString(const bson_value_t *value,
									const SchemaNode *node);
static bool ValidateBsonValueNumeric(const bson_value_t *value,
									 const SchemaNode *node);
static bool ValidateBsonValueArray(const bson_value_t *value,
								   const SchemaNode *node);

static BsonTypeFlags GetBsonValueTypeFlag(bson_type_t type);
static void FreeListOfDocKvLists(List *list);
static void FreeDocKvList(List *list);

static int CompareKeysInBsonKeyValuePairs(const ListCell *cellA, const ListCell *cellB);
static int CompareBsonDocKvLists(List *docKvListA, List *docKvListB,
								 bool *isComparisonValid);
static int CompareBsonKvPairs(BsonKeyValuePair *pairA, BsonKeyValuePair *pairB,
							  bool *isComparisonValid);
static List * GetSortedListOfKeyValuePairs(const bson_value_t *value);

static bool IsBsonArrayUnique(const bson_value_t *value, bool ignoreKeyOrderInObject);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_dollar_json_schema);

/*
 * implements the Mongo's $jsonSchema operator functionality
 * in the runtime. It generates a Json Schema Tree (and store it in cache),
 * then validates the given document against the Json Schema Tree.
 */
Datum
bson_dollar_json_schema(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *schema = PG_GETARG_PGBSON(1);

	pgbsonelement element;
	bson_iter_t schemaIter;

	PgbsonToSinglePgbsonElement(schema, &element);

	if (element.bsonValue.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR,
				(errcode(MongoTypeMismatch),
				 errmsg("$jsonSchema must be an object")));
	}

	BsonValueInitIterator(&(element.bsonValue), &schemaIter);

	/* Build a cacheable state for processing json schema tree */
	const SchemaTreeState *treeState = NULL;
	SchemaTreeState localTreeState = { 0 };

	SetCachedFunctionState(
		treeState,
		SchemaTreeState,
		1,
		BuildSchemaTree,
		&schemaIter);

	if (treeState == NULL)
	{
		/* Generate the schema tree */
		BuildSchemaTree(&localTreeState, &schemaIter);
		treeState = &localTreeState;
	}

	/* Validate the document against the schema tree */
	bson_value_t docBsonValue = ConvertPgbsonToBsonValue(document);
	bool isValid = ValidateBsonValueAgainstSchemaTree(&docBsonValue, treeState->rootNode);

	PG_RETURN_BOOL(isValid);
}


/* --------------------------------------------------------- */
/*                          Core Functions                   */
/* --------------------------------------------------------- */

/*
 * Validate given bson value against given Json Schema tree / sub-tree
 */
static bool
ValidateBsonValueAgainstSchemaTree(const bson_value_t *value, const
								   SchemaNode *node)
{
	if (node->validationFlags.common && !ValidateBsonValueCommon(value, node))
	{
		return false;
	}
	if (node->validationFlags.object && !ValidateBsonValueObject(value, node))
	{
		return false;
	}
	if (node->validationFlags.string && !ValidateBsonValueString(value, node))
	{
		return false;
	}
	if (node->validationFlags.numeric && !ValidateBsonValueNumeric(value, node))
	{
		return false;
	}
	if (node->validationFlags.array && !ValidateBsonValueArray(value, node))
	{
		return false;
	}

	return true;
}


/*
 * Validate given bson value against given Json Schema tree / sub-tree for
 * Common validations set:
 * jsonType, bsonType, enum, allOf, anyOf, oneOf, not
 */
static bool
ValidateBsonValueCommon(const bson_value_t *value, const SchemaNode *node)
{
	uint16_t flags = node->validationFlags.common;
	if (flags & CommonValidationTypes_JsonType)
	{
		BsonTypeFlags bsonTypeFlag = GetBsonValueTypeFlag(value->value_type);
		if (!(node->validations.common->jsonTypes & bsonTypeFlag))
		{
			return false;
		}
	}
	if (flags & CommonValidationTypes_BsonType)
	{
		BsonTypeFlags bsonTypeFlag = GetBsonValueTypeFlag(value->value_type);
		if (!(node->validations.common->bsonTypes & bsonTypeFlag))
		{
			return false;
		}
	}

	/* TODO: Add more common validations here */

	return true;
}


/*
 * Validate given bson value against given Json Schema tree / sub-tree for
 * Object validations set:
 * required, maxProperties, minProperties, patternProperties, additionalProperties, dependencies
 */
static bool
ValidateBsonValueObject(const bson_value_t *value, const SchemaNode *node)
{
	if (value->value_type != BSON_TYPE_DOCUMENT)
	{
		return true;
	}

	bson_iter_t iter;
	BsonValueInitIterator(value, &iter);
	while (bson_iter_next(&iter))
	{
		const char *field = bson_iter_key(&iter);
		const bson_value_t *fieldValue = bson_iter_value(&iter);

		SchemaFieldNode *fieldNode = FindFieldNodeByName(node, field);
		if (fieldNode != NULL)
		{
			if (!ValidateBsonValueAgainstSchemaTree(fieldValue,
													(SchemaNode *) fieldNode))
			{
				return false;
			}
		}
	}

	/* TODO: Add more object validations here */

	return true;
}


/*
 * Validate given bson value against given Json Schema tree / sub-tree for
 * String validations set:
 * maxLength, minLength, pattern
 */
static bool
ValidateBsonValueString(const bson_value_t *value, const SchemaNode *node)
{
	if (value->value_type != BSON_TYPE_UTF8)
	{
		return true;
	}

	const StringView strView =
	{
		.string = value->value.v_utf8.str,
		.length = value->value.v_utf8.len
	};

	uint16_t flags = node->validationFlags.string;
	if (flags & (StringValidationTypes_MaxLength | StringValidationTypes_MinLength))
	{
		uint32_t len = StringViewMultiByteCharStrlen(&strView);
		if (flags & StringValidationTypes_MaxLength)
		{
			if (len > node->validations.string->maxLength)
			{
				return false;
			}
		}
		if (flags & StringValidationTypes_MinLength)
		{
			if (len < node->validations.string->minLength)
			{
				return false;
			}
		}
	}
	if (flags & StringValidationTypes_Pattern)
	{
		RegexData *regexData = node->validations.string->pattern;
		if (!PcreRegexExecute(regexData->regex, regexData->options, regexData->pcreData,
							  &strView))
		{
			return false;
		}
	}
	return true;
}


/*
 * Validate given bson value against given Json Schema tree / sub-tree for
 * Numeric validations set:
 * maximum, minimum, exclusiveMaximum, exclusiveMinimum, multipleOf
 */
static bool
ValidateBsonValueNumeric(const bson_value_t *value, const SchemaNode *node)
{
	if (!BsonValueIsNumber(value))
	{
		return true;
	}

	bool isComparisonValid = true;
	uint16_t flags = node->validationFlags.numeric;

	if (flags & NumericValidationTypes_Maximum)
	{
		int res = CompareBsonValueAndType(value, node->validations.numeric->maximum,
										  &isComparisonValid);
		if ((flags & NumericValidationTypes_ExclusiveMaximum) &&
			node->validations.numeric->exclusiveMaximum)
		{
			if (res >= 0 || !isComparisonValid)
			{
				return false;
			}
		}
		else
		{
			if (res == 1 || !isComparisonValid)
			{
				return false;
			}
		}
	}

	if (flags & NumericValidationTypes_Minimum)
	{
		int res = CompareBsonValueAndType(value, node->validations.numeric->minimum,
										  &isComparisonValid);
		if ((flags & NumericValidationTypes_ExclusiveMinimum) &&
			node->validations.numeric->exclusiveMinimum)
		{
			if (res <= 0 || !isComparisonValid)
			{
				return false;
			}
		}
		else
		{
			if (res == -1 || !isComparisonValid)
			{
				return false;
			}
		}
	}

	if (flags & NumericValidationTypes_MultipleOf)
	{
		bson_value_t remainder;
		bool validateInputs = true;
		GetRemainderFromModBsonValues(value, node->validations.numeric->multipleOf,
									  validateInputs, &remainder);
		if (BsonValueAsDouble(&remainder) != 0.0)
		{
			return false;
		}
	}

	return true;
}


/*
 * Validate given bson value against given Json Schema tree / sub-tree for
 * Array validations set:
 * maxItems, minItems, uniqueItems, items, additionalItems
 */
static bool
ValidateBsonValueArray(const bson_value_t *value, const SchemaNode *node)
{
	if (value->value_type != BSON_TYPE_ARRAY)
	{
		return true;
	}
	uint16_t flags = node->validationFlags.array;

	if (flags & (ArrayValidationTypes_MaxItems | ArrayValidationTypes_MinItems))
	{
		uint16_t arrayLen = BsonDocumentValueCountKeys(value);
		if (flags & ArrayValidationTypes_MaxItems)
		{
			if (arrayLen > node->validations.array->maxItems)
			{
				return false;
			}
		}
		if (flags & ArrayValidationTypes_MinItems)
		{
			if (arrayLen < node->validations.array->minItems)
			{
				return false;
			}
		}
	}
	if (flags & ArrayValidationTypes_UniqueItems)
	{
		if (node->validations.array->uniqueItems)
		{
			bool ignoreKeyOrderInObject = true;
			if (!IsBsonArrayUnique(value, ignoreKeyOrderInObject))
			{
				return false;
			}
		}
	}
	if (flags & ArrayValidationTypes_ItemsObject)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);
		while (bson_iter_next(&arrayIter))
		{
			const bson_value_t *arrayElementValue = bson_iter_value(&arrayIter);
			if (!ValidateBsonValueAgainstSchemaTree(arrayElementValue,
													(SchemaNode *) node->
													validations.array->itemsNode))
			{
				return false;
			}
		}
	}
	if (flags & ArrayValidationTypes_ItemsArray)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);

		SchemaNode *itemNode = (SchemaNode *) node->validations.array->itemsArray;
		while (itemNode != NULL)
		{
			if (bson_iter_next(&arrayIter))
			{
				const bson_value_t *arrayElementValue = bson_iter_value(&arrayIter);
				if (!ValidateBsonValueAgainstSchemaTree(arrayElementValue,
														itemNode))
				{
					return false;
				}
			}
			else
			{
				break;
			}
			itemNode = itemNode->next;
		}

		/* if there are more items in the array than in 'items' of the schema, validate with additional items */
		if (bson_iter_next(&arrayIter))
		{
			if (node->validationFlags.array & ArrayValidationTypes_AdditionalItemsBool)
			{
				if (!node->validations.array->additionalItemsBool)
				{
					return false;
				}
			}
			else if (node->validationFlags.array &
					 ArrayValidationTypes_AdditionalItemsObject)
			{
				do {
					const bson_value_t *arrayElementValue = bson_iter_value(&arrayIter);
					if (!ValidateBsonValueAgainstSchemaTree(arrayElementValue,
															(SchemaNode *) (node->
																			validations
																			.array
																			->
																			additionalItemsNode)))
					{
						return false;
					}
				} while (bson_iter_next(&arrayIter));
			}
		}
	}

	return true;
}


/*
 * Compares all elements of a BSON array with each other for uniqueness.
 * Returns true if all the elements are unique.
 * If ignoreKeyOrderInObject flag is true, comparison of array elements of document types
 * is performed comparing values of matching keys. Key order does not matter.
 *   e.g. {a:1, b:2} and {b:2, a:1} are considered equal in such case.
 *        Subsequently the array [{a:1, b:2}, {b:2, a:1}] is not unique.
 *
 * Sorted key order object comparision:
 * - Generate a list of "key" and "value" pairs of given document.
 * - Sort this list in lexicographical order of keys.
 * - Compare this sorted key-value list with sorted key-value list of other documents in the array
 *   - In total there will be n^2 such comparisions, where n is the number of documents in the array.
 * - If any document has sub documents, then they are also stored as key-value lists within the list
 * - In order to not re-generate this key-value list for each document during comparision,
 *   the generated key-value list of each document is stored in another top level list.
 *
 * - Example [{e: {c:1, b:2}, a:3}, {d:4}] is stored as shown in diagram:
 *      brackets () denote one element of a list.
 *      arrow --> denotes relation to next element in the list
 *      vertial bar | denotes pointer to the head of another list
 *
 *      (list) --> (list)
 *        |          |
 *        |        (d,4,nil)
 *        |
 *      (a,3,nil) --> (e,{c:1,b:2},list)
 *                                   |
 *                                 (b,2,nil) --> (c,1,nil)
 */
bool
IsBsonArrayUnique(const bson_value_t *value, bool ignoreKeyOrderInObject)
{
	Assert(value->value_type == BSON_TYPE_ARRAY);
	HTAB *bsonValueHashSet = CreateBsonValueHashSet();
	List *docsList = NIL;
	bool found = false;
	bson_iter_t arrayIter;
	BsonValueInitIterator(value, &arrayIter);
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIter);
		hash_search(bsonValueHashSet, arrayValue, HASH_ENTER, &found);
		if (found)
		{
			hash_destroy(bsonValueHashSet);
			FreeListOfDocKvLists(docsList);
			return false;
		}

		/* If value is a document, and hash didn't match, do a second check of comparison with
		 * other documents in the array, where document key-value pairs are sorted by keys */
		else if (arrayValue->value_type == BSON_TYPE_DOCUMENT &&
				 ignoreKeyOrderInObject)
		{
			ListCell *cell;
			bool isComparisonValid = true;
			List *docKvListA = GetSortedListOfKeyValuePairs(arrayValue);

			/* For empty document { } the List could be NIL */
			if (docKvListA == NIL)
			{
				continue;
			}
			foreach(cell, docsList)
			{
				List *docKvListB = (List *) lfirst(cell);
				int cmp = CompareBsonDocKvLists(docKvListA, docKvListB,
												&isComparisonValid);
				if (isComparisonValid && (cmp == 0))
				{
					hash_destroy(bsonValueHashSet);
					FreeListOfDocKvLists(docsList);
					return false;
				}
			}
			docsList = lappend(docsList, docKvListA);
		}
	}
	hash_destroy(bsonValueHashSet);
	FreeListOfDocKvLists(docsList);
	return true;
}


/* --------------------------------------------------------- */
/* Helper Functions */
/* --------------------------------------------------------- */


static void
FreeListOfDocKvLists(List *list)
{
	ListCell *cell;
	foreach(cell, list)
	{
		List *docKvList = (List *) lfirst(cell);
		Assert(docKvList != NIL);
		FreeDocKvList(docKvList);
	}
	list_free(list);
}


static void
FreeDocKvList(List *list)
{
	ListCell *cell;
	foreach(cell, list)
	{
		BsonKeyValuePair *kvPair = (BsonKeyValuePair *) lfirst(cell);
		Assert(kvPair != NULL);
		pfree(kvPair->value);
		if (kvPair->subDocKvList != NIL)
		{
			FreeDocKvList(kvPair->subDocKvList);
		}
	}
	list_free(list);
}


/*
 * Comparator function required for list_sort for the list of BsonKeyValuePair
 */
static int
CompareKeysInBsonKeyValuePairs(const ListCell *cellA, const ListCell *cellB)
{
	BsonKeyValuePair *kvPairA = (BsonKeyValuePair *) lfirst(cellA);
	BsonKeyValuePair *kvPairB = (BsonKeyValuePair *) lfirst(cellB);
	return strcmp(kvPairA->key, kvPairB->key);
}


/*
 * This function creates a list of key and value pairs of a bson document,
 * which is sorted by the key string.
 */
static List *
GetSortedListOfKeyValuePairs(const bson_value_t *value)
{
	List *list = NIL;
	bson_iter_t iter;
	BsonValueInitIterator(value, &iter);
	while (bson_iter_next(&iter))
	{
		BsonKeyValuePair *keyValuePair = palloc0(sizeof(BsonKeyValuePair));
		keyValuePair->key = bson_iter_key(&iter);
		keyValuePair->value = (bson_value_t *) palloc0(sizeof(bson_value_t));
		keyValuePair->subDocKvList = NIL;
		bson_value_copy(bson_iter_value(&iter), keyValuePair->value);
		list = lappend(list, keyValuePair);
	}

	/* Sort the list in the order of keys */
	list_sort(list, CompareKeysInBsonKeyValuePairs);
	return list;
}


/*
 * This functions compares two linked lists of BsonKeyValuePair.
 * It sequentially compares key & value of first list with corresponding key & value of second list.
 * If the lengths of lists differ, it returns the differnce in list lengths.
 */
static int
CompareBsonDocKvLists(List *docKvListA, List *docKvListB, bool *isComparisonValid)
{
	check_stack_depth();

	*isComparisonValid = true;
	int keyCountA = list_length(docKvListA);
	int keyCountB = list_length(docKvListB);
	if (keyCountA != keyCountB)
	{
		return keyCountA - keyCountB;
	}

	ListCell *cellA = NULL;
	ListCell *cellB = NULL;
	forboth(cellA, docKvListA, cellB, docKvListB)
	{
		CHECK_FOR_INTERRUPTS();

		BsonKeyValuePair *kvPairA = (BsonKeyValuePair *) lfirst(cellA);
		BsonKeyValuePair *kvPairB = (BsonKeyValuePair *) lfirst(cellB);

		int cmp = CompareBsonKvPairs(kvPairA, kvPairB, isComparisonValid);
		if (*isComparisonValid == false || cmp != 0)
		{
			return cmp;
		}
	}
	return 0;
}


/*
 * This functions compares key and values of two BsonKeyValuePair.
 * If the BsonKeyValuePair contains list of key value pairs of sub document,
 * it also compares the sub document key value lists
 */
static int
CompareBsonKvPairs(BsonKeyValuePair *pairA, BsonKeyValuePair *pairB,
				   bool *isComparisonValid)
{
	*isComparisonValid = true;
	int keyComp = strcmp(pairA->key, pairB->key);
	if (keyComp != 0)
	{
		return keyComp;
	}

	/* if value types are different, comparison is not valid */
	int sortOrderCmp = CompareSortOrderType(pairA->value->value_type,
											pairB->value->value_type);
	if (sortOrderCmp != 0)
	{
		*isComparisonValid = false;
		return sortOrderCmp;
	}

	/* if both values are document, compare indiviual keys and values */
	else if (pairA->value->value_type == BSON_TYPE_DOCUMENT)
	{
		if (pairA->subDocKvList == NIL)
		{
			pairA->subDocKvList = GetSortedListOfKeyValuePairs(pairA->value);
		}
		if (pairB->subDocKvList == NIL)
		{
			pairB->subDocKvList = GetSortedListOfKeyValuePairs(pairB->value);
		}
		return CompareBsonDocKvLists(pairA->subDocKvList, pairB->subDocKvList,
									 isComparisonValid);
	}

	/* Do regular comparison for other bson types */
	return CompareBsonValueAndType(pairA->value, pairB->value, isComparisonValid);
}


static BsonTypeFlags
GetBsonValueTypeFlag(bson_type_t type)
{
	switch (type)
	{
		case BSON_TYPE_EOD:
		{
			return BsonTypeFlag_EOD;
		}

		case BSON_TYPE_DOUBLE:
		{
			return BsonTypeFlag_DOUBLE;
		}

		case BSON_TYPE_UTF8:
		{
			return BsonTypeFlag_UTF8;
		}

		case BSON_TYPE_DOCUMENT:
		{
			return BsonTypeFlag_DOCUMENT;
		}

		case BSON_TYPE_ARRAY:
		{
			return BsonTypeFlag_ARRAY;
		}

		case BSON_TYPE_BINARY:
		{
			return BsonTypeFlag_BINARY;
		}

		case BSON_TYPE_UNDEFINED:
		{
			return BsonTypeFlag_UNDEFINED;
		}

		case BSON_TYPE_OID:
		{
			return BsonTypeFlag_OID;
		}

		case BSON_TYPE_BOOL:
		{
			return BsonTypeFlag_BOOL;
		}

		case BSON_TYPE_DATE_TIME:
		{
			return BsonTypeFlag_DATE_TIME;
		}

		case BSON_TYPE_NULL:
		{
			return BsonTypeFlag_NULL;
		}

		case BSON_TYPE_REGEX:
		{
			return BsonTypeFlag_REGEX;
		}

		case BSON_TYPE_DBPOINTER:
		{
			return BsonTypeFlag_DBPOINTER;
		}

		case BSON_TYPE_CODE:
		{
			return BsonTypeFlag_CODE;
		}

		case BSON_TYPE_SYMBOL:
		{
			return BsonTypeFlag_SYMBOL;
		}

		case BSON_TYPE_CODEWSCOPE:
		{
			return BsonTypeFlag_CODEWSCOPE;
		}

		case BSON_TYPE_INT32:
		{
			return BsonTypeFlag_INT32;
		}

		case BSON_TYPE_TIMESTAMP:
		{
			return BsonTypeFlag_TIMESTAMP;
		}

		case BSON_TYPE_INT64:
		{
			return BsonTypeFlag_INT64;
		}

		case BSON_TYPE_DECIMAL128:
		{
			return BsonTypeFlag_DECIMAL128;
		}

		case BSON_TYPE_MINKEY:
		{
			return BsonTypeFlag_MINKEY;
		}

		case BSON_TYPE_MAXKEY:
		{
			return BsonTypeFlag_MAXKEY;
		}

		default:
			return 0;
	}
}
