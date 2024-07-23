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

#define IndexTermTruncatedFlag 0x1
#define IndexTermMetadataFlag 0x2

extern bool EnableIndexTermTruncationOnNestedObjects;

/* --------------------------------------------------------- */
/* Forward Declaration */
/* --------------------------------------------------------- */
static bool SerializeTermToWriter(pgbson_writer *writer, pgbsonelement *indexElement,
								  const IndexTermCreateMetadata *termMetadata);

static BsonIndexTermSerialized SerializeBsonIndexTermCore(pgbsonelement *indexElement,
														  const IndexTermCreateMetadata *
														  createMetadata,
														  bool forceTruncated,
														  bool isMetadataTerm);
static bool TruncateDocumentTerm(int32_t dataSize, int32_t softLimit, int32_t hardLimit,
								 bson_iter_t *documentIterator,
								 pgbson_writer *documentWriter);

static bool TruncateArrayTerm(int32_t dataSize, int32_t indexTermSoftLimit, int32_t
							  indexTermHardLimit,
							  bson_iter_t *arrayIterator,
							  pgbson_array_writer *arrayWriter);

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
	bytea *left = PG_GETARG_BYTEA_PP(0);
	bytea *right = PG_GETARG_BYTEA_PP(1);

	BsonIndexTerm leftTerm;
	BsonIndexTerm rightTerm;

	InitializeBsonIndexTerm(left, &leftTerm);
	InitializeBsonIndexTerm(right, &rightTerm);
	bool isComparisonValidIgnore = false;
	int32_t compareTerm = CompareBsonIndexTerm(&leftTerm, &rightTerm,
											   &isComparisonValidIgnore);

	PG_FREE_IF_COPY(left, 0);
	PG_FREE_IF_COPY(right, 1);
	return compareTerm;
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
	/* First compare metadata - metadata terms are less than all terms */
	if (leftTerm->isIndexTermMetadata ^ rightTerm->isIndexTermMetadata)
	{
		/* If left is metadata and right is not metadata this will be
		 * 1 - 0 == 1 so return -1 (left < right )
		 */
		return (int32_t) rightTerm->isIndexTermMetadata -
			   (int32_t) leftTerm->isIndexTermMetadata;
	}

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

	return (buffer[0] & IndexTermTruncatedFlag) != 0;
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
	indexTerm->isIndexTermTruncated = (buffer[0] & IndexTermTruncatedFlag) != 0;
	indexTerm->isIndexTermMetadata = (buffer[0] & IndexTermMetadataFlag) != 0;

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
 * Truncate an entry that is present in an object or array.
 * For types that don't support truncation mark the outer entry as definitely
 * not truncated.
 */
static bool
TruncateNestedEntry(pgbson_element_writer *elementWriter, const
					bson_value_t *currentValue,
					bool *forceAsNotTruncated, int32_t sizeBudgetForElementWriter, int32_t
					currentLength)
{
	switch (currentValue->value_type)
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
			PgbsonElementWriterWriteValue(elementWriter, currentValue);
			return false;
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
			bson_value_t numericValue = *currentValue;
			bool checkFixedInteger = true;
			if (IsBsonValue32BitInteger(currentValue, checkFixedInteger))
			{
				numericValue.value.v_int32 = BsonValueAsInt32(currentValue);
				numericValue.value_type = BSON_TYPE_INT32;
			}
			else if (IsBsonValue64BitInteger(currentValue, checkFixedInteger))
			{
				numericValue.value.v_int64 = BsonValueAsInt64(currentValue);
				numericValue.value_type = BSON_TYPE_INT64;
			}
			else
			{
				/*
				 * We convert to double to save some space if applicable
				 * That way we always store it as the smallest possible size
				 */
				numericValue.value.v_decimal128 = GetBsonValueAsDecimal128(currentValue);
				numericValue.value_type = BSON_TYPE_DECIMAL128;

				if (IsDecimal128InDoubleRange(&numericValue))
				{
					numericValue.value.v_double = BsonValueAsDouble(&numericValue);
					numericValue.value_type = BSON_TYPE_DOUBLE;
				}
			}
			PgbsonElementWriterWriteValue(elementWriter, &numericValue);

			/* Numerics are not truncated by themselves */
			return false;
		}

		case BSON_TYPE_DOCUMENT:
		{
			if (!EnableIndexTermTruncationOnNestedObjects)
			{
				*forceAsNotTruncated = true;
				PgbsonElementWriterWriteValue(elementWriter, currentValue);
				return false;
			}

			/* See details about size constant in SerializeTermToWriter() */
			int32_t fixedTermLimit = 17;
			int32_t softLimit = sizeBudgetForElementWriter - fixedTermLimit;
			int32_t dataSize = 5;


			bson_iter_t documentIterator;
			pgbson_writer documentWriter;
			BsonValueInitIterator(currentValue, &documentIterator);
			PgbsonElementWriterStartDocument(elementWriter, &documentWriter);

			bool isTruncated = TruncateDocumentTerm(dataSize, softLimit,
													sizeBudgetForElementWriter,
													&documentIterator,
													&documentWriter);
			PgbsonElementWriterEndDocument(elementWriter, &documentWriter);

			return isTruncated;
		}

		case BSON_TYPE_ARRAY:
		{
			if (!EnableIndexTermTruncationOnNestedObjects)
			{
				*forceAsNotTruncated = true;
				PgbsonElementWriterWriteValue(elementWriter, currentValue);
				return false;
			}

			/* See details about size constant in SerializeTermToWriter() */
			int32_t fixedTermSize = 21;
			int32_t loweredSizeLimit = sizeBudgetForElementWriter - fixedTermSize;
			int32_t dataSize = 5;

			bson_iter_t arrayIterator;
			pgbson_array_writer arrayWriter;
			BsonValueInitIterator(currentValue, &arrayIterator);
			PgbsonElementWriterStartArray(elementWriter, &arrayWriter);
			bool isTruncated = TruncateArrayTerm(dataSize, loweredSizeLimit,
												 sizeBudgetForElementWriter,
												 &arrayIterator, &arrayWriter);
			PgbsonElementWriterEndArray(elementWriter, &arrayWriter);

			return isTruncated;
		}

		case BSON_TYPE_CODEWSCOPE:
		case BSON_TYPE_DBPOINTER:
		case BSON_TYPE_REGEX:
		case BSON_TYPE_CODE:
		{
			/* TODO: Support truncating this?  We fail when obejct or arrays or arrays of arrays/objects goes over 2K limit*/
			*forceAsNotTruncated = true;
			PgbsonElementWriterWriteValue(elementWriter, currentValue);
			return false;
		}

		case BSON_TYPE_BINARY:
		case BSON_TYPE_SYMBOL:
		case BSON_TYPE_UTF8:
		{
			int valueSizeLimit = sizeBudgetForElementWriter - currentLength;
			bson_value_t valueCopy = *currentValue;

			/* The prefix for the string/binary is 1 byte for the type code
			 * given that the path
			 * overhead is tracked externally by the caller.
			 */
			const int32_t stringPrefixLength = 1;
			bool isTruncated = TruncateStringOrBinaryTerm(
				stringPrefixLength,
				valueSizeLimit, currentValue->value_type,
				&valueCopy.value.v_utf8.len);
			PgbsonElementWriterWriteValue(elementWriter, &valueCopy);

			return isTruncated;
		}

		default:
		{
			*forceAsNotTruncated = true;
			PgbsonElementWriterWriteValue(elementWriter, currentValue);
			return false;
		}
	}
}


/*
 * Write and truncate an array into the target array writer accounting
 * for the index term size limit.
 *
 *  dataSize: currentsize of the arrayWriter
 *  indexTermSoftLimit: Before writing any new element we check if it will take us over the soft limit.
 *      If it does, we don't write it and return as truncated. This is to make sure that the index terms
 *      are comparable irrespective of the datatype of the new element. A bool might have space, but a decimal 128
 *      may not. The soft limit preserves the space or large fixed types like Decimal128, so that we can accurately
 *      determine truncation boundary.
 *  indexTermHardLimit: This is absolute limit for the index term. While calling from the Top level object we preserve
 *      2 bytes for writing MaxKey (see caller). For subsequent calls we don't need that 2 bytes.
 *  arrayIterator: Source iterator of the array elements.
 *  arrayWriter: Writer for serializing the Index terms.
 *
 */
static bool
TruncateArrayTerm(int32_t dataSize, int32_t indexTermSoftLimit, int32_t
				  indexTermHardLimit,
				  bson_iter_t *arrayIterator, pgbson_array_writer *arrayWriter)
{
	bool forceAsNotTruncated = false;
	int32_t currentLength = 0;
	bson_type_t lastType = BSON_TYPE_MINKEY;

	pgbson_element_writer elementWriter;
	PgbsonInitArrayElementWriter(arrayWriter, &elementWriter);
	while (bson_iter_next(arrayIterator))
	{
		/* Get the current size */
		currentLength = (int32_t) PgbsonArrayWriterGetSize(arrayWriter);

		/* We stop writing if we are over the soft limit. */
		if (currentLength + dataSize > indexTermSoftLimit)
		{
			/* We've exceeded the limit - mark as truncated */
			if (forceAsNotTruncated)
			{
				ereport(LOG, (errmsg(
								  "Truncation limit reached and LastType %s is requested as ForceAsNotTruncated",
								  BsonTypeName(lastType))));
			}

			return !forceAsNotTruncated;
		}
		else
		{
			/* We're under the size limit reset */
			forceAsNotTruncated = false;
		}

		const bson_value_t *iterValue = bson_iter_value(arrayIterator);

		/* Leave up to 4 bytes from the hard limit for the path\0 */
		int32_t valueLengthLeft = indexTermHardLimit - dataSize - 4;
		lastType = iterValue->value_type;

		/* Values can go until the hard limit. */
		bool truncated = TruncateNestedEntry(&elementWriter, iterValue,
											 &forceAsNotTruncated,
											 valueLengthLeft, currentLength);
		if (truncated)
		{
			return true;
		}
	}

	currentLength = (int32_t) PgbsonArrayWriterGetSize(arrayWriter);
	if (currentLength + dataSize > indexTermSoftLimit)
	{
		/* We've exceeded the limit - mark as truncated */
		return !forceAsNotTruncated;
	}

	/* Not truncated */
	return false;
}


/*
 * Write and truncate a document into the target document writer accounting
 * for the index term size limit.
 */
static bool
TruncateDocumentTerm(int32_t existingTermSize, int32_t softLimit, int32_t hardLimit,
					 bson_iter_t *documentIterator, pgbson_writer *documentWriter)
{
	bool forceAsNotTruncated = false;
	int32_t currentLength = 0;
	bson_type_t lastType = BSON_TYPE_MINKEY;

	pgbson_element_writer elementWriter;
	while (bson_iter_next(documentIterator))
	{
		/* Get the current size */
		currentLength = (int32_t) PgbsonWriterGetSize(documentWriter);
		if (currentLength + existingTermSize > softLimit)
		{
			/* We've exceeded the limit - mark as truncated */
			if (forceAsNotTruncated)
			{
				ereport(LOG, (errmsg(
								  "Truncation limit reached and LastType %s is requested as ForceAsNotTruncated",
								  BsonTypeName(lastType))));
			}

			return !forceAsNotTruncated;
		}
		else
		{
			/* We're under the size limit reset */
			forceAsNotTruncated = false;
		}

		const bson_value_t *iterValue = bson_iter_value(documentIterator);
		const StringView pathView = bson_iter_key_string_view(documentIterator);
		lastType = iterValue->value_type;

		/* Determine how we're going to write this value */
		int32_t requiredLengthWithPath = currentLength + existingTermSize +
										 (int32_t) pathView.length + 1;

		bool truncated;
		if (requiredLengthWithPath < softLimit)
		{
			/* Path at least fits under the soft limit, write the path */
			PgbsonInitObjectElementWriter(documentWriter, &elementWriter, pathView.string,
										  pathView.length);

			/* Since the path is under the limit, the value for this path can go until the hard limit */
			int32_t valueLengthLeft = hardLimit - existingTermSize - pathView.length - 1;
			truncated = TruncateNestedEntry(&elementWriter, iterValue,
											&forceAsNotTruncated,
											valueLengthLeft, currentLength);
		}
		else if (requiredLengthWithPath < hardLimit)
		{
			/* The path fits between the soft limit and hard limit
			 * Write the path only but with MaxKey and mark this path as truncated
			 */
			bson_value_t maxKeyValue = { 0 };
			maxKeyValue.value_type = BSON_TYPE_MAXKEY;
			PgbsonWriterAppendValue(documentWriter, pathView.string, pathView.length,
									&maxKeyValue);
			truncated = true;
		}
		else
		{
			/*
			 * currentLength + dataSize < softLimit but
			 * currentLength + dataSize + path.Length > hardLimit
			 * which implies pathLength > (hardLimit - softLimit)
			 *  The path goes over the hard limit - write as many bytes until
			 * the hard limit and write a MaxKey()
			 */
			int32_t pathExcess = requiredLengthWithPath - hardLimit;
			int32_t pathViewlength = pathView.length - pathExcess;
			bson_value_t maxKeyValue = { 0 };
			maxKeyValue.value_type = BSON_TYPE_MAXKEY;
			PgbsonWriterAppendValue(documentWriter, pathView.string, pathViewlength,
									&maxKeyValue);
			truncated = true;
		}

		if (truncated)
		{
			return true;
		}
	}

	currentLength = (int32_t) PgbsonWriterGetSize(documentWriter);
	if (currentLength + existingTermSize > softLimit)
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

	char *newPath = NULL;

	if (termMetadata->pathPrefix.length > 0 && indexPath.length > 0 &&
		!termMetadata->isWildcard)
	{
		if (!StringViewEquals(&indexPath, &termMetadata->pathPrefix))
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg(
								"Wildcard Prefix path encountered with non-wildcard index - path %s, prefix %s",
								indexPath.string, termMetadata->pathPrefix.string),
							errhint(
								"Wildcard Prefix path encountered with non-wildcard index")));
		}

		/* Index term should occupy a minimal amount of space */
		indexPath.length = 1;
		indexPath.string = "$";
	}
	else if (termMetadata->indexTermSizeLimit > 0 && termMetadata->isWildcard)
	{
		/* If it is a single path wildcard index with a non root projection we can trim down the key by removing the prefix. */
		if (!termMetadata->isWildcardProjection && termMetadata->pathPrefix.length > 0 &&
			indexPath.length > 0)
		{
			int32_t newIndexPathLength = indexPath.length -
										 termMetadata->pathPrefix.length;

			/* We only proceed with the optimization if the new index path length >= 0, otherwise we lose the root term */
			if (newIndexPathLength == 0)
			{
				indexPath.string = "$";
				indexPath.length = 1;
			}
			if (newIndexPathLength > 0)
			{
				newPath = palloc0(sizeof(char) * (newIndexPathLength + 2));

				/* Build the $.suffix string i.e if the pathPrefix is a.b.c.d.e.$** and the index element path is a.b.c.d.e.f.g we can transform to $.f.g */
				newPath[0] = '$';
				memcpy(&newPath[1], indexPath.string + termMetadata->pathPrefix.length,
					   newIndexPathLength);

				indexPath.string = newPath;
				indexPath.length = newIndexPathLength + 1; /* account for the added $ at the beginning. */
			}
		}

		if (indexPath.length > termMetadata->wildcardIndexTruncatedPathLimit)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"Wildcard index key exceeded the maximum allowed size of %d.",
								termMetadata->wildcardIndexTruncatedPathLimit)));
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

	bool truncated = false;

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
			break;
		}

		case BSON_TYPE_ARRAY:
		{
			/* The fixed-size term sizes are
			 * 1 byte: bool, maxkey, minkey, null, undefined
			 * 4 bytes: int32,
			 * 8 bytes:datetime, double, int64, timestamp
			 * 12 bytes: oid
			 * 16 bytes: decimal128
			 * For an array term, the value for each array element is
			 *  : <index-as-path> <typeCode> <value>
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
			BsonValueInitIterator(&indexElement->bsonValue, &arrayIterator);
			PgbsonWriterStartArray(writer, indexPath.string, indexPath.length,
								   &arrayWriter);
			truncated = TruncateArrayTerm(dataSize, loweredSizeLimit,
										  termMetadata->indexTermSizeLimit,
										  &arrayIterator, &arrayWriter);
			PgbsonWriterEndArray(writer, &arrayWriter);

			break;
		}

		case BSON_TYPE_DOCUMENT:
		{
			/* For documents, the fixed term sizes are as above.
			 * Given that paths are variable, unlike arrays, the rules for serialization
			 * become different. Instead of the logic above, we assume that the soft-limit
			 * is MaxTermSize - 17. When we hit the soft-limit:
			 * - If we've already serialized a path, we serialize the value with truncation
			 * and bail (and mark as truncated)
			 * - If we haven't serialized a path, we serialize until HardLimit - 2, Write
			 * MaxKey() and mark as truncated.
			 */
			int32_t fixedTermLimit = 17;
			int32_t softLimit = termMetadata->indexTermSizeLimit - fixedTermLimit;
			int32_t hardLimit = termMetadata->indexTermSizeLimit - 2;

			bson_iter_t documentIterator;
			pgbson_writer documentWriter;
			BsonValueInitIterator(&indexElement->bsonValue, &documentIterator);
			PgbsonWriterStartDocument(writer, indexPath.string, indexPath.length,
									  &documentWriter);

			truncated = TruncateDocumentTerm(dataSize, softLimit, hardLimit,
											 &documentIterator,
											 &documentWriter);
			PgbsonWriterEndDocument(writer, &documentWriter);

			break;
		}

		/* TODO: These types need to be truncated */
		case BSON_TYPE_CODEWSCOPE:
		case BSON_TYPE_DBPOINTER:
		case BSON_TYPE_REGEX:
		{
			/* Regex truncation is in the history but removed for now;
			 * TODO: Add this back.
			 */
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			break;
		}

		case BSON_TYPE_BINARY:
		{
			truncated = TruncateStringOrBinaryTerm(dataSize,
												   termMetadata->indexTermSizeLimit,
												   BSON_TYPE_BINARY,
												   &indexElement->bsonValue.value.
												   v_binary.data_len);
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			break;
		}

		case BSON_TYPE_CODE:
		{
			truncated = TruncateStringOrBinaryTerm(dataSize,
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
			break;
		}

		case BSON_TYPE_SYMBOL:
		{
			truncated = TruncateStringOrBinaryTerm(dataSize,
												   termMetadata->indexTermSizeLimit,
												   BSON_TYPE_SYMBOL,
												   &indexElement->bsonValue.value.
												   v_symbol.len);
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			truncated = TruncateStringOrBinaryTerm(dataSize,
												   termMetadata->indexTermSizeLimit,
												   BSON_TYPE_UTF8,
												   &indexElement->bsonValue.value.
												   v_utf8.len);
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			break;
		}

		default:
		{
			PgbsonWriterAppendValue(writer, indexPath.string, indexPath.length,
									&indexElement->bsonValue);
			break;
		}
	}

	if (newPath != NULL)
	{
		pfree(newPath);
	}

	return truncated;
}


/*
 * Internal Core method that serializes the term and returns the
 * serialized term. ALlows for the term to be force marked truncated
 * This is useful for the "root" truncated term.
 */
static BsonIndexTermSerialized
SerializeBsonIndexTermCore(pgbsonelement *indexElement,
						   const IndexTermCreateMetadata *createMetadata,
						   bool forceTruncated,
						   bool isMetadataTerm)
{
	BsonIndexTerm indexTerm = {
		false, false, { 0 }
	};
	BsonIndexTermSerialized serializedTerm = {
		false, false, NULL
	};

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	indexTerm.isIndexTermTruncated = SerializeTermToWriter(&writer, indexElement,
														   createMetadata);
	if (forceTruncated)
	{
		indexTerm.isIndexTermTruncated = true;
	}
	if (isMetadataTerm)
	{
		indexTerm.isIndexTermMetadata = true;
	}

	uint32_t dataSize = PgbsonWriterGetSize(&writer);

	if (createMetadata->indexTermSizeLimit > 0 &&
		dataSize > (uint32_t) createMetadata->indexTermSizeLimit)
	{
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg(
							"Truncation size limit specified %d, but index term with type %s was larger %d - isTruncated %d",
							createMetadata->indexTermSizeLimit, BsonTypeName(
								indexElement->bsonValue.value_type), dataSize,
							indexTerm.isIndexTermTruncated),
						errhint(
							"Truncation size limit specified %d, but index term with type %s was larger %d - isTruncated %d",
							createMetadata->indexTermSizeLimit, BsonTypeName(
								indexElement->bsonValue.value_type), dataSize,
							indexTerm.isIndexTermTruncated)));
	}

	int indexTermSize = dataSize + VARHDRSZ + sizeof(uint8_t);
	bytea *indexTermVal = (bytea *) palloc(indexTermSize);
	SET_VARSIZE(indexTermVal, indexTermSize);

	uint8_t *buffer = (uint8_t *) VARDATA(indexTermVal);
	buffer[0] = 0;
	if (indexTerm.isIndexTermTruncated)
	{
		buffer[0] = buffer[0] | IndexTermTruncatedFlag;
	}

	if (indexTerm.isIndexTermMetadata)
	{
		buffer[0] = buffer[0] | IndexTermMetadataFlag;
	}

	PgbsonWriterCopyToBuffer(&writer, &buffer[1], dataSize);
	serializedTerm.indexTermVal = indexTermVal;
	serializedTerm.isIndexTermTruncated = indexTerm.isIndexTermTruncated;
	serializedTerm.isRootMetadataTerm = indexTerm.isIndexTermMetadata;
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
	bool isMetadataTerm = false;
	return SerializeBsonIndexTermCore(indexElement, indexTermSizeLimit, forceTruncated,
									  isMetadataTerm);
}


/*
 * This is the root term that all documents get when the indexed path exists.
 * We pick a term that points to a path that is illegal ('')
 * and give it to all terms. This allows us to answer questions
 * on complement sets (e.g. NOT operators) from the index.
 */
Datum
GenerateRootTerm(const IndexTermCreateMetadata *termData)
{
	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;
	element.bsonValue.value_type = BSON_TYPE_MINKEY;


	bool forceTruncated = false;

	/* Can't mark this as metadata due to back-compat. This is okay coz this is legacy. */
	bool isMetadataTerm = false;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData, forceTruncated,
													  isMetadataTerm).indexTermVal);
}


/*
 * This is the root term that all documents get when the indexed path exists.
 * We pick a term that points to a path that is illegal ('')
 * and give it to all terms. This allows us to answer questions
 * on complement sets (e.g. NOT operators) from the index.
 */
Datum
GenerateRootExistsTerm(const IndexTermCreateMetadata *termData)
{
	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;
	element.bsonValue.value_type = BSON_TYPE_BOOL;
	element.bsonValue.value.v_bool = true;
	bool forceTruncated = false;
	bool isMetadataTerm = true;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData, forceTruncated,
													  isMetadataTerm).indexTermVal);
}


/*
 * This is the root term that all documents get when the indexed path doesn't exist.
 * We pick a term that points to a path that is illegal ('').
 * This allows us to answer questions for exists and null equality in the index
 */
Datum
GenerateRootNonExistsTerm(const IndexTermCreateMetadata *termData)
{
	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;
	element.bsonValue.value_type = BSON_TYPE_UNDEFINED;
	bool forceTruncated = false;
	bool isMetadataTerm = true;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData, forceTruncated,
													  isMetadataTerm).indexTermVal);
}


/*
 * This is the root term that all documents get when the indexed path contains an array
 * or array ancestor.
 * We pick a term that points to a path that is illegal (''). In addition we add the
 * Metadata "isMetadata" to distinguish this from regular data paths.
 * This allows us to answer questions for null equality in the index.
 */
Datum
GenerateRootMultiKeyTerm(const IndexTermCreateMetadata *termData)
{
	bson_iter_t iter;
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendEmptyArray(&writer, "", 0);

	PgbsonWriterGetIterator(&writer, &iter);

	pgbsonelement element = { 0 };
	BsonIterToSinglePgbsonElement(&iter, &element);
	bool forceTruncated = false;
	bool isMetadataTerm = true;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData, forceTruncated,
													  isMetadataTerm).indexTermVal);
}


/*
 * This is a marker term that is generated when a given document has a term
 * that is truncated.
 * TODO: This can produce false positives for wildcard indexes... Need to figure this
 * out.
 */
Datum
GenerateRootTruncatedTerm(const IndexTermCreateMetadata *termData)
{
	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;

	/* Use maxKey just so they're differentiated from the Root term */
	element.bsonValue.value_type = BSON_TYPE_MAXKEY;
	bool forceTruncated = true;

	/* Can't mark this as metadata due to back-compat. Need to think of how to
	 * handle this. TODO: When we bump index versions make this a metadata term.
	 * Note that this isn't bad since we can never get a MAXKEY that is truncated.
	 */
	bool isMetadataTerm = termData->indexVersion >= IndexOptionsVersion_V1;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  forceTruncated,
													  isMetadataTerm).indexTermVal);
}
