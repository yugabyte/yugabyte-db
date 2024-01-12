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

#include "io/bson_core.h"
#include "types/decimal128.h"

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

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

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
 * HashNumber returns the hash of a double.
 */
static uint64
HashNumber(double number, int64 seed)
{
	return BSON_FIXED_LENGTH_FIELD_HASH(number, seed);
}
