/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_json_schema_tree.c
 *
 * Json Schema Tree generation for $jsonSchema query operator
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include "utils/hsearch.h"
#include "io/bson_core.h"
#include "query/bson_compare.h"
#include "types/decimal128.h"
#include "jsonschema/bson_json_schema_tree.h"
#include "utils/documentdb_errors.h"
#include "utils/hashset_utils.h"
#include "utils/string_view.h"
#include "commands/parse_error.h"

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
static void ParseRequired(const bson_value_t *value, SchemaNode *node);

static void ParseJsonType(const bson_value_t *value, SchemaNode *node);
static void ParseBsonType(const bson_value_t *value, SchemaNode *node);

static void ParseEncryptMetadata(const bson_value_t *value, SchemaNode *node);
static void ParseEncrypt(const bson_value_t *value, SchemaNode *node);

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

extern bool EnableSchemaEnforcementForCSFLE;

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

		if (strcmp(key, "properties") == 0)
		{
			ParseProperties(value, node);
		}
		else if (strcmp(key, "required") == 0)
		{
			ParseRequired(value, node);
		}

		/* TODO: Add other Object Validators here */

		/* -------------------------------------------------------- */
		/*              Common Validators                           */
		/* -------------------------------------------------------- */

		else if (strcmp(key, "type") == 0)
		{
			ParseJsonType(value, node);
		}
		else if (strcmp(key, "bsonType") == 0)
		{
			ParseBsonType(value, node);
		}
		else if (strcmp(key, "encrypt") == 0)
		{
			ParseEncrypt(value, node);
		}
		else if (strcmp(key, "encryptMetadata") == 0)
		{
			ParseEncryptMetadata(value, node);
		}

		/* TODO: Add other Common Validators here */

		/* -------------------------------------------------------- */
		/*              Numeric Validators                          */
		/* -------------------------------------------------------- */

		else if (strcmp(key, "multipleOf") == 0)
		{
			if (!BsonTypeIsNumber(value->value_type))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'multipleOf' must be a number")));
			}
			if ((value->value_type == BSON_TYPE_DECIMAL128 && IsDecimal128Zero(value)) ||
				(BsonValueAsDouble(value) == 0.0))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg(
									"$jsonSchema keyword 'multipleOf' must have a positive value")));
			}
			if (IsBsonValueNaN(value))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("A divisor value must not be NaN")));
			}
			if (IsBsonValueInfinity(value))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Division by an infinite value is not allowed")));
			}
			node->validations.numeric->multipleOf = (bson_value_t *) palloc0(
				sizeof(bson_value_t));
			bson_value_copy(value, node->validations.numeric->multipleOf);
			node->validationFlags.numeric |= NumericValidationTypes_MultipleOf;
		}
		else if (strcmp(key, "maximum") == 0)
		{
			if (!BsonTypeIsNumber(value->value_type))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'maximum' must be a number")));
			}
			node->validations.numeric->maximum = (bson_value_t *) palloc0(
				sizeof(bson_value_t));
			bson_value_copy(value, node->validations.numeric->maximum);
			node->validationFlags.numeric |= NumericValidationTypes_Maximum;
		}
		else if (strcmp(key, "exclusiveMaximum") == 0)
		{
			if (!BSON_ITER_HOLDS_BOOL(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'exclusiveMaximum' must be a boolean")));
			}
			node->validations.numeric->exclusiveMaximum = bson_iter_bool(schemaIter);
			node->validationFlags.numeric |= NumericValidationTypes_ExclusiveMaximum;
		}
		else if (strcmp(key, "minimum") == 0)
		{
			if (!BsonTypeIsNumber(value->value_type))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'minimum' must be a number")));
			}
			node->validations.numeric->minimum = (bson_value_t *) palloc0(
				sizeof(bson_value_t));
			bson_value_copy(value, node->validations.numeric->minimum);
			node->validationFlags.numeric |= NumericValidationTypes_Minimum;
		}
		else if (strcmp(key, "exclusiveMinimum") == 0)
		{
			if (!BSON_ITER_HOLDS_BOOL(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'exclusiveMinimum' must be a boolean")));
			}
			node->validations.numeric->exclusiveMinimum = bson_iter_bool(schemaIter);
			node->validationFlags.numeric |= NumericValidationTypes_ExclusiveMinimum;
		}

		/* -------------------------------------------------------- */
		/*              String Validators                           */
		/* -------------------------------------------------------- */

		else if (strcmp(key, "maxLength") == 0)
		{
			node->validations.string->maxLength = GetValidatedBsonIntValue(value,
																		   "maxLength");
			node->validationFlags.string |= StringValidationTypes_MaxLength;
		}
		else if (strcmp(key, "minLength") == 0)
		{
			node->validations.string->minLength = GetValidatedBsonIntValue(value,
																		   "minLength");
			node->validationFlags.string |= StringValidationTypes_MinLength;
		}
		else if (strcmp(key, "pattern") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
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

		else if (strcmp(key, "items") == 0)
		{
			ParseItems(value, node);
		}
		else if (strcmp(key, "additionalItems") == 0)
		{
			ParseAdditionalItems(value, node);
		}
		else if (strcmp(key, "maxItems") == 0)
		{
			node->validations.array->maxItems = GetValidatedBsonIntValue(value,
																		 "maxItems");
			node->validationFlags.array |= ArrayValidationTypes_MaxItems;
		}
		else if (strcmp(key, "minItems") == 0)
		{
			node->validations.array->minItems = GetValidatedBsonIntValue(value,
																		 "minItems");
			node->validationFlags.array |= ArrayValidationTypes_MinItems;
		}
		else if (strcmp(key, "uniqueItems") == 0)
		{
			if (!BSON_ITER_HOLDS_BOOL(schemaIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'uniqueItems' must be a boolean")));
			}
			node->validations.array->uniqueItems = bson_iter_bool(schemaIter);
			node->validationFlags.array |= ArrayValidationTypes_UniqueItems;
		}

		/* -------------------------------------------------------- */
		/*              Unsupported Keywords                        */
		/* -------------------------------------------------------- */

		else if (strcmp(key, "$ref") == 0 || strcmp(key, "$schema") == 0 ||
				 strcmp(key, "default") == 0 || strcmp(key, "definitions") == 0 ||
				 strcmp(key, "format") == 0 || strcmp(key, "id") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"The '%s' keyword in $jsonSchema is not supported at this time",
								key)));
		}

		/* -------------------------------------------------------- */
		/*              Unknown Keywords                            */
		/* -------------------------------------------------------- */

		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unknown $jsonSchema keyword: %s", key)));
		}
	}

	if ((node->validationFlags.numeric & NumericValidationTypes_ExclusiveMaximum) &&
		!(node->validationFlags.numeric & NumericValidationTypes_Maximum))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"$jsonSchema keyword 'maximum' must be a present if exclusiveMaximum is present")));
	}

	if ((node->validationFlags.numeric & NumericValidationTypes_ExclusiveMinimum) &&
		!(node->validationFlags.numeric & NumericValidationTypes_Minimum))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"$jsonSchema keyword 'minimum' must be a present if exclusiveMinimum is present")));
	}

	/*Encrypt alongside type/bsontype should fail to parse. */
	if (node->validationFlags.binary & BinaryValidationTypes_Encrypt &&
		(node->validationFlags.common & (CommonValidationTypes_JsonType |
										 CommonValidationTypes_BsonType)))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"'encrypt' implies 'bsonType: BinData' and cannot be combined with 'type' in $jsonSchema")));
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Dot notation paths are currently not supported: %s",
								   field)));
		}

		const bson_value_t *fieldValue = bson_iter_value(&iter);
		if (!BSON_ITER_HOLDS_DOCUMENT(&iter))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
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
 * This function reads the schema value for "required" keyword.
 * And stores it in the parent Node's Object validations section.
 */
static void
ParseRequired(const bson_value_t *value, SchemaNode *node)
{
	if (value->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"Expected an array for 'required' in $jsonSchema, but got %s",
							BsonTypeName(value->value_type))));
	}
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);
	bson_iter_t iter;
	BsonValueInitIterator(value, &iter);

	/* Hash table to track seen field names for efficient duplicate detection */
	HTAB *seenFieldsHash = CreateStringViewHashSet();

	while (bson_iter_next(&iter))
	{
		if (!BSON_ITER_HOLDS_UTF8(&iter))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"All elements in 'required' array must be strings, but encountered %s",
								BsonIterTypeName(&iter))));
		}
		const char *field = bson_iter_utf8(&iter, NULL);

		/* TODO: Add support for dot notation paths */
		if (strchr(field, '.') != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Dot notation paths are currently not supported: %s",
								   field)));
		}

		/* Check for duplicate field names using hash table */
		StringView fieldView = CreateStringViewFromString(field);
		bool found;
		hash_search(seenFieldsHash, &fieldView, HASH_ENTER, &found);

		if (found)
		{
			hash_destroy(seenFieldsHash);
			PgbsonWriterFree(&writer);
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"Duplicate field name in $jsonSchema 'required' array")));
		}

		PgbsonArrayWriterWriteUtf8(&arrayWriter, field);
	}

	/* Clean up hash table */
	hash_destroy(seenFieldsHash);
	PgbsonWriterEndArray(&writer, &arrayWriter);
	node->validations.object->required = (bson_value_t *) palloc0(
		sizeof(bson_value_t));
	PgbsonArrayWriterCopyDataToBsonValue(&arrayWriter,
										 node->validations.object->required);
	PgbsonWriterFree(&writer);
	node->validationFlags.object |= ObjectValidationTypes_Required;
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'type' array elements must be strings")));
			}
			uint32_t length;
			const char *jsonTypeStr = bson_iter_utf8(&arrayIter, &length);
			BsonTypeFlags jsonType = GetJsonTypeEnumFromJsonTypeString(jsonTypeStr);
			if (jsonTypes & jsonType)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg(
									"$jsonSchema keyword 'type' has duplicate value: %s",
									jsonTypeStr)));
			}
			jsonTypes |= jsonType;
		}

		if (jsonTypes == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"$jsonSchema keyword 'type' must name at least one type")));
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"The 'type' property in $jsonSchema must be defined as either a single string or an array containing multiple strings")));
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'bsonType' array elements must be strings")));
			}
			uint32_t length;
			const char *bsonTypeStr = bson_iter_utf8(&arrayIter, &length);
			BsonTypeFlags bsonType = GetBsonTypeEnumFromBsonTypeString(bsonTypeStr);
			if (bsonTypes & bsonType)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg(
									"$jsonSchema keyword 'bsonType' has duplicate value: %s",
									bsonTypeStr)));
			}
			bsonTypes |= bsonType;
		}

		if (bsonTypes == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"$jsonSchema keyword 'bsonType' must name at least one type")));
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"$jsonSchema keyword 'bsonType' must be either a string or an array of strings")));
	}

	node->validations.common->bsonTypes = bsonTypes;
	node->validationFlags.common |= CommonValidationTypes_BsonType;
}


/*
 * ParseEncryptMetadata function reads the schema value for "encrypt" keyword.
 * And stores it in the Node's common validations section.
 */
static void
ParseEncrypt(const bson_value_t *value, SchemaNode *node)
{
	if (!EnableSchemaEnforcementForCSFLE)
	{
		return;
	}

	if (value->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"$jsonSchema keyword 'encrypt' must be a document")));
	}
	bson_iter_t encryptIter;
	BsonValueInitIterator(value, &encryptIter);
	while (bson_iter_next(&encryptIter))
	{
		const char *key = bson_iter_key(&encryptIter);
		if (strcmp(key, "keyId") == 0)
		{
			EnsureTopLevelFieldType("encrypt.keyId", &encryptIter,
									BSON_TYPE_ARRAY);
		}
		else if (strcmp(key, "algorithm") == 0)
		{
			EnsureTopLevelFieldType("encrypt.algorithm", &encryptIter,
									BSON_TYPE_UTF8);
		}
		else if (strcmp(key, "bsonType") == 0)
		{
			if (bson_iter_type(&encryptIter) != BSON_TYPE_ARRAY && bson_iter_type(
					&encryptIter) != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"$jsonSchema keyword 'bsonType' must be either a string or an array of strings")));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg(
								"The BSON field 'encrypt.%s' is not recognized as a valid field.",
								key)));
		}
	}
	node->validationFlags.binary |= BinaryValidationTypes_Encrypt;
}


/*
 * ParseEncryptMetadata function reads the schema value for "encryptMetadata" keyword.
 * And stores it in the Node's common validations section.
 */
static void
ParseEncryptMetadata(const bson_value_t *value, SchemaNode *node)
{
	if (!EnableSchemaEnforcementForCSFLE)
	{
		return;
	}
	if (value->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"$jsonSchema keyword 'encryptMetadata' must be a document")));
	}

	if (IsBsonValueEmptyDocument(value))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"$jsonSchema keyword 'encryptMetadata' cannot be an empty object")));
	}
	bson_iter_t encryptMetadataIter;
	BsonValueInitIterator(value, &encryptMetadataIter);
	while (bson_iter_next(&encryptMetadataIter))
	{
		const char *key = bson_iter_key(&encryptMetadataIter);
		if (strcmp(key, "keyId") == 0)
		{
			EnsureTopLevelFieldType("encryptMetadata.keyId", &encryptMetadataIter,
									BSON_TYPE_ARRAY);
		}
		else if (strcmp(key, "algorithm") == 0)
		{
			EnsureTopLevelFieldType("encryptMetadata.algorithm",
									&encryptMetadataIter, BSON_TYPE_UTF8);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg(
								"The BSON field 'encryptMetadata.%s' is not recognized as a valid or supported field.",
								key)));
		}
	}
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
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
	if (strcmp(jsonTypeStr, "array") == 0)
	{
		return BsonTypeFlag_ARRAY;
	}
	else if (strcmp(jsonTypeStr, "boolean") == 0)
	{
		return BsonTypeFlag_BOOL;
	}
	else if (strcmp(jsonTypeStr, "integer") == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"The 'integer' type in $jsonSchema is not supported at this time.")));
	}
	else if (strcmp(jsonTypeStr, "number") == 0)
	{
		return BsonTypeFlag_INT32 | BsonTypeFlag_INT64 | BsonTypeFlag_DECIMAL128 |
			   BsonTypeFlag_DOUBLE;
	}
	else if (strcmp(jsonTypeStr, "null") == 0)
	{
		return BsonTypeFlag_NULL;
	}
	else if (strcmp(jsonTypeStr, "object") == 0)
	{
		return BsonTypeFlag_DOCUMENT;
	}
	else if (strcmp(jsonTypeStr, "string") == 0)
	{
		return BsonTypeFlag_UTF8;
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg("Unrecognized data type alias: %s", jsonTypeStr)));
}


/*
 * Maps bson type to flags of strings.
 */
static BsonTypeFlags
GetBsonTypeEnumFromBsonTypeString(const char *bsonTypeStr)
{
	if (strcmp(bsonTypeStr, "double") == 0)
	{
		return BsonTypeFlag_DOUBLE;
	}
	else if (strcmp(bsonTypeStr, "string") == 0)
	{
		return BsonTypeFlag_UTF8;
	}
	else if (strcmp(bsonTypeStr, "object") == 0)
	{
		return BsonTypeFlag_DOCUMENT;
	}
	else if (strcmp(bsonTypeStr, "array") == 0)
	{
		return BsonTypeFlag_ARRAY;
	}
	else if (strcmp(bsonTypeStr, "binData") == 0)
	{
		return BsonTypeFlag_BINARY;
	}
	else if (strcmp(bsonTypeStr, "undefined") == 0)
	{
		/* Deprecated */
		return BsonTypeFlag_UNDEFINED;
	}
	else if (strcmp(bsonTypeStr, "objectId") == 0)
	{
		return BsonTypeFlag_OID;
	}
	else if (strcmp(bsonTypeStr, "bool") == 0)
	{
		return BsonTypeFlag_BOOL;
	}
	else if (strcmp(bsonTypeStr, "date") == 0)
	{
		return BsonTypeFlag_DATE_TIME;
	}
	else if (strcmp(bsonTypeStr, "null") == 0)
	{
		return BsonTypeFlag_NULL;
	}
	else if (strcmp(bsonTypeStr, "regex") == 0)
	{
		return BsonTypeFlag_REGEX;
	}
	else if (strcmp(bsonTypeStr, "dbPointer") == 0)
	{
		/* Deprecated */
		return BsonTypeFlag_DBPOINTER;
	}
	else if (strcmp(bsonTypeStr, "javascript") == 0)
	{
		return BsonTypeFlag_CODE;
	}
	else if (strcmp(bsonTypeStr, "symbol") == 0)
	{
		/* Deprecated */
		return BsonTypeFlag_SYMBOL;
	}
	else if (strcmp(bsonTypeStr, "javascriptWithScope") == 0)
	{
		/* Deprecated */
		return BsonTypeFlag_CODEWSCOPE;
	}
	else if (strcmp(bsonTypeStr, "int") == 0)
	{
		return BsonTypeFlag_INT32;
	}
	else if (strcmp(bsonTypeStr, "integer") == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"The 'integer' type in $jsonSchema is not supported at this time.")));
	}
	else if (strcmp(bsonTypeStr, "timestamp") == 0)
	{
		return BsonTypeFlag_TIMESTAMP;
	}
	else if (strcmp(bsonTypeStr, "long") == 0)
	{
		return BsonTypeFlag_INT64;
	}
	else if (strcmp(bsonTypeStr, "decimal") == 0)
	{
		return BsonTypeFlag_DECIMAL128;
	}
	else if (strcmp(bsonTypeStr, "minKey") == 0)
	{
		return BsonTypeFlag_MAXKEY;
	}
	else if (strcmp(bsonTypeStr, "maxKey") == 0)
	{
		return BsonTypeFlag_MINKEY;
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg(" Unrecognized data type alias: %s", bsonTypeStr)));
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
	node->validationFlags.binary = 0;

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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"A numeric value was expected in %s, but instead %s was encountered",
							nonPIIfield,
							BsonValueToJsonForLogging(value)),
						errdetail_log(
							"A numeric value was expected in %s, but instead %s was encountered",
							nonPIIfield,
							BsonTypeName(value->value_type))));
	}
	if (!IsBsonValue64BitInteger(value, false))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Unable to store value as a 64-bit integer: %s: %s",
							   nonPIIfield,
							   BsonValueToJsonForLogging(value)),
						errdetail_log(
							"Unable to store value as a 64-bit integer: %s, input type is %s",
							nonPIIfield,
							BsonTypeName(value->value_type))));
	}
	if (!IsBsonValueFixedInteger(value))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Expected value of integer type: %s: %s", nonPIIfield,
							   BsonValueToJsonForLogging(value)),
						errdetail_log("Expected value of integer type: %s: %s",
									  nonPIIfield,
									  BsonTypeName(value->value_type))));
	}

	int64_t intValue = BsonValueAsInt64(value);
	if (intValue < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Value for %s cannot be zero or negative. Got %s",
							   nonPIIfield,
							   BsonValueToJsonForLogging(value)),
						errdetail_log("Value for %s cannot be zero or negative. Got %s",
									  nonPIIfield, BsonValueToJsonForLogging(value))));
	}
	return intValue;
}
