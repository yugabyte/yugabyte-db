/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef AG_GRAPHID_H
#define AG_GRAPHID_H

#include "utils/fmgroids.h"
#include "utils/syscache.h"

#include "catalog/ag_namespace.h"
#include "catalog/pg_type.h"

typedef int64 graphid;

#define F_GRAPHIDEQ F_INT8EQ

#define LABEL_ID_MIN 1
#define LABEL_ID_MAX PG_UINT16_MAX
#define INVALID_LABEL_ID 0

#define label_id_is_valid(id) (id >= LABEL_ID_MIN && id <= LABEL_ID_MAX)

#define ENTRY_ID_MIN INT64CONST(0)
/* 0x0000ffffffffffff */
#define ENTRY_ID_MAX INT64CONST(281474976710655)
#define INVALID_ENTRY_ID INT64CONST(0)

#define entry_id_is_valid(id) (id >= ENTRY_ID_MIN && id <= ENTRY_ID_MAX)

#define ENTRY_ID_BITS (32 + 16)
#define ENTRY_ID_MASK INT64CONST(0x0000ffffffffffff)

#define DATUM_GET_GRAPHID(d) DatumGetInt64(d)
#define GRAPHID_GET_DATUM(x) Int64GetDatum(x)

#define AG_GETARG_GRAPHID(a) DATUM_GET_GRAPHID(PG_GETARG_DATUM(a))
#define AG_RETURN_GRAPHID(x) return GRAPHID_GET_DATUM(x)

/* Oid accessors for GRAPHID */
#define GRAPHIDOID get_GRAPHIDOID()
#define GRAPHIDARRAYOID get_GRAPHIDARRAYOID()

#define GET_LABEL_ID(id) \
       (((uint64)id) >> ENTRY_ID_BITS)

graphid make_graphid(const int32 label_id, const int64 entry_id);
int32 get_graphid_label_id(const graphid gid);
int64 get_graphid_entry_id(const graphid gid);
Oid get_GRAPHIDOID(void);
Oid get_GRAPHIDARRAYOID(void);
void clear_global_Oids_GRAPHID(void);

#endif
