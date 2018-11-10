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

/*
 * TODO For now we use the CQL/YQL types here (listed in common.proto).
 * Eventually we should use the internal (protobuf) types listed in
 * yb/client/schema.h.
 */
YBCPgDataType
YBCDataTypeFromName(TypeName *typeName)
{
	Oid			type_id = 0;
	int32		typmod = 0;

	typenameTypeIdAndMod(NULL /* parseState */ , typeName, &type_id, &typmod);

	if (YBCIsPgBinarySerializedType(type_id)) {
		/*
		 * TODO: distinguish between string and binary types in the backend.
		 */
		return YB_YQL_DATA_TYPE_STRING;
	}

#define REPORT_INVALID_TYPE_AND_BREAK() \
	YB_REPORT_TYPE_NOT_SUPPORTED(type_id); break

	switch (type_id)
	{
		case BOOLOID: return YB_YQL_DATA_TYPE_BOOL;
		case INT2OID: return YB_YQL_DATA_TYPE_INT16;
		case INT4OID: return YB_YQL_DATA_TYPE_INT32;
		case INT8OID: return YB_YQL_DATA_TYPE_INT64;
		case FLOAT4OID: return YB_YQL_DATA_TYPE_FLOAT;
		case FLOAT8OID: return YB_YQL_DATA_TYPE_DOUBLE;
		case OIDOID:
			/* TODO: need to use UINT32 here */
			return YB_YQL_DATA_TYPE_INT32;
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
			/** TODO: should probably be UINT64 */
			return YB_YQL_DATA_TYPE_INT64;
		case INT4ARRAYOID:
			/** TODO: make this binary */
			return YB_YQL_DATA_TYPE_STRING;
		case REGPROCOID: REPORT_INVALID_TYPE_AND_BREAK();
		case CHAROID: REPORT_INVALID_TYPE_AND_BREAK();
		case NAMEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case XIDOID: REPORT_INVALID_TYPE_AND_BREAK();
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
		case REGPROCEDUREOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGOPEROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGOPERATOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGCLASSOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGTYPEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGROLEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGNAMESPACEOID: REPORT_INVALID_TYPE_AND_BREAK();
		case UUIDOID: REPORT_INVALID_TYPE_AND_BREAK();
		case LSNOID: REPORT_INVALID_TYPE_AND_BREAK();
		case TSVECTOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case GTSVECTOROID: REPORT_INVALID_TYPE_AND_BREAK();
		case TSQUERYOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGCONFIGOID: REPORT_INVALID_TYPE_AND_BREAK();
		case REGDICTIONARYOID: REPORT_INVALID_TYPE_AND_BREAK();
		case INT4RANGEOID: REPORT_INVALID_TYPE_AND_BREAK();
		default: REPORT_INVALID_TYPE_AND_BREAK();
	}
#undef REPORT_INVALID_TYPE_AND_BREAK
	return -1;
}

bool
YBCIsPgBinarySerializedType(Oid type_id) {
	switch (type_id) {
		case BYTEAOID:
		case INT2ARRAYOID:
		case INT2VECTOROID:
		case JSONBOID:
		case OIDVECTOROID:
		case TEXTOID:
		case VARCHAROID:
		case REGTYPEARRAYOID:
		case BPCHAROID:
			return true;
		default:
			return false;
	}
}
