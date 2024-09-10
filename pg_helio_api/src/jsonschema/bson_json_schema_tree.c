/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_json_schema_tree.c
 *
 * Json Schema Tree generation for $jsonSchema query operator
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "types/decimal128.h"
#include "jsonschema/bson_json_schema_tree.h"
#include "utils/helio_errors.h"

/* --------------------------------------------------------- */
/*              Forward Declerations                         */
/* --------------------------------------------------------- */

static inline ValidationsObject * InitValidationsObject(void);
static inline ValidationsCommon * InitValidationsCommon(void);
static inline ValidationsNumeric * InitValidationsNumeric(void);
static inline ValidationsString * InitValidationsString(void);
static inline ValidationsArray * InitValidationsArray(void);

static void AllocateValidators(SchemaNode *node);
static void FreeUnusedValidators(SchemaNode *node);

static void ParseProperties(const bson_value_t *value, SchemaNode *node);

static void ParseJsonType(const bson_value_t *value, SchemaNode *node);
static void ParseBsonType(const bson_value_t *value, SchemaNode *node);

static void ParseItems(const bson_value_t *value, SchemaNode *node);
static void ParseAdditionalItems(const bson_value_t *value, SchemaNode *node);

static BsonTypeFlags GetJsonTypeEnumFromJsonTypeString(const char *jsonTypeStr);
static BsonTypeFlags GetBsonTypeEnumFromBsonTypeString(const char *bsonTypeStr);
static int64_t GetValidatedBsonIntValue(const bson_value_t *value, const char *field);

static void InitSchemaNode(SchemaNode *node, SchemaNodeType type);
static SchemaFieldNode * InitFieldNode(const char *field);
static SchemaKeywordNode * InitNonFieldNode(SchemaNodeType type);
static SchemaFieldNode * FindOrAddEmptyFieldNode(const char *field,
												 SchemaNode *node);
static void AppendNodeToLinkedList(SchemaNode **head, SchemaNode *node);
static inline void AppendKeywordNodeToLinkedList(SchemaKeywordNode **head,
												 SchemaKeywordNode *node);

static SchemaNode * BuildSchemaTreeCore(bson_iter_t *schemaIter,
										const char *field,
										SchemaNodeType nodeType);
static void BuildSchemaTreeCoreOnNode(bson_iter_t *schemaIter, SchemaNode *node);


/* -------------------------------------------------------- */
/*              Exported Functions                          */
/* -------------------------------------------------------- */

/*
 * Top level API to generate a Json Schema Tree with given schema document iterator,
 * and store the node of the tree in the treeState (used for caching)
 */
void
BuildSchemaTree(SchemaTreeState *treeState, bson_iter_t *schemaIter)
{
	SchemaNode *node = BuildSchemaTreeCore(schemaIter, "",
										   SchemaNodeType_Root);
	treeState->rootNode = node;
}


/*
 * For a given field name, this function searches and returns the matching field node,
 * from the "properties" Linked List.
 * Returns null if such child node does not exist.
 * TODO: Optimize this function by storing field nodes as BST (instead of Linked List)
 */
SchemaFieldNode *
FindFieldNodeByName(const SchemaNode *node, const char *field)
{
	if (node == NULL)
	{
		return NULL;
	}

	SchemaFieldNode *fieldNode = node->validations.object->properties;
	while (fieldNode != NULL)
	{
		if (StringViewEqualsCString(&(fieldNode->field), field))
		{
			return fieldNode;
		}
		fieldNode = (SchemaFieldNode *) fieldNode->base.next;
	}

	return NULL;
}


/* -------------------------------------------------------- */
/*              Core Functions                              */
/* -------------------------------------------------------- */

/*
 * Function to generate a Json Schema Sub Tree with given schema document iterator,
 * and corresponding field name.
 */
static SchemaNode *
BuildSchemaTreeCore(bson_iter_t *schemaIter, const char *field,
					SchemaNodeType nodeType)
{
	SchemaNode *node = NULL;
	if (nodeType == SchemaNodeType_Field)
	{
		node = (SchemaNode *) InitFieldNode(field);
	}
	else
	{
		node = (SchemaNode *) InitNonFieldNode(nodeType);
	}
	BuildSchemaTreeCoreOnNode(schemaIter, node);
	return node;
}


/*
 * Core Function to add Json Schema Sub tree on a given node.
 * Used when a tree node already exist, and more validations need to be added on the node.
 */
static void
BuildSchemaTreeCoreOnNode(bson_iter_t *schemaIter, SchemaNode *node)
{
	AllocateValidators(node);

	while (bson_iter_next(schemaIter))
	{
		const char *key = bson_iter_key(schemaIter);
		const bson_value_t *value = bson_iter_value(schemaIter);

		/* -------------------------------------------------------- */
		/*              Object Validators                           */
		/* -------------------------------------------------------- */

		if (!strcmp(key, "properties"))
		{
			ParseProperties(value, node);
		}

		/* TODO: Add other Object Validators here */


		/* -------------------------------------------------------- */
		/*              Common Validators                           */
		/* -------------------------------------------------------- */

		else if (!strcmp(key, "type"))
		{
			ParseJsonType(value, node);
		}
		else if (!strcmp(key, "bsonType"))
		{
			ParseBsonType(value, node);
		}

		/* TODO: Add other Common Validators here */

		/* -------------------------------------------------------- */
		/*              Numeric Validators                          */
		/* -------------------------------------------------------- */

		else if (!strcmp(key, "multipleOf"))
		{
			if (!BsonTypeIsNumber(value->value_type))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'multipleOf' must be a number")));
			}
			if ((value->value_type == BSON_TYPE_DECIMAL128 && IsDecimal128Zero(value)) ||
				(BsonValueAsDouble(value) == 0.0))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
								errmsg(
									"$jsonSchema keyword 'multipleOf' must have a positive value")));
			}
			if (IsBsonValueNaN(value))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("divisor cannot be NaN")));
			}
			if (IsBsonValueInfinity(value))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg("divisor cannot be infinite")));
			}
			node->validations.numeric->multipleOf = (bson_value_t *) palloc0(
				sizeof(bson_value_t));
			bson_value_copy(value, node->validations.numeric->multipleOf);
			node->validationFlags.numeric |= NumericValidationTypes_MultipleOf;
		}
		else if (!strcmp(key, "maximum"))
		{
			if (!BsonTypeIsNumber(value->value_type))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'maximum' must be a number")));
			}
			node->validations.numeric->maximum = (bson_value_t *) palloc0(
				sizeof(bson_value_t));
			bson_value_copy(value, node->validations.numeric->maximum);
			node->validationFlags.numeric |= NumericValidationTypes_Maximum;
		}
		else if (!strcmp(key, "exclusiveMaximum"))
		{
			if (!BSON_ITER_HOLDS_BOOL(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'exclusiveMaximum' must be a boolean")));
			}
			node->validations.numeric->exclusiveMaximum = bson_iter_bool(schemaIter);
			node->validationFlags.numeric |= NumericValidationTypes_ExclusiveMaximum;
		}
		else if (!strcmp(key, "minimum"))
		{
			if (!BsonTypeIsNumber(value->value_type))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'minimum' must be a number")));
			}
			node->validations.numeric->minimum = (bson_value_t *) palloc0(
				sizeof(bson_value_t));
			bson_value_copy(value, node->validations.numeric->minimum);
			node->validationFlags.numeric |= NumericValidationTypes_Minimum;
		}
		else if (!strcmp(key, "exclusiveMinimum"))
		{
			if (!BSON_ITER_HOLDS_BOOL(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'exclusiveMinimum' must be a boolean")));
			}
			node->validations.numeric->exclusiveMinimum = bson_iter_bool(schemaIter);
			node->validationFlags.numeric |= NumericValidationTypes_ExclusiveMinimum;
		}

		/* -------------------------------------------------------- */
		/*              String Validators                           */
		/* -------------------------------------------------------- */

		else if (!strcmp(key, "maxLength"))
		{
			node->validations.string->maxLength = GetValidatedBsonIntValue(value,
																		   "maxLength");
			node->validationFlags.string |= StringValidationTypes_MaxLength;
		}
		else if (!strcmp(key, "minLength"))
		{
			node->validations.string->minLength = GetValidatedBsonIntValue(value,
																		   "minLength");
			node->validationFlags.string |= StringValidationTypes_MinLength;
		}
		else if (!strcmp(key, "pattern"))
		{
			if (!BSON_ITER_HOLDS_UTF8(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'pattern' must be a string")));
			}
			RegexData *regexData = (RegexData *) palloc0(sizeof(RegexData));
			regexData->regex = (char *) bson_iter_utf8(schemaIter, NULL);
			regexData->options = NULL;
			regexData->pcreData = RegexCompile(regexData->regex, NULL);
			node->validations.string->pattern = regexData;
			node->validationFlags.string |= StringValidationTypes_Pattern;
		}

		/* -------------------------------------------------------- */
		/*              Array Validators                            */
		/* -------------------------------------------------------- */

		else if (!strcmp(key, "items"))
		{
			ParseItems(value, node);
		}
		else if (!strcmp(key, "additionalItems"))
		{
			ParseAdditionalItems(value, node);
		}
		else if (!strcmp(key, "maxItems"))
		{
			node->validations.array->maxItems = GetValidatedBsonIntValue(value,
																		 "maxItems");
			node->validationFlags.array |= ArrayValidationTypes_MaxItems;
		}
		else if (!strcmp(key, "minItems"))
		{
			node->validations.array->minItems = GetValidatedBsonIntValue(value,
																		 "minItems");
			node->validationFlags.array |= ArrayValidationTypes_MinItems;
		}
		else if (!strcmp(key, "uniqueItems"))
		{
			if (!BSON_ITER_HOLDS_BOOL(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'uniqueItems' must be a boolean")));
			}
			node->validations.array->uniqueItems = bson_iter_bool(schemaIter);
			node->validationFlags.array |= ArrayValidationTypes_UniqueItems;
		}

		/* -------------------------------------------------------- */
		/*              Unsupported Keywords                        */
		/* -------------------------------------------------------- */

		else if (!strcmp(key, "$ref") || !strcmp(key, "$schema") ||
				 !strcmp(key, "default") || !strcmp(key, "definitions") ||
				 !strcmp(key, "format") || !strcmp(key, "id"))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
							errmsg("$jsonSchema keyword '%s' is not currently supported",
								   key)));
		}

		/* -------------------------------------------------------- */
		/*              Unknown Keywords                            */
		/* -------------------------------------------------------- */

		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
							errmsg("Unknown $jsonSchema keyword: %s", key)));
		}
	}

	if ((node->validationFlags.numeric & NumericValidationTypes_ExclusiveMaximum) &&
		!(node->validationFlags.numeric & NumericValidationTypes_Maximum))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg(
							"$jsonSchema keyword 'maximum' must be a present if exclusiveMaximum is present")));
	}

	if ((node->validationFlags.numeric & NumericValidationTypes_ExclusiveMinimum) &&
		!(node->validationFlags.numeric & NumericValidationTypes_Minimum))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg(
							"$jsonSchema keyword 'minimum' must be a present if exclusiveMinimum is present")));
	}

	FreeUnusedValidators(node);
}


/*
 * ParseProperties function reads the schema value for "properties" keyword,
 * and adds a field Node for each of the fields in the provided document type bson value.
 */
static void
ParseProperties(const bson_value_t *value, SchemaNode *node)
{
	if (value->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
						errmsg("$jsonSchema keyword 'properties' must be an object")));
	}

	bson_iter_t iter;
	BsonValueInitIterator(value, &iter);
	while (bson_iter_next(&iter))
	{
		const char *field = bson_iter_key(&iter);

		/* TODO: Add support for dot notation paths */
		if (strchr(field, '.') != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
							errmsg("Dot notation paths are currently not supported: %s",
								   field)));
		}

		const bson_value_t *fieldValue = bson_iter_value(&iter);
		if (!BSON_ITER_HOLDS_DOCUMENT(&iter))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
							errmsg(
								"Nested schema for $jsonSchema property '%s' must be an object",
								field)));
		}

		SchemaFieldNode *fieldNode = FindOrAddEmptyFieldNode(field, node);

		bson_iter_t innerIter;
		BsonValueInitIterator(fieldValue, &innerIter);
		BuildSchemaTreeCoreOnNode(&innerIter, (SchemaNode *) fieldNode);
	}
	node->validationFlags.object |= ObjectValidationTypes_Properties;
}


/*
 * ParseJsonType function reads the schema value for "type" keyword.
 * And stores it in the Node's common validations section.
 */
static void
ParseJsonType(const bson_value_t *value, SchemaNode *node)
{
	if (node->validationFlags.common & CommonValidationTypes_BsonType)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg(
							"Cannot specify both $jsonSchema keywords 'type' and 'bsonType'")));
	}

	BsonTypeFlags jsonTypes = 0;
	if (value->value_type == BSON_TYPE_UTF8)
	{
		jsonTypes = GetJsonTypeEnumFromJsonTypeString(value->value.v_utf8.str);
	}
	else if (value->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);

		while (bson_iter_next(&arrayIter))
		{
			if (!BSON_ITER_HOLDS_UTF8(&arrayIter))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'type' array elements must be strings")));
			}
			uint32_t length;
			const char *jsonTypeStr = bson_iter_utf8(&arrayIter, &length);
			BsonTypeFlags jsonType = GetJsonTypeEnumFromJsonTypeString(jsonTypeStr);
			if (jsonTypes & jsonType)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
								errmsg(
									"$jsonSchema keyword 'type' has duplicate value: %s",
									jsonTypeStr)));
			}
			jsonTypes |= jsonType;
		}

		if (jsonTypes == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
							errmsg(
								"$jsonSchema keyword 'type' must name at least one type")));
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
						errmsg(
							"$jsonSchema keyword 'type' must be either a string or an array of strings")));
	}

	node->validations.common->jsonTypes = jsonTypes;
	node->validationFlags.common |= CommonValidationTypes_JsonType;
}


/*
 * ParseBsonType function reads the schema value for "bsonType" keyword.
 * And stores it in the Node's common validations section.
 */
static void
ParseBsonType(const bson_value_t *value, SchemaNode *node)
{
	if (node->validationFlags.common & CommonValidationTypes_JsonType)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg(
							"Cannot specify both $jsonSchema keywords 'type' and 'bsonType'")));
	}

	BsonTypeFlags bsonTypes = 0;
	if (value->value_type == BSON_TYPE_UTF8)
	{
		bsonTypes = GetBsonTypeEnumFromBsonTypeString(value->value.v_utf8.str);
	}
	else if (value->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);

		while (bson_iter_next(&arrayIter))
		{
			if (!BSON_ITER_HOLDS_UTF8(&arrayIter))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'bsonType' array elements must be strings")));
			}
			uint32_t length;
			const char *bsonTypeStr = bson_iter_utf8(&arrayIter, &length);
			BsonTypeFlags bsonType = GetBsonTypeEnumFromBsonTypeString(bsonTypeStr);
			if (bsonTypes & bsonType)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
								errmsg(
									"$jsonSchema keyword 'bsonType' has duplicate value: %s",
									bsonTypeStr)));
			}
			bsonTypes |= bsonType;
		}

		if (bsonTypes == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
							errmsg(
								"$jsonSchema keyword 'bsonType' must name at least one type")));
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
						errmsg(
							"$jsonSchema keyword 'bsonType' must be either a string or an array of strings")));
	}

	node->validations.common->bsonTypes = bsonTypes;
	node->validationFlags.common |= CommonValidationTypes_BsonType;
}


/*
 * This function reads the schema value for "items" keyword.
 * And stores it in the Node's Array validations section.
 */
static void
ParseItems(const bson_value_t *value, SchemaNode *node)
{
	if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t iter;
		BsonValueInitIterator(value, &iter);
		node->validations.array->itemsNode =
			(SchemaKeywordNode *) BuildSchemaTreeCore(&iter, "",
													  SchemaNodeType_Items);
		node->validationFlags.array |= ArrayValidationTypes_ItemsObject;
	}
	else if (value->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);

		while (bson_iter_next(&arrayIter))
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&arrayIter))
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'items' requires that each element of the array is an object, but found a %s",
									BsonIterTypeName(&arrayIter))));
			}

			const bson_value_t *arrayElementValue = bson_iter_value(&arrayIter);
			bson_iter_t docIter;
			BsonValueInitIterator(arrayElementValue, &docIter);
			SchemaKeywordNode *itemsNode =
				(SchemaKeywordNode *) BuildSchemaTreeCore(&docIter, "",
														  SchemaNodeType_Items);

			AppendKeywordNodeToLinkedList(&node->validations.array->itemsArray,
										  itemsNode);
		}
		node->validationFlags.array |= ArrayValidationTypes_ItemsArray;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
						errmsg(
							"$jsonSchema keyword 'items' must be an array or an object, not %s",
							BsonTypeName(value->value_type))));
	}
}


/*
 * This function reads the schema value for "additionalItems" keyword.
 * And stores it in the Node's Array validations section.
 */
static void
ParseAdditionalItems(const bson_value_t *value, SchemaNode *node)
{
	if (value->value_type == BSON_TYPE_BOOL)
	{
		node->validations.array->additionalItemsBool = value->value.v_bool;
		node->validationFlags.array |= ArrayValidationTypes_AdditionalItemsBool;
	}
	else if (value->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t iter;
		BsonValueInitIterator(value, &iter);
		node->validations.array->additionalItemsNode =
			(SchemaKeywordNode *) BuildSchemaTreeCore(&iter, "",
													  SchemaNodeType_AdditionalItems);
		node->validationFlags.array |= ArrayValidationTypes_AdditionalItemsObject;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH),
						errmsg(
							"$jsonSchema keyword 'additionalItems' must be either an object or a boolean, but got a %s",
							BsonTypeName(value->value_type))));
	}
}


/* -------------------------------------------------------- */
/*              Helper Functions                            */
/* -------------------------------------------------------- */

/*
 * Refer https://datatracker.ietf.org/doc/html/draft-zyp-json-schema-04#section-3.5 for list of jsonType
 */
static BsonTypeFlags
GetJsonTypeEnumFromJsonTypeString(const char *jsonTypeStr)
{
	if (!strcmp(jsonTypeStr, "array"))
	{
		return BsonTypeFlag_ARRAY;
	}
	else if (!strcmp(jsonTypeStr, "boolean"))
	{
		return BsonTypeFlag_BOOL;
	}
	else if (!strcmp(jsonTypeStr, "integer"))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg(
							"$jsonSchema type 'integer' is not currently supported.")));
	}
	else if (!strcmp(jsonTypeStr, "number"))
	{
		return BsonTypeFlag_INT32 | BsonTypeFlag_INT64 | BsonTypeFlag_DECIMAL128 |
			   BsonTypeFlag_DOUBLE;
	}
	else if (!strcmp(jsonTypeStr, "null"))
	{
		return BsonTypeFlag_NULL;
	}
	else if (!strcmp(jsonTypeStr, "object"))
	{
		return BsonTypeFlag_DOCUMENT;
	}
	else if (!strcmp(jsonTypeStr, "string"))
	{
		return BsonTypeFlag_UTF8;
	}

	ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
					errmsg("Unknown type name alias: %s", jsonTypeStr)));
}


/*
 * Maps bson type to flags of strings.
 */
static BsonTypeFlags
GetBsonTypeEnumFromBsonTypeString(const char *bsonTypeStr)
{
	if (!strcmp(bsonTypeStr, "double"))
	{
		return BsonTypeFlag_DOUBLE;
	}
	else if (!strcmp(bsonTypeStr, "string"))
	{
		return BsonTypeFlag_UTF8;
	}
	else if (!strcmp(bsonTypeStr, "object"))
	{
		return BsonTypeFlag_DOCUMENT;
	}
	else if (!strcmp(bsonTypeStr, "array"))
	{
		return BsonTypeFlag_ARRAY;
	}
	else if (!strcmp(bsonTypeStr, "binData"))
	{
		return BsonTypeFlag_BINARY;
	}
	else if (!strcmp(bsonTypeStr, "undefined"))
	{
		/* Deprecated */
		return BsonTypeFlag_UNDEFINED;
	}
	else if (!strcmp(bsonTypeStr, "objectId"))
	{
		return BsonTypeFlag_OID;
	}
	else if (!strcmp(bsonTypeStr, "bool"))
	{
		return BsonTypeFlag_BOOL;
	}
	else if (!strcmp(bsonTypeStr, "date"))
	{
		return BsonTypeFlag_DATE_TIME;
	}
	else if (!strcmp(bsonTypeStr, "null"))
	{
		return BsonTypeFlag_NULL;
	}
	else if (!strcmp(bsonTypeStr, "regex"))
	{
		return BsonTypeFlag_REGEX;
	}
	else if (!strcmp(bsonTypeStr, "dbPointer"))
	{
		/* Deprecated */
		return BsonTypeFlag_DBPOINTER;
	}
	else if (!strcmp(bsonTypeStr, "javascript"))
	{
		return BsonTypeFlag_CODE;
	}
	else if (!strcmp(bsonTypeStr, "symbol"))
	{
		/* Deprecated */
		return BsonTypeFlag_SYMBOL;
	}
	else if (!strcmp(bsonTypeStr, "javascriptWithScope"))
	{
		/* Deprecated in MongoDB 4.4 */
		return BsonTypeFlag_CODEWSCOPE;
	}
	else if (!strcmp(bsonTypeStr, "int"))
	{
		return BsonTypeFlag_INT32;
	}
	else if (!strcmp(bsonTypeStr, "integer"))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg(
							"$jsonSchema type 'integer' is not currently supported.")));
	}
	else if (!strcmp(bsonTypeStr, "timestamp"))
	{
		return BsonTypeFlag_TIMESTAMP;
	}
	else if (!strcmp(bsonTypeStr, "long"))
	{
		return BsonTypeFlag_INT64;
	}
	else if (!strcmp(bsonTypeStr, "decimal"))
	{
		return BsonTypeFlag_DECIMAL128;
	}
	else if (!strcmp(bsonTypeStr, "minKey"))
	{
		return BsonTypeFlag_MAXKEY;
	}
	else if (!strcmp(bsonTypeStr, "maxKey"))
	{
		return BsonTypeFlag_MINKEY;
	}

	ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
					errmsg(" Unknown type name alias: %s", bsonTypeStr)));
}


/*
 * For a given field name, this function searches and returns the matching field node,
 * from the "properties" Linked List.
 * if the field node does not exist, it creates an empty node with given field name,
 * and appends it to end of the 'properties' list.
 * TODO: Optimize this function by storing field nodes as BST (instead of Linked List)
 */
static SchemaFieldNode *
FindOrAddEmptyFieldNode(const char *field, SchemaNode *node)
{
	SchemaFieldNode *fieldNode = FindFieldNodeByName(node, field);
	if (fieldNode == NULL)
	{
		fieldNode = InitFieldNode(field);
		AppendNodeToLinkedList((SchemaNode **) &node->validations.object->properties,
							   (SchemaNode *) fieldNode);
	}
	return fieldNode;
}


/*
 * AppendKeywordNodeToLinkedList function adds a Keyword type Node to a given Linked List
 */
static inline void
AppendKeywordNodeToLinkedList(SchemaKeywordNode **head, SchemaKeywordNode *node)
{
	AppendNodeToLinkedList((SchemaNode **) head, (SchemaNode *) node);
}


/*
 * AppendNodeToLinkedList function appends any schema Node to a given Linked List of schema nodes
 */
static void
AppendNodeToLinkedList(SchemaNode **head, SchemaNode *node)
{
	if (*head == NULL)
	{
		*head = node;
	}
	else
	{
		SchemaNode *currentNode = *head;
		while (currentNode->next != NULL)
		{
			currentNode = currentNode->next;
		}
		currentNode->next = node;
	}
}


/*
 * InitSchemaNode function initializes a given SchemaNode
 */
static void
InitSchemaNode(SchemaNode *node, SchemaNodeType nodeType)
{
	node->nodeType = nodeType;

	/* For these fields, memory is allocated on demand */
	node->validations.common = NULL;
	node->validations.object = NULL;
	node->validations.string = NULL;
	node->validations.numeric = NULL;
	node->validations.array = NULL;

	node->validationFlags.common = 0;
	node->validationFlags.object = 0;
	node->validationFlags.string = 0;
	node->validationFlags.numeric = 0;
	node->validationFlags.array = 0;

	node->next = NULL;
}


static SchemaKeywordNode *
InitNonFieldNode(SchemaNodeType nodeType)
{
	SchemaKeywordNode *node = (SchemaKeywordNode *) palloc(
		sizeof(SchemaKeywordNode));
	InitSchemaNode(&(node->base), nodeType);

	node->fieldPattern = NULL;

	return node;
}


static SchemaFieldNode *
InitFieldNode(const char *field)
{
	SchemaFieldNode *node = (SchemaFieldNode *) palloc(
		sizeof(SchemaFieldNode));
	InitSchemaNode(&(node->base), SchemaNodeType_Field);

	node->field = CreateStringViewFromString(field);

	return node;
}


static void
AllocateValidators(SchemaNode *node)
{
	if (!node->validationFlags.object)
	{
		node->validations.object = InitValidationsObject();
	}

	if (!node->validationFlags.common)
	{
		node->validations.common = InitValidationsCommon();
	}

	if (!node->validationFlags.numeric)
	{
		node->validations.numeric = InitValidationsNumeric();
	}

	if (!node->validationFlags.string)
	{
		node->validations.string = InitValidationsString();
	}

	if (!node->validationFlags.array)
	{
		node->validations.array = InitValidationsArray();
	}
}


static void
FreeUnusedValidators(SchemaNode *node)
{
	if (!node->validationFlags.object)
	{
		pfree(node->validations.object);
		node->validations.object = NULL;
	}

	if (!node->validationFlags.common)
	{
		pfree(node->validations.common);
		node->validations.common = NULL;
	}

	if (!node->validationFlags.numeric)
	{
		pfree(node->validations.numeric);
		node->validations.numeric = NULL;
	}

	if (!node->validationFlags.string)
	{
		pfree(node->validations.string);
		node->validations.string = NULL;
	}

	if (!node->validationFlags.array)
	{
		pfree(node->validations.array);
		node->validations.array = NULL;
	}
}


static inline ValidationsObject *
InitValidationsObject(void)
{
	return (ValidationsObject *) palloc0(sizeof(ValidationsObject));
}


static inline ValidationsCommon *
InitValidationsCommon(void)
{
	return (ValidationsCommon *) palloc0(sizeof(ValidationsCommon));
}


static inline ValidationsNumeric *
InitValidationsNumeric(void)
{
	return (ValidationsNumeric *) palloc0(sizeof(ValidationsNumeric));
}


static inline ValidationsString *
InitValidationsString(void)
{
	return (ValidationsString *) palloc0(sizeof(ValidationsString));
}


static inline ValidationsArray *
InitValidationsArray(void)
{
	return (ValidationsArray *) palloc0(sizeof(ValidationsArray));
}


static int64_t
GetValidatedBsonIntValue(const bson_value_t *value, const char *nonPIIfield)
{
	if (!BsonTypeIsNumber(value->value_type))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg("Expected a number in: %s: %s", nonPIIfield,
							   BsonValueToJsonForLogging(value)),
						errdetail_log("Expected a number in: %s: found %s", nonPIIfield,
									  BsonTypeName(value->value_type))));
	}
	if (!IsBsonValue64BitInteger(value, false))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg("Cannot represent as a 64-bit integer: %s: %s",
							   nonPIIfield,
							   BsonValueToJsonForLogging(value)),
						errdetail_log(
							"Cannot represent as a 64-bit integer: %s: input type: %s",
							nonPIIfield,
							BsonTypeName(value->value_type))));
	}
	if (!IsBsonValueFixedInteger(value))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg("Expected an integer: %s: %s", nonPIIfield,
							   BsonValueToJsonForLogging(value)),
						errdetail_log("Expected an integer: %s: %s", nonPIIfield,
									  BsonTypeName(value->value_type))));
	}

	int64_t intValue = BsonValueAsInt64(value);
	if (intValue < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg("Expected a positive number in: %s: %s", nonPIIfield,
							   BsonValueToJsonForLogging(value)),
						errdetail_log("Expected a positive number in: %s", nonPIIfield)));
	}
	return intValue;
}
