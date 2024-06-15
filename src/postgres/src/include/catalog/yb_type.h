/*--------------------------------------------------------------------------------------------------
 *
 * yb_type.h
 *	  prototypes for yb_type.c.
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
 * src/include/catalog/yb_type.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#ifndef YB_TYPE_H
#define YB_TYPE_H

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

/*
 * Special type entity used for ybgin null categories.
 */
extern const YBCPgTypeEntity YBCGinNullTypeEntity;

extern const YBCPgTypeEntity *YbDataTypeFromOidMod(int attnum, Oid type_id);

/*
 * For non-primitive types (the ones without a corresponding YBCPgTypeEntity),
 * returns the corresponding primitive type's oid.
 */
extern Oid YbGetPrimitiveTypeOid(Oid type_id, char typtype,
								 Oid typbasetype);

/*
 * Returns true if we are allow the given type to be used for key columns such as primary key or
 * indexing key.
 */
bool YbDataTypeIsValidForKey(Oid type_id);

/*
 * Array of all type entities.
 */
void YbGetTypeTable(const YBCPgTypeEntity **type_table, int *count);

/*
 * Callback functions
 */
int64_t YbUnixEpochToPostgresEpoch(int64_t unix_t);
bool YbTypeDetails(Oid elmtype, int *elmlen, bool *elmbyval, char *elmalign);
void YbConstructArrayDatum(Oid arraytypoid, const char **items,
						   const int nelems, char **datum, size_t *len);
#endif
