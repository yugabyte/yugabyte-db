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
#include <access/toast_compression.h>
#include <access/toast_internals.h>
#include "opclass/bson_gin_index_term.h"
#include "query/bson_compare.h"
#include "utils/documentdb_errors.h"
#include "types/decimal128.h"
#include "io/bsonvalue_utils.h"


/*
 * While this looks like a flags enum, it started out that way
 * but it's not. Intermediate values are allowed. However, at
 * the point of conversion, the binaries using the first 2 bits were
 * not upgraded, so the code uses 0x04 and 0x08 until the next release.
 * Note that subsequent metadata values can use intermediate values.
 * IndexTermPartialUndefinedValue uses 2 flags that were not used earlier
 * and so is safe to use as a new value.
 * The descending metadata values are also treated as flags style. This is
 * primarily from a perf standpoint to say descending terms are those that
 * are `>= 0x80` but there's not a need for 1:1 mapping. We do retain the 1:1
 * mapping so that back-compat flag checks work.
 */
typedef enum IndexTermMetadata
{
	IndexTermNoMetadata = 0x00,

	IndexTermTruncated = 0x01,

	IndexTermIsMetadata = 0x02,

	IndexTermComposite = 0x04,

	IndexTermValueOnly = 0x05,

	IndexTermValueOnlyTruncated = 0x06,

	IndexTermUndefinedValue = 0x08,

	IndexTermPartialUndefinedValue = 0x0C,

	IndexTermDescending = 0x80,

	IndexTermDescendingTruncated = 0x81,

	IndexTermValueOnlyDescending = 0x85,

	IndexTermValueOnlyDescendingTruncated = 0x86,

	IndexTermDescendingUndefinedValue = 0x88,

	IndexTermDescendingPartialUndefinedValue = 0x8C,
} IndexTermMetadata;

extern int IndexTermCompressionThreshold;

/* --------------------------------------------------------- */
/* Forward Declaration */
/* --------------------------------------------------------- */
static bool SerializeTermToWriter(pgbson_writer *writer, pgbsonelement *indexElement,
								  const IndexTermCreateMetadata *termMetadata,
								  bool allowValueOnly, bool *isValueOnly);

static BsonIndexTermSerialized SerializeBsonIndexTermCore(pgbsonelement *indexElement,
														  const IndexTermCreateMetadata *
														  createMetadata,
														  IndexTermMetadata termMetadata);
static bool TruncateDocumentTerm(int32_t dataSize, int32_t softLimit, int32_t hardLimit,
								 bson_iter_t *documentIterator,
								 pgbson_writer *documentWriter);

static bool TruncateArrayTerm(int32_t dataSize, int32_t indexTermSoftLimit, int32_t
							  indexTermHardLimit,
							  bson_iter_t *arrayIterator,
							  pgbson_array_writer *arrayWriter);
static bytea * BuildSerializedIndexTerm(pgbsonelement *indexElement, const
										IndexTermCreateMetadata *createMetadata,
										IndexTermMetadata termMetadata,
										BsonIndexTerm *indexTerm);
static int32_t CompareCompositeIndexTerms(const uint8_t *leftBuffer,
										  uint32_t leftIndexTermSize,
										  const uint8_t *rightBuffer,
										  uint32_t rightIndexTermSize);
static void InitializeBsonIndexTermFromBuffer(const uint8_t *buffer,
											  uint32_t indexTermSize,
											  BsonIndexTerm *indexTerm);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(gin_bson_compare);
PG_FUNCTION_INFO_V1(gin_bson_index_term_to_bson);


inline static bool
IsIndexTermMetadataTruncated(IndexTermMetadata termMetadata)
{
	return termMetadata == IndexTermTruncated ||
		   termMetadata == IndexTermDescendingTruncated ||
		   termMetadata == IndexTermValueOnlyTruncated ||
		   termMetadata == IndexTermValueOnlyDescendingTruncated;
}


inline static bool
IsIndexTermMetadataValueOnly(IndexTermMetadata termMetadata)
{
	return termMetadata == IndexTermValueOnly ||
		   termMetadata == IndexTermValueOnlyTruncated ||
		   termMetadata == IndexTermValueOnlyDescending ||
		   termMetadata == IndexTermValueOnlyDescendingTruncated;
}


inline static bool
IsIndexTermMetadataComposite(IndexTermMetadata metadata)
{
	return metadata == IndexTermComposite;
}


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

	const uint8_t *leftBuffer = (const uint8_t *) VARDATA_ANY(left);
	uint32_t leftSize = VARSIZE_ANY_EXHDR(left);

	const uint8_t *rightBuffer = (const uint8_t *) VARDATA_ANY(right);
	uint32_t rightSize = VARSIZE_ANY_EXHDR(right);

	bool isLeftComposite = IsIndexTermMetadataComposite(leftBuffer[0]);
	bool isRightComposite = IsIndexTermMetadataComposite(rightBuffer[0]);
	int32_t compareTerm;
	if (isLeftComposite || isRightComposite)
	{
		/* Both are composite terms: Compare as composite */
		compareTerm = CompareCompositeIndexTerms(leftBuffer, leftSize, rightBuffer,
												 rightSize);
	}
	else
	{
		BsonIndexTerm leftTerm;
		BsonIndexTerm rightTerm;
		InitializeBsonIndexTermFromBuffer(leftBuffer, leftSize, &leftTerm);
		InitializeBsonIndexTermFromBuffer(rightBuffer, rightSize, &rightTerm);
		bool isComparisonValidIgnore = false;
		compareTerm = CompareBsonIndexTerm(&leftTerm, &rightTerm,
										   &isComparisonValidIgnore);
	}

	PG_FREE_IF_COPY(left, 0);
	PG_FREE_IF_COPY(right, 1);
	PG_RETURN_INT32(compareTerm);
}


/*
 * Debug function to print the bytea into a bson format.
 */
Datum
gin_bson_index_term_to_bson(PG_FUNCTION_ARGS)
{
	bytea *indexTerm = PG_GETARG_BYTEA_PP(0);
	const uint8_t *termBuffer = (const uint8_t *) VARDATA_ANY(indexTerm);
	uint32_t termSize = VARSIZE_ANY_EXHDR(indexTerm);

	bool isComposite = IsIndexTermMetadataComposite(termBuffer[0]);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	if (isComposite)
	{
		BsonIndexTerm terms[INDEX_MAX_KEYS] = { 0 };
		int numTerms = InitializeCompositeIndexTerm(indexTerm, terms);

		pgbson_array_writer arrayWriter;
		PgbsonWriterStartArray(&writer, "$$COMP", 6, &arrayWriter);
		for (int i = 0; i < numTerms; i++)
		{
			pgbson_writer childWriter;
			PgbsonArrayWriterStartDocument(&arrayWriter, &childWriter);
			PgbsonWriterAppendValue(&childWriter, terms[i].element.path,
									terms[i].element.pathLength,
									&terms[i].element.bsonValue);
			PgbsonWriterAppendInt32(&childWriter, "$flags", 6, terms[i].termMetadata);
			PgbsonArrayWriterEndDocument(&arrayWriter, &childWriter);
		}

		PgbsonWriterEndArray(&writer, &arrayWriter);
	}
	else
	{
		BsonIndexTerm term;
		InitializeBsonIndexTermFromBuffer(termBuffer, termSize, &term);
		PgbsonWriterAppendValue(&writer, term.element.path, term.element.pathLength,
								&term.element.bsonValue);
		PgbsonWriterAppendInt32(&writer, "$flags", 6, term.termMetadata);
	}

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


static int32_t
CompareCompositeIndexTerms(const uint8_t *leftBuffer, uint32_t leftIndexTermSize,
						   const uint8_t *rightBuffer, uint32_t rightIndexTermSize)
{
	Assert(leftIndexTermSize > (sizeof(uint8_t) + 2));
	Assert(rightIndexTermSize > (sizeof(uint8_t) + 2));

	if (!IsIndexTermMetadataComposite(leftBuffer[0]) ||
		!IsIndexTermMetadataComposite(rightBuffer[0]))
	{
		/* One of them is composite - composite is greater than non-composite */
		return IsIndexTermMetadataComposite(leftBuffer[0]) ? 1 : -1;
	}

	/* Skip the first byte - gets the first terms metadata */
	leftBuffer++;
	rightBuffer++;
	leftIndexTermSize--;
	rightIndexTermSize--;

	while (leftIndexTermSize > 0 && rightIndexTermSize > 0)
	{
		bytea *leftBytes = (bytea *) leftBuffer;
		bytea *rightBytes = (bytea *) rightBuffer;
		uint32_t leftSize = VARSIZE_ANY(leftBytes);
		uint32_t rightSize = VARSIZE_ANY(rightBytes);

		BsonIndexTerm leftTerm;
		BsonIndexTerm rightTerm;
		InitializeBsonIndexTerm(leftBytes, &leftTerm);
		InitializeBsonIndexTerm(rightBytes, &rightTerm);
		bool isComparisonValidIgnore = false;
		int32_t compareTerm = CompareBsonIndexTerm(&leftTerm, &rightTerm,
												   &isComparisonValidIgnore);
		if (compareTerm != 0)
		{
			return compareTerm;
		}

		/* Proceed to the subsequent term */
		leftBuffer += leftSize;
		rightBuffer += rightSize;
		leftIndexTermSize -= leftSize;
		rightIndexTermSize -= rightSize;
	}

	return leftIndexTermSize > rightIndexTermSize ? 1 :
		   leftIndexTermSize < rightIndexTermSize ? -1 : 0;
}


inline static int32_t
CompareIndexTermPathAndValue(const BsonIndexTerm *leftTerm, const
							 BsonIndexTerm *rightTerm,
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
		return cmp;
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

	/*
	 * Everything is equal by value - if one of them is undefined, compare at this point
	 */
	if (IsIndexTermValueUndefined(leftTerm) ^ IsIndexTermValueUndefined(rightTerm))
	{
		/* If one of them is undefined, it is less than the other */
		return (int32_t) IsIndexTermValueUndefined(rightTerm) -
			   (int32_t) IsIndexTermValueUndefined(leftTerm);
	}

	if (IsIndexTermMaybeUndefined(leftTerm) ^ IsIndexTermMaybeUndefined(rightTerm))
	{
		/* If one of them is maybe undefined, it is less than the other */
		return (int32_t) IsIndexTermMaybeUndefined(rightTerm) -
			   (int32_t) IsIndexTermMaybeUndefined(leftTerm);
	}

	/* Both terms are equal by value - compare for truncation */
	if (IsIndexTermTruncated(leftTerm) ^ IsIndexTermTruncated(rightTerm))
	{
		return (int32_t) IsIndexTermTruncated(leftTerm) -
			   (int32_t) IsIndexTermTruncated(rightTerm);
	}

	return 0;
}


/*
 * Implements the core logic for comparing index terms.
 * Index terms are compared first by path, then value.
 * If the values are equal, a truncated value is considered greater than
 * a non-truncated value.
 */
int32_t
CompareBsonIndexTerm(const BsonIndexTerm *leftTerm, const BsonIndexTerm *rightTerm,
					 bool *isComparisonValid)
{
	/* First compare metadata - metadata terms are less than all terms */
	if (IsIndexTermMetadata(leftTerm) ^ IsIndexTermMetadata(rightTerm))
	{
		/* If left is metadata and right is not metadata this will be
		 * 1 - 0 == 1 so return -1 (left < right )
		 */
		return (int32_t) IsIndexTermMetadata(rightTerm) -
			   (int32_t) IsIndexTermMetadata(leftTerm);
	}

	/* If it's not a metadata term, then ensure that we don't compare asc/desc mixed */
	bool isLeftDescending = IsIndexTermValueDescending(leftTerm);
	if (isLeftDescending ^ IsIndexTermValueDescending(rightTerm))
	{
		/* Special case here - the root truncated term is not metadata */
		if (IsRootTruncationTerm(leftTerm) || IsRootTruncationTerm(rightTerm))
		{
			/* Treat similar to metadata terms */
			return (int32_t) IsRootTruncationTerm(rightTerm) -
				   (int32_t) IsRootTruncationTerm(leftTerm);
		}

		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Cannot compare ascending and descending index terms: left %d, right %d",
							leftTerm->termMetadata, rightTerm->termMetadata)));
	}

	int32_t compare = CompareIndexTermPathAndValue(leftTerm, rightTerm,
												   isComparisonValid);

	return isLeftDescending ? -compare : compare;
}


bool
IsSerializedIndexTermComposite(bytea *indexTermSerialized)
{
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);
	uint32_t indexTermSize PG_USED_FOR_ASSERTS_ONLY = VARSIZE_ANY_EXHDR(
		indexTermSerialized);

	/* size must be bigger than metadata + bson overhead */
	Assert(indexTermSize >= (sizeof(uint8_t) + 2));

	return IsIndexTermMetadataComposite(buffer[0]);
}


/*
 * Indicates true when the serialized index term has been truncated.
 */
bool
IsSerializedIndexTermTruncated(bytea *indexTermSerialized)
{
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);
	uint32_t indexTermSize PG_USED_FOR_ASSERTS_ONLY = VARSIZE_ANY_EXHDR(
		indexTermSerialized);

	/* size must be bigger than metadata + bson overhead */
	Assert(indexTermSize > (sizeof(uint8_t)));

	return IsIndexTermMetadataTruncated(buffer[0]);
}


bool
IsSerializedIndexTermMetadata(bytea *indexTermSerialized)
{
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);
	uint32_t indexTermSize PG_USED_FOR_ASSERTS_ONLY = VARSIZE_ANY_EXHDR(
		indexTermSerialized);

	/* size must be bigger than metadata + bson overhead */
	Assert(indexTermSize > (sizeof(uint8_t) + 2));

	return (IndexTermMetadata) buffer[0] == IndexTermIsMetadata;
}


bool
IsIndexTermTruncated(const BsonIndexTerm *indexTerm)
{
	return IsIndexTermMetadataTruncated(indexTerm->termMetadata);
}


bool
IsIndexTermMaybeUndefined(const BsonIndexTerm *indexTerm)
{
	return indexTerm->termMetadata == IndexTermPartialUndefinedValue ||
		   indexTerm->termMetadata == IndexTermDescendingPartialUndefinedValue;
}


bool
IsIndexTermValueUndefined(const BsonIndexTerm *indexTerm)
{
	return indexTerm->termMetadata == IndexTermUndefinedValue ||
		   indexTerm->termMetadata == IndexTermDescendingUndefinedValue;
}


bool
IsIndexTermValueDescending(const BsonIndexTerm *indexTerm)
{
	return indexTerm->termMetadata >= IndexTermDescending;
}


bool
IsIndexTermMetadata(const BsonIndexTerm *indexTerm)
{
	return indexTerm->termMetadata == IndexTermIsMetadata;
}


static void
InitializeBsonIndexTermFromBuffer(const uint8_t *buffer, uint32_t indexTermSize,
								  BsonIndexTerm *indexTerm)
{
	/* size must be bigger than metadata + bson overhead */
	Assert((indexTermSize >= (sizeof(uint8_t) + 2)));

	/* First we have the metadata */
	indexTerm->termMetadata = buffer[0];
	switch ((IndexTermMetadata) buffer[0])
	{
		case IndexTermIsMetadata:
		{
			/* Next is the bson data serialized */
			BsonDocumentBytesToPgbsonElementUnsafe(
				(const uint8_t *) &buffer[1], indexTermSize - 1, &indexTerm->element);
			return;
		}

		case IndexTermValueOnly:
		case IndexTermValueOnlyTruncated:
		case IndexTermValueOnlyDescending:
		case IndexTermValueOnlyDescendingTruncated:
		{
			bool skipLengthOffset = true;
			BsonDocumentBytesToPgbsonElementWithOptionsUnsafe(
				(const uint8_t *) &buffer[1], indexTermSize - 1, &indexTerm->element,
				skipLengthOffset);
			indexTerm->element.path = "$";
			indexTerm->element.pathLength = 1;
			return;
		}

		default:
		{
			/* Next is the bson data serialized */
			BsonDocumentBytesToPgbsonElementUnsafe(
				(const uint8_t *) &buffer[1], indexTermSize - 1, &indexTerm->element);
			return;
		}
	}
}


/*
 * Initializes the BsonIndexTerm from the serialized index format.
 */
void
InitializeBsonIndexTerm(bytea *indexTermSerialized, BsonIndexTerm *indexTerm)
{
	uint32_t indexTermSize = VARSIZE_ANY_EXHDR(indexTermSerialized);
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);
	InitializeBsonIndexTermFromBuffer(buffer, indexTermSize, indexTerm);
}


int32_t
InitializeSerializedCompositeIndexTerm(bytea *indexTermSerialized,
									   bytea *termValues[INDEX_MAX_KEYS])
{
	if (!IsSerializedIndexTermComposite(indexTermSerialized))
	{
		termValues[0] = indexTermSerialized;
		return 1;
	}

	uint32_t termSize = VARSIZE_ANY_EXHDR(indexTermSerialized);
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);

	/* Skip the first byte - gets the first terms metadata */
	buffer++;
	termSize--;

	int index = 0;
	while (termSize > 0)
	{
		if (index >= INDEX_MAX_KEYS)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg("Index term exceeds maximum number of keys %d",
								   INDEX_MAX_KEYS)));
		}

		bytea *bytes = (bytea *) buffer;
		termValues[index] = bytes;
		uint32_t leftSize = VARSIZE_ANY(bytes);

		/* Proceed to the subsequent term */
		index++;
		buffer += leftSize;
		termSize -= leftSize;
	}

	return index;
}


int32_t
InitializeCompositeIndexTerm(bytea *indexTermSerialized, BsonIndexTerm
							 indexTerm[INDEX_MAX_KEYS])
{
	if (!IsSerializedIndexTermComposite(indexTermSerialized))
	{
		InitializeBsonIndexTerm(indexTermSerialized, &indexTerm[0]);
		return 1;
	}

	uint32_t termSize = VARSIZE_ANY_EXHDR(indexTermSerialized);
	const uint8_t *buffer = (const uint8_t *) VARDATA_ANY(indexTermSerialized);

	Assert(termSize > (sizeof(uint8_t) + 2));

	if (buffer[0] != IndexTermComposite)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Cannot compare non-composite index terms as composite")));
	}

	/* Skip the first byte - gets the first terms metadata */
	buffer++;
	termSize--;

	int index = 0;
	while (termSize > 0)
	{
		if (index >= INDEX_MAX_KEYS)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg("Index term exceeds maximum number of keys %d",
								   INDEX_MAX_KEYS)));
		}

		bytea *bytes = (bytea *) buffer;
		uint32_t leftSize = VARSIZE_ANY(bytes);

		InitializeBsonIndexTerm(bytes, &indexTerm[index]);

		/* Proceed to the subsequent term */
		index++;
		buffer += leftSize;
		termSize -= leftSize;
	}

	return index;
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Cannot create index key required length %d for type %s exceeds max size %d.",
								stringLen, BsonTypeName(bsonValueType),
								indexTermSizeLimit),
							errdetail_log(
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
			/* See details about size constant in SerializeTermToWriter() */
			int32_t fixedTermLimit = 17;
			int32_t softLimit = sizeBudgetForElementWriter - fixedTermLimit;
			int32_t dataSize = currentLength;


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
			/* See details about size constant in SerializeTermToWriter() */
			int32_t fixedTermSize = 21;
			int32_t loweredSizeLimit = sizeBudgetForElementWriter - fixedTermSize;
			int32_t dataSize = currentLength;

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
		{
			/* TODO: Support truncating this?  We fail when objects or arrays or arrays of arrays/objects goes over 2K limit*/
			*forceAsNotTruncated = true;
			PgbsonElementWriterWriteValue(elementWriter, currentValue);
			return false;
		}

		case BSON_TYPE_CODE:
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

			/* Code ignores code_len when writing. We need to manually truncate */
			if (isTruncated && currentValue->value_type == BSON_TYPE_CODE)
			{
				valueCopy.value.v_code.code = pnstrdup(
					valueCopy.value.v_code.code,
					valueCopy.value.v_code.code_len);
			}

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
		/* Get the current size  */
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
		/* Get the current size  */
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
										 (int32_t) pathView.length + 2;

		bool truncated;
		if (requiredLengthWithPath < softLimit)
		{
			/* Path at least fits under the soft limit, write the path */
			PgbsonInitObjectElementWriter(documentWriter, &elementWriter, pathView.string,
										  pathView.length);

			/* Since the path is under the limit, the value for this path can go until the hard limit */
			int32_t valueLengthLeft = hardLimit - existingTermSize - pathView.length - 2;
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
					  const IndexTermCreateMetadata *termMetadata,
					  bool allowValueOnly, bool *isValueOnly)
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Wildcard Prefix path encountered with non-wildcard index - path %s, prefix %s",
								indexPath.string, termMetadata->pathPrefix.string)));
		}

		/* Index term should occupy a minimal amount of space */
		if (termMetadata->allowValueOnly && allowValueOnly)
		{
			indexPath.length = 0;
			indexPath.string = "";
			*isValueOnly = true;
		}
		else
		{
			indexPath.length = 1;
			indexPath.string = "$";
		}
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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

	/* BSON document metadata (int32 + unsigned_byte(0)) + indexPath.length + typecode + unsigned_byte(0) */
	int32_t dataSize = 5 + indexPath.length + 2;

	if (dataSize >= termMetadata->indexTermSizeLimit)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Cannot create index key because the path length %d exceeds truncation limit %d.",
							dataSize, termMetadata->indexTermSizeLimit),
						errdetail_log(
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
						   IndexTermMetadata termMetadata)
{
	BsonIndexTermSerialized serializedTerm = { 0 };
	BsonIndexTerm indexTerm = { 0 };

	bytea *indexTermVal = BuildSerializedIndexTerm(indexElement, createMetadata,
												   termMetadata, &indexTerm);

	serializedTerm.indexTermVal = indexTermVal;
	serializedTerm.isIndexTermTruncated = IsIndexTermTruncated(&indexTerm);
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

	IndexTermMetadata termMetadata = IndexTermNoMetadata;
	return SerializeBsonIndexTermCore(indexElement, indexTermSizeLimit, termMetadata);
}


static Datum
CompressTermIfNeeded(bytea *inputTerm)
{
	if (VARSIZE(inputTerm) > (Size) IndexTermCompressionThreshold)
	{
		Datum result = toast_compress_datum(PointerGetDatum(inputTerm),
											default_toast_compression);
		if (result != (Datum) NULL)
		{
			pfree(inputTerm);
			return result;
		}
	}

	return PointerGetDatum(inputTerm);
}


BsonCompressableIndexTermSerialized
SerializeBsonIndexTermWithCompression(pgbsonelement *indexElement,
									  const IndexTermCreateMetadata *createMetadata)
{
	Assert(createMetadata != NULL);

	IndexTermMetadata termMetadata = IndexTermNoMetadata;
	BsonIndexTerm indexTerm = { 0 };
	BsonCompressableIndexTermSerialized serializedTerm = { 0 };

	bytea *indexTermVal = BuildSerializedIndexTerm(indexElement, createMetadata,
												   termMetadata, &indexTerm);
	serializedTerm.indexTermDatum = CompressTermIfNeeded(indexTermVal);

	serializedTerm.isIndexTermTruncated = IsIndexTermTruncated(&indexTerm);
	return serializedTerm;
}


BsonIndexTermSerialized
SerializeCompositeBsonIndexTerm(bytea **individualTerms, int32_t numTerms)
{
	BsonIndexTermSerialized serializedTerm = { 0 };

	if (numTerms == 1)
	{
		/* Special case, it's just 1 term - treat it as a non-composite term */
		serializedTerm.indexTermVal = individualTerms[0];
		serializedTerm.isIndexTermTruncated = false;
		return serializedTerm;
	}

	/* First byte is the metadata byte */
	uint32_t totalSize = VARHDRSZ + 1;

	for (int i = 0; i < numTerms; i++)
	{
		/* Take the content size of each (including varhdr size)
		 * Similar to heap_compute_data_size, see if we can leverage VARSIZE_SHORT
		 * for the VARHDRSIZE
		 */
		if (VARATT_CAN_MAKE_SHORT(individualTerms[i]))
		{
			totalSize += VARATT_CONVERTED_SHORT_SIZE(individualTerms[i]);
		}
		else
		{
			totalSize += VARSIZE(individualTerms[i]);
		}
	}

	/* now the composite term will be a bytea with the concat of the values */
	bytea *compositeTerm = palloc(totalSize);
	SET_VARSIZE(compositeTerm, totalSize);
	char *dataBuffer = VARDATA(compositeTerm);

	dataBuffer[0] = IndexTermComposite;
	dataBuffer++;
	for (int i = 0; i < numTerms; i++)
	{
		/* Take the content size of each (excluding varhdr size) */
		uint32_t dataSize = VARSIZE(individualTerms[i]);
		if (VARATT_CAN_MAKE_SHORT(individualTerms[i]))
		{
			uint8_t data_length = VARATT_CONVERTED_SHORT_SIZE(individualTerms[i]);
			SET_VARSIZE_SHORT(dataBuffer, data_length);
			memcpy(dataBuffer + 1, VARDATA(individualTerms[i]), data_length - 1);
			dataBuffer += data_length;
		}
		else
		{
			memcpy(dataBuffer, individualTerms[i], dataSize);
			dataBuffer += dataSize;
		}
	}

	serializedTerm.indexTermVal = compositeTerm;
	serializedTerm.isIndexTermTruncated = false;
	return serializedTerm;
}


BsonCompressableIndexTermSerialized
SerializeCompositeBsonIndexTermWithCompression(bytea **individualTerms,
											   int32_t numTerms)
{
	BsonCompressableIndexTermSerialized serializedTerm = { 0 };

	if (numTerms == 1)
	{
		/* Special case, it's just 1 term - treat it as a non-composite term */
		serializedTerm.indexTermDatum = CompressTermIfNeeded(individualTerms[0]);
		serializedTerm.isIndexTermTruncated = false;
		return serializedTerm;
	}


	BsonIndexTermSerialized indexTerm =
		SerializeCompositeBsonIndexTerm(individualTerms, numTerms);
	serializedTerm.indexTermDatum = CompressTermIfNeeded(indexTerm.indexTermVal);
	serializedTerm.isIndexTermTruncated = indexTerm.isIndexTermTruncated;
	return serializedTerm;
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

	/* Can't mark this as metadata due to back-compat. This is okay coz this is legacy. */
	IndexTermMetadata termMetadata = IndexTermNoMetadata;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  termMetadata).indexTermVal);
}


Datum
GenerateValueUndefinedTerm(const IndexTermCreateMetadata *termData)
{
	pgbsonelement element = { 0 };
	element.path = termData->pathPrefix.string;
	element.pathLength = termData->pathPrefix.length;
	element.bsonValue.value_type = BSON_TYPE_UNDEFINED;

	IndexTermMetadata termMetadata = IndexTermUndefinedValue;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  termMetadata).indexTermVal);
}


Datum
GenerateValueMaybeUndefinedTerm(const IndexTermCreateMetadata *termData)
{
	pgbsonelement element = { 0 };
	element.path = termData->pathPrefix.string;
	element.pathLength = termData->pathPrefix.length;
	element.bsonValue.value_type = BSON_TYPE_UNDEFINED;

	IndexTermMetadata termMetadata = IndexTermPartialUndefinedValue;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  termMetadata).indexTermVal);
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
	IndexTermMetadata termMetadata = IndexTermIsMetadata;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  termMetadata).indexTermVal);
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
	IndexTermMetadata termMetadata = IndexTermIsMetadata;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  termMetadata).indexTermVal);
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
	IndexTermMetadata termMetadata = IndexTermIsMetadata;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  termMetadata).indexTermVal);
}


Datum
GenerateCorrelatedRootArrayTerm(const IndexTermCreateMetadata *termData)
{
	bson_iter_t iter;
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt32(&writer, "", 0, RootMetadataKind_CorrelatedRootArray);

	PgbsonWriterGetIterator(&writer, &iter);

	pgbsonelement element = { 0 };
	BsonIterToSinglePgbsonElement(&iter, &element);
	IndexTermMetadata termMetadata = IndexTermIsMetadata;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  termMetadata).indexTermVal);
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

	/* Can't mark this as metadata due to back-compat. Need to think of how to
	 * handle this. TODO: When we bump index versions make this a metadata term.
	 * Note that this isn't bad since we can never get a MAXKEY that is truncated.
	 */
	uint8_t truncatedMetadata = IndexTermTruncated;
	return PointerGetDatum(SerializeBsonIndexTermCore(&element, termData,
													  truncatedMetadata).indexTermVal);
}


static bytea *
BuildSerializedIndexTerm(pgbsonelement *indexElement, const
						 IndexTermCreateMetadata *createMetadata,
						 IndexTermMetadata termMetadata,
						 BsonIndexTerm *indexTerm)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bool isValueOnly = false;

	/* Only allow user terms to be value only */
	bool allowValueOnly = termMetadata == IndexTermNoMetadata;
	bool isTermTruncated = SerializeTermToWriter(&writer, indexElement,
												 createMetadata,
												 allowValueOnly,
												 &isValueOnly);

	if (isValueOnly)
	{
		if (!allowValueOnly)
		{
			ereport(ERROR, (errmsg(
								"Index term requested no valueOnly generation but got a valueOnlyoffset")));
		}

		termMetadata = isTermTruncated ? IndexTermValueOnlyTruncated : IndexTermValueOnly;
	}
	else if (isTermTruncated && (termMetadata == IndexTermNoMetadata))
	{
		/* If the term is truncated, we need to mark it as such */
		termMetadata = IndexTermTruncated;
	}

	/* Patch term metadata for descending */
	if (createMetadata->isDescending)
	{
		switch (termMetadata)
		{
			case IndexTermValueOnly:
			case IndexTermValueOnlyTruncated:
			case IndexTermNoMetadata:
			case IndexTermTruncated:
			case IndexTermPartialUndefinedValue:
			case IndexTermUndefinedValue:
			{
				termMetadata |= IndexTermDescending;
				break;
			}

			case IndexTermIsMetadata:
			{
				/* This is a metadata term, so we don't need to patch it */
				break;
			}

			default:
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg("Unexpected term metadata %d for descending index",
									   termMetadata),
								errdetail_log(
									"Unexpected term metadata %d for descending index",
									termMetadata)));
				break;
			}
		}
	}

	uint32_t dataSize = PgbsonWriterGetSize(&writer);

	if (createMetadata->indexTermSizeLimit > 0 &&
		dataSize > (uint32_t) createMetadata->indexTermSizeLimit)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Truncation size limit specified %d, but index term with type %s was larger %d - isTruncated %d",
							createMetadata->indexTermSizeLimit, BsonTypeName(
								indexElement->bsonValue.value_type), dataSize,
							IsIndexTermTruncated(indexTerm)),
						errdetail_log(
							"Truncation size limit specified %d, but index term with type %s was larger %d - isTruncated %d",
							createMetadata->indexTermSizeLimit, BsonTypeName(
								indexElement->bsonValue.value_type), dataSize,
							IsIndexTermTruncated(indexTerm))));
	}

	if (isValueOnly)
	{
		/* Allocate enough for the actual data + overhead */
		int indexTermAllocSize = dataSize + VARHDRSZ + sizeof(uint8_t);
		bytea *indexTermVal = (bytea *) palloc(indexTermAllocSize);
		uint8_t *buffer = (uint8_t *) indexTermVal;

		/* we first copy the bson onto the buffer at the appropriate offset
		 * We need 4 bytes in the beginning for the VARLENA header, 1 byte for
		 * the metadata. Technically the content starts at byte 6 (index 5).
		 * However, since BSON prepends a 4-byte length header, we serialize
		 * it starting at index 1 so that the path + value_type starts at
		 * index 5.
		 */
		PgbsonWriterCopyToBuffer(&writer, &buffer[1], dataSize);

		/* Now set the metadata */
		buffer[4] = termMetadata;

		/* Set the real VARSIZE: We subtract the 4 bytes of length we removed
		 * from the bson, and the trailing '\0' that bson carries.
		 */
		int indexTermSize = indexTermAllocSize - sizeof(int32_t) - 1;
		SET_VARSIZE(indexTermVal, indexTermSize);

		indexTerm->termMetadata = termMetadata;
		return indexTermVal;
	}
	else
	{
		int indexTermSize = dataSize + VARHDRSZ + sizeof(uint8_t);
		bytea *indexTermVal = (bytea *) palloc(indexTermSize);
		SET_VARSIZE(indexTermVal, indexTermSize);

		uint8_t *buffer = (uint8_t *) VARDATA(indexTermVal);
		buffer[0] = termMetadata;
		indexTerm->termMetadata = termMetadata;
		PgbsonWriterCopyToBuffer(&writer, &buffer[1], dataSize);
		return indexTermVal;
	}
}
