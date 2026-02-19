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

#include "utils/agtype_raw.h"

/*
 * Used for building an agtype container.
 */
struct agtype_build_state
{
    int a_offset;        /* next location to write agtentry */
    int i;               /* index of current agtentry being processed */
    int d_start;         /* start of variable-length portion */
    StringInfo buffer;
};

/*
 * Define the type and size of the agt_header.
 * Copied from agtype_ext.c.
 */
#define AGT_HEADER_TYPE uint32
#define AGT_HEADER_SIZE sizeof(AGT_HEADER_TYPE)

/*
 * Following macros are usable in the context where
 * agtype_build_state is available
 */
#define BUFFER_RESERVE(size) reserve_from_buffer(bstate->buffer, (size))
#define BUFFER_WRITE_PAD() pad_buffer_to_int(bstate->buffer)
#define BUFFER_WRITE_CONST(offset, type, val) *((type *)(bstate->buffer->data + (offset))) = (val)
#define BUFFER_WRITE_PTR(offset, ptr, len) memcpy(bstate->buffer->data + offset, ptr, len)

static int write_pointer(agtype_build_state *bstate, char *ptr, int len);
static void write_agtentry(agtype_build_state *bstate, agtentry agte);

/*
 * Same as `write_ptr` except the content comes from
 * constant value instead of pointer.
 */
#define write_const(val, type) \
    do \
    { \
        int len = sizeof(type); \
        int offset = BUFFER_RESERVE(len); \
        BUFFER_WRITE_CONST(offset, type, val); \
    } \
    while (0)
#define write_ptr(ptr, len) write_pointer(bstate, ptr, len)
#define write_agt(agte) write_agtentry(bstate, agte)

/*
 * Copies the content of `ptr` to the tail of the buffer (variable-length
 * portion).
 */
static int write_pointer(agtype_build_state *bstate, char *ptr, int len)
{
    int offset = BUFFER_RESERVE(len);
    BUFFER_WRITE_PTR(offset, ptr, len);
    return len;
}

/*
 * Copies the content of `agte` to the next available location in the agtentry
 * portion of the buffer. That location is pointed by `bstate->a_offset`.
 *
 * This function must be called after data is written to the variable-length
 * portion.
 */
static void write_agtentry(agtype_build_state *bstate, agtentry agte)
{
    int totallen = bstate->buffer->len - bstate->d_start;

    /*
     * Bail out if total variable-length data exceeds what will fit in a
     * agtentry length field.  We check this in each iteration, not just
     * once at the end, to forestall possible integer overflow.
     */
    if (totallen > AGTENTRY_OFFLENMASK)
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                "total size of agtype array elements exceeds the maximum of %u bytes",
                AGTENTRY_OFFLENMASK)));
    }

    if (((bstate->i) % AGT_OFFSET_STRIDE) == 0)
    {
        agte = (agte & AGTENTRY_TYPEMASK) | totallen | AGTENTRY_HAS_OFF;
    }

    BUFFER_WRITE_CONST(bstate->a_offset, agtentry, agte);
    bstate->a_offset += sizeof(agtentry);
}

/*
 * `header_flag` = a valid agtype_container header field
 * `size` = size of container (number of pairs or elements)
 */
agtype_build_state *init_agtype_build_state(uint32 size, uint32 header_flag)
{
    int agtentry_count;
    int agtentry_len;
    agtype_build_state *bstate;

    bstate = palloc0(sizeof(agtype_build_state));
    bstate->buffer = makeStringInfo();
    bstate->a_offset = 0;
    bstate->i = 0;

    /* reserve for varlen header */
    BUFFER_RESERVE(VARHDRSZ);
    bstate->a_offset += VARHDRSZ;

    /* write container header */
    BUFFER_RESERVE(sizeof(uint32));
    BUFFER_WRITE_CONST(bstate->a_offset, uint32, header_flag | size);
    bstate->a_offset += sizeof(uint32);

    /* reserve for agtentry headers */
    if ((header_flag & AGT_FOBJECT) != 0)
    {
        agtentry_count = size * 2;
    }
    else if ((header_flag & AGT_FARRAY) != 0)
    {
        agtentry_count = size;
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Invalid container type.")));
    }

    agtentry_len = sizeof(agtentry) * agtentry_count;
    BUFFER_RESERVE(agtentry_len);
    bstate->d_start = bstate->a_offset + agtentry_len;

    return bstate;
}

agtype *build_agtype(agtype_build_state *bstate)
{
    agtype *result = (agtype *) bstate->buffer->data;
    SET_VARSIZE(result, bstate->buffer->len);
    return result;
}

void pfree_agtype_build_state(agtype_build_state *bstate)
{
    /*
     * bstate->buffer->data is not pfree'd because this pointer
     * is returned by the `build_agtype` function.
     */
    pfree_if_not_null(bstate->buffer);
    pfree_if_not_null(bstate);
}

void write_string(agtype_build_state *bstate, char *str)
{
    int length = strlen(str);

    write_ptr(str, length);
    write_agt(AGTENTRY_IS_STRING | length);

    bstate->i++;
}

void write_graphid(agtype_build_state *bstate, graphid graphid)
{
    int length = 0;

    /* padding */
    length += BUFFER_WRITE_PAD();

    /* graphid header */
    write_const(AGT_HEADER_INTEGER, AGT_HEADER_TYPE);
    length += AGT_HEADER_SIZE;

    /* graphid value */
    write_const(graphid, int64);
    length += sizeof(int64);

    /* agtentry */
    write_agt(AGTENTRY_IS_AGTYPE | length);

    bstate->i++;
}

void write_container(agtype_build_state *bstate, agtype *agtype)
{
    int length = 0;

    /* padding */
    length += BUFFER_WRITE_PAD();

    /* varlen data */
    length += write_ptr((char *) &agtype->root, VARSIZE(agtype));

    /* agtentry */
    write_agt(AGTENTRY_IS_CONTAINER | length);

    bstate->i++;
}

/*
 * `val` = container of the extended type
 * `header` = AGT_HEADER_VERTEX, AGT_HEADER_EDGE or AGT_HEADER_PATH
 */
void write_extended(agtype_build_state *bstate, agtype *val, uint32 header)
{
    int length = 0;

    /* padding */
    length += BUFFER_WRITE_PAD();

    /* vertex header */
    write_const(header, AGT_HEADER_TYPE);
    length += AGT_HEADER_SIZE;

    /* vertex data */
    length += write_ptr((char *) &val->root, VARSIZE(val));

    /* agtentry */
    write_agt(AGTENTRY_IS_AGTYPE | length);

    bstate->i++;
}
