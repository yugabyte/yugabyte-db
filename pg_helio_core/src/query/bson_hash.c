/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_hash.c
 *
 * Implementation of hashing of the BSON type.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <common/hashfn.h>
#include <math.h>
#include <miscadmin.h>
#include <fmgr.h>

#include "io/bson_hash.h"
#include "types/decimal128.h"
#include "utils/helio_errors.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

#define BSON_VARIABLE_LENGTH_FIELD_HASH(fieldPointer, length, seed) \
	hash_bytes_extended((const unsigned char *) fieldPointer, length, seed)

#define BSON_FIXED_LENGTH_FIELD_HASH(field, seed) \
	BSON_VARIABLE_LENGTH_FIELD_HASH(&(field), sizeof(field), seed)

static uint64 HashNumber(double number, int64 seed);
static uint64 HashBytesUint64(const uint8_t *bytes, uint32_t bytesLength, int64 seed);

static uint64_t HashBsonValueCompare(const bson_value_t *value,
									 uint64 (*hash_bytes_func)(const uint8_t *bytes,
															   uint32_t bytesLength, int64
															   seed),
									 uint64 (*hash_combine_func)(uint64 left, uint64
																 right),
									 int64 seed);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(extension_bson_hash_int8);
PG_FUNCTION_INFO_V1(extension_bson_hash_int4);


/*
 * Generates the hash of a bson as an int4 value
 */
Datum
extension_bson_hash_int4(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON_PACKED(0);
	Datum result;

	bson_iter_t bsonIterator;
	PgbsonInitIterator(bson, &bsonIterator);

	int64 seed = 0;
	result = UInt32GetDatum((uint32_t) BsonHashCompare(&bsonIterator,
													   HashBytesUint32AsUint64,
													   HashCombineUint32AsUint64, seed));

	/* Avoid leaking memory for toasted inputs */
	PG_FREE_IF_COPY(bson, 0);
	return result;
}


/*
 * Generates the hash of a bson as an int8 value
 */
Datum
extension_bson_hash_int8(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON_PACKED(0);
	int64 seed = PG_GETARG_INT64(1);
	Datum result;

	bson_iter_t bsonIterator;
	PgbsonInitIterator(bson, &bsonIterator);

	result = UInt64GetDatum(BsonHashCompare(&bsonIterator, HashBytesUint64,
											hash_combine64, seed));
	PG_FREE_IF_COPY(bson, 0);

	return result;
}


/*
 * BsonValueHash returns a hash value for a given BSON value using
 * the internal hash_bytes function in PostgreSQL.
 */
int64
BsonValueHash(const bson_value_t *value, int64 seed)
{
	bson_type_t hashType = value->value_type;

	if (hashType == BSON_TYPE_INT32 || hashType == BSON_TYPE_INT64)
	{
		/* map double, int, and long use the same hash type */
		hashType = BSON_TYPE_DOUBLE;
	}

	/* start by hashing the type */
	int64 hashValue = hash_bytes_uint32_extended(hashType, seed);

	switch (value->value_type)
	{
		case BSON_TYPE_EOD:
		case BSON_TYPE_MINKEY:
		case BSON_TYPE_UNDEFINED:
		case BSON_TYPE_NULL:
		case BSON_TYPE_MAXKEY:
		{
			/* no meaningful value, only hash the type */
			return hashValue;
		}

		case BSON_TYPE_BOOL:
		{
			return hash_bytes_uint32_extended(value->value.v_bool, hashValue);
		}

		case BSON_TYPE_INT32:
		{
			return HashNumber(value->value.v_int32, hashValue);
		}

		case BSON_TYPE_INT64:
		{
			return HashNumber(value->value.v_int64, hashValue);
		}

		case BSON_TYPE_DOUBLE:
		{
			return HashNumber(value->value.v_double, hashValue);
		}

		case BSON_TYPE_DATE_TIME:
		{
			return BSON_FIXED_LENGTH_FIELD_HASH(value->value.v_datetime, hashValue);
		}

		case BSON_TYPE_TIMESTAMP:
		{
			return BSON_FIXED_LENGTH_FIELD_HASH(value->value.v_timestamp, hashValue);
		}

		case BSON_TYPE_OID:
		{
			return BSON_FIXED_LENGTH_FIELD_HASH(value->value.v_oid, hashValue);
		}

		case BSON_TYPE_DECIMAL128:
		{
			return BSON_FIXED_LENGTH_FIELD_HASH(value->value.v_decimal128, hashValue);
		}

		case BSON_TYPE_UTF8:
		{
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_utf8.str,
												   value->value.v_utf8.len, hashValue);
		}

		case BSON_TYPE_ARRAY:
		case BSON_TYPE_DOCUMENT:
		{
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_doc.data,
												   value->value.v_doc.data_len,
												   hashValue);
		}

		case BSON_TYPE_SYMBOL:
		{
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_symbol.symbol,
												   value->value.v_symbol.len, hashValue);
		}

		case BSON_TYPE_BINARY:
		{
			/* ignores subtype */
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_binary.data,
												   value->value.v_binary.data_len,
												   hashValue);
		}

		case BSON_TYPE_REGEX:
		{
			/* ignores options */
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_regex.regex,
												   strlen(value->value.v_regex.regex),
												   hashValue);
		}

		case BSON_TYPE_DBPOINTER:
		{
			/* ignores BSON OID */
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_dbpointer.collection,
												   value->value.v_dbpointer.collection_len,
												   hashValue);
		}

		case BSON_TYPE_CODE:
		{
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_code.code,
												   value->value.v_code.code_len,
												   hashValue);
		}

		case BSON_TYPE_CODEWSCOPE:
		{
			/* ignores scope */
			return BSON_VARIABLE_LENGTH_FIELD_HASH(value->value.v_codewscope.code,
												   value->value.v_codewscope.code_len,
												   hashValue);
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("cannot compute hash for BSON type")));
		}
	}
}


/*
 * BsonValueHashUint32 generates a uint32 hash value for a given BSON value.
 */
uint32
BsonValueHashUint32(const bson_value_t *bsonValue)
{
	switch (bsonValue->value_type)
	{
		case BSON_TYPE_BOOL:
		{
			return hash_bytes((const unsigned char *)
							  &(bsonValue->value.v_bool),
							  sizeof(bool));
		}

		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		{
			int64 value = BsonValueAsInt64(bsonValue);
			return hash_bytes((const unsigned char *) &value,
							  sizeof(int64));
		}

		case BSON_TYPE_DOUBLE:
		{
			/* NaN and -NaN should create same hash value */
			if (isnan(bsonValue->value.v_double))
			{
				return 1;
			}

			/* If the value can be converted to int64, then convert it to int64 and generate a hash,
			 * which ensures that the hash value of both 1.00 and 1 remains the same. */
			bool checkFixedInteger = true;
			if (IsBsonValue64BitInteger(bsonValue, checkFixedInteger))
			{
				int64 value = BsonValueAsInt64(bsonValue);
				return hash_bytes((const unsigned char *) &value,
								  sizeof(int64));
			}

			/* In MongoDB set operators aggregation, non-fixed double and Decimal128 values with the same numerical value are not considered equal.
			 * For example, if we have a double value of "1.1" and a Decimal128 value of "1.1", they will not be considered equal.
			 * To ensure that these values are not treated as equal, different hashes are generated for these values.*/
			return hash_bytes((const unsigned char *)
							  &bsonValue->value.v_double,
							  sizeof(double));
		}

		case BSON_TYPE_DECIMAL128:
		{
			/* NaN and -NaN should create same hash value */
			if (IsDecimal128NaN(bsonValue))
			{
				return 1;
			}

			/* If the value can be converted to int64, then convert it to int64 and generate a hash,
			 * which ensures that the hash value of both 1.00 and 1 remains the same. */
			bool checkFixedInteger = true;
			if (IsBsonValue64BitInteger(bsonValue, checkFixedInteger))
			{
				int64 value = BsonValueAsInt64(bsonValue);
				return hash_bytes((const unsigned char *) &value,
								  sizeof(int64));
			}

			/* In MongoDB set operators aggregation, non-fixed double and Decimal128 values with the same numerical value are not considered equal.
			 * For example, if we have a double value of "1.1" and a Decimal128 value of "1.1", they will not be considered equal.
			 * To ensure that these values are not treated as equal, different hashes are generated for these values.*/
			return hash_bytes((const unsigned char *)
							  &bsonValue->value.v_decimal128,
							  sizeof(bsonValue->value.v_utf8.len));
		}

		case BSON_TYPE_UTF8:
		{
			return hash_bytes((const unsigned char *)
							  bsonValue->value.v_utf8.str,
							  bsonValue->value.v_utf8.len);
		}

		case BSON_TYPE_DOCUMENT:
		case BSON_TYPE_ARRAY:
		{
			return hash_bytes((const unsigned char *)
							  bsonValue->value.v_doc.data,
							  bsonValue->value.v_doc.data_len);
		}

		case BSON_TYPE_BINARY:
		{
			return hash_bytes((const unsigned char *)
							  bsonValue->value.v_binary.data,
							  bsonValue->value.v_binary.data_len);
		}

		case BSON_TYPE_DATE_TIME:
		{
			return hash_bytes((const unsigned char *)
							  &bsonValue->value.v_datetime,
							  sizeof(int64));
		}

		case BSON_TYPE_TIMESTAMP:
		{
			return hash_bytes((const unsigned char *)
							  &bsonValue->value.v_timestamp.timestamp,
							  sizeof(int64));
		}

		case BSON_TYPE_OID:
		{
			return hash_bytes((const unsigned char *)
							  &bsonValue->value.v_oid.bytes,
							  sizeof(bson_oid_t));
		}

		case BSON_TYPE_REGEX:
		{
			return hash_bytes((const unsigned char *)
							  &bsonValue->value.v_regex.regex,
							  strlen(bsonValue->value.v_regex.regex));
		}

		case BSON_TYPE_CODE:
		case BSON_TYPE_CODEWSCOPE:
		{
			return hash_bytes((const unsigned char *)
							  bsonValue->value.v_code.code,
							  bsonValue->value.v_code.code_len);
		}

		case BSON_TYPE_SYMBOL:
		{
			return hash_bytes((const unsigned char *)
							  bsonValue->value.v_symbol.symbol,
							  bsonValue->value.v_symbol.len);
		}

		case BSON_TYPE_DBPOINTER:
		{
			/*
			 * we generate a hash using only the collection name because including the 'oid' field in the
			 * hash computation would be more expensive than comparing the 'oid' values directly if hash matches.
			 */
			return hash_bytes((const unsigned char *)
							  bsonValue->value.v_dbpointer.collection,
							  bsonValue->value.v_dbpointer.collection_len);
		}

		case BSON_TYPE_UNDEFINED:
		case BSON_TYPE_NULL:
		case BSON_TYPE_EOD:
		case BSON_TYPE_MINKEY:
		case BSON_TYPE_MAXKEY:
		{
			return 0;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("unknown BSON type code %d",
								   bsonValue->value_type)));
		}
	}
}


/*
 * Hash as uint32 but represented as uint64.
 */
uint64
HashBytesUint32AsUint64(const uint8_t *bytes, uint32_t bytesLength, int64 seed)
{
	return hash_bytes(bytes, bytesLength);
}


/*
 * Combine hash as uint32 but represented as uint64.
 */
uint64
HashCombineUint32AsUint64(uint64 left, uint64 right)
{
	return hash_combine((uint32_t) left, (uint32_t) right);
}


uint64
BsonHashCompare(bson_iter_t *bsonIterValue,
				uint64 (*hash_bytes_func)(const uint8_t *bytes, uint32_t bytesLength,
										  int64 seed),
				uint64 (*hash_combine_func)(uint64 left, uint64 right),
				int64 seed)
{
	check_stack_depth();
	uint64_t hashValue = 0;
	while (bson_iter_next(bsonIterValue))
	{
		pgbsonelement leftElement;

		BsonIterToPgbsonElement(bsonIterValue, &leftElement);

		/* next compare field name. */
		uint64_t pathHash = hash_bytes_func((uint8_t *) leftElement.path,
											leftElement.pathLength, seed);
		hashValue = hash_combine_func(hashValue, pathHash);

		uint64_t valueHash = HashBsonValueCompare(&leftElement.bsonValue, hash_bytes_func,
												  hash_combine_func, seed);
		hashValue = hash_combine_func(hashValue, valueHash);
	}

	return hashValue;
}


/*
 * HashNumber returns the hash of a double.
 */
static uint64
HashNumber(double number, int64 seed)
{
	return BSON_FIXED_LENGTH_FIELD_HASH(number, seed);
}


/*
 * Hashes a value assuming that if ValueA == ValueB
 * Hash(ValueA) == Hash(ValueB)
 *
 * CODESYNC: This needs to match the behavior of CompareBsonValue in bson_compare.c
 */
static uint64_t
HashBsonValueCompare(const bson_value_t *value,
					 uint64 (*hash_bytes_func)(const uint8_t *bytes, uint32_t bytesLength,
											   int64 seed),
					 uint64 (*hash_combine_func)(uint64 left, uint64 right),
					 int64 seed)
{
	int typeCodeInt = (int) value->value_type;
	switch (value->value_type)
	{
		case BSON_TYPE_EOD:
		case BSON_TYPE_MINKEY:
		{
			typeCodeInt = (int) BSON_TYPE_MINKEY;
			return hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed);
		}

		case BSON_TYPE_UNDEFINED:
		case BSON_TYPE_NULL:
		{
			typeCodeInt = (int) BSON_TYPE_UNDEFINED;
			return hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed);
		}

		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		{
			/* All numbers are cocomparable - use a fixed type code */
			typeCodeInt = BSON_TYPE_INT64;
			int64_t int64Value = BsonValueAsInt64(value);
			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) &int64Value, sizeof(int64_t), seed));
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_DECIMAL128:
		{
			/* All numbers are cocomparable - use a fixed type code */
			typeCodeInt = BSON_TYPE_INT64;
			bool checkFixedInteger = true;
			if (IsBsonValue64BitInteger(value, checkFixedInteger))
			{
				int64_t int64Value = BsonValueAsInt64(value);
				return hash_combine_func(
					hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
					hash_bytes_func((uint8_t *) &int64Value, sizeof(int64_t), seed));
			}

			bson_decimal128_t decimalValue = GetBsonValueAsDecimal128(value);
			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) &decimalValue, sizeof(bson_decimal128_t),
								seed));
		}

		case BSON_TYPE_UTF8:
		case BSON_TYPE_SYMBOL:
		{
			typeCodeInt = (int) BSON_TYPE_UTF8;
			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) value->value.v_utf8.str,
								value->value.v_utf8.len, seed));
		}

		case BSON_TYPE_DOCUMENT:
		case BSON_TYPE_ARRAY:
		{
			bson_iter_t leftInnerIt;
			if (!bson_iter_init_from_data(
					&leftInnerIt,
					value->value.v_doc.data,
					value->value.v_doc.data_len))
			{
				ereport(ERROR, errmsg(
							"Could not initialize nested iterator for document"));
			}

			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				BsonHashCompare(&leftInnerIt, hash_bytes_func, hash_combine_func, seed));
		}

		case BSON_TYPE_BINARY:
		{
			return hash_combine_func(
				hash_combine_func(hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int),
												  seed),
								  hash_bytes_func(
									  (uint8_t *) &value->value.v_binary.subtype,
									  sizeof(bson_subtype_t), seed)),
				hash_bytes_func((uint8_t *) value->value.v_binary.data,
								value->value.v_binary.data_len, seed));
		}

		case BSON_TYPE_OID:
		{
			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) &value->value.v_oid.bytes, 12, seed));
		}

		case BSON_TYPE_BOOL:
		{
			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) &value->value.v_bool, sizeof(bool), seed));
		}

		case BSON_TYPE_DATE_TIME:
		{
			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) &value->value.v_datetime, sizeof(int64_t),
								seed));
		}

		case BSON_TYPE_TIMESTAMP:
		{
			return hash_combine_func(
				hash_combine_func(hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int),
												  seed),
								  hash_bytes_func(
									  (uint8_t *) &value->value.v_timestamp.increment,
									  sizeof(uint32_t), seed)),
				hash_bytes_func((uint8_t *) &value->value.v_timestamp.timestamp,
								sizeof(uint32_t), seed));
		}

		case BSON_TYPE_REGEX:
		{
			uint64 hashValue = hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int),
											   seed);

			if (value->value.v_regex.regex != NULL)
			{
				int regexLen = strlen(value->value.v_regex.regex);
				hashValue = hash_combine_func(hashValue,
											  hash_bytes_func(
												  (uint8_t *) value->value.v_regex.regex,
												  regexLen, seed));
			}

			if (value->value.v_regex.options != NULL)
			{
				int optionsLen = strlen(value->value.v_regex.options);
				hashValue = hash_combine_func(hashValue,
											  hash_bytes_func(
												  (uint8_t *) value->value.v_regex.options,
												  optionsLen, seed));
			}

			return hashValue;
		}

		case BSON_TYPE_DBPOINTER:
		{
			uint64 codeHash = hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) value->value.v_dbpointer.collection,
								value->value.v_dbpointer.collection_len, seed));


			return hash_combine_func(
				codeHash,
				hash_bytes_func((uint8_t *) &value->value.v_dbpointer.oid.bytes, 12,
								seed));
		}

		case BSON_TYPE_CODE:
		{
			return hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) value->value.v_code.code,
								value->value.v_code.code_len, seed));
		}

		case BSON_TYPE_CODEWSCOPE:
		{
			uint64 codeHash = hash_combine_func(
				hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed),
				hash_bytes_func((uint8_t *) value->value.v_code.code,
								value->value.v_code.code_len, seed));

			bson_iter_t leftInnerIt;
			if (!bson_iter_init_from_data(
					&leftInnerIt,
					value->value.v_codewscope.scope_data,
					value->value.v_codewscope.scope_len))
			{
				ereport(ERROR, errmsg(
							"Could not initialize nested iterator for scope"));
			}

			return hash_combine_func(
				codeHash,
				BsonHashCompare(&leftInnerIt, hash_bytes_func, hash_combine_func, seed));
		}

		case BSON_TYPE_MAXKEY:
		{
			return hash_bytes_func((uint8_t *) &typeCodeInt, sizeof(int), seed);
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
							errmsg(
								"invalid bson type for hash value bson- not supported yet"),
							errdetail_log(
								"bson value hash - encountered unsupported type: %d",
								value->value_type)));
		}
	}
}


/*
 * Passthrough for hash uint64 with type coercion
 */
static uint64
HashBytesUint64(const uint8_t *bytes, uint32_t bytesLength, int64 seed)
{
	return hash_bytes_extended((unsigned char *) bytes, bytesLength, seed);
}
