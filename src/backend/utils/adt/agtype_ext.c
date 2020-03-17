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

#include "utils/agtype_ext.h"
#include "utils/agtype.h"
#include "utils/graphid.h"

/* define the type and size of the agt_header */
#define AGT_HEADER_TYPE uint32
#define AGT_HEADER_SIZE sizeof(AGT_HEADER_TYPE)

/* values for the AGTYPE header field to denote the stored data type */
#define AGT_HEADER_INTEGER 0x00000000
#define AGT_HEADER_FLOAT 0x00000001
#define AGT_HEADER_VERTEX 0x00000002

static void ag_deserialize_vertex(char *base_addr, agtype_value *result);

static short ag_serialize_header(StringInfo buffer, uint32 type)
{
    short padlen;
    int offset;

    padlen = pad_buffer_to_int(buffer);
    offset = reserve_from_buffer(buffer, AGT_HEADER_SIZE);
    *((AGT_HEADER_TYPE *)(buffer->data + offset)) = type;

    return padlen;
}

/*
 * Function serializes the data into the buffer provided.
 * Returns false if the type is not defined. Otherwise, true.
 */
bool ag_serialize_extended_type(StringInfo buffer, agtentry *agtentry,
                                agtype_value *scalar_val)
{
    short padlen;
    int numlen;
    int offset;

    switch (scalar_val->type)
    {
    case AGTV_INTEGER:
        padlen = ag_serialize_header(buffer, AGT_HEADER_INTEGER);

        /* copy in the int_value data */
        numlen = sizeof(int64);
        offset = reserve_from_buffer(buffer, numlen);
        *((int64 *)(buffer->data + offset)) = scalar_val->val.int_value;

        *agtentry = AGTENTRY_IS_AGTYPE | (padlen + numlen + AGT_HEADER_SIZE);
        break;

    case AGTV_FLOAT:
        padlen = ag_serialize_header(buffer, AGT_HEADER_FLOAT);

        /* copy in the float_value data */
        numlen = sizeof(scalar_val->val.float_value);
        offset = reserve_from_buffer(buffer, numlen);
        *((float8 *)(buffer->data + offset)) = scalar_val->val.float_value;

        *agtentry = AGTENTRY_IS_AGTYPE | (padlen + numlen + AGT_HEADER_SIZE);
        break;

    case AGTV_VERTEX:
    {
        uint32 object_ae = 0;
        padlen = ag_serialize_header(buffer, AGT_HEADER_VERTEX);
        convert_vertex_object(buffer, &object_ae, scalar_val);

        *agtentry = AGTENTRY_IS_AGTYPE |
                    ((AGTENTRY_OFFLENMASK & (int)object_ae) + AGT_HEADER_SIZE);
        break;
    }
    default:
        return false;
    }
    return true;
}

/*
 * Function deserializes the data from the buffer pointed to by base_addr.
 * NOTE: This function writes to the error log and exits for any UNKNOWN
 * AGT_HEADER type.
 */
void ag_deserialize_extended_type(char *base_addr, uint32 offset,
                                  agtype_value *result)
{
    char *base = base_addr + INTALIGN(offset);
    AGT_HEADER_TYPE agt_header = *((AGT_HEADER_TYPE *)base);

    switch (agt_header)
    {
    case AGT_HEADER_INTEGER:
        result->type = AGTV_INTEGER;
        result->val.int_value = *((int64 *)(base + AGT_HEADER_SIZE));
        break;

    case AGT_HEADER_FLOAT:
        result->type = AGTV_FLOAT;
        result->val.float_value = *((float8 *)(base + AGT_HEADER_SIZE));
        break;

    case AGT_HEADER_VERTEX:
        ag_deserialize_vertex(base, result);
        break;
    default:
        elog(ERROR, "Invalid AGT header value.");
    }
}

static void ag_deserialize_vertex(char *base, agtype_value *result)
{
    agtype_iterator *it;
    agtype_iterator_token tok;
    agtype_parse_state *parse_state = NULL;
    agtype_value *r;
    agtype_value *parsed_agtype_value = NULL;
    //offset container by the extended type header
    char *container_base = base + AGT_HEADER_SIZE;

    r = palloc(sizeof(agtype_value));

    it = agtype_iterator_init((agtype_container *)container_base);
    while ((tok = agtype_iterator_next(&it, r, true)) != WAGT_DONE)
    {
        parsed_agtype_value = push_agtype_value(
            &parse_state, tok, tok < WAGT_BEGIN_ARRAY ? r : NULL);

    }

    result->type = AGTV_VERTEX;
    result->val = parsed_agtype_value->val;
}
