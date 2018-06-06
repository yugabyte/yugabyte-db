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
 * http: *www.apache.org/licenses/LICENSE-2.0
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

/*
 * TODO For now we use the CQL/YQL types here, eventually we should use the
 * internal (protobuf) types.
 */
YBCPgDataType
YBCDataTypeFromName(TypeName *typeName)
{
	Oid			typeId;
	int32		typmod;

	typenameTypeIdAndMod(NULL /* parseState */ , typeName, &typeId, &typmod);

	if (typmod != -1)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Type modifiers are not supported yet: %d", typmod)));
	}

	switch (typeId)
	{
		case INT2OID:
			/* INT16 */
			return 2;
		case INT4OID:
			/* INT32 */
			return 3;
		case INT8OID:
			/* INT64 */
			return 4;
		case FLOAT4OID:
			/* FLOAT */
			return 7;
		case FLOAT8OID:
			/* DOUBLE */
			return 8;
		case TEXTOID:
			/* STRING */
			return 5;

		default:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("Type not yet supported: %d", typeName->typeOid)));
	}
	return -1;
}
