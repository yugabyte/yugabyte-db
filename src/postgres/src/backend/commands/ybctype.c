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

	switch (type_id)
	{
		case BOOLOID:
			return YB_YQL_DATA_TYPE_BOOL;
		case CHAROID:
		case NAMEOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
			break;
		case INT8OID:
			return YB_YQL_DATA_TYPE_INT64;
		case INT2OID:
			return YB_YQL_DATA_TYPE_INT16;
		case INT4OID:
			return YB_YQL_DATA_TYPE_INT32;
		case REGPROCOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
			break;
		case OIDOID:
			/* TODO: need to use UINT32 here */
			return YB_YQL_DATA_TYPE_INT32;
		case TIDOID:
		case XIDOID:
		case CIDOID:
		case POINTOID:
		case LSEGOID:
		case PATHOID:
		case BOXOID:
		case POLYGONOID:
		case LINEOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
			break;
		case FLOAT4OID:
			return YB_YQL_DATA_TYPE_FLOAT;
		case FLOAT8OID:
			return YB_YQL_DATA_TYPE_DOUBLE;
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
		case TIMESTAMPOID:
		case TIMESTAMPTZOID:
		case INTERVALOID:
		case TIMETZOID:
		case VARBITOID:
		case NUMERICOID:
		case REFCURSOROID:
		case REGPROCEDUREOID:
		case REGOPEROID:
		case REGOPERATOROID:
		case REGCLASSOID:
		case REGTYPEOID:
		case REGROLEOID:
		case REGNAMESPACEOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
			break;
		case UUIDOID:
		case LSNOID:
		case TSVECTOROID:
		case GTSVECTOROID:
		case TSQUERYOID:
		case REGCONFIGOID:
		case REGDICTIONARYOID:
		case INT4RANGEOID:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
		case INT4ARRAYOID:
			/** TODO: make this binary */
			return YB_YQL_DATA_TYPE_STRING;
		default:
			YB_REPORT_TYPE_NOT_SUPPORTED(type_id);
	}
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
