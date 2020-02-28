/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "postgres.h"

#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/sortsupport.h"

#include "utils/graphid.h"

static int graphid_btree_fast_cmp(Datum x, Datum y, SortSupport ssup);

PG_FUNCTION_INFO_V1(graphid_in);

// graphid type input function
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

// graphid type output function
Datum graphid_out(PG_FUNCTION_ARGS)
{
    const int GRAPHID_LEN = 20; // -9223372036854775808
    graphid gid = AG_GETARG_GRAPHID(0);
    char buf[GRAPHID_LEN + 1];
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
