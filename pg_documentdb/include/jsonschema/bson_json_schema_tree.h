/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_json_schema_tree.h
 *
 * Common declarations of structs and functions for handling bson json schema tree.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_JSON_SCHEMA_TREE_H
#define BSON_JSON_SCHEMA_TREE_H

#include "query/helio_bson_compare.h"

#include "types/pcre_regex.h"
#include "query/bson_dollar_operators.h"

/* -------------------------------------------------------- */
/*                  Data types                              */
/* -------------------------------------------------------- */

typedef struct SchemaNode SchemaNode;
typedef struct SchemaFieldNode SchemaFieldNode;
typedef struct SchemaKeywordNode SchemaKeywordNode;

typedef enum BsonTypeFlags
{
	BsonTypeFlag_EOD = 1 << 0,
	BsonTypeFlag_DOUBLE = 1 << 1,
	BsonTypeFlag_UTF8 = 1 << 2,
	BsonTypeFlag_DOCUMENT = 1 << 3,
	BsonTypeFlag_ARRAY = 1 << 4,
	BsonTypeFlag_BINARY = 1 << 5,
	BsonTypeFlag_UNDEFINED = 1 << 6,
	BsonTypeFlag_OID = 1 << 7,
	BsonTypeFlag_BOOL = 1 << 8,
	BsonTypeFlag_DATE_TIME = 1 << 9,
	BsonTypeFlag_NULL = 1 << 10,
	BsonTypeFlag_REGEX = 1 << 11,
	BsonTypeFlag_DBPOINTER = 1 << 12,
	BsonTypeFlag_CODE = 1 << 13,
	BsonTypeFlag_SYMBOL = 1 << 14,
	BsonTypeFlag_CODEWSCOPE = 1 << 15,
	BsonTypeFlag_INT32 = 1 << 16,
	BsonTypeFlag_TIMESTAMP = 1 << 17,
	BsonTypeFlag_INT64 = 1 << 18,
	BsonTypeFlag_DECIMAL128 = 1 << 19,
	BsonTypeFlag_MINKEY = 1 << 20,
	BsonTypeFlag_MAXKEY = 1 << 21
} BsonTypeFlags;

typedef enum ObjectValidationTypes
{
	ObjectValidationTypes_MaxProperties = 1 << 0,
	ObjectValidationTypes_MinProperties = 1 << 1,
	ObjectValidationTypes_Required = 1 << 2,
	ObjectValidationTypes_Properties = 1 << 3,
	ObjectValidationTypes_PatternProperties = 1 << 4,
	ObjectValidationTypes_AdditionalPropertiesBool = 1 << 5,
	ObjectValidationTypes_AdditionalPropertiesObject = 1 << 6,
	ObjectValidationTypes_Dependency = 1 << 7,
	ObjectValidationTypes_DependencyArray = 1 << 8,
	ObjectValidationTypes_DependencyObject = 1 << 9,
} ObjectValidationTypes;

typedef enum CommonValidationTypes
{
	CommonValidationTypes_Enum = 1 << 0,
	CommonValidationTypes_JsonType = 1 << 1,
	CommonValidationTypes_BsonType = 1 << 2,
	CommonValidationTypes_AllOf = 1 << 3,
	CommonValidationTypes_AnyOf = 1 << 4,
	CommonValidationTypes_OneOf = 1 << 5,
	CommonValidationTypes_Not = 1 << 6
} CommonValidationTypes;

typedef enum NumericValidationTypes
{
	NumericValidationTypes_MultipleOf = 1 << 0,
	NumericValidationTypes_Maximum = 1 << 1,
	NumericValidationTypes_ExclusiveMaximum = 1 << 2,
	NumericValidationTypes_Minimum = 1 << 3,
	NumericValidationTypes_ExclusiveMinimum = 1 << 4,
} NumericValidationTypes;

typedef enum StringValidationTypes
{
	StringValidationTypes_MaxLength = 1 << 0,
	StringValidationTypes_MinLength = 1 << 1,
	StringValidationTypes_Pattern = 1 << 2,
} StringValidationTypes;

typedef enum ArrayValidationTypes
{
	ArrayValidationTypes_MaxItems = 1 << 0,
	ArrayValidationTypes_MinItems = 1 << 1,
	ArrayValidationTypes_UniqueItems = 1 << 2,
	ArrayValidationTypes_ItemsObject = 1 << 3,
	ArrayValidationTypes_ItemsArray = 1 << 4,
	ArrayValidationTypes_AdditionalItemsBool = 1 << 5,
	ArrayValidationTypes_AdditionalItemsObject = 1 << 6,
} ArrayValidationTypes;

typedef enum SchemaNodeType
{
	SchemaNodeType_Invalid = 0,
	SchemaNodeType_Field,
	SchemaNodeType_Root,
	SchemaNodeType_AdditionalProperties,
	SchemaNodeType_PatternProperties,
	SchemaNodeType_Dependencies,
	SchemaNodeType_Items,
	SchemaNodeType_AdditionalItems,
	SchemaNodeType_AllOf,
	SchemaNodeType_AnyOf,
	SchemaNodeType_OneOf,
	SchemaNodeType_Not,
} SchemaNodeType;


typedef struct ValidationsObject
{
	/* List of child field nodes */
	SchemaFieldNode *properties;
} ValidationsObject;

typedef struct ValidationsCommon
{
	BsonTypeFlags jsonTypes;
	BsonTypeFlags bsonTypes;
} ValidationsCommon;

typedef struct ValidationsNumeric
{
	bson_value_t *maximum;
	bool exclusiveMaximum;
	bson_value_t *minimum;
	bool exclusiveMinimum;
	bson_value_t *multipleOf;
} ValidationsNumeric;

typedef struct ValidationsString
{
	uint32_t maxLength;
	uint32_t minLength;
	RegexData *pattern;
} ValidationsString;

typedef struct ValidationsArray
{
	uint32_t maxItems;
	uint32_t minItems;
	bool uniqueItems;
	union
	{
		SchemaKeywordNode *itemsNode;

		/* List of dependency item schemas */
		SchemaKeywordNode *itemsArray;
	};
	union
	{
		bool additionalItemsBool;
		SchemaKeywordNode *additionalItemsNode;
	};
} ValidationsArray;


typedef struct Validations
{
	ValidationsObject *object;
	ValidationsCommon *common;
	ValidationsNumeric *numeric;
	ValidationsString *string;
	ValidationsArray *array;
}Validations;

typedef struct ValidationFlags
{
	uint16_t object;
	uint16_t common;
	uint16_t numeric;
	uint16_t string;
	uint16_t array;
}ValidationFlags;

struct SchemaNode
{
	SchemaNodeType nodeType;

	Validations validations;
	ValidationFlags validationFlags;

	/* link to next node of linked list (e.g. sibling node) */
	struct SchemaNode *next;
};

struct SchemaFieldNode
{
	SchemaNode base;

	/* This is the non-dotted field path at the current level */
	StringView field;
};

struct SchemaKeywordNode
{
	SchemaNode base;
	RegexData *fieldPattern;
};


typedef struct SchemaTreeState
{
	SchemaNode *rootNode;
}SchemaTreeState;

/* -------------------------------------------------------- */
/*                  Functions                               */
/* -------------------------------------------------------- */

void BuildSchemaTree(SchemaTreeState *treeState, bson_iter_t *schemaIter);
SchemaFieldNode * FindFieldNodeByName(const SchemaNode *parent, const
									  char *field);

#endif
