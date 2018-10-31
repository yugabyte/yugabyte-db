/*--------------------------------------------------------------------------------------------------
 *
 * ybctype.h
 *	  prototypes for ybctype.c.
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
 * src/include/catalog/ybctype.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#ifndef YBCTYPE_H
#define YBCTYPE_H

#include "access/htup.h"
#include "catalog/dependency.h"
#include "nodes/parsenodes.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

/*
 * Constants for OIDs of supported Postgres native types (that do not have an
 * already declared constant in Postgres).
 */
#define YB_CHARARRAYOID 1002 /* char[] */
#define YB_TEXTARRAYOID 1009 /* text[] */
#define YB_ACLITEMARRAYOID 1034 /* aclitem[] */

extern YBCPgDataType YBCDataTypeFromName(TypeName *typeName);
extern YBCPgDataType YBCDataTypeFromOidMod(Oid type_id, int32 typmod);

/**
 * Returns true if we are using PostgreSQL's own binary serialization for the given type, as given
 * by VARDATA_ANY and VARSIZE_ANY_EXHDR.
 */
extern bool YBCIsPgBinarySerializedType(Oid type_id);

#endif
