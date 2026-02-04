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

#include "postgres.h"

#include "utils/builtins.h"
#include "utils/sortsupport.h"

#include "utils/graphid.h"

static int graphid_btree_fast_cmp(Datum x, Datum y, SortSupport ssup);

/* global storage of  OID for graphid and _graphid */
static Oid g_GRAPHIDOID = InvalidOid;
static Oid g_GRAPHIDARRAYOID = InvalidOid;

/* helper function to quickly set, if necessary, and retrieve GRAPHIDOID */
Oid get_GRAPHIDOID(void)
{
    if (g_GRAPHIDOID == InvalidOid)
    {
        g_GRAPHIDOID = GetSysCacheOid2(TYPENAMENSP, Anum_pg_type_oid,
                                       CStringGetDatum("graphid"),
                                       ObjectIdGetDatum(ag_catalog_namespace_id()));
    }

    return g_GRAPHIDOID;
}

/* helper function to quickly set, if necessary, and retrieve GRAPHIDARRAYOID */
Oid get_GRAPHIDARRAYOID(void)
{
    if (g_GRAPHIDARRAYOID == InvalidOid)
    {
        g_GRAPHIDARRAYOID = GetSysCacheOid2(TYPENAMENSP, Anum_pg_type_oid,
                                            CStringGetDatum("_graphid"),
                                            ObjectIdGetDatum(ag_catalog_namespace_id()));
    }

    return g_GRAPHIDARRAYOID;
}

/* helper function to clear the GRAPHOIDs after a drop extension */
void clear_global_Oids_GRAPHID(void)
{
    g_GRAPHIDOID = InvalidOid;
    g_GRAPHIDARRAYOID = InvalidOid;
}

PG_FUNCTION_INFO_V1(graphid_in);

/* graphid type input function */
Datum graphid_in(PG_FUNCTION_ARGS)
{
    char *str = PG_GETARG_CSTRING(0);
    char *endptr;
    int64 i;

    errno = 0;
    i = strtol(str, &endptr, 10);
    if (errno != 0 || endptr == str || *endptr != '\0')
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("invalid value for type graphid: \"%s\"", str)));
    }

    AG_RETURN_GRAPHID(i);
}

PG_FUNCTION_INFO_V1(graphid_out);

/* graphid type output function */
Datum graphid_out(PG_FUNCTION_ARGS)
{
    graphid gid = AG_GETARG_GRAPHID(0);
    char buf[32]; /* greater than MAXINT8LEN+1 */
    char *out;

    pg_lltoa(gid, buf);
    out = pstrdup(buf);

    PG_RETURN_CSTRING(out);
}

PG_FUNCTION_INFO_V1(graphid_eq);

Datum graphid_eq(PG_FUNCTION_ARGS)
{
    graphid lgid = AG_GETARG_GRAPHID(0);
    graphid rgid = AG_GETARG_GRAPHID(1);

    PG_RETURN_BOOL(lgid == rgid);
}

PG_FUNCTION_INFO_V1(graphid_ne);

Datum graphid_ne(PG_FUNCTION_ARGS)
{
    graphid lgid = AG_GETARG_GRAPHID(0);
    graphid rgid = AG_GETARG_GRAPHID(1);

    PG_RETURN_BOOL(lgid != rgid);
}

PG_FUNCTION_INFO_V1(graphid_lt);

Datum graphid_lt(PG_FUNCTION_ARGS)
{
    graphid lgid = AG_GETARG_GRAPHID(0);
    graphid rgid = AG_GETARG_GRAPHID(1);

    PG_RETURN_BOOL(lgid < rgid);
}

PG_FUNCTION_INFO_V1(graphid_gt);

Datum graphid_gt(PG_FUNCTION_ARGS)
{
    graphid lgid = AG_GETARG_GRAPHID(0);
    graphid rgid = AG_GETARG_GRAPHID(1);

    PG_RETURN_BOOL(lgid > rgid);
}

PG_FUNCTION_INFO_V1(graphid_le);

Datum graphid_le(PG_FUNCTION_ARGS)
{
    graphid lgid = AG_GETARG_GRAPHID(0);
    graphid rgid = AG_GETARG_GRAPHID(1);

    PG_RETURN_BOOL(lgid <= rgid);
}

PG_FUNCTION_INFO_V1(graphid_ge);

Datum graphid_ge(PG_FUNCTION_ARGS)
{
    graphid lgid = AG_GETARG_GRAPHID(0);
    graphid rgid = AG_GETARG_GRAPHID(1);

    PG_RETURN_BOOL(lgid >= rgid);
}

PG_FUNCTION_INFO_V1(graphid_btree_cmp);

Datum graphid_btree_cmp(PG_FUNCTION_ARGS)
{
    graphid lgid = AG_GETARG_GRAPHID(0);
    graphid rgid = AG_GETARG_GRAPHID(1);

    if (lgid > rgid)
        PG_RETURN_INT32(1);
    else if (lgid == rgid)
        PG_RETURN_INT32(0);
    else
        PG_RETURN_INT32(-1);
}

PG_FUNCTION_INFO_V1(graphid_btree_sort);

Datum graphid_btree_sort(PG_FUNCTION_ARGS)
{
    SortSupport ssup = (SortSupport)PG_GETARG_POINTER(0);

    ssup->comparator = graphid_btree_fast_cmp;
    PG_RETURN_VOID();
}

static int graphid_btree_fast_cmp(Datum x, Datum y, SortSupport ssup)
{
    graphid lgid = DATUM_GET_GRAPHID(x);
    graphid rgid = DATUM_GET_GRAPHID(y);

    if (lgid > rgid)
        return 1;
    else if (lgid == rgid)
        return 0;
    else
        return -1;
}

graphid make_graphid(const int32 label_id, const int64 entry_id)
{
    uint64 tmp;

    if (!label_id_is_valid(label_id))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("label_id must be %d .. %d",
                               LABEL_ID_MIN, LABEL_ID_MAX)));
    }
    if (!entry_id_is_valid(entry_id))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("entry_id must be " INT64_FORMAT " .. " INT64_FORMAT,
                        ENTRY_ID_MIN, ENTRY_ID_MAX)));
    }

    tmp = (((uint64)label_id) << ENTRY_ID_BITS) |
          (((uint64)entry_id) & ENTRY_ID_MASK);

    return (graphid)tmp;
}

int32 get_graphid_label_id(const graphid gid)
{
    return (int32)(((uint64)gid) >> ENTRY_ID_BITS);
}

int64 get_graphid_entry_id(const graphid gid)
{
    return (int64)(((uint64)gid) & ENTRY_ID_MASK);
}

PG_FUNCTION_INFO_V1(_graphid);

Datum _graphid(PG_FUNCTION_ARGS)
{
    int32 label_id;
    int64 entry_id;
    graphid gid;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
    {
        ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                        errmsg("label_id and entry_id must not be null")));
    }
    label_id = PG_GETARG_INT32(0);
    entry_id = PG_GETARG_INT64(1);

    gid = make_graphid(label_id, entry_id);

    AG_RETURN_GRAPHID(gid);
}

/* Hashing Function for Hash Indexes */
PG_FUNCTION_INFO_V1(graphid_hash_cmp);

Datum graphid_hash_cmp(PG_FUNCTION_ARGS)
{
    graphid l = AG_GETARG_GRAPHID(0);
    int hash = (int) ((l >> 32) ^ l);/* ^ seed; */

    PG_RETURN_INT32(hash);
}
