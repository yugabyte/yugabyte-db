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
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/pg_type.h"
#include "commands/ybctype.h"
#include "utils/syscache.h"
#include "parser/parse_type.h"
#include "access/htup_details.h"
#include "parser/parse_type.h"

#include "yb/yql/pggate/ybc_pggate.h"

#include "pg_yb_utils.h"

/**
 * Types we need for system tables:
 *
 * bool
 * char
 * text
 * int2
 * int4
 * int8
 * float4
 * float8
 * timestamptz
 * bytea
 * oid
 * xid
 * cid
 * tid
 * name (same as text?)
 * aclitem
 * pg_node_tree
 * pg_lsn
 * pg_ndistinct
 * pg_dependencies
 *
 * OID aliases:
 *
 * regproc
 * regprocedure
 * regoper
 * regoperator
 * regclass
 * regtype
 * regconfig
 * regdictionary
 *
 * Vectors/arrays:
 *
 * int2vector (list of 16-bit integers)
 * oidvector (list of 32-bit unsigned integers)
 * anyarray (list of 32-bit integers - signed or unsigned)
 */

YBCPgDataType
YBCDataTypeFromName(TypeName *typeName)
{
	Oid   type_id = 0;
	int32 typmod  = 0;

	typenameTypeIdAndMod(NULL /* parseState */ , typeName, &type_id, &typmod);
	return YBCDataTypeFromOidMod(type_id, typmod);
}

YBCPgDataType
YBCDataTypeFromOidMod(Oid type_id, int32 typmod)
{
	if (YBCIsPgBinarySerializedType(type_id)) {
		return YB_YQL_DATA_TYPE_BINARY;
	}

#define REPORT_INVALID_TYPE_AND_BREAK() \
	YB_REPORT_TYPE_NOT_SUPPORTED(type_id); break

	switch (type_id)
	{
		case BOOLOID: return YB_YQL_DATA_TYPE_BOOL;
		case INT2OID: return YB_YQL_DATA_TYPE_INT16;
		case XIDOID: /* fallthrough */
		case INT4OID: return YB_YQL_DATA_TYPE_INT32;
		case INT8OID: return YB_YQL_DATA_TYPE_INT64;
		case FLOAT4OID: return YB_YQL_DATA_TYPE_FLOAT;
		case FLOAT8OID: return YB_YQL_DATA_TYPE_DOUBLE;
		case REGPROCOID: /* fallthrough: Alias for OID */
		case REGPROCEDUREOID: /* fallthrough: Alias for OID */
		case OIDOID:
			/* TODO: need to use UINT32 here */
			return YB_YQL_DATA_TYPE_INT32;
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
			/** TODO: should probably be UINT64 */
			return YB_YQL_DATA_TYPE_INT64;
		case INT4ARRAYOID:
			return YB_YQL_DATA_TYPE_BINARY;
		case NAMEOID:
		case CSTRINGOID:
			return YB_YQL_DATA_TYPE_STRING;
		case CHAROID:
			return YB_YQL_DATA_TYPE_INT8;
		case TIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case POINTOID: REPORT_INVALID_TYPE_AND_BREAK();
		case LSEGOID: REPORT_INVALID_TYPE_AND_BREAK();
		case PATHOID: REPORT_INVALID_TYPE_AND_BREAK();
		case BOXOID: REPORT_INVALID_TYPE_AND_BREAK();
		case POLYGONOID: REPORT_INVALID_TYPE_AND_BREAK();
		case LINEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case ABSTIMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case RELTIMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TINTERVALOID: REPORT_INVALID_TYPE_AND_BREAK();
		case UNKNOWNOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CIRCLEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CASHOID: REPORT_INVALID_TYPE_AND_BREAK();
		case INETOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CIDROID: REPORT_INVALID_TYPE_AND_BREAK();
		case DATEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TIMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case INTERVALOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TIMETZOID: REPORT_INVALID_TYPE_AND_BREAK();
		case VARBITOID: REPORT_INVALID_TYPE_AND_BREAK();
		case NUMERICOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REFCURSOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGOPEROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGOPERATOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGCLASSOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGTYPEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGROLEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGNAMESPACEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case UUIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TSVECTOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case GTSVECTOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case TSQUERYOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGCONFIGOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGDICTIONARYOID: REPORT_INVALID_TYPE_AND_BREAK();
		case INT4RANGEOID: REPORT_INVALID_TYPE_AND_BREAK();
		default:
		{
			HeapTuple type = typeidType(type_id);
			Form_pg_type tp = (Form_pg_type) GETSTRUCT(type);
			Oid basetp_oid = tp->typbasetype;
			int32 basetp_mod = tp->typtypmod;
			ReleaseSysCache(type);

			if (basetp_oid != InvalidOid)
			{
				return YBCDataTypeFromOidMod(basetp_oid, basetp_mod);
			}
			REPORT_INVALID_TYPE_AND_BREAK();
		}
	}
#undef REPORT_INVALID_TYPE_AND_BREAK
	return -1;
}

bool
YBCIsPgBinarySerializedType(Oid type_id)
{
	switch (type_id)
	{
		case ANYARRAYOID:
		case BYTEAOID:
		case BPCHAROID:
		case FLOAT4ARRAYOID:
		case INT2ARRAYOID:
		case INT2VECTOROID:
		case JSONBOID:
		case LSNOID:
		case OIDARRAYOID:
		case OIDVECTOROID:
		case PGNODETREEOID:
		case PGNDISTINCTOID:
		case PGDEPENDENCIESOID:
		case REGTYPEARRAYOID:
		case TEXTOID:
		case VARCHAROID:
		case YB_CHARARRAYOID:
		case YB_TEXTARRAYOID:
		case YB_ACLITEMARRAYOID:
			return true;
		default:
			return false;
	}
}

/*
 * TODO(Alex) Turn ON or OFF certain type for KEY when testing its support.
 */
bool
YBCDataTypeIsValidForKey(Oid type_id)
{
	switch (type_id)
	{
		case BOOLOID:
		case INT2OID:
		case XIDOID:
		case INT4OID:
		case INT8OID:
		case FLOAT4OID:
		case FLOAT8OID:
		case REGPROCOID:
		case REGPROCEDUREOID:
		case OIDOID:
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
		case NAMEOID:
		case CSTRINGOID:
		case CHAROID:
		case BYTEAOID:
		case TEXTOID:
		case VARCHAROID:
		case INT2VECTOROID:
		case OIDVECTOROID:
		case BPCHAROID:
			return true;

		default:
			return false;

		/* If certain datatype can be used in a PRIMARY or INDEX KEY, move it from the following list
		 * to the "true" switch block.
		 *
			case INT4ARRAYOID:
			case ANYARRAYOID:
			case FLOAT4ARRAYOID:
			case INT2ARRAYOID:
			case JSONBOID:
			case LSNOID:
			case OIDARRAYOID:
			case PGNODETREEOID:
			case PGNDISTINCTOID:
			case PGDEPENDENCIESOID:
			case REGTYPEARRAYOID:
			case YB_CHARARRAYOID:
			case YB_TEXTARRAYOID:
			case YB_ACLITEMARRAYOID:
			case TIDOID:
			case CIDOID:
			case POINTOID:
			case LSEGOID:
			case PATHOID:
			case BOXOID:
			case POLYGONOID:
			case LINEOID:
			case ABSTIMEOID:
			case RELTIMEOID:
			case TINTERVALOID:
			case UNKNOWNOID:
			case CIRCLEOID:
			case CASHOID:
			case INETOID:
			case CIDROID:
			case DATEOID:
			case TIMEOID:
			case INTERVALOID:
			case TIMETZOID:
			case VARBITOID:
			case NUMERICOID:
			case REFCURSOROID:
			case REGOPEROID:
			case REGOPERATOROID:
			case REGCLASSOID:
			case REGTYPEOID:
			case REGROLEOID:
			case REGNAMESPACEOID:
			case UUIDOID:
			case TSVECTOROID:
			case GTSVECTOROID:
			case TSQUERYOID:
			case REGCONFIGOID:
			case REGDICTIONARYOID:
			case INT4RANGEOID:
		*/
	}
}
