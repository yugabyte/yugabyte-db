/*--------------------------------------------------------------------------------------------------
 *
 * ybctype.c
 *        Commands for creating and altering table structures and settings
 *
 * Copyright (c) YugaByte, Inc.
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
 *        src/backend/catalog/ybctype.c
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
#include "catalog/pg_type.h"
#include "catalog/ybctype.h"
#include "mb/pg_wchar.h"
#include "parser/parse_type.h"
#include "utils/date.h"
#include "utils/builtins.h"
#include "utils/cash.h"
#include "utils/syscache.h"
#include "utils/numeric.h"
#include "utils/uuid.h"

#include "yb/yql/pggate/ybc_pggate.h"

#include "pg_yb_utils.h"

/***************************************************************************************************
 * Find YugaByte storage type for each PostgreSQL datatype.
 * NOTE: Because YugaByte network buffer can be deleted after it is processed, Postgres layer must
 *       allocate a buffer to keep the data in its slot.
 **************************************************************************************************/
const YBCPgTypeEntity *
YBCDataTypeFromOidMod(int attnum, Oid type_id)
{
	/* Find type for system column */
	if (attnum < InvalidAttrNumber) {
		if (attnum < FirstLowInvalidHeapAttributeNumber) {
			/* YugaByte system columns */
			type_id = BYTEAOID;
		} else if (attnum == SelfItemPointerAttributeNumber) {
			/* ctid column */
			type_id = INT8OID;
		} else  {
			/* Other postgres system columns */
			type_id = INT4OID;
		}
	}

	/* Find the type mapping entry */
	const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(type_id);
	YBCPgDataType yb_type = YBCPgGetType(type_entity);

	/* Find the basetype if the actual type does not have any entry */
	if (yb_type == YB_YQL_DATA_TYPE_UNKNOWN_DATA) {
		HeapTuple type = typeidType(type_id);
		Form_pg_type tp = (Form_pg_type) GETSTRUCT(type);
		Oid basetp_oid = tp->typbasetype;
		ReleaseSysCache(type);

		if (basetp_oid == InvalidOid)
		{
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
		}
		return YBCDataTypeFromOidMod(InvalidAttrNumber, basetp_oid);
	}

	/* Report error if type is not supported */
	if (yb_type == YB_YQL_DATA_TYPE_NOT_SUPPORTED) {
		YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
	}

	/* Return the type-mapping entry */
	return type_entity;
}

bool
YBCDataTypeIsValidForKey(Oid type_id)
{
	const YBCPgTypeEntity *type_entity = YBCDataTypeFromOidMod(InvalidAttrNumber, type_id);
	return YBCPgAllowForPrimaryKey(type_entity);
}

const YBCPgTypeEntity *
YBCDataTypeFromName(TypeName *typeName)
{
	Oid   type_id = 0;
	int32 typmod  = 0;

	typenameTypeIdAndMod(NULL /* parseState */ , typeName, &type_id, &typmod);
	return YBCDataTypeFromOidMod(InvalidAttrNumber, type_id);
}

/***************************************************************************************************
 * Conversion Functions.
 **************************************************************************************************/
/*
 * BOOL conversion.
 * Fixed size: Ignore the "bytes" data size.
 */
void YBCDatumToBool(Datum datum, bool *data, int64 *bytes) {
	*data = DatumGetBool(datum);
}

Datum YBCBoolToDatum(const bool *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return BoolGetDatum(*data);
}

/*
 * BINARY conversion.
 */
void YBCDatumToBinary(Datum datum, void **data, int64 *bytes) {
	*data = VARDATA_ANY(datum);
	*bytes = VARSIZE_ANY_EXHDR(datum);
}

Datum YBCBinaryToDatum(const void *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
  /* PostgreSQL can represent text strings up to 1 GB minus a four-byte header. */
  if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0) {
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
										errmsg("Invalid data size")));
	}
	return PointerGetDatum(cstring_to_text_with_len(data, bytes));
}

/*
 * CHAR type conversion.
 */
void YBCDatumToChar(Datum datum, char *data, int64 *bytes) {
	*data = DatumGetChar(datum);
}

Datum YBCCharToDatum(const char *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return CharGetDatum(*data);
}

/*
 * CHAR-based type conversion.
 */
void YBCDatumToBPChar(Datum datum, char **data, int64 *bytes) {
	int size;
	*data = TextDatumGetCString(datum);

	/*
	 * Right trim all spaces on the right. For CHAR(n) - BPCHAR - datatype, Postgres treats space
	 * characters at tail-end the same as '\0' characters.
	 *   "abc  " == "abc"
	 * Left spaces don't have this special behaviors.
	 *   "  abc" != "abc"
	 */
	size = strlen(*data);
	while (size > 0 && isspace((*data)[size - 1])) {
		size--;
	}
	*bytes = size;
}

Datum YBCBPCharToDatum(const char *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
  /* PostgreSQL can represent text strings up to 1 GB minus a four-byte header. */
  if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0) {
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
										errmsg("Invalid data size")));
	}

	/* Convert YugaByte cstring to Postgres internal representation */
	FunctionCallInfoData fargs;
	FunctionCallInfo fcinfo = &fargs;
	PG_GETARG_DATUM(0) = CStringGetDatum(data);
	PG_GETARG_DATUM(2) = Int32GetDatum(type_attrs->typmod);
	return bpcharin(fcinfo);
}

void YBCDatumToVarchar(Datum datum, char **data, int64 *bytes) {
	*data = TextDatumGetCString(datum);
	*bytes = strlen(*data);
}

Datum YBCVarcharToDatum(const char *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
  /* PostgreSQL can represent text strings up to 1 GB minus a four-byte header. */
  if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0) {
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
										errmsg("Invalid data size")));
	}

	/* Convert YugaByte cstring to Postgres internal representation */
	FunctionCallInfoData fargs;
	FunctionCallInfo fcinfo = &fargs;
	PG_GETARG_DATUM(0) = CStringGetDatum(data);
	PG_GETARG_DATUM(2) = Int32GetDatum(type_attrs->typmod);
	return varcharin(fcinfo);
}

/*
 * NAME conversion.
 */
void YBCDatumToName(Datum datum, char **data, int64 *bytes) {
	*data = DatumGetCString(datum);
	*bytes = strlen(*data);
}

Datum YBCNameToDatum(const char *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
  /* PostgreSQL can represent text strings up to 1 GB minus a four-byte header. */
  if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0) {
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
						errmsg("Invalid data size")));
	}

	/* Truncate oversize input */
	if (bytes >= NAMEDATALEN)
		bytes = pg_mbcliplen(data, bytes, NAMEDATALEN - 1);

	/* We use palloc0 here to ensure result is zero-padded */
	Name result = (Name)palloc0(NAMEDATALEN);
	memcpy(NameStr(*result), data, bytes);
	return NameGetDatum(result);
}

/*
 * PSEUDO-type cstring conversion.
 * Not a type that is used by users.
 */
void YBCDatumToCStr(Datum datum, char **data, int64 *bytes) {
	*data = DatumGetCString(datum);
	*bytes = strlen(*data);
}

Datum YBCCStrToDatum(const char *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
  /* PostgreSQL can represent text strings up to 1 GB minus a four-byte header. */
  if (bytes > kYBCMaxPostgresTextSizeBytes || bytes < 0) {
		ereport(ERROR, (errcode(ERRCODE_STRING_DATA_RIGHT_TRUNCATION),
						errmsg("Invalid data size")));
	}

	/* Convert YugaByte cstring to Postgres internal representation */
	FunctionCallInfoData fargs;
	FunctionCallInfo fcinfo = &fargs;
	PG_GETARG_DATUM(0) = CStringGetDatum(data);
	return cstring_in(fcinfo);
}

/*
 * INTEGERs conversion.
 * Fixed size: Ignore the "bytes" data size.
 */
void YBCDatumToInt16(Datum datum, int16 *data, int64 *bytes) {
	*data = DatumGetInt16(datum);
}

Datum YBCInt16ToDatum(const int16 *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return Int16GetDatum(*data);
}

void YBCDatumToInt32(Datum datum, int32 *data, int64 *bytes) {
	*data = DatumGetInt32(datum);
}

Datum YBCInt32ToDatum(const int32 *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return Int32GetDatum(*data);
}

void YBCDatumToInt64(Datum datum, int64 *data, int64 *bytes) {
	*data = DatumGetInt64(datum);
}

Datum YBCInt64ToDatum(const int64 *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return Int64GetDatum(*data);
}

/*
 * FLOATs conversion.
 * Fixed size: Ignore the "bytes" data size.
 */
void YBCDatumToFloat4(Datum datum, float *data, int64 *bytes) {
	*data = DatumGetFloat4(datum);
}

Datum YBCFloat4ToDatum(const float *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return Float4GetDatum(*data);
}

void YBCDatumToFloat8(Datum datum, double *data, int64 *bytes) {
	*data = DatumGetFloat8(datum);
}

Datum YBCFloat8ToDatum(const double *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return Float8GetDatum(*data);
}

/*
 * DECIMAL / NUMERIC conversion.
 * We're using plaintext c-string as an intermediate step between PG and YB numerics.
 */
void YBCDatumToDecimalText(Datum datum, char *plaintext[], int64 *bytes) {
	Numeric num = DatumGetNumeric(datum);
	*plaintext = numeric_normalize(num);
	// NaN support will be added in ENG-4645
	if (strncmp(*plaintext, "NaN", 3) == 0) {
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("DECIMAL does not support NaN yet")));
	}
}

Datum YBCDecimalTextToDatum(const char plaintext[], int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	FunctionCallInfoData fargs;
	FunctionCallInfo fcinfo = &fargs;
	PG_GETARG_DATUM(0) = CStringGetDatum(plaintext);
	PG_GETARG_DATUM(2) = Int32GetDatum(type_attrs->typmod);
	return numeric_in(fcinfo);
}

/*
 * MONEY conversion.
 * We're using int64 as a representation, just like Postgres does.
 */
void YBCDatumToMoneyInt64(Datum datum, int64 *data, int64 *bytes) {
	*data = DatumGetCash(datum);
}

Datum YBCMoneyInt64ToDatum(const int64 *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return CashGetDatum(*data);
}

/*
 * UUID Datatype.
 */
void YBCDatumToUuid(Datum datum, unsigned char **data, int64 *bytes) {
	// Postgres store uuid as hex string.
	*data = (DatumGetUUIDP(datum))->data;
	*bytes = UUID_LEN;
}

Datum YBCUuidToDatum(const unsigned char *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	// We have to make a copy for data because the "data" pointer belongs to YugaByte cache memory
	// which can be cleared at any time.
	pg_uuid_t *uuid;
	if (bytes != UUID_LEN) {
		ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
										errmsg("Unexpected size for UUID (%ld)", bytes)));
	}

	uuid = (pg_uuid_t *)palloc(sizeof(pg_uuid_t));
	memcpy(uuid->data, data, UUID_LEN);
	return UUIDPGetDatum(uuid);
}

/*
 * DATE conversions.
 * PG represents DATE as signed int32 number of days since 2000-01-01, we store it as-is
 */

void YBCDatumToDate(Datum datum, int32 *data, int64 *bytes) {
	*data = DatumGetDateADT(datum);
}

Datum YBCDateToDatum(const int32 *data, int64 bytes, const YBCPgTypeAttrs* type_attrs) {
	return DateADTGetDatum(*data);
}

/*
 * TIME conversions.
 * PG represents TIME as microseconds in int64, we store it as-is
 */

void YBCDatumToTime(Datum datum, int64 *data, int64 *bytes) {
	*data = DatumGetTimeADT(datum);
}

Datum YBCTimeToDatum(const int64 *data, int64 bytes, const YBCPgTypeAttrs *type_attrs) {
	return TimeADTGetDatum(*data);
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
static const YBCPgTypeEntity YBCTypeEntityTable[] = {
	{ BOOLOID, YB_YQL_DATA_TYPE_BOOL, true,
		(YBCPgDatumToData)YBCDatumToBool,
		(YBCPgDatumFromData)YBCBoolToDatum },

	{ BYTEAOID, YB_YQL_DATA_TYPE_BINARY, true,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ CHAROID, YB_YQL_DATA_TYPE_INT8, true,
		(YBCPgDatumToData)YBCDatumToChar,
		(YBCPgDatumFromData)YBCCharToDatum },

	{ NAMEOID, YB_YQL_DATA_TYPE_STRING, true,
		(YBCPgDatumToData)YBCDatumToName,
		(YBCPgDatumFromData)YBCNameToDatum },

	{ INT8OID, YB_YQL_DATA_TYPE_INT64, true,
		(YBCPgDatumToData)YBCDatumToInt64,
		(YBCPgDatumFromData)YBCInt64ToDatum },

	{ INT2OID, YB_YQL_DATA_TYPE_INT16, true,
		(YBCPgDatumToData)YBCDatumToInt16,
		(YBCPgDatumFromData)YBCInt16ToDatum },

	{ INT2VECTOROID, YB_YQL_DATA_TYPE_BINARY, true,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ INT4OID, YB_YQL_DATA_TYPE_INT32, true,
		(YBCPgDatumToData)YBCDatumToInt32,
		(YBCPgDatumFromData)YBCInt32ToDatum },

	{ REGPROCOID, YB_YQL_DATA_TYPE_INT32, true,
		(YBCPgDatumToData)YBCDatumToInt32,
		(YBCPgDatumFromData)YBCInt32ToDatum },

	/*
	 * TODO(neil) We need to change TEXT to char-based datatype to be the same with PostgreSQL.
	 */
	{ TEXTOID, YB_YQL_DATA_TYPE_BINARY, true,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ OIDOID, YB_YQL_DATA_TYPE_INT32, true,
		(YBCPgDatumToData)YBCDatumToInt32,
		(YBCPgDatumFromData)YBCInt32ToDatum },

	{ TIDOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ XIDOID, YB_YQL_DATA_TYPE_INT32, true,
		(YBCPgDatumToData)YBCDatumToInt32,
		(YBCPgDatumFromData)YBCInt32ToDatum },

	{ CIDOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ OIDVECTOROID, YB_YQL_DATA_TYPE_BINARY, true,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ JSONOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ XMLOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ PGNODETREEOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ PGNDISTINCTOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ PGDEPENDENCIESOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ PGDDLCOMMANDOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ POINTOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ LSEGOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ PATHOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ BOXOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ POLYGONOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ LINEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ FLOAT4OID, YB_YQL_DATA_TYPE_FLOAT, true,
		(YBCPgDatumToData)YBCDatumToFloat4,
		(YBCPgDatumFromData)YBCFloat4ToDatum },

	{ FLOAT8OID, YB_YQL_DATA_TYPE_DOUBLE, true,
		(YBCPgDatumToData)YBCDatumToFloat8,
		(YBCPgDatumFromData)YBCFloat8ToDatum },

	{ ABSTIMEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ RELTIMEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ TINTERVALOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ UNKNOWNOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ CIRCLEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	// We're using int64 to represent monetary type, just like Postgres does.
	{ CASHOID, YB_YQL_DATA_TYPE_INT64, true,
		(YBCPgDatumToData)YBCDatumToMoneyInt64,
		(YBCPgDatumFromData)YBCMoneyInt64ToDatum },

	{ MACADDROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ INETOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ CIDROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ MACADDR8OID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ YB_CHARARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ INT2ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ INT4ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ TEXTARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ OIDARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ FLOAT4ARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ ACLITEMOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ YB_ACLITEMARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ CSTRINGARRAYOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ BPCHAROID, YB_YQL_DATA_TYPE_STRING, true,
		(YBCPgDatumToData)YBCDatumToBPChar,
		(YBCPgDatumFromData)YBCBPCharToDatum },

	{ VARCHAROID, YB_YQL_DATA_TYPE_STRING, true,
		(YBCPgDatumToData)YBCDatumToVarchar,
		(YBCPgDatumFromData)YBCVarcharToDatum },

	{ DATEOID, YB_YQL_DATA_TYPE_INT32, true,
		(YBCPgDatumToData)YBCDatumToDate,
		(YBCPgDatumFromData)YBCDateToDatum },

	{ TIMEOID, YB_YQL_DATA_TYPE_INT64, true,
		(YBCPgDatumToData)YBCDatumToTime,
		(YBCPgDatumFromData)YBCTimeToDatum },

	{ TIMESTAMPOID, YB_YQL_DATA_TYPE_INT64, true,
		(YBCPgDatumToData)YBCDatumToInt64,
		(YBCPgDatumFromData)YBCInt64ToDatum },

	{ TIMESTAMPTZOID, YB_YQL_DATA_TYPE_INT64, true,
		(YBCPgDatumToData)YBCDatumToInt64,
		(YBCPgDatumFromData)YBCInt64ToDatum },

	{ INTERVALOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ TIMETZOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ BITOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ VARBITOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ NUMERICOID, YB_YQL_DATA_TYPE_DECIMAL, true,
		(YBCPgDatumToData)YBCDatumToDecimalText,
		(YBCPgDatumFromData)YBCDecimalTextToDatum },

	{ REFCURSOROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGPROCEDUREOID, YB_YQL_DATA_TYPE_INT32, true,
		(YBCPgDatumToData)YBCDatumToInt32,
		(YBCPgDatumFromData)YBCInt32ToDatum },

	{ REGOPEROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGOPERATOROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGCLASSOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGTYPEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGROLEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGNAMESPACEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGTYPEARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ UUIDOID, YB_YQL_DATA_TYPE_BINARY, true,
		(YBCPgDatumToData)YBCDatumToUuid,
		(YBCPgDatumFromData)YBCUuidToDatum },

	{ LSNOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ TSVECTOROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ GTSVECTOROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ TSQUERYOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGCONFIGOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ REGDICTIONARYOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ JSONBOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ INT4RANGEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ RECORDOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ RECORDARRAYOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ CSTRINGOID, YB_YQL_DATA_TYPE_STRING, true,
		(YBCPgDatumToData)YBCDatumToCStr,
		(YBCPgDatumFromData)YBCCStrToDatum },

	{ ANYOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ ANYARRAYOID, YB_YQL_DATA_TYPE_BINARY, false,
		(YBCPgDatumToData)YBCDatumToBinary,
		(YBCPgDatumFromData)YBCBinaryToDatum },

	{ VOIDOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ TRIGGEROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ EVTTRIGGEROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ LANGUAGE_HANDLEROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ INTERNALOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ OPAQUEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ ANYELEMENTOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ ANYNONARRAYOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ ANYENUMOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ FDW_HANDLEROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ INDEX_AM_HANDLEROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ TSM_HANDLEROID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },

	{ ANYRANGEOID, YB_YQL_DATA_TYPE_NOT_SUPPORTED, false,
		(YBCPgDatumToData)NULL,
		(YBCPgDatumFromData)NULL },
};

void YBCGetTypeTable(const YBCPgTypeEntity **type_table, int *count) {
	*type_table = YBCTypeEntityTable;
	*count = sizeof(YBCTypeEntityTable)/sizeof(YBCPgTypeEntity);
}
