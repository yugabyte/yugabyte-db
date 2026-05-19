/*--------------------------------------------------------------------------------------------------
 *
 * yb_type.c
 *        Commands for creating and altering table structures and settings
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/catalog/yb_catalog/yb_type.c
 *
 * TODO(all) At the mininum we must support the following datatype efficiently as they are used
 * for system tables.
 *   bool
 *   char
 *   text
 *   int2
 *   int4
 *   int8
 *   float4
 *   float8
 *   timestamptz
 *   bytea
 *   oid
 *   xid
 *   cid
 *   tid
 *   name (same as text?)
 *   aclitem
 *   pg_node_tree
 *   pg_lsn
 *   pg_ndistinct
 *   pg_dependencies
 *
 *   OID aliases:
 *
 *   regproc
 *   regprocedure
 *   regoper
 *   regoperator
 *   regclass
 *   regtype
 *   regconfig
 *   regdictionary
 *
 *   Vectors/arrays:
 *
 *   int2vector (list of 16-bit integers)
 *   oidvector (list of 32-bit unsigned integers)
 *   anyarray (list of 32-bit integers - signed or unsigned)
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "catalog/pg_enum.h"
#include "catalog/pg_type.h"
#include "catalog/yb_type.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "parser/parse_type.h"
#include "pg_yb_utils.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/cash.h"
#include "utils/date.h"
#include "utils/geo_decls.h"
#include "utils/inet.h"
#include "utils/lsyscache.h"
#include "utils/numeric.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/uuid.h"
#include "yb/yql/pggate/ybc_pggate.h"

static const YbcPgTypeEntity YBCFixedLenByValTypeEntity;
static const YbcPgTypeEntity YBCNullTermByRefTypeEntity;
static const YbcPgTypeEntity YBCVarLenByRefTypeEntity;

static Datum YbDocdbToDatum(const uint8 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs);
static void YbDatumToDocdb(Datum datum, uint8 **data, int64 *bytes);

static void
yb_report_type_not_supported(Oid typid)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("type not yet supported in Yugabyte: %s (%s)",
					YbGetPotentiallyHiddenOidText(typid),
					YBPgTypeOidToStr(typid))));
}

/***************************************************************************************************
 * Find YugaByte storage type for each PostgreSQL datatype.
 * NOTE: Because YugaByte network buffer can be deleted after it is processed, Postgres layer must
 *       allocate a buffer to keep the data in its slot.
 **************************************************************************************************/
const YbcPgTypeEntity *
YbDataTypeFromOidMod(int attnum, Oid type_id)
{
	/* Find type for system column */
	if (attnum < InvalidAttrNumber)
	{
		switch (attnum)
		{
			case SelfItemPointerAttributeNumber:	/* ctid */
				type_id = TIDOID;
				break;
			case TableOidAttributeNumber:	/* tableoid */
				type_id = OIDOID;
				break;
			case MinCommandIdAttributeNumber:	/* cmin */
			case MaxCommandIdAttributeNumber:	/* cmax */
				type_id = CIDOID;
				break;
			case MinTransactionIdAttributeNumber:	/* xmin */
			case MaxTransactionIdAttributeNumber:	/* xmax */
				type_id = XIDOID;
				break;
			case YBTupleIdAttributeNumber:	/* ybctid */
			case YBIdxBaseTupleIdAttributeNumber:	/* ybidxbasectid */
			case YBUniqueIdxKeySuffixAttributeNumber:	/* ybuniqueidxkeysuffix */
				type_id = BYTEAOID;
				break;
			default:
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("system column not yet supported in Yugabyte: %d", attnum)));
				break;
		}
	}

	/* Find the type mapping entry */
	const YbcPgTypeEntity *type_entity = YBCPgFindTypeEntity(type_id);
	const YbcPgDataType yb_type = (type_entity ?
								   type_entity->yb_type :
								   YB_YQL_DATA_TYPE_UNKNOWN_DATA);

	/* For non-primitive types, we need to look up the definition */
	if (yb_type == YB_YQL_DATA_TYPE_UNKNOWN_DATA)
	{
		HeapTuple	type = typeidType(type_id);
		Form_pg_type tp = (Form_pg_type) GETSTRUCT(type);

		ReleaseSysCache(type);

		if (tp->typtype == TYPTYPE_BASE)
		{
			if (tp->typbyval)
			{
				/* fixed-length, pass-by-value base type */
				return &YBCFixedLenByValTypeEntity;
			}
			else
			{
				switch (tp->typlen)
				{
					case -2:
						/* null-terminated, pass-by-reference base type */
						return &YBCNullTermByRefTypeEntity;
						break;
					case -1:
						/* variable-length, pass-by-reference base type */
						return &YBCVarLenByRefTypeEntity;
						break;
					default:;
						/* fixed-length, pass-by-reference base type */
						YbcPgTypeEntity *fixed_ref_type_entity;

						fixed_ref_type_entity =
							(YbcPgTypeEntity *) palloc(sizeof(YbcPgTypeEntity));
						fixed_ref_type_entity->type_oid = InvalidOid;
						fixed_ref_type_entity->yb_type = YB_YQL_DATA_TYPE_BINARY;
						fixed_ref_type_entity->allow_for_primary_key = false;
						fixed_ref_type_entity->datum_fixed_size = tp->typlen;
						fixed_ref_type_entity->direct_datum = false;
						fixed_ref_type_entity->datum_to_yb = (YbcPgDatumToData) YbDatumToDocdb;
						fixed_ref_type_entity->yb_to_datum =
							(YbcPgDatumFromData) YbDocdbToDatum;
						return fixed_ref_type_entity;
						break;
				}
			}
		}
		else
		{
			Oid			primitive_type_oid =
				YbGetPrimitiveTypeOid(type_id, tp->typtype, tp->typbasetype);

			return YbDataTypeFromOidMod(InvalidAttrNumber, primitive_type_oid);
		}
	}

	/* Report error if type is not supported */
	if (yb_type == YB_YQL_DATA_TYPE_NOT_SUPPORTED)
	{
		yb_report_type_not_supported(type_id);
	}

	/* Return the type-mapping entry */
	return type_entity;
}

Oid
YbGetPrimitiveTypeOid(Oid type_id, char typtype, Oid typbasetype)
{
	Oid			primitive_type_oid;

	switch (typtype)
	{
		case TYPTYPE_BASE:
			primitive_type_oid = type_id;
			break;
		case TYPTYPE_COMPOSITE:
			primitive_type_oid = RECORDOID;
			break;
		case TYPTYPE_DOMAIN:
			primitive_type_oid = typbasetype;
			break;
		case TYPTYPE_ENUM:
			primitive_type_oid = ANYENUMOID;
			break;
		case TYPTYPE_RANGE:
			primitive_type_oid = ANYRANGEOID;
			break;
		default:
			yb_report_type_not_supported(type_id);
			pg_unreachable();
			break;
	}
	return primitive_type_oid;
}

bool
YbDataTypeIsValidForKey(Oid type_id)
{
	const YbcPgTypeEntity *entity = YbDataTypeFromOidMod(InvalidAttrNumber,
														 type_id);

	return entity && entity->allow_for_primary_key;
}

const YbcPgTypeEntity *
YbDataTypeFromName(TypeName *typeName)
{
	Oid			type_id = 0;
	int32		typmod = 0;

	typenameTypeIdAndMod(NULL /* parseState */ , typeName, &type_id, &typmod);
	return YbDataTypeFromOidMod(InvalidAttrNumber, type_id);
}

/***************************************************************************************************
 * Conversion Functions.
 **************************************************************************************************/
/*
 * BOOL conversion.
 * Fixed size: Ignore the "bytes" data size.
 */
void
YbDatumToBool(Datum datum, bool *data, int64 *bytes)
{
	*data = DatumGetBool(datum);
}

Datum
YbBoolToDatum(const bool *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return BoolGetDatum(*data);
}

/*
 * BINARY conversion.
 */
void
YbDatumToBinary(Datum datum, void **data, int64 *bytes)
{
	datum = PointerGetDatum(PG_DETOAST_DATUM(datum));
	*data = VARDATA_ANY(datum);
	*bytes = VARSIZE_ANY_EXHDR(datum);
}

Datum
YbBinaryToDatum(const void *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/*
	 * PostgreSQL can represent text strings up to 1 GB minus a four-byte
	 * header.
	 */
	if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
						errmsg("invalid data size")));
	}
	return PointerGetDatum(cstring_to_text_with_len(data, bytes));
}

/*
 * TEXT type conversion.
 */
void
YbDatumToText(Datum datum, char **data, int64 *bytes)
{
	datum = PointerGetDatum(PG_DETOAST_DATUM(datum));
	*data = VARDATA_ANY(datum);
	*bytes = VARSIZE_ANY_EXHDR(datum);
}

Datum
YbTextToDatum(const char *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/*
	 * While reading back TEXT from storage, we don't need to check for data
	 * length.
	 */
	return PointerGetDatum(cstring_to_text_with_len(data, bytes));
}

/*
 * CHAR type conversion.
 */
void
YbDatumToChar(Datum datum, char *data, int64 *bytes)
{
	*data = DatumGetChar(datum);
}

Datum
YbCharToDatum(const char *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return CharGetDatum(*data);
}

/*
 * CHAR-based type conversion.
 */
void
YbDatumToBPChar(Datum datum, char **data, int64 *bytes)
{
	int			size;

	*data = TextDatumGetCString(datum);

	/*
	 * Right trim all spaces on the right. For CHAR(n) - BPCHAR - datatype, Postgres treats space
	 * characters at tail-end the same as '\0' characters.
	 *   "abc  " == "abc"
	 * Left spaces don't have this special behaviors.
	 *   "  abc" != "abc"
	 */
	size = strlen(*data);
	while (size > 0 && isspace((*data)[size - 1]))
	{
		size--;
	}
	*bytes = size;
}

Datum
YbBPCharToDatum(const char *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/*
	 * PostgreSQL can represent text strings up to 1 GB minus a four-byte
	 * header.
	 */
	if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
						errmsg("invalid data size")));
	}

	LOCAL_FCINFO(fcinfo, 3);
	InitFunctionCallInfoData(*fcinfo, NULL, 3, InvalidOid, NULL, NULL);

	PG_GETARG_DATUM(0) = CStringGetDatum(data);
	PG_GETARG_DATUM(2) = Int32GetDatum(type_attrs->typmod);
	return bpcharin(fcinfo);
}

void
YbDatumToVarchar(Datum datum, char **data, int64 *bytes)
{
	*data = TextDatumGetCString(datum);
	*bytes = strlen(*data);
}

Datum
YbVarcharToDatum(const char *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/*
	 * PostgreSQL can represent text strings up to 1 GB minus a four-byte
	 * header.
	 */
	if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
						errmsg("invalid data size")));
	}

	LOCAL_FCINFO(fcinfo, 3);
	InitFunctionCallInfoData(*fcinfo, NULL, 3, InvalidOid, NULL, NULL);

	PG_GETARG_DATUM(0) = CStringGetDatum(data);
	PG_GETARG_DATUM(2) = Int32GetDatum(type_attrs->typmod);
	return varcharin(fcinfo);
}

/*
 * NAME conversion.
 */
void
YbDatumToName(Datum datum, char **data, int64 *bytes)
{
	*data = DatumGetCString(datum);
	*bytes = strlen(*data);
}

Datum
YbNameToDatum(const char *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/*
	 * PostgreSQL can represent text strings up to 1 GB minus a four-byte
	 * header.
	 */
	if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
						errmsg("invalid data size")));
	}

	/* Truncate oversize input */
	if (bytes >= NAMEDATALEN)
		bytes = pg_mbcliplen(data, bytes, NAMEDATALEN - 1);

	/* We use palloc0 here to ensure result is zero-padded */
	Name		result = (Name) palloc0(NAMEDATALEN);

	memcpy(NameStr(*result), data, bytes);
	return NameGetDatum(result);
}

/*
 * PSEUDO-type cstring conversion.
 * Not a type that is used by users.
 */
void
YbDatumToCStr(Datum datum, char **data, int64 *bytes)
{
	*data = DatumGetCString(datum);
	*bytes = strlen(*data);
}

Datum
YbCStrToDatum(const char *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/*
	 * PostgreSQL can represent text strings up to 1 GB minus a four-byte
	 * header.
	 */
	if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
						errmsg("invalid data size")));
	}

	/*
	 * data may or may not contain tailing \0.
	 * The result will be null-terminated string in both cases.
	 */
	return CStringGetDatum(pnstrdup(data, bytes));
}

/*
 * INTEGERs conversion.
 * Fixed size: Ignore the "bytes" data size.
 */
void
YbDatumToInt16(Datum datum, int16 *data, int64 *bytes)
{
	*data = DatumGetInt16(datum);
}

Datum
YbInt16ToDatum(const int16 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return Int16GetDatum(*data);
}

void
YbDatumToInt32(Datum datum, int32 *data, int64 *bytes)
{
	*data = DatumGetInt32(datum);
}

Datum
YbInt32ToDatum(const int32 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return Int32GetDatum(*data);
}

void
YbDatumToInt64(Datum datum, int64 *data, int64 *bytes)
{
	*data = DatumGetInt64(datum);
}

Datum
YbInt64ToDatum(const int64 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return Int64GetDatum(*data);
}

void
YbDatumToUInt64(Datum datum, uint64 *data, uint64 *bytes)
{
	*data = DatumGetUInt64(datum);
}

Datum
YbUInt64ToDatum(const uint64 *data, uint64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return UInt64GetDatum(*data);
}

/*
 * Given datum representing a 4-byte enum oid, lookup its sort order which is
 * a 4-byte float, then treat the sort order as a 4-byte integer. Combine
 * the sort order with the enum oid to make an int64 by putting the sort order
 * at the high 4-byte and the enum oid at the low 4-byte.
 */
void
YbDatumToEnum(Datum datum, int64 *data, int64 *bytes)
{
	if (!bytes)
	{
		HeapTuple	tup;
		Form_pg_enum en;
		uint32_t	sort_order;

		/*
		 * We expect datum to only contain a enum oid and does not already contain a sort order.
		 * For OID >= 2147483648, Postgres sign-extends datum with 0xffffffff, which is -NaN and
		 * does not reprensent a valid sort order.
		 */
		Assert(!(datum >> 32) || ((datum >> 32) == 0xffffffff));

		/* Clear the high 4-byte in case it is not zero. */
		datum &= 0xffffffff;

		/*
		 * Find the sort order of this enum oid.
		 */
		tup = SearchSysCache1(ENUMOID, datum);
		Assert(tup);
		en = (Form_pg_enum) GETSTRUCT(tup);
		sort_order = *(uint32 *) (&en->enumsortorder);

		/*
		 * Place the sort order at the high 4-byte of datum.
		 */
		datum |= ((int64) sort_order) << 32;
		ReleaseSysCache(tup);
	}
	else
	{
		/*
		 * If the caller passes a non-null address to bytes then it means it requests
		 * us to not add sort order to datum for testing purpose.
		 */
	}
	*data = DatumGetInt64(datum);
}

Datum
YbEnumToDatum(const int64 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/* Clear the sort order from the higher 4-bytes. */
	return Int64GetDatum(*data) & 0xffffffffLL;
}

void
YbDatumToOid(Datum datum, Oid *data, int64 *bytes)
{
	*data = DatumGetObjectId(datum);
}

Datum
YbOidToDatum(const Oid *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return ObjectIdGetDatum(*data);
}

void
YbDatumToCommandId(Datum datum, CommandId *data, int64 *bytes)
{
	*data = DatumGetCommandId(datum);
}

Datum
YbCommandIdToDatum(const CommandId *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return CommandIdGetDatum(*data);
}

void
YbDatumToTransactionId(Datum datum, TransactionId *data, int64 *bytes)
{
	*data = DatumGetTransactionId(datum);
}

Datum
YbTransactionIdToDatum(const TransactionId *data, int64 bytes,
					   const YbcPgTypeAttrs *type_attrs)
{
	return TransactionIdGetDatum(*data);
}

/*
 * FLOATs conversion.
 * Fixed size: Ignore the "bytes" data size.
 */
void
YbDatumToFloat4(Datum datum, float *data, int64 *bytes)
{
	*data = DatumGetFloat4(datum);
}

Datum
YbFloat4ToDatum(const float *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return Float4GetDatum(*data);
}

void
YbDatumToFloat8(Datum datum, double *data, int64 *bytes)
{
	*data = DatumGetFloat8(datum);
}

Datum
YbFloat8ToDatum(const double *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return Float8GetDatum(*data);
}

/*
 * DECIMAL / NUMERIC conversion.
 * We're using plaintext c-string as an intermediate step between PG and YB numerics.
 */
void
YbDatumToDecimalText(Datum datum, char *plaintext[], int64 *bytes)
{
	Numeric		num = DatumGetNumeric(datum);

	*plaintext = numeric_normalize(num);
	/* NaN and infinity support is tracked under GH issue #23075 */
	if (strncmp(*plaintext, "NaN", 3) == 0)
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("DECIMAL does not support NaN yet")));
	else if (strcmp(*plaintext, "Infinity") == 0 ||
			 strcmp(*plaintext, "-Infinity") == 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("DECIMAL does not support Infinity yet")));
}

Datum
YbDecimalTextToDatum(const char plaintext[], int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	LOCAL_FCINFO(fcinfo, 3);
	InitFunctionCallInfoData(*fcinfo, NULL, 3, InvalidOid, NULL, NULL);

	PG_GETARG_DATUM(0) = CStringGetDatum(plaintext);
	PG_GETARG_DATUM(2) = Int32GetDatum(type_attrs->typmod);
	return numeric_in(fcinfo);
}

/*
 * MONEY conversion.
 * We're using int64 as a representation, just like Postgres does.
 */
void
YbDatumToMoneyInt64(Datum datum, int64 *data, int64 *bytes)
{
	*data = DatumGetCash(datum);
}

Datum
YbMoneyInt64ToDatum(const int64 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return CashGetDatum(*data);
}

/*
 * UUID Datatype.
 */
void
YbDatumToUuid(Datum datum, unsigned char **data, int64 *bytes)
{
	/* Postgres store uuid as hex string. */
	*data = (DatumGetUUIDP(datum))->data;
	*bytes = UUID_LEN;
}

Datum
YbUuidToDatum(const unsigned char *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	/*
	 * We have to make a copy for data because the "data" pointer belongs to
	 * YugaByte cache memory which can be cleared at any time.
	 */
	pg_uuid_t  *uuid;

	if (bytes != UUID_LEN)
	{
		ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
						errmsg("unexpected size for UUID (%ld)", bytes)));
	}

	uuid = (pg_uuid_t *) palloc(sizeof(pg_uuid_t));
	memcpy(uuid->data, data, UUID_LEN);
	return UUIDPGetDatum(uuid);
}

/*
 * DATE conversions.
 * PG represents DATE as signed int32 number of days since 2000-01-01, we store it as-is
 */

void
YbDatumToDate(Datum datum, int32 *data, int64 *bytes)
{
	*data = DatumGetDateADT(datum);
}

Datum
YbDateToDatum(const int32 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return DateADTGetDatum(*data);
}

/*
 * TIME conversions.
 * PG represents TIME as microseconds in int64, we store it as-is
 */

void
YbDatumToTime(Datum datum, int64 *data, int64 *bytes)
{
	*data = DatumGetTimeADT(datum);
}

Datum
YbTimeToDatum(const int64 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	return TimeADTGetDatum(*data);
}

/*
 * INTERVAL conversions.
 * PG represents INTERVAL as 128 bit structure, store it as binary
 */
void
YbDatumToInterval(Datum datum, void **data, int64 *bytes)
{
	*data = DatumGetIntervalP(datum);
	*bytes = sizeof(Interval);
}

Datum
YbIntervalToDatum(const void *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	const size_t sz = sizeof(Interval);

	if (bytes != sz)
	{
		ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
						errmsg("unexpected size for Interval (%ld)", bytes)));
	}
	Interval   *result = palloc(sz);

	memcpy(result, data, sz);
	return IntervalPGetDatum(result);
}

void
YbDatumToGinNull(Datum datum, uint8 *data, int64 *bytes)
{
	*data = DatumGetUInt8(datum);
}

Datum
YbGinNullToDatum(const uint8 *data,
				 int64 bytes,
				 const YbcPgTypeAttrs *type_attrs)
{
	return UInt8GetDatum(*data);
}

/*
 * Workaround: These conversion functions can be used as a quick workaround to support a type.
 * - Used for Datum that contains address or pointer of actual data structure.
 *     Datum = pointer to { 1 or 4 bytes for data-size | data }
 * - Save Datum exactly as-is in YugaByte storage when writing.
 * - Read YugaByte storage and copy as-is to Postgres's in-memory datum when reading.
 *
 * IMPORTANT NOTE: This doesn't work for data values that are cached in-place instead of in a
 * separate space to which the datum is pointing to. For example, it doesn't work for numeric
 * values such as int64_t.
 *   int64_value = (int64)(datum)
 *   Datum = (cached_in_place_datatype)(data)
 */

void
YbDatumToDocdb(Datum datum, uint8 **data, int64 *bytes)
{
	if (*bytes < 0)
	{
		datum = PointerGetDatum(PG_DETOAST_DATUM(datum));
		*bytes = VARSIZE_ANY(datum);
	}
	*data = (uint8 *) datum;
}

Datum
YbDocdbToDatum(const uint8 *data, int64 bytes, const YbcPgTypeAttrs *type_attrs)
{
	uint8	   *result = palloc(bytes);

	memcpy(result, data, bytes);
	return PointerGetDatum(result);
}

/*
 * Other conversions.
 */

/***************************************************************************************************
 * Conversion Table
 * Contain function pointers for conversion between PostgreSQL Datum to YugaByte data.
 *
 * TODO(Alex)
 * - Change NOT_SUPPORTED to proper datatype.
 * - Turn ON or OFF certain type for KEY (true or false) when testing its support.
 **************************************************************************************************/
/* YB_TODO(mihnea@yugabyte)
 * Assign an engineer to go through a list of PG13 new datatype to check the following.
 * For each new entry.
 * - Check sizeof(type) is correct.
 * - Check yugabyte storage type is correct.
 * - Check conversion method is correct.
 * - Add tests for all new PG13 datatypes.
 */
static const YbcPgTypeEntity YbTypeEntityTable[] = {
	{BOOLOID, YB_YQL_DATA_TYPE_BOOL, true, sizeof(bool), true,
		(YbcPgDatumToData) YbDatumToBool,
	(YbcPgDatumFromData) YbBoolToDatum},

	{BYTEAOID, YB_YQL_DATA_TYPE_BINARY, true, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{CHAROID, YB_YQL_DATA_TYPE_INT8, true, -1, true,
		(YbcPgDatumToData) YbDatumToChar,
	(YbcPgDatumFromData) YbCharToDatum},

	{NAMEOID, YB_YQL_DATA_TYPE_STRING, true, -1, false,
		(YbcPgDatumToData) YbDatumToName,
	(YbcPgDatumFromData) YbNameToDatum},

	{INT8OID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToInt64,
	(YbcPgDatumFromData) YbInt64ToDatum},

	{INT2OID, YB_YQL_DATA_TYPE_INT16, true, sizeof(int16), true,
		(YbcPgDatumToData) YbDatumToInt16,
	(YbcPgDatumFromData) YbInt16ToDatum},

	{INT2VECTOROID, YB_YQL_DATA_TYPE_BINARY, true, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT4OID, YB_YQL_DATA_TYPE_INT32, true, sizeof(int32), false,
		(YbcPgDatumToData) YbDatumToInt32,
	(YbcPgDatumFromData) YbInt32ToDatum},

	{REGPROCOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{TEXTOID, YB_YQL_DATA_TYPE_STRING, true, -1, false,
		(YbcPgDatumToData) YbDatumToText,
	(YbcPgDatumFromData) YbTextToDatum},

	{OIDOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{TIDOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(ItemPointerData), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{XIDOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(TransactionId), true,
		(YbcPgDatumToData) YbDatumToTransactionId,
	(YbcPgDatumFromData) YbTransactionIdToDatum},

	{CIDOID, YB_YQL_DATA_TYPE_UINT32, false, sizeof(CommandId), true,
		(YbcPgDatumToData) YbDatumToCommandId,
	(YbcPgDatumFromData) YbCommandIdToDatum},

	{OIDVECTOROID, YB_YQL_DATA_TYPE_BINARY, true, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{JSONOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{JSONARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{XMLOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{PG_NODE_TREEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_NDISTINCTOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_DEPENDENCIESOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_MCV_LISTOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_DDL_COMMANDOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToInt64,
	(YbcPgDatumFromData) YbInt64ToDatum},

	{XID8OID, YB_YQL_DATA_TYPE_UINT64, true, sizeof(TransactionId), true,
		(YbcPgDatumToData) YbDatumToTransactionId,
	(YbcPgDatumFromData) YbTransactionIdToDatum},

	{POINTOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(Point), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{LSEGOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(LSEG), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{PATHOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{BOXOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(BOX), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{POLYGONOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{LINEOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(LINE), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{FLOAT4OID, YB_YQL_DATA_TYPE_FLOAT, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToFloat4,
	(YbcPgDatumFromData) YbFloat4ToDatum},

	{FLOAT8OID, YB_YQL_DATA_TYPE_DOUBLE, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToFloat8,
	(YbcPgDatumFromData) YbFloat8ToDatum},

	{UNKNOWNOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{CIRCLEOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(CIRCLE), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	/* We're using int64 to represent monetary type, just like Postgres does. */
	{MONEYOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToMoneyInt64,
	(YbcPgDatumFromData) YbMoneyInt64ToDatum},

	{MACADDROID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(macaddr), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{INETOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{CIDROID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{CIDRARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{MACADDR8OID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(macaddr8), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{ACLITEMOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(AclItem), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{BPCHAROID, YB_YQL_DATA_TYPE_STRING, true, -1, false,
		(YbcPgDatumToData) YbDatumToBPChar,
	(YbcPgDatumFromData) YbBPCharToDatum},

	{VARCHAROID, YB_YQL_DATA_TYPE_STRING, true, -1, false,
		(YbcPgDatumToData) YbDatumToVarchar,
	(YbcPgDatumFromData) YbVarcharToDatum},

	{DATEOID, YB_YQL_DATA_TYPE_INT32, true, sizeof(int32), true,
		(YbcPgDatumToData) YbDatumToDate,
	(YbcPgDatumFromData) YbDateToDatum},

	{TIMEOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToTime,
	(YbcPgDatumFromData) YbTimeToDatum},

	{TIMESTAMPOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToInt64,
	(YbcPgDatumFromData) YbInt64ToDatum},

	{TIMESTAMPTZOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToInt64,
	(YbcPgDatumFromData) YbInt64ToDatum},

	{INTERVALOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(Interval), false,
		(YbcPgDatumToData) YbDatumToInterval,
	(YbcPgDatumFromData) YbIntervalToDatum},

	{TIMETZOID, YB_YQL_DATA_TYPE_BINARY, false, sizeof(TimeTzADT), false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{BITOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{VARBITOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{NUMERICOID, YB_YQL_DATA_TYPE_DECIMAL, true, -1, false,
		(YbcPgDatumToData) YbDatumToDecimalText,
	(YbcPgDatumFromData) YbDecimalTextToDatum},

	{REFCURSOROID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{REGPROCEDUREOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGOPEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGOPERATOROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGCLASSOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGCOLLATIONOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGTYPEOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGROLEOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGNAMESPACEOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{UUIDOID, YB_YQL_DATA_TYPE_BINARY, true, -1, false,
		(YbcPgDatumToData) YbDatumToUuid,
	(YbcPgDatumFromData) YbUuidToDatum},

	{PG_LSNOID, YB_YQL_DATA_TYPE_UINT64, true, sizeof(uint64), true,
		(YbcPgDatumToData) YbDatumToUInt64,
	(YbcPgDatumFromData) YbUInt64ToDatum},

	{TSVECTOROID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{GTSVECTOROID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{TSQUERYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{REGCONFIGOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{REGDICTIONARYOID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{JSONBOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{JSONPATHOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TXID_SNAPSHOTOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_SNAPSHOTOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT4RANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{NUMRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSTZRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{DATERANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT8RANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT4MULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{NUMMULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSMULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSTZMULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{DATEMULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT8MULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{RECORDOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{RECORDARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	/*
	 * Length(cstring) == -2 to be consistent with Postgres's 'typlen'
	 * attribute
	 */
	{CSTRINGOID, YB_YQL_DATA_TYPE_STRING, true, -2, false,
		(YbcPgDatumToData) YbDatumToCStr,
	(YbcPgDatumFromData) YbCStrToDatum},

	{ANYARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{VOIDOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToInt64,
	(YbcPgDatumFromData) YbInt64ToDatum},

	{TRIGGEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{EVENT_TRIGGEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{LANGUAGE_HANDLEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{INTERNALOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), true,
		(YbcPgDatumToData) YbDatumToInt64,
	(YbcPgDatumFromData) YbInt64ToDatum},

	{ANYELEMENTOID, YB_YQL_DATA_TYPE_INT32, true, sizeof(int32), true,
		(YbcPgDatumToData) YbDatumToInt32,
	(YbcPgDatumFromData) YbInt32ToDatum},

	{ANYNONARRAYOID, YB_YQL_DATA_TYPE_INT32, true, sizeof(int32), true,
		(YbcPgDatumToData) YbDatumToInt32,
	(YbcPgDatumFromData) YbInt32ToDatum},

	{ANYENUMOID, YB_YQL_DATA_TYPE_INT64, true, sizeof(int64), false,
		(YbcPgDatumToData) YbDatumToEnum,
	(YbcPgDatumFromData) YbEnumToDatum},

	{FDW_HANDLEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{INDEX_AM_HANDLEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{TSM_HANDLEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{TABLE_AM_HANDLEROID, YB_YQL_DATA_TYPE_UINT32, true, sizeof(Oid), true,
		(YbcPgDatumToData) YbDatumToOid,
	(YbcPgDatumFromData) YbOidToDatum},

	{ANYRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{ANYCOMPATIBLEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{ANYCOMPATIBLEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{ANYCOMPATIBLENONARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{ANYCOMPATIBLERANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{ANYMULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{ANYCOMPATIBLEMULTIRANGEOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{PG_BRIN_BLOOM_SUMMARYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_BRIN_MINMAX_MULTI_SUMMARYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{BOOLARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{BYTEAARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{CHARARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{NAMEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT8ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT2ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT2VECTORARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT4ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGPROCARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TEXTARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{OIDARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TIDARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{XIDARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{CIDARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{OIDVECTORARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_TYPEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_ATTRIBUTEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_PROCARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_CLASSARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{JSONARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{XMLARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{XID8ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{POINTARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{LSEGARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PATHARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{BOXARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{POLYGONARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{LINEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{FLOAT4ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{FLOAT8ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{CIRCLEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{MONEYARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToDocdb,
	(YbcPgDatumFromData) YbDocdbToDatum},

	{MACADDRARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INETARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{CIDRARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{MACADDR8ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{ACLITEMARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{BPCHARARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{VARCHARARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{DATEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TIMEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TIMESTAMPARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TIMESTAMPTZARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INTERVALARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TIMETZARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{BITARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{VARBITARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{NUMERICARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REFCURSORARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGPROCEDUREARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGOPERARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGOPERATORARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGCLASSARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGCOLLATIONARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGTYPEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGROLEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGNAMESPACEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{UUIDARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_LSNARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSVECTORARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{GTSVECTORARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSQUERYARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGCONFIGARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{REGDICTIONARYARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{JSONBARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{JSONPATHARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TXID_SNAPSHOTARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{PG_SNAPSHOTARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT4RANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{NUMRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSTZRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{DATERANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT8RANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT4MULTIRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{NUMMULTIRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSMULTIRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{TSTZMULTIRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{DATEMULTIRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{INT8MULTIRANGEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{CSTRINGARRAYOID, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{VECTOROID, YB_YQL_DATA_TYPE_VECTOR, false, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum},

	{BSONOID, YB_YQL_DATA_TYPE_BSON, true, -1, false,
		(YbcPgDatumToData) YbDatumToBinary,
	(YbcPgDatumFromData) YbBinaryToDatum}
};

/*
 * Special type entity used for fixed-length, pass-by-value user-defined types.
 * TODO(jason): When user-defined types as primary keys are supported, change
 * the below `false` to `true`.
 */
static const YbcPgTypeEntity YBCFixedLenByValTypeEntity =
{InvalidOid, YB_YQL_DATA_TYPE_INT64, false, sizeof(int64), true,
	(YbcPgDatumToData) YbDatumToInt64,
(YbcPgDatumFromData) YbInt64ToDatum};

/*
 * Special type entity used for null-terminated, pass-by-reference user-defined
 * types.
 * TODO(jason): When user-defined types as primary keys are supported, change
 * the below `false` to `true`.
 */
static const YbcPgTypeEntity YBCNullTermByRefTypeEntity =
{InvalidOid, YB_YQL_DATA_TYPE_BINARY, false, -2, false,
	(YbcPgDatumToData) YbDatumToCStr,
(YbcPgDatumFromData) YbCStrToDatum};

/*
 * Special type entity used for variable-length, pass-by-reference user-defined
 * types.
 * TODO(jason): When user-defined types as primary keys are supported, change
 * the below `false` to `true`.
 */
static const YbcPgTypeEntity YBCVarLenByRefTypeEntity =
{InvalidOid, YB_YQL_DATA_TYPE_BINARY, false, -1, false,
	(YbcPgDatumToData) YbDatumToBinary,
(YbcPgDatumFromData) YbBinaryToDatum};

/*
 * Special type entity used for ybgin null categories.
 */
const YbcPgTypeEntity YBCGinNullTypeEntity =
{InvalidOid, YB_YQL_DATA_TYPE_GIN_NULL, true, -1, true,
	(YbcPgDatumToData) YbDatumToGinNull,
(YbcPgDatumFromData) YbGinNullToDatum};

YbcPgTypeEntities
YbGetTypeTable()
{
	return (YbcPgTypeEntities)
	{
		.data = YbTypeEntityTable,
			.count = lengthof(YbTypeEntityTable),
	};
}

int64_t
YbUnixEpochToPostgresEpoch(int64_t unix_t)
{
	return unix_t - ((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * USECS_PER_DAY);
}

bool
YbTypeDetails(Oid elmtype, int16_t *elmlen, bool *elmbyval, char *elmalign)
{
	switch (elmtype)
	{
		case TEXTOID:
		case XMLOID:
		case BYTEAOID:
		case INT2VECTOROID:
		case OIDVECTOROID:
		case BPCHAROID:
		case VARCHAROID:
		case INETOID:
		case NUMERICOID:
		case BITOID:
		case VARBITOID:
		case TSVECTOROID:
		case GTSVECTOROID:
		case TSQUERYOID:
		case JSONBOID:
			*elmlen = -1;
			*elmbyval = false;
			*elmalign = 'i';
			break;
		case PATHOID:
		case RECORDOID:
		case TXID_SNAPSHOTOID:
			*elmlen = -1;
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case CSTRINGOID:
			*elmlen = -2;
			*elmbyval = false;
			*elmalign = 'c';
			break;
		case NAMEOID:
			*elmlen = NAMEDATALEN;
			*elmbyval = false;
			*elmalign = 'c';
			break;
		case UUIDOID:
			*elmlen = UUID_LEN;
			*elmbyval = false;
			*elmalign = 'c';
			break;
		case INTERVALOID:
			*elmlen = sizeof(Interval);
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case BOXOID:
			*elmlen = sizeof(BOX);
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case CIRCLEOID:
			*elmlen = sizeof(CIRCLE);
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case LINEOID:
			*elmlen = sizeof(LINE);
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case LSEGOID:
			*elmlen = sizeof(LSEG);
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case POINTOID:
			*elmlen = sizeof(Point);
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case MACADDR8OID:
			*elmlen = sizeof(macaddr8);
			*elmbyval = false;
			*elmalign = 'i';
			break;
		case MACADDROID:
			*elmlen = sizeof(macaddr);
			*elmbyval = false;
			*elmalign = 'i';
			break;
		case REGPROCOID:
		case OIDOID:
		case REGPROCEDUREOID:
		case REGOPEROID:
		case REGOPERATOROID:
		case REGCLASSOID:
		case REGTYPEOID:
		case REGROLEOID:
		case REGNAMESPACEOID:
		case REGCONFIGOID:
		case REGDICTIONARYOID:
			*elmlen = sizeof(Oid);
			*elmbyval = true;
			*elmalign = 'i';
			break;
		case TIDOID:
			*elmlen = sizeof(ItemPointerData);
			*elmbyval = false;
			*elmalign = 's';
			break;
		case XIDOID:
			*elmlen = sizeof(TransactionId);
			*elmbyval = true;
			*elmalign = 'i';
			break;
		case CIDOID:
			*elmlen = sizeof(CommandId);
			*elmbyval = true;
			*elmalign = 'i';
			break;
		case ACLITEMOID:
			*elmlen = sizeof(AclItem);
			*elmbyval = false;
			*elmalign = 'i';
			break;
		case TIMETZOID:
			*elmlen = 12;		/* sizeof(TimeTzADT) gives 16 */
			*elmbyval = false;
			*elmalign = 'd';
			break;
		case CASHOID:
		case INT8OID:
		case TIMESTAMPOID:
		case TIMEOID:
		case TIMESTAMPTZOID:
		case LSNOID:
			*elmlen = sizeof(int64);
			*elmbyval = true;
			*elmalign = 'd';
			break;
		case INT4OID:
		case FLOAT4OID:
		case DATEOID:
		case ANYOID:
			*elmlen = sizeof(int32);
			*elmbyval = true;
			*elmalign = 'i';
			break;
		case INT2OID:
			*elmlen = sizeof(int16);
			*elmbyval = true;
			*elmalign = 's';
			break;
		case BOOLOID:
		case CHAROID:
			*elmlen = sizeof(char);
			*elmbyval = true;
			*elmalign = 'c';
			break;
		case FLOAT8OID:
			*elmlen = 8;
			*elmbyval = FLOAT8PASSBYVAL;
			*elmalign = 'd';
			break;
			/* TODO: Extend support to other types as well. */
		default:
			return false;
	}
	return true;
}

/*
 * This function creates an ARRAY datum from the given list of items and returns
 * a palloc'd ARRAY. Note that item values will be copied into the ARRAY, even
 * if pass-by-ref type.
 */
void
YbConstructArrayDatum(Oid arraytypoid, const char **items, const int nelems,
					  char **datum, size_t *len)
{
	Oid			elemtypoid;
	ArrayType  *array;
	Datum	   *elems = NULL;
	int16		elmlen;
	bool		elmbyval;
	char		elmalign;

	elemtypoid = get_element_type(arraytypoid);

	if (!OidIsValid(elemtypoid))
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("subscripted object is not an array")));

	if (nelems > 0)
	{
		elems = (Datum *) palloc(nelems * sizeof(Datum));
		for (int i = 0; i < nelems; i++)
		{
			switch (arraytypoid)
			{
				case TEXTARRAYOID:
					elems[i] = CStringGetTextDatum(items[i]);
					break;
				case UUIDARRAYOID:
					elems[i] = UUIDPGetDatum(items[i]);
					break;
				default:
					elog(ERROR, "unsupported array type: %d", arraytypoid);
			}
		}
	}

	get_typlenbyvalalign(elemtypoid, &elmlen, &elmbyval, &elmalign);

	array = construct_array(elems, nelems, elemtypoid, elmlen, elmbyval, elmalign);

	*datum = VARDATA_ANY(array);
	*len = VARSIZE_ANY_EXHDR(array);
}
