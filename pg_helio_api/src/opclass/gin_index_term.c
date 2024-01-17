/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/gin_index_term.c
 *
 * Serialization and storage of index terms in the GIN/RUM index.
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include "opclass/helio_gin_index_term.h"
#include "query/helio_bson_compare.h"
#include "utils/mongo_errors.h"
#include "types/decimal128.h"
#include "io/bsonvalue_utils.h"

/* --------------------------------------------------------- */
/* Forward Declaration */
/* --------------------------------------------------------- */
static bool SerializeTermToWriter(pgbson_writer *writer, pgbsonelement *indexElement,
								  const IndexTermCreateMetadata *termMetadata);

static BsonIndexTermSerialized SerializeBsonIndexTermCore(pgbsonelement *indexElement,
														  const IndexTermCreateMetadata *
														  createMetadata,
														  bool forceTruncated);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(gin_bson_compare);


/*
 * gin_bson_compare compares two elements to sort the values
 * when placed in the gin index. This is different from a regular BSON
 * compare as Bson compares are done as <type><path><value>
 * gin_bson_compare compares the top level entry as path first,
 * then by sort order type and then resumes regular comparisons on value.
 */
Datum
gin_bson_compare(PG_FUNCTION_ARGS)
{
	bytea *left = PG_GETARG_BYTEA_P(0);
	bytea *right = PG_GETARG_BYTEA_P(1);

	BsonIndexTerm leftTerm;
	BsonIndexTerm rightTerm;

	InitializeBsonIndexTerm(left, &leftTerm);
	InitializeBsonIndexTerm(right, &rightTerm);
	bool isComparisonValidIgnore = false;
	return CompareBsonIndexTerm(&leftTerm, &rightTerm, &isComparisonValidIgnore);
}


/*
 * Implements the core logic for comparing index terms.
 * Index terms are compared first by path, then value.
 * If the values are equal, a truncated value is considered greater than
 * a non-truncated value.
 */
int32_t
CompareBsonIndexTerm(BsonIndexTerm *leftTerm, BsonIndexTerm *rightTerm,
					 bool *isComparisonValid)
{
	/* Compare path */
	StringView leftView = {
		.length = leftTerm->element.pathLength, .string = leftTerm->element.path
	};
	StringView rightView = {
		.length = rightTerm->element.pathLength, .string = rightTerm->element.path
	};
	int32_t cmp = CompareStringView(&leftView, &rightView);
	if (cmp != 0)
	{
		PG_RETURN_INT32(cmp);
	}

	/* We explicitly ignore the validity of the comparisons since this is applying
	 * a sort operation on types and values
	 */
	cmp = CompareBsonValueAndType(&leftTerm->element.bsonValue,
								  &rightTerm->element.bsonValue,
								  isComparisonValid);
	if (cmp != 0)
	{
		return cmp;
	}

	/* Both terms are equal by value - compare for truncation */
	if (leftTerm->isIndexTermTruncated ^ rightTerm->isIndexTermTruncated)
	{
		return (int32_t) leftTerm->isIndexTermTruncated -
			   (int32_t) rightTerm->isIndexTermTruncated;
	}

	return 0;
}


/*
 * Returns true if the serialized index term is truncated.
 */
bool
IsSerializedIndexTermTruncated(bytea *indexTermSerialized)
{
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);
	uint32_t indexTermSize PG_USED_FOR_ASSERTS_ONLY = VARSIZE_ANY_EXHDR(
		indexTermSerialized);

	/* size must be bigger than metadata + bson overhead */
	Assert(indexTermSize > (sizeof(uint8_t) + 5));

	return buffer[0] != 0;
}


/*
 * Initializes the BsonIndexTerm from the serialized index format.
 */
void
InitializeBsonIndexTerm(bytea *indexTermSerialized, BsonIndexTerm *indexTerm)
{
	uint32_t indexTermSize = VARSIZE_ANY_EXHDR(indexTermSerialized);
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);

	/* size must be bigger than metadata + bson overhead */
	Assert(indexTermSize > (sizeof(uint8_t) + 5));

	/* First we have the metadata */
	indexTerm->isIndexTermTruncated = buffer[0] != 0;

	/* Next is the bson data serialized */
	bson_value_t value;
	value.value_type = BSON_TYPE_DOCUMENT;
	value.value.v_doc.data_len = indexTermSize - 1;
	value.value.v_doc.data = (uint8_t *) &buffer[1];

	BsonValueToPgbsonElement(&value, &indexTerm->element);
}


/*
 * Truncate binarystring like terms - this includes types such as UTF8 strings,
 * binary, symbol, etc.
 */
static bool
TruncateStringOrBinaryTerm(int32_t dataSize, int32_t indexTermSizeLimit,
						   bson_type_t bsonValueType,
						   uint32_t *lengthPointer)
{
	/* String: strlen + chars + \0 */
	/* Binary: binLen + subtype + bytes* */
	int32_t stringLen = (int32_t) *lengthPointer;
	int32_t requiredLength = dataSize + sizeof(int32_t) + stringLen + 1;

	bool truncated = false;
	if (requiredLength > indexTermSizeLimit)
	{
		int32_t excess = requiredLength - indexTermSizeLimit;
		if (excess >= stringLen)
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg(
								"Cannot create index key required length %d for type %s exceeds max size %d.",
								stringLen, BsonTypeName(bsonValueType),
								indexTermSizeLimit),
							errhint(
								"Cannot create index key required length %d for type %s exceeds max size %d.",
								stringLen, BsonTypeName(bsonValueType),
								indexTermSizeLimit)));
		}

		*lengthPointer -= excess;
		truncated = true;
	}

	return truncated;
}


/*
 * Write and truncate an array into the target array writer accounting
 * for the index term size limit.
 */
static bool
TruncateArrayTerm(int32_t dataSize, int32_t indexTermSizeLimit,
				  bson_iter_t *arrayIterator, pgbson_array_writer *arrayWriter)
{
	bool forceAsNotTruncated = false;
	int32_t currentLength = 0;
	while (bson_iter_next(arrayIterator))
	{
		/* Get the current size */
		currentLength = (int32_t) PgbsonArrayWriterGetSize(arrayWriter);
		if (currentLength + dataSize > indexTermSizeLimit)
		{
			/* We've exceeded the limit - mark as truncated */
			return !forceAsNotTruncated;
		}
		else
		{
			/* We're under the size limit reset */
			forceAsNotTruncated = false;
		}

		const bson_value_t *iterValue = bson_iter_value(arrayIterator);
		switch (iterValue->value_type)
		{
			/* Fixed size values - just write it out */
			case BSON_TYPE_MAXKEY:
			case BSON_TYPE_MINKEY:
			case BSON_TYPE_BOOL:
			case BSON_TYPE_DATE_TIME:
			case BSON_TYPE_NULL:
			case BSON_TYPE_OID:
			case BSON_TYPE_TIMESTAMP:
			case BSON_TYPE_UNDEFINED:
			{
				/* Primitive values aren't truncated */
				PgbsonArrayWriterWriteValue(arrayWriter, iterValue);
				continue;
			}

			case BSON_TYPE_DECIMAL128:
			case BSON_TYPE_DOUBLE:
			case BSON_TYPE_INT32:
			case BSON_TYPE_INT64:
			{
				/* These are variable length but can be compared together
				 * To ensure we have proper comparison in the middle of an array,
				 * Convert them to a unified type. This is needed to handle
				 * terms that come after the number.
				 * e.g. if you had an array
				 * [ Int32(1), "123456789012345" ]
				 * and
				 * [ Decimal128(1), "123456789012345" ]
				 *
				 * These 2 arrays are and should be able to be read via a query of
				 * $eq: [ Double(1), "1234567890123456789" ]
				 *
				 * If the truncation limit was say 18 bytes we would get
				 * (ignoring structural overhead):
				 * [ Int32(1), "123456789012345"] (not truncated)
				 * [ Decimal128(1), "12" ] (truncated)
				 * which can produce incorrect results.
				 *
				 * So we convert all numerics to a common value.
				 */
				bson_value_t numericValue = *iterValue;
				bool checkFixedInteger = true;
				if (IsBsonValue32BitInteger(iterValue, checkFixedInteger))
				{
					numericValue.value.v_int32 = BsonValueAsInt32(iterValue);
					numericValue.value_type = BSON_TYPE_INT32;
				}
				else if (IsBsonValue64BitInteger(iterValue, checkFixedInteger))
				{
					numericValue.value.v_int64 = BsonValueAsInt64(iterValue);
					numericValue.value_type = BSON_TYPE_INT64;
				}
				else
				{
					/*
					 * We convert to double to save some space if applicable
					 * That way we always store it as the smallest possible size
					 */
					numericValue.value.v_decimal128 = GetBsonValueAsDecimal128(iterValue);
					numericValue.value_type = BSON_TYPE_DECIMAL128;

					if (IsDecimal128InDoubleRange(&numericValue))
					{
						numericValue.value.v_double = BsonValueAsDouble(&numericValue);
						numericValue.value_type = BSON_TYPE_DOUBLE;
					}
				}
				PgbsonArrayWriterWriteValue(arrayWriter, &numericValue);
				continue;
			}

			case BSON_TYPE_CODEWSCOPE:
			case BSON_TYPE_DBPOINTER:
			case BSON_TYPE_DOCUMENT:
			case BSON_TYPE_ARRAY:
			case BSON_TYPE_REGEX:
			case BSON_TYPE_BINARY:
			case BSON_TYPE_CODE:
			{
				/* TODO: Support truncating this? */
				forceAsNotTruncated = true;
				PgbsonArrayWriterWriteValue(arrayWriter, iterValue);
				break;
			}

			case BSON_TYPE_SYMBOL:
			case BSON_TYPE_UTF8:
			{
				int valueSizeLimit = indexTermSizeLimit - currentLength;
				bson_value_t valueCopy = *iterValue;
				const int32_t stringPrefixLength = 4;
				bool isTruncated = TruncateStringOrBinaryTerm(
					stringPrefixLength,
					valueSizeLimit, iterValue->value_type,
					&valueCopy.value.v_utf8.len);
				PgbsonArrayWriterWriteValue(arrayWriter, &valueCopy);

				if (isTruncated)
				{
					/* Stop writing any more */
					return true;
				}
				break;
			}

			default:
			{
				forceAsNotTruncated = true;
				PgbsonArrayWriterWriteValue(arrayWriter, iterValue);
				break;
			}
		}
	}

	currentLength = (int32_t) PgbsonArrayWriterGetSize(arrayWriter);
	if (currentLength + dataSize > indexTermSizeLimit)
	{
		/* We've exceeded the limit - mark as truncated */
		return !forceAsNotTruncated;
	}

	/* Not truncated */
	return false;
}


/*
 * Serializes a given term to a pgbson writer honoring the truncation limits.
 * Returns whether the term was truncated or not.
 */
static bool
SerializeTermToWriter(pgbson_writer *writer, pgbsonelement *indexElement,
					  const IndexTermCreateMetadata *termMetadata)
{
	/* Bson size + \0 overhead + path + \0 + type marker */
	StringView indexPath =
	{
		.length = indexElement->pathLength,
		.string = indexElement->path
	};

	if (termMetadata->pathPrefix.length > 0)
	{
		if (StringViewEquals(&indexPath, &termMetadata->pathPrefix))
		{
			/* Index term should occupy a minimal amount of space */
			indexPath.length = 1;
			indexPath.string = "$";
		}
		else if (StringViewStartsWithStringView(&indexPath, &termMetadata->pathPrefix))
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg("Wildcard Prefix with path truncation not supported"),
							errhint(
								"Wildcard Prefix with path truncation not supported")));
		}
	}

	if (termMetadata->indexTermSizeLimit <= 0)
	{
		PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
								&indexElement->bsonValue);
		return false;
	}

	int32_t dataSize = 5 + indexPath.length + 2;

	if (dataSize >= termMetadata->indexTermSizeLimit)
	{
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg(
							"Cannot create index key because the path length %d exceeds truncation limit %d.",
							dataSize, termMetadata->indexTermSizeLimit),
						errhint(
							"Cannot create index key because the path length %d exceeds truncation limit %d.",
							dataSize, termMetadata->indexTermSizeLimit)));
	}

	switch (indexElement->bsonValue.value_type)
	{
		/* These types are not truncated */
		case BSON_TYPE_BOOL:
		case BSON_TYPE_DATE_TIME:
		case BSON_TYPE_DECIMAL128:
		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_MAXKEY:
		case BSON_TYPE_MINKEY:
		case BSON_TYPE_NULL:
		case BSON_TYPE_OID:
		case BSON_TYPE_TIMESTAMP:
		case BSON_TYPE_UNDEFINED:
		{
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return false;
		}

		case BSON_TYPE_ARRAY:
		{
			/* The fixed-size term sizes are
			 * 1 byte: bool, maxkey, minkey, null, undefined
			 * 4 bytes: int32,
			 * 8 bytes:datetime, double, int64, timestamp
			 * 12 bytes: oid
			 * 16 bytes: decimal128
			 * For an array term, the value length is
			 * <index-as-path> <typeCode> <value>
			 * Given ~2500 bytes, with the "smallest" type which is 1 byte,
			 * we would expect indexes of <= 900. Consequently, if you presume
			 * that the index is 4 bytes long ("900\0"), then the largest
			 * possible value is 21 bytes
			 * (4 bytes for path, 1 byte for typecode, 16 bytes for value)
			 * So in order to ensure that comparisons work as expected,
			 * array truncation limit is the overall truncation limit - 21
			 * (the largest fixed size value). This ensures that when we
			 * hit the limit, we can at least write 1 more fixed size
			 * array value.
			 *
			 * Note we can't short-cut and simply write the array if it's shorter
			 * due to numeric value rewrites (see TruncateArrayTerm)
			 */
			int32_t fixedTermSize = 21;
			int32_t loweredSizeLimit = termMetadata->indexTermSizeLimit - fixedTermSize;

			bson_iter_t arrayIterator;
			pgbson_array_writer arrayWriter;
			bool truncated = false;
			BsonValueInitIterator(&indexElement->bsonValue, &arrayIterator);
			PgbsonWriterStartArray(writer, indexPath.string, indexPath.length,
								   &arrayWriter);
			truncated = TruncateArrayTerm(dataSize, loweredSizeLimit, &arrayIterator,
										  &arrayWriter);
			PgbsonWriterEndArray(writer, &arrayWriter);
			return truncated;
		}

		/* TODO: These types need to be truncated */
		case BSON_TYPE_CODEWSCOPE:
		case BSON_TYPE_DBPOINTER:
		case BSON_TYPE_DOCUMENT:
		{
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return false;
		}

		case BSON_TYPE_REGEX:
		{
			/* Regex truncation is in the history but removed for now;
			 * TODO: Add this back.
			 */
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return false;
		}

		case BSON_TYPE_BINARY:
		{
			bool truncated = TruncateStringOrBinaryTerm(dataSize,
														termMetadata->indexTermSizeLimit,
														BSON_TYPE_BINARY,
														&indexElement->bsonValue.value.
														v_binary.data_len);
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return truncated;
		}

		case BSON_TYPE_CODE:
		{
			bool truncated = TruncateStringOrBinaryTerm(dataSize,
														termMetadata->indexTermSizeLimit,
														BSON_TYPE_CODE,
														&indexElement->bsonValue.value.
														v_code.code_len);

			/* Code ignores code_len when writing. We need to manually truncate */
			if (truncated)
			{
				indexElement->bsonValue.value.v_code.code = pnstrdup(
					indexElement->bsonValue.value.v_code.code,
					indexElement->
					bsonValue.value.v_code.code_len);
			}

			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return truncated;
		}

		case BSON_TYPE_SYMBOL:
		{
			bool truncated = TruncateStringOrBinaryTerm(dataSize,
														termMetadata->indexTermSizeLimit,
														BSON_TYPE_SYMBOL,
														&indexElement->bsonValue.value.
														v_symbol.len);
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return truncated;
		}

		case BSON_TYPE_UTF8:
		{
			bool truncated = TruncateStringOrBinaryTerm(dataSize,
														termMetadata->indexTermSizeLimit,
														BSON_TYPE_UTF8,
														&indexElement->bsonValue.value.
														v_utf8.len);
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return truncated;
		}

		default:
		{
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			return false;
		}
	}
}


/*
 * Internal Core method that serializes the term and returns the
 * serialized term. ALlows for the term to be force marked truncated
 * This is useful for the "root" truncated term.
 */
static BsonIndexTermSerialized
SerializeBsonIndexTermCore(pgbsonelement *indexElement,
						   const IndexTermCreateMetadata *createMetadata,
						   bool forceTruncated)
{
	BsonIndexTerm indexTerm = {
		0, { 0 }
	};
	indexTerm.isIndexTermTruncated = false;

	BsonIndexTermSerialized serializedTerm = {
		0, NULL
	};

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	indexTerm.isIndexTermTruncated = SerializeTermToWriter(&writer, indexElement,
														   createMetadata);
	if (forceTruncated)
	{
		indexTerm.isIndexTermTruncated = true;
	}

	uint32_t dataSize = PgbsonWriterGetSize(&writer);

	if (createMetadata->indexTermSizeLimit > 0 &&
		dataSize > (uint32_t) createMetadata->indexTermSizeLimit)
	{
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg(
							"Truncation size limit specified %d, but index term with type %s was larger %d",
							createMetadata->indexTermSizeLimit, BsonTypeName(
								indexElement->bsonValue.value_type), dataSize),
						errhint(
							"Truncation size limit specified %d, but index term with type %s was larger %d",
							createMetadata->indexTermSizeLimit, BsonTypeName(
								indexElement->bsonValue.value_type), dataSize)));
	}

	int indexTermSize = dataSize + VARHDRSZ + sizeof(uint8_t);
	bytea *indexTermVal = (bytea *) palloc(indexTermSize);
	SET_VARSIZE(indexTermVal, indexTermSize);

	uint8_t *buffer = (uint8_t *) VARDATA(indexTermVal);
	buffer[0] = (uint8_t) indexTerm.isIndexTermTruncated;
	PgbsonWriterCopyToBuffer(&writer, &buffer[1], dataSize);
	serializedTerm.indexTermVal = indexTermVal;
	serializedTerm.isIndexTermTruncated = indexTerm.isIndexTermTruncated;
	return serializedTerm;
}


/*
 * Serializes a single bson term (path and value) to the serialized index
 * format. The serialized format is:
 * byte 0: Metadata:
 *  Today this is simply whether or not the term is truncated
 * byte 1:N: A single bson object with { path : value }
 */
BsonIndexTermSerialized
SerializeBsonIndexTerm(pgbsonelement *indexElement, const
					   IndexTermCreateMetadata *indexTermSizeLimit)
{
	Assert(indexTermSizeLimit != NULL);

	bool forceTruncated = false;
	return SerializeBsonIndexTermCore(indexElement, indexTermSizeLimit, forceTruncated);
}


/*
 * This is the root term that all documents get.
 * We pick a term that points to a path that is illegal ('.')
 * and give it to all terms. This allows us to answer questions
 * on complement sets (e.g. NOT operators) from the index.
 */
Datum
GenerateRootTerm(void)
{
	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;
	element.bsonValue.value_type = BSON_TYPE_MINKEY;

	IndexTermCreateMetadata termData =
	{
		.indexTermSizeLimit = 0,
		.pathPrefix = { 0 }
	};
	return PointerGetDatum(SerializeBsonIndexTerm(&element, &termData).indexTermVal);
}


/*
 * This is a marker term that is generated when a given document has a term
 * that is truncated.
 * TODO: This can produce false positives for wildcard indexes... Need to figure this
 * out.
 */
Datum
GenerateRootTruncatedTerm(void)
{
	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;

	IndexTermCreateMetadata termData =
	{
		.indexTermSizeLimit = 0,
		.pathPrefix = { 0 }
	};

	/* Use maxKey just so they're differentiated from the Root term */
	element.bsonValue.value_type = BSON_TYPE_MAXKEY;
	bool forceTruncated = true;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, &termData,
													  forceTruncated).indexTermVal);
}
